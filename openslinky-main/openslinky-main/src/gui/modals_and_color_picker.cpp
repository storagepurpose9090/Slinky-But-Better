#include "gui/click_gui.hpp"
#include "gui/gui_internal.hpp"
#include "gui/widgets.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "util/containers.hpp"
#include "util/time.hpp"

namespace slinky::gui {

namespace {

// 8-slot "Recently used" ring buffer - matches qword_1803148A0 & flt_1803148B0[8][3]
size_t s_recent_color_head = 0;
math::Color3f s_recent_colors[8]{};

inline bool colors_equal(const math::Color3f& a, const math::Color3f& b) noexcept {
    return a.r == b.r && a.g == b.g && a.b == b.b;
}

inline uint8_t parse_hex_nibble(char ch) noexcept {
    const auto u = static_cast<uint8_t>(ch);
    return (u >= static_cast<uint8_t>('a'))
        ? static_cast<uint8_t>(u - static_cast<uint8_t>('a') + 10u)
        : static_cast<uint8_t>(u - static_cast<uint8_t>('0'));
}

inline float parse_hex_byte_norm(char hi, char lo) noexcept {
    const uint32_t byte_val = (static_cast<uint32_t>(parse_hex_nibble(hi)) << 4u) |
                              static_cast<uint32_t>(parse_hex_nibble(lo));
    return static_cast<float>(static_cast<double>(byte_val) / 255.0);
}

inline bool ascii_icontains(const char* haystack, const char* needle) noexcept {
    if (haystack == nullptr || needle == nullptr) {
        return false;
    }
    const size_t nlen = std::strlen(needle);
    if (nlen == 0u) {
        return true;
    }
    const size_t hlen = std::strlen(haystack);
    if (hlen < nlen) {
        return false;
    }
    const size_t max_pos = hlen - nlen;
    for (size_t pos = 0; pos <= max_pos; ++pos) {
        size_t i = 0;
        for (; i < nlen; ++i) {
            const auto hc_raw = static_cast<uint8_t>(haystack[pos + i]);
            const auto nc_raw = static_cast<uint8_t>(needle[i]);
            const uint8_t hc = (static_cast<uint8_t>(hc_raw - 'A') < 26u)
                ? static_cast<uint8_t>(hc_raw | 0x20u)
                : hc_raw;
            const uint8_t nc = (static_cast<uint8_t>(nc_raw - 'A') < 26u)
                ? static_cast<uint8_t>(nc_raw | 0x20u)
                : nc_raw;
            if (hc != nc) {
                break;
            }
        }
        if (i == nlen) {
            return true;
        }
    }
    return false;
}

// Renders interactive HSL slider bar + circular thumb - matches sub_180039CA0
bool render_hsl_channel_bar(
    ClickGui* gui,
    GuiRenderer* renderer,
    const math::Vec4f& bar_rect,
    uint32_t channel,
    const NVGcolor& thumb_color) noexcept
{
    NVGcontext* vg = renderer->vg;

    nvgStrokeColor(vg, detail::color_border());
    nvgStrokeWidth(vg, 2.0f);
    renderer->stroke_rounded_rect(bar_rect, 5.0f);

    constexpr float kThumbRadius = 10.0f;
    const float thumb_x = (bar_rect.x + kThumbRadius) +
                          (bar_rect.w - 20.0f) * gui->color_picker.hsl[channel];
    const auto thumb_y = static_cast<float>(
        static_cast<double>(bar_rect.y) + static_cast<double>(bar_rect.h) * 0.5);

    nvgBeginPath(vg);
    nvgCircle(vg, thumb_x, thumb_y, kThumbRadius);
    nvgFillColor(vg, thumb_color);
    nvgFill(vg);
    nvgStrokeColor(vg, detail::color_border());
    nvgStrokeWidth(vg, 2.0f);
    nvgStroke(vg);

    if (gui->consume_click_in(bar_rect)) {
        gui->color_picker.dragging_channel[channel] = true;
    } else if (gui->mouse_left_down) {
        if (!gui->color_picker.dragging_channel[channel]) {
            return false;
        }
    } else {
        gui->color_picker.dragging_channel[channel] = false;
        return false;
    }

    const float raw_t = (bar_rect.w != 0.0f)
        ? ((gui->cursor_logical_scrolled.x - bar_rect.x) / bar_rect.w)
        : 0.0f;
    gui->color_picker.hsl[channel] = math::clampf(0.0f, 1.0f, raw_t);
    return true;
}

} // namespace

namespace detail {

// Matches sub_180037E00
void close_modal_internal(ClickGui* gui, bool invoke_dismiss_cb) noexcept {
    gui->focus_mode = GuiFocusMode::None;

    switch (gui->modal_kind) {
        case GuiModalKind::None:
            return;

        case GuiModalKind::TextPrompt: {
            if (invoke_dismiss_cb && gui->modal_primary_cb != nullptr) {
                reinterpret_cast<TextPromptSubmitFn>(gui->modal_primary_cb)(gui->modal_ctx, nullptr);
            }
            break;
        }

        case GuiModalKind::ColorPicker: {
            if (invoke_dismiss_cb && gui->modal_primary_cb != nullptr) {
                reinterpret_cast<ColorPickerApplyFn>(gui->modal_primary_cb)(
                    gui->modal_ctx,
                    &gui->color_picker.rgb);
            }

            const math::Color3f chosen = gui->color_picker.rgb;
            bool already_in_recent = false;
            for (size_t i = 0; i < 8u; ++i) {
                const size_t idx = (s_recent_color_head + i) & 7u;
                if (colors_equal(chosen, s_recent_colors[idx])) {
                    already_in_recent = true;
                    break;
                }
            }
            if (!already_in_recent) {
                s_recent_color_head = (s_recent_color_head == 0u) ? 7u : (s_recent_color_head - 1u);
                s_recent_colors[s_recent_color_head] = chosen;
            }
            break;
        }

        case GuiModalKind::Confirm:
            break;

        case GuiModalKind::ItemPicker: {
            if (invoke_dismiss_cb && gui->item_picker.select_cb != nullptr) {
                gui->item_picker.select_cb(gui->modal_ctx, 0u, nullptr);
            }
            if (gui->item_picker.filtered != nullptr) {
                gui->item_picker.filtered->count = 0u;
                gui->item_picker.filtered->capacity = 0u;
                std::free(gui->item_picker.filtered->data);
                gui->item_picker.filtered->data = nullptr;
                std::free(gui->item_picker.filtered);
                gui->item_picker.filtered = nullptr;
            }
            break;
        }
    }

    gui->modal_kind = GuiModalKind::None;
    gui->modal_closing = false;
}

// Matches sub_180037D30
void open_item_picker_with_validator(
    ClickGui* gui,
    GuiRenderer* renderer,
    const char* title,
    const char* const* items,
    size_t item_count,
    config::ListInputValidatorFn validator,
    void* ctx,
    ItemPickerSelectFn select_cb) noexcept
{
    close_modal_internal(gui, true);
    const double now = (renderer != nullptr && renderer->frame_time > 0.0)
        ? renderer->frame_time
        : time::now_seconds();

    gui->modal_kind = GuiModalKind::ItemPicker;
    gui->modal_closing = false;
    gui->modal_ctx = ctx;
    gui->modal_title = title;
    gui->modal_anim_time = now;
    gui->modal_anim_progress = 0.0f;
    gui->modal_height = 0.0f;
    gui->modal_rendering = false;

    const config::ListInputValidatorFn eff_validator = (validator != nullptr)
        ? validator
        : widgets::validate_alnum_space_char;
    gui->modal_primary_cb = reinterpret_cast<void*>(eff_validator);

    std::memset(gui->modal_payload, 0, sizeof(gui->modal_payload));
    gui->item_picker.select_cb = select_cb;
    gui->item_picker.items = items;
    gui->item_picker.item_count = item_count;
    gui->item_picker.last_filter_time = 0.0;
    gui->item_picker.filtered = nullptr;
    gui->item_picker.overflow_count = 0u;

    gui->begin_text_input(eff_validator, &gui->modal_kind);
    gui->promote_text_input_to_modal();
}

} // namespace detail

// Matches sub_1800380A0
bool ClickGui::has_active_modal() const noexcept {
    return modal_kind != GuiModalKind::None;
}

// Matches sub_180033EB0 / sub_180037A30
void ClickGui::close_modal(double /*current_time*/) noexcept {
    detail::close_modal_internal(this, true);
}

// Matches sub_180037970
void ClickGui::open_text_prompt_modal(
    GuiRenderer* renderer,
    const char* title,
    config::ListInputValidatorFn validator,
    void* ctx,
    TextPromptSubmitFn submit_cb) noexcept
{
    detail::close_modal_internal(this, true);
    const double now = (renderer != nullptr && renderer->frame_time > 0.0)
        ? renderer->frame_time
        : time::now_seconds();

    modal_kind = GuiModalKind::TextPrompt;
    modal_closing = false;
    modal_ctx = ctx;
    modal_title = title;
    modal_anim_time = now;
    modal_anim_progress = 0.0f;
    modal_height = 0.0f;
    modal_rendering = false;
    modal_primary_cb = reinterpret_cast<void*>(submit_cb);

    std::memset(modal_payload, 0, sizeof(modal_payload));
    const config::ListInputValidatorFn eff_validator = (validator != nullptr)
        ? validator
        : widgets::validate_alnum_space_char;
    begin_text_input(eff_validator, &modal_kind);
}

// Matches sub_180037A40
void ClickGui::open_color_picker_modal(
    GuiRenderer* renderer,
    const char* title,
    void* ctx,
    const math::Color3f& initial_rgb,
    ColorPickerApplyFn apply_cb) noexcept
{
    detail::close_modal_internal(this, true);
    const double now = (renderer != nullptr && renderer->frame_time > 0.0)
        ? renderer->frame_time
        : time::now_seconds();

    modal_kind = GuiModalKind::ColorPicker;
    modal_closing = false;
    modal_ctx = ctx;
    modal_title = title;
    modal_anim_time = now;
    modal_anim_progress = 0.0f;
    modal_height = 0.0f;
    modal_rendering = false;
    modal_primary_cb = reinterpret_cast<void*>(apply_cb);

    std::memset(modal_payload, 0, sizeof(modal_payload));
    color_picker.rgb = initial_rgb;
    detail::rgb_to_hsl(initial_rgb, color_picker.hsl);
}

// Matches sub_180037C60
void ClickGui::open_confirm_modal(
    GuiRenderer* renderer,
    void* ctx,
    ConfirmModalFn confirm_cb,
    const char* message) noexcept
{
    detail::close_modal_internal(this, true);
    const double now = (renderer != nullptr && renderer->frame_time > 0.0)
        ? renderer->frame_time
        : time::now_seconds();

    modal_kind = GuiModalKind::Confirm;
    modal_closing = false;
    modal_ctx = ctx;
    modal_title = "Are you sure?";
    modal_anim_time = now;
    modal_anim_progress = 0.0f;
    modal_height = 0.0f;
    modal_rendering = false;
    modal_primary_cb = reinterpret_cast<void*>(confirm_cb);

    std::memset(modal_payload, 0, sizeof(modal_payload));
    char* inline_msg = reinterpret_cast<char*>(modal_payload + sizeof(const char*));
    constexpr size_t kInlineCap = sizeof(modal_payload) - sizeof(const char*);
    if (message != nullptr) {
        std::snprintf(inline_msg, kInlineCap, "%s", message);
    } else {
        inline_msg[0] = '\0';
    }
    confirm.message = inline_msg;
}

// Matches sub_180037D30
void ClickGui::open_item_picker_modal(
    GuiRenderer* renderer,
    const char* title,
    const char* const* items,
    size_t item_count,
    void* ctx,
    ItemPickerSelectFn select_cb) noexcept
{
    detail::open_item_picker_with_validator(
        this,
        renderer,
        title,
        items,
        item_count,
        widgets::validate_alnum_space_char,
        ctx,
        select_cb);
}

// Matches sub_1800380C0
void ClickGui::render_modal_overlay(GuiRenderer* renderer) noexcept {
    NVGcontext* vg = renderer->vg;
    modal_rendering = true;

    const double next_prog = static_cast<double>(modal_anim_progress) +
                             (renderer->frame_time - modal_anim_time) / 0.3;
    modal_anim_progress = (next_prog > 1.000000059604645)
        ? 1.0f
        : static_cast<float>(next_prog);

    set_columns(1);
    const math::Vec4f card_rect = next_cell(modal_height);

    nvgFillColor(vg, detail::color_card_bg());
    renderer->fill_rounded_rect(card_rect, 20.0f);

    math::Vec4f clipped_card = card_rect;
    push_Clipped_card(renderer, clipped_card, 8.0f, 12.0f);
    set_columns(1);

    // Modal header row (title + top-right 'X' close button)
    const math::Vec4f header_row = next_cell(20.0f);
    nvgFontSize(vg, 16.0f);
    nvgFillColor(vg, detail::color_text_primary());
    renderer->draw_text(
        header_row,
        kTextAlignLeftMiddle,
        kFontRegular,
        modal_title != nullptr ? modal_title : "");

    const math::Vec4f close_rect = math::make_rect(
        (header_row.x + header_row.w) - header_row.h,
        header_row.y,
        header_row.h,
        header_row.h);
    const bool close_hovered = is_hovered(close_rect);
    nvgStrokeColor(vg, close_hovered ? detail::color_danger_hover() : detail::color_text_muted());
    nvgStrokeWidth(vg, 2.0f);
    const auto cross_pad = static_cast<float>(static_cast<double>(close_rect.h) * 0.2);
    nvgBeginPath(vg);
    nvgMoveTo(vg, close_rect.x + cross_pad, close_rect.y + cross_pad);
    nvgLineTo(vg, (close_rect.x + close_rect.w) - cross_pad, (close_rect.y + close_rect.h) - cross_pad);
    nvgMoveTo(vg, (close_rect.x + close_rect.w) - cross_pad, close_rect.y + cross_pad);
    nvgLineTo(vg, close_rect.x + cross_pad, (close_rect.y + close_rect.h) - cross_pad);
    nvgStroke(vg);

    const bool close_clicked = consume_click_in(close_rect);
    // close_state: 0 = stay open (unless close_clicked), 1 = cancelled/dismissed, 2 = completed primary action
    int close_state = 1;

    switch (modal_kind) {
        case GuiModalKind::TextPrompt: {
            const math::Vec4f input_box = next_cell(30.0f);
            nvgFontSize(vg, 15.0f);
            nvgStrokeColor(vg, detail::color_border());
            nvgStrokeWidth(vg, 2.0f);
            renderer->stroke_rounded_rect(input_box, 8.0f);

            if (is_editing_text(&modal_kind)) {
                const math::Vec4f inner_rect = math::rect_inset(input_box, 4.0f);
                nvgFillColor(vg, detail::color_text_hover());
                detail::render_centered_text_input(this, renderer, inner_rect);

                const char* submitted = consume_submitted_text();
                if (submitted != nullptr) {
                    if (modal_primary_cb != nullptr) {
                        reinterpret_cast<TextPromptSubmitFn>(modal_primary_cb)(modal_ctx, submitted);
                    }
                    close_state = 2;
                } else {
                    close_state = 0;
                }
            }
            break;
        }

        case GuiModalKind::ColorPicker: {
            const math::Vec4f hex_row = next_cell(24.0f);
            const bool hex_row_hovered = is_hovered(hex_row);

            const math::Vec4f preview_rect = math::make_rect(hex_row.x, hex_row.y, hex_row.h, hex_row.h);
            nvgFillColor(vg, nvgRGBf(color_picker.rgb.r, color_picker.rgb.g, color_picker.rgb.b));
            renderer->fill_rounded_rect(preview_rect, 5.0f);
            nvgStrokeColor(vg, detail::color_border());
            nvgStrokeWidth(vg, 2.0f);
            renderer->stroke_rounded_rect(preview_rect, 5.0f);

            nvgFontSize(vg, 20.0f);
            nvgFillColor(vg, detail::color_text_muted());
            const float hash_x = hex_row.x + hex_row.h + 8.0f;
            const math::Vec4f hash_rect = math::make_rect(
                hash_x,
                hex_row.y,
                (hex_row.x + hex_row.w) - hash_x,
                hex_row.h);
            const float hex_x = detail::draw_text_adv(
                renderer,
                hash_rect,
                kTextAlignLeftMiddle,
                kFontRegular,
                "#");
            const math::Vec4f hex_text_rect = math::make_rect(
                hex_x,
                hex_row.y,
                (hex_row.x + hex_row.w) - hex_x,
                hex_row.h);

            if (detail::activate_or_check_text_input(
                    this,
                    renderer,
                    hex_row,
                    widgets::validate_hex_color_char,
                    &color_picker.hex_owner_tag[0]))
            {
                nvgFillColor(vg, detail::color_text_hover());
                detail::render_aligned_text_input(this, renderer, hex_text_rect, kTextAlignLeftMiddle);

                const char* submitted_hex = consume_submitted_text();
                if (submitted_hex != nullptr) {
                    if (std::strlen(submitted_hex) > 5u) {
                        color_picker.rgb.r = parse_hex_byte_norm(submitted_hex[0], submitted_hex[1]);
                        color_picker.rgb.g = parse_hex_byte_norm(submitted_hex[2], submitted_hex[3]);
                        color_picker.rgb.b = parse_hex_byte_norm(submitted_hex[4], submitted_hex[5]);
                    }
                    detail::rgb_to_hsl(color_picker.rgb, color_picker.hsl);
                }
            } else {
                char hex_buf[8]{};
                const auto r_byte = static_cast<uint8_t>(static_cast<int64_t>(color_picker.rgb.r * 255.0f));
                const auto g_byte = static_cast<uint8_t>(static_cast<int64_t>(color_picker.rgb.g * 255.0f));
                const auto b_byte = static_cast<uint8_t>(static_cast<int64_t>(color_picker.rgb.b * 255.0f));
                std::snprintf(hex_buf, sizeof(hex_buf), "%02x%02x%02x", r_byte, g_byte, b_byte);

                nvgFillColor(
                    vg,
                    hex_row_hovered ? detail::color_text_muted_hover() : detail::color_text_muted());
                renderer->draw_text(hex_text_rect, kTextAlignLeftMiddle, kFontRegular, hex_buf);
            }

            // Hue slider (6 rainbow segments)
            widgets::render_section_header(this, renderer, "Hue");
            const math::Vec4f hue_row = next_cell(15.0f);
            const float seg_w = hue_row.w / 6.0f;
            const float seg_w_ceil = std::ceil(seg_w);

            for (int seg = 0; seg < 6; ++seg) {
                const float seg_x = std::floor(hue_row.x + static_cast<float>(seg) * seg_w);
                const NVGcolor c0 = nvgHSL(
                    static_cast<float>(seg) / 6.0f,
                    color_picker.hsl[1],
                    color_picker.hsl[2]);
                const NVGcolor c1 = nvgHSL(
                    static_cast<float>(seg + 1) / 6.0f,
                    color_picker.hsl[1],
                    color_picker.hsl[2]);
                const NVGpaint seg_paint = nvgLinearGradient(
                    vg,
                    seg_x,
                    hue_row.y,
                    seg_x + seg_w,
                    hue_row.y,
                    c0,
                    c1);

                nvgBeginPath(vg);
                if (seg == 0) {
                    nvgRoundedRectVarying(
                        vg, seg_x, hue_row.y, seg_w_ceil, hue_row.h, 5.0f, 0.0f, 0.0f, 5.0f);
                } else if (seg == 5) {
                    nvgRoundedRectVarying(
                        vg, seg_x, hue_row.y, seg_w_ceil, hue_row.h, 0.0f, 5.0f, 5.0f, 0.0f);
                } else {
                    nvgRect(vg, seg_x, hue_row.y, seg_w_ceil, hue_row.h);
                }
                nvgFillPaint(vg, seg_paint);
                nvgFill(vg);
            }

            const NVGcolor hue_thumb = nvgHSL(
                color_picker.hsl[0],
                color_picker.hsl[1],
                color_picker.hsl[2]);
            const bool hue_changed = render_hsl_channel_bar(this, renderer, hue_row, 0u, hue_thumb);

            // Saturation slider
            widgets::render_section_header(this, renderer, "Saturation");
            const NVGcolor sat_c0 = nvgHSL(color_picker.hsl[0], 0.0f, color_picker.hsl[2]);
            const NVGcolor sat_c1 = nvgHSL(color_picker.hsl[0], 1.0f, color_picker.hsl[2]);
            const NVGcolor sat_thumb = nvgLerpRGBA(sat_c0, sat_c1, color_picker.hsl[1]);
            const math::Vec4f sat_row = next_cell(15.0f);
            const NVGpaint sat_paint = nvgLinearGradient(
                vg,
                sat_row.x,
                sat_row.y,
                sat_row.x + sat_row.w,
                sat_row.y + sat_row.h,
                sat_c0,
                sat_c1);
            nvgFillPaint(vg, sat_paint);
            renderer->fill_rounded_rect(sat_row, 5.0f);
            const bool sat_changed = render_hsl_channel_bar(this, renderer, sat_row, 1u, sat_thumb);

            // Lightness slider
            widgets::render_section_header(this, renderer, "Lightness");
            const NVGcolor light_c0 = nvgHSL(color_picker.hsl[0], color_picker.hsl[1], 0.0f);
            const NVGcolor light_c1 = nvgHSL(color_picker.hsl[0], color_picker.hsl[1], 1.0f);
            const NVGcolor light_thumb = nvgLerpRGBA(light_c0, light_c1, color_picker.hsl[2]);
            const math::Vec4f light_row = next_cell(15.0f);
            const NVGpaint light_paint = nvgLinearGradient(
                vg,
                light_row.x,
                light_row.y,
                light_row.x + light_row.w,
                light_row.y + light_row.h,
                light_c0,
                light_c1);
            nvgFillPaint(vg, light_paint);
            renderer->fill_rounded_rect(light_row, 5.0f);
            const bool light_changed = render_hsl_channel_bar(this, renderer, light_row, 2u, light_thumb);

            if (hue_changed || sat_changed || light_changed) {
                const NVGcolor updated = nvgHSL(
                    color_picker.hsl[0],
                    color_picker.hsl[1],
                    color_picker.hsl[2]);
                color_picker.rgb = math::Color3f{updated.r, updated.g, updated.b};
            }

            // Recently used palette (8 swatches)
            widgets::render_section_header(this, renderer, "Recently used");
            const math::Vec4f recent_row = next_cell(25.0f);
            float swatch_x = recent_row.x;
            const float swatch_w = (recent_row.w - 56.0f) * 0.125f;
            nvgStrokeWidth(vg, 2.0f);

            size_t clicked_recent = 8u;
            for (size_t i = 0; i < 8u; ++i) {
                const size_t ring_idx = (s_recent_color_head + i) & 7u;
                const math::Vec4f swatch_rect = math::make_rect(
                    swatch_x,
                    recent_row.y,
                    swatch_w,
                    recent_row.h);
                const NVGcolor border_col = is_hovered(swatch_rect)
                    ? detail::color_widget_hover()
                    : detail::color_border();
                nvgStrokeColor(vg, border_col);

                if (consume_click_in(swatch_rect)) {
                    clicked_recent = ring_idx;
                }

                const math::Color3f& entry = s_recent_colors[ring_idx];
                nvgBeginPath(vg);
                nvgRoundedRect(vg, swatch_rect.x, swatch_rect.y, swatch_rect.w, swatch_rect.h, 5.0f);
                nvgFillColor(vg, nvgRGBf(entry.r, entry.g, entry.b));
                nvgFill(vg);
                nvgStroke(vg);

                swatch_x += swatch_w + 8.0f;
            }

            if (clicked_recent < 8u) {
                color_picker.rgb = s_recent_colors[clicked_recent];
                detail::rgb_to_hsl(color_picker.rgb, color_picker.hsl);
            }

            close_state = 0;
            break;
        }

        case GuiModalKind::Confirm: {
            const math::Vec4f msg_row = next_cell(30.0f);
            nvgFontSize(vg, 15.0f);
            nvgFillColor(vg, detail::color_text_primary());
            renderer->draw_text(
                msg_row,
                kTextAlignLeftMiddle,
                kFontRegular,
                confirm.message != nullptr ? confirm.message : "");

            const math::Vec4f btn_row = next_cell(30.0f);
            const NVGcolor btn_bg = is_hovered(btn_row)
                ? detail::color_danger_hover()
                : detail::color_danger();
            nvgFillColor(vg, btn_bg);
            renderer->fill_rounded_rect(btn_row, 5.0f);

            nvgFillColor(vg, detail::color_text_hover());
            renderer->draw_text(btn_row, kTextAlignCenterMiddle, kFontRegular, "Continue");

            if (consume_click_in(btn_row)) {
                if (modal_primary_cb != nullptr) {
                    reinterpret_cast<ConfirmModalFn>(modal_primary_cb)(modal_ctx);
                }
                close_state = 2;
            } else {
                close_state = 0;
            }
            break;
        }

        case GuiModalKind::ItemPicker: {
            const math::Vec4f search_box = next_cell(30.0f);
            nvgFontSize(vg, 15.0f);
            nvgStrokeColor(vg, detail::color_border());
            nvgStrokeWidth(vg, 2.0f);
            renderer->stroke_rounded_rect(search_box, 8.0f);

            if (!is_editing_text(&modal_kind)) {
                break;
            }

            const math::Vec4f inner_search = math::rect_inset(search_box, 4.0f);
            nvgFillColor(vg, detail::color_text_hover());
            detail::render_centered_text_input(this, renderer, inner_search);

            const char* query = current_text_buffer();
            if ((renderer->frame_time - item_picker.last_filter_time) >= 0.25) {
                if (item_picker.filtered == nullptr) {
                    item_picker.filtered = static_cast<PickerSearchList*>(
                        util::zero_alloc(1u, sizeof(PickerSearchList)));
                }
                if (item_picker.filtered != nullptr) {
                    item_picker.filtered->count = 0u;
                    item_picker.filtered->capacity = 0u;
                    std::free(item_picker.filtered->data);
                    item_picker.filtered->data = nullptr;
                    item_picker.overflow_count = 0u;

                    if (item_picker.items != nullptr) {
                        for (size_t i = 0; i < item_picker.item_count; ++i) {
                            const char* candidate = item_picker.items[i];
                            if (ascii_icontains(candidate, query != nullptr ? query : "")) {
                                if (item_picker.filtered->count >= 30u) {
                                    ++item_picker.overflow_count;
                                } else if (util::ensure_array_capacity(
                                               reinterpret_cast<void**>(&item_picker.filtered->data),
                                               sizeof(PickerSearchMatch),
                                               item_picker.filtered->count,
                                               &item_picker.filtered->capacity))
                                {
                                    PickerSearchMatch& m =
                                        item_picker.filtered->data[item_picker.filtered->count++];
                                    m.option_index = i;
                                    m.label = candidate;
                                }
                            }
                        }
                    }
                    item_picker.last_filter_time = renderer->frame_time;
                }
            }

            const char* selected_label = nullptr;
            size_t selected_index = 0u;

            if (item_picker.filtered != nullptr && item_picker.filtered->count > 0u) {
                nvgFontSize(vg, 16.0f);
                math::Vec4f pair_row{};
                for (size_t i = 0; i < item_picker.filtered->count; ++i) {
                    math::Vec4f cell_rect{};
                    if ((i & 1u) == 0u) {
                        pair_row = next_cell(25.0f);
                        cell_rect = math::make_rect(
                            pair_row.x,
                            pair_row.y,
                            pair_row.w * 0.5f,
                            pair_row.h);
                    } else {
                        const float half_w = pair_row.w * 0.5f;
                        cell_rect = math::make_rect(
                            pair_row.x + half_w,
                            pair_row.y,
                            half_w,
                            pair_row.h);
                    }

                    const PickerSearchMatch& match = item_picker.filtered->data[i];
                    nvgFillColor(
                        vg,
                        is_hovered(cell_rect) ? detail::color_text_hover() : detail::color_text_primary());
                    renderer->draw_text(cell_rect, kTextAlignLeftMiddle, kFontRegular, match.label);

                    if (consume_click_in(cell_rect)) {
                        selected_label = match.label;
                        selected_index = match.option_index;
                    }
                }

                if (item_picker.overflow_count > 0u) {
                    const size_t clamped_more = (item_picker.overflow_count > 9999u)
                        ? 9999u
                        : item_picker.overflow_count;
                    char more_buf[16]{};
                    std::snprintf(
                        more_buf,
                        sizeof(more_buf),
                        "+%d more...",
                        static_cast<int>(clamped_more));

                    const math::Vec4f more_row = next_cell(25.0f);
                    nvgFillColor(vg, detail::color_text_muted());
                    nvgFontSize(vg, 15.0f);
                    renderer->draw_text(more_row, kTextAlignLeftMiddle, kFontRegular, more_buf);
                }
            }

            if (selected_label != nullptr) {
                if (item_picker.select_cb != nullptr) {
                    item_picker.select_cb(modal_ctx, selected_index, selected_label);
                }
                close_state = 2;
            } else {
                close_state = 0;
            }
            break;
        }

        case GuiModalKind::None:
        default:
            break;
    }

    modal_height = pop_layout(renderer);
    modal_rendering = false;
    modal_anim_time = renderer->frame_time;

    if (close_state != 0) {
        detail::close_modal_internal(this, close_state == 1);
    } else if (close_clicked) {
        detail::close_modal_internal(this, true);
    }
}

} // namespace slinky::gui
