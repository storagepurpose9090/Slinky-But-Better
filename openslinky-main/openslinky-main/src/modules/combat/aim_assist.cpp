#include "modules/combat/aim_assist.hpp"

#include <cfloat>
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
    "Linear",
    "Silent",
};

constexpr const char* kTargetModeOptions[] = {
    "Single",
    "Switch",
};

constexpr const char* kSortByOptions[] = {
    "Distance",
    "Health",
    "Hurt time",
    "Aim angle",
};

// 8-point LOS fallback offsets around target feet (@ 0x18016C650)
constexpr math::Vec3d kLosMultipointOffsets[8] = {
    {0.2, 1.62, 0.2},
    {0.2, 1.62, -0.2},
    {-0.2, 1.62, -0.2},
    {-0.2, 1.62, 0.2},
    {0.2, 0.0, 0.2},
    {0.2, 0.0, -0.2},
    {-0.2, 0.0, -0.2},
    {-0.2, 0.0, 0.2},
};

// Ken Perlin 256-byte permutation table (@ 0x1803135A0)
constexpr uint8_t kPerlinPerm[256] = {
    151, 160, 137,  91,  90,  15, 131,  13, 201,  95,  96,  53, 194, 233,   7, 225,
    140,  36, 103,  30,  69, 142,   8,  99,  37, 240,  21,  10,  23, 190,   6, 148,
    247, 120, 234,  75,   0,  26, 197,  62,  94, 252, 219, 203, 117,  35,  11,  32,
     57, 177,  33,  88, 237, 149,  56,  87, 174,  20, 125, 136, 171, 168,  68, 175,
     74, 165,  71, 134, 139,  48,  27, 166,  77, 146, 158, 231,  83, 111, 229, 122,
     60, 211, 133, 230, 220, 105,  92,  41,  55,  46, 245,  40, 244, 102, 143,  54,
     65,  25,  63, 161,   1, 216,  80,  73, 209,  76, 132, 187, 208,  89,  18, 169,
    200, 196, 135, 130, 116, 188, 159,  86, 164, 100, 109, 198, 173, 186,   3,  64,
     52, 217, 226, 250, 124, 123,   5, 202,  38, 147, 118, 126, 255,  82,  85, 212,
    207, 206,  59, 227,  47,  16,  58,  17, 182, 189,  28,  42, 223, 183, 170, 213,
    119, 248, 152,   2,  44, 154, 163,  70, 221, 153, 101, 155, 167,  43, 172,   9,
    129,  22,  39, 253,  19,  98, 108, 110,  79, 113, 224, 232, 178, 185, 112, 104,
    218, 246,  97, 228, 251,  34, 242, 193, 238, 210, 144,  12, 191, 179, 162, 241,
     81,  51, 145, 235, 249,  14, 239, 107,  49, 192, 214,  31, 181, 199, 106, 157,
    184,  84, 204, 176, 115, 121,  50,  45, 127,   4, 150, 254, 138, 236, 205,  93,
    222, 114,  67,  29,  24,  72, 243, 141, 128, 195,  78,  66, 215,  61, 156, 180,
};

struct TargetSearchCriteria {
    double max_dist = 0.0;
    float max_fov = 0.0f;
    float max_health = FLT_MAX;
    int32_t max_hurt_time = INT32_MAX;
    bool is_locked_recheck = false;
};

struct TargetEvalResult {
    math::Vec3d aim_point{};
    double distance = 0.0;
    float yaw_error = 0.0f;
    bool valid_lock = false;
    bool should_pull = false;
};

bool s_should_pull = false;
math::Vec3d s_prev_aim_point{};
math::Vec3d s_cur_aim_point{};
bool s_silent_spoofing = false;
random::DriftNoise s_yaw_damp_drift{};
random::DriftNoise s_pitch_jitter_drift{};
random::DriftNoise s_hor_speed_drift{};
random::DriftNoise s_ver_speed_drift{};
random::DriftNoise s_global_speed_drift{};
int32_t s_target_lock_ticks = 0;
int32_t s_last_switch_index = 0;
int32_t s_click_start_warmup_ticks = 0;

