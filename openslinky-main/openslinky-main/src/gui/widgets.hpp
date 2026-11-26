#pragma once

#include <cstddef>
#include <cstdint>
#include "config/settings.hpp"
#include "gui/click_gui.hpp"
#include "gui/gui_renderer.hpp"
#include "util/math.hpp"

namespace slinky::gui::widgets {

// Animated pill toggle switch - matches sub_180034F50
bool render_toggle_pill(
    ClickGui* gui,
    GuiRenderer* renderer,
    math::Vec4f pill_rect,
    config::ToggleAnimState* anim,
    bool* inout_value) noexcept;

// Labeled boolean toggle row - matches sub_1800351C0
bool render_bool_toggle(
    ClickGui* gui,
    GuiRenderer* renderer,
    const char* label,
    config::ToggleAnimState* anim,
    bool* inout_value) noexcept;

// Subsection header label inside a module card - matches sub_1800352C0
void render_section_header(
    ClickGui* gui,
    GuiRenderer* renderer,
    const char* title) noexcept;

// Single-thumb float slider with Ctrl+Click exact numeric input - matches sub_180035370
bool render_float_slider(
    ClickGui* gui,
    GuiRenderer* renderer,
    const char* label,
    const char* format,
    config::SliderAnimState* anim,
    float min_val,
    float max_val,
    float step,
    float display_scale,
    float* inout_value) noexcept;

// Dual-thumb min/max range slider with Ctrl+Click exact numeric input - matches sub_1800359B0
bool render_range_slider(
    ClickGui* gui,
    GuiRenderer* renderer,
    const char* label,
    const char* single_format,
    const char* range_format,
    config::RangeSliderAnimState* anim,
    float min_val,
    float max_val,
    float step,
    float display_scale,
    float* inout_min,
    float* inout_max) noexcept;

// Collapsible mode dropdown (< 6 options inline, >= 6 opens searchable modal) - matches sub_180036130
bool render_mode_dropdown(
    ClickGui* gui,
    GuiRenderer* renderer,
    const char* label,
    config::DropdownAnimState* anim,
    uint32_t option_count,
    const char* const* options,
    int32_t* inout_index,
    bool nullable_zero) noexcept;

// Color swatch button opening the HSL/Hex color picker modal - matches sub_1800369E0
bool render_color_button(
    ClickGui* gui,
    GuiRenderer* renderer,
    const char* label,
    math::Color3f* inout_color) noexcept;

// Dynamic list editor (strings or colored entries, +/- buttons, searchable picker or text prompt) - matches sub_180036D20
bool render_dynamic_list(
    ClickGui* gui,
    GuiRenderer* renderer,
    const char* label,
    const char* prompt_title,
    config::ListInputValidatorFn validator,
    config::DynamicListItemKind item_kind,
    config::DynamicListSetting* setting,
    const char* const* picker_items,
    size_t picker_item_count) noexcept;

// Keybind pill + optional HOLD pill - matches sub_180034850
void render_keybind_pill(
    ClickGui* gui,
    GuiRenderer* renderer,
    SetKeybindTargetFn set_bind_cb,
    void* target,
    uint32_t current_key,
    bool* inout_hold_mode,
    const math::Vec4f& bounds) noexcept;

// Input validators used by text boxes & modals - matches sub_180034F00, sub_180036980, sub_180037930, sub_180039C50
size_t validate_profile_name_char(char* buffer, char ch) noexcept;
size_t validate_mc_username_char(char* buffer, char ch) noexcept;
size_t validate_alnum_space_char(char* buffer, char ch) noexcept;
size_t validate_numeric_char(char* buffer, char ch) noexcept;
size_t validate_hex_color_char(char* buffer, char ch) noexcept;

} // namespace slinky::gui::widgets
