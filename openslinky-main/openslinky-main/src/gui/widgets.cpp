#include "gui/widgets.hpp"
#include "gui/gui_internal.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include "config/profile_manager.hpp"
#include "util/keycodes.hpp"

namespace slinky::gui {

namespace {

// Parses optional '-' + digits + optional ('.' or ',') + fractional digits - matches sub_1800357A0
float parse_slider_text_value(const char* str) noexcept {
    if (str == nullptr) {
        return 0.0f;
    }
    const bool negative = (*str == '-');
    const char* p = str + (negative ? 1 : 0);

    int32_t int_part = 0;
    while (static_cast<uint32_t>(static_cast<uint8_t>(*p) - '0') <= 9u) {
        int_part = int_part * 10 + (static_cast<uint8_t>(*p) - '0');
        ++p;
    }

    const int32_t sign = negative ? -1 : 1;
    if ((static_cast<uint8_t>(*p) | 2u) == static_cast<uint8_t>('.')) {
        ++p;
        float frac_part = 0.0f;
        float place = 0.1f;
        while (static_cast<uint32_t>(static_cast<uint8_t>(*p) - '0') <= 9u) {
            frac_part += static_cast<float>(static_cast<uint8_t>(*p) - '0') * place;
            place *= 0.1f;
            ++p;
        }
        return (static_cast<float>(int_part) + frac_part) * static_cast<float>(sign);
    }

    return static_cast<float>(sign * int_part);
}

// Updates a single slider thumb (mouse drag + Ctrl+Click numeric text input) - matches sub_1800357A0
bool update_slider_thumb_input(
    ClickGui* gui,
    const math::Vec4f& track_rect,
    const math::Vec4f& hit_rect,
    float min_val,
    float max_val,
    float step,
    float display_scale,
    bool* inout_dragging,
    float* inout_value) noexcept
{
    if (gui->consume_click_in(hit_rect)) {
        *inout_dragging = true;
        if (gui->ctrl_held) {
            gui->begin_text_input(widgets::validate_numeric_char, inout_value);
        }
    } else if (!gui->mouse_left_down && !gui->is_editing_text(inout_value)) {
        *inout_dragging = false;
    }

    const bool active = *inout_dragging;
    if (active) {
        float new_val = 0.0f;
        if (gui->is_editing_text(inout_value)) {
            const char* submitted = gui->consume_submitted_text();
            if (submitted == nullptr) {
                return active;
            }
            new_val = parse_slider_text_value(submitted);
            if (display_scale != 0.0f) {
                new_val /= display_scale;
            }
        } else {
            const float t = (track_rect.w != 0.0f)
                ? ((gui->cursor_logical_scrolled.x - track_rect.x) / track_rect.w)
                : 0.0f;
            new_val = min_val + (max_val - min_val) * t;
        }

        if (step != 0.0f) {
            new_val = std::round(new_val / step) * step;
        }
        if (new_val <= min_val) {
            new_val = min_val;
        } else if (new_val >= max_val) {
            new_val = max_val;
        }
        *inout_value = new_val;
    }
    return active;
}

// Callback for >= 6 option ModeSetting searchable picker - matches sub_1800369D0
void on_dropdown_item_selected(void* ctx, size_t index, const char* label) noexcept {
    if (label != nullptr && ctx != nullptr) {
        auto* dst = static_cast<int32_t*>(ctx);
        const auto next_val = static_cast<int32_t>(index + 1);
        if (*dst != next_val) {
            *dst = next_val;
            config::mark_profile_dirty(true);
        }
    }
}

// Callback for ColorPickerApplyFn writing back to math::Color3f - matches sub_180036D00
void on_color_picker_apply(void* ctx, const math::Color3f* rgb) noexcept {
    if (ctx != nullptr && rgb != nullptr) {
        *static_cast<math::Color3f*>(ctx) = *rgb;
    }
}

// Callback for DynamicListSetting searchable picker add - matches sub_1800375D0
void on_dynamic_list_picker_selected(void* ctx, size_t /*index*/, const char* label) noexcept {
    if (label != nullptr && ctx != nullptr) {
        auto* setting = static_cast<config::DynamicListSetting*>(ctx);
        if (setting->add_item != nullptr) {
            const math::Color3f default_red{1.0f, 0.0f, 0.0f};
            if (setting->add_item(setting->ctx, label, &default_red)) {
                setting->modified = true;
                config::mark_profile_dirty(true);
            }
        }
    }
}

// Callback for DynamicListSetting text prompt add - matches sub_180037610
void on_dynamic_list_text_submitted(void* ctx, const char* text) noexcept {
    if (text != nullptr && ctx != nullptr) {
        auto* setting = static_cast<config::DynamicListSetting*>(ctx);
        if (setting->add_item != nullptr) {
            const math::Color3f default_red{1.0f, 0.0f, 0.0f};
            if (setting->add_item(setting->ctx, text, &default_red)) {
                setting->modified = true;
                config::mark_profile_dirty(true);
            }
        }
    }
}

// Draws the small square 'X' remove/clear icon inside `btn_rect`
void draw_small_close_cross(
    NVGcontext* vg,
    const math::Vec4f& btn_rect,
    bool hovered,
    float inset_ratio) noexcept
{
    nvgStrokeColor(vg, hovered ? detail::color_danger_hover() : detail::color_border());
    nvgStrokeWidth(vg, 2.0f);
    const float pad = static_cast<float>(static_cast<double>(btn_rect.h) * static_cast<double>(inset_ratio));
    const float x0 = btn_rect.x + pad;
    const float y0 = btn_rect.y + pad;
    const float x1 = btn_rect.x + btn_rect.w - pad;
    const float y1 = btn_rect.y + btn_rect.h - pad;

    nvgBeginPath(vg);
    nvgMoveTo(vg, x0, y0);
    nvgLineTo(vg, x1, y1);
    nvgMoveTo(vg, x1, y0);
    nvgLineTo(vg, x0, y1);
    nvgStroke(vg);
}

} // namespace

namespace detail {

// Matches sub_180037650
bool render_action_button(
    ClickGui* gui,
    GuiRenderer* renderer,
    const char* label,
    bool enabled,
    const math::Vec4f& rect) noexcept
{
    NVGcontext* vg = renderer->vg;
    const bool clicked = enabled ? gui->consume_click_in(rect) : false;

    nvgFillColor(vg, color_widget_bg());
    renderer->fill_rounded_rect(rect, 8.0f);

    nvgFillColor(vg, enabled ? color_text_primary() : color_text_muted());
    nvgFontSize(vg, 15.0f);
    renderer->draw_text(rect, kTextAlignCenterMiddle, kFontRegular, label);
    return clicked;
}

// Matches sub_180037770
bool activate_or_check_text_input(
    ClickGui* gui,
    GuiRenderer* /*renderer*/,
    const math::Vec4f& hit_rect,
    config::ListInputValidatorFn validator,
    void* owner) noexcept
{
    if (gui->consume_click_in(hit_rect)) {
        gui->begin_text_input(validator, owner);
    }
    return gui->is_editing_text(owner);
}

// Matches sub_1800377C0
void render_centered_text_input(
    ClickGui* gui,
    GuiRenderer* renderer,
    const math::Vec4f& rect) noexcept
{
    const float end_x = draw_text_adv(
        renderer,
        rect,
        kTextAlignCenterMiddle,
        kFontRegular,
        gui->text_input.buffer);
    const math::Vec4f caret_rect = math::make_rect(end_x + 1.0f, rect.y, rect.w, rect.h);
    draw_text_caret(renderer, caret_rect, color_text_primary(), 8.0f, 1.0f);
}

// Matches sub_180037870
void render_aligned_text_input(
    ClickGui* gui,
    GuiRenderer* renderer,
    const math::Vec4f& rect,
    int32_t align_mode) noexcept
{
    const float end_x = draw_text_adv(
        renderer,
        rect,
        align_mode,
        kFontRegular,
        gui->text_input.buffer);
    const math::Vec4f caret_rect = math::make_rect(end_x + 1.0f, rect.y, rect.w, rect.h);
    draw_text_caret(renderer, caret_rect, color_text_primary(), 8.0f, 1.0f);
}

} // namespace detail

namespace widgets {

// Matches sub_180034F00
size_t validate_profile_name_char(char* buffer, char ch) noexcept {
    const size_t len = std::strlen(buffer);
    const auto uch = static_cast<uint8_t>(ch);
    if (len < 15u && static_cast<uint8_t>(uch - 32u) < 95u && ch != '/' && ch != '\\') {
        buffer[len] = ch;
        buffer[len + 1] = '\0';
    }
    return len;
}

// Minecraft cracked-account names: 1-16 chars, letters/digits/underscore only.
size_t validate_mc_username_char(char* buffer, char ch) noexcept {
    const size_t len = std::strlen(buffer);
    const auto uch = static_cast<uint8_t>(ch);
    const bool valid =
        (uch >= static_cast<uint8_t>('A') && uch <= static_cast<uint8_t>('Z')) ||
        (uch >= static_cast<uint8_t>('a') && uch <= static_cast<uint8_t>('z')) ||
        (uch >= static_cast<uint8_t>('0') && uch <= static_cast<uint8_t>('9')) ||
        ch == '_';
    if (len < 16u && valid) {
        buffer[len] = ch;
        buffer[len + 1] = '\0';
    }
    return len;
}

// Matches sub_180036980
size_t validate_alnum_space_char(char* buffer, char ch) noexcept {
    const size_t len = std::strlen(buffer);
    const auto uch = static_cast<uint8_t>(ch);
    if (len <= 99u &&
        (static_cast<uint8_t>((uch & 0xDFu) - 'A') < 26u ||
         static_cast<uint8_t>(uch - '0') < 10u ||
         ch == ' ')) {
        buffer[len] = ch;
        buffer[len + 1] = '\0';
    }
    return len;
}

// Matches sub_180037930
size_t validate_numeric_char(char* buffer, char ch) noexcept {
    const size_t len = std::strlen(buffer);
    const auto uch = static_cast<uint8_t>(ch);
    if (len != 100u &&
        (static_cast<uint8_t>(uch - ',') < 3u || static_cast<uint8_t>(uch - '0') < 10u)) {
        buffer[len] = ch;
        buffer[len + 1] = '\0';
    }
    return len;
}

// Matches sub_180039C50
size_t validate_hex_color_char(char* buffer, char ch) noexcept {
    const size_t len = std::strlen(buffer);
    if (len <= 5u) {
        auto uch = static_cast<uint8_t>(ch);
        if (static_cast<uint8_t>(uch - 'A') <= 5u) {
            uch |= 0x20u;
        } else if (static_cast<uint8_t>(uch - '0') >= 10u && static_cast<uint8_t>(uch - 'a') >= 6u) {
            return len;
        }
        buffer[len] = static_cast<char>(uch);
        buffer[len + 1] = '\0';
    }
    return len;
}

// Matches sub_180034850
void render_keybind_pill(
    ClickGui* gui,
    GuiRenderer* renderer,
    SetKeybindTargetFn set_bind_cb,
    void* target,
    uint32_t current_key,
    bool* inout_hold_mode,
    const math::Vec4f& bounds) noexcept
{
    static float s_key_width_cache[input::KEY_COUNT]{};

    NVGcontext* vg = renderer->vg;
    nvgFontSize(vg, 12.0f);
    renderer->select_font(kFontBold);

    const bool capturing = gui->is_capturing_keybind(target);
    float text_w = 0.0f;
    if (capturing) {
        text_w = std::ceil(nvgTextBounds(vg, 0.0f, 0.0f, "PRESS A KEY...", nullptr, nullptr));
    } else {
        const uint32_t safe_key = (current_key < static_cast<uint32_t>(input::KEY_COUNT)) ? current_key : 0u;
        text_w = s_key_width_cache[safe_key];
        if (text_w == 0.0f) {
            const char* key_name = input::get_key_name(static_cast<int>(safe_key));
            text_w = std::ceil(nvgTextBounds(vg, 0.0f, 0.0f, key_name, nullptr, nullptr));
            s_key_width_cache[safe_key] = text_w;
        }
    }

    const math::Vec4f pill_rect = math::make_rect(bounds.x, bounds.y, text_w + 12.0f, bounds.h);
    const NVGcolor base_bg = (current_key != 0u) ? gui->accent_color : detail::color_text_muted();
    nvgFillColor(vg, nvgTransRGBAf(base_bg, 0.15f));
    renderer->fill_rounded_rect(pill_rect, 5.0f);

    NVGcolor label_color = detail::color_text_muted();
    if (gui->is_hovered(pill_rect)) {
        label_color = detail::color_text_hover();
    } else if (current_key != 0u) {
        label_color = gui->accent_color;
    }
    nvgFillColor(vg, label_color);

    const char* key_label = capturing
        ? "PRESS A KEY..."
        : input::get_key_name(static_cast<int>(current_key));
    renderer->draw_text(pill_rect, kTextAlignCenterMiddle, kFontBold, key_label);

    if (gui->consume_click_in(pill_rect)) {
        gui->begin_keybind_capture(set_bind_cb, target);
    }

    if (current_key != 0u) {
        if (inout_hold_mode == nullptr) {
            return;
        }
        const float hold_text_w = std::ceil(nvgTextBounds(vg, 0.0f, 0.0f, "HOLD", nullptr, nullptr));
        const math::Vec4f hold_rect = math::make_rect(
            pill_rect.x + pill_rect.w + 5.0f,
            pill_rect.y,
            hold_text_w + 12.0f,
            pill_rect.h);

        if (!*inout_hold_mode && !gui->is_hovered(bounds)) {
            return;
        }

        const NVGcolor hold_bg = *inout_hold_mode
            ? detail::color_text_primary()
            : detail::color_text_muted();
        nvgFillColor(vg, nvgTransRGBAf(hold_bg, 0.15f));
        renderer->fill_rounded_rect(hold_rect, 5.0f);

        const NVGcolor hold_fg = gui->is_hovered(hold_rect)
            ? detail::color_text_primary()
            : detail::color_text_muted();
        nvgFillColor(vg, hold_fg);
        renderer->draw_text(hold_rect, kTextAlignCenterMiddle, kFontBold, "HOLD");

        if (gui->consume_click_in(hold_rect)) {
            *inout_hold_mode = !*inout_hold_mode;
        }
    } else if (inout_hold_mode != nullptr) {
        *inout_hold_mode = false;
    }
}

// Matches sub_180034F50
bool render_toggle_pill(
    ClickGui* gui,
    GuiRenderer* renderer,
    math::Vec4f pill_rect,
    config::ToggleAnimState* anim,
    bool* inout_value) noexcept
{
    const double now = renderer->frame_time;
    const auto dt = static_cast<float>(now - anim->last_update_time);
    const double dt_d = static_cast<double>(dt);
    const bool cur_val = *inout_value;

    float prog = anim->progress;
    if (dt_d <= 0.1) {
        if (cur_val) {
            if (prog < 1.0f) {
                prog = std::fmin(static_cast<float>(dt_d / 0.1 + static_cast<double>(prog)), 1.0f);
                anim->progress = prog;
            }
        } else {
            if (prog > 0.0f) {
                prog = std::fmax(static_cast<float>(dt_d / -0.1 + static_cast<double>(prog)), 0.0f);
                anim->progress = prog;
            }
        }
    } else {
        prog = cur_val ? 1.0f : 0.0f;
        anim->progress = prog;
    }
    anim->last_update_time = now;

    NVGcontext* vg = renderer->vg;
    const float smooth_t = math::smoothstep01(prog);
    const NVGcolor off_col = gui->is_hovered(pill_rect)
        ? detail::color_widget_hover()
        : detail::color_widget_bg();
    const NVGcolor pill_col = nvgLerpRGBA(off_col, gui->accent_color, smooth_t);
    nvgFillColor(vg, pill_col);
    renderer->fill_rounded_rect(pill_rect, 10.0f);

    const bool clicked = gui->consume_click_in(pill_rect);
    if (clicked) {
        *inout_value = !*inout_value;
    }

    pill_rect = math::rect_inset(pill_rect, 6.0f);
    const auto knob_r = static_cast<float>(static_cast<int32_t>(pill_rect.h));
    const float half_r = knob_r * 0.5f;
    const float knob_x = math::lerpf(
        pill_rect.x + half_r,
        (pill_rect.x + pill_rect.w) - half_r,
        smooth_t);
    const float knob_y = pill_rect.y + half_r;

    nvgFillColor(vg, detail::color_card_bg());
    nvgBeginPath(vg);
    nvgCircle(vg, knob_x, knob_y, knob_r);
    nvgFill(vg);

    return clicked;
}

// Matches sub_1800351C0
bool render_bool_toggle(
    ClickGui* gui,
    GuiRenderer* renderer,
    const char* label,
    config::ToggleAnimState* anim,
    bool* inout_value) noexcept
{
    NVGcontext* vg = renderer->vg;
    const math::Vec4f row = gui->next_cell(20.0f);

    nvgFillColor(vg, detail::color_text_primary());
    nvgFontSize(vg, 15.0f);
    renderer->draw_text(row, kTextAlignLeftMiddle, kFontRegular, label);

    const math::Vec4f pill_rect = math::make_rect((row.x + row.w) - 40.0f, row.y, 40.0f, row.h);
    return render_toggle_pill(gui, renderer, pill_rect, anim, inout_value);
}

// Matches sub_1800352C0
void render_section_header(
    ClickGui* gui,
    GuiRenderer* renderer,
    const char* title) noexcept
{
    NVGcontext* vg = renderer->vg;
    math::Vec4f row = gui->next_cell(19.0f);
    row.y += 6.0f;

    nvgFillColor(vg, detail::color_text_muted());
    nvgFontSize(vg, 15.0f);
    renderer->draw_text(row, kTextAlignLeftMiddle, kFontRegular, title);
}

// Matches sub_180035370
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
    float* inout_value) noexcept
{
    NVGcontext* vg = renderer->vg;
    const math::Vec4f row = gui->next_cell(20.0f);

    nvgFillColor(vg, detail::color_text_primary());
    nvgFontSize(vg, 15.0f);
    renderer->draw_text(row, kTextAlignLeftMiddle, kFontRegular, label);

    const double half_w = static_cast<double>(row.w) * 0.5;
    const auto track_x = static_cast<float>(static_cast<double>(row.x) + half_w);
    const float track_y = (row.y + 10.0f) - 2.0f;
    const auto track_w = static_cast<float>(half_w - 2.0);
    const math::Vec4f track_rect = math::make_rect(track_x, track_y, track_w, 4.0f);

    const NVGcolor bg_col = gui->is_hovered(row)
        ? detail::color_widget_hover()
        : detail::color_widget_bg();
    nvgFillColor(vg, bg_col);
    renderer->fill_rounded_rect(track_rect, 2.0f);

    const float denom = max_val - min_val;
    const float target_t = (denom != 0.0f)
        ? math::clampf(0.0f, 1.0f, (*inout_value - min_val) / denom)
        : 0.0f;
    const auto dt = static_cast<float>(renderer->frame_time - anim->last_update_time);
    const float visual_t = math::lerpf_clamped(
        anim->visual_t,
        target_t,
        static_cast<float>(static_cast<double>(dt) / 0.1));
    anim->visual_t = visual_t;
    anim->last_update_time = renderer->frame_time;

    const float thumb_x = math::lerpf(track_x, track_x + track_w, visual_t);
    const math::Vec4f fill_rect = math::make_rect(track_x, track_y, thumb_x - track_x, 4.0f);
    nvgFillColor(vg, gui->accent_color);
    renderer->fill_rounded_rect(fill_rect, 2.0f);

    nvgBeginPath(vg);
    nvgCircle(vg, thumb_x, track_y + 2.0f, 6.0f);
    nvgFill(vg);

    const bool changed = update_slider_thumb_input(
        gui,
        track_rect,
        row,
        min_val,
        max_val,
        step,
        display_scale,
        &anim->dragging,
        inout_value);

    if (anim->dragging || (gui->is_hovered(row) && !gui->mouse_left_down)) {
        char val_buf[64]{};
        const char* tip_str = nullptr;
        if (gui->is_editing_text(inout_value)) {
            tip_str = gui->text_input.buffer;
        } else {
            const float shown = (display_scale != 0.0f) ? (*inout_value * display_scale) : *inout_value;
            const char* fmt = (format != nullptr) ? format : "%0.2f";
            std::snprintf(val_buf, sizeof(val_buf), fmt, static_cast<double>(shown));
            tip_str = val_buf;
        }
        gui->show_tooltip(
            tip_str,
            static_cast<float>(static_cast<int32_t>(thumb_x)),
            static_cast<float>(static_cast<int32_t>(track_y)));
    }

    return changed;
}

// Matches sub_1800359B0
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
    float* inout_max) noexcept
{
    NVGcontext* vg = renderer->vg;
    const math::Vec4f row = gui->next_cell(20.0f);

    nvgFillColor(vg, detail::color_text_primary());
    nvgFontSize(vg, 15.0f);
    renderer->draw_text(row, kTextAlignLeftMiddle, kFontRegular, label);

    const double half_w = static_cast<double>(row.w) * 0.5;
    const auto track_x = static_cast<float>(static_cast<double>(row.x) + half_w);
    const float track_y = (row.y + 10.0f) - 2.0f;
    const auto track_w = static_cast<float>(half_w - 2.0);
    const math::Vec4f track_rect = math::make_rect(track_x, track_y, track_w, 4.0f);

    const NVGcolor bg_col = gui->is_hovered(row)
        ? detail::color_widget_hover()
        : detail::color_widget_bg();
    nvgFillColor(vg, bg_col);
    renderer->fill_rounded_rect(track_rect, 2.0f);

    const float denom = max_val - min_val;
    const float target_min_t = (denom != 0.0f)
        ? math::clampf(0.0f, 1.0f, (*inout_min - min_val) / denom)
        : 0.0f;
    const float target_max_t = (denom != 0.0f)
        ? math::clampf(0.0f, 1.0f, (*inout_max - min_val) / denom)
        : 0.0f;

    const auto dt = static_cast<float>(renderer->frame_time - anim->last_update_time);
    const auto step_t = static_cast<float>(static_cast<double>(dt) / 0.1);
    anim->visual_min_t = math::lerpf_clamped(anim->visual_min_t, target_min_t, step_t);
    anim->visual_max_t = math::lerpf_clamped(anim->visual_max_t, target_max_t, step_t);
    anim->last_update_time = renderer->frame_time;

    const float min_thumb_x = math::lerpf(track_x, track_x + track_w, anim->visual_min_t);
    const float max_thumb_x = math::lerpf(track_x, track_x + track_w, anim->visual_max_t);

    const math::Vec4f active_rect = math::make_rect(
        min_thumb_x,
        track_y,
        max_thumb_x - min_thumb_x,
        4.0f);
    nvgFillColor(vg, gui->accent_color);
    renderer->fill_rounded_rect(active_rect, 2.0f);

    nvgBeginPath(vg);
    nvgCircle(vg, min_thumb_x, track_y + 2.0f, 6.0f);
    nvgFill(vg);

    nvgBeginPath(vg);
    nvgCircle(vg, max_thumb_x, track_y + 2.0f, 6.0f);
    nvgFill(vg);

    const auto mid_x = static_cast<float>(
        static_cast<int32_t>(static_cast<double>(min_thumb_x + max_thumb_x) * 0.5));
    const math::Vec4f min_hit = math::make_rect(
        track_x - 6.0f,
        row.y,
        (mid_x - track_x) + 12.0f,
        row.h);
    const math::Vec4f max_hit = math::make_rect(
        mid_x,
        row.y,
        (track_w - (mid_x - track_x)) + 12.0f,
        row.h);

    char val_buf[64]{};
    if (update_slider_thumb_input(
            gui,
            track_rect,
            min_hit,
            min_val,
            max_val,
            step,
            display_scale,
            &anim->dragging_min,
            inout_min))
    {
        if (*inout_min > *inout_max) {
            *inout_max = *inout_min;
        }
        const char* tip_str = nullptr;
        if (gui->is_editing_text(inout_min)) {
            tip_str = gui->text_input.buffer;
        } else {
            const float shown = (display_scale != 0.0f) ? (*inout_min * display_scale) : *inout_min;
            const char* fmt = (single_format != nullptr) ? single_format : "%0.2f";
            std::snprintf(val_buf, sizeof(val_buf), fmt, static_cast<double>(shown));
            tip_str = val_buf;
        }
        gui->show_tooltip(
            tip_str,
            static_cast<float>(static_cast<int32_t>(min_thumb_x)),
            static_cast<float>(static_cast<int32_t>(track_y)));
        return true;
    }

    if (update_slider_thumb_input(
            gui,
            track_rect,
            max_hit,
            min_val,
            max_val,
            step,
            display_scale,
            &anim->dragging_max,
            inout_max))
    {
        if (*inout_min > *inout_max) {
            *inout_min = *inout_max;
        }
        const char* tip_str = nullptr;
        if (gui->is_editing_text(inout_max)) {
            tip_str = gui->text_input.buffer;
        } else {
            const float shown = (display_scale != 0.0f) ? (*inout_max * display_scale) : *inout_max;
            const char* fmt = (single_format != nullptr) ? single_format : "%0.2f";
            std::snprintf(val_buf, sizeof(val_buf), fmt, static_cast<double>(shown));
            tip_str = val_buf;
        }
        gui->show_tooltip(
            tip_str,
            static_cast<float>(static_cast<int32_t>(max_thumb_x)),
            static_cast<float>(static_cast<int32_t>(track_y)));
        return true;
    }

    if (gui->is_hovered(row) && (!gui->mouse_left_down || anim->dragging_min || anim->dragging_max)) {
        const float shown_min = (display_scale != 0.0f) ? (*inout_min * display_scale) : *inout_min;
        const float shown_max = (display_scale != 0.0f) ? (*inout_max * display_scale) : *inout_max;
        const char* fmt = (range_format != nullptr) ? range_format : "%0.2f - %0.2f";
        std::snprintf(
            val_buf,
            sizeof(val_buf),
            fmt,
            static_cast<double>(shown_min),
            static_cast<double>(shown_max));
        const auto center_tip_x = static_cast<int32_t>(
            static_cast<double>((min_hit.x + min_hit.w) + max_hit.x) * 0.5);
        gui->show_tooltip(
            val_buf,
            static_cast<float>(center_tip_x),
            static_cast<float>(static_cast<int32_t>(track_y)));
    }

    return false;
}

