#pragma once

#include <array>
#include <cstdint>
#include <linux/input-event-codes.h>
#include <optional>

namespace cloudplay::input {

// USB HID page07 physical usages, not layout-dependent symbols or XKB (+8) codes.
inline std::optional<int> evdev_key(std::uint16_t usage) noexcept {
    static constexpr std::array basic{
        KEY_A,         KEY_B,          KEY_C,          KEY_D,          KEY_E,
        KEY_F,         KEY_G,          KEY_H,          KEY_I,          KEY_J,
        KEY_K,         KEY_L,          KEY_M,          KEY_N,          KEY_O,
        KEY_P,         KEY_Q,          KEY_R,          KEY_S,          KEY_T,
        KEY_U,         KEY_V,          KEY_W,          KEY_X,          KEY_Y,
        KEY_Z,         KEY_1,          KEY_2,          KEY_3,          KEY_4,
        KEY_5,         KEY_6,          KEY_7,          KEY_8,          KEY_9,
        KEY_0,         KEY_ENTER,      KEY_ESC,        KEY_BACKSPACE,  KEY_TAB,
        KEY_SPACE,     KEY_MINUS,      KEY_EQUAL,      KEY_LEFTBRACE,  KEY_RIGHTBRACE,
        KEY_BACKSLASH, KEY_BACKSLASH,  KEY_SEMICOLON,  KEY_APOSTROPHE, KEY_GRAVE,
        KEY_COMMA,     KEY_DOT,        KEY_SLASH,      KEY_CAPSLOCK,   KEY_F1,
        KEY_F2,        KEY_F3,         KEY_F4,         KEY_F5,         KEY_F6,
        KEY_F7,        KEY_F8,         KEY_F9,         KEY_F10,        KEY_F11,
        KEY_F12,       KEY_SYSRQ,      KEY_SCROLLLOCK, KEY_PAUSE,      KEY_INSERT,
        KEY_HOME,      KEY_PAGEUP,     KEY_DELETE,     KEY_END,        KEY_PAGEDOWN,
        KEY_RIGHT,     KEY_LEFT,       KEY_DOWN,       KEY_UP,         KEY_NUMLOCK,
        KEY_KPSLASH,   KEY_KPASTERISK, KEY_KPMINUS,    KEY_KPPLUS,     KEY_KPENTER,
        KEY_KP1,       KEY_KP2,        KEY_KP3,        KEY_KP4,        KEY_KP5,
        KEY_KP6,       KEY_KP7,        KEY_KP8,        KEY_KP9,        KEY_KP0,
        KEY_KPDOT,     KEY_102ND,      KEY_COMPOSE};
    static_assert(basic.size() == 0x65 - 0x04 + 1);
    static constexpr std::array extended{KEY_KPEQUAL, KEY_F13, KEY_F14, KEY_F15, KEY_F16,
                                         KEY_F17,     KEY_F18, KEY_F19, KEY_F20, KEY_F21,
                                         KEY_F22,     KEY_F23, KEY_F24};
    static constexpr std::array modifiers{KEY_LEFTCTRL, KEY_LEFTSHIFT, KEY_LEFTALT,
                                          KEY_LEFTMETA, KEY_RIGHTCTRL, KEY_RIGHTSHIFT,
                                          KEY_RIGHTALT, KEY_RIGHTMETA};
    if (usage >= 0x04 && usage <= 0x65)
        return basic[usage - 0x04];
    if (usage >= 0x67 && usage <= 0x73)
        return extended[usage - 0x67];
    if (usage >= 0xe0 && usage <= 0xe7)
        return modifiers[usage - 0xe0];
    return std::nullopt;
}
inline std::optional<int> evdev_button(std::uint8_t button) noexcept {
    static constexpr std::array buttons{BTN_LEFT, BTN_RIGHT, BTN_MIDDLE, BTN_SIDE, BTN_EXTRA};
    if (button == 0 || button > buttons.size())
        return std::nullopt;
    return buttons[button - 1];
}
} // namespace cloudplay::input
