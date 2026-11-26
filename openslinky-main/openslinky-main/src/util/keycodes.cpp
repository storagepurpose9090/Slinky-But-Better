#include "util/keycodes.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace slinky::input {

namespace {

// Exact 104-entry key name table (0..103) from dword_18015AB58
constexpr const char* kKeyNames[KEY_COUNT] = {
    "NONE",      "MOUSE1",    "MOUSE2",   "MOUSE3",   "MOUSE4",   "MOUSE5",
    "0",         "1",         "2",        "3",        "4",        "5",
    "6",         "7",         "8",        "9",        "NUMPAD0",  "NUMPAD1",
    "NUMPAD2",   "NUMPAD3",   "NUMPAD4",  "NUMPAD5",  "NUMPAD6",  "NUMPAD7",
    "NUMPAD8",   "NUMPAD9",   "A",        "B",        "C",        "D",
    "E",         "F",         "G",        "H",        "I",        "J",
    "K",         "L",         "M",        "N",        "O",        "P",
    "Q",         "R",         "S",        "T",        "U",        "V",
    "W",         "X",         "Y",        "Z",        "COLON",    "PLUS",
    "COMMA",     "MINUS",     "PERIOD",   "SLASH",    "GRAVE",    "LBRACKET",
    "BACKSLASH", "RBRACKET",  "QUOTE",    "LEFT",     "UP",       "RIGHT",
    "DOWN",      "F1",        "F2",       "F3",       "F4",       "F5",
    "F6",        "F7",        "F8",       "F9",       "F10",      "F11",
    "F12",       "F13",       "F14",      "F15",      "F16",      "BACKSPACE",
    "TAB",       "ENTER",     "CAPS",     "ESC",      "SPACE",    "PREV",
    "NEXT",      "END",       "HOME",     "PRNTSCR",  "INS",      "DEL",
    "NUMLOCK",   "SCROLL",    "LSHIFT",   "RSHIFT",   "LCONTROL", "RCONTROL",
    "LMENU",     "RMENU",
};

} // namespace

GlfwFunctions g_glfw{};

const char* get_key_name(int key_code) noexcept {
    const unsigned int idx = static_cast<unsigned int>(key_code - 1);
    if (idx > 0x66u) {
        return "NONE";
    }
    return kKeyNames[key_code];
}

char key_code_to_ascii(int key_code) noexcept {
    if (static_cast<unsigned int>(key_code - KEY_A) <= 25u) {
        return static_cast<char>(key_code + 71); // 'a'..'z'
    }
    if (static_cast<unsigned int>(key_code - KEY_0) <= 9u) {
        return static_cast<char>(key_code + 42); // '0'..'9'
    }
    switch (key_code) {
        case KEY_SPACE:  return ' ';
        case KEY_PERIOD: return '.';
        case KEY_MINUS:  return '-';
        default:         return '\0';
    }
}

int glfw_key_to_key_code(int glfw_key) noexcept {
    switch (glfw_key) {
        case 32:  return KEY_SPACE;
        case 39:  return KEY_QUOTE;
        case 44:  return KEY_COMMA;
        case 45:
        case 333: return KEY_MINUS;
        case 46:
        case 330: return KEY_PERIOD;
        case 47:
        case 331: return KEY_SLASH;
        case 48: case 49: case 50: case 51: case 52:
        case 53: case 54: case 55: case 56: case 57:
            return glfw_key - 42;
        case 59:  return KEY_COLON;
        case 61:
        case 334:
        case 336: return KEY_PLUS;
        case 65: case 66: case 67: case 68: case 69: case 70: case 71:
        case 72: case 73: case 74: case 75: case 76: case 77: case 78:
        case 79: case 80: case 81: case 82: case 83: case 84: case 85:
        case 86: case 87: case 88: case 89: case 90:
            return glfw_key - 39;
        case 91:  return KEY_LBRACKET;
        case 92:  return KEY_BACKSLASH;
        case 93:  return KEY_RBRACKET;
        case 96:  return KEY_GRAVE;
        case 256: return KEY_ESC;
        case 257:
        case 335: return KEY_ENTER;
        case 258: return KEY_TAB;
        case 259: return KEY_BACKSPACE;
        case 260: return KEY_INS;
        case 261: return KEY_DEL;
        case 262: return KEY_RIGHT;
        case 263: return KEY_LEFT;
        case 264: return KEY_DOWN;
        case 265: return KEY_UP;
        case 266: return KEY_PREV;
        case 267: return KEY_NEXT;
        case 268: return KEY_HOME;
        case 269: return KEY_END;
        case 280: return KEY_CAPS;
        case 281: return KEY_SCROLL;
        case 282: return KEY_NUMLOCK;
        case 283: return KEY_PRNTSCR;
        case 290: case 291: case 292: case 293: case 294: case 295: case 296: case 297:
        case 298: case 299: case 300: case 301: case 302: case 303: case 304: case 305:
            return glfw_key - 223;
        case 320: case 321: case 322: case 323: case 324:
        case 325: case 326: case 327: case 328: case 329:
            return glfw_key - 304;
        case 340: return KEY_LSHIFT;
        case 341: return KEY_LCONTROL;
        case 342: return KEY_LMENU;
        case 344: return KEY_RSHIFT;
        case 345: return KEY_RCONTROL;
        case 346: return KEY_RMENU;
        default:  return KEY_NONE;
    }
}

