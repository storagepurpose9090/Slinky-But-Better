#include "modules/combat/velocity.hpp"

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

constexpr const char* kModeOptions[] = {
    "Regular",
    "Jump",
    "Reduce",
    "Ignore",
};

constexpr const char* kIgnoreVerticalOptions[] = {
    "Never",
    "Only in air",
    "Always",
};

std::atomic<bool> s_teleported_flag{false};
bool s_has_saved_post_motion = false;
math::Vec3d s_saved_post_motion{};
random::DriftNoise s_chance_drift{};
std::atomic<int32_t> s_local_entity_id{0};
std::atomic<uint8_t> s_pending_kb{0};
math::Vec3d s_saved_pre_attack_motion{};
bool s_reduced_this_tick = false;
int32_t s_reduce_delay_counter = 0;
bool s_jump_key_overridden = false;

void on_game_tick(void* event) noexcept;
void on_incoming_packet(void* event) noexcept;
void on_module_state_changed(void* event) noexcept;
void on_pre_attack_or_use_entity(void* event) noexcept;
void on_post_attack_or_use_entity(void* event) noexcept;
void on_mid_living_update(void* event) noexcept;
void on_post_living_update(void* event) noexcept;
void render_settings(gui::ClickGui* gui, gui::GuiRenderer* renderer) noexcept;
void visit_config(config::ConfigArchive* ar) noexcept;
int format_hud_tag(char* out_buf) noexcept;

struct VelocityModule {
    events::Module base{};
    config::ModeSetting mode{{}, "Mode", 4, 0, kModeOptions, 0, 0, false};
    config::FloatSetting ignore_chance{{}, "Chance", "%.0f%%", 1.0f, 0.0f, 0.0f, 100.0f, 100.0f, 100.0f};
    config::BoolSetting require_forward{{}, "Moving forward", false, false};
    config::BoolSetting require_mouse{{}, "Mouse pressed", false, false};
    config::BoolSetting require_weapon{{}, "Holding weapon", false, false};
    config::BoolSetting require_target{{}, "Looking at player", false, false};
    config::ModeSetting ignore_vertical{{}, "Ignore vertical", 3, 0, kIgnoreVerticalOptions, 0, 0, false};
    config::BoolSetting reduce_multiple_per_tick{{}, "Allow for double-clicks", false, false};
    config::FloatSetting reduce_chance{{}, "Chance", "%.0f%%", 1.0f, 0.0f, 0.0f, 100.0f, 100.0f, 100.0f};
    config::FloatSetting reduce_delay{{}, "Reduce delay", "%.0f tick(s)", 1.0f, 0.0f, 0.0f, 10.0f, 0.0f, 0.0f};
    config::FloatSetting reduce_amount{{}, "Reduce to", "%.0f%%", 0.01f, 100.0f, -1.0f, 1.0f, 1.0f, 1.0f};
    config::FloatSetting fov{
        {},
        "FOV",
        "%.0f\xC2\xB0",
        0.0f,
        static_cast<float>(360.0 / math::kPi),
        0.0f,
        static_cast<float>(math::kPi),
        static_cast<float>(math::kPi),
        static_cast<float>(math::kPi)};
    config::FloatSetting jump_chance{{}, "Chance", "%.0f%%", 1.0f, 0.0f, 0.0f, 100.0f, 100.0f, 100.0f};
    config::BoolSetting require_sprinting_jump{{}, "Sprinting", true, true};

    VelocityModule() noexcept {
        base.name = "Velocity";
        base.description = "Reduces the amount of knockback you take.";
        base.category = events::ModuleCategory::Combat;
        base.enabled = false;
        base.flags = events::kModuleFlagNone;
        base.keybind = 0;
        base.hold_mode = false;
        base.events[events::kEventGameTick] = {&on_game_tick, 0};
        base.events[events::kEventIncomingPacket] = {&on_incoming_packet, -1000};
        base.events[events::kEventModuleStateChanged] = {&on_module_state_changed, 0};
        base.events[events::kEventPreAttackOrUseEntity] = {&on_pre_attack_or_use_entity, 0};
        base.events[events::kEventPostAttackOrUseEntity] = {&on_post_attack_or_use_entity, 100};
        base.events[events::kEventMidLivingUpdate] = {&on_mid_living_update, 100};
        base.events[events::kEventPostLivingUpdate] = {&on_post_living_update, 0};
        base.render_settings = &render_settings;
        base.visit_config = &visit_config;
        base.format_hud_tag = &format_hud_tag;
    }
};

