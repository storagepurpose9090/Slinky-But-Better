#include "modules/combat/criticals.hpp"

#include <atomic>
#include <cstdint>
#include <cstdio>

#include "config/settings.hpp"
#include "gui/widgets.hpp"
#include "minecraft/game_helpers.hpp"
#include "minecraft/mc_wrappers.hpp"
#include "modules/module_interop.hpp"
#include "util/math.hpp"
#include "util/random.hpp"

namespace slinky::modules {

namespace {

constexpr const char* kModeOptions[] = {
    "Packet",
    "Timer",
};

std::atomic<bool> s_packet_crit_ready{false};
std::atomic<uint16_t> s_max_delay_ms{0};
bool s_timer_slow_active = false;
int32_t s_chance_window_ticks = 0;
bool s_chance_passed = false;
bool s_timer_modified = false;
double s_boost_debt_ticks = 0.0;
int32_t s_cur_hold_delay_ms = 0;
double s_last_packet_y = 0.0;
int32_t s_last_packet_id = 0;
bool s_packet_falling = false;

void on_game_tick(void* event) noexcept;
void on_frame_update(void* event) noexcept;
void on_outgoing_packet(void* event) noexcept;
void on_module_state_changed(void* event) noexcept;
void render_settings(gui::ClickGui* gui, gui::GuiRenderer* renderer) noexcept;
void visit_config(config::ConfigArchive* ar) noexcept;
int format_hud_tag(char* out_buf) noexcept;

struct CriticalsModule {
    events::Module base{};
    config::FloatSetting max_delay{{}, "Maximum delay", "%.0f ms", 1.0f, 0.0f, 50.0f, 300.0f, 100.0f, 100.0f};
    config::FloatSetting chance{{}, "Chance", "%.0f%%", 0.01f, 100.0f, 0.0f, 1.0f, 1.0f, 1.0f};
    config::BoolSetting require_mouse_pressed{{}, "Mouse pressed", false, false};
    config::BoolSetting require_weapon{{}, "Holding weapon", false, false};
    config::ModeSetting mode{{}, "Mode", 2, 0, kModeOptions, 0, 0, false};
    config::FloatSetting timer_speed{{}, "Timer speed", "%.2fx", 0.01f, 0.0f, 0.5f, 1.0f, 0.85f, 0.85f};
    config::FloatSetting boost{{}, "Boost", "%.0f ms", 1.0f, 50.0f, 0.0f, 5.0f, 0.0f, 0.0f};

