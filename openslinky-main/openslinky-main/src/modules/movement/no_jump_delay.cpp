#include "modules/movement/no_jump_delay.hpp"

#include "minecraft/mc_wrappers.hpp"

namespace slinky::modules {

namespace {

// Matches sub_1800574C0
void on_mid_living_update(void* raw_event) noexcept {
    const auto* ev = static_cast<const events::ContextEvent*>(raw_event);
    events::GameContext* ctx = ev->ctx;
    if (ctx->in_game_focus) {
        minecraft::living_set_jump_ticks(ctx->jni, ctx->local_player, 0);
    }
}

consteval events::Module make_module() noexcept {
    events::Module mod{};
    mod.name = "No Jump Delay";
    mod.description = "Removes cooldown when holding jump key.";
    mod.category = events::ModuleCategory::Move;
    mod.events[events::kEventMidLivingUpdate] = {&on_mid_living_update, 100, 0};
    return mod;
}

constinit events::Module s_module = make_module();

} // namespace

events::Module* get_no_jump_delay_module() noexcept {
    return &s_module;
}

} // namespace slinky::modules
