#pragma once

#include <cstdint>

namespace slinky::input {

// Slinky unified keycodes (0..103) - matches dword_18015AB58
enum KeyCode : int {
    KEY_NONE = 0,
    KEY_MOUSE1 = 1,
    KEY_MOUSE2 = 2,
    KEY_MOUSE3 = 3,
    KEY_MOUSE4 = 4,
    KEY_MOUSE5 = 5,
    KEY_0 = 6,
    KEY_1 = 7,
    KEY_2 = 8,
    KEY_3 = 9,
    KEY_4 = 10,
    KEY_5 = 11,
    KEY_6 = 12,
    KEY_7 = 13,
    KEY_8 = 14,
    KEY_9 = 15,
    KEY_NUMPAD0 = 16,
    KEY_NUMPAD1 = 17,
    KEY_NUMPAD2 = 18,
    KEY_NUMPAD3 = 19,
    KEY_NUMPAD4 = 20,
    KEY_NUMPAD5 = 21,
    KEY_NUMPAD6 = 22,
    KEY_NUMPAD7 = 23,
    KEY_NUMPAD8 = 24,
    KEY_NUMPAD9 = 25,
    KEY_A = 26,
    KEY_B = 27,
    KEY_C = 28,
    KEY_D = 29,
    KEY_E = 30,
    KEY_F = 31,
    KEY_G = 32,
    KEY_H = 33,
    KEY_I = 34,
    KEY_J = 35,
    KEY_K = 36,
    KEY_L = 37,
    KEY_M = 38,
    KEY_N = 39,
    KEY_O = 40,
    KEY_P = 41,
    KEY_Q = 42,
    KEY_R = 43,
    KEY_S = 44,
    KEY_T = 45,
    KEY_U = 46,
    KEY_V = 47,
    KEY_W = 48,
    KEY_X = 49,
    KEY_Y = 50,
    KEY_Z = 51,
    KEY_COLON = 52,
    KEY_PLUS = 53,
    KEY_COMMA = 54,
    KEY_MINUS = 55,
    KEY_PERIOD = 56,
    KEY_SLASH = 57,
    KEY_GRAVE = 58,
    KEY_LBRACKET = 59,
    KEY_BACKSLASH = 60,
    KEY_RBRACKET = 61,
    KEY_QUOTE = 62,
    KEY_LEFT = 63,
    KEY_UP = 64,
    KEY_RIGHT = 65,
    KEY_DOWN = 66,
    KEY_F1 = 67,
    KEY_F2 = 68,
    KEY_F3 = 69,
    KEY_F4 = 70,
    KEY_F5 = 71,
    KEY_F6 = 72,
    KEY_F7 = 73,
    KEY_F8 = 74,
    KEY_F9 = 75,
    KEY_F10 = 76,
    KEY_F11 = 77,
    KEY_F12 = 78,
    KEY_F13 = 79,
    KEY_F14 = 80,
    KEY_F15 = 81,
    KEY_F16 = 82,
    KEY_BACKSPACE = 83,
    KEY_TAB = 84,
    KEY_ENTER = 85,
    KEY_CAPS = 86,
    KEY_ESC = 87,
    KEY_SPACE = 88,
    KEY_PREV = 89,
    KEY_NEXT = 90,
    KEY_END = 91,
    KEY_HOME = 92,
    KEY_PRNTSCR = 93,
    KEY_INS = 94,
    KEY_DEL = 95,
    KEY_NUMLOCK = 96,
    KEY_SCROLL = 97,
    KEY_LSHIFT = 98,
    KEY_RSHIFT = 99,
    KEY_LCONTROL = 100,
    KEY_RCONTROL = 101,
    KEY_LMENU = 102,
    KEY_RMENU = 103,
    KEY_COUNT = 104,
};

// Returns uppercase display name for Slinky keycode (1..103), or "NONE" - matches sub_18002B060
const char* get_key_name(int key_code) noexcept;

// Maps Slinky keycode to lowercase ASCII search/profile character - matches sub_18002B020
char key_code_to_ascii(int key_code) noexcept;

// Maps GLFW keycode to Slinky keycode - matches sub_180030380
int glfw_key_to_key_code(int glfw_key) noexcept;

// Maps Win32 virtual-key code to Slinky keycode - matches sub_18006E350
int win32_vk_to_key_code(uint32_t vk, uint32_t lparam) noexcept;

// Dynamic GLFW3 bindings for Lunar Client / LWJGL3 compatibility - matches sub_180030250 & qword_180314540..0x314578
struct GLFWwindow;
using GLFWframebuffersizefun = void (*)(GLFWwindow*, int, int);
using GLFWmousebuttonfun = void (*)(GLFWwindow*, int, int, int);
using GLFWkeyfun = void (*)(GLFWwindow*, int, int, int, int);
using GLFWcharfun = void (*)(GLFWwindow*, unsigned int);
using GLFWcursorposfun = void (*)(GLFWwindow*, double, double);
using GLFWscrollfun = void (*)(GLFWwindow*, double, double);

struct GlfwFunctions {
    GLFWwindow* (*get_current_context)() = nullptr;
    void (*get_framebuffer_size)(GLFWwindow*, int*, int*) = nullptr;
    GLFWframebuffersizefun (*set_framebuffer_size_callback)(GLFWwindow*, GLFWframebuffersizefun) = nullptr;
    GLFWmousebuttonfun (*set_mouse_button_callback)(GLFWwindow*, GLFWmousebuttonfun) = nullptr;
    GLFWkeyfun (*set_key_callback)(GLFWwindow*, GLFWkeyfun) = nullptr;
    GLFWcharfun (*set_char_callback)(GLFWwindow*, GLFWcharfun) = nullptr;
    GLFWcursorposfun (*set_cursor_pos_callback)(GLFWwindow*, GLFWcursorposfun) = nullptr;
    GLFWscrollfun (*set_scroll_callback)(GLFWwindow*, GLFWscrollfun) = nullptr;
};

extern GlfwFunctions g_glfw;

// Resolves GLFW exports if glfw.dll is loaded in the process; returns 0 on success, 1000 if not loaded, 1001 on missing symbol
int load_glfw_functions() noexcept;

} // namespace slinky::input
