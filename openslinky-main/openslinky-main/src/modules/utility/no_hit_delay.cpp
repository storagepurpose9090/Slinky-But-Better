#include "modules/utility/no_hit_delay.hpp"
#include "modules/module_interop.hpp"

#include <cstdio>

#include "config/settings.hpp"
#include "gui/widgets.hpp"
#include "minecraft/mc_wrappers.hpp"

namespace slinky::modules {

namespace {

constexpr const char* kModeOptions[2] = {
    "Regular",
    "Legit",
};

config::ModeSetting s_mode{
    {},
    "Mode",
    2,
    0,
    kModeOptions,
    0,
    0,
    false,
    {},
};

// sub_18005F990
void on_pre_attack_or_use(void* raw_event) noexcept {
    auto* event = static_cast<events::AttackOrUseEntityEvent*>(raw_event);
    if (!event->is_attack) {
        return;
    }

    events::GameContext* ctx = event->ctx;
    events::JniContext* jni = ctx->jni;
    if (mc::is_version_1_7_10(jni)) {
        return;
    }

    if (mc::mc_get_left_click_counter(jni, ctx->minecraft) > 0) {
        jobject mop = nullptr;
        if (s_mode.value != 1 ||
            ((mop = mc::mc_get_object_mouse_over(jni, ctx->minecraft)) != nullptr &&
             mc::mop_get_type_ordinal(jni, mop) != 0)) {
            mc::mc_set_left_click_counter(jni, ctx->minecraft, 0);
        } else if (!mc::player_is_using_item(jni, ctx->local_player)) {
            mc::living_swing_item_super(jni, ctx->local_player);
        }
    }
}

// sub_18005FA30
void render_settings(gui::ClickGui* gui, gui::GuiRenderer* renderer) noexcept {
    gui::widgets::render_section_header(gui, renderer, "In-game");
    s_mode.render(gui, renderer);
}

// sub_18005FA70
void visit_config(config::ConfigArchive* archive) noexcept {
    s_mode.visit(archive, "mode");
}

// sub_18005FA90
int format_hud_tag(char* out_buf_32) noexcept {
    return std::snprintf(out_buf_32, 32, "%s", kModeOptions[s_mode.value]);
}

events::Module make_module() noexcept {
    events::Module mod{};
    mod.name = "No Hit Delay";
    mod.description = "Removes 1.8 swing delay.";
    mod.category = events::ModuleCategory::Utility;
    mod.enabled = false;
    mod.flags = events::kModuleFlagNone;
    mod.keybind = 0;
    mod.hold_mode = false;
    mod.events[events::kEventPreAttackOrUseEntity] = {on_pre_attack_or_use, 0, 0};
    mod.render_settings = render_settings;
    mod.visit_config = visit_config;
    mod.format_hud_tag = format_hud_tag;
    return mod;
}

events::Module s_module = make_module();

} // namespace

events::Module* get_no_hit_delay_module() noexcept {
    return &s_module;
}

} // namespace slinky::modules
