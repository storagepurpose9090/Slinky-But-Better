#include "modules/combat/knockback_delay.hpp"

#include <atomic>
#include <cmath>
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

bool s_delaying = false;
int32_t s_held_vel_packet_id = 0;
std::atomic<bool> s_disallowed_on_ground{false};
std::atomic<bool> s_can_start_delay{false};
std::atomic<uint16_t> s_max_delay_ms{0};
std::atomic<int32_t> s_local_entity_id{0};
random::DriftNoise s_chance_drift{};

void on_incoming_packet(void* event) noexcept;
void on_post_living_update(void* event) noexcept;
void render_settings(gui::ClickGui* gui, gui::GuiRenderer* renderer) noexcept;
void visit_config(config::ConfigArchive* ar) noexcept;
int format_hud_tag(char* out_buf) noexcept;

struct KnockbackDelayModule {
    events::Module base{};
    config::FloatSetting max_delay{{}, "Maximum delay", "%.0f ms", 0.0f, 0.0f, 25.0f, 1000.0f, 50.0f, 50.0f};
    config::BoolSetting require_air{{}, "In air", true, true};
    config::FloatSetting range{{}, "Distance to target", "%.1f blocks", 0.0f, 0.0f, 2.0f, 8.0f, 4.0f, 4.0f};
    config::BoolSetting require_weapon{{}, "Holding weapon", false, false};
    config::BoolSetting require_mouse{{}, "Mouse pressed", false, false};
    config::BoolSetting require_target{{}, "Looking at player", false, false};
    config::FloatSetting chance{{}, "Chance", "%.0f%%", 0.01f, 100.0f, 0.0f, 1.0f, 1.0f, 1.0f};

    KnockbackDelayModule() noexcept {
        base.name = "Knockback Delay";
        base.description = "Delays your knockback in certain scenarios.";
        base.category = events::ModuleCategory::Combat;
        base.enabled = false;
        base.flags = events::kModuleFlagNone;
        base.keybind = 0;
        base.hold_mode = false;
        base.events[events::kEventIncomingPacket] = {&on_incoming_packet, 0};
        base.events[events::kEventPostLivingUpdate] = {&on_post_living_update, 0};
        base.render_settings = &render_settings;
        base.visit_config = &visit_config;
        base.format_hud_tag = &format_hud_tag;
    }
};

KnockbackDelayModule s_mod{};

// sub_180067700
void on_incoming_packet(void* event) noexcept {
    auto* ev = static_cast<events::NettyPacketEvent*>(event);
    if (ev->action != 0) {
        return;
    }

    jobject s12 = mc::as_packet_s12_entity_velocity(ev->jni, ev->packet);
    const bool is_local_velocity =
        (s12 != nullptr) &&
        (mc::packet_s12_get_entity_id(ev->jni, s12) == s_local_entity_id.load(std::memory_order_acquire));

    bool active = false;
    if (s_disallowed_on_ground.load(std::memory_order_acquire)) {
        active = (ev->elapsed_ms == 0) ? s_can_start_delay.load(std::memory_order_acquire) : false;
        s_delaying = active;
    } else if (is_local_velocity && s_can_start_delay.load(std::memory_order_acquire)) {
        s_delaying = true;
        active = true;
    } else {
        active = s_delaying;
    }

    if (active && is_local_velocity) {
        if (s_held_vel_packet_id != 0) {
            if (s_held_vel_packet_id != ev->packet_id) {
                s_delaying = false;
                s_can_start_delay.exchange(false, std::memory_order_seq_cst);
                s_held_vel_packet_id = 0;
                return;
            }
        } else {
            s_held_vel_packet_id = ev->packet_id;
        }
    } else if (!active) {
        return;
    }

    if (ev->elapsed_ms > static_cast<int64_t>(s_max_delay_ms.load(std::memory_order_acquire))) {
        s_delaying = false;
        s_can_start_delay.exchange(false, std::memory_order_seq_cst);
        s_held_vel_packet_id = 0;
        return;
    }

    if (mc::as_packet_s19_entity_status(ev->jni, ev->packet) != nullptr ||
        mc::as_packet_s06_update_health(ev->jni, ev->packet) != nullptr ||
        mc::as_packet_s29_sound_effect(ev->jni, ev->packet) != nullptr) {
        ev->action = 3;
    } else {
        ev->action = 1;
    }
}