// Matches sub_180036130
bool render_mode_dropdown(
    ClickGui* gui,
    GuiRenderer* renderer,
    const char* label,
    config::DropdownAnimState* anim,
    uint32_t option_count,
    const char* const* options,
    int32_t* inout_index,
    bool nullable_zero) noexcept
{
    NVGcontext* vg = renderer->vg;
    const auto dt = static_cast<float>(renderer->frame_time - anim->last_update_time);
    const float target_h = anim->expanded ? static_cast<float>(28u * option_count) : 20.0f;
    const float cur_h = math::lerpf_clamped(
        anim->current_height,
        target_h,
        static_cast<float>(static_cast<double>(dt) / 0.1));
    anim->current_height = cur_h;
    anim->last_update_time = renderer->frame_time;

    const math::Vec4f row = gui->next_cell(cur_h);
    const math::Vec4f label_rect = math::make_rect(row.x, row.y, row.w, 20.0f);

    nvgFillColor(vg, detail::color_text_primary());
    nvgFontSize(vg, 15.0f);
    renderer->draw_text(label_rect, kTextAlignLeftMiddle, kFontRegular, label);

    const auto box_x = static_cast<float>(static_cast<double>(row.x) + static_cast<double>(row.w) * 0.5);
    const math::Vec4f box_rect = math::make_rect(box_x, row.y, row.w * 0.5f, row.h);

    nvgStrokeColor(vg, detail::color_border());
    nvgStrokeWidth(vg, 2.0f);
    renderer->stroke_rounded_rect(box_rect, 10.0f);

    if (anim->expanded) {
        if (option_count == 0u || options == nullptr) {
            return false;
        }
        nvgStrokeColor(vg, nvgTransRGBAf(detail::color_border(), 0.5f));
        const float item_h = box_rect.h / static_cast<float>(option_count);
        bool changed = false;

        for (uint32_t i = 0; i < option_count; ++i) {
            const math::Vec4f item_rect = math::make_rect(
                box_rect.x,
                box_rect.y + static_cast<float>(i) * item_h,
                box_rect.w,
                item_h);
            const bool hovered = gui->is_hovered(item_rect);
            const bool selected = (static_cast<uint32_t>(*inout_index) == i);

            NVGcolor item_col{};
            if (selected) {
                item_col = gui->accent_color;
            } else if (hovered) {
                item_col = (nullable_zero && i == 0u)
                    ? detail::color_text_muted_hover()
                    : detail::color_text_hover();
            } else {
                item_col = (nullable_zero && i == 0u)
                    ? detail::color_text_muted()
                    : detail::color_text_primary();
            }

            nvgFillColor(vg, item_col);
            renderer->draw_text(item_rect, kTextAlignCenterMiddle, kFontRegular, options[i]);

            if (!changed && gui->consume_click_in(item_rect)) {
                *inout_index = static_cast<int32_t>(i);
                anim->expanded = false;
                changed = true;
            }

            if (i > 0u) {
                nvgBeginPath(vg);
                nvgMoveTo(vg, item_rect.x, item_rect.y);
                nvgLineTo(vg, item_rect.x + item_rect.w, item_rect.y);
                nvgStroke(vg);
            }
        }
        return changed;
    }

    bool box_hovered = gui->is_hovered(box_rect);
    bool cleared = false;
    int32_t cur_idx = *inout_index;

    if (option_count >= 6u && nullable_zero && cur_idx != 0) {
        const math::Vec4f clear_rect = math::make_rect(
            (box_rect.x + box_rect.w) - box_rect.h,
            box_rect.y,
            box_rect.h,
            box_rect.h);
        const bool clear_hovered = gui->is_hovered(clear_rect);
        draw_small_close_cross(vg, clear_rect, clear_hovered, 0.3f);

        if (gui->consume_click_in(clear_rect)) {
            *inout_index = 0;
            cur_idx = 0;
            cleared = true;
        }
        box_hovered = box_hovered && !clear_hovered;
    }

    NVGcolor current_col{};
    if (!nullable_zero || cur_idx != 0) {
        current_col = box_hovered ? detail::color_text_hover() : detail::color_text_primary();
    } else {
        current_col = box_hovered ? detail::color_text_muted_hover() : detail::color_text_muted();
    }

    nvgFillColor(vg, current_col);
    const uint32_t safe_idx = (options != nullptr && static_cast<uint32_t>(cur_idx) < option_count)
        ? static_cast<uint32_t>(cur_idx)
        : 0u;
    const char* cur_label = (options != nullptr && option_count > 0u) ? options[safe_idx] : "";
    renderer->draw_text(box_rect, kTextAlignCenterMiddle, kFontRegular, cur_label);

    if (gui->consume_click_in(box_rect)) {
        if (option_count < 6u) {
            anim->expanded = true;
        } else if (options != nullptr) {
            gui->open_item_picker_modal(
                renderer,
                "Select item",
                options + 1,
                static_cast<size_t>(option_count - 1u),
                inout_index,
                on_dropdown_item_selected);
        }
        return cleared;
    }

    return cleared;
}