VelocityModule s_mod{};

bool roll_percent_chance_drift(float percent) noexcept {
    if (percent > 99.0f) {
        return true;
    }
    const float unit_f = static_cast<float>(static_cast<double>(percent) * 0.01);
    const double unit = static_cast<double>(unit_f);
    return s_chance_drift.sample_chance_timed(
        {static_cast<float>(unit - 0.05), static_cast<float>(unit + 0.05)},
        60.0);
}

// sub_180052390
bool check_velocity_conditions(events::GameContext* ctx) noexcept {
    if (s_mod.require_forward.value &&
        mc::living_get_move_forward(ctx->jni, ctx->local_player) <= 0.0f) {
        return false;
    }
    if ((s_mod.require_mouse.value && !mc::is_attack_key_down(ctx)) ||
        (s_mod.require_weapon.value && !weapons_is_holding_weapon_simple(ctx->jni, ctx->local_player))) {
        return false;
    }
    if (!s_mod.require_target.value) {
        return true;
    }

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
                return true;
            }
        }
    }
    return false;
}

// sub_180051890
void on_game_tick(void* event) noexcept {
    if (s_mod.mode.value != 3) {
        return;
    }

    auto* ev = static_cast<events::ContextEvent*>(event);
    events::GameContext* ctx = ev->ctx;
    if (!ctx->in_game_focus) {
        return;
    }

    if (s_teleported_flag.load(std::memory_order_acquire)) {
        s_teleported_flag.exchange(false, std::memory_order_seq_cst);
        return;
    }

    if (s_has_saved_post_motion &&
        check_velocity_conditions(ctx) &&
        roll_percent_chance_drift(s_mod.ignore_chance.value)) {
        math::Vec3d motion = mc::entity_get_motion(ctx->jni, ctx->local_player);
        motion.x = s_saved_post_motion.x;
        if (s_mod.ignore_vertical.value == 2 ||
            (s_mod.ignore_vertical.value == 1 && !mc::entity_is_on_ground(ctx->jni, ctx->local_player))) {
            motion.y = s_saved_post_motion.y;
        }
        motion.z = s_saved_post_motion.z;
        mc::entity_set_motion(ctx->jni, ctx->local_player, motion);
        s_has_saved_post_motion = false;
    }
}

// sub_1800519E0
void on_incoming_packet(void* event) noexcept {
    auto* ev = static_cast<events::NettyPacketEvent*>(event);
    if (ev->action != 0) {
        return;
    }

    jobject s12 = mc::as_packet_s12_entity_velocity(ev->jni, ev->packet);
    if (s12 != nullptr) {
        if (mc::packet_s12_get_entity_id(ev->jni, s12) == s_local_entity_id.load(std::memory_order_acquire)) {
            s_pending_kb.exchange(1u, std::memory_order_seq_cst);
        }
        return;
    }

    jobject s27 = mc::as_packet_s27_explosion(ev->jni, ev->packet);
    if (s27 != nullptr) {
        const float mx = mc::packet_s27_get_motion_x(ev->jni, s27);
        const float mz = mc::packet_s27_get_motion_z(ev->jni, s27);
        const uint8_t has_kb = (static_cast<double>(mx * mx + mz * mz) > 0.05) ? 1u : 0u;
        s_pending_kb.exchange(has_kb, std::memory_order_seq_cst);
        return;
    }

    if (mc::as_packet_s08_player_pos_look(ev->jni, ev->packet) != nullptr) {
        s_teleported_flag.exchange(true, std::memory_order_seq_cst);
    }
}

// sub_180051AB0
void on_module_state_changed(void* event) noexcept {
    auto* ev = static_cast<events::ModuleStateChangedEvent*>(event);
    if (ev->module == &s_mod.base) {
        s_has_saved_post_motion = false;
    }
}

// sub_180051AD0
void on_pre_attack_or_use_entity(void* event) noexcept {
    auto* ev = static_cast<events::AttackOrUseEntityEvent*>(event);
    if (ev->is_attack && ev->ctx->local_player != nullptr) {
        s_saved_pre_attack_motion = mc::entity_get_motion(ev->ctx->jni, ev->ctx->local_player);
    }
}