int win32_vk_to_key_code(uint32_t vk, uint32_t lparam) noexcept {
    const bool extended = (lparam & (1u << 24)) != 0;
    const uint32_t scancode = (lparam >> 16) & 0xFFu;
    if (vk == VK_SHIFT) {
        vk = MapVirtualKeyW(scancode, MAPVK_VSC_TO_VK_EX);
    } else if (vk == VK_CONTROL) {
        vk = extended ? VK_RCONTROL : VK_LCONTROL;
    } else if (vk == VK_MENU) {
        vk = extended ? VK_RMENU : VK_LMENU;
    }

    if (vk >= '0' && vk <= '9') {
        return static_cast<int>(vk - '0' + KEY_0);
    }
    if (vk >= 'A' && vk <= 'Z') {
        return static_cast<int>(vk - 'A' + KEY_A);
    }
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) {
        return static_cast<int>(vk - VK_NUMPAD0 + KEY_NUMPAD0);
    }
    if (vk >= VK_F1 && vk <= VK_F16) {
        return static_cast<int>(vk - VK_F1 + KEY_F1);
    }

    switch (vk) {
        case VK_LBUTTON:    return KEY_MOUSE1;
        case VK_RBUTTON:    return KEY_MOUSE2;
        case VK_MBUTTON:    return KEY_MOUSE3;
        case VK_XBUTTON1:   return KEY_MOUSE4;
        case VK_XBUTTON2:   return KEY_MOUSE5;
        case VK_OEM_1:      return KEY_COLON;
        case VK_OEM_PLUS:
        case VK_ADD:        return KEY_PLUS;
        case VK_OEM_COMMA:  return KEY_COMMA;
        case VK_OEM_MINUS:
        case VK_SUBTRACT:   return KEY_MINUS;
        case VK_OEM_PERIOD:
        case VK_DECIMAL:    return KEY_PERIOD;
        case VK_OEM_2:
        case VK_DIVIDE:     return KEY_SLASH;
        case VK_OEM_3:      return KEY_GRAVE;
        case VK_OEM_4:      return KEY_LBRACKET;
        case VK_OEM_5:      return KEY_BACKSLASH;
        case VK_OEM_6:      return KEY_RBRACKET;
        case VK_OEM_7:      return KEY_QUOTE;
        case VK_LEFT:       return KEY_LEFT;
        case VK_UP:         return KEY_UP;
        case VK_RIGHT:      return KEY_RIGHT;
        case VK_DOWN:       return KEY_DOWN;
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
        default:            return KEY_NONE;
    }
}

int load_glfw_functions() noexcept {
    HMODULE mod = GetModuleHandleA("glfw");
    if (mod == nullptr) {
        mod = GetModuleHandleA("glfw3");
    }
    if (mod == nullptr) {
        return 1000;
    }

    const auto get_sym = [mod](const char* name) noexcept -> void* {
        return reinterpret_cast<void*>(GetProcAddress(mod, name));
    };

    g_glfw.get_current_context = reinterpret_cast<decltype(g_glfw.get_current_context)>(
        get_sym("glfwGetCurrentContext"));
    if (!g_glfw.get_current_context) return 1001;

    g_glfw.get_framebuffer_size = reinterpret_cast<decltype(g_glfw.get_framebuffer_size)>(
        get_sym("glfwGetFramebufferSize"));
    if (!g_glfw.get_framebuffer_size) return 1001;

    g_glfw.set_framebuffer_size_callback = reinterpret_cast<decltype(g_glfw.set_framebuffer_size_callback)>(
        get_sym("glfwSetFramebufferSizeCallback"));
    if (!g_glfw.set_framebuffer_size_callback) return 1001;

    g_glfw.set_mouse_button_callback = reinterpret_cast<decltype(g_glfw.set_mouse_button_callback)>(
        get_sym("glfwSetMouseButtonCallback"));
    if (!g_glfw.set_mouse_button_callback) return 1001;

    g_glfw.set_key_callback = reinterpret_cast<decltype(g_glfw.set_key_callback)>(
        get_sym("glfwSetKeyCallback"));
    if (!g_glfw.set_key_callback) return 1001;

    g_glfw.set_char_callback = reinterpret_cast<decltype(g_glfw.set_char_callback)>(
        get_sym("glfwSetCharCallback"));
    if (!g_glfw.set_char_callback) return 1001;

    g_glfw.set_cursor_pos_callback = reinterpret_cast<decltype(g_glfw.set_cursor_pos_callback)>(
        get_sym("glfwSetCursorPosCallback"));
    if (!g_glfw.set_cursor_pos_callback) return 1001;

    g_glfw.set_scroll_callback = reinterpret_cast<decltype(g_glfw.set_scroll_callback)>(
        get_sym("glfwSetScrollCallback"));
    if (!g_glfw.set_scroll_callback) return 1001;

    return 0;
}

} // namespace slinky::input
