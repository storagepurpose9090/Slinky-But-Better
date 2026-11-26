#include "modules/movement/instant_stop.hpp"

#include "minecraft/game_helpers.hpp"
#include "minecraft/mc_wrappers.hpp"
#include "util/math.hpp"

namespace slinky::modules {

namespace {

constinit int32_t s_state = 0; // dword_1803215C0

// Matches sub_180063B20
void on_pre_living_update(void* raw_event) noexcept {
    const auto* ev = static_cast<const events::ContextEvent*>(raw_event);
    events::GameContext* ctx = ev->ctx;

    if (!minecraft::mc_has_ingame_focus(ctx->jni, ctx->minecraft)) {
        s_state = 0;
        return;
    }

    jobject settings    = minecraft::mc_get_game_settings(ctx->jni, ctx->minecraft);
    jobject key_forward = minecraft::settings_get_key_forward(ctx->jni, settings);
    jobject key_back    = minecraft::settings_get_key_back(ctx->jni, settings);
    jobject key_left    = minecraft::settings_get_key_left(ctx->jni, settings);
    jobject key_right   = minecraft::settings_get_key_right(ctx->jni, settings);

    if (s_state == 2) {
        minecraft::key_binding_set_pressed(
            ctx->jni, key_back, minecraft::key_binding_is_physically_down(ctx->jni, key_back));
        minecraft::key_binding_set_pressed(
            ctx->jni, key_left, minecraft::key_binding_is_physically_down(ctx->jni, key_left));
        minecraft::key_binding_set_pressed(
            ctx->jni, key_right, minecraft::key_binding_is_physically_down(ctx->jni, key_right));
        s_state = 0;
        return;
    }

    if (s_state != 1) {
        if (minecraft::key_binding_is_pressed(ctx->jni, key_forward)) {
            s_state = 1;
        }
        return;
    }

    if (minecraft::key_binding_is_pressed(ctx->jni, key_forward)) {
        return;
    }

    if (!minecraft::entity_is_on_ground(ctx->jni, ctx->local_player)) {
        return;
    }

    const math::Vec3d motion = minecraft::entity_get_motion(ctx->jni, ctx->local_player);
    const math::Vec2f rot = minecraft::entity_get_rotation(ctx->jni, ctx->local_player);
    float move_factor = minecraft::living_get_land_movement_factor(ctx->jni, ctx->local_player);
    if (minecraft::entity_is_sprinting(ctx->jni, ctx->local_player)) {
        move_factor = static_cast<float>(static_cast<double>(move_factor) / 1.3);
    }

    int32_t best_strafe = 999;
    double best_speed_sq = motion.x * motion.x + motion.z * motion.z;

    for (int32_t strafe = -1; strafe <= 1; ++strafe) {
        const math::Vec3d delta = minecraft::compute_strafe_motion(
            rot, -1.0f, static_cast<float>(strafe), move_factor);
        const math::Vec3d next_motion = math::vec3_add(motion, delta);
        const double speed_sq = next_motion.x * next_motion.x + next_motion.z * next_motion.z;
        if (speed_sq < best_speed_sq) {
            best_speed_sq = speed_sq;
            best_strafe = strafe;
        }
    }

    if (best_strafe != 999) {
        minecraft::key_binding_set_pressed(ctx->jni, key_back, true);
        minecraft::key_binding_set_pressed(ctx->jni, key_left, best_strafe == 1);
        minecraft::key_binding_set_pressed(ctx->jni, key_right, best_strafe == -1);
        s_state = 2;
    } else {
        s_state = 0;
    }
}

consteval events::Module make_module() noexcept {
    events::Module mod{};
    mod.name = "Instant Stop";
    mod.description = "Instantly stops moving when move keys are released.";
    mod.category = events::ModuleCategory::Move;
    mod.events[events::kEventPreLivingUpdate] = {&on_pre_living_update, 200, 0};
    return mod;
}

constinit events::Module s_module = make_module();

} // namespace

events::Module* get_instant_stop_module() noexcept {
    return &s_module;
}

} // namespace slinky::modules
