#pragma once

#include <cstddef>
#include <cstdint>
#include "nanovg.h"
#include "util/math.hpp"

struct NVGLUframebuffer;

namespace slinky::gui {

enum FontId : uint32_t {
    kFontRegular = 0, // Inter-Medium.ttf
    kFontBold    = 1, // Inter-Bold.ttf
};

enum TextAlignMode : int32_t {
    kTextAlignLeftMiddle   = 0, // NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE
    kTextAlignCenterMiddle = 1, // NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE
    kTextAlignRightMiddle  = 2, // NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE
};

// NanoVG + FBO wrapper (88 bytes = 0x58) - matches sub_180031310 .. sub_180032280
struct GuiRenderer {
    NVGcontext* vg = nullptr;                 // +0x00 (0)
    double frame_time = 0.0;                  // +0x08 (8)
    int32_t fb_width = 0;                     // +0x10 (16)
    int32_t fb_height = 0;                    // +0x14 (20)
    float logical_width = 0.0f;               // +0x18 (24)
    float logical_height = 0.0f;              // +0x1C (28)
    float scale = 1.0f;                       // +0x20 (32)
    uint32_t pad0 = 0;                        // +0x24 (36)
    NVGLUframebuffer* offscreen_fbo = nullptr;// +0x28 (40)
    float saved_clear_color[4]{};             // +0x30..+0x3F (48..63)
    int32_t font_handles[2]{-1, -1};          // +0x40..+0x47 (64..71)
    int32_t logo_image = 0;                   // +0x48 (72)
    float logo_width = 0.0f;                  // +0x4C (76)
    float logo_height = 0.0f;                 // +0x50 (80)
    uint32_t pad1 = 0;                        // +0x54 (84)

    // Initializes GLAD + NanoVG GL2 context + embedded Inter fonts + logo PNG - matches sub_180031360
    int initialize() noexcept;

    // Releases FBO, logo image, and NanoVG context - matches sub_180031470
    void shutdown() noexcept;

    // Loads an in-memory TTF font into slot `font_slot` - matches sub_180031310
    int load_font_memory(uint32_t font_slot, const char* name, const uint8_t* data, int32_t size) noexcept;

    // Updates UI scale and recomputes logical_width / logical_height - matches sub_1800314D0
    void set_scale(float ui_scale) noexcept;

    // Ensures offscreen FBO matches framebuffer dimensions (returns true if recreated) - matches sub_1800314F0
    bool update_framebuffer_size(uint32_t width, uint32_t height) noexcept;

    // Begins direct NanoVG frame on current GL framebuffer - matches sub_180031550
    void begin_frame(double current_time) noexcept;

    // Binds offscreen FBO, clears it, and begins NanoVG frame - matches sub_1800315B0
    void begin_offscreen_frame(double current_time) noexcept;

    // Composites offscreen FBO texture onto current NanoVG frame with alpha - matches sub_180031650
    void composite_offscreen(float alpha) noexcept;

    // Ends NanoVG frame - matches sub_180031720
    void end_frame() noexcept;

    // Ends offscreen NanoVG frame, unbinds FBO, and restores saved GL clear color - matches sub_180031730
    void end_offscreen_frame() noexcept;

    // Selects font face by FontId - matches sub_180031780
    void select_font(uint32_t font_slot) const noexcept;

    // Drawing primitives - matches sub_180031E20 .. sub_180032280
    void fill_rect(const math::Vec4f& rect) const noexcept;
    void fill_rounded_rect(const math::Vec4f& rect, float radius) const noexcept;
    void stroke_rounded_rect(const math::Vec4f& rect, float radius) const noexcept;
    void draw_logo_icon(const math::Vec4f& rect, float alpha = 1.0f) const noexcept;
    void draw_text(const math::Vec4f& rect, int32_t align_mode, uint32_t font_slot, const char* text) const noexcept;
    void draw_text_range(math::Vec4f rect, int32_t align_mode, uint32_t font_slot, const char* start, const char* end) const noexcept;
    void draw_checkmark(const math::Vec2f& center, float radius, const NVGcolor& color) const noexcept;
    void draw_fullscreen_dim(float opacity = 0.5f) const noexcept;
};
static_assert(sizeof(GuiRenderer) == 0x58, "GuiRenderer must match 0x58-byte layout");

} // namespace slinky::gui