// Matches sub_1800369E0
bool render_color_button(
    ClickGui* gui,
    GuiRenderer* renderer,
    const char* label,
    math::Color3f* inout_color) noexcept
{
    NVGcontext* vg = renderer->vg;
    const math::Vec4f row = gui->next_cell(20.0f);
    const math::Vec4f label_rect = math::make_rect(row.x, row.y, row.w, 20.0f);

    nvgFillColor(vg, detail::color_text_primary());
    nvgFontSize(vg, 15.0f);
    renderer->draw_text(label_rect, kTextAlignLeftMiddle, kFontRegular, label);

    const math::Vec4f swatch_rect = math::make_rect((row.x + row.w) - 40.0f, row.y, 40.0f, 20.0f);
    nvgStrokeColor(vg, detail::color_border());
    nvgStrokeWidth(vg, 2.0f);
    renderer->stroke_rounded_rect(swatch_rect, 10.0f);

    const NVGcolor opaque_col = nvgRGBf(inout_color->r, inout_color->g, inout_color->b);
    const NVGcolor faded_col = nvgRGBAf(inout_color->r, inout_color->g, inout_color->b, 0.25f);
    const NVGpaint grad = nvgLinearGradient(
        vg,
        swatch_rect.x,
        swatch_rect.y,
        swatch_rect.x + swatch_rect.w,
        swatch_rect.y,
        opaque_col,
        faded_col);
    nvgFillPaint(vg, grad);
    renderer->fill_rounded_rect(swatch_rect, 10.0f);

    const auto hit_x = static_cast<float>(static_cast<double>(row.x) + static_cast<double>(row.w) * 0.5);
    const math::Vec4f hit_rect = math::make_rect(hit_x, row.y, row.w * 0.5f, row.h);
    if (gui->consume_click_in(hit_rect)) {
        gui->open_color_picker_modal(renderer, label, inout_color, *inout_color, on_color_picker_apply);
        return true;
    }
    return false;
}

