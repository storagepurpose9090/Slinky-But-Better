#include "modules/movement/null_move.hpp"

#include "minecraft/mc_wrappers.hpp"

namespace slinky::modules {

namespace {

constinit int32_t s_forward_press_tick = 0; // dword_180321BC0
constinit int32_t s_left_press_tick    = 0; // dword_180321BC4
constinit int32_t s_back_press_tick    = 0; // dword_180321BC8
constinit int32_t s_right_press_tick   = 0; // dword_180321BCC

void update_null_move_key(
    events::JniContext* jni,
    jobject primary_key,
    jobject opposite_key,
    int32_t ticks_existed,
    int32_t* inout_press_tick) noexcept
{
    const int32_t prev_tick = *inout_press_tick;
    const bool primary_pressed = minecraft::key_binding_is_pressed(jni, primary_key);
    const bool opposite_pressed = minecraft::key_binding_is_pressed(jni, opposite_key);

    if (primary_pressed) {
        if (prev_tick == 0 && opposite_pressed) {
            minecraft::key_binding_set_pressed(jni, opposite_key, false);
        }
        *inout_press_tick = ticks_existed;
    } else {
        if (prev_tick != 0 && minecraft::key_binding_is_physically_down(jni, opposite_key)) {
            minecraft::key_binding_set_pressed(jni, opposite_key, true);
        }
        *inout_press_tick = 0;
    }
}

// Matches sub_180068080
void on_pre_living_update(void* raw_event) noexcept {
    const auto* ev = static_cast<const events::ContextEvent*>(raw_event);
    events::GameContext* ctx = ev->ctx;
    if (!minecraft::mc_has_ingame_focus(ctx->jni, ctx->minecraft)) {
        return;
    }

    const int32_t ticks_existed = minecraft::entity_get_ticks_existed(ctx->jni, ctx->local_player);
    jobject settings    = minecraft::mc_get_game_settings(ctx->jni, ctx->minecraft);
    jobject key_forward = minecraft::settings_get_key_forward(ctx->jni, settings);
    jobject key_left    = minecraft::settings_get_key_left(ctx->jni, settings);
    jobject key_back    = minecraft::settings_get_key_back(ctx->jni, settings);
    jobject key_right   = minecraft::settings_get_key_right(ctx->jni, settings);

    update_null_move_key(ctx->jni, key_forward, key_back, ticks_existed, &s_forward_press_tick);
    update_null_move_key(ctx->jni, key_left, key_right, ticks_existed, &s_left_press_tick);
    update_null_move_key(ctx->jni, key_back, key_forward, ticks_existed, &s_back_press_tick);
    update_null_move_key(ctx->jni, key_right, key_left, ticks_existed, &s_right_press_tick);
}

consteval events::Module make_module() noexcept {
    events::Module mod{};
    mod.name = "Null Move";
    mod.description = "Makes it easier to switch move direction instantly.";
    mod.category = events::ModuleCategory::Move;
    mod.events[events::kEventPreLivingUpdate] = {&on_pre_living_update, 100, 0};
    return mod;
}

constinit events::Module s_module = make_module();

} // namespace

events::Module* get_null_move_module() noexcept {
    return &s_module;
}

} // namespace slinky::modules
