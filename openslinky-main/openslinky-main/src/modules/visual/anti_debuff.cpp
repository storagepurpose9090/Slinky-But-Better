#include "modules/visual/anti_debuff.hpp"

#include "config/settings.hpp"
#include "gui/widgets.hpp"
#include "minecraft/mc_wrappers.hpp"

namespace slinky::modules {

namespace {

constexpr uint32_t kPotionBlindnessId = 15u;

config::BoolSetting s_blindness{{}, "Blindness", true, false};
config::BoolSetting s_nausea{{}, "Nausea", true, false};

// Matches sub_180065ED0
void on_post_living_update(void* raw_event) noexcept {
    const auto* ev = static_cast<const events::ContextEvent*>(raw_event);
    events::GameContext* ctx = ev->ctx;

    if (s_blindness.value) {
        jobject potion = minecraft::potion_get_by_id(ctx->jni, kPotionBlindnessId);
        jobject active_effect = minecraft::living_get_active_potion_effect(
            ctx->jni, ctx->local_player, potion);
        if (active_effect != nullptr) {
            minecraft::potion_effect_set_duration(ctx->jni, active_effect, 0);
        }
    }

    if (s_nausea.value) {
        minecraft::player_sp_set_time_in_portal(ctx->jni, ctx->local_player, 0.0f);
        minecraft::player_sp_set_prev_time_in_portal(ctx->jni, ctx->local_player, 0.0f);
    }
}

void render_settings(gui::ClickGui* gui, gui::GuiRenderer* renderer) noexcept {
    gui::widgets::render_section_header(gui, renderer, "Effects");
    s_blindness.render(gui, renderer);
    s_nausea.render(gui, renderer);
}

void visit_config(config::ConfigArchive* ar) noexcept {
    s_blindness.visit(ar, "blindness");
    s_nausea.visit(ar, "nausea");
}

events::Module create_module() noexcept {
    events::Module mod{};
    mod.name = "Anti Debuff";
    mod.description = "Removes negative visual effects.";
    mod.category = events::ModuleCategory::Visual;
    mod.events[events::kEventPostLivingUpdate] = {&on_post_living_update, 0};
    mod.render_settings = &render_settings;
    mod.visit_config = &visit_config;
    return mod;
}

} // namespace

events::Module* get_anti_debuff_module() noexcept {
    static events::Module s_module = create_module();
    return &s_module;
}

} // namespace slinky::modules