void on_frame_update(void* event) noexcept;
void on_module_state_changed(void* event) noexcept;
void on_rotation_spoof_query(void* event) noexcept;
void on_post_living_update(void* event) noexcept;
void render_settings(gui::ClickGui* gui, gui::GuiRenderer* renderer) noexcept;
void visit_config(config::ConfigArchive* ar) noexcept;
int format_hud_tag(char* out_buf) noexcept;

struct AimAssistModule {
    events::Module base{};
    config::FloatSetting aim_randomization{{}, "Randomization", "%.0f%%", 0.0f, 100.0f, 0.0f, 1.0f, 0.5f, 0.5f};
    config::RangeSetting fov{{}, "FOV", "%0.0f\xC2\xB0", "%0.0f - %0.0f\xC2\xB0", 0.5f, 2.0f, 0.0f, 180.0f, {4.0f, 60.0f}, {4.0f, 60.0f}};
    config::FloatSetting hor_speed{{}, "Horizontal speed", "%.1f", 0.1f, 0.0f, 0.0f, 30.0f, 15.0f, 15.0f};
    config::FloatSetting ver_speed{{}, "Vertical speed", "%.1f", 0.1f, 0.0f, 0.0f, 30.0f, 0.0f, 0.0f};
    config::BoolSetting require_mouse_moved{{}, "Mouse moved", false, false};
    config::ModeSetting mode{{}, "Mode", 3, 0, kModeOptions, 0, 0, false};
    config::BoolSetting silent_move_correct{{}, "Keep move direction", true, true};
    config::RangeSetting range{{}, "Range", "%0.1f blocks", "%0.1f - %0.1f blocks", 0.1f, 0.0f, 0.0f, 10.0f, {1.0f, 4.0f}, {1.0f, 4.0f}};
    config::FloatSetting target_delay{{}, "Delay", "%0.0f ms", 1.0f, 50.0f, 0.0f, 10.0f, 0.0f, 0.0f};
    config::BoolSetting require_sprint{{}, "Sprinting", false, false};
    config::BoolSetting require_mouse_pressed{{}, "Mouse pressed", false, false};
    config::BoolSetting disable_on_block_break{{}, "Disable while breaking blocks", false, false};
    config::BoolSetting require_weapon{{}, "Holding weapon", false, false};
    config::ModeSetting mode_target{{}, "Mode", 2, 0, kTargetModeOptions, 0, 0, false};
    config::ModeSetting sort_by{{}, "Sort by", 4, 0, kSortByOptions, 0, 0, false};
    config::FloatSetting predict{{}, "Predict", "%.0f%%", 0.0f, 100.0f, 0.0f, 1.0f, 0.0f, 0.0f};
    config::BoolSetting ignore_invis{{}, "Ignore invisibles", true, true};
    config::FloatSetting hor_hitbox_correct{{}, "Horizontal", "%.0f%%", 0.0f, 100.0f, 0.0f, 1.0f, 0.0f, 0.0f};
    config::FloatSetting ver_hitbox_correct{{}, "Vertical", "%.0f%%", 0.0f, 100.0f, 0.0f, 1.0f, 1.0f, 1.0f};
    config::BoolSetting allow_exceed_fov{{}, "Increased FOV while locked", false, false};
    config::BoolSetting require_line_of_sight{{}, "Not behind blocks", true, true};

    AimAssistModule() noexcept {
        base.name = "Aim Assist";
        base.description = "Pulls your crosshair towards other players.";
        base.category = events::ModuleCategory::Combat;
        base.enabled = false;
        base.flags = events::kModuleFlagNone;
        base.keybind = 0;
        base.hold_mode = false;
        base.events[events::kEventFrameUpdate] = {&on_frame_update, 0};
        base.events[events::kEventModuleStateChanged] = {&on_module_state_changed, 0};
        base.events[events::kEventRotationSpoofQuery] = {&on_rotation_spoof_query, -100};
        base.events[events::kEventPostLivingUpdate] = {&on_post_living_update, 0};
        base.render_settings = &render_settings;
        base.visit_config = &visit_config;
        base.format_hud_tag = &format_hud_tag;
    }
};