// sub_180067830
void on_post_living_update(void* event) noexcept {
    auto* ev = static_cast<events::ContextEvent*>(event);
    events::GameContext* ctx = ev->ctx;
    if (ctx->local_player == nullptr) {
        return;
    }

    s_local_entity_id.exchange(mc::entity_get_id(ctx->jni, ctx->local_player), std::memory_order_seq_cst);
    const bool disallowed_ground =
        s_mod.require_air.value && mc::entity_is_on_ground(ctx->jni, ctx->local_player);
    s_disallowed_on_ground.exchange(disallowed_ground, std::memory_order_seq_cst);

    bool can_delay = false;
    events::TargetTracker* tracker = ctx->target_tracker;
    const double max_dist_sq = static_cast<double>(s_mod.range.value * s_mod.range.value);

    if (tracker->target_entity != nullptr &&
        tracker->distance_sq <= max_dist_sq &&
        (!s_mod.require_weapon.value || weapons_is_holding_weapon_simple(ctx->jni, ctx->local_player)) &&
        (!s_mod.require_mouse.value || mc::is_attack_key_down(ctx))) {
        bool looking_at_player = !s_mod.require_target.value;
        if (!looking_at_player) {
            const math::Vec3d local_pos = mc::entity_get_position(ctx->jni, ctx->local_player);
            const math::Vec2f local_rot = mc::entity_get_rotation(ctx->jni, ctx->local_player);
            jobject players = mc::world_get_player_entities(ctx->jni, ctx->world);
            for (int32_t i = 0; i < mc::java_list_size(ctx->jni, players); ++i) {
                jobject other = mc::java_list_get(ctx->jni, players, i);
                if (ctx->jni->env->IsSameObject(ctx->local_player, other)) {
                    continue;
                }
                const math::Vec3d other_pos = mc::entity_get_position(ctx->jni, other);
                if (math::vec3_dist_sq(local_pos, other_pos) <= 36.0) {
                    const math::Vec2f aim = math::calc_yaw_pitch(local_pos, other_pos);
                    if (std::fabs(math::wrap_degrees_180(aim.x - local_rot.x)) <= 90.0f) {
                        looking_at_player = true;
                        break;
                    }
                }
            }
        }

        if (looking_at_player) {
            const double chance_val = static_cast<double>(s_mod.chance.value);
            if (chance_val > 0.999) {
                can_delay = true;
            } else {
                const float lo = static_cast<float>(chance_val - 0.025);
                const float hi = static_cast<float>(chance_val + 0.025);
                can_delay = s_chance_drift.sample_chance_timed({lo, hi}, 60.0);
            }
        }
    }

    s_can_start_delay.exchange(can_delay, std::memory_order_seq_cst);
    s_max_delay_ms.exchange(static_cast<uint16_t>(static_cast<int32_t>(s_mod.max_delay.value)), std::memory_order_seq_cst);
}

// sub_180067AF0
void render_settings(gui::ClickGui* gui, gui::GuiRenderer* renderer) noexcept {
    gui::widgets::render_section_header(gui, renderer, "Main");
    s_mod.range.render(gui, renderer);
    s_mod.chance.render(gui, renderer);
    s_mod.max_delay.render(gui, renderer);

    gui::widgets::render_section_header(gui, renderer, "Conditions");
    s_mod.require_air.render(gui, renderer);
    s_mod.require_target.render(gui, renderer);
    s_mod.require_mouse.render(gui, renderer);
    s_mod.require_weapon.render(gui, renderer);
}

// sub_180067BB0
void visit_config(config::ConfigArchive* ar) noexcept {
    s_mod.range.visit(ar, "range");
    s_mod.chance.visit(ar, "chance");
    s_mod.max_delay.visit(ar, "max_delay");
    s_mod.require_air.visit(ar, "require_air");
    s_mod.require_target.visit(ar, "require_target");
    s_mod.require_mouse.visit(ar, "require_mouse");
    s_mod.require_weapon.visit(ar, "require_weapon");
}

// sub_180067C60
int format_hud_tag(char* out_buf) noexcept {
    return std::snprintf(out_buf, 32, "%.0fms", static_cast<double>(s_mod.max_delay.value));
}

} // namespace

events::Module* get_knockback_delay_module() noexcept {
    return &s_mod.base;
}

} // namespace slinky::modules
