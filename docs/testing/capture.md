# Capture Acceptance on Windows 11

Use a visible user-owned test app such as Notepad. Build in an x64 Native Tools
prompt with a recent Windows SDK, CMake 3.25+, and Ninja:

```powershell
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

In PowerShell, obtain its window handle and run the probe:

```powershell
$targetWindow = Get-Process notepad | Where-Object MainWindowHandle -ne 0 | Select-Object -First 1
.\build\native\host\capture\cloudplay_capture_probe.exe --window $targetWindow.MainWindowHandle --seconds 10
```

The probe checks GPU texture acquisition and prints a JSON summary; it does not
preview/save pixels. Exit codes: 0 = frames delivered, 1 = capture failure,
2 = invalid arguments, 3 = no frames in the measurement interval. Failure integers
follow `CaptureFailure` in `window_capture.hpp`, with a numeric HRESULT.

## Checklist
- [ ] Typing/scrolling yields delivered frames and a visible capture indicator.
- [ ] Resize repeatedly; resize count increases and acquisition resumes.
- [ ] Minimize, restore, move between monitors; no crash and acquisition resumes.
- [ ] Close the target while capturing; failure is reported and resources close.
- [ ] Repeat the probe; no lingering process or capture indicator remains.
- [ ] Invalid/unavailable targets and permission denial produce bounded failures.
- [ ] Record GPU/driver, resolution, refresh rate, duration, counts, and last latency.

The last latency is not a percentile or throughput measurement. CI cannot replace
this checklist. Device-loss tests should use a controlled environment; do not
disrupt the user's GPU driver merely to provoke failure. No hardware results exist yet.