AimAssistModule s_mod{};

// sub_1800BA6E0
float perlin_noise_1d(float x) noexcept {
    const int32_t trunc_x = static_cast<int32_t>(x);
    const int32_t xi = trunc_x - (x <= static_cast<float>(trunc_x) ? 1 : 0);
    const float xf = x - static_cast<float>(xi);
    const float u = ((6.0f * xf - 15.0f) * xf + 10.0f) * (xf * xf * xf);

    const uint8_t p0 = kPerlinPerm[static_cast<uint8_t>(xi)];
    const uint8_t p1 = kPerlinPerm[static_cast<uint8_t>(xi + 1)];

    float g0 = static_cast<float>((p0 & 7u) + 1u);
    if ((p0 & 8u) != 0u) {
        g0 = -g0;
    }

    float g1 = static_cast<float>((p1 & 7u) + 1u);
    if ((p1 & 8u) != 0u) {
        g1 = -g1;
    }

    const float n0 = xf * g0;
    const float n1 = (xf - 1.0f) * g1;
    return ((n1 - n0) * u + n0) * 0.18799999356269836f;
}

// sub_18004DEF0
math::Vec3d apply_multipoint_noise(
    const math::Vec3d& eye_pos,
    const math::Vec3d& target_pt,
    double dist) noexcept {
    const float base = static_cast<float>(
        (eye_pos.x + eye_pos.y + eye_pos.z + target_pt.x + target_pt.y + target_pt.z) * 0.1);
    const float nx = perlin_noise_1d(base);
    const float ny = perlin_noise_1d(base + 10000.0f);
    const float nz = perlin_noise_1d(base + 20000.0f);

    float scale = s_mod.aim_randomization.value;
    if (dist < 3.5) {
        scale = static_cast<float>(static_cast<double>(scale) * (dist / 3.5 * 0.9 + 0.1));
    }

    const math::Vec3d offset{
        static_cast<double>(nx) * 0.5,
        static_cast<double>(ny) * 0.5,
        static_cast<double>(nz) * 0.5};
    return math::vec3_add(target_pt, math::vec3_scale(offset, static_cast<double>(scale)));
}

