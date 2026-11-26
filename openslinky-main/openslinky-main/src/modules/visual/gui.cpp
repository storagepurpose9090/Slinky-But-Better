#include "modules/visual/gui.hpp"

#include "nanovg.h"

#include "commands/command_manager.hpp"
#include "config/profile_manager.hpp"
#include "config/settings.hpp"
#include "gui/click_gui.hpp"
#include "gui/widgets.hpp"
#include "minecraft/mc_wrappers.hpp"
#include "modules/module_interop.hpp"
#include "util/keycodes.hpp"
#include "util/time.hpp"

namespace slinky::modules {

namespace {

constexpr float kScaleFactors[4] = {0.75f, 1.0f, 1.5f, 2.0f};

constexpr const char* kScaleOptions[4] = {
    "Small (75%)",
    "Medium (100%)",
    "Large (150%)",
    "Extra large (200%)",
};

config::ModeSetting s_scale{
    {}, "Scale", 4, 0, kScaleOptions, 1, 0, false
};

config::ColorSetting s_accent_color{
    "Accent color", {0.953f, 0.365f, 0.071f}, {0.0f, 0.0f, 0.0f}
};

config::BoolSetting s_allow_input{{}, "Allow input while open", true, false};
config::BoolSetting s_compact{{}, "Compact", false, false};
config::BoolSetting s_wide{{}, "Wide", false, false};

gui::ClickGui s_click_gui{};
bool s_unloading = false;

// Matches sub_180053D20
void on_game_tick(void* raw_event) noexcept {
    events::GameContext* ctx = static_cast<events::ContextEvent*>(raw_event)->ctx;
    if (minecraft::lwjgl_mouse_is_grabbed(ctx->jni)) {
        minecraft::lwjgl_mouse_set_grabbed(ctx->jni, false);
    }
}

// Matches sub_180053D60
void on_module_state_changed(void* raw_event) noexcept {
    const auto* ev = static_cast<const events::ModuleStateChangedEvent*>(raw_event);
    if (ev->module != get_gui_module()) {
        return;
    }

    events::GameContext* ctx = ev->ctx;
    if (ev->enabled) {
        if (minecraft::lwjgl_mouse_is_grabbed(ctx->jni)) {
            const int32_t disp_w = minecraft::lwjgl_display_get_width(ctx->jni);
            const int32_t disp_h = minecraft::lwjgl_display_get_height(ctx->jni);
            minecraft::lwjgl_mouse_set_cursor_position(ctx->jni, disp_w / 2, disp_h / 2);
            minecraft::lwjgl_mouse_set_grabbed(ctx->jni, false);
        }
        s_click_gui.on_open(time::now_seconds());
    } else {
        if (minecraft::mc_has_ingame_focus(ctx->jni, ctx->minecraft)) {
            minecraft::lwjgl_mouse_set_grabbed(ctx->jni, true);
        }
        s_click_gui.close_modal(time::now_seconds());
        s_click_gui.reset_interaction();
        if (!s_unloading) {
            const int err = config::save_global_settings(ctx->module_manager, ctx->profile_registry);
            if (err != 0) {
                events::ErrorNotificationEvent err_ev{};
                err_ev.id = events::kEventErrorNotification;
                err_ev.error_code = err;
                ctx->module_manager->dispatch(&err_ev);
            }
        }
    }
}

// Matches sub_180053E90
void on_gui_frame_poll(void* raw_event) noexcept {
    auto* ev = static_cast<events::GuiFramePollEvent*>(raw_event);
    ev->render_flags |= 3u;
}

// Matches sub_180053EA0
void on_gui_render_2d(void* raw_event) noexcept {
    auto* ev = static_cast<events::GuiRender2DEvent*>(raw_event);
    events::GameContext* ctx = ev->ctx;

    s_click_gui.accent_color = nvgRGBf(
        s_accent_color.value.r,
        s_accent_color.value.g,
        s_accent_color.value.b);
    s_click_gui.hide_descriptions = s_compact.value;
    s_click_gui.wide_layout = s_wide.value;
    s_click_gui.render(ev->renderer, ctx->module_manager, ctx->profile_registry, ctx->jni);
}

// Matches sub_180053F20
void on_client_shutdown(void* raw_event) noexcept {
    (void)raw_event;
    s_unloading = true;
}

void render_settings(gui::ClickGui* gui, gui::GuiRenderer* renderer) noexcept {
    gui::widgets::render_section_header(gui, renderer, "Main");
    s_scale.render(gui, renderer);
    s_accent_color.render(gui, renderer);
    s_allow_input.render(gui, renderer);
    gui::widgets::render_section_header(gui, renderer, "Layout");
    s_compact.render(gui, renderer);
    s_wide.render(gui, renderer);
}

void visit_config(config::ConfigArchive* ar) noexcept {
    s_scale.visit(ar, "scale");
    s_accent_color.visit(ar, "accent_color");
    s_allow_input.visit(ar, "allow_input");
    s_compact.visit(ar, "compact");
    s_wide.visit(ar, "wide");
}

events::Module create_module() noexcept {
    events::Module mod{};
    mod.name = "GUI";
    mod.description = "Graphical interface for the cheat.";
    mod.category = events::ModuleCategory::Visual;
    mod.flags = events::kModuleFlagNoToggleSwitch |
                events::kModuleFlagHiddenFromHud |
                events::kModuleFlagNoHoldMode |
                events::kModuleFlagGlobalSettings;
    mod.keybind = input::KEY_RSHIFT;
    mod.events[events::kEventGameTick] = {&on_game_tick, 0};
    mod.events[events::kEventModuleStateChanged] = {&on_module_state_changed, 0};
    mod.events[events::kEventGuiFramePoll] = {&on_gui_frame_poll, 0};
    mod.events[events::kEventGuiRender2D] = {&on_gui_render_2d, 0};
    mod.events[events::kEventClientShutdown] = {&on_client_shutdown, 0};
    mod.render_settings = &render_settings;
    mod.visit_config = &visit_config;
    return mod;
}

} // namespace

void gui_initialize_click_gui() noexcept {
    s_click_gui.initialize();
}

// Matches sub_180053BD0
bool gui_is_unload_confirmed() noexcept {
    return s_click_gui.is_unload_requested();
}

// Matches sub_180053BF0
float gui_get_scale_factor() noexcept {
    const int32_t idx = s_scale.value;
    if (idx >= 0 && idx < 4) {
        return kScaleFactors[idx];
    }
    return 1.0f;
}

// Matches sub_180053C10
math::Color3f gui_get_accent_color() noexcept {
    return s_accent_color.value;
}

// Matches sub_180053C30
bool gui_handle_window_input(
    const input::InputEvent* event,
    bool was_already_down,
    events::ModuleManager* manager) noexcept
{
    events::Module* gui_mod = get_gui_module();
    if (gui_mod->enabled && s_click_gui.handle_input(event, time::now_seconds())) {
        return true;
    }

    if (event->kind == input::InputEventKind::KeyOrButton && event->pressed == 1) {
        const int32_t key_code = event->key.key_code;
        if (key_code != 0) {
            if (!was_already_down && key_code == gui_mod->keybind) {
                manager->request_set_enabled(gui_mod, !gui_mod->enabled);
                return true;
            }
            if (gui_mod->enabled && key_code == input::KEY_ESC) {
                manager->request_set_enabled(gui_mod, false);
                return true;
            }
        }
    }

    return gui_mod->enabled &&
           (event->kind != input::InputEventKind::KeyOrButton ||
            !s_allow_input.value ||
            static_cast<uint32_t>(event->key.key_code) <= static_cast<uint32_t>(input::KEY_MOUSE5));
}

// Matches sub_180053CF0
void gui_command_open(events::GameContext* ctx, const commands::CommandArgValue* args) noexcept {
    (void)args;
    ctx->module_manager->request_set_enabled(get_gui_module(), true);
}

// Matches off_18016DAC8
const commands::CommandSpec& get_gui_command_spec() noexcept {
    static constexpr commands::CommandSpec kGuiCommandSpec{
        "gui",
        "Opens the GUI",
        {},
        &gui_command_open,
        nullptr,
    };
    return kGuiCommandSpec;
}

events::Module* get_gui_module() noexcept {
    static events::Module s_module = create_module();
    return &s_module;
}

} // namespace slinky::modules
