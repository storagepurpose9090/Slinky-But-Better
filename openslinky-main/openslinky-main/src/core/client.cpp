#include "core/client.hpp"

#include <cstdlib>
#include <cstring>
#include <immintrin.h>

#include "account/alt_manager.hpp"
#include "bytecode/jvmti_installer.hpp"
#include "commands/command_manager.hpp"
#include "hooks/jni_hooks.hpp"
#include "minecraft/game_helpers.hpp"
#include "minecraft/mc_wrappers.hpp"
#include "modules/module_interop.hpp"
#include "modules/module_registry.hpp"
#include "util/containers.hpp"
#include "util/filesystem.hpp"
#include "util/keycodes.hpp"
#include "util/random.hpp"
#include "util/time.hpp"

namespace slinky::core {

namespace {

// EnumWindows callback matching sub_180028B50
BOOL CALLBACK enum_process_window_cb(HWND hwnd, LPARAM lparam) noexcept {
    DWORD window_pid = 0;
    GetWindowThreadProcessId(hwnd, &window_pid);
    if (window_pid == GetCurrentProcessId() && IsWindowVisible(hwnd)) {
        *reinterpret_cast<HWND*>(lparam) = hwnd;
        return FALSE;
    }
    return TRUE;
}

} // namespace

ClientRuntime& runtime() noexcept {
    static ClientRuntime s_runtime{};
    return s_runtime;
}

void acquire_packet_lock() noexcept {
    ClientRuntime& rt = runtime();
    while (rt.packet_spinlock.test_and_set(std::memory_order_acquire)) {
        _mm_pause();
    }
}

void release_packet_lock() noexcept {
    runtime().packet_spinlock.clear(std::memory_order_release);
}

// Matches sub_18002CA70
int find_created_jvm(JavaVM** out_jvm) noexcept {
    HMODULE jvm_mod = GetModuleHandleW(L"jvm.dll");
    if (jvm_mod == nullptr) {
        return 1000;
    }

    using GetCreatedJavaVMsFn = jint (JNICALL*)(JavaVM**, jsize, jsize*);
    auto fn = reinterpret_cast<GetCreatedJavaVMsFn>(
        reinterpret_cast<void*>(GetProcAddress(jvm_mod, "JNI_GetCreatedJavaVMs")));
    if (fn == nullptr) {
        return 1001;
    }

    jsize vm_count = 0;
    if (fn(out_jvm, 1, &vm_count) != JNI_OK || vm_count < 1 || *out_jvm == nullptr) {
        return 2002;
    }
    return 0;
}

// Matches sub_180028B50
HWND find_process_main_window() noexcept {
    HWND found = nullptr;
    EnumWindows(&enum_process_window_cb, reinterpret_cast<LPARAM>(&found));
    return found;
}

int initialize_client(HMODULE dll_module) noexcept {
    ClientRuntime& rt = runtime();
    rt.dll_module = dll_module;
    rt.unloading = false;
    rt.prev_in_game_focus = false;
    rt.gui_warmup_frames = 10;
    rt.gui_visible = false;
    rt.gui_needs_redraw = true;
    rt.last_gui_poll_time = -1.0;

    random::seed_rng();
    time::update_cached_time();

    int err = find_created_jvm(&rt.jvm);
    if (err != 0) {
        return err;
    }

    JNIEnv* env = nullptr;
    if (rt.jvm->AttachCurrentThread(reinterpret_cast<void**>(&env), nullptr) != JNI_OK || env == nullptr) {
        return 2003;
    }

    auto fail_and_detach = [&](int code) noexcept -> int {
        shutdown_client_subsystems(env, true);
        rt.jvm->DetachCurrentThread();
        return code;
    };

    if (rt.jvm->GetEnv(reinterpret_cast<void**>(&rt.jvmti), JVMTI_VERSION_1_2) != JNI_OK &&
        rt.jvm->GetEnv(reinterpret_cast<void**>(&rt.jvmti), JVMTI_VERSION_1_0) != JNI_OK) {
        rt.jvm->DetachCurrentThread();
        return 2004;
    }

    rt.mappings = static_cast<mc::Mappings*>(util::zero_alloc(1, sizeof(mc::Mappings)));
    if (rt.mappings == nullptr) {
        return fail_and_detach(1005);
    }

    err = minecraft::initialize_mappings(rt.mappings, env, &rt.child_class_loader);
    if (err != 0) {
        return fail_and_detach(err);
    }

    account::initialize();
    events::JniContext account_jni{rt.mappings, env};
    account::apply_saved_to_game(&account_jni);

    rt.key_states = static_cast<uint8_t*>(util::zero_alloc(input::KEY_COUNT, sizeof(uint8_t)));
    if (rt.key_states == nullptr) {
        return fail_and_detach(1005);
    }

    rt.profile_keybinds = config::KeybindMap::create(&on_trigger_profile_keybind, nullptr, false);
    rt.module_keybinds = config::KeybindMap::create(&on_trigger_module_keybind, nullptr, true);
    if (rt.profile_keybinds == nullptr || rt.module_keybinds == nullptr) {
        return fail_and_detach(1005);
    }

    rt.profile_registry = config::ProfileRegistry::create(&on_set_profile_keybind);
    if (rt.profile_registry == nullptr) {
        return fail_and_detach(1005);
    }
    rt.profile_registry->refresh_from_disk();

    events::Module* const* registry = modules::get_module_registry();
    err = rt.module_manager.initialize(registry, modules::kModuleRegistryCount, &on_set_module_keybind);
    if (err != 0) {
        return fail_and_detach(err);
    }

    for (size_t i = 0; i < modules::kModuleRegistryCount; ++i) {
        events::Module* mod = registry[i];
        if (mod != nullptr && mod->keybind != 0) {
            rt.module_keybinds->bind(mod->keybind, mod);
        }
    }

    config::capture_all_module_defaults(&rt.module_manager);
    modules::gui_initialize_click_gui();
    config::load_global_settings(&rt.module_manager, rt.profile_registry);

    err = commands::initialize_commands(
        modules::get_friends_command_spec(),
        modules::get_gui_command_spec());
    if (err != 0) {
        return fail_and_detach(err);
    }

    events::JniContext jni{rt.mappings, env};
    err = hooks::define_and_register_channel_handler(
        &jni,
        &rt.child_class_loader,
        &rt.channel_handler,
        reinterpret_cast<void*>(&hooks::on_netty_packet));
    if (err != 0) {
        return fail_and_detach(err);
    }

    bytecode::set_hook_callbacks(hooks::get_jni_hook_callbacks());
    err = bytecode::install_jvmti_hooks(&jni, rt.jvmti);
    if (err != 0) {
        return fail_and_detach(err);
    }

    rt.game_window = find_process_main_window();
    input::init_win32_input(&on_window_input_event);

    rt.jvm->DetachCurrentThread();
    return 0;
}

int ensure_render_thread_initialized() noexcept {
    ClientRuntime& rt = runtime();
    auto* renderer = static_cast<gui::GuiRenderer*>(util::zero_alloc(1, sizeof(gui::GuiRenderer)));
    if (renderer == nullptr) {
        return 1005;
    }

    const int err = renderer->initialize();
    if (err != 0) {
        renderer->shutdown();
        std::free(renderer);
        return err;
    }

    rt.gui_renderer = renderer;
    rt.gui_warmup_frames = 10;
    rt.gui_needs_redraw = true;
    return 0;
}

// Matches sub_180028A30
void shutdown_client_subsystems(JNIEnv* env, bool from_error) noexcept {
    ClientRuntime& rt = runtime();

    if (rt.jvmti != nullptr && env != nullptr && bytecode::are_jvmti_hooks_installed()) {
        const int rc = bytecode::uninstall_jvmti_hooks(env, rt.jvmti);
        if (rc != 0 && !from_error) {
            fs::show_error_dialog("Disabling hooks failed: %u", static_cast<unsigned int>(rc));
        }
    }

    if (env != nullptr) {
        hooks::unregister_channel_handler(env, &rt.channel_handler);
        minecraft::destroy_class_loader_handle(&rt.child_class_loader, env);
        if (rt.mappings != nullptr) {
            minecraft::destroy_mappings(rt.mappings, env);
            std::free(rt.mappings);
            rt.mappings = nullptr;
        }
    }

    if (rt.gui_renderer != nullptr) {
        rt.gui_renderer->shutdown();
        std::free(rt.gui_renderer);
        rt.gui_renderer = nullptr;
    }

    commands::shutdown_commands();
    rt.module_manager.shutdown();

    if (rt.profile_registry != nullptr) {
        config::ProfileRegistry::destroy(rt.profile_registry);
        rt.profile_registry = nullptr;
    }
    if (rt.module_keybinds != nullptr) {
        config::KeybindMap::destroy(rt.module_keybinds);
        rt.module_keybinds = nullptr;
    }
    if (rt.profile_keybinds != nullptr) {
        config::KeybindMap::destroy(rt.profile_keybinds);
        rt.profile_keybinds = nullptr;
    }

    config::free_active_profile_name();
    std::free(rt.key_states);
    rt.key_states = nullptr;

    if (rt.jvmti != nullptr) {
        rt.jvmti->DisposeEnvironment();
        rt.jvmti = nullptr;
    }
}

// Matches sub_180028BB0
void dispatch_frame_update(events::GameContext* ctx, jobject timer_obj, double now_sec) noexcept {
    ClientRuntime& rt = runtime();
    if (rt.last_frame_update_time == 0.0) {
        rt.last_frame_update_time = now_sec;
    }

    time::update_cached_time();

    if (ctx->world != nullptr && ctx->local_player != nullptr) {
        const math::Vec2f cur_rot = minecraft::entity_get_rotation(ctx->jni, ctx->local_player);
        const math::Vec2f rot_diff = math::rotation_diff_deg(cur_rot, rt.last_player_rotation);
        if (rot_diff.x != 0.0f || rot_diff.y != 0.0f) {
            minecraft::accumulate_aimassist_mouse_delta(rot_diff);
        }

        const double dt = now_sec - rt.last_frame_update_time;
        if (dt >= 0.002) {
            events::FrameUpdateEvent ev{};
            ev.id = events::kEventFrameUpdate;
            ev.ctx = ctx;
            ev.partial_ticks = minecraft::timer_get_render_partial_ticks(ctx->jni, timer_obj);
            ev.delta_time = dt;
            rt.module_manager.dispatch(&ev);

            minecraft::apply_pending_aim_rotation(ctx->jni, ctx->local_player, false);
            rt.last_frame_update_time = now_sec;
            rt.last_player_rotation = minecraft::entity_get_rotation(ctx->jni, ctx->local_player);
        }
    } else {
        rt.last_frame_update_time = now_sec;
    }
}

// Matches sub_180028D00
bool query_rotation_spoof(
    bool* out_move_fix,
    bool* out_silent_restore,
    math::Vec2f* inout_rotation) noexcept
{
    ClientRuntime& rt = runtime();
    if (rt.module_manager.has_no_subscribers(events::kEventRotationSpoofQuery)) {
        return false;
    }

    events::RotationSpoofEvent ev{};
    ev.id = events::kEventRotationSpoofQuery;
    ev.rotation = *inout_rotation;
    rt.module_manager.dispatch(&ev);

    *out_move_fix = ev.move_fix;
    *inout_rotation = ev.rotation;
    *out_silent_restore = ev.silent_restore;
    return ev.active;
}

// Matches sub_180028D90
void on_set_module_keybind(events::Module* mod, uint32_t key_code) noexcept {
    ClientRuntime& rt = runtime();
    if (rt.module_keybinds != nullptr) {
        rt.module_keybinds->unbind(mod->keybind, mod);
    }
    mod->keybind = static_cast<int32_t>(key_code);
    if (rt.module_keybinds != nullptr) {
        rt.module_keybinds->bind(static_cast<int32_t>(key_code), mod);
    }

    events::ModuleKeybindChangedEvent ev{};
    ev.id = events::kEventModuleKeybindChanged;
    ev.manager = &rt.module_manager;
    ev.module = mod;
    rt.module_manager.dispatch(&ev);
}

// Matches sub_180028E10
bool on_trigger_profile_keybind(void* target, uint32_t key_code, bool pressed) noexcept {
    if (!pressed) {
        return false;
    }

    ClientRuntime& rt = runtime();
    const auto* profile_name = static_cast<const char*>(target);
    const int err = config::load_or_reset_profile(profile_name, &rt.module_manager);
    if (err != 0) {
        if (rt.profile_registry != nullptr) {
            config::ProfileEntry* entry = rt.profile_registry->find_by_name(profile_name);
            if (entry != nullptr) {
                entry->keybind = 0;
            }
        }

        events::ErrorNotificationEvent err_ev{};
        err_ev.id = events::kEventErrorNotification;
        err_ev.error_code = err;
        err_ev.custom_message = "Failed to load profile";
        rt.module_manager.dispatch(&err_ev);

        if (rt.profile_keybinds != nullptr) {
            rt.profile_keybinds->unbind(static_cast<int32_t>(key_code), nullptr);
        }
        return false;
    }

    events::ProfileActionEvent ok_ev{};
    ok_ev.id = events::kEventProfileAction;
    ok_ev.profile_name = profile_name;
    ok_ev.action = 0;
    rt.module_manager.dispatch(&ok_ev);
    return true;
}

// Matches sub_180028EF0
bool on_trigger_module_keybind(void* target, uint32_t key_code, bool pressed) noexcept {
    (void)key_code;
    ClientRuntime& rt = runtime();
    auto* mod = static_cast<events::Module*>(target);
    if (mod->hold_mode) {
        rt.module_manager.request_set_enabled(mod, pressed);
        mod->hold_active = pressed;
    } else if (pressed) {
        rt.module_manager.request_set_enabled(mod, !mod->enabled);
    }
    return true;
}

// Matches sub_180028F50
void on_set_profile_keybind(config::ProfileEntry* entry, uint32_t key_code) noexcept {
    ClientRuntime& rt = runtime();
    if (rt.profile_keybinds == nullptr) {
        entry->keybind = key_code;
        return;
    }

    rt.profile_keybinds->unbind(static_cast<int32_t>(entry->keybind), nullptr);

    if (void* existing = rt.profile_keybinds->find(static_cast<int32_t>(key_code), nullptr);
        existing != nullptr) {
        if (rt.profile_registry != nullptr) {
            config::ProfileEntry* prev = rt.profile_registry->find_by_name(static_cast<const char*>(existing));
            if (prev != nullptr) {
                prev->keybind = 0;
                rt.profile_keybinds->unbind(static_cast<int32_t>(key_code), nullptr);
            }
        }
    }

    if (key_code == 0) {
        entry->keybind = 0;
        return;
    }

    const size_t name_len = std::strlen(entry->name);
    auto* owned_name = static_cast<char*>(util::zero_alloc(name_len + 1, sizeof(char)));
    if (owned_name == nullptr) {
        events::ErrorNotificationEvent err_ev{};
        err_ev.id = events::kEventErrorNotification;
        err_ev.error_code = 1005;
        err_ev.custom_message = "Failed to set bind";
        rt.module_manager.dispatch(&err_ev);
        return;
    }

    std::memcpy(owned_name, entry->name, name_len);
    rt.profile_keybinds->bind(static_cast<int32_t>(key_code), owned_name);
    entry->keybind = key_code;
}

// Matches sub_18002AD90
bool on_window_input_event(const input::InputEvent* event) noexcept {
    ClientRuntime& rt = runtime();
    bool was_already_down = true;

    if (event->kind == input::InputEventKind::KeyOrButton) {
        const int32_t key_code = event->key.key_code;
        if (key_code >= 0 && key_code < input::KEY_COUNT && rt.key_states != nullptr) {
            const uint8_t prev_down = rt.key_states[key_code] & 1u;
            uint8_t next_state = event->pressed;
            if (event->is_synthetic == 1) {
                if (prev_down == next_state) {
                    goto dispatch_jni;
                }
                next_state = static_cast<uint8_t>(((rt.key_states[key_code] & 2u) | next_state) ^ 2u);
            }
            rt.key_states[key_code] = next_state;
            was_already_down = (prev_down == event->pressed);
        }
    }

dispatch_jni:
    if (rt.jvm == nullptr) {
        return false;
    }

    JNIEnv* env = nullptr;
    if (rt.jvm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_8) != JNI_OK || env == nullptr) {
        return false;
    }

    events::JniContext jni{rt.mappings, env};
    const bool consumed_by_gui = modules::gui_handle_window_input(event, was_already_down, &rt.module_manager);
    if (!consumed_by_gui && event->kind == input::InputEventKind::KeyOrButton && !was_already_down) {
        const int32_t key_code = event->key.key_code;
        if (key_code != 0) {
            jobject mc = minecraft::mc_get_instance(&jni);
            if (minecraft::mc_has_ingame_focus(&jni, mc) && !modules::get_gui_module()->enabled) {
                if (modules::get_friends_module()->enabled &&
                    event->pressed == 1 &&
                    key_code == input::KEY_MOUSE3) {
                    modules::friends_on_middle_click(&jni, &rt.module_manager);
                }

                const bool handled_profile =
                    rt.profile_keybinds != nullptr &&
                    rt.profile_keybinds->dispatch(static_cast<uint32_t>(key_code), event->pressed != 0);
                if (!handled_profile && rt.module_keybinds != nullptr) {
                    rt.module_keybinds->dispatch(static_cast<uint32_t>(key_code), event->pressed != 0);
                }
            }
        }
    }

    return consumed_by_gui;
}

} // namespace slinky::core
