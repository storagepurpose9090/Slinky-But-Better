#pragma once

#include <cstdint>

namespace slinky::input {

// Unified 16-byte input event passed to Client/ClickGUI input callback - matches MultiByteStr in sub_18006E0E0 & sub_18002AD90
enum class InputEventKind : int32_t {
    RawInput = 0,     // WM_INPUT (0xFF)
    MouseMove = 1,    // x, y in pixels
    KeyOrButton = 2,  // key_code, utf8_char[4], pressed, is_synthetic
    MouseScroll = 3,  // scroll_delta (in wheel notches, e.g. delta / 120.0f)
};

struct InputEvent {
    InputEventKind kind = InputEventKind::RawInput; // +0
    union {
        struct {
            int32_t x; // +4
            int32_t y; // +8
        } move;
        struct {
            int32_t key_code;  // +4 (0 if WM_CHAR text-only event)
            char utf8_text[4]; // +8
        } key;
        struct {
            float delta; // +4
            int32_t pad; // +8
        } scroll;
    };
    uint8_t pressed = 0;      // +12 (1 = down, 0 = up, or 1 = char event)
    uint8_t is_synthetic = 0; // +13 (1 = posted via post_synthetic_mouse_button)
    uint8_t pad[2]{};         // +14..+15
};
static_assert(sizeof(InputEvent) == 16, "InputEvent must be 16 bytes");

using InputEventHandlerFn = bool (*)(const InputEvent* event);

// Installs/removes Win32 WndProc hook on the active Minecraft window - matches sub_18006E4B0 & sub_18006E020
void init_win32_input(InputEventHandlerFn handler) noexcept;
void ensure_wndproc_installed() noexcept;
void shutdown_win32_input() noexcept;

// Posts synthetic mouse button down/up (messages 36083..36086) to the active window - matches sub_18006E620
bool post_synthetic_mouse_button(bool left_button, bool down) noexcept;

// Queries physical async mouse button state (left_button=true -> VK_LBUTTON=1, false -> VK_RBUTTON=2) - matches sub_18006E670
bool is_mouse_button_physically_down(bool left_button) noexcept;

// Pins current thread to lowest active process CPU core - matches sub_18006E6B0
bool pin_thread_to_first_core() noexcept;

// Spawns 1-second delayed FreeLibraryAndExitThread self-unload worker - matches sub_18006DFB0
bool spawn_self_unload_thread(void* module_handle) noexcept;

} // namespace slinky::input
