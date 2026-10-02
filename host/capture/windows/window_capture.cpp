#include <algorithm>
#include <atomic>
#include <cloudplay/capture/window_capture.hpp>
#include <d3d11.h>
#include <dxgi1_6.h>
#include <thread>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <winrt/Windows.Graphics.DirectX.h>

namespace cloudplay::capture {
namespace {
using namespace winrt::Windows::Graphics::Capture;
using namespace winrt::Windows::Graphics::DirectX;
using winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice;

struct FrameCloser {
    Direct3D11CaptureFrame frame{nullptr};
    ~FrameCloser() { close(); }
    void close() noexcept {
        if (frame) {
            try {
                frame.Close();
            } catch (...) {
            }
            frame = nullptr;
        }
    }
};

winrt::com_ptr<ID3D11Device> create_device() {
    winrt::com_ptr<IDXGIFactory6> factory;
    winrt::check_hresult(CreateDXGIFactory1(__uuidof(IDXGIFactory6), factory.put_void()));
    winrt::com_ptr<IDXGIAdapter1> adapter;
    for (UINT index = 0;; ++index) {
        const auto result =
            factory->EnumAdapterByGpuPreference(index, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                                                __uuidof(IDXGIAdapter1), adapter.put_void());
        if (result == DXGI_ERROR_NOT_FOUND)
            throw CaptureError(CaptureFailure::Unsupported, result);
        winrt::check_hresult(result);
        DXGI_ADAPTER_DESC1 desc{};
        winrt::check_hresult(adapter->GetDesc1(&desc));
        if ((desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0)
            break;
        adapter = nullptr;
    }
    const D3D_FEATURE_LEVEL levels[]{D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
    winrt::com_ptr<ID3D11Device> device;
    winrt::check_hresult(D3D11CreateDevice(adapter.get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
                                           D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, 2,
                                           D3D11_SDK_VERSION, device.put(), nullptr, nullptr));
    return device;
}
} // namespace

struct WindowCapture::Impl {
    const std::thread::id owner{std::this_thread::get_id()};
    CaptureState state{CaptureState::Idle};
    CaptureStats stats;
    HWND window{};
    FrameSize size{};
    LARGE_INTEGER frequency{};
    std::shared_ptr<std::atomic<bool>> target_closed;
    winrt::com_ptr<ID3D11Device> device;
    IDirect3DDevice runtime_device{nullptr};
    GraphicsCaptureItem item{nullptr};
    GraphicsCaptureItem::Closed_revoker closed_event;
    Direct3D11CaptureFramePool pool{nullptr};
    GraphicsCaptureSession session{nullptr};

    void check_owner() const {
        if (owner != std::this_thread::get_id())
            throw std::logic_error("Capture requires its owner thread");
    }

    void close() noexcept {
        // Revoke first; the callback holds only a weak atomic notification, never this object.
        closed_event.revoke();
        if (session) {
            try {
                session.Close();
            } catch (...) {
            }
        }
        if (pool) {
            try {
                pool.Close();
            } catch (...) {
            }
        }
        session = nullptr;
        pool = nullptr;
        item = nullptr;
        runtime_device = nullptr;
        device = nullptr;
        target_closed.reset();
        window = nullptr;
    }

    void fail(CaptureFailure failure, HRESULT result) noexcept {
        close();
        stats.failure = failure;
        stats.result = result;
        state = CaptureState::Failed;
    }
};

WindowCapture::WindowCapture() : impl_(std::make_unique<Impl>()) {}
WindowCapture::~WindowCapture() { impl_->close(); }

void WindowCapture::start(HWND window) {
    auto &self = *impl_;
    self.check_owner();
    if (self.state != CaptureState::Idle && self.state != CaptureState::Stopped &&
        self.state != CaptureState::Failed)
        throw std::logic_error("Capture is already active");
    self.close();
    self.stats = {};
    self.state = CaptureState::Starting;
    try {
        if (!window || !IsWindow(window))
            throw CaptureError(CaptureFailure::InvalidTarget, E_INVALIDARG);
        if (!GraphicsCaptureSession::IsSupported())
            throw CaptureError(CaptureFailure::Unsupported, E_NOTIMPL);
        if (!QueryPerformanceFrequency(&self.frequency))
            winrt::throw_last_error();
        self.window = window;
        self.device = create_device();
        auto dxgi_device = self.device.as<IDXGIDevice>();
        winrt::com_ptr<IInspectable> inspectable;
        winrt::check_hresult(
            CreateDirect3D11DeviceFromDXGIDevice(dxgi_device.get(), inspectable.put()));
        self.runtime_device = inspectable.as<IDirect3DDevice>();

        auto interop =
            winrt::get_activation_factory<GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
        winrt::check_hresult(interop->CreateForWindow(window, winrt::guid_of<GraphicsCaptureItem>(),
                                                      winrt::put_abi(self.item)));
        const auto initial_size = self.item.Size();
        self.size = {initial_size.Width, initial_size.Height};
        if (self.size.width <= 0 || self.size.height <= 0)
            throw CaptureError(CaptureFailure::InvalidTarget, E_INVALIDARG);

        self.target_closed = std::make_shared<std::atomic<bool>>(false);
        const std::weak_ptr<std::atomic<bool>> signal = self.target_closed;
        self.closed_event =
            self.item.Closed(winrt::auto_revoke, [signal](auto &&, auto &&) noexcept {
                if (const auto closed = signal.lock())
                    closed->store(true, std::memory_order_release);
            });
        self.pool = Direct3D11CaptureFramePool::CreateFreeThreaded(
            self.runtime_device, DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, initial_size);
        self.session = self.pool.CreateCaptureSession(self.item);
        self.session.StartCapture();
        self.state = CaptureState::Capturing;
    } catch (const CaptureError &error) {
        self.fail(error.reason, error.result);
        throw;
    } catch (const winrt::hresult_error &error) {
        const HRESULT result = error.code();
        const auto failure =
            result == E_ACCESSDENIED ? CaptureFailure::PermissionDenied : CaptureFailure::Platform;
        self.fail(failure, result);
        throw CaptureError(failure, result);
    } catch (...) {
        self.fail(CaptureFailure::Platform, E_FAIL);
        throw;
    }
}

bool WindowCapture::poll(const std::function<void(const FrameView &)> &consumer) {
    auto &self = *impl_;
    self.check_owner();
    if (!consumer)
        throw std::invalid_argument("Capture consumer is required");
    if (self.state != CaptureState::Capturing)
        throw std::logic_error("Capture is not running");
    try {
        if (self.target_closed->load(std::memory_order_acquire) || !IsWindow(self.window))
            throw CaptureError(CaptureFailure::TargetClosed, RO_E_CLOSED);
        const auto device_result = self.device->GetDeviceRemovedReason();
        if (FAILED(device_result))
            throw CaptureError(CaptureFailure::DeviceLost, device_result);

        FrameCloser latest{self.pool.TryGetNextFrame()};
        if (!latest.frame)
            return false;
        ++self.stats.received;
        // Drain at most the two configured buffers. Continuous producers cannot starve the owner.
        if (auto next = self.pool.TryGetNextFrame()) {
            latest.close();
            latest.frame = std::move(next);
            ++self.stats.received;
            ++self.stats.discarded;
        }
        const auto content = latest.frame.ContentSize();
        const FrameSize size{content.Width, content.Height};
        switch (decide_frame(self.size, size)) {
        case FrameDecision::Discard:
            ++self.stats.discarded;
            return false;
        case FrameDecision::RecreatePool:
            // Return every old frame before rebuilding the pool; never deliver clipped/undefined
            // edges.
            latest.close();
            self.pool.Recreate(self.runtime_device, DirectXPixelFormat::B8G8R8A8UIntNormalized, 2,
                               content);
            self.size = size;
            ++self.stats.resizes;
            ++self.stats.discarded;
            return false;
        case FrameDecision::Deliver:
            break;
        }
        auto access =
            latest.frame.Surface()
                .as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
        winrt::com_ptr<ID3D11Texture2D> texture;
        winrt::check_hresult(access->GetInterface(__uuidof(ID3D11Texture2D), texture.put_void()));
        LARGE_INTEGER now{};
        if (!QueryPerformanceCounter(&now))
            winrt::throw_last_error();
        const auto timestamp = latest.frame.SystemRelativeTime().count();
        const double latency = std::max(0.0, static_cast<double>(now.QuadPart) * 1'000'000.0 /
                                                     static_cast<double>(self.frequency.QuadPart) -
                                                 static_cast<double>(timestamp) / 10.0);
        self.state = CaptureState::Delivering;
        try {
            consumer({texture.get(), size, timestamp, latency});
        } catch (...) {
            self.state = CaptureState::Capturing;
            ++self.stats.discarded;
            throw;
        }
        self.state = CaptureState::Capturing;
        ++self.stats.delivered;
        self.stats.last_capture_latency_us = latency;
        return true;
    } catch (const CaptureError &error) {
        self.fail(error.reason, error.result);
        throw;
    } catch (const winrt::hresult_error &error) {
        const HRESULT result = error.code();
        const auto failure =
            result == DXGI_ERROR_DEVICE_REMOVED || result == DXGI_ERROR_DEVICE_RESET
                ? CaptureFailure::DeviceLost
                : CaptureFailure::Platform;
        self.fail(failure, result);
        throw CaptureError(failure, result);
    }
}

void WindowCapture::stop() {
    auto &self = *impl_;
    self.check_owner();
    if (self.state == CaptureState::Delivering)
        throw std::logic_error("Capture consumer cannot reenter stop");
    if (self.state == CaptureState::Stopped)
        return;
    self.state = CaptureState::Stopping;
    self.close();
    self.state = CaptureState::Stopped;
}

CaptureState WindowCapture::state() const {
    impl_->check_owner();
    return impl_->state;
}

CaptureStats WindowCapture::stats() const {
    impl_->check_owner();
    return impl_->stats;
}

} // namespace cloudplay::capture
