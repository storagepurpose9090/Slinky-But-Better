#include "util/input_win32.hpp"
#include "util/keycodes.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace slinky::input {

namespace {

constexpr UINT kMsgSyntheticLButtonDown = 36083u;
constexpr UINT kMsgSyntheticLButtonUp   = 36084u;
constexpr UINT kMsgSyntheticRButtonDown = 36085u;
constexpr UINT kMsgSyntheticRButtonUp   = 36086u;

HWND g_hooked_hwnd = nullptr;
WNDPROC g_original_wndproc = nullptr;
InputEventHandlerFn g_input_handler = nullptr;
HCURSOR g_arrow_cursor = nullptr;

decltype(&MapVirtualKeyW) g_map_vk = &MapVirtualKeyW;
decltype(&CallWindowProcW) g_call_wndproc = &CallWindowProcW;
decltype(&SetWindowLongPtrW) g_set_wnd_long = &SetWindowLongPtrW;
decltype(&PostMessageW) g_post_message = &PostMessageW;

// Matches sub_18006E710
int resolved_vk_to_key_code(uint64_t vk) noexcept {
    if (vk - 65ull <= 25ull) return static_cast<int>(vk - 39ull);
    if (vk - 48ull <= 9ull)  return static_cast<int>(vk - 42ull);
    if (vk - 96ull <= 9ull)  return static_cast<int>(vk - 80ull);
    if ((vk & ~0xFull) == 0x70ull) return static_cast<int>(vk - 45ull);

    switch (vk) {
        case VK_BACK:       return KEY_BACKSPACE;
        case VK_TAB:        return KEY_TAB;
        case VK_RETURN:     return KEY_ENTER;
        case VK_CAPITAL:    return KEY_CAPS;
        case VK_ESCAPE:     return KEY_ESC;
        case VK_SPACE:      return KEY_SPACE;
        case VK_PRIOR:      return KEY_PREV;
        case VK_NEXT:       return KEY_NEXT;
        case VK_END:        return KEY_END;
        case VK_HOME:       return KEY_HOME;
        case VK_LEFT:       return KEY_LEFT;
        case VK_UP:         return KEY_UP;
        case VK_RIGHT:      return KEY_RIGHT;
        case VK_DOWN:       return KEY_DOWN;
        case VK_PRINT:
        case VK_SNAPSHOT:   return KEY_PRNTSCR;
        case VK_INSERT:     return KEY_INS;
        case VK_DELETE:     return KEY_DEL;
        case VK_NUMLOCK:    return KEY_NUMLOCK;
        case VK_SCROLL:     return KEY_SCROLL;
        case VK_LSHIFT:     return KEY_LSHIFT;
        case VK_RSHIFT:     return KEY_RSHIFT;
        case VK_LCONTROL:   return KEY_LCONTROL;
        case VK_RCONTROL:   return KEY_RCONTROL;
        case VK_LMENU:      return KEY_LMENU;
        case VK_RMENU:      return KEY_RMENU;
        case VK_OEM_1:      return KEY_COLON;
        case VK_OEM_PLUS:   return KEY_PLUS;
        case VK_OEM_COMMA:  return KEY_COMMA;
        case VK_OEM_MINUS:  return KEY_MINUS;
        case VK_OEM_PERIOD: return KEY_PERIOD;
        case VK_OEM_2:      return KEY_SLASH;
        case VK_OEM_3:
        case VK_OEM_8:      return KEY_GRAVE;
        case VK_OEM_4:      return KEY_LBRACKET;
        case VK_OEM_5:      return KEY_BACKSLASH;
        case VK_OEM_6:      return KEY_RBRACKET;
        case VK_OEM_7:      return KEY_QUOTE;
        default:            return KEY_NONE;
    }
}

// Matches sub_18006E0E0
LRESULT CALLBACK slinky_wndproc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (msg > 511u) {
        switch (msg) {
            case WM_MOUSEMOVE: {
                InputEvent ev{};
                ev.kind = InputEventKind::MouseMove;
                ev.move.x = static_cast<int16_t>(LOWORD(lparam));
                ev.move.y = static_cast<int16_t>(HIWORD(lparam));
                if (g_input_handler != nullptr && g_input_handler(&ev)) {
                    SetCursor(g_arrow_cursor);
                    return 0;
                }
                return g_call_wndproc(g_original_wndproc, hwnd, msg, wparam, lparam);
            }
            case WM_LBUTTONDOWN:
            case WM_LBUTTONUP: {
                InputEvent ev{};
                ev.kind = InputEventKind::KeyOrButton;
                ev.key.key_code = KEY_MOUSE1;
                ev.pressed = (msg == WM_LBUTTONDOWN) ? 1u : 0u;
                if (g_input_handler != nullptr && g_input_handler(&ev)) {
                    return 0;
                }
                return g_call_wndproc(g_original_wndproc, hwnd, msg, wparam, lparam);
            }
            case WM_RBUTTONDOWN:
            case WM_RBUTTONUP: {
                InputEvent ev{};
                ev.kind = InputEventKind::KeyOrButton;
                ev.key.key_code = KEY_MOUSE2;
                ev.pressed = (msg == WM_RBUTTONDOWN) ? 1u : 0u;
                if (g_input_handler != nullptr && g_input_handler(&ev)) {
                    return 0;
                }
                return g_call_wndproc(g_original_wndproc, hwnd, msg, wparam, lparam);
            }
            case WM_MBUTTONDOWN:
            case WM_MBUTTONUP: {
                InputEvent ev{};
                ev.kind = InputEventKind::KeyOrButton;
                ev.key.key_code = KEY_MOUSE3;
                ev.pressed = (msg == WM_MBUTTONDOWN) ? 1u : 0u;
                if (g_input_handler != nullptr && g_input_handler(&ev)) {
                    return 0;
                }
                return g_call_wndproc(g_original_wndproc, hwnd, msg, wparam, lparam);
            }
            case WM_MOUSEWHEEL: {
                InputEvent ev{};
                ev.kind = InputEventKind::MouseScroll;
                ev.scroll.delta = static_cast<float>(GET_WHEEL_DELTA_WPARAM(wparam)) / 120.0f;
                if (g_input_handler != nullptr && g_input_handler(&ev)) {
                    return 0;
                }
                return g_call_wndproc(g_original_wndproc, hwnd, msg, wparam, lparam);
            }
            case WM_XBUTTONDOWN:
            case WM_XBUTTONUP: {
                InputEvent ev{};
                ev.kind = InputEventKind::KeyOrButton;
                ev.key.key_code = (HIWORD(wparam) == XBUTTON1) ? KEY_MOUSE4 : KEY_MOUSE5;
                ev.pressed = (msg == WM_XBUTTONDOWN) ? 1u : 0u;
                if (g_input_handler != nullptr && g_input_handler(&ev)) {
                    return 0;
                }
                return g_call_wndproc(g_original_wndproc, hwnd, msg, wparam, lparam);
            }
            case kMsgSyntheticLButtonDown:
            case kMsgSyntheticLButtonUp: {
                InputEvent ev{};
                ev.kind = InputEventKind::KeyOrButton;
                ev.key.key_code = KEY_MOUSE1;
                ev.pressed = (msg == kMsgSyntheticLButtonDown) ? 1u : 0u;
                ev.is_synthetic = 1u;
                if (g_input_handler != nullptr && g_input_handler(&ev)) {
                    return 0;
                }
                const UINT real_msg = (msg == kMsgSyntheticLButtonDown) ? WM_LBUTTONDOWN : WM_LBUTTONUP;
                return g_call_wndproc(g_original_wndproc, hwnd, real_msg, wparam, lparam);
            }
            case kMsgSyntheticRButtonDown:
            case kMsgSyntheticRButtonUp: {
                InputEvent ev{};
                ev.kind = InputEventKind::KeyOrButton;
                ev.key.key_code = KEY_MOUSE2;
                ev.pressed = (msg == kMsgSyntheticRButtonDown) ? 1u : 0u;
                ev.is_synthetic = 1u;
                if (g_input_handler != nullptr && g_input_handler(&ev)) {
                    return 0;
                }
                const UINT real_msg = (msg == kMsgSyntheticRButtonDown) ? WM_RBUTTONDOWN : WM_RBUTTONUP;
                return g_call_wndproc(g_original_wndproc, hwnd, real_msg, wparam, lparam);
            }
            default:
                return g_call_wndproc(g_original_wndproc, hwnd, msg, wparam, lparam);
        }
    }

