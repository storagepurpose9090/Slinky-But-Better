#pragma once

#include <cstddef>
#include <cstdint>
#include "nanovg.h"
#include "config/settings.hpp"
#include "events/module_manager.hpp"
#include "gui/gui_renderer.hpp"
#include "util/input_win32.hpp"
#include "util/math.hpp"

namespace slinky::config {
struct ProfileRegistry;
}

namespace slinky::events {
struct JniContext;
}

namespace slinky::gui {

// 120-byte layout stack frame - matches sub_1800317C0 .. sub_180031C80
struct LayoutFrame {
    math::Vec4f bounds{};           // +0x00 (0..15)
    float gap = 0.0f;               // +0x10 (16)
    float outer_margin = 0.0f;      // +0x14 (20)
    bool has_scissor = false;       // +0x18 (24)
    uint8_t pad0[3]{};              // +0x19
    math::Vec2f cursor_origin{};    // +0x1C (28..35)
    uint32_t pad1 = 0;              // +0x24 (36)
    size_t columns = 1;             // +0x28 (40)
    math::Vec4f prev_cells[4]{};    // +0x30..+0x6F (48..111)
    size_t item_index = 0;          // +0x70 (112)
};
static_assert(sizeof(LayoutFrame) == 120, "LayoutFrame must be 120 bytes");

struct GuiTab {
    uint32_t tab_id = 0;            // +0x00 (0..4=categories, 5=Profiles, 6=Unload)
    uint32_t pad = 0;
    const char* label = nullptr;    // +0x08
};
static_assert(sizeof(GuiTab) == 16, "GuiTab must be 16 bytes");

enum class GuiFocusMode : int32_t {
    None            = 0,
    KeybindCapture  = 1,
    InlineTextInput = 2,
    ModalTextInput  = 3,
};

enum class GuiModalKind : int32_t {
    None         = 0,
    TextPrompt   = 1,
    ColorPicker  = 2,
    Confirm      = 3,
    ItemPicker   = 4,
};

struct PickerSearchMatch {
    size_t option_index = 0;
    const char* label = nullptr;
};
static_assert(sizeof(PickerSearchMatch) == 16, "PickerSearchMatch must be 16 bytes");

struct PickerSearchList {
    size_t count = 0;
    size_t capacity = 0;
    PickerSearchMatch* data = nullptr;
};
static_assert(sizeof(PickerSearchList) == 24, "PickerSearchList must be 24 bytes");

using SetKeybindTargetFn = void (*)(void* target, uint32_t key_code);
using TextPromptSubmitFn = void (*)(void* ctx, const char* text);
using ColorPickerApplyFn = void (*)(void* ctx, const math::Color3f* rgb);
using ConfirmModalFn     = void (*)(void* ctx);
using ItemPickerSelectFn = void (*)(void* ctx, size_t index, const char* label);

// Full ClickGUI state (1232 bytes = 0x4D0 @ 0x180314440) - matches sub_1800317A0 .. sub_180039EA0
struct ClickGui {
    LayoutFrame layout_stack[5]{};          // +0x000..+0x257 (0..599)
    size_t layout_depth = 0;                // +0x258 (600)
    int32_t max_content_bottom = 0;         // +0x260 (608)
    uint32_t pad_content = 0;               // +0x264 (612)

    bool tooltip_active = false;            // +0x268 (616)
    uint8_t tooltip_pad = 0;                // +0x269 (617)
    char tooltip_text[102]{};               // +0x26A..+0x2CF (618..719)
    math::Vec2f tooltip_pos{};              // +0x2D0 (720..727)
    uint64_t pad_tooltip = 0;               // +0x2D8 (728)

    GuiTab tabs[7]{};                       // +0x2E0..+0x34F (736..847)
    size_t selected_tab_index = 0;          // +0x350 (848)
    float tab_indicator_anim = 0.0f;        // +0x358 (856)
    uint32_t pad_tab = 0;                   // +0x35C (860)
    NVGcolor accent_color{};                // +0x360..+0x36F (864..879)

    GuiFocusMode focus_mode = GuiFocusMode::None; // +0x370 (880)
    uint32_t pad_focus = 0;                 // +0x374 (884)
    union {
        struct {
            SetKeybindTargetFn callback;    // +0x378 (888)
            void* target;                   // +0x380 (896)
        } keybind;
        struct {
            char buffer[104];               // +0x378..+0x3DF (888..991)
            config::ListInputValidatorFn validator; // +0x3E0 (992)
            void* owner;                    // +0x3E8 (1000)
            bool submitted;                 // +0x3F0 (1008)
            uint8_t pad[7];
        } text_input;
    };

    GuiModalKind modal_kind = GuiModalKind::None; // +0x3F8 (1016)
    bool modal_closing = false;             // +0x3FC (1020)
    uint8_t pad_modal0[3]{};                // +0x3FD
    void* modal_ctx = nullptr;              // +0x400 (1024)
    const char* modal_title = nullptr;      // +0x408 (1032)
    double modal_anim_time = 0.0;           // +0x410 (1040)
    float modal_anim_progress = 0.0f;       // +0x418 (1048)
    float modal_height = 0.0f;              // +0x41C (1052)
    bool modal_rendering = false;           // +0x420 (1056)
    uint8_t pad_modal1[7]{};                // +0x421
    void* modal_primary_cb = nullptr;       // +0x428 (1064)
    union {
        struct {
            char hex_owner_tag[8];          // +0x430 (1072) - v3 + 266 -> 1064? wait: kept as 96-byte modal payload
            math::Color3f rgb;              // +0x438
            float hsl[3];                   // +0x444
            bool dragging_channel[3];       // +0x450
            uint8_t pad[61];
        } color_picker;
        struct {
            const char* message;            // +0x430 (1072 = v3 + 268 floats)
            uint8_t pad[88];
        } confirm;
        struct {
            ItemPickerSelectFn select_cb;   // +0x430 (1072 = v3[134] qword)
            const char* const* items;       // +0x438 (1080 = v3[135] qword)
            size_t item_count;              // +0x440 (1088 = v3[136] qword)
            double last_filter_time;        // +0x448 (1096 = v3[137] double)
            PickerSearchList* filtered;     // +0x450 (1104 = v3[138] qword)
            size_t overflow_count;          // +0x458 (1112 = v3[139] qword)
            uint8_t pad[48];
        } item_picker;
        uint8_t modal_payload[104]{};       // +0x430..+0x497 (1072..1175)
    };

