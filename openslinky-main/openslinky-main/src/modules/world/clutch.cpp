#include "modules/world/clutch.hpp"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>

#include "config/settings.hpp"
#include "events/events.hpp"
#include "events/module_manager.hpp"
#include "gui/widgets.hpp"
#include "minecraft/game_helpers.hpp"
#include "minecraft/mc_wrappers.hpp"
#include "util/math.hpp"
#include "util/random.hpp"

namespace slinky::modules {

namespace {

constexpr const char* kSelectBlocksOptions[3] = {
    "No",
    "On depletion",
    "Always",
};

// Cumulative entry counts per Manhattan search radius 0..7 (@ 0x180173150)
constexpr uint16_t kRangeEntryCounts[8] = {
    1, 6, 19, 43, 79, 123, 163, 167,
};

// 167 packed (dx+8) | ((dy+8) << 4) | ((dz+8) << 8) block offsets ordered by Manhattan shell (@ 0x180173160)
constexpr uint16_t kPackedSearchOffsets[167] = {
    0x888, 0x887, 0x878, 0x788, 0x988, 0x889, 0x886, 0x877, 0x787, 0x987, 0x868, 0x778, 0x978, 0x688, 0xa88, 0x879,
    0x789, 0x989, 0x88a, 0x885, 0x876, 0x786, 0x986, 0x867, 0x777, 0x977, 0x687, 0xa87, 0x768, 0x968, 0x678, 0xa78,
    0x588, 0xb88, 0x869, 0x779, 0x979, 0x689, 0xa89, 0x87a, 0x78a, 0x98a, 0x88b, 0x884, 0x875, 0x785, 0x985, 0x866,
    0x776, 0x976, 0x686, 0xa86, 0x767, 0x967, 0x677, 0xa77, 0x587, 0xb87, 0x668, 0xa68, 0x578, 0xb78, 0x488, 0xc88,
    0x769, 0x969, 0x679, 0xa79, 0x589, 0xb89, 0x86a, 0x77a, 0x97a, 0x68a, 0xa8a, 0x87b, 0x78b, 0x98b, 0x88c, 0x874,
    0x784, 0x984, 0x865, 0x775, 0x975, 0x685, 0xa85, 0x766, 0x966, 0x676, 0xa76, 0x586, 0xb86, 0x667, 0xa67, 0x577,
    0xb77, 0x487, 0xc87, 0x568, 0xb68, 0x478, 0xc78, 0x669, 0xa69, 0x579, 0xb79, 0x489, 0xc89, 0x76a, 0x96a, 0x67a,
    0xa7a, 0x58a, 0xb8a, 0x86b, 0x77b, 0x97b, 0x68b, 0xa8b, 0x87c, 0x78c, 0x98c, 0x774, 0x974, 0x684, 0xa84, 0x765,
    0x965, 0x675, 0xa75, 0x585, 0xb85, 0x666, 0xa66, 0x576, 0xb76, 0x486, 0xc86, 0x567, 0xb67, 0x477, 0xc77, 0x569,
    0xb69, 0x479, 0xc79, 0x66a, 0xa6a, 0x57a, 0xb7a, 0x48a, 0xc8a, 0x76b, 0x96b, 0x67b, 0xa7b, 0x58b, 0xb8b, 0x77c,
    0x97c, 0x68c, 0xa8c, 0x575, 0xb75, 0x57b, 0xb7b,
};

constexpr double kMaxPlaceReachSq = 20.1601; // 4.49^2

void on_game_tick(void* event) noexcept;
void on_rotation_spoof_query(void* event) noexcept;
void on_post_living_update(void* event) noexcept;
void render_settings(gui::ClickGui* gui, gui::GuiRenderer* renderer) noexcept;
void visit_config(config::ConfigArchive* archive) noexcept;

struct ClutchState {
    events::Module base{};

