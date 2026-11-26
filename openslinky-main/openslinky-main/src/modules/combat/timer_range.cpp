#include "modules/combat/timer_range.hpp"

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

namespace slinky::modules {

namespace {

constexpr const char* kModeOptions[] = {
    "Regular",
    "Legit",
};

int32_t s_pending_ticks = 0;
bool s_freezing = false;
bool s_can_simulate = false;
float s_move_forward = 0.0f;
float s_move_strafe = 0.0f;
std::atomic<bool> s_hold_outbound{false};
double s_cooldown_remaining_sec = 0.0;
double s_freeze_remaining_sec = 0.0;

void on_game_tick(void* event) noexcept;
void on_frame_update(void* event) noexcept;
void on_outgoing_packet(void* event) noexcept;
void on_module_state_changed(void* event) noexcept;
void render_settings(gui::ClickGui* gui, gui::GuiRenderer* renderer) noexcept;
void visit_config(config::ConfigArchive* ar) noexcept;
int format_hud_tag(char* out_buf) noexcept;

struct TimerRangeModule {
    events::Module base{};
    config::BoolSetting require_mouse_pressed{{}, "Mouse pressed", false, false};
    config::BoolSetting require_weapon{{}, "Holding weapon", false, false};
    config::BoolSetting require_not_using_item{{}, "Not using item", false, false};
    config::ModeSetting mode{{}, "Mode", 2, 0, kModeOptions, 0, 0, false};
    config::BoolSetting block_while_frozen{{}, "Block while frozen", false, false};
    config::FloatSetting cooldown{{}, "Cooldown", "%0.1f s", 0.0f, 0.0f, 0.1f, 5.0f, 2.0f, 2.0f};
    config::FloatSetting duration{{}, "Duration", "%0.0f ms", 1.0f, 50.0f, 1.0f, 10.0f, 3.0f, 3.0f};
    config::FloatSetting duration_legit{{}, "Duration", "%0.0f ms", 1.0f, 50.0f, 1.0f, 4.0f, 2.0f, 2.0f};
    config::FloatSetting min_dist{{}, "Minimum distance", "%0.2f blocks", 0.0f, 0.0f, 2.0f, 2.9f, 2.7f, 2.7f};