    if (msg == WM_KEYDOWN || msg == WM_KEYUP || msg == WM_SYSKEYDOWN || msg == WM_SYSKEYUP) {
        uint64_t vk = static_cast<uint64_t>(wparam);
        const bool extended = (lparam & (1ll << 24)) != 0;
        if (vk == VK_MENU) {
            vk = extended ? VK_RMENU : VK_LMENU;
        } else if (vk == VK_CONTROL) {
            vk = extended ? VK_RCONTROL : VK_LCONTROL;
        } else if (vk == VK_SHIFT) {
            const UINT scancode = static_cast<UINT>((lparam >> 16) & 0xFF);
            vk = g_map_vk(scancode, MAPVK_VSC_TO_VK_EX);
        }
        const int key_code = resolved_vk_to_key_code(vk);
        if (key_code != KEY_NONE) {
            InputEvent ev{};
            ev.kind = InputEventKind::KeyOrButton;
            ev.key.key_code = key_code;
            ev.pressed = ((msg & ~4u) == WM_KEYDOWN) ? 1u : 0u;
            if (g_input_handler != nullptr && g_input_handler(&ev)) {
                return 0;
            }
        }
        return g_call_wndproc(g_original_wndproc, hwnd, msg, wparam, lparam);
    }

    if (msg == WM_INPUT) {
        InputEvent ev{};
        ev.kind = InputEventKind::RawInput;
        if (g_input_handler != nullptr && g_input_handler(&ev)) {
            return 0;
        }
        return g_call_wndproc(g_original_wndproc, hwnd, msg, wparam, lparam);
    }