// sub_18004D390
void step_aim_towards(math::Vec2f angle_diff, float dt) noexcept {
    const float min_fov = s_mod.fov.value.min;
    const float dyaw = (angle_diff.x <= 0.0f)
                           ? std::fmin(angle_diff.x + min_fov, 0.0f)
                           : std::fmax(angle_diff.x - min_fov, 0.0f);
    float dpitch = (angle_diff.y <= 0.0f)
                       ? std::fmin(angle_diff.y + min_fov, 0.0f)
                       : std::fmax(angle_diff.y - min_fov, 0.0f);

    const float abs_dyaw = std::fabs(dyaw);
    const float yaw_norm = std::fmin(abs_dyaw / s_yaw_damp_drift.sample_timed({40.0f, 120.0f}, 5.0), 1.0f);

    float hor_spd = s_mod.hor_speed.value;
    float ver_spd = s_mod.ver_speed.value;
    if (s_mod.mode.value == 2) {
        hor_spd = std::fmax(5.0f, hor_spd);
        ver_spd = std::fmax(5.0f, ver_spd);
    }

    const float pitch_damp = yaw_norm * s_mod.aim_randomization.value;
    float hor_rate = FLT_MAX;
    if (static_cast<double>(hor_spd) <= 29.99) {
        hor_rate = s_hor_speed_drift.sample_timed({0.9f, 1.1f}, 1.0) * ((hor_spd * 0.5f) * (hor_spd * 0.5f));
    }

    float ver_rate = FLT_MAX;
    if (static_cast<double>(ver_spd) <= 29.99) {
        ver_rate = s_ver_speed_drift.sample_timed({0.9f, 1.1f}, 1.0) * ((ver_spd * 0.5f) * (ver_spd * 0.5f));
    }

    dpitch *= (1.0f - pitch_damp);
    const float speed_mod = s_global_speed_drift.sample_timed({0.95f, 1.05f}, 60.0);

    math::Vec2f step{};
    if (s_mod.mode.value == 1) {
        const float max_h = (hor_rate * 5.0f) * speed_mod * dt;
        const float max_v = (ver_rate * 5.0f) * speed_mod * dt;
        step.x = math::clampf(-max_h, max_h, dyaw);
        step.y = math::clampf(-max_v, max_v, dpitch);
    } else {
        const math::Vec2f scaled =
            math::vec2_scale(math::vec2_scale({hor_rate * dyaw, ver_rate * dpitch}, speed_mod), dt);
        step.x = (std::fabs(scaled.x) <= abs_dyaw) ? scaled.x : dyaw;
        step.y = (std::fabs(scaled.y) <= std::fabs(dpitch)) ? scaled.y : dpitch;
    }

    const float abs_step_x = std::fabs(step.x);
    const float jitter_amp = static_cast<float>(
        static_cast<double>(s_mod.aim_randomization.value) * (static_cast<double>(abs_step_x) * 0.1));
    step.y += s_pitch_jitter_drift.sample_timed({-jitter_amp, jitter_amp}, 1.0);

    if (s_mod.mode.value != 2 && s_mod.require_mouse_moved.value) {
        const float abs_step_y = std::fabs(step.y);
        const float mouse_dx = std::fabs(mc::g_aimassist_mouse_delta.x);
        const float mouse_dy = std::fabs(mc::g_aimassist_mouse_delta.y);
        if (step.x != 0.0f && mouse_dx < abs_step_x) {
            step.x = (mouse_dx / abs_step_x) * step.x;
        }
        if (step.y != 0.0f && mouse_dy < abs_step_y) {
            step.y = (mouse_dy / abs_step_y) * step.y;
        }
        mc::g_aimassist_mouse_delta = {0.0f, 0.0f};
    }

    mc::queue_aim_rotation_delta(step);
}