// Matches sub_180036D20
bool render_dynamic_list(
    ClickGui* gui,
    GuiRenderer* renderer,
    const char* label,
    const char* prompt_title,
    config::ListInputValidatorFn validator,
    config::DynamicListItemKind item_kind,
    config::DynamicListSetting* setting,
    const char* const* picker_items,
    size_t picker_item_count) noexcept
{
    NVGcontext* vg = renderer->vg;
    const size_t item_count = (setting != nullptr && setting->get_count != nullptr)
        ? setting->get_count(setting->ctx)
        : 0u;
    const size_t total_rows = item_count + 1u;
    const size_t total_px = item_count + 27u * total_rows + 1u;

    const math::Vec4f row = gui->next_cell(static_cast<float>(total_px));
    const math::Vec4f label_rect = math::make_rect(row.x, row.y, row.w, 20.0f);

    nvgFillColor(vg, detail::color_text_primary());
    nvgFontSize(vg, 15.0f);
    renderer->draw_text(label_rect, kTextAlignLeftMiddle, kFontRegular, label);

    const auto box_x = static_cast<float>(static_cast<double>(row.x) + static_cast<double>(row.w) * 0.5);
    const math::Vec4f box_rect = math::make_rect(box_x, row.y, row.w * 0.5f, row.h);

    nvgStrokeColor(vg, detail::color_border());
    nvgStrokeWidth(vg, 2.0f);
    renderer->stroke_rounded_rect(box_rect, 10.0f);

    const float item_h = box_rect.h / static_cast<float>(total_rows);
    bool modified = false;
    size_t remove_index = static_cast<size_t>(-1);

    for (size_t i = 0; i < item_count; ++i) {
        const math::Vec4f item_rect = math::make_rect(
            box_rect.x,
            box_rect.y + static_cast<float>(i) * item_h,
            box_rect.w,
            item_h);

        const char* entry_name = (setting->get_name != nullptr)
            ? setting->get_name(setting->ctx, i)
            : "";
        if (entry_name == nullptr) {
            entry_name = "";
        }
        const size_t name_len = std::strlen(entry_name);
        const size_t clamped_len = (name_len > 20u) ? 20u : name_len;

        nvgFillColor(vg, detail::color_text_primary());
        renderer->draw_text_range(
            item_rect,
            kTextAlignCenterMiddle,
            kFontRegular,
            entry_name,
            entry_name + clamped_len);

        if (item_kind == config::DynamicListItemKind::ColoredEntry && setting->get_color != nullptr) {
            const math::Vec4f color_outer = math::make_rect(item_rect.x, item_rect.y, item_rect.h, item_rect.h);
            const math::Vec4f swatch_rect = math::rect_inset(color_outer, 4.0f);
            math::Color3f* entry_col = setting->get_color(setting->ctx, i);
            if (entry_col != nullptr) {
                nvgFillColor(vg, nvgRGBf(entry_col->r, entry_col->g, entry_col->b));
                renderer->fill_rounded_rect(swatch_rect, 5.0f);
                if (gui->consume_click_in(swatch_rect)) {
                    gui->open_color_picker_modal(
                        renderer,
                        entry_name,
                        entry_col,
                        *entry_col,
                        on_color_picker_apply);
                    modified = true;
                }
            }
        }

        const math::Vec4f del_rect = math::make_rect(
            (item_rect.x + item_rect.w) - item_rect.h,
            item_rect.y,
            item_rect.h,
            item_rect.h);
        const bool del_hovered = gui->is_hovered(del_rect);
        draw_small_close_cross(vg, del_rect, del_hovered, 0.3f);

        if (gui->consume_click_in(del_rect)) {
            remove_index = i;
        }

        nvgStrokeColor(vg, nvgTransRGBAf(detail::color_border(), 0.5f));
        nvgBeginPath(vg);
        nvgMoveTo(vg, item_rect.x, item_rect.y + item_rect.h);
        nvgLineTo(vg, item_rect.x + item_rect.w, item_rect.y + item_rect.h);
        nvgStroke(vg);
    }

    if (remove_index != static_cast<size_t>(-1) && setting != nullptr && setting->remove_at != nullptr) {
        setting->remove_at(setting->ctx, remove_index);
        modified = true;
    }

    const math::Vec4f add_rect = math::make_rect(
        box_rect.x,
        box_rect.y + static_cast<float>(item_count) * item_h,
        box_rect.w,
        item_h);
    const NVGcolor add_col = gui->is_hovered(add_rect)
        ? detail::color_text_muted_hover()
        : detail::color_text_muted();
    nvgFillColor(vg, add_col);
    renderer->draw_text(add_rect, kTextAlignCenterMiddle, kFontRegular, "Click to add");

    if (gui->consume_click_in(add_rect) && setting != nullptr) {
        const config::ListInputValidatorFn eff_validator = (validator != nullptr)
            ? validator
            : validate_alnum_space_char;
        if (picker_items != nullptr) {
            detail::open_item_picker_with_validator(
                gui,
                renderer,
                prompt_title,
                picker_items,
                picker_item_count,
                eff_validator,
                setting,
                on_dynamic_list_picker_selected);
        } else {
            gui->open_text_prompt_modal(
                renderer,
                prompt_title,
                eff_validator,
                setting,
                on_dynamic_list_text_submitted);
        }
        modified = true;
    }

    return modified;
}

} // namespace widgets
} // namespace slinky::gui
