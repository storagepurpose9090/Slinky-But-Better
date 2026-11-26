#include "modules/visual/free_look.hpp"

#include "config/settings.hpp"
#include "gui/widgets.hpp"
#include "minecraft/game_helpers.hpp"
#include "minecraft/mc_wrappers.hpp"

namespace slinky::modules {

namespace {

constexpr const char* kPerspectiveOptions[3] = {
    "First person",
    "Third person",
    "Third person (reverse)",
};

config::ModeSetting s_target_perspective{
    {}, "Switch to", 3, 0, kPerspectiveOptions, 1, 0, false
};

bool s_has_locked_rotation = false;
math::Vec2f s_locked_rotation{};
int32_t s_saved_perspective = 0;

// Matches sub_180058790
void on_module_state_changed(void* raw_event) noexcept {
    const auto* ev = static_cast<const events::ModuleStateChangedEvent*>(raw_event);
    if (ev->module != get_free_look_module()) {
        return;
    }

    events::GameContext* ctx = ev->ctx;
    if (ctx->world == nullptr) {
        return;
    }

    if (ev->enabled) {
        s_has_locked_rotation = true;
        if (minecraft::is_rotation_spoof_active()) {
            s_locked_rotation = minecraft::get_server_spoof_rotation();
        } else {
            s_locked_rotation = minecraft::entity_get_rotation(ctx->jni, ctx->local_player);
        }
    }

    jobject settings = minecraft::mc_get_game_settings(ctx->jni, ctx->minecraft);
    if (ev->enabled) {
        s_saved_perspective = minecraft::settings_get_third_person_view(ctx->jni, settings);
        minecraft::settings_set_third_person_view(ctx->jni, settings, s_target_perspective.value);
    } else {
        minecraft::settings_set_third_person_view(ctx->jni, settings, s_saved_perspective);
        if (s_has_locked_rotation) {
            minecraft::set_server_spoof_rotation(s_locked_rotation);
            s_has_locked_rotation = false;
        }
    }
}

// Matches sub_180058870
void on_rotation_spoof_query(void* raw_event) noexcept {
    auto* ev = static_cast<events::RotationSpoofEvent*>(raw_event);
    ev->active = true;
    ev->rotation = s_locked_rotation;
}

void render_settings(gui::ClickGui* gui, gui::GuiRenderer* renderer) noexcept {
    gui::widgets::render_section_header(gui, renderer, "Main");
    s_target_perspective.render(gui, renderer);
}

void visit_config(config::ConfigArchive* ar) noexcept {
    s_target_perspective.visit(ar, "target_perspective");
}

events::Module create_module() noexcept {
    events::Module mod{};
    mod.name = "Free Look";
    mod.description = "Freely move the camera around.";
    mod.category = events::ModuleCategory::Visual;
    mod.events[events::kEventModuleStateChanged] = {&on_module_state_changed, 0};
    mod.events[events::kEventRotationSpoofQuery] = {&on_rotation_spoof_query, -1000};
    mod.render_settings = &render_settings;
    mod.visit_config = &visit_config;
    return mod;
}

} // namespace

events::Module* get_free_look_module() noexcept {
    static events::Module s_module = create_module();
    return &s_module;
}

} // namespace slinky::modules