// sub_18004D7E0
TargetEvalResult evaluate_candidate_target(
    events::GameContext* ctx,
    jobject entity,
    TargetSearchCriteria* criteria,
    const math::Vec3d& eye_pos,
    math::Vec2f ref_rot) noexcept {
    TargetEvalResult res{};
    if (mc::entity_is_dead(ctx->jni, entity) ||
        mc::player_is_sleeping(ctx->jni, entity) ||
        (s_mod.ignore_invis.value && mc::player_is_invisible_to_player(ctx->jni, entity, ctx->local_player)) ||
        mc::player_has_tag_flags(ctx->jni, entity, mc::kPlayerTagExcludedMask)) {
        return res;
    }

    const float health = mc::living_get_health(ctx->jni, entity);
    if (health > criteria->max_health) {
        return res;
    }

    const int32_t hurt_time = mc::living_get_hurt_time(ctx->jni, entity);
    if (hurt_time > criteria->max_hurt_time) {
        return res;
    }

    math::Vec3d entity_pos = mc::entity_get_position(ctx->jni, entity);
    if (s_mod.predict.value > 0.0f) {
        const math::Vec3d server_pos = mc::other_player_mp_get_target_pos(ctx->jni, entity);
        entity_pos = math::vec3_lerp(entity_pos, server_pos, static_cast<double>(s_mod.predict.value));
    }

    const math::AABB raw_box = mc::aabb_to_aabb(ctx->jni, mc::entity_get_bounding_box(ctx->jni, entity));
    const math::AABB expanded_box = math::aabb_expand(raw_box, 0.10000000149011612);
    const math::Vec3d closest_pt = math::aabb_closest_point(expanded_box, eye_pos);
    const double dist = math::vec3_dist(eye_pos, closest_pt);
    bool within_pull_range = static_cast<double>(s_mod.range.value.min) <= dist;
    if (dist > criteria->max_dist) {
        return res;
    }

    const math::Vec3d entity_eye = math::vec3_add_eye_height(entity_pos);
    double aim_x = entity_eye.x;
    double aim_z = entity_eye.z;
    float hor_blend = s_mod.hor_hitbox_correct.value;
    if (hor_blend > 0.0f) {
        if (dist < 3.5) {
            hor_blend = static_cast<float>(static_cast<double>(hor_blend) * (dist / 3.5));
        }
        const math::Vec3d blended = math::vec3_lerp(entity_eye, closest_pt, static_cast<double>(hor_blend));
        aim_x = blended.x;
        aim_z = blended.z;
    }

    const double aim_y =
        math::lerpd(entity_eye.y, closest_pt.y, static_cast<double>(s_mod.ver_hitbox_correct.value));
    math::Vec3d noisy_aim = apply_multipoint_noise(eye_pos, {aim_x, aim_y, aim_z}, dist);

    const math::Vec2f needed_rot = math::calc_yaw_pitch(eye_pos, noisy_aim);
    const math::Vec2f rot_diff = math::vec2_sub(needed_rot, ref_rot);
    const float yaw_err = std::fabs(math::wrap_degrees_180(rot_diff.x));

    if (yaw_err > criteria->max_fov) {
        if (!criteria->is_locked_recheck) {
            return res;
        }
        within_pull_range = within_pull_range && s_mod.allow_exceed_fov.value;
    }

    if (within_pull_range && s_mod.require_line_of_sight.value) {
        within_pull_range = true;
        if (!mc::world_has_line_of_sight(ctx->jni, ctx->world, eye_pos, noisy_aim)) {
            bool found_los = false;
            for (const math::Vec3d& offset : kLosMultipointOffsets) {
                const math::Vec3d probe_base = math::vec3_add(entity_pos, offset);
                const double probe_dist = math::vec3_dist(eye_pos, noisy_aim);
                const math::Vec3d probe_pt = apply_multipoint_noise(eye_pos, probe_base, probe_dist);
                if (mc::world_has_line_of_sight(ctx->jni, ctx->world, eye_pos, probe_pt)) {
                    noisy_aim = probe_pt;
                    found_los = true;
                    break;
                }
            }
            if (!found_los) {
                return res;
            }
            within_pull_range = true;
        }
    }

    if (s_mod.mode_target.value != 0) {
        switch (s_mod.sort_by.value) {
            case 0:
                criteria->max_dist = dist;
                break;
            case 1:
                criteria->max_health = health;
                break;
            case 2:
                criteria->max_hurt_time = hurt_time;
                break;
            case 3:
                criteria->max_fov = yaw_err;
                break;
            default:
                break;
        }
    } else {
        criteria->max_fov = yaw_err;
    }

    res.aim_point = noisy_aim;
    res.distance = dist;
    res.yaw_error = yaw_err;
    res.valid_lock = true;
    res.should_pull = within_pull_range;
    return res;
}

void clear_locked_target(events::GameContext* ctx) noexcept {
    if (mc::g_aimassist_target_entity != nullptr) {
        ctx->jni->env->DeleteGlobalRef(mc::g_aimassist_target_entity);
    }
    mc::g_aimassist_target_entity = nullptr;
    s_target_lock_ticks = 0;
    s_should_pull = false;
}