    double open_time = 0.0;                 // +0x498 (1176)
    double last_profile_refresh_time = -1.0;// +0x4A0 (1184)
    math::Vec2f cursor_logical_scrolled{};  // +0x4A8 (1192..1199)
    math::Vec2f cursor_fb{};                // +0x4B0 (1200..1207)
    float open_fade_anim = 0.0f;            // +0x4B8 (1208)
    float scroll_y = 0.0f;                  // +0x4BC (1212)
    float max_scroll_y = 0.0f;              // +0x4C0 (1216)
    uint32_t pad_scroll = 0;                // +0x4C4 (1220)
    bool mouse_left_down = false;           // +0x4C8 (1224)
    bool mouse_click_consumed = false;      // +0x4C9 (1225)
    bool ctrl_held = false;                 // +0x4CA (1226)
    bool unload_requested = false;          // +0x4CB (1227)
    bool hide_descriptions = false;         // +0x4CC (1228)
    bool wide_layout = false;               // +0x4CD (1229)
    uint8_t pad_end[2]{};                   // +0x4CE..+0x4CF (1230..1231)

    // Initializes tabs, default accent color, and modal state - matches sub_180033DE0
    void initialize() noexcept;

    // Resets open fade animation on GUI open - matches sub_180032C10
    void on_open(double current_time) noexcept;

    // Resets focus/mouse capture on GUI close - matches sub_180033EC0
    void reset_interaction() noexcept;

    // Dismisses any active modal dialog - matches sub_180033EB0 / sub_180037A30
    void close_modal(double current_time) noexcept;

    // Returns true if user confirmed "Unload Slinky?" - matches sub_180033EE0
    bool is_unload_requested() const noexcept { return unload_requested; }

    // Processes mouse/keyboard/scroll events when GUI is open - matches sub_180032340
    bool handle_input(const input::InputEvent* ev, double current_time) noexcept;

    // Renders the complete ClickGUI (tabs, module cards, profiles, modals, tooltips) - matches sub_180032C30
    void render(
        GuiRenderer* renderer,
        events::ModuleManager* mgr,
        config::ProfileRegistry* profiles,
        events::JniContext* jni = nullptr) noexcept;

    // Layout engine methods - matches sub_1800317A0 .. sub_180031C80
    void reset_layout() noexcept;
    void push_layout(math::Vec4f& inout_bounds, float gap, float margin) noexcept;
    void push_Clipped_card(GuiRenderer* renderer, math::Vec4f& inout_bounds, float gap, float margin) noexcept;
    float pop_layout(GuiRenderer* renderer) noexcept;
    size_t set_columns(int32_t cols) noexcept;
    size_t set_auto_columns(float min_col_width) noexcept;
    math::Vec4f next_cell(float height) noexcept;

    // Hit-testing & focus helpers - matches sub_180032590 .. sub_1800327E0
    bool is_hovered(const math::Vec4f& rect) const noexcept;
    bool consume_click_in(const math::Vec4f& rect) noexcept;
    void consume_any_click() noexcept;
    void begin_keybind_capture(SetKeybindTargetFn cb, void* target) noexcept;
    bool is_capturing_keybind(const void* target) const noexcept;
    void begin_text_input(config::ListInputValidatorFn validator, void* owner) noexcept;
    bool is_editing_text(const void* owner) const noexcept;
    void promote_text_input_to_modal() noexcept;
    const char* consume_submitted_text() noexcept;
    const char* current_text_buffer() const noexcept;
    void show_tooltip(const char* text, float x, float y) noexcept;

    // Modal openers - matches sub_180037A40 .. sub_1800380B0
    bool has_active_modal() const noexcept;
    bool is_rendering_modal() const noexcept { return modal_rendering; }
    void open_color_picker_modal(
        GuiRenderer* renderer,
        const char* title,
        void* ctx,
        const math::Color3f& initial_rgb,
        ColorPickerApplyFn apply_cb) noexcept;
    void open_text_prompt_modal(
        GuiRenderer* renderer,
        const char* title,
        config::ListInputValidatorFn validator,
        void* ctx,
        TextPromptSubmitFn submit_cb) noexcept;
    void open_confirm_modal(
        GuiRenderer* renderer,
        void* ctx,
        ConfirmModalFn confirm_cb,
        const char* message) noexcept;
    void open_item_picker_modal(
        GuiRenderer* renderer,
        const char* title,
        const char* const* items,
        size_t item_count,
        void* ctx,
        ItemPickerSelectFn select_cb) noexcept;
    void render_modal_overlay(GuiRenderer* renderer) noexcept;
};
static_assert(sizeof(ClickGui) == 0x4D0, "ClickGui must match exact 0x4D0-byte layout");

} // namespace slinky::gui
