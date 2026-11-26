#include "modules/combat/reach.hpp"

#include <cstdint>
#include <cstdio>

#include "config/settings.hpp"
#include "gui/widgets.hpp"
#include "minecraft/mc_wrappers.hpp"

namespace slinky::modules {

namespace {

int32_t s_last_attacker_time = 0;
int32_t s_combo_ticks = 0;

void on_reach_raycast_query(void* event) noexcept;
void render_settings(gui::ClickGui* gui, gui::GuiRenderer* renderer) noexcept;
void visit_config(config::ConfigArchive* ar) noexcept;
int format_hud_tag(char* out_buf) noexcept;

struct ReachModule {
    events::Module base{};
    config::FloatSetting distance{{}, "Distance", "%0.2f blocks", 0.01f, 0.0f, 3.0f, 6.0f, 3.05f, 3.05f};
    config::FloatSetting activate_time{{}, "Activate for", "%.0f tick(s)", 1.0f, 0.0f, 1.0f, 10.0f, 3.0f, 3.0f};
    config::BoolSetting require_sprint{{}, "Sprinting", false, false};

    ReachModule() noexcept {
        base.name = "Reach";
        base.description = "Increases your melee range.";
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

ReachModule s_mod{};

// sub_1800516C0
void on_reach_raycast_query(void* event) noexcept {
    auto* ev = static_cast<events::ReachRaycastQueryEvent*>(event);
    int32_t ticks = s_combo_ticks;
    if (ticks > 0) {
        ticks = --s_combo_ticks;
    }

    events::GameContext* ctx = ev->ctx;
    if (s_mod.require_sprint.value) {
        if (!mc::entity_is_sprinting(ctx->jni, ctx->local_player)) {
            return;
        }
        ticks = s_combo_ticks;
    }

    if (ticks == 0) {
        const int32_t attacker_time = mc::living_get_last_attacker_time(ctx->jni, ctx->local_player);
        if (attacker_time != s_last_attacker_time) {
            s_last_attacker_time = attacker_time;
            s_combo_ticks = 9;
            ticks = 9;
        }
    }

    if (s_mod.activate_time.value > static_cast<float>(ticks)) {
        ev->override_raycast = true;
        ev->reach_distance = static_cast<double>(s_mod.distance.value);
    }
}

// sub_180051770
void render_settings(gui::ClickGui* gui, gui::GuiRenderer* renderer) noexcept {
    gui::widgets::render_section_header(gui, renderer, "Main");
    s_mod.distance.render(gui, renderer);

    gui::widgets::render_section_header(gui, renderer, "Advanced");
    s_mod.activate_time.render(gui, renderer);

    gui::widgets::render_section_header(gui, renderer, "Conditions");
    s_mod.require_sprint.render(gui, renderer);
}

// sub_1800517F0
void visit_config(config::ConfigArchive* ar) noexcept {
    s_mod.distance.visit(ar, "distance");
    s_mod.activate_time.visit(ar, "activate_time");
    s_mod.require_sprint.visit(ar, "require_sprint");
}

// sub_180051850
int format_hud_tag(char* out_buf) noexcept {
    return std::snprintf(out_buf, 32, "%.2f", static_cast<double>(s_mod.distance.value));
}

} // namespace

events::Module* get_reach_module() noexcept {
    return &s_mod.base;
}

} // namespace slinky::modules
