#include "gui/gui_renderer.hpp"
#include "gui/gui_internal.hpp"

#include <cmath>
#include <glad/glad.h>
#include "nanovg.h"
#define NANOVG_GL3
extern "C" {
#include "nanovg_gl.h"
#include "nanovg_gl_utils.h"
}
#include "embedded_assets.hpp"

namespace slinky::gui {

namespace {

// Align mode lookup table matching dword_18015CC0C[3] = {17, 18, 20}
constexpr int kNvgAlignTable[3] = {
    NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE,   // 0 -> 17
    NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, // 1 -> 18
    NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE,  // 2 -> 20
};

// 20x20 sprite-sheet tile origins inside kImageLogoPng matching 0x180178750 (ptr @ 0x18015CC18):
//   0 = Save (0, 0)
//   1 = Delete (20, 0)
//   2 = Open Folder (40, 0)
//   3 = Load (0, 20)
constexpr math::Vec2f kAtlasTileOrigins[4] = {
    {0.0f, 0.0f},
    {20.0f, 0.0f},
    {40.0f, 0.0f},
    {0.0f, 20.0f},
};

} // namespace

namespace detail {

// Matches sub_180031FB0
void draw_atlas_icon(
    const GuiRenderer* renderer,
    const math::Vec4f& rect,
    uint32_t icon_id,
    float alpha) noexcept
{
    NVGcontext* ctx = renderer->vg;
    const uint32_t safe_id = (icon_id < 4u) ? icon_id : 0u;
    const float sx = rect.w / 20.0f;
    const float sy = rect.h / 20.0f;
    const float ox = rect.x - kAtlasTileOrigins[safe_id].x * sx;
    const float oy = rect.y - kAtlasTileOrigins[safe_id].y * sy;
    const float ex = sx * renderer->logo_width;
    const float ey = sy * renderer->logo_height;

    const NVGpaint paint = nvgImagePattern(ctx, ox, oy, ex, ey, 0.0f, renderer->logo_image, alpha);
    nvgBeginPath(ctx);
    nvgRect(ctx, rect.x, rect.y, rect.w, rect.h);
    nvgFillPaint(ctx, paint);
    nvgFill(ctx);
}

// Matches sub_1800320C0 (returns horizontal advance X from nvgText)
float draw_text_adv(
    const GuiRenderer* renderer,
    const math::Vec4f& rect,
    int32_t align_mode,
    uint32_t font_slot,
    const char* text) noexcept
{
    renderer->select_font(font_slot);
    NVGcontext* ctx = renderer->vg;
    const int32_t safe_align = (align_mode >= 0 && align_mode < 3) ? align_mode : 0;
    nvgTextAlign(ctx, kNvgAlignTable[safe_align]);

    float x = rect.x;
    if (safe_align == kTextAlignRightMiddle) {
        x = rect.x + rect.w;
    } else if (safe_align == kTextAlignCenterMiddle) {
        x = static_cast<float>(static_cast<double>(rect.x) + static_cast<double>(rect.w) * 0.5);
    }
    const float y = static_cast<float>(static_cast<double>(rect.y + 1.0f) + static_cast<double>(rect.h) * 0.5);
    return nvgText(ctx, x, y, text, nullptr);
}

// Matches sub_1800321B0 (returns horizontal advance X from nvgText)
float draw_text_range_adv(
    const GuiRenderer* renderer,
    math::Vec4f rect,
    int32_t align_mode,
    uint32_t font_slot,
    const char* start,
    const char* end) noexcept
{
    renderer->select_font(font_slot);
    NVGcontext* ctx = renderer->vg;
    const int32_t safe_align = (align_mode >= 0 && align_mode < 3) ? align_mode : 0;
    nvgTextAlign(ctx, kNvgAlignTable[safe_align]);

    rect.y += 1.0f;
    float x = rect.x;
    if (safe_align == kTextAlignRightMiddle) {
        x = rect.x + rect.w;
    } else if (safe_align == kTextAlignCenterMiddle) {
        x = static_cast<float>(static_cast<double>(rect.x) + static_cast<double>(rect.w) * 0.5);
    }
    const float y = static_cast<float>(static_cast<double>(rect.y) + static_cast<double>(rect.h) * 0.5);
    return nvgText(ctx, x, y, start, end);
}

// Matches sub_180032280
void draw_text_caret(
    const GuiRenderer* renderer,
    const math::Vec4f& rect,
    const NVGcolor& color,
    float blink_speed,
    float stroke_width) noexcept
{
    NVGcontext* ctx = renderer->vg;
    const float phase = static_cast<float>(static_cast<double>(blink_speed) * renderer->frame_time);
    const float alpha = 0.5f + 0.5f * std::sin(phase);

    nvgBeginPath(ctx);
    nvgStrokeColor(ctx, nvgTransRGBAf(color, alpha));
    nvgStrokeWidth(ctx, stroke_width);
    nvgMoveTo(ctx, rect.x, rect.y);
    nvgLineTo(ctx, rect.x, rect.y + rect.h);
    nvgStroke(ctx);
}

// Matches sub_180033CF0 (ease-out quint alpha + 75% dim rect + -50px -> 0px slide-in)
void apply_fade_and_dim_slide(
    const GuiRenderer* renderer,
    float progress) noexcept
{
    NVGcontext* ctx = renderer->vg;
    const float eased = 1.0f - std::pow(1.0f - progress, 5.0f);

    nvgGlobalAlpha(ctx, eased);
    nvgFillColor(ctx, nvgTransRGBAf(color_dim_overlay(), 0.75f));
    renderer->fill_rect(math::make_rect(0.0f, 0.0f, renderer->logical_width, renderer->logical_height));
    nvgTranslate(ctx, 0.0f, -50.0f);
    nvgTranslate(ctx, 0.0f, eased * 50.0f);
}

} // namespace detail

// Matches sub_180031310
int GuiRenderer::load_font_memory(
    uint32_t font_slot,
    const char* name,
    const uint8_t* data,
    int32_t size) noexcept
{
    const int handle = nvgCreateFontMem(vg, name, const_cast<unsigned char*>(data), size, 0);
    if (handle == -1) {
        return 6002;
    }
    if (font_slot < 2u) {
        font_handles[font_slot] = handle;
    }
    return 0;
}

// Matches sub_180031360
int GuiRenderer::initialize() noexcept {
    if (!gladLoadGL()) {
        return 6000;
    }

    vg = nvgCreateGL3(NVG_ANTIALIAS | NVG_STENCIL_STROKES);
    if (vg == nullptr) {
        return 6001;
    }

    int err = load_font_memory(
        kFontRegular,
        "regular",
        assets::kFontInterMedium,
        static_cast<int32_t>(assets::kFontInterMedium_size));
    if (err != 0) {
        return err;
    }

    err = load_font_memory(
        kFontBold,
        "bold",
        assets::kFontInterBold,
        static_cast<int32_t>(assets::kFontInterBold_size));
    if (err != 0) {
        return err;
    }

    logo_image = nvgCreateImageMem(
        vg,
        0,
        const_cast<unsigned char*>(assets::kImageLogoPng),
        static_cast<int>(assets::kImageLogoPng_size));
    if (logo_image <= 0) {
        return 6003;
    }

    int img_w = 0;
    int img_h = 0;
    nvgImageSize(vg, logo_image, &img_w, &img_h);
    logo_width = static_cast<float>(img_w);
    logo_height = static_cast<float>(img_h);
    scale = 1.0f;
    return 0;
}

// Matches sub_180031470
void GuiRenderer::shutdown() noexcept {
    if (offscreen_fbo != nullptr) {
        nvgluDeleteFramebuffer(offscreen_fbo);
        offscreen_fbo = nullptr;
    }
    if (vg != nullptr) {
        if (logo_image > 0) {
            nvgDeleteImage(vg, logo_image);
            logo_image = 0;
        }
        nvgDeleteGL3(vg);
        vg = nullptr;
    }
    font_handles[0] = -1;
    font_handles[1] = -1;
}

// Matches sub_1800314D0
void GuiRenderer::set_scale(float ui_scale) noexcept {
    scale = ui_scale;
    logical_width = static_cast<float>(fb_width) / ui_scale;
    logical_height = static_cast<float>(fb_height) / ui_scale;
}

// Matches sub_1800314F0
bool GuiRenderer::update_framebuffer_size(uint32_t width, uint32_t height) noexcept {
    bool recreated = false;
    if (width != static_cast<uint32_t>(fb_width) || height != static_cast<uint32_t>(fb_height)) {
        if (offscreen_fbo != nullptr) {
            nvgluDeleteFramebuffer(offscreen_fbo);
        }
        offscreen_fbo = nvgluCreateFramebuffer(
            vg,
            static_cast<int>(width),
            static_cast<int>(height),
            0);
        recreated = true;
    }
    fb_width = static_cast<int32_t>(width);
    fb_height = static_cast<int32_t>(height);
    return recreated;
}

// Matches sub_180031550
void GuiRenderer::begin_frame(double current_time) noexcept {
    nvgBeginFrame(vg, static_cast<float>(fb_width), static_cast<float>(fb_height), 1.0f);
    nvgScale(vg, scale, scale);
    frame_time = current_time;
}

// Matches sub_1800315B0
void GuiRenderer::begin_offscreen_frame(double current_time) noexcept {
    nvgluBindFramebuffer(offscreen_fbo);
    glGetFloatv(GL_COLOR_CLEAR_VALUE, saved_clear_color);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    nvgBeginFrame(vg, static_cast<float>(fb_width), static_cast<float>(fb_height), 1.0f);
    nvgScale(vg, scale, scale);
    frame_time = current_time;
}

// Matches sub_180031650
void GuiRenderer::composite_offscreen(float alpha) noexcept {
    if (offscreen_fbo == nullptr) {
        return;
    }
    const float effective_alpha = (alpha > 0.0f) ? alpha : 1.0f;
    const NVGpaint paint = nvgImagePattern(
        vg,
        0.0f,
        0.0f,
        logical_width,
        logical_height,
        0.0f,
        offscreen_fbo->image,
        effective_alpha);
    nvgBeginPath(vg);
    nvgRect(vg, 0.0f, 0.0f, logical_width, logical_height);
    nvgFillPaint(vg, paint);
    nvgFill(vg);
}

// Matches sub_180031720
void GuiRenderer::end_frame() noexcept {
    nvgEndFrame(vg);
}

// Matches sub_180031730
void GuiRenderer::end_offscreen_frame() noexcept {
    nvgEndFrame(vg);
    nvgluBindFramebuffer(nullptr);
    glClearColor(
        saved_clear_color[0],
        saved_clear_color[1],
        saved_clear_color[2],
        saved_clear_color[3]);
}

// Matches sub_180031780
void GuiRenderer::select_font(uint32_t font_slot) const noexcept {
    if (font_slot < 2u) {
        nvgFontFaceId(vg, font_handles[font_slot]);
    }
}

// Matches sub_180031E20
void GuiRenderer::fill_rect(const math::Vec4f& rect) const noexcept {
    nvgBeginPath(vg);
    nvgRect(vg, rect.x, rect.y, rect.w, rect.h);
    nvgFill(vg);
}

// Matches sub_180031E70
void GuiRenderer::fill_rounded_rect(const math::Vec4f& rect, float radius) const noexcept {
    nvgBeginPath(vg);
    nvgRoundedRect(vg, rect.x, rect.y, rect.w, rect.h, radius);
    nvgFill(vg);
}

// Matches sub_180031F10
void GuiRenderer::stroke_rounded_rect(const math::Vec4f& rect, float radius) const noexcept {
    nvgBeginPath(vg);
    nvgRoundedRect(vg, rect.x, rect.y, rect.w, rect.h, radius);
    nvgStroke(vg);
}

void GuiRenderer::draw_logo_icon(const math::Vec4f& rect, float alpha) const noexcept {
    detail::draw_atlas_icon(this, rect, 0u, alpha);
}

// Matches sub_1800320C0
void GuiRenderer::draw_text(
    const math::Vec4f& rect,
    int32_t align_mode,
    uint32_t font_slot,
    const char* text) const noexcept
{
    detail::draw_text_adv(this, rect, align_mode, font_slot, text);
}

// Matches sub_1800321B0
void GuiRenderer::draw_text_range(
    math::Vec4f rect,
    int32_t align_mode,
    uint32_t font_slot,
    const char* start,
    const char* end) const noexcept
{
    detail::draw_text_range_adv(this, rect, align_mode, font_slot, start, end);
}

void GuiRenderer::draw_checkmark(
    const math::Vec2f& center,
    float radius,
    const NVGcolor& color) const noexcept
{
    nvgBeginPath(vg);
    nvgStrokeColor(vg, color);
    nvgStrokeWidth(vg, 2.0f);
    nvgLineCap(vg, NVG_ROUND);
    nvgLineJoin(vg, NVG_ROUND);
    nvgMoveTo(vg, center.x - radius * 0.55f, center.y + radius * 0.05f);
    nvgLineTo(vg, center.x - radius * 0.15f, center.y + radius * 0.45f);
    nvgLineTo(vg, center.x + radius * 0.60f, center.y - radius * 0.40f);
    nvgStroke(vg);
}

void GuiRenderer::draw_fullscreen_dim(float opacity) const noexcept {
    nvgFillColor(vg, nvgTransRGBAf(detail::color_dim_overlay(), opacity));
    fill_rect(math::make_rect(0.0f, 0.0f, logical_width, logical_height));
}

} // namespace slinky::gui