    // Settings (@ 0x180312970 .. 0x180312CD7)
    config::FloatSetting click_speed{{}, "Click speed", "%.1f CPS", 0.1f, 0.0f, 10.0f, 20.0f, 18.0f, 18.0f};
    config::FloatSetting snapback_duration{{}, "Duration", "%.0f ms", 1.0f, 50.0f, 1.0f, 10.0f, 4.0f, 4.0f};
    config::FloatSetting snapback_delay{{}, "Delay", "%.0f ms", 1.0f, 50.0f, 1.0f, 10.0f, 6.0f, 6.0f};
    config::FloatSetting randomization{{}, "Randomization", "%.0f%%", 0.01f, 100.0f, 0.0f, 1.0f, 0.5f, 0.5f};
    config::FloatSetting aim_speed{{}, "Base speed", "%.0f \xC2\xB0/s", 1.0f, 20.0f, 20.0f, 180.0f, 30.0f, 30.0f};
    config::FloatSetting aim_mult{{}, "Acceleration", "%.3fx", 0.0f, 0.0f, 0.3f, 1.0f, 0.5f, 0.5f};
    config::FloatSetting aim_mult_strength{{}, "Acceleration strength", "%.0f%%", 0.0f, 100.0f, 0.0f, 1.0f, 0.3f, 0.3f};
    config::BoolSetting snapback_instant_on_jump{{}, "Keep jump direction", false, false, {}};
    config::BoolSetting require_air{{}, "In air", true, true, {}};
    config::BoolSetting require_damage{{}, "Damaged", false, false, {}};
    config::BoolSetting require_backwards{{}, "Moving backwards", false, false, {}};
    config::FloatSetting min_height{{}, "Minimum height", "%.0f block(s)", 1.0f, 0.0f, 1.0f, 10.0f, 4.0f, 4.0f};
    config::FloatSetting range{{}, "Range", "%.0f block(s)", 1.0f, 0.0f, 1.0f, 7.0f, 7.0f, 7.0f};
    config::FloatSetting max_aim_dist{{}, "FOV", "%.0f\xC2\xB0", 0.0f, 0.0f, 50.0f, 360.0f, 360.0f, 360.0f};
    config::BoolSetting sideways_only{{}, "Only place sideways", false, false, {}};
    config::BoolSetting aim_multipoint{{}, "Multipoint", false, false, {}};
    config::ModeSetting select_blocks{{}, "Select blocks", 3, 0, kSelectBlocksOptions, 2, 2, false, {}};
    config::BoolSetting snapback_disable_after{{}, "Disable afterwards", false, false, {}};

    // Runtime state (@ 0x180321C88 .. 0x180321D58)
    float click_cooldown_ticks = 0.0f;
    bool clutch_active = false;
    mc::Vec3i target_block{0, 0, 0};
    bool place_on_top_face = false;
    bool spoof_rotation_active = false;
    int32_t ticks_since_damage = 0;
    int32_t post_clutch_jump_window = 0;
    int32_t snapback_delay_ticks = 0;
    bool pending_auto_disable = false;
    int32_t snapback_duration_ticks = 0;
    math::Vec3d aim_point{0.0, 0.0, 0.0};
    random::DriftNoise vertical_yaw_drift{};
    random::DriftNoise yaw_speed_drift{};
    random::DriftNoise pitch_speed_drift{};
    double jump_snap_yaw_accum = 0.0;
    random::DriftNoise top_x_drift{};
    random::DriftNoise top_z_drift{};
    random::DriftNoise edge_inset_drift{};
    random::DriftNoise side_x_drift{};
    random::DriftNoise side_z_drift{};

