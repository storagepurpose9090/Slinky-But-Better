#include "modules/combat/hitboxes.hpp"

#include <cstdio>

#include "config/settings.hpp"
#include "gui/widgets.hpp"

namespace slinky::modules {

namespace {

void on_reach_raycast_query(void* event) noexcept;
void render_settings(gui::ClickGui* gui, gui::GuiRenderer* renderer) noexcept;
void visit_config(config::ConfigArchive* ar) noexcept;
int format_hud_tag(char* out_buf) noexcept;

struct HitboxesModule {
    events::Module base{};
    config::FloatSetting expand{{}, "Expand", "%0.2f blocks", 0.01f, 0.0f, 0.0f, 1.0f, 0.1f, 0.1f};

    HitboxesModule() noexcept {
        base.name = "Hitboxes";
        base.description = "Expands hitboxes of other players.";
        base.category = events::ModuleCategory::Combat;
        base.enabled = false;
        base.flags = events::kModuleFlagNone;
        base.keybind = 0;
        base.hold_mode = false;
        base.events[events::kEventReachRaycastQuery] = {&on_reach_raycast_query, 0};
        base.render_settings = &render_settings;
        base.visit_config = &visit_config;
        base.format_hud_tag = &format_hud_tag;
    }
};

HitboxesModule s_mod{};

// sub_180055720
void on_reach_raycast_query(void* event) noexcept {
    auto* ev = static_cast<events::ReachRaycastQueryEvent*>(event);
    ev->override_raycast = true;
    ev->hitbox_expansion = static_cast<double>(s_mod.expand.value);
}

// sub_180055740
void render_settings(gui::ClickGui* gui, gui::GuiRenderer* renderer) noexcept {
    gui::widgets::render_section_header(gui, renderer, "Main");
    s_mod.expand.render(gui, renderer);
}

// sub_180055780
void visit_config(config::ConfigArchive* ar) noexcept {
    s_mod.expand.visit(ar, "expand");
}

// sub_1800557A0
int format_hud_tag(char* out_buf) noexcept {
    return std::snprintf(out_buf, 32, "%.2f", static_cast<double>(s_mod.expand.value));
}

} // namespace

events::Module* get_hitboxes_module() noexcept {
    return &s_mod.base;
}

} // namespace slinky::modules
