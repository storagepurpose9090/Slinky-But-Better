#include "modules/utility/debug.hpp"

#include "config/settings.hpp"
#include "gui/widgets.hpp"
#include "minecraft/game_helpers.hpp"
#include "minecraft/mc_wrappers.hpp"

namespace slinky::modules {

namespace {

constexpr int32_t kDiggingReleaseUseItem = 5;
constexpr int32_t kNettyActionForward = 0;
constexpr int32_t kNettyActionBypassQueue = 3;

// Color codes for hurt_time 7, 8, 9, 10 (aA_1 @ 0x1801717C0)
constexpr uint32_t kHurtTimeColorCodes[4] = {'a', 'e', 'c', '4'};

config::BoolSetting s_hurt_time{
    {},
    "Hurt time",
    false,
    false,
    {},
};

config::BoolSetting s_item_use_state{
    {},
    "Server item use state",
    false,
    false,
    {},
};

config::BoolSetting s_clicks{
    {},
    "Clicks",
    false,
    false,
    {},
};

uint8_t s_last_reported_item_use = 0;
uint8_t s_server_item_use_active = 0;
uint32_t s_left_clicks = 0;
uint32_t s_right_clicks = 0;
int32_t s_ticks_since_click = 0;

// sub_180067CA0
void on_game_tick(void* raw_event) noexcept {
    auto* event = static_cast<events::ContextEvent*>(raw_event);
    events::GameContext* ctx = event->ctx;
    if (!ctx->in_game_focus) {
        return;
    }

    events::JniContext* jni = ctx->jni;
    if (s_hurt_time.value) {
        const int32_t hurt_time = mc::living_get_hurt_time(jni, ctx->local_player);
        if (hurt_time >= 7) {
            const uint32_t tick_mod =
                static_cast<uint32_t>(mc::entity_get_ticks_existed(jni, ctx->local_player)) % 1000u;
            uint32_t hurt_color = 'f';
            if (static_cast<uint32_t>(hurt_time - 7) <= 3u) {
                hurt_color = kHurtTimeColorCodes[hurt_time - 7];
            }
            const uint32_t sprint_color = mc::entity_is_sprinting(jni, ctx->local_player) ? '8' : 'c';
            const uint32_t block_color = mc::player_is_using_item(jni, ctx->local_player) ? 'c' : '8';
            mc::print_client_chat(
                jni,
                "\xC2\xA7" "7[%03d] [debug/hurt_time] \xC2\xA7%c%02d \xC2\xA7%cS \xC2\xA7%cB",
                tick_mod,
                hurt_color,
                hurt_time,
                sprint_color,
                block_color);
        }
    }

    if (s_item_use_state.value) {
        const uint8_t current_state = s_server_item_use_active & 1u;
        if (current_state != s_last_reported_item_use) {
            s_last_reported_item_use = current_state;
            const uint32_t tick_mod =
                static_cast<uint32_t>(mc::entity_get_ticks_existed(jni, ctx->local_player)) % 1000u;
            const char* label = (current_state != 0) ? "aStart" : "cStop";
            mc::print_client_chat(
                jni,
                "\xC2\xA7" "7[%03d] [debug/item_use] \xC2\xA7%s",
                tick_mod,
                label);
        }
    }
}

// sub_180067E10
void on_outgoing_packet(void* raw_event) noexcept {
    auto* event = static_cast<events::NettyPacketEvent*>(raw_event);
    if (event->action == kNettyActionBypassQueue || event->action == kNettyActionForward) {
        if (mc::as_packet_c08_block_placement(event->jni, event->packet) != nullptr) {
            s_server_item_use_active = 1;
            return;
        }
        jobject digging = mc::as_packet_c07_digging(event->jni, event->packet);
        if (digging != nullptr && mc::packet_c07_get_status(event->jni, digging) == kDiggingReleaseUseItem) {
            s_server_item_use_active = 0;
        }
    }
}

// sub_180067E80
void on_post_attack_or_use(void* raw_event) noexcept {
    if (!s_clicks.value) {
        return;
    }

    auto* event = static_cast<events::AttackOrUseEntityEvent*>(raw_event);
    if (event->is_attack) {
        ++s_left_clicks;
    } else {
        ++s_right_clicks;
    }
    s_ticks_since_click = 0;
}

// sub_180067EC0
void on_post_living_update(void* raw_event) noexcept {
    auto* event = static_cast<events::ContextEvent*>(raw_event);
    events::GameContext* ctx = event->ctx;
    if (!ctx->in_game_focus) {
        return;
    }

    int32_t idle_ticks = s_ticks_since_click;
    if (s_clicks.value) {
        if (static_cast<uint32_t>(idle_ticks) <= 9u) {
            uint32_t left_color = '2';
            if (s_left_clicks == 1) {
                left_color = 'a';
            } else if (s_left_clicks == 0) {
                left_color = '8';
            }

            uint32_t right_color = '3';
            if (s_right_clicks == 1) {
                right_color = 'b';
            } else if (s_right_clicks == 0) {
                right_color = '8';
            }

            const uint32_t tick_mod =
                static_cast<uint32_t>(mc::entity_get_ticks_existed(ctx->jni, ctx->local_player)) % 1000u;
            mc::print_client_chat(
                ctx->jni,
                "\xC2\xA7" "7[%03d] [debug/click] \xC2\xA7%cL %u \xC2\xA7%cR %u",
                tick_mod,
                left_color,
                s_left_clicks,
                right_color,
                s_right_clicks);
            idle_ticks = s_ticks_since_click;
        }
        s_left_clicks = 0;
        s_right_clicks = 0;
    }
    s_ticks_since_click = idle_ticks + 1;
}

// sub_180067FC0
void render_settings(gui::ClickGui* gui, gui::GuiRenderer* renderer) noexcept {
    gui::widgets::render_section_header(gui, renderer, "Main");
    s_clicks.render(gui, renderer);
    s_hurt_time.render(gui, renderer);
    s_item_use_state.render(gui, renderer);
}

// sub_180068020
void visit_config(config::ConfigArchive* archive) noexcept {
    s_clicks.visit(archive, "clicks");
    s_hurt_time.visit(archive, "hurt_time");
    s_item_use_state.visit(archive, "item_use_state");
}

events::Module make_module() noexcept {
    events::Module mod{};
    mod.name = "Debug";
    mod.description = "Prints debug information in chat.";
    mod.category = events::ModuleCategory::Utility;
    mod.enabled = false;
    mod.flags = events::kModuleFlagHiddenFromHud;
    mod.keybind = 0;
    mod.hold_mode = false;
    mod.events[events::kEventGameTick] = {on_game_tick, 0, 0};
    mod.events[events::kEventOutgoingPacket] = {on_outgoing_packet, -10000, 0};
    mod.events[events::kEventPostAttackOrUseEntity] = {on_post_attack_or_use, 0, 0};
    mod.events[events::kEventPostLivingUpdate] = {on_post_living_update, 0, 0};
    mod.render_settings = render_settings;
    mod.visit_config = visit_config;
    return mod;
}

events::Module s_module = make_module();

} // namespace

events::Module* get_debug_module() noexcept {
    return &s_module;
}

} // namespace slinky::modules