    ClutchState() noexcept {
        base.name = "Clutch";
        base.description = "Clutches for you when falling.";
        base.category = events::ModuleCategory::Block;
        base.enabled = false;
        base.flags = events::kModuleFlagNone;
        base.keybind = 0;
        base.hold_mode = false;
        base.events[events::kEventGameTick] = {on_game_tick, 0, 0};
        base.events[events::kEventRotationSpoofQuery] = {on_rotation_spoof_query, -100, 0};
        base.events[events::kEventPostLivingUpdate] = {on_post_living_update, -1000, 0};
        base.render_settings = render_settings;
        base.visit_config = visit_config;
        base.format_hud_tag = nullptr;
    }
};

ClutchState g_clutch{};

// Computes humanized side-face aim point and angular distance for a candidate block offset (sub_18006BD70)
float evaluate_side_aim_point(
    const math::Vec3d& eye_pos,
    math::Vec2f cur_rot,
    const math::Vec3d& anchor_pos,
    const mc::Vec3i& base_block,
    const mc::Vec3i& offset,
    math::Vec3d* out_aim_point) noexcept
{
    const float rand_val = g_clutch.randomization.value;
    const float inset_noise = g_clutch.edge_inset_drift.sample_timed({0.0f, 0.05f}, 1.0);
    const float x_jitter = g_clutch.side_x_drift.sample_timed({-0.01f, 0.01f}, 0.5);
    const float z_jitter = g_clutch.side_z_drift.sample_timed({-0.01f, 0.01f}, 0.5);

    double offset_x = static_cast<double>(x_jitter * rand_val);
    double offset_z = static_cast<double>(z_jitter * rand_val);

    if (!g_clutch.aim_multipoint.value) {
        const double frac_x = std::fabs(std::fmod(anchor_pos.x, 1.0));
        const double frac_z = std::fabs(std::fmod(anchor_pos.z, 1.0));
        const double grid_dist_x = (frac_x > 0.5) ? std::fabs(frac_x - 1.0) : frac_x;
        const double grid_dist_z = (frac_z > 0.5) ? std::fabs(frac_z - 1.0) : frac_z;

        const double dir_x = (g_clutch.aim_point.x < anchor_pos.x) ? -1.0 : 1.0;
        const double dir_z = (g_clutch.aim_point.z < anchor_pos.z) ? -1.0 : 1.0;
        offset_x += dir_x * grid_dist_x;
        offset_z += dir_z * grid_dist_z;
    }

    const int32_t dx = offset.x;
    const int32_t dy = offset.y;
    const int32_t dz = offset.z;

    const uint32_t abs_dx = static_cast<uint32_t>(dx > 0 ? dx : -dx);
    const uint32_t abs_dz = static_cast<uint32_t>(dz > 0 ? dz : -dz);
    const double face_x = static_cast<double>(static_cast<uint32_t>(dx) >> 31);
    const double face_z = static_cast<double>(static_cast<uint32_t>(dz) >> 31);

    const double inset = static_cast<double>(rand_val * inset_noise) + 0.1;
    double min_x_off = inset;
    double max_x_off = 1.0 - inset;
    double min_z_off = face_z;
    double max_z_off = face_z;

    if (abs_dx > abs_dz) {
        min_x_off = face_x;
        max_x_off = face_x;
        min_z_off = inset;
        max_z_off = 1.0 - inset;
    }

    const int32_t block_x = base_block.x + dx;
    const int32_t block_y = base_block.y + dy;
    const int32_t block_z = base_block.z + dz;
    const double horiz_manhattan = static_cast<double>(std::abs(dx) + std::abs(dz));

    const double aim_x = static_cast<double>(math::clampf(
        static_cast<float>(static_cast<double>(block_x) + min_x_off),
        static_cast<float>(static_cast<double>(block_x) + max_x_off),
        static_cast<float>(anchor_pos.x + offset_x)));

    const double y_raise = static_cast<double>(math::clampf(
        0.0f,
        0.65f,
        static_cast<float>(horiz_manhattan * 0.25)));
    const double aim_y = static_cast<double>(block_y) + 0.25 + y_raise;

    const double aim_z = static_cast<double>(math::clampf(
        static_cast<float>(static_cast<double>(block_z) + min_z_off),
        static_cast<float>(static_cast<double>(block_z) + max_z_off),
        static_cast<float>(anchor_pos.z + offset_z)));

    const math::Vec3d target_pt = math::make_vec3(aim_x, aim_y, aim_z);
    const math::Vec2f desired_rot = math::calc_yaw_pitch(eye_pos, target_pt);
    const math::Vec2f rot_diff = math::vec2_sub(desired_rot, cur_rot);
    const float yaw_err = std::fabs(math::wrap_degrees_180(rot_diff.x));
    const float pitch_err = std::fabs(rot_diff.y);

    *out_aim_point = target_pt;
    return yaw_err + pitch_err;
}

// Clicks use_item when crosshair raycast hits the selected clutch block face (sub_18006A6E0)
void on_game_tick(void* event) noexcept {
    float cd = g_clutch.click_cooldown_ticks;
    if (cd > 0.0f) {
        cd -= 1.0f;
        g_clutch.click_cooldown_ticks = cd;
        if (cd > 0.0f) {
            return;
        }
    }

    g_clutch.click_cooldown_ticks = cd + (20.0f / g_clutch.click_speed.value);

    events::GameContext* ctx = static_cast<events::ContextEvent*>(event)->ctx;
    if (!ctx->in_game_focus || !g_clutch.clutch_active) {
        return;
    }

    events::JniContext* jni = ctx->jni;
    jobject mop = mc::mc_get_object_mouse_over(jni, ctx->minecraft);
    if (mop == nullptr || mc::mop_get_type_ordinal(jni, mop) != 1) {
        return;
    }

    const mc::Vec3i hit_pos = mc::mop_get_block_pos_vec3i(jni, mop);
    if (hit_pos.x == g_clutch.target_block.x &&
        hit_pos.y == g_clutch.target_block.y &&
        hit_pos.z == g_clutch.target_block.z) {
        const bool is_non_top_face = (mc::mop_get_side_hit(jni, mop) != 1);
        if (g_clutch.place_on_top_face != is_non_top_face) {
            jobject settings = mc::mc_get_game_settings(jni, ctx->minecraft);
            jobject use_key = mc::settings_get_key_use_item(jni, settings);
            mc::key_binding_increment_press_time(jni, use_key);

            const math::Vec3d next_pos = mc::mop_get_offset_block_pos_vec3d(jni, mop);
            g_clutch.target_block = {
                static_cast<int32_t>(next_pos.x),
                static_cast<int32_t>(next_pos.y),
                static_cast<int32_t>(next_pos.z),
            };
        }
    }
}

// Enables silent rotation spoofing + WASD move-fix while clutching or snapping back (sub_18006A830)
void on_rotation_spoof_query(void* event) noexcept {
    if (g_clutch.spoof_rotation_active) {
        auto* ev = static_cast<events::RotationSpoofEvent*>(event);
        ev->active = true;
        ev->move_fix = true;
    }
}

// Evaluates fall clutch target, block selection, humanized rotation, and snap-back (sub_18006A850)
void on_post_living_update(void* event) noexcept {
    g_clutch.clutch_active = false;
    g_clutch.spoof_rotation_active = false;

    events::GameContext* ctx = static_cast<events::ContextEvent*>(event)->ctx;
    if (!ctx->in_game_focus) {
        return;
    }

    events::JniContext* jni = ctx->jni;
    jobject player = ctx->local_player;
    const bool on_ground = mc::entity_is_on_ground(jni, player);
    const int32_t hurt_time = mc::living_get_hurt_time(jni, player);
    g_clutch.ticks_since_damage = (hurt_time == 0) ? (g_clutch.ticks_since_damage + 1) : 0;

    if (g_clutch.post_clutch_jump_window > 0) {
        --g_clutch.post_clutch_jump_window;
        if (on_ground && mc::is_rotation_spoof_active() && g_clutch.snapback_instant_on_jump.value) {
            jobject settings = mc::mc_get_game_settings(jni, ctx->minecraft);
            jobject jump_key = mc::settings_get_key_jump(jni, settings);
            if (mc::key_binding_is_physically_down(jni, jump_key)) {
                const math::Vec2f cur_rot = math::vec2_add(
                    mc::entity_get_rotation(jni, player),
                    mc::get_pending_aim_rotation_delta());
                const math::Vec2f spoof_target = mc::get_target_spoof_rotation();

                const double sign = (g_clutch.jump_snap_yaw_accum < 0.0) ? 1.0 : -1.0;
                const float jitter_yaw = static_cast<float>(
                    static_cast<double>(random::uniform_range({5.0f, 15.0f})) * sign);
                const float jitter_pitch = static_cast<float>(
                    static_cast<double>(random::uniform_range({5.0f, 15.0f})) * sign);
                g_clutch.jump_snap_yaw_accum += static_cast<double>(jitter_yaw);

                const math::Vec2f jitter = math::vec2_scale(
                    {jitter_yaw, jitter_pitch}, g_clutch.randomization.value);
                math::Vec2f delta = math::vec2_sub(math::vec2_add(spoof_target, jitter), cur_rot);
                delta.x = math::wrap_degrees_180(delta.x);

                mc::queue_aim_rotation_delta(delta);
                g_clutch.spoof_rotation_active = true;
                g_clutch.snapback_delay_ticks = static_cast<int32_t>(g_clutch.snapback_delay.value);
                return;
            }
        }
    }

    const math::AABB player_box = mc::aabb_to_aabb(jni, mc::entity_get_bounding_box(jni, player));
    const math::Vec3d eye_pos = mc::vec3_to_vec3d(jni, mc::living_get_position_eyes(jni, player));
    const math::Vec2f cur_rot = math::vec2_add(
        mc::entity_get_rotation(jni, player),
        mc::get_pending_aim_rotation_delta());

    const auto apply_aim_or_snapback = [&]() noexcept {
        if (!mc::try_claim_aim_priority(1000u)) {
            return;
        }
        g_clutch.spoof_rotation_active = true;

        if (g_clutch.clutch_active) {
            const math::Vec3d look_vec = mc::entity_get_look_vector(jni, player);
            const math::Vec3d ray_delta = math::vec3_scale(look_vec, 4.4);
            const math::AABB inset_target_box = math::make_aabb(
                {
                    static_cast<double>(g_clutch.target_block.x) + 0.02,
                    static_cast<double>(g_clutch.target_block.y) + 0.02,
                    static_cast<double>(g_clutch.target_block.z) + 0.02,
                },
                {
                    static_cast<double>(g_clutch.target_block.x) + 0.98,
                    static_cast<double>(g_clutch.target_block.y + 1),
                    static_cast<double>(g_clutch.target_block.z) + 0.98,
                });

            math::Vec3d hit_point{};
            if (math::aabb_raycast_segment(inset_target_box, eye_pos, ray_delta, &hit_point) &&
                hit_point.y < static_cast<double>(g_clutch.target_block.y) + 0.99) {
                return;
            }

            const math::Vec2f desired_rot = math::calc_yaw_pitch(eye_pos, g_clutch.aim_point);
            const math::Vec2f raw_diff = math::vec2_sub(desired_rot, cur_rot);
            const float pitch_diff = raw_diff.y;
            float yaw_diff = 0.0f;
            if (static_cast<double>(desired_rot.y) <= 89.999) {
                yaw_diff = math::wrap_degrees_180(raw_diff.x);
            } else {
                yaw_diff = g_clutch.vertical_yaw_drift.sample_timed({-20.0f, 20.0f}, 0.5) *
                           g_clutch.randomization.value;
            }

            const float min_speed_scale = static_cast<float>(
                static_cast<double>(g_clutch.randomization.value) * -0.25 + 1.0);
            const float max_yaw_step =
                g_clutch.yaw_speed_drift.sample_dt({min_speed_scale, 1.0f}, 10.0, 1.0) *
                g_clutch.aim_speed.value;
            const float max_pitch_step =
                g_clutch.pitch_speed_drift.sample_dt({min_speed_scale, 1.0f}, 10.0, 1.0) *
                g_clutch.aim_speed.value;

            const float clamped_yaw = math::clampf(-max_yaw_step, max_yaw_step, yaw_diff);
            const float clamped_pitch = math::clampf(-max_pitch_step, max_pitch_step, pitch_diff);

            const float yaw_mult =
                g_clutch.aim_mult.value +
                g_clutch.yaw_speed_drift.sample_dt({-0.05f, 0.05f}, 10.0, 1.0);
            const float pitch_mult =
                g_clutch.aim_mult.value +
                g_clutch.pitch_speed_drift.sample_dt({-0.05f, 0.05f}, 10.0, 1.0);

            const float step_yaw = math::lerpf(
                clamped_yaw, yaw_mult * yaw_diff, g_clutch.aim_mult_strength.value);
            const float step_pitch = math::lerpf(
                clamped_pitch, pitch_mult * pitch_diff, g_clutch.aim_mult_strength.value);

            mc::queue_aim_rotation_delta({step_yaw, step_pitch});
        } else if (g_clutch.snapback_delay_ticks <= 0 &&
                   g_clutch.snapback_duration_ticks > 0 &&
                   mc::is_rotation_spoof_active()) {
            const math::Vec2f cur_aim = math::vec2_add(
                mc::entity_get_rotation(jni, player),
                mc::get_pending_aim_rotation_delta());
            math::Vec2f diff = math::vec2_sub(mc::get_target_spoof_rotation(), cur_aim);
            diff.x = math::wrap_degrees_180(diff.x);
            mc::queue_aim_rotation_delta(
                math::vec2_scale(diff, 1.0f / static_cast<float>(g_clutch.snapback_duration_ticks)));
        }
    };

    const auto handle_inactive = [&]() noexcept {
        g_clutch.clutch_active = false;
        if (g_clutch.snapback_delay_ticks > 0) {
            --g_clutch.snapback_delay_ticks;
            g_clutch.snapback_duration_ticks = static_cast<int32_t>(g_clutch.snapback_duration.value);
        } else if (g_clutch.snapback_duration_ticks > 0) {
            --g_clutch.snapback_duration_ticks;
            g_clutch.clutch_active = false;
        } else {
            if (g_clutch.pending_auto_disable) {
                ctx->module_manager->request_set_enabled(&g_clutch.base, false);
                g_clutch.pending_auto_disable = false;
            }
            return;
        }
        apply_aim_or_snapback();
    };

    if ((on_ground && g_clutch.require_air.value) ||
        (g_clutch.ticks_since_damage >= 11 && g_clutch.require_damage.value) ||
        mc::entity_get_riding_entity(jni, player) != nullptr) {
        handle_inactive();
        return;
    }

    if (g_clutch.require_backwards.value) {
        const math::Vec3d motion = mc::entity_get_motion(jni, player);
        const double speed_horiz = std::hypot(motion.x, motion.z);
        if (speed_horiz == 0.0) {
            handle_inactive();
            return;
        }
        const float player_yaw = mc::entity_get_rotation(jni, player).x;
        const float back_rad = static_cast<float>(
            (static_cast<double>(player_yaw) * -math::kPi) / 180.0 - math::kPi);
        const float back_x = std::sin(back_rad);
        const float back_z = std::cos(back_rad);
        const double cos_angle =
            (static_cast<double>(back_x) * motion.x + static_cast<double>(back_z) * motion.z) /
            speed_horiz;
        if (std::acos(cos_angle) >= (math::kPi * 0.5)) {
            handle_inactive();
            return;
        }
    }

    g_clutch.place_on_top_face = false;
    const double center_x = (player_box.min.x + player_box.max.x) * 0.5;
    const double anchor_y = player_box.min.y - 0.99;
    const double center_z = (player_box.min.z + player_box.max.z) * 0.5;
    const math::Vec3d anchor_pos = math::make_vec3(center_x, anchor_y, center_z);

    jobject mutable_pos = (jni->mappings->version != 0) ? mc::block_pos_new(jni, 0, 0, 0) : nullptr;
    const int32_t base_x = static_cast<int32_t>(std::floor(center_x));
    const int32_t base_y = static_cast<int32_t>(std::floor(anchor_y));
    const int32_t base_z = static_cast<int32_t>(std::floor(center_z));
    const mc::Vec3i base_block{base_x, base_y, base_z};

    if (g_clutch.min_height.value > 0.0f) {
        int32_t step = 1;
        int32_t check_y = base_y;
        while (true) {
            jobject below_block =
                mc::world_get_block_mutable_pos(jni, ctx->world, mutable_pos, base_x, check_y, base_z);
            if (mc::block_get_id(jni, below_block) != 0) {
                handle_inactive();
                return;
            }
            const float checked_blocks = static_cast<float>(step++);
            --check_y;
            if (g_clutch.min_height.value <= checked_blocks) {
                break;
            }
        }
    }

    size_t max_entries = kRangeEntryCounts[static_cast<int32_t>(g_clutch.range.value)];
    int64_t prev_target_dist_limit = -3;
    float best_angle_dist = std::numeric_limits<float>::max();
    math::Vec3d best_aim_point{};
    mc::Vec3i best_block{};

    if (static_cast<double>(g_clutch.target_block.y) <= anchor_y) {
        jobject prev_block = mc::world_get_block_mutable_pos(
            jni,
            ctx->world,
            mutable_pos,
            g_clutch.target_block.x,
            g_clutch.target_block.y,
            g_clutch.target_block.z);
        if (mc::block_get_id(jni, prev_block) != 0) {
            const mc::Vec3i prev_offset{
                g_clutch.target_block.x - base_x,
                g_clutch.target_block.y - base_y,
                g_clutch.target_block.z - base_z,
            };
            math::Vec3d cand_pt{};
            const float angle_dist = evaluate_side_aim_point(
                eye_pos, cur_rot, anchor_pos, base_block, prev_offset, &cand_pt);
            if (angle_dist <= g_clutch.max_aim_dist.value &&
                math::vec3_dist_sq(eye_pos, cand_pt) < kMaxPlaceReachSq) {
                jobject start_vec = mc::vec3_new(jni, eye_pos);
                jobject end_vec = mc::vec3_new(jni, cand_pt);
                jobject ray_mop = mc::world_ray_trace_blocks(
                    jni, ctx->world, start_vec, end_vec, false, true, false);
                mc::Vec3i ray_block = g_clutch.target_block;
                bool los_ok = true;
                if (ray_mop != nullptr) {
                    ray_block = mc::mop_get_block_pos_vec3i(jni, ray_mop);
                    los_ok = (ray_block.x == g_clutch.target_block.x &&
                              ray_block.y == g_clutch.target_block.y &&
                              ray_block.z == g_clutch.target_block.z);
                }
                if (los_ok) {
                    best_aim_point = cand_pt;
                    best_block = ray_block;
                    if (angle_dist != std::numeric_limits<float>::max()) {
                        const int32_t manhattan =
                            std::abs(base_x - ray_block.x) +
                            std::abs(base_y - ray_block.y) +
                            std::abs(base_z - ray_block.z);
                        prev_target_dist_limit = static_cast<int64_t>(static_cast<uint32_t>(manhattan)) - 2;
                        best_angle_dist = angle_dist;
                    }
                }
            }
        }
    }

    bool placed_directly_below = false;
    for (size_t i = 0; i < max_entries; ++i) {
        const uint16_t packed = kPackedSearchOffsets[i];
        const int32_t raw_x = static_cast<int32_t>(packed & 0xFu);
        const int32_t raw_y = static_cast<int32_t>((packed >> 4) & 0xFu);
        const int32_t raw_z = static_cast<int32_t>((packed >> 8) & 0xFu);

        const int32_t dx = raw_x - 8;
        const int32_t dy = raw_y - 8;
        const int32_t dz = raw_z - 8;

        const int32_t bx = base_x + dx;
        const int32_t by = base_y + dy;
        const int32_t bz = base_z + dz;

        jobject cand_block = mc::world_get_block_mutable_pos(jni, ctx->world, mutable_pos, bx, by, bz);
        if (mc::block_get_id(jni, cand_block) == 0) {
            continue;
        }

        if (dx == 0 && dz == 0) {
            if (g_clutch.sideways_only.value) {
                handle_inactive();
                return;
            }

            const float rand_val = g_clutch.randomization.value;
            const double inset =
                static_cast<double>(g_clutch.edge_inset_drift.sample_timed({0.0f, 0.05f}, 1.0) * rand_val) + 0.1;
            const double jitter_x =
                static_cast<double>(g_clutch.top_x_drift.sample_timed({-0.1f, 0.1f}, 1.0) * rand_val);
            const double jitter_z =
                static_cast<double>(g_clutch.top_z_drift.sample_timed({-0.1f, 0.1f}, 1.0) * rand_val);

            const double aim_x = static_cast<double>(math::clampf(
                static_cast<float>(static_cast<double>(bx) + inset),
                static_cast<float>(static_cast<double>(bx + 1) - inset),
                static_cast<float>(center_x + jitter_x)));
            const double aim_z = static_cast<double>(math::clampf(
                static_cast<float>(static_cast<double>(bz) + inset),
                static_cast<float>(static_cast<double>(bz + 1) - inset),
                static_cast<float>(center_z + jitter_z)));

            g_clutch.aim_point = math::make_vec3(aim_x, 1.0, aim_z);
            g_clutch.target_block = {bx, by, bz};
            g_clutch.place_on_top_face = true;
            placed_directly_below = true;
            break;
        }

        const int32_t manhattan = std::abs(dx) + std::abs(dy) + std::abs(dz);
        max_entries = kRangeEntryCounts[manhattan];

        const mc::Vec3i offset{dx, dy, dz};
        math::Vec3d cand_pt{};
        const float angle_dist = evaluate_side_aim_point(
            eye_pos, cur_rot, anchor_pos, base_block, offset, &cand_pt);
        if (angle_dist <= best_angle_dist || prev_target_dist_limit > static_cast<int64_t>(manhattan)) {
            if (math::vec3_dist_sq(eye_pos, cand_pt) < kMaxPlaceReachSq) {
                best_aim_point = cand_pt;
                best_block = {bx, by, bz};
                best_angle_dist = angle_dist;
            }
        }
    }

    if (!placed_directly_below) {
        if (best_angle_dist > g_clutch.max_aim_dist.value) {
            handle_inactive();
            return;
        }
        g_clutch.aim_point = best_aim_point;
        g_clutch.target_block = best_block;
    }

    if (g_clutch.select_blocks.value == 1 || g_clutch.select_blocks.value == 2) {
        mc::select_hotbar_block_slot(ctx, &g_clutch.base, g_clutch.select_blocks.value == 2);
    }

    jobject held_item = mc::player_get_held_item(jni, player);
    g_clutch.clutch_active = mc::is_block_item(jni, held_item);
    if (!g_clutch.clutch_active) {
        handle_inactive();
        return;
    }

    const float max_delay_scale = static_cast<float>(
        static_cast<double>(g_clutch.randomization.value) * 0.5 + 1.0);
    g_clutch.snapback_delay_ticks = static_cast<int32_t>(
        random::uniform_range({1.0f, max_delay_scale}) * g_clutch.snapback_delay.value);
    g_clutch.post_clutch_jump_window = 10;
    g_clutch.pending_auto_disable = g_clutch.snapback_disable_after.value;
    apply_aim_or_snapback();
}

// Renders Clutch settings in ClickGUI (sub_18006BA30)
void render_settings(gui::ClickGui* gui, gui::GuiRenderer* renderer) noexcept {
    gui::widgets::render_section_header(gui, renderer, "Main");
    g_clutch.range.render(gui, renderer);
    g_clutch.max_aim_dist.render(gui, renderer);
    g_clutch.min_height.render(gui, renderer);
    g_clutch.click_speed.render(gui, renderer);
    g_clutch.randomization.render(gui, renderer);
    g_clutch.select_blocks.render(gui, renderer);
    g_clutch.sideways_only.render(gui, renderer);

    gui::widgets::render_section_header(gui, renderer, "Aim");
    g_clutch.aim_speed.render(gui, renderer);
    g_clutch.aim_mult.render(gui, renderer);
    g_clutch.aim_mult_strength.render(gui, renderer);
    g_clutch.aim_multipoint.render(gui, renderer);

    gui::widgets::render_section_header(gui, renderer, "Snap-back");
    g_clutch.snapback_delay.render(gui, renderer);
    g_clutch.snapback_duration.render(gui, renderer);
    g_clutch.snapback_instant_on_jump.render(gui, renderer);
    g_clutch.snapback_disable_after.render(gui, renderer);

    gui::widgets::render_section_header(gui, renderer, "Conditions");
    g_clutch.require_air.render(gui, renderer);
    g_clutch.require_damage.render(gui, renderer);
    g_clutch.require_backwards.render(gui, renderer);
}

// Serializes/deserializes Clutch config (sub_18006BBD0)
void visit_config(config::ConfigArchive* archive) noexcept {
    g_clutch.range.visit(archive, "range");
    g_clutch.max_aim_dist.visit(archive, "max_aim_dist");
    g_clutch.min_height.visit(archive, "min_height");
    g_clutch.click_speed.visit(archive, "click_speed");
    g_clutch.randomization.visit(archive, "randomization");
    g_clutch.select_blocks.visit(archive, "select_blocks");
    g_clutch.sideways_only.visit(archive, "sideways_only");
    g_clutch.aim_speed.visit(archive, "aim_speed");
    g_clutch.aim_mult.visit(archive, "aim_mult");
    g_clutch.aim_mult_strength.visit(archive, "aim_mult_strength");
    g_clutch.aim_multipoint.visit(archive, "aim_multipoint");
    g_clutch.snapback_delay.visit(archive, "snapback_delay");
    g_clutch.snapback_duration.visit(archive, "snapback_duration");
    g_clutch.snapback_instant_on_jump.visit(archive, "snapback_instant_on_jump");
    g_clutch.snapback_disable_after.visit(archive, "snapback_disable_after");
    g_clutch.require_air.visit(archive, "require_air");
    g_clutch.require_damage.visit(archive, "require_damage");
    g_clutch.require_backwards.visit(archive, "require_backwards");
}

} // namespace

events::Module* get_clutch_module() noexcept {
    return &g_clutch.base;
}

} // namespace slinky::modules