    CriticalsModule() noexcept {
        base.name = "Criticals";
        base.description = "Increases chance of landing critical hits.";
        base.category = events::ModuleCategory::Combat;
        base.enabled = false;
        base.flags = events::kModuleFlagNone;
        base.keybind = 0;
        base.hold_mode = false;
        base.events[events::kEventGameTick] = {&on_game_tick, 0};
        base.events[events::kEventFrameUpdate] = {&on_frame_update, 100};
        base.events[events::kEventOutgoingPacket] = {&on_outgoing_packet, 0};
        base.events[events::kEventModuleStateChanged] = {&on_module_state_changed, 0};
        base.render_settings = &render_settings;
        base.visit_config = &visit_config;
        base.format_hud_tag = &format_hud_tag;
    }
};

CriticalsModule s_mod{};

// sub_180054670
void on_game_tick(void* event) noexcept {
    auto* ev = static_cast<events::ContextEvent*>(event);
    events::GameContext* ctx = ev->ctx;

    bool conditions_met = false;
    if (ctx->in_game_focus) {
        if (s_chance_window_ticks != 0) {
            --s_chance_window_ticks;
        } else {
            s_chance_window_ticks = 10;
            s_chance_passed = random::roll_chance(s_mod.chance.value);
            --s_chance_window_ticks;
        }

        if (s_chance_passed &&
            (!s_mod.require_mouse_pressed.value || mc::is_attack_key_down(ctx)) &&
            (!s_mod.require_weapon.value || weapons_is_holding_weapon_simple(ctx->jni, ctx->local_player))) {
            const int32_t last_attacker_time = mc::living_get_last_attacker_time(ctx->jni, ctx->local_player);
            const int32_t ticks_existed = mc::entity_get_ticks_existed(ctx->jni, ctx->local_player);
            conditions_met = (ticks_existed - last_attacker_time) < 11;
        }
    }

    if (s_mod.mode.value == 1) {
        s_packet_crit_ready.store(false, std::memory_order_relaxed);
        bool should_slow_timer = false;
        if (conditions_met && !mc::entity_is_on_ground(ctx->jni, ctx->local_player)) {
            const math::Vec3d motion = mc::entity_get_motion(ctx->jni, ctx->local_player);
            if (motion.y < 0.0 && mc::living_get_hurt_time(ctx->jni, ctx->local_player) != 10) {
                jobject mop = mc::mc_get_object_mouse_over(ctx->jni, ctx->minecraft);
                if (mop != nullptr && mc::mop_get_type_ordinal(ctx->jni, mop) == 2) {
                    should_slow_timer = true;
                }
            }
        }
        s_timer_slow_active = should_slow_timer;
    } else if (s_mod.mode.value == 0) {
        s_packet_crit_ready.store(conditions_met, std::memory_order_relaxed);
        s_max_delay_ms.store(static_cast<uint16_t>(static_cast<int32_t>(s_mod.max_delay.value)), std::memory_order_relaxed);
        s_timer_slow_active = false;
    }
}

// sub_1800547E0
void on_frame_update(void* event) noexcept {
    auto* ev = static_cast<events::FrameUpdateEvent*>(event);
    events::GameContext* ctx = ev->ctx;
    jobject timer = mc::mc_get_timer(ctx->jni, ctx->minecraft);

    if (s_timer_slow_active) {
        mc::timer_set_speed(ctx->jni, timer, s_mod.timer_speed.value);
        s_timer_modified = true;
        s_boost_debt_ticks += static_cast<double>(1.0f - s_mod.timer_speed.value) * (ev->delta_time * 20.0);
        const double max_boost = static_cast<double>(s_mod.boost.value);
        if (s_boost_debt_ticks > max_boost) {
            s_boost_debt_ticks = max_boost;
        }
    } else {
        if (s_boost_debt_ticks > 1.0) {
            const float cur_partial = mc::timer_get_elapsed_partial_ticks(ctx->jni, timer);
            const int32_t whole_ticks = static_cast<int32_t>(s_boost_debt_ticks);
            mc::timer_set_elapsed_partial_ticks(ctx->jni, timer, cur_partial + static_cast<float>(whole_ticks));
            s_boost_debt_ticks -= static_cast<double>(whole_ticks);
        }
        if (s_timer_modified) {
            mc::timer_set_speed(ctx->jni, timer, 1.0f);
            s_timer_modified = false;
        }
    }
}

// sub_180054900
void on_outgoing_packet(void* event) noexcept {
    auto* ev = static_cast<events::NettyPacketEvent*>(event);
    jobject c03 = mc::as_packet_c03_player(ev->jni, ev->packet);
    if (c03 != nullptr && mc::packet_c03_is_moving(ev->jni, c03) && ev->packet_id > s_last_packet_id) {
        const double packet_y = mc::packet_c03_get_y(ev->jni, c03);
        const bool on_ground = mc::packet_c03_is_on_ground(ev->jni, c03);
        if (on_ground || s_last_packet_y > packet_y) {
            s_packet_falling = !on_ground;
        }

        if (s_packet_crit_ready.load(std::memory_order_relaxed) && s_packet_falling) {
            const uint16_t max_ms = s_max_delay_ms.load(std::memory_order_relaxed);
            s_cur_hold_delay_ms += static_cast<int32_t>(max_ms / 5u);
            if (s_cur_hold_delay_ms > static_cast<int32_t>(max_ms)) {
                s_cur_hold_delay_ms = static_cast<int32_t>(max_ms);
            }
        } else {
            s_cur_hold_delay_ms = 0;
        }

        s_last_packet_y = packet_y;
        s_last_packet_id = ev->packet_id;
    }

    if (ev->elapsed_ms < static_cast<int64_t>(s_cur_hold_delay_ms)) {
        ev->action = 1;
    }
}

// sub_180054A10
void on_module_state_changed(void* event) noexcept {
    auto* ev = static_cast<events::ModuleStateChangedEvent*>(event);
    if (ev->module == &s_mod.base && !ev->enabled && s_timer_modified) {
        jobject timer = mc::mc_get_timer(ev->ctx->jni, ev->ctx->minecraft);
        mc::timer_set_speed(ev->ctx->jni, timer, 1.0f);
        s_timer_modified = false;
    }
}

// sub_180054A70
void render_settings(gui::ClickGui* gui, gui::GuiRenderer* renderer) noexcept {
    gui::widgets::render_section_header(gui, renderer, "Main");
    s_mod.mode.render(gui, renderer);
    s_mod.chance.render(gui, renderer);
    if (s_mod.mode.value != 0) {
        s_mod.timer_speed.render(gui, renderer);
        s_mod.boost.render(gui, renderer);
    } else {
        s_mod.max_delay.render(gui, renderer);
    }

    gui::widgets::render_section_header(gui, renderer, "Conditions");
    s_mod.require_weapon.render(gui, renderer);
    s_mod.require_mouse_pressed.render(gui, renderer);
}

// sub_180054B30
void visit_config(config::ConfigArchive* ar) noexcept {
    s_mod.mode.visit(ar, "mode");
    s_mod.chance.visit(ar, "chance");
    s_mod.max_delay.visit(ar, "max_delay");
    s_mod.timer_speed.visit(ar, "timer_speed");
    s_mod.boost.visit(ar, "boost");
    s_mod.require_weapon.visit(ar, "require_weapon");
    s_mod.require_mouse_pressed.visit(ar, "require_mouse_pressed");
}

// sub_180054BE0
int format_hud_tag(char* out_buf) noexcept {
    return std::snprintf(out_buf, 32, "%s", kModeOptions[s_mod.mode.value]);
}

} // namespace

events::Module* get_criticals_module() noexcept {
    return &s_mod.base;
}

} // namespace slinky::modules
