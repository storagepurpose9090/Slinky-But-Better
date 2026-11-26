#include "modules/utility/timer.hpp"

#include <cstdio>

#include "config/settings.hpp"
#include "gui/widgets.hpp"
#include "minecraft/game_helpers.hpp"
#include "minecraft/mc_wrappers.hpp"

namespace slinky::modules {

namespace {

extern events::Module s_module;

config::FloatSetting s_speed{
    {},
    "Speed",
    "%0.2fx",
    0.01f,
    0.0f,
    0.0f,
    2.0f,
    1.0f,
    0.0f,
};

config::BoolSetting s_require_mouse{
    {},
    "Mouse pressed",
    false,
    false,
    {},
};

config::BoolSetting s_require_falling{
    {},
    "Falling",
    false,
    false,
    {},
};

config::BoolSetting s_require_damage{
    {},
    "Damage",
    false,
    false,
    {},
};

// sub_180054050
void on_frame_update(void* raw_event) noexcept {
    auto* event = static_cast<events::FrameUpdateEvent*>(raw_event);
    events::GameContext* ctx = event->ctx;
    events::JniContext* jni = ctx->jni;
    jobject timer_obj = mc::mc_get_timer(jni, ctx->minecraft);

    const bool blocked =
        (s_require_mouse.value && !mc::is_attack_key_down(ctx)) ||
        (s_require_falling.value &&
         (mc::entity_is_on_ground(jni, ctx->local_player) ||
          mc::entity_get_motion(jni, ctx->local_player).y >= 0.0)) ||
        (s_require_damage.value &&
         (!ctx->in_game_focus || mc::living_get_hurt_time(jni, ctx->local_player) == 0));

    mc::timer_set_speed(jni, timer_obj, blocked ? 1.0f : s_speed.value);
}

// sub_180054110
void on_module_state_changed(void* raw_event) noexcept {
    auto* event = static_cast<events::ModuleStateChangedEvent*>(raw_event);
    if (event->module == &s_module && !event->enabled) {
        events::GameContext* ctx = event->ctx;
        jobject timer_obj = mc::mc_get_timer(ctx->jni, ctx->minecraft);
        mc::timer_set_speed(ctx->jni, timer_obj, 1.0f);
    }
}

// sub_180054160
void render_settings(gui::ClickGui* gui, gui::GuiRenderer* renderer) noexcept {
    gui::widgets::render_section_header(gui, renderer, "Main");
    s_speed.render(gui, renderer);
    gui::widgets::render_section_header(gui, renderer, "Conditions");
    s_require_mouse.render(gui, renderer);
    s_require_damage.render(gui, renderer);
    s_require_falling.render(gui, renderer);
}

// sub_1800541E0
void visit_config(config::ConfigArchive* archive) noexcept {
    s_speed.visit(archive, "speed");
    s_require_mouse.visit(archive, "require_mouse");
    s_require_damage.visit(archive, "require_damage");
    s_require_falling.visit(archive, "require_falling");
}

// sub_180054250
int format_hud_tag(char* out_buf_32) noexcept {
    return std::snprintf(out_buf_32, 32, "%.2fx", static_cast<double>(s_speed.value));
}

events::Module make_module() noexcept {
    events::Module mod{};
    mod.name = "Timer";
    mod.description = "Modifies the speed the game runs at.";
    mod.category = events::ModuleCategory::Utility;
    mod.enabled = false;
    mod.flags = events::kModuleFlagNone;
    mod.keybind = 0;
    mod.hold_mode = false;
    mod.events[events::kEventFrameUpdate] = {on_frame_update, 200, 0};
    mod.events[events::kEventModuleStateChanged] = {on_module_state_changed, 0, 0};
    mod.render_settings = render_settings;
    mod.visit_config = visit_config;
    mod.format_hud_tag = format_hud_tag;
    return mod;
}

events::Module s_module = make_module();

} // namespace

events::Module* get_timer_module() noexcept {
    return &s_module;
}

} // namespace slinky::modules
