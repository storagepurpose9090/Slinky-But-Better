#pragma once

#include <atomic>
#include <cstdint>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <jni.h>
#include <jvmti.h>

#include "config/profile_manager.hpp"
#include "events/events.hpp"
#include "events/module_manager.hpp"
#include "gui/gui_renderer.hpp"
#include "hooks/channel_handler.hpp"
#include "minecraft/mappings.hpp"
#include "util/input_win32.hpp"
#include "util/math.hpp"

namespace slinky::core {

// Global client runtime state (matches 0x1803140C0 .. 0x180314508)
struct ClientRuntime {
    HMODULE dll_module = nullptr;                        // lpParameter @ 0x1803140B8
    JavaVM* jvm = nullptr;                               // qword_1803140C0
    jvmtiEnv* jvmti = nullptr;                           // qword_1803140C8
    mc::Mappings* mappings = nullptr;                    // qword_1803140D0
    mc::ClassLoaderHandle child_class_loader{};          // child SecureClassLoader
    math::Vec2f last_player_rotation{};                  // qword_1803140D8
    double last_frame_update_time = 0.0;                 // qword_1803140E0
    events::ModuleManager module_manager{};              // qword_1803140E8 (0x310 bytes)
    uint8_t* key_states = nullptr;                       // qword_1803143F8 (104 bytes)
    HWND game_window = nullptr;                          // qword_180314400
    config::KeybindMap* profile_keybinds = nullptr;      // qword_180314410
    config::KeybindMap* module_keybinds = nullptr;       // qword_180314418
    config::ProfileRegistry* profile_registry = nullptr; // qword_180314420
    hooks::ChannelHandlerHandle channel_handler{};       // xmmword_180314428
    std::atomic_flag packet_spinlock = ATOMIC_FLAG_INIT; // byte_180314438
    bool unloading = false;                              // byte_180314439
    bool prev_in_game_focus = false;                     // byte_18031443A
    events::TargetTracker target_tracker{};              // unk_180314440
    int32_t gui_warmup_frames = 10;                      // dword_180314460
    bool gui_visible = false;                            // byte_180314464
    bool gui_needs_redraw = true;                        // byte_180314465
    gui::GuiRenderer* gui_renderer = nullptr;            // qword_180314468
    uint32_t framebuffer_width = 0;                      // dword_180314470
    uint32_t framebuffer_height = 0;                     // dword_180314474
    float gl_modelview[16]{};                            // unk_180314480
    float gl_projection[16]{};                           // unk_1803144C0
    std::atomic<uint8_t> gl_initialized{0};              // byte_180314500
    double last_gui_poll_time = -1.0;                    // qword_180302000
};

// Singleton runtime accessor
ClientRuntime& runtime() noexcept;

// Acquires/releases the spinlock protecting Netty packet dispatch & module toggle flushes (byte_180314438)
void acquire_packet_lock() noexcept;
void release_packet_lock() noexcept;

// Locates `jvm.dll!JNI_GetCreatedJavaVMs` and retrieves the active JavaVM* (sub_18002CA70)
int find_created_jvm(JavaVM** out_jvm) noexcept;

// Finds the visible top-level HWND belonging to the current process (sub_180028B50)
HWND find_process_main_window() noexcept;

// Initializes JVM, Mappings, Modules, Profiles, Commands, Window Hook, and JVMTI Hooks
int initialize_client(HMODULE dll_module) noexcept;

// One-shot OpenGL + NanoVG + ClickGUI initialization on the render thread (called in getMouseOver, 0x29C4E)
int ensure_render_thread_initialized() noexcept;

// Uninstalls JVMTI hooks, window hook, GUI renderer, mappings, and managers (sub_180028A30)
void shutdown_client_subsystems(JNIEnv* env, bool from_error) noexcept;

// Per-frame aim/interpolation dispatcher called at end of updateDisplay (sub_180028BB0)
void dispatch_frame_update(events::GameContext* ctx, jobject timer_obj, double now_sec) noexcept;

// Queries active rotation spoof state from modules (sub_180028D00)
bool query_rotation_spoof(bool* out_move_fix, bool* out_silent_restore, math::Vec2f* inout_rotation) noexcept;

// Module & profile keybind callbacks wired into ModuleManager, ProfileRegistry, and KeybindMap (0x28D90..0x28F50)
void on_set_module_keybind(events::Module* mod, uint32_t key_code) noexcept;
bool on_trigger_profile_keybind(void* target, uint32_t key_code, bool pressed) noexcept;
bool on_trigger_module_keybind(void* target, uint32_t key_code, bool pressed) noexcept;
void on_set_profile_keybind(config::ProfileEntry* entry, uint32_t key_code) noexcept;

// Win32 WndProc input event callback (sub_18002AD90)
bool on_window_input_event(const input::InputEvent* event) noexcept;

} // namespace slinky::core
