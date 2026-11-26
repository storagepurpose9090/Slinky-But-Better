#include "modules/utility/blink.hpp"

#include <cstdio>

#include "config/settings.hpp"
#include "events/module_manager.hpp"
#include "gui/widgets.hpp"
#include "minecraft/mc_wrappers.hpp"

namespace slinky::modules {

namespace {

extern events::Module s_module;

constexpr int32_t kNettyActionHold = 1;
constexpr int32_t kNettyActionBypassQueue = 3;
constexpr int32_t kMopTypeBlock = 1;

constexpr const char* kDirectionOptions[3] = {
    "In",
    "Out",
    "Both",
};

config::BoolSetting s_disable_on_attack{
    {},
    "Attack",
    false,
    false,
    {},
};

config::FloatSetting s_max_duration{
    {},
    "Maximum duration",
    "%.2f s",
    1.0f,
    0.05f,
    0.0f,
    1000.0f,
    100.0f,
    0.0f,
};

config::ModeSetting s_direction{
    {},
    "Direction",
    3,
    0,
    kDirectionOptions,
    1,
    0,
    false,
    {},
};

config::BoolSetting s_disable_on_block_dig{
    {},
    "Block dig",
    false,
    false,
    {},
};

config::BoolSetting s_disable_on_block_interact{
    {},
    "Block interact",
    false,
    false,
    {},
};

int32_t s_last_attacker_time = 0x7FFFFFFF;
int32_t s_remaining_ticks = 0;
uint8_t s_active_direction = 0;

// sub_180054DC0
void on_game_tick(void* raw_event) noexcept {
    auto* event = static_cast<events::ContextEvent*>(raw_event);
    events::GameContext* ctx = event->ctx;

    if (ctx->in_game_focus) {
        const int32_t attacker_time = mc::living_get_last_attacker_time(ctx->jni, ctx->local_player);
        const bool was_attacked = attacker_time > s_last_attacker_time;
        s_last_attacker_time = attacker_time;

        if (--s_remaining_ticks == 0 || (s_disable_on_attack.value && was_attacked)) {
            ctx->module_manager->request_set_enabled(&s_module, false);
        }
    }
}

// sub_180054E30
void on_incoming_packet(void* raw_event) noexcept {
    if (s_active_direction == 1) {
        return;
    }

    auto* event = static_cast<events::NettyPacketEvent*>(raw_event);
    events::JniContext* jni = event->jni;
    jobject packet = event->packet;

    if (mc::as_packet_s19_entity_status(jni, packet) != nullptr ||
        mc::as_packet_s06_update_health(jni, packet) != nullptr ||
        mc::as_packet_s29_sound_effect(jni, packet) != nullptr ||
        mc::as_packet_s2a_particles(jni, packet) != nullptr) {
        event->action = kNettyActionBypassQueue;
    } else {
        event->action = kNettyActionHold;
    }
}

// sub_180054EB0
void on_outgoing_packet(void* raw_event) noexcept {
    if (s_active_direction != 0) {
        auto* event = static_cast<events::NettyPacketEvent*>(raw_event);
        event->action = kNettyActionHold;
    }
}

// sub_180054ED0
void on_module_state_changed(void* raw_event) noexcept {
    auto* event = static_cast<events::ModuleStateChangedEvent*>(raw_event);
    if (event->module == &s_module && event->enabled) {
        s_remaining_ticks = static_cast<int32_t>(s_max_duration.value);
        s_last_attacker_time = 0x7FFFFFFF;
        s_active_direction = static_cast<uint8_t>(s_direction.value);
    }
}

// sub_180054F20
void on_pre_attack_or_use(void* raw_event) noexcept {
    auto* event = static_cast<events::AttackOrUseEntityEvent*>(raw_event);
    if (event->is_attack) {
        if (!s_disable_on_block_dig.value) {
            return;
        }
    } else if (!s_disable_on_block_interact.value) {
        return;
    }

    events::GameContext* ctx = event->ctx;
    jobject mop = mc::mc_get_object_mouse_over(ctx->jni, ctx->minecraft);
    if (mop != nullptr && mc::mop_get_type_ordinal(ctx->jni, mop) == kMopTypeBlock) {
        ctx->module_manager->request_set_enabled(&s_module, false);
    }
}

// sub_180054F90
void render_settings(gui::ClickGui* gui, gui::GuiRenderer* renderer) noexcept {
    gui::widgets::render_section_header(gui, renderer, "Main");
    s_direction.render(gui, renderer);
    s_max_duration.render(gui, renderer);
    gui::widgets::render_section_header(gui, renderer, "Disable on");
    s_disable_on_attack.render(gui, renderer);
    s_disable_on_block_interact.render(gui, renderer);
    s_disable_on_block_dig.render(gui, renderer);
}

// sub_180055030
void visit_config(config::ConfigArchive* archive) noexcept {
    s_direction.visit(archive, "direction");
    s_max_duration.visit(archive, "max_duration");
    s_disable_on_attack.visit(archive, "disable_on_attack");
    s_disable_on_block_interact.visit(archive, "disable_on_block_interact");
    s_disable_on_block_dig.visit(archive, "disable_on_block_dig");
}

// sub_1800550B0
int format_hud_tag(char* out_buf_32) noexcept {
    return std::snprintf(out_buf_32, 32, "%s", kDirectionOptions[s_direction.value]);
}

events::Module make_module() noexcept {
    events::Module mod{};
    mod.name = "Blink";
    mod.description = "Simulates teleporting using lag.";
    mod.category = events::ModuleCategory::Utility;
    mod.enabled = false;
    mod.flags = events::kModuleFlagNone;
    mod.keybind = 0;
    mod.hold_mode = false;
    mod.events[events::kEventGameTick] = {on_game_tick, 0, 0};
    mod.events[events::kEventIncomingPacket] = {on_incoming_packet, 0, 0};
    mod.events[events::kEventOutgoingPacket] = {on_outgoing_packet, 0, 0};
    mod.events[events::kEventModuleStateChanged] = {on_module_state_changed, 0, 0};
    mod.events[events::kEventPreAttackOrUseEntity] = {on_pre_attack_or_use, 0, 0};
    mod.render_settings = render_settings;
    mod.visit_config = visit_config;
    mod.format_hud_tag = format_hud_tag;
    return mod;
}

events::Module s_module = make_module();

} // namespace

events::Module* get_blink_module() noexcept {
    return &s_module;
}

} // namespace slinky::modules