// sub_18004C6C0
void on_frame_update(void* event) noexcept {
    auto* ev = static_cast<events::FrameUpdateEvent*>(event);
    events::GameContext* ctx = ev->ctx;
    if (!ctx->in_game_focus) {
        return;
    }

    if (s_should_pull) {
        if (!mc::try_claim_aim_priority(100u)) {
            return;
        }
        if (!s_silent_spoofing && s_mod.mode.value == 2) {
            s_silent_spoofing = true;
        }
        if (s_silent_spoofing && !mc::is_rotation_spoof_active()) {
            return;
        }

        const math::Vec3d prev_pos = mc::entity_get_prev_position(ctx->jni, ctx->local_player);
        const math::Vec3d cur_pos = mc::entity_get_position(ctx->jni, ctx->local_player);
        math::Vec3d interp_eye = math::vec3_add(
            prev_pos,
            math::vec3_scale(math::vec3_sub(cur_pos, prev_pos), static_cast<double>(ev->partial_ticks)));
        if (!mc::is_version_1_7_10(ctx->jni)) {
            interp_eye = math::vec3_add_eye_height(interp_eye);
        }

        const math::Vec3d interp_aim =
            math::vec3_lerp(s_prev_aim_point, s_cur_aim_point, static_cast<double>(ev->partial_ticks));
        const math::Vec2f desired_rot = math::calc_yaw_pitch(interp_eye, interp_aim);
        const math::Vec2f cur_rot = mc::entity_get_rotation(ctx->jni, ctx->local_player);
        const math::Vec2f pending_rot = math::vec2_add(cur_rot, mc::get_pending_aim_rotation_delta());
        math::Vec2f diff = math::vec2_sub(desired_rot, pending_rot);
        diff.x = math::wrap_degrees_180(diff.x);
        step_aim_towards(diff, static_cast<float>(ev->delta_time));
        return;
    }

    if (s_silent_spoofing) {
        if (mc::is_rotation_spoof_active() && mc::try_claim_aim_priority(100u)) {
            const math::Vec2f cur_rot = mc::entity_get_rotation(ctx->jni, ctx->local_player);
            const math::Vec2f pending_rot = math::vec2_add(cur_rot, mc::get_pending_aim_rotation_delta());
            const math::Vec2f target_spoof = mc::get_target_spoof_rotation();
            math::Vec2f diff = math::vec2_sub(target_spoof, pending_rot);
            diff.x = math::wrap_degrees_180(diff.x);
            if (math::vec2_length(diff) < 10.0f) {
                s_silent_spoofing = false;
            }
            step_aim_towards(diff, static_cast<float>(ev->delta_time));
        } else {
            s_silent_spoofing = false;
        }
    }
}

// sub_18004C9C0
void on_module_state_changed(void* event) noexcept {
    auto* ev = static_cast<events::ModuleStateChangedEvent*>(event);
    if (ev->module == &s_mod.base && !ev->enabled) {
        clear_locked_target(ev->ctx);
    }
}

// sub_18004CA20
void on_rotation_spoof_query(void* event) noexcept {
    auto* ev = static_cast<events::RotationSpoofEvent*>(event);
    if (s_silent_spoofing) {
        ev->active = true;
        if (s_mod.silent_move_correct.value) {
            ev->move_fix = true;
        }
    }
}

