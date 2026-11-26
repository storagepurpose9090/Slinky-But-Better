#include "modules/utility/item_use_fix.hpp"

#include "minecraft/mc_wrappers.hpp"

namespace slinky::modules {

namespace {

bool s_was_hitting_block = false;

// sub_180064080
void on_pre_attack_or_use(void* raw_event) noexcept {
    auto* event = static_cast<events::AttackOrUseEntityEvent*>(raw_event);
    if (!event->is_attack) {
        events::GameContext* ctx = event->ctx;
        jobject controller = mc::mc_get_player_controller(ctx->jni, ctx->minecraft);
        const bool hitting = mc::controller_is_hitting_block(ctx->jni, controller);
        s_was_hitting_block = hitting;
        if (hitting) {
            mc::controller_set_hitting_block(ctx->jni, controller, false);
        }
    }
}

// sub_1800640E0
void on_post_attack_or_use(void* raw_event) noexcept {
    auto* event = static_cast<events::AttackOrUseEntityEvent*>(raw_event);
    if (s_was_hitting_block && !event->is_attack) {
        events::GameContext* ctx = event->ctx;
        jobject controller = mc::mc_get_player_controller(ctx->jni, ctx->minecraft);
        mc::controller_set_hitting_block(ctx->jni, controller, s_was_hitting_block);
    }
}

events::Module make_module() noexcept {
    events::Module mod{};
    mod.name = "Item Use Fix";
    mod.description = "Allows using items while breaking blocks.";
    mod.category = events::ModuleCategory::Utility;
    mod.enabled = false;
    mod.flags = events::kModuleFlagNone;
    mod.keybind = 0;
    mod.hold_mode = false;
    mod.events[events::kEventPreAttackOrUseEntity] = {on_pre_attack_or_use, 0, 0};
    mod.events[events::kEventPostAttackOrUseEntity] = {on_post_attack_or_use, 0, 0};
    return mod;
}

events::Module s_module = make_module();

} // namespace

events::Module* get_item_use_fix_module() noexcept {
    return &s_module;
}

} // namespace slinky::modules
