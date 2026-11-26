#include "modules/movement/inv_walk.hpp"

#include <cstdio>
#include "config/settings.hpp"
#include "gui/widgets.hpp"
#include "minecraft/mc_wrappers.hpp"

namespace slinky::modules {

namespace {

constexpr const char* kModeOptions[2] = {
    "Vanilla",
    "Spoof",
};

constexpr const char* kStopOptions[3] = {
    "Full",
    "Partial",
    "No",
};

consteval config::ModeSetting make_mode(
    const char* label,
    uint32_t option_count,
    const char* const* options,
    int32_t initial = 0) noexcept
{
    config::ModeSetting s{};
    s.label = label;
    s.option_count = option_count;
    s.options = options;
    s.value = initial;
    s.default_value = initial;
    return s;
}

consteval config::FloatSetting make_float(
    const char* label,
    const char* format,
    float step,
    float display_scale,
    float min_val,
    float max_val,
    float initial) noexcept
{
    config::FloatSetting s{};
    s.label = label;
    s.format = format;
    s.step = step;
    s.display_scale = display_scale;
    s.min_val = min_val;
    s.max_val = max_val;
    s.value = initial;
    s.default_value = initial;
    return s;
}

constinit config::ModeSetting  s_mode          = make_mode("Mode", 2, kModeOptions, 0);                      // unk_18030C680
constinit config::ModeSetting  s_stop_on_click = make_mode("Stop on interact", 3, kStopOptions, 0);          // unk_18030C6D8
constinit config::FloatSetting s_stop_duration = make_float("Stop duration", "%.0f ms", 1.0f, 50.0f, 1.0f, 20.0f, 2.0f); // unk_18030C718
constinit config::ModeSetting  s_stop_non_inv  = make_mode("Always stop", 3, kStopOptions, 0);               // unk_18030C750

constexpr size_t kMaxQueuedPackets = 32;

constinit jobject  s_queued_packets[kMaxQueuedPackets]{};         // qword_180319860
constinit size_t   s_queued_packet_count = 0;                     // qword_180319960
constinit bool     s_was_stopping_movement = false;               // byte_180319968
constinit uint32_t s_tick_counter = 0;                            // dword_18031996C
constinit int32_t  s_inventory_state = 0;                         // dword_180319970
constinit uint32_t s_last_interact_tick = 0;                      // dword_180319974
constinit bool     s_opened_server_inventory_this_tick = false;   // byte_180319978

// Matches sub_18005B3C0
bool flush_queued_packets(events::JniContext* jni, jobject mc_instance) noexcept {
    if (s_queued_packet_count == 0) {
        return false;
    }

    JNIEnv* env = jni->env;
    jobject net_handler = minecraft::mc_get_net_handler(jni, mc_instance);
    jobject net_manager = minecraft::net_handler_get_network_manager(jni, net_handler);
    const bool sent = (net_handler != nullptr) && (net_manager != nullptr);

    if (sent) {
        for (size_t i = 0; i < s_queued_packet_count; ++i) {
            jobject pkt = s_queued_packets[i];
            minecraft::network_manager_send_packet_no_event(jni, net_manager, pkt);
            env->DeleteGlobalRef(pkt);
        }
    } else {
        for (size_t i = 0; i < s_queued_packet_count; ++i) {
            env->DeleteGlobalRef(s_queued_packets[i]);
        }
    }

    s_queued_packet_count = 0;
    return sent;
}

// Matches sub_18005AB30
void on_module_state_changed(void* raw_event) noexcept {
    const auto* ev = static_cast<const events::ModuleStateChangedEvent*>(raw_event);
    if (!ev->enabled && ev->module == get_inv_walk_module()) {
        flush_queued_packets(ev->ctx->jni, ev->ctx->minecraft);
    }
}

// Matches sub_18005AB60
void on_mid_living_update(void* raw_event) noexcept {
    const auto* ev = static_cast<const events::ContextEvent*>(raw_event);
    events::GameContext* ctx = ev->ctx;

    jobject container_screen = minecraft::mc_get_gui_container_screen(ctx->jni, ctx->minecraft);
    jobject settings         = minecraft::mc_get_game_settings(ctx->jni, ctx->minecraft);
    jobject key_back         = minecraft::settings_get_key_back(ctx->jni, settings);
    jobject key_left         = minecraft::settings_get_key_left(ctx->jni, settings);
    jobject key_right        = minecraft::settings_get_key_right(ctx->jni, settings);
    jobject key_forward      = minecraft::settings_get_key_forward(ctx->jni, settings);
    jobject key_jump         = minecraft::settings_get_key_jump(ctx->jni, settings);

    const bool back_down    = minecraft::key_binding_is_physically_down(ctx->jni, key_back);
    const bool left_down    = minecraft::key_binding_is_physically_down(ctx->jni, key_left);
    const bool right_down   = minecraft::key_binding_is_physically_down(ctx->jni, key_right);
    const bool forward_down = minecraft::key_binding_is_physically_down(ctx->jni, key_forward);
    const bool jump_down    = minecraft::key_binding_is_physically_down(ctx->jni, key_jump);

    bool stop_sprint = (s_mode.value == 1) && (s_stop_on_click.value != 2) && (s_queued_packet_count != 0);
    bool stop_all_keys = stop_sprint && (s_stop_on_click.value == 0);
    ++s_tick_counter;

    bool allow_gui_move = false;

    if (container_screen != nullptr) {
        const bool any_move_down = back_down || left_down || right_down || forward_down || jump_down;
        if (!minecraft::is_gui_inventory(ctx->jni, container_screen)) {
            s_inventory_state = 1;
            allow_gui_move = true;
            if (s_mode.value == 1) {
                const bool idle_or_queued = (!any_move_down) || stop_sprint;
                bool interact_cooldown = false;
                if (s_stop_on_click.value != 2) {
                    const auto duration_ticks = static_cast<uint32_t>(static_cast<int64_t>(s_stop_duration.value));
                    interact_cooldown = (s_tick_counter - s_last_interact_tick) < duration_ticks;
                }
                const bool click_stop = idle_or_queued || interact_cooldown;
                stop_sprint = click_stop || (s_stop_non_inv.value != 2);
                stop_all_keys = (click_stop && s_stop_on_click.value == 0) ||
                                (stop_sprint && s_stop_non_inv.value == 0);
            }
        } else if (s_mode.value != 1) {
            s_inventory_state = 3;
            allow_gui_move = true;
        } else {
            jobject inv_player = minecraft::player_get_inventory(ctx->jni, ctx->local_player);
            jobject net_handler = minecraft::mc_get_net_handler(ctx->jni, ctx->minecraft);
            jobject net_manager = minecraft::net_handler_get_network_manager(ctx->jni, net_handler);
            jobject dragged_stack = minecraft::inv_player_get_dragged_stack(ctx->jni, inv_player);

            bool holding_or_crafting = true;
            if (dragged_stack == nullptr) {
                if (s_inventory_state == 1) {
                    holding_or_crafting = stop_sprint;
                } else {
                    jobject inv_container = minecraft::player_get_inventory_container(ctx->jni, ctx->local_player);
                    jobject craft_matrix = minecraft::container_player_get_craft_matrix(ctx->jni, inv_container);
                    if (minecraft::inventory_get_stack_in_slot(ctx->jni, craft_matrix, 0) == nullptr &&
                        minecraft::inventory_get_stack_in_slot(ctx->jni, craft_matrix, 1) == nullptr &&
                        minecraft::inventory_get_stack_in_slot(ctx->jni, craft_matrix, 2) == nullptr &&
                        minecraft::inventory_get_stack_in_slot(ctx->jni, craft_matrix, 3) == nullptr) {
                        holding_or_crafting = stop_sprint;
                    }
                }
            }

            const bool want_move_and_safe_to_close = (!holding_or_crafting) && any_move_down;
            int32_t next_state = s_inventory_state;

            if (s_inventory_state == 2 && !want_move_and_safe_to_close) {
                jobject open_state = minecraft::c16_get_open_inventory_state(ctx->jni);
                jobject open_pkt = minecraft::packet_c16_new(ctx->jni, open_state);
                minecraft::network_manager_send_packet_no_event(ctx->jni, net_manager, open_pkt);
                next_state = 3;
                s_inventory_state = 3;
            } else if (s_queued_packet_count == 0 && s_inventory_state == 3 && want_move_and_safe_to_close) {
                bool cooldown_blocking = false;
                if (s_stop_on_click.value != 2) {
                    const auto duration_ticks = static_cast<uint32_t>(static_cast<int64_t>(s_stop_duration.value));
                    if ((s_tick_counter - s_last_interact_tick) < duration_ticks) {
                        next_state = 3;
                        cooldown_blocking = true;
                    }
                }
                if (!cooldown_blocking) {
                    jobject close_pkt = minecraft::packet_c0d_new(ctx->jni, 0);
                    minecraft::network_manager_send_packet_no_event(ctx->jni, net_manager, close_pkt);
                    next_state = 2;
                    s_inventory_state = 2;
                }
            }

            const bool server_inv_busy = (next_state == 3) || holding_or_crafting;
            allow_gui_move = (next_state != 0);
            stop_sprint = stop_sprint || (server_inv_busy && s_stop_on_click.value != 2);
            stop_all_keys = stop_sprint && (s_stop_on_click.value == 0);
        }
    } else {
        if (s_inventory_state != 0) {
            minecraft::key_binding_set_pressed(ctx->jni, key_back, back_down);
            minecraft::key_binding_set_pressed(ctx->jni, key_left, left_down);
            minecraft::key_binding_set_pressed(ctx->jni, key_right, right_down);
            minecraft::key_binding_set_pressed(ctx->jni, key_forward, forward_down);
            minecraft::key_binding_set_pressed(ctx->jni, key_jump, jump_down);
        }
        s_inventory_state = 0;
        if (!ctx->in_game_focus) {
            return;
        }
    }

    if (allow_gui_move || s_was_stopping_movement || stop_all_keys) {
        if (stop_all_keys) {
            minecraft::key_binding_set_pressed(ctx->jni, key_back, false);
            minecraft::key_binding_set_pressed(ctx->jni, key_left, false);
            minecraft::key_binding_set_pressed(ctx->jni, key_right, false);
            minecraft::key_binding_set_pressed(ctx->jni, key_forward, false);
            minecraft::key_binding_set_pressed(ctx->jni, key_jump, false);
            stop_all_keys = true;
        } else {
            minecraft::key_binding_set_pressed(ctx->jni, key_back, back_down);
            minecraft::key_binding_set_pressed(ctx->jni, key_left, left_down);
            minecraft::key_binding_set_pressed(ctx->jni, key_right, right_down);
            minecraft::key_binding_set_pressed(ctx->jni, key_forward, forward_down);
            minecraft::key_binding_set_pressed(ctx->jni, key_jump, jump_down);
            stop_all_keys = false;
        }
    }

    if (stop_sprint) {
        minecraft::entity_set_sprinting(ctx->jni, ctx->local_player, false);
        jobject key_sprint = minecraft::settings_get_key_sprint(ctx->jni, settings);
        minecraft::key_binding_set_pressed(ctx->jni, key_sprint, false);
    }

    s_was_stopping_movement = stop_all_keys;
    s_opened_server_inventory_this_tick = false;
}

// Matches sub_18005B0B0
void on_main_thread_packet(void* raw_event) noexcept {
    if (s_mode.value != 1) {
        return;
    }

    auto* ev = static_cast<events::MainThreadPacketEvent*>(raw_event);
    jobject c16 = minecraft::as_packet_c16_client_status(ev->jni, ev->packet);
    if (c16 != nullptr) {
        if (minecraft::packet_c16_get_status_ordinal(ev->jni, c16) == 2) {
            ev->cancelled = true;
            s_inventory_state = 2;
        }
        return;
    }

    if (minecraft::as_packet_c0e_click_window(ev->jni, ev->packet) != nullptr) {
        if (s_inventory_state == 2) {
            jobject open_state = minecraft::c16_get_open_inventory_state(ev->jni);
            jobject open_pkt = minecraft::packet_c16_new(ev->jni, open_state);
            minecraft::network_manager_send_packet_no_event(ev->jni, ev->net_handler, open_pkt);
            s_inventory_state = 3;
            s_opened_server_inventory_this_tick = true;
        } else if (!s_opened_server_inventory_this_tick && s_queued_packet_count == 0) {
            s_last_interact_tick = s_tick_counter;
            return;
        }
    } else {
        if (minecraft::as_packet_c0d_close_window(ev->jni, ev->packet) == nullptr) {
            return;
        }
        if (s_inventory_state == 2) {
            ev->cancelled = true;
            return;
        }
        if (s_queued_packet_count == 0) {
            return;
        }
    }

    if (s_queued_packet_count != kMaxQueuedPackets) {
        jobject global_pkt = ev->jni->env->NewGlobalRef(ev->packet);
        if (global_pkt != nullptr) {
            s_queued_packets[s_queued_packet_count] = global_pkt;
            ++s_queued_packet_count;
        }
    }
    ev->cancelled = true;
}

// Matches sub_18005B200
void on_gui_screen_tick(void* raw_event) noexcept {
    if (s_opened_server_inventory_this_tick) {
        return;
    }

    const auto* ev = static_cast<const events::GuiScreenTickEvent*>(raw_event);
    events::GameContext* ctx = ev->ctx;
    if (flush_queued_packets(ctx->jni, ctx->minecraft)) {
        s_last_interact_tick = s_tick_counter;
        if (ctx->in_game_focus) {
            while (minecraft::lwjgl_mouse_next(ctx->jni)) {
            }
            while (minecraft::lwjgl_keyboard_next(ctx->jni)) {
            }
        }
    }
}

// Matches sub_18005B270
void render_settings(gui::ClickGui* gui, gui::GuiRenderer* renderer) noexcept {
    gui::widgets::render_section_header(gui, renderer, "Main");
    s_mode.render(gui, renderer);
    if (s_mode.value != 0) {
        s_stop_on_click.render(gui, renderer);
        if (s_stop_on_click.value != 2) {
            s_stop_duration.render(gui, renderer);
        }
        gui::widgets::render_section_header(gui, renderer, "Other containers");
        s_stop_non_inv.render(gui, renderer);
    }
}

// Matches sub_18005B310
void visit_config(config::ConfigArchive* ar) noexcept {
    s_mode.visit(ar, "mode");
    s_stop_on_click.visit(ar, "stop_on_click");
    s_stop_duration.visit(ar, "stop_duration");
    s_stop_non_inv.visit(ar, "stop_non_inv");
}

// Matches sub_18005B380
int format_hud_tag(char* out_buf_32) noexcept {
    return std::snprintf(out_buf_32, 32, "%s", kModeOptions[s_mode.value]);
}

consteval events::Module make_module() noexcept {
    events::Module mod{};
    mod.name = "Inv Walk";
    mod.description = "Allows moving in inventory.";
    mod.category = events::ModuleCategory::Move;
    mod.events[events::kEventModuleStateChanged] = {&on_module_state_changed, 0, 0};
    mod.events[events::kEventMidLivingUpdate] = {&on_mid_living_update, 0, 0};
    mod.events[events::kEventMainThreadPacket] = {&on_main_thread_packet, 0, 0};
    mod.events[events::kEventGuiScreenTick] = {&on_gui_screen_tick, 0, 0};
    mod.render_settings = &render_settings;
    mod.visit_config = &visit_config;
    mod.format_hud_tag = &format_hud_tag;
    return mod;
}

constinit events::Module s_module = make_module();

} // namespace

events::Module* get_inv_walk_module() noexcept {
    return &s_module;
}

} // namespace slinky::modules