    if (msg == WM_CHAR) {
        WCHAR wch = static_cast<WCHAR>(wparam);
        InputEvent ev{};
        ev.kind = InputEventKind::KeyOrButton;
        ev.key.key_code = 0;
        ev.pressed = 1u;
        if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, &wch, 1, ev.key.utf8_text, 4, nullptr, nullptr) > 0) {
            if (g_input_handler != nullptr && g_input_handler(&ev)) {
                return 0;
            }
        }
        return g_call_wndproc(g_original_wndproc, hwnd, msg, wparam, lparam);
    }

    if (msg == WM_DESTROY) {
        g_original_wndproc = nullptr;
        g_hooked_hwnd = nullptr;
    }
    return g_call_wndproc(g_original_wndproc, hwnd, msg, wparam, lparam);
}

DWORD WINAPI self_unload_worker(LPVOID param) {
    Sleep(1000u);
    FreeLibraryAndExitThread(static_cast<HMODULE>(param), 0);
}

} // namespace

void init_win32_input(InputEventHandlerFn handler) noexcept {
    g_input_handler = handler;
    g_arrow_cursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    ensure_wndproc_installed();
}

void ensure_wndproc_installed() noexcept {
    if (g_input_handler == nullptr || g_original_wndproc != nullptr) {
        return;
    }
    HWND active = GetActiveWindow();
    if (active == nullptr) {
        return;
    }
    const bool is_unicode = IsWindowUnicode(active) != FALSE;
    g_map_vk = is_unicode ? &MapVirtualKeyW : &MapVirtualKeyA;
    g_call_wndproc = is_unicode ? &CallWindowProcW : &CallWindowProcA;
    g_set_wnd_long = is_unicode ? &SetWindowLongPtrW : &SetWindowLongPtrA;
    g_post_message = is_unicode ? &PostMessageW : &PostMessageA;

    g_hooked_hwnd = active;
    g_original_wndproc = reinterpret_cast<WNDPROC>(
        g_set_wnd_long(active, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&slinky_wndproc)));
}

void shutdown_win32_input() noexcept {
    if (g_original_wndproc != nullptr && g_hooked_hwnd != nullptr && IsWindow(g_hooked_hwnd)) {
        g_set_wnd_long(g_hooked_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(g_original_wndproc));
    }
    g_original_wndproc = nullptr;
    g_hooked_hwnd = nullptr;
    g_input_handler = nullptr;
}

bool post_synthetic_mouse_button(bool left_button, bool down) noexcept {
    if (g_post_message == nullptr) {
        return false;
    }
    const UINT msg = static_cast<UINT>(2u * (left_button ? 0u : 1u) + 36084u - (down ? 1u : 0u));
    HWND active = GetActiveWindow();
    if (active == nullptr) {
        return false;
    }
    return g_post_message(active, msg, 0, 0) != FALSE;
}

bool is_mouse_button_physically_down(bool left_button) noexcept {
    const int vk = left_button ? VK_LBUTTON : VK_RBUTTON;
    return (static_cast<uint16_t>(GetAsyncKeyState(vk)) >> 15) != 0;
}

bool pin_thread_to_first_core() noexcept {
    ULONG_PTR proc_mask = 0;
    ULONG_PTR sys_mask = 0;
    if (!GetProcessAffinityMask(GetCurrentProcess(), &proc_mask, &sys_mask)) {
        return false;
    }
    const ULONG_PTR lowest_core = proc_mask & (0ull - proc_mask);
    return SetThreadAffinityMask(GetCurrentThread(), lowest_core) != 0;
}

bool spawn_self_unload_thread(void* module_handle) noexcept {
    HANDLE thread = CreateThread(nullptr, 0, &self_unload_worker, module_handle, 0, nullptr);
    if (thread == nullptr) {
        return false;
    }
    return CloseHandle(thread) != FALSE;
}

} // namespace slinky::input