// sub_180051B20
void on_post_attack_or_use_entity(void* event) noexcept {
    auto* ev = static_cast<events::AttackOrUseEntityEvent*>(event);
    if (s_mod.mode.value != 2 ||
        (s_reduced_this_tick && !s_mod.reduce_multiple_per_tick.value) ||
        !ev->is_attack) {
        return;
    }

    events::GameContext* ctx = ev->ctx;
    if (ctx->local_player == nullptr || !mc::target_tracker_is_moving_away(ctx->target_tracker)) {
        return;
    }

    if (s_mod.require_forward.value &&
        mc::living_get_move_forward(ctx->jni, ctx->local_player) <= 0.0f) {
        return;
    }
    if (s_mod.require_weapon.value &&
        !weapons_is_holding_weapon_simple(ctx->jni, ctx->local_player)) {
        return;
    }

    const int32_t ticks_existed = mc::entity_get_ticks_existed(ctx->jni, ctx->local_player);
    if (ticks_existed == mc::living_get_last_attacker_time(ctx->jni, ctx->local_player)) {
        s_saved_pre_attack_motion.x *= 0.6;
        s_saved_pre_attack_motion.z *= 0.6;
        mc::entity_set_motion(ctx->jni, ctx->local_player, s_saved_pre_attack_motion);
        s_reduced_this_tick = true;
    }
}

// sub_180051C40
void on_mid_living_update(void* event) noexcept {
    s_reduced_this_tick = false;
    if (s_mod.mode.value == 2) {
        return;
    }

    auto* ev = static_cast<events::ContextEvent*>(event);
    events::GameContext* ctx = ev->ctx;
    if (!ctx->in_game_focus) {
        return;
    }

    s_local_entity_id.exchange(mc::entity_get_id(ctx->jni, ctx->local_player), std::memory_order_seq_cst);

    if (s_mod.mode.value == 1) {
        if (s_jump_key_overridden) {
            jobject settings = mc::mc_get_game_settings(ctx->jni, ctx->minecraft);
            jobject key_jump = mc::settings_get_key_jump(ctx->jni, settings);
            mc::key_binding_set_pressed(ctx->jni, key_jump, mc::key_binding_is_physically_down(ctx->jni, key_jump));
            s_jump_key_overridden = false;
        } else if (check_velocity_conditions(ctx) &&
                   roll_percent_chance_drift(s_mod.jump_chance.value) &&
                   (!s_mod.require_sprinting_jump.value || mc::entity_is_sprinting(ctx->jni, ctx->local_player)) &&
                   mc::entity_is_on_ground(ctx->jni, ctx->local_player)) {
            const math::Vec3d motion = mc::entity_get_motion(ctx->jni, ctx->local_player);
            if (motion.y > 0.0) {
                jobject jump_boost = mc::potion_get_by_id(ctx->jni, 8u);
                if (mc::living_get_active_potion_effect(ctx->jni, ctx->local_player, jump_boost) == nullptr) {
                    jobject settings = mc::mc_get_game_settings(ctx->jni, ctx->minecraft);
                    jobject key_jump = mc::settings_get_key_jump(ctx->jni, settings);
                    if (!mc::key_binding_is_pressed(ctx->jni, key_jump)) {
                        mc::key_binding_set_pressed(ctx->jni, key_jump, true);
                        s_jump_key_overridden = true;
                    }
                }
            }
        }
        return;
    }

    if (s_mod.mode.value != 0) {
        return;
    }

    int32_t delay = s_reduce_delay_counter;
    if (delay <= 0) {
        uint8_t expected = 1u;
        if (!s_pending_kb.compare_exchange_strong(expected, 0u, std::memory_order_seq_cst)) {
            return;
        }
        if (!check_velocity_conditions(ctx) || !roll_percent_chance_drift(s_mod.reduce_chance.value)) {
            return;
        }

        const math::Vec3d motion = mc::entity_get_motion(ctx->jni, ctx->local_player);
        const double horiz_speed = std::hypot(motion.x, motion.z);
        if (horiz_speed == 0.0) {
            return;
        }

        const math::Vec2f rot = mc::entity_get_rotation(ctx->jni, ctx->local_player);
        const float back_rad = static_cast<float>(static_cast<double>(rot.x) * (-math::kPi) / 180.0 - math::kPi);
        const double dot =
            static_cast<double>(std::sin(back_rad)) * motion.x +
            static_cast<double>(std::cos(back_rad)) * motion.z;
        if (static_cast<double>(s_mod.fov.value) < std::acos(dot / horiz_speed)) {
            return;
        }

        delay = static_cast<int32_t>(s_mod.reduce_delay.value);
        s_reduce_delay_counter = delay;
    }

    if (delay > 0) {
        s_reduce_delay_counter = delay - 1;
        if (delay != 1) {
            return;
        }
    }

    math::Vec3d cur_motion = mc::entity_get_motion(ctx->jni, ctx->local_player);
    const double scale = static_cast<double>(s_mod.reduce_amount.value);
    cur_motion.x *= scale;
    cur_motion.z *= scale;
    mc::entity_set_motion(ctx->jni, ctx->local_player, cur_motion);
}