// sub_18004CA40
void on_post_living_update(void* event) noexcept {
    ++s_target_lock_ticks;
    mc::g_aimassist_mouse_delta = {0.0f, 0.0f};

    auto* ev = static_cast<events::ContextEvent*>(event);
    events::GameContext* ctx = ev->ctx;
    if (!ctx->in_game_focus) {
        clear_locked_target(ctx);
        return;
    }

    if (s_mod.require_sprint.value &&
        s_mod.mode.value != 2 &&
        !mc::entity_is_sprinting(ctx->jni, ctx->local_player)) {
        s_should_pull = false;
        return;
    }

    const bool attack_down = mc::is_attack_key_down(ctx);
    if (s_mod.require_mouse_pressed.value) {
        if (!attack_down) {
            s_click_start_warmup_ticks = random::roll_chance(0.5f) ? 2 : 1;
            clear_locked_target(ctx);
            return;
        }
        if (static_cast<uint32_t>(s_mod.mode.value) < 2u && s_click_start_warmup_ticks != 0) {
            --s_click_start_warmup_ticks;
            clear_locked_target(ctx);
            return;
        }
    }

    if (s_mod.disable_on_block_break.value && attack_down) {
        jobject mop = mc::mc_get_object_mouse_over(ctx->jni, ctx->minecraft);
        if (mop != nullptr && mc::mop_get_type_ordinal(ctx->jni, mop) == 1) {
            s_should_pull = false;
            return;
        }
    }

    if (s_mod.require_weapon.value && !weapons_is_holding_weapon_simple(ctx->jni, ctx->local_player)) {
        clear_locked_target(ctx);
        return;
    }

    math::Vec3d eye_pos = mc::entity_get_position(ctx->jni, ctx->local_player);
    if (!mc::is_version_1_7_10(ctx->jni)) {
        eye_pos = math::vec3_add_eye_height(eye_pos);
    }

    const math::Vec2f ref_rot = mc::is_rotation_spoof_active()
                                    ? mc::get_target_spoof_rotation()
                                    : mc::entity_get_rotation(ctx->jni, ctx->local_player);

    TargetSearchCriteria criteria{};
    criteria.max_dist = static_cast<double>(s_mod.range.value.max);
    criteria.max_fov = s_mod.fov.value.max;
    criteria.max_health = FLT_MAX;
    criteria.max_hurt_time = INT32_MAX;
    criteria.is_locked_recheck = true;

    const float lock_ticks_f = static_cast<float>(s_target_lock_ticks);
    const float delay_ticks = s_mod.target_delay.value;
    bool need_full_scan = false;

    if (s_mod.mode_target.value != 0 && lock_ticks_f > delay_ticks) {
        need_full_scan = true;
    } else {
        bool kept_lock = false;
        if (mc::g_aimassist_target_entity != nullptr) {
            const TargetEvalResult eval =
                evaluate_candidate_target(ctx, mc::g_aimassist_target_entity, &criteria, eye_pos, ref_rot);
            if (eval.valid_lock) {
                kept_lock = true;
                s_should_pull = eval.should_pull;
                s_prev_aim_point = s_cur_aim_point;
                s_cur_aim_point = eval.aim_point;
            } else {
                clear_locked_target(ctx);
            }
        }
        if (!kept_lock && lock_ticks_f > delay_ticks) {
            need_full_scan = true;
        }
    }

    if (!need_full_scan) {
        return;
    }

    criteria.is_locked_recheck = false;
    jobject players = mc::world_get_player_entities(ctx->jni, ctx->world);
    if (mc::java_list_size(ctx->jni, players) <= 0) {
        clear_locked_target(ctx);
        return;
    }

    int32_t best_idx = -1;
    jobject best_entity = nullptr;
    TargetEvalResult best_eval{};

    for (int32_t i = 0; i < mc::java_list_size(ctx->jni, players); ++i) {
        jobject candidate = mc::java_list_get(ctx->jni, players, i);
        if (ctx->jni->env->IsSameObject(ctx->local_player, candidate)) {
            continue;
        }
        if (s_mod.sort_by.value == 2 && best_idx != -1 && i <= s_last_switch_index) {
            continue;
        }

        const TargetEvalResult eval =
            evaluate_candidate_target(ctx, candidate, &criteria, eye_pos, ref_rot);
        if (eval.valid_lock) {
            best_eval = eval;
            best_entity = candidate;
            best_idx = i;
        }
    }

    if (best_entity == nullptr) {
        clear_locked_target(ctx);
        return;
    }

    JNIEnv* env = ctx->jni->env;
    if (env->IsSameObject(mc::g_aimassist_target_entity, best_entity)) {
        s_prev_aim_point = s_cur_aim_point;
        s_cur_aim_point = best_eval.aim_point;
    } else {
        s_cur_aim_point = best_eval.aim_point;
        s_prev_aim_point = best_eval.aim_point;
    }
    s_should_pull = best_eval.should_pull;

    jobject prev_ref = nullptr;
    if (mc::g_aimassist_target_entity != nullptr) {
        env->DeleteGlobalRef(mc::g_aimassist_target_entity);
        prev_ref = mc::g_aimassist_target_entity;
    }
    if (!env->IsSameObject(prev_ref, best_entity)) {
        jobject fresh_ref = env->NewGlobalRef(best_entity);
        if (fresh_ref != nullptr) {
            mc::g_aimassist_target_entity = fresh_ref;
            s_target_lock_ticks = 0;
        }
    }
    s_last_switch_index = best_idx;
}

