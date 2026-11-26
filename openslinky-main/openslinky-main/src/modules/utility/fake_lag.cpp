#include "modules/utility/fake_lag.hpp"
#include "modules/module_interop.hpp"

#include <cstdio>

#include "config/settings.hpp"
#include "gui/widgets.hpp"
#include "minecraft/mc_wrappers.hpp"

namespace slinky::modules {

namespace {

constexpr int32_t kNettyActionHold = 1;
constexpr int32_t kNettyActionBypassQueue = 3;

constexpr const char* kModeOptions[2] = {
    "Static",
    "Pulse",
};

config::FloatSetting s_inbound{
    {},
    "Inbound",
    "%.0f ms",
    1.0f,
    0.0f,
    0.0f,
    1000.0f,
    50.0f,
    0.0f,
};

config::FloatSetting s_outbound{
    {},
    "Outbound",
    "%.0f ms",
    1.0f,
    0.0f,
    0.0f,
    1000.0f,
    50.0f,
    0.0f,
};

config::BoolSetting s_realtime_damage{
    {},
    "Realtime damage",
    false,
    false,
    {},
};

config::ModeSetting s_mode{
    {},
    "Mode",
    2,
    0,
    kModeOptions,
    0,
    0,
    false,
    {},
};

config::BoolSetting s_require_ingame{
    {},
    "In-game",
    false,
    false,
    {},
};

config::BoolSetting s_require_weapon{
    {},
    "Holding weapon",
    false,
    false,
    {},
};

config::BoolSetting s_require_attack{
    {},
    "Attacked",
    false,
    false,
    {},
};

int32_t s_last_attacker_time = 0;
int32_t s_attacked_ticks = 0;
int32_t s_pulse_inbound_ms = 0;
int32_t s_pulse_outbound_ms = 0;
bool s_cached_realtime_damage = false;
bool s_flush_inbound_pulse = false;
bool s_flush_outbound_pulse = false;
uint16_t s_active_inbound_ms = 0;
uint16_t s_active_outbound_ms = 0;

// sub_180054290
void on_game_tick(void* raw_event) noexcept {
    if (s_attacked_ticks > 0) {
        --s_attacked_ticks;
    }

    s_cached_realtime_damage = s_realtime_damage.value;

    if (s_mode.value == 1) {
        const uint32_t prev_in = static_cast<uint32_t>(s_pulse_inbound_ms);
        if (s_pulse_inbound_ms <= 0) {
            s_pulse_inbound_ms = static_cast<int32_t>(static_cast<float>(s_pulse_inbound_ms) + s_inbound.value);
            s_flush_inbound_pulse = false;
        } else {
            s_pulse_inbound_ms -= 50;
            s_flush_inbound_pulse = (prev_in < 51u);
        }

        const uint32_t prev_out = static_cast<uint32_t>(s_pulse_outbound_ms);
        if (s_pulse_outbound_ms <= 0) {
            s_pulse_outbound_ms = static_cast<int32_t>(static_cast<float>(s_pulse_outbound_ms) + s_outbound.value);
            s_flush_outbound_pulse = false;
        } else {
            s_pulse_outbound_ms -= 50;
            s_flush_outbound_pulse = (prev_out < 51u);
        }
    } else {
        s_flush_inbound_pulse = false;
        s_flush_outbound_pulse = false;
    }

    auto* event = static_cast<events::ContextEvent*>(raw_event);
    events::GameContext* ctx = event->ctx;

    if (ctx->world == nullptr || (s_require_ingame.value && !ctx->in_game_focus)) {
        s_attacked_ticks = 0;
        s_active_outbound_ms = 0;
        s_active_inbound_ms = 0;
        return;
    }

    const int32_t attacker_time = mc::living_get_last_attacker_time(ctx->jni, ctx->local_player);
    bool active = false;
    if (!s_require_weapon.value || weapons_is_holding_weapon_simple(ctx->jni, ctx->local_player)) {
        active = (s_attacked_ticks != 0) || !s_require_attack.value;
        if (attacker_time != s_last_attacker_time) {
            s_attacked_ticks = 20;
        }
    } else {
        s_attacked_ticks = 0;
        if (attacker_time != s_last_attacker_time) {
            s_attacked_ticks = 20;
        }
    }
    s_last_attacker_time = attacker_time;

    if (active) {
        s_active_inbound_ms = static_cast<uint16_t>(static_cast<int32_t>(s_inbound.value));
        s_active_outbound_ms = static_cast<uint16_t>(static_cast<int32_t>(s_outbound.value));
    } else {
        s_active_outbound_ms = 0;
        s_active_inbound_ms = 0;
    }
}

// sub_180054410
void on_netty_packet(void* raw_event) noexcept {
    auto* event = static_cast<events::NettyPacketEvent*>(raw_event);
    uint16_t delay_ms = 0;

    if (event->id == events::kEventIncomingPacket) {
        if (s_cached_realtime_damage) {
            if (mc::as_packet_s19_entity_status(event->jni, event->packet) != nullptr ||
                mc::as_packet_s06_update_health(event->jni, event->packet) != nullptr ||
                mc::as_packet_s29_sound_effect(event->jni, event->packet) != nullptr) {
                event->action = kNettyActionBypassQueue;
                return;
            }
        }
        if (s_flush_inbound_pulse) {
            return;
        }
        delay_ms = s_active_inbound_ms;
    } else {
        if (s_flush_outbound_pulse) {
            return;
        }
        delay_ms = s_active_outbound_ms;
    }

    event->elapsed_ms -= static_cast<int64_t>(delay_ms);
    if (event->elapsed_ms < 0) {
        event->elapsed_ms = 0;
        event->action = kNettyActionHold;
    }
}

// sub_1800544C0
void render_settings(gui::ClickGui* gui, gui::GuiRenderer* renderer) noexcept {
    gui::widgets::render_section_header(gui, renderer, "Delay");
    s_mode.render(gui, renderer);
    s_inbound.render(gui, renderer);
    s_outbound.render(gui, renderer);
    s_realtime_damage.render(gui, renderer);
    gui::widgets::render_section_header(gui, renderer, "Conditions");
    s_require_weapon.render(gui, renderer);
    s_require_attack.render(gui, renderer);
    s_require_ingame.render(gui, renderer);
}

// sub_180054580
void visit_config(config::ConfigArchive* archive) noexcept {
    s_mode.visit(archive, "mode");
    s_inbound.visit(archive, "inbound");
    s_outbound.visit(archive, "outbound");
    s_realtime_damage.visit(archive, "realtime_damage");
    s_require_weapon.visit(archive, "require_weapon");
    s_require_attack.visit(archive, "require_attack");
    s_require_ingame.visit(archive, "require_ingame");
}

// sub_180054630
int format_hud_tag(char* out_buf_32) noexcept {
    return std::snprintf(out_buf_32, 32, "%s", kModeOptions[s_mode.value]);
}

events::Module make_module() noexcept {
    events::Module mod{};
    mod.name = "Fake Lag";
    mod.description = "Adds an artificial delay to your packets.";
    mod.category = events::ModuleCategory::Utility;
    mod.enabled = false;
    mod.flags = events::kModuleFlagNone;
    mod.keybind = 0;
    mod.hold_mode = false;
    mod.events[events::kEventGameTick] = {on_game_tick, 0, 0};
    mod.events[events::kEventIncomingPacket] = {on_netty_packet, 100, 0};
    mod.events[events::kEventOutgoingPacket] = {on_netty_packet, 100, 0};
    mod.render_settings = render_settings;
    mod.visit_config = visit_config;
    mod.format_hud_tag = format_hud_tag;
    return mod;
}

events::Module s_module = make_module();

} // namespace

events::Module* get_fake_lag_module() noexcept {
    return &s_module;
}

} // namespace slinky::modules