    TimerRangeModule() noexcept {
        base.name = "Timer Range";
        base.description = "Abuses timer lag to teleport you.";
        base.category = events::ModuleCategory::Combat;
        base.enabled = false;
        base.flags = events::kModuleFlagNone;
        base.keybind = 0;
        base.hold_mode = false;
        base.events[events::kEventGameTick] = {&on_game_tick, -100};
        base.events[events::kEventFrameUpdate] = {&on_frame_update, 0};
        base.events[events::kEventOutgoingPacket] = {&on_outgoing_packet, 0};
        base.events[events::kEventModuleStateChanged] = {&on_module_state_changed, 0};
        base.render_settings = &render_settings;
        base.visit_config = &visit_config;
        base.format_hud_tag = &format_hud_tag;
    }
};

TimerRangeModule s_mod{};

// sub_180065120
void begin_timer_freeze(events::GameContext* ctx) noexcept {
    jobject timer = mc::mc_get_timer(ctx->jni, ctx->minecraft);
    mc::timer_set_speed(ctx->jni, timer, 0.0f);
    s_freezing = true;

    if (s_mod.mode.value == 1) {
        s_hold_outbound.exchange(true, std::memory_order_seq_cst);
    } else if (s_mod.block_while_frozen.value) {
        jobject held_item = mc::player_get_held_item(ctx->jni, ctx->local_player);
        if (mc::is_sword_item(ctx->jni, held_item)) {
            jobject settings = mc::mc_get_game_settings(ctx->jni, ctx->minecraft);
            jobject key_use = mc::settings_get_key_use_item(ctx->jni, settings);
            mc::key_binding_set_pressed(ctx->jni, key_use, true);
            mc::key_binding_increment_press_time(ctx->jni, key_use);
        }
    }
}

// sub_1800651E0
void sample_movement_inputs(events::JniContext* jni, jobject settings) noexcept {
    jobject key_fwd = mc::settings_get_key_forward(jni, settings);
    jobject key_back = mc::settings_get_key_back(jni, settings);
    jobject key_left = mc::settings_get_key_left(jni, settings);
    jobject key_right = mc::settings_get_key_right(jni, settings);

    float forward = mc::key_binding_is_pressed(jni, key_fwd) ? 1.0f : 0.0f;
    if (mc::key_binding_is_pressed(jni, key_back)) {
        forward -= 1.0f;
    }

    float strafe = mc::key_binding_is_pressed(jni, key_left) ? 1.0f : 0.0f;
    if (mc::key_binding_is_pressed(jni, key_right)) {
        strafe -= 1.0f;
    }

    s_move_forward = forward;
    s_move_strafe = strafe;
}

// sub_180064160
void on_game_tick(void* event) noexcept {
    auto* ev = static_cast<events::ContextEvent*>(event);
    events::GameContext* ctx = ev->ctx;

    if (s_pending_ticks > 0) {
        jobject settings = mc::mc_get_game_settings(ctx->jni, ctx->minecraft);
        --s_pending_ticks;
        if (s_mod.mode.value == 0 && s_pending_ticks == 0) {
            begin_timer_freeze(ctx);
        }

        if (ctx->in_game_focus && ctx->target_tracker->target_entity != nullptr) {
            jobject mop = mc::mc_get_object_mouse_over(ctx->jni, ctx->minecraft);
            if (mop != nullptr && mc::mop_get_type_ordinal(ctx->jni, mop) == 2) {
                mc::mc_set_left_click_counter(ctx->jni, ctx->minecraft, 0);
                jobject key_attack = mc::settings_get_key_attack(ctx->jni, settings);
                mc::key_binding_increment_press_time(ctx->jni, key_attack);
            }
        }
        return;
    }

    s_can_simulate = false;
    if (!ctx->in_game_focus) {
        return;
    }

    events::TargetTracker* tracker = ctx->target_tracker;
    if (tracker->target_entity == nullptr ||
        tracker->distance_sq < 3.0 ||
        !mc::entity_is_on_ground(ctx->jni, ctx->local_player) ||
        mc::living_get_hurt_time(ctx->jni, tracker->target_entity) > 1 ||
        (s_mod.require_mouse_pressed.value && !mc::is_attack_key_down(ctx)) ||
        (s_mod.require_weapon.value && !weapons_is_holding_weapon_simple(ctx->jni, ctx->local_player)) ||
        (s_mod.require_not_using_item.value && mc::player_is_using_item(ctx->jni, ctx->local_player))) {
        return;
    }

    const math::Vec3d motion = mc::entity_get_motion(ctx->jni, ctx->local_player);
    if (motion.y > 0.0) {
        return;
    }

    jobject settings = mc::mc_get_game_settings(ctx->jni, ctx->minecraft);
    jobject key_jump = mc::settings_get_key_jump(ctx->jni, settings);
    if (mc::key_binding_is_pressed(ctx->jni, key_jump)) {
        return;
    }

    math::Vec3d local_eye = mc::entity_get_position(ctx->jni, ctx->local_player);
    if (!mc::is_version_1_7_10(ctx->jni)) {
        local_eye = math::vec3_add_eye_height(local_eye);
    }

    math::Vec3d target_eye = mc::other_player_mp_get_target_pos(ctx->jni, tracker->target_entity);
    if (!mc::is_version_1_7_10(ctx->jni)) {
        target_eye = math::vec3_add_eye_height(target_eye);
    }

    if (mc::world_has_line_of_sight(ctx->jni, ctx->world, local_eye, target_eye)) {
        sample_movement_inputs(ctx->jni, settings);
        s_can_simulate = s_move_forward > 0.0f;
    }
}

// sub_180064420
void on_frame_update(void* event) noexcept {
    auto* ev = static_cast<events::FrameUpdateEvent*>(event);
    events::GameContext* ctx = ev->ctx;
    jobject timer = mc::mc_get_timer(ctx->jni, ctx->minecraft);

    if (s_freezing) {
        s_freeze_remaining_sec -= ev->delta_time;
        if (s_freeze_remaining_sec > 0.0) {
            mc::timer_set_speed(ctx->jni, timer, 0.0f);
            return;
        }

        jobject cur_timer = mc::mc_get_timer(ctx->jni, ctx->minecraft);
        mc::timer_set_speed(ctx->jni, cur_timer, 1.0f);
        s_freezing = false;
        s_hold_outbound.exchange(false, std::memory_order_seq_cst);

        if (s_mod.mode.value != 1 && s_mod.block_while_frozen.value) {
            jobject held_item = mc::player_get_held_item(ctx->jni, ctx->local_player);
            if (mc::is_sword_item(ctx->jni, held_item)) {
                jobject settings = mc::mc_get_game_settings(ctx->jni, ctx->minecraft);
                jobject key_use = mc::settings_get_key_use_item(ctx->jni, settings);
                mc::key_binding_set_pressed(ctx->jni, key_use, false);
            }
        }

        if (s_mod.mode.value == 1) {
            mc::timer_set_elapsed_partial_ticks(ctx->jni, timer, static_cast<float>(s_pending_ticks));
        }
    }

    if (s_cooldown_remaining_sec > 0.0) {
        s_cooldown_remaining_sec -= ev->delta_time;
        return;
    }

    if (!s_can_simulate || !ctx->in_game_focus) {
        return;
    }

    events::TargetTracker* tracker = ctx->target_tracker;
    if (tracker->target_entity == nullptr) {
        return;
    }

    math::Vec3d sim_motion = mc::entity_get_motion(ctx->jni, ctx->local_player);
    math::Vec3d sim_eye = mc::entity_get_position(ctx->jni, ctx->local_player);
    if (!mc::is_version_1_7_10(ctx->jni)) {
        sim_eye = math::vec3_add_eye_height(sim_eye);
    }

    math::Vec3d target_pos = mc::entity_get_position(ctx->jni, tracker->target_entity);
    math::Vec3d target_server_pos = mc::other_player_mp_get_target_pos(ctx->jni, tracker->target_entity);
    int32_t interp_steps = mc::other_player_mp_get_pos_rot_increments(ctx->jni, tracker->target_entity);
    const math::Vec2f rot = mc::entity_get_rotation(ctx->jni, ctx->local_player);

    constexpr double kDegToRad = 0.017453292519943295;
    constexpr double kNegPiF = -3.1415927410125732;
    const float yaw_rad = static_cast<float>(kNegPiF - static_cast<double>(rot.x) * kDegToRad);
    const float pitch_rad = static_cast<float>(static_cast<double>(-rot.y) * kDegToRad);
    const float sin_yaw = std::sin(yaw_rad);
    const float cos_yaw = std::cos(yaw_rad);
    const float sin_pitch = std::sin(pitch_rad);
    const float neg_cos_pitch = -std::cos(pitch_rad);

    const float max_dur = (s_mod.mode.value == 0) ? s_mod.duration.value : s_mod.duration_legit.value;
    const int32_t max_ticks = static_cast<int32_t>(max_dur);
    if (max_ticks <= 0) {
        return;
    }

    const math::Vec3d look_dir{
        static_cast<double>(sin_yaw * neg_cos_pitch),
        static_cast<double>(sin_pitch),
        static_cast<double>(cos_yaw * neg_cos_pitch)};
    const math::Vec3d inv_reach_ray = math::vec3_rcp(math::vec3_scale(look_dir, 3.0));

    for (int32_t sim_tick = 1; sim_tick <= max_ticks; ++sim_tick) {
        const math::AABB target_box{
            math::vec3_sub(target_pos, {0.4, 0.1, 0.4}),
            math::vec3_add(target_pos, {0.4, 1.9, 0.4})};

        if (math::aabb_ray_intersects_rcp(target_box, sim_eye, inv_reach_ray)) {
            const math::Vec3d closest_pt = math::aabb_closest_point(target_box, sim_eye);
            const double hit_dist_sq = math::vec3_dist_sq(sim_eye, closest_pt);
            if (hit_dist_sq > 0.0) {
                if (static_cast<double>(s_mod.min_dist.value * s_mod.min_dist.value) <= hit_dist_sq) {
                    if (s_mod.mode.value == 1) {
                        begin_timer_freeze(ctx);
                    } else {
                        mc::timer_set_elapsed_partial_ticks(ctx->jni, timer, static_cast<float>(sim_tick));
                    }
                    s_pending_ticks = sim_tick;
                    s_freeze_remaining_sec =
                        static_cast<double>(static_cast<float>(sim_tick) - ev->partial_ticks) * 0.05;
                    s_cooldown_remaining_sec = static_cast<double>(s_mod.cooldown.value);
                }
                return;
            }
        }

        const float move_factor = mc::living_get_land_movement_factor(ctx->jni, ctx->local_player);
        const math::Vec3d strafe_accel =
            mc::compute_strafe_motion(rot, s_move_forward, s_move_strafe, move_factor);
        sim_motion = math::vec3_add(sim_motion, strafe_accel);
        sim_eye = math::vec3_add(sim_eye, sim_motion);
        sim_motion = math::vec3_scale(sim_motion, 0.546);

        if (s_mod.mode.value == 1) {
            const double horiz_speed = std::hypot(sim_motion.x, sim_motion.z);
            const math::Vec3d delta = math::vec3_sub(sim_eye, target_server_pos);
            const double horiz_dist = std::hypot(delta.x, delta.z);
            math::Vec3d extrap = math::vec3_scale(delta, horiz_speed / horiz_dist);
            extrap.y = 0.0;
            target_server_pos = math::vec3_add(target_server_pos, extrap);
        }

        if (interp_steps > 0) {
            const math::Vec3d step_vec =
                math::vec3_scale(math::vec3_sub(target_server_pos, target_pos), 1.0 / static_cast<double>(interp_steps));
            target_pos = math::vec3_add(target_pos, step_vec);
            --interp_steps;
        }
    }
}

// sub_180064E80
void on_outgoing_packet(void* event) noexcept {
    auto* ev = static_cast<events::NettyPacketEvent*>(event);
    if (s_hold_outbound.load(std::memory_order_acquire)) {
        ev->action = 1;
    }
}

// sub_180064EA0
void on_module_state_changed(void* event) noexcept {
    auto* ev = static_cast<events::ModuleStateChangedEvent*>(event);
    if (ev->module == &s_mod.base && !ev->enabled) {
        jobject timer = mc::mc_get_timer(ev->ctx->jni, ev->ctx->minecraft);
        mc::timer_set_speed(ev->ctx->jni, timer, 1.0f);
        s_freezing = false;
        s_pending_ticks = 0;
        s_hold_outbound.exchange(false, std::memory_order_seq_cst);
    }
}

// sub_180064F10
void render_settings(gui::ClickGui* gui, gui::GuiRenderer* renderer) noexcept {
    gui::widgets::render_section_header(gui, renderer, "Main");
    s_mod.mode.render(gui, renderer);
    s_mod.min_dist.render(gui, renderer);
    if (s_mod.mode.value == 0) {
        s_mod.duration.render(gui, renderer);
    } else {
        s_mod.duration_legit.render(gui, renderer);
    }
    s_mod.cooldown.render(gui, renderer);
    if (s_mod.mode.value == 0) {
        s_mod.block_while_frozen.render(gui, renderer);
    }

    gui::widgets::render_section_header(gui, renderer, "Conditions");
    s_mod.require_mouse_pressed.render(gui, renderer);
    s_mod.require_weapon.render(gui, renderer);
    s_mod.require_not_using_item.render(gui, renderer);
}

// sub_180065000
void visit_config(config::ConfigArchive* ar) noexcept {
    s_mod.mode.visit(ar, "mode");
    s_mod.min_dist.visit(ar, "min_dist");
    s_mod.duration.visit(ar, "duration");
    s_mod.duration_legit.visit(ar, "duration_legit");
    s_mod.cooldown.visit(ar, "cooldown");
    s_mod.block_while_frozen.visit(ar, "block_while_frozen");
    s_mod.require_mouse_pressed.visit(ar, "require_mouse_pressed");
    s_mod.require_weapon.visit(ar, "require_weapon");
    s_mod.require_not_using_item.visit(ar, "require_not_using_item");
}

// sub_1800650E0
int format_hud_tag(char* out_buf) noexcept {
    return std::snprintf(out_buf, 32, "%s", kModeOptions[s_mod.mode.value]);
}

} // namespace

// sub_180064130
bool timer_range_is_manipulating() noexcept {
    return s_mod.base.enabled && (s_freezing || s_pending_ticks > 0);
}

events::Module* get_timer_range_module() noexcept {
    return &s_mod.base;
}

} // namespace slinky::modules