// sub_18004CF70
void render_settings(gui::ClickGui* gui, gui::GuiRenderer* renderer) noexcept {
    gui::widgets::render_section_header(gui, renderer, "Aim");
    s_mod.mode.render(gui, renderer);
    if (s_mod.mode.value == 2) {
        s_mod.silent_move_correct.render(gui, renderer);
    }
    s_mod.hor_speed.render(gui, renderer);
    s_mod.ver_speed.render(gui, renderer);
    s_mod.predict.render(gui, renderer);
    s_mod.aim_randomization.render(gui, renderer);

    gui::widgets::render_section_header(gui, renderer, "Multi-point");
    s_mod.hor_hitbox_correct.render(gui, renderer);
    s_mod.ver_hitbox_correct.render(gui, renderer);

    gui::widgets::render_section_header(gui, renderer, "Target");
    s_mod.mode_target.render(gui, renderer);
    if (s_mod.mode_target.value == 1) {
        s_mod.sort_by.render(gui, renderer);
    } else if (s_mod.mode_target.value == 0) {
        s_mod.allow_exceed_fov.render(gui, renderer);
    }
    s_mod.range.render(gui, renderer);
    s_mod.fov.render(gui, renderer);
    s_mod.target_delay.render(gui, renderer);

    gui::widgets::render_section_header(gui, renderer, "Conditions");
    if (s_mod.mode.value != 2) {
        s_mod.require_sprint.render(gui, renderer);
        s_mod.require_mouse_moved.render(gui, renderer);
    }
    s_mod.require_mouse_pressed.render(gui, renderer);
    s_mod.disable_on_block_break.render(gui, renderer);
    s_mod.require_weapon.render(gui, renderer);
    s_mod.ignore_invis.render(gui, renderer);
    s_mod.require_line_of_sight.render(gui, renderer);
}

// sub_18004D170
void visit_config(config::ConfigArchive* ar) noexcept {
    s_mod.mode.visit(ar, "mode");
    s_mod.hor_speed.visit(ar, "hor_speed");
    s_mod.ver_speed.visit(ar, "ver_speed");
    s_mod.silent_move_correct.visit(ar, "silent_move_correct");
    s_mod.predict.visit(ar, "predict");
    s_mod.aim_randomization.visit(ar, "aim_randomization");
    s_mod.hor_hitbox_correct.visit(ar, "hor_hitbox_correct");
    s_mod.ver_hitbox_correct.visit(ar, "ver_hitbox_correct");
    s_mod.mode_target.visit(ar, "mode_target");
    s_mod.sort_by.visit(ar, "sort_by");
    s_mod.allow_exceed_fov.visit(ar, "allow_exceed_fov");
    s_mod.range.visit(ar, "range");
    s_mod.fov.visit(ar, "fov");
    s_mod.target_delay.visit(ar, "target_delay");
    s_mod.require_sprint.visit(ar, "require_sprint");
    s_mod.require_mouse_moved.visit(ar, "require_mouse_moved");
    s_mod.require_mouse_pressed.visit(ar, "require_mouse_pressed");
    s_mod.disable_on_block_break.visit(ar, "disable_on_block_break");
    s_mod.require_weapon.visit(ar, "require_weapon");
    s_mod.ignore_invis.visit(ar, "ignore_invis");
    s_mod.require_line_of_sight.visit(ar, "require_line_of_sight");
}

// sub_18004D350
int format_hud_tag(char* out_buf) noexcept {
    return std::snprintf(out_buf, 32, "%s", kModeOptions[s_mod.mode.value]);
}

} // namespace

events::Module* get_aim_assist_module() noexcept {
    return &s_mod.base;
}

} // namespace slinky::modules
