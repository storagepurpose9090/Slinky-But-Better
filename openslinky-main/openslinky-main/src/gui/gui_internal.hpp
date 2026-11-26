#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include "config/settings.hpp"
#include "gui/click_gui.hpp"
#include "gui/gui_renderer.hpp"
#include "util/math.hpp"

namespace slinky::gui::detail {

// Theme palette constants extracted from .rdata of libslinky.dll (0x18015CC00..0x18015D360)
inline NVGcolor color_card_bg() noexcept {
    return nvgRGBAf(0.078431375f, 0.078431375f, 0.078431375f, 1.0f); // 20/255
}

inline NVGcolor color_widget_bg() noexcept {
    return nvgRGBAf(0.2509804f, 0.2509804f, 0.2509804f, 1.0f); // 64/255
}

inline NVGcolor color_border() noexcept {
    return nvgRGBAf(0.2509804f, 0.2509804f, 0.2509804f, 1.0f); // 64/255
}

inline NVGcolor color_widget_hover() noexcept {
    return nvgRGBAf(0.3137255f, 0.3137255f, 0.3137255f, 1.0f); // 80/255
}

inline NVGcolor color_text_primary() noexcept {
    return nvgRGBAf(0.8666667f, 0.8666667f, 0.8666667f, 1.0f); // 221/255
}

inline NVGcolor color_text_hover() noexcept {
    return nvgRGBAf(1.0f, 1.0f, 1.0f, 1.0f); // 255/255
}

inline NVGcolor color_text_muted() noexcept {
    return nvgRGBAf(0.6f, 0.6f, 0.6f, 1.0f); // 153/255
}

inline NVGcolor color_text_muted_hover() noexcept {
    return nvgRGBAf(0.73333335f, 0.73333335f, 0.73333335f, 1.0f); // 187/255
}

inline NVGcolor color_danger() noexcept {
    return nvgRGBAf(0.8666667f, 0.2784314f, 0.2784314f, 1.0f); // (221, 71, 71)
}

inline NVGcolor color_danger_hover() noexcept {
    return nvgRGBAf(0.93333334f, 0.34509805f, 0.34509805f, 1.0f); // (238, 88, 88)
}

inline NVGcolor color_dim_overlay() noexcept {
    return nvgRGBAf(0.1882353f, 0.1882353f, 0.1882353f, 1.0f); // 48/255
}

// Converts linear/sRGB [0..1] Color3f into HSL [0..1] - matches sub_180037A40 / sub_1800380C0
inline void rgb_to_hsl(const math::Color3f& rgb, float out_hsl[3]) noexcept {
    const float r = rgb.r;
    const float g = rgb.g;
    const float b = rgb.b;

    const float max_bg = std::isnan(g) ? b : std::fmax(b, g);
    const float max_c = std::isnan(r) ? max_bg : std::fmax(max_bg, r);
    const float min_bg = std::isnan(g) ? b : std::fmin(b, g);
    const float min_c = std::isnan(r) ? min_bg : std::fmin(min_bg, r);

    const float sum = max_c + min_c;
    const float l = 0.5f * sum;
    float s = 0.0f;
    float h = 0.0f;

    if (max_c != min_c) {
        const float d = max_c - min_c;
        const float denom = (l > 0.5f) ? ((2.0f - max_c) - min_c) : sum;
        float hue6 = l;
        if (max_c == r) {
            hue6 = ((g - b) / d) + ((g < b) ? 6.0f : 0.0f);
        } else if (max_c == g) {
            hue6 = ((b - r) / d) + 2.0f;
        } else if (max_c == b) {
            hue6 = ((r - g) / d) + 4.0f;
        }
        s = d / denom;
        h = hue6 / 6.0f;
    }

    out_hsl[0] = h;
    out_hsl[1] = s;
    out_hsl[2] = l;
}

// Renderer internal primitives (implemented in gui_renderer.cpp)
void draw_atlas_icon(
    const GuiRenderer* renderer,
    const math::Vec4f& rect,
    uint32_t icon_id,
    float alpha) noexcept;

float draw_text_adv(
    const GuiRenderer* renderer,
    const math::Vec4f& rect,
    int32_t align_mode,
    uint32_t font_slot,
    const char* text) noexcept;

float draw_text_range_adv(
    const GuiRenderer* renderer,
    math::Vec4f rect,
    int32_t align_mode,
    uint32_t font_slot,
    const char* start,
    const char* end) noexcept;

void draw_text_caret(
    const GuiRenderer* renderer,
    const math::Vec4f& rect,
    const NVGcolor& color,
    float blink_speed,
    float stroke_width) noexcept;

void apply_fade_and_dim_slide(
    const GuiRenderer* renderer,
    float progress) noexcept;

// Inline text input & button helpers (implemented in widgets.cpp)
bool render_action_button(
    ClickGui* gui,
    GuiRenderer* renderer,
    const char* label,
    bool enabled,
    const math::Vec4f& rect) noexcept;

bool activate_or_check_text_input(
    ClickGui* gui,
    GuiRenderer* renderer,
    const math::Vec4f& hit_rect,
    config::ListInputValidatorFn validator,
    void* owner) noexcept;

void render_centered_text_input(
    ClickGui* gui,
    GuiRenderer* renderer,
    const math::Vec4f& rect) noexcept;

void render_aligned_text_input(
    ClickGui* gui,
    GuiRenderer* renderer,
    const math::Vec4f& rect,
    int32_t align_mode) noexcept;

// Modal lifecycle helpers (implemented in modals_and_color_picker.cpp)
void close_modal_internal(ClickGui* gui, bool invoke_dismiss_cb) noexcept;

void open_item_picker_with_validator(
    ClickGui* gui,
    GuiRenderer* renderer,
    const char* title,
    const char* const* items,
    size_t item_count,
    config::ListInputValidatorFn validator,
    void* ctx,
    ItemPickerSelectFn select_cb) noexcept;

} // namespace slinky::gui::detail