// sub_180052020
void on_post_living_update(void* event) noexcept {
    auto* ev = static_cast<events::ContextEvent*>(event);
    events::GameContext* ctx = ev->ctx;
    if (ctx->local_player != nullptr) {
        s_saved_post_motion = mc::entity_get_motion(ctx->jni, ctx->local_player);
        s_has_saved_post_motion = true;
    }
}

// sub_180052070
void render_settings(gui::ClickGui* gui, gui::GuiRenderer* renderer) noexcept {
    gui::widgets::render_section_header(gui, renderer, "Main");
    s_mod.mode.render(gui, renderer);
    switch (s_mod.mode.value) {
        case 0:
            s_mod.reduce_chance.render(gui, renderer);
            s_mod.fov.render(gui, renderer);
            s_mod.reduce_amount.render(gui, renderer);
            s_mod.reduce_delay.render(gui, renderer);
            break;
        case 1:
            s_mod.jump_chance.render(gui, renderer);
            break;
        case 2:
            s_mod.reduce_multiple_per_tick.render(gui, renderer);
            break;
        case 3:
            s_mod.ignore_vertical.render(gui, renderer);
            s_mod.ignore_chance.render(gui, renderer);
            break;
        default:
            break;
    }

    gui::widgets::render_section_header(gui, renderer, "Conditions");
    if (s_mod.mode.value != 2) {
        s_mod.require_target.render(gui, renderer);
        s_mod.require_mouse.render(gui, renderer);
    }
    s_mod.require_forward.render(gui, renderer);
    s_mod.require_weapon.render(gui, renderer);
    if (s_mod.mode.value == 1) {
        s_mod.require_sprinting_jump.render(gui, renderer);
    }
}

// sub_1800521D0
void visit_config(config::ConfigArchive* ar) noexcept {
    s_mod.mode.visit(ar, "mode");
    s_mod.reduce_chance.visit(ar, "reduce_chance");
    s_mod.fov.visit(ar, "fov");
    s_mod.reduce_amount.visit(ar, "reduce_amount");
    s_mod.reduce_delay.visit(ar, "reduce_delay");
    s_mod.jump_chance.visit(ar, "jump_chance");
    s_mod.reduce_multiple_per_tick.visit(ar, "reduce_multiple_per_tick");
    s_mod.ignore_vertical.visit(ar, "ignore_vertical");
    s_mod.ignore_chance.visit(ar, "ignore_chance");
    s_mod.require_target.visit(ar, "require_target");
    s_mod.require_mouse.visit(ar, "require_mouse");
    s_mod.require_forward.visit(ar, "require_forward");
    s_mod.require_weapon.visit(ar, "require_weapon");
    s_mod.require_sprinting_jump.visit(ar, "require_sprinting_jump");
}

// sub_180052320
int format_hud_tag(char* out_buf) noexcept {
    if (s_mod.mode.value != 0) {
        return std::snprintf(out_buf, 32, "%s", kModeOptions[s_mod.mode.value]);
    }
    return std::snprintf(out_buf, 32, "%.0f%%", static_cast<double>(s_mod.reduce_amount.value * 100.0f));
}

} // namespace

events::Module* get_velocity_module() noexcept {
    return &s_mod.base;
}

} // namespace slinky::modules
