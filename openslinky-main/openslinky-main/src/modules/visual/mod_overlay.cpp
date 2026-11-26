#include "modules/visual/mod_overlay.hpp"

#include <bit>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "config/settings.hpp"
#include "events/module_manager.hpp"
#include "gui/gui_renderer.hpp"
#include "gui/widgets.hpp"
#include "modules/module_interop.hpp"
#include "nanovg.h"
#include "util/math.hpp"

namespace slinky::modules {

namespace {

constexpr size_t kMaxOverlayEntries = 67;

// 80-byte active module HUD entry (@ 0x180318200)
struct OverlayEntry {
    events::Module* mod = nullptr; // +0x00
    char display_name[20]{};       // +0x08..+0x1B
    char details[32]{};            // +0x1C..+0x3B
    float total_width = 0.0f;      // +0x3C
    float text_height = 0.0f;      // +0x40
    float anim = 0.0f;             // +0x44
    bool active = false;           // +0x48
    uint8_t pad[7]{};              // +0x49
};
static_assert(sizeof(OverlayEntry) == 0x50, "OverlayEntry must be 80 bytes");

config::BoolSetting s_show_combat{{}, "Combat", true, false};
config::BoolSetting s_show_move{{}, "Move", true, false};
config::BoolSetting s_show_block{{}, "Block", true, false};
config::BoolSetting s_show_visual{{}, "Visual", true, false};
config::BoolSetting s_show_utility{{}, "Utility", true, false};
config::FloatSetting s_text_size{
    {}, "Text size", "%.0f px", 1.0f, 0.0f, 10.0f, 24.0f, 16.0f, 0.0f
};
config::FloatSetting s_padding_x{
    {}, "Padding X", "%.0f px", 1.0f, 0.0f, 0.0f, 5.0f, 3.0f, 0.0f
};
config::FloatSetting s_padding_y{
    {}, "Padding Y", "%.0f px", 1.0f, 0.0f, 0.0f, 5.0f, 1.0f, 0.0f
};
config::FloatSetting s_bg_rounding{
    {}, "Rounding", "%.0f px", 1.0f, 0.0f, 0.0f, 16.0f, 6.0f, 0.0f
};
config::ColorSetting s_name_color{
    "Text color",
    {
        std::bit_cast<float>(0x3F73F7CFu),
        std::bit_cast<float>(0x3EBAE148u),
        std::bit_cast<float>(0x3D916873u),
    },
    {0.0f, 0.0f, 0.0f}
};
config::ColorSetting s_details_color{
    "Details color",
    {0.6f, 0.6f, 0.6f},
    {0.0f, 0.0f, 0.0f}
};
config::BoolSetting s_lowercase{{}, "Lowercase", true, false};
config::BoolSetting s_show_details{{}, "Show details", true, false};
config::BoolSetting s_require_bound{{}, "Only bound modules", false, false};

uint32_t s_last_filter_mask = 0; // dword_1803181F0
bool s_force_resync = false;     // byte_1803181F4
bool s_layout_dirty = false;     // byte_1803181F5
size_t s_entry_count = 0;        // qword_1803181F8
OverlayEntry s_entries[kMaxOverlayEntries]{};
float s_redraw_timer = 0.0f;     // dword_1803191A0

bool is_category_visible(events::ModuleCategory cat) noexcept {
    switch (cat) {
        case events::ModuleCategory::Combat:  return s_show_combat.value;
        case events::ModuleCategory::Move:    return s_show_move.value;
        case events::ModuleCategory::Visual:  return s_show_visual.value;
        case events::ModuleCategory::Block:   return s_show_block.value;
        case events::ModuleCategory::Utility: return s_show_utility.value;
        default:                              return true;
    }
}

bool passes_module_filter(const events::Module* mod) noexcept {
    if ((mod->flags & events::kModuleFlagHiddenFromHud) != 0) {
        return false;
    }
    if (s_require_bound.value && mod->keybind == 0) {
        return false;
    }
    return is_category_visible(mod->category);
}

void format_entry_strings(OverlayEntry& entry) noexcept {
    const events::Module* mod = entry.mod;
    if (s_lowercase.value) {
        const size_t len = std::strlen(mod->name);
        for (size_t i = 0; i < len; ++i) {
            entry.display_name[i] = static_cast<char>(
                std::tolower(static_cast<unsigned char>(mod->name[i])));
        }
        entry.display_name[len] = '\0';
    } else {
        std::snprintf(entry.display_name, sizeof(entry.display_name), "%s", mod->name);
    }

    if (mod->format_hud_tag != nullptr && s_show_details.value) {
        mod->format_hud_tag(entry.details);
        if (s_lowercase.value && entry.details[0] != '\0') {
            for (size_t i = 0; i < std::strlen(entry.details); ++i) {
                entry.details[i] = static_cast<char>(
                    std::tolower(static_cast<unsigned char>(entry.details[i])));
            }
        }
    } else {
        entry.details[0] = '\0';
    }
}

// Matches sub_180056600
void append_entry(events::Module* mod) noexcept {
    if (s_entry_count >= kMaxOverlayEntries) {
        return;
    }
    OverlayEntry& entry = s_entries[s_entry_count];
    entry.mod = mod;
    entry.anim = 0.0f;
    entry.active = true;
    format_entry_strings(entry);
    ++s_entry_count;
}

// Matches sub_180056450
void resync_entries(events::ModuleManager* mgr) noexcept {
    for (size_t i = 0; i < s_entry_count; ++i) {
        if (!passes_module_filter(s_entries[i].mod)) {
            s_entries[i].active = false;
        }
    }

    if (mgr == nullptr) {
        return;
    }

    for (size_t i = 0; i < mgr->module_count; ++i) {
        events::Module* mod = mgr->all_modules[i];
        if (!mod->enabled || !passes_module_filter(mod)) {
            continue;
        }

        bool found = false;
        for (size_t j = 0; j < s_entry_count; ++j) {
            if (s_entries[j].mod == mod) {
                s_entries[j].active = true;
                found = true;
                break;
            }
        }
        if (!found) {
            append_entry(mod);
        }
    }
}

// Matches sub_180056720
int compare_overlay_entries(const void* a, const void* b) noexcept {
    const auto* ea = static_cast<const OverlayEntry*>(a);
    const auto* eb = static_cast<const OverlayEntry*>(b);
    if (eb->total_width > ea->total_width) {
        return 1;
    }
    if (ea->total_width > eb->total_width) {
        return -1;
    }
    return std::strcmp(ea->display_name, eb->display_name);
}

// Matches sub_1800557E0
void on_game_tick(void* raw_event) noexcept {
    events::GameContext* ctx = static_cast<events::ContextEvent*>(raw_event)->ctx;

    const uint32_t filter_mask =
        (s_show_combat.value ? 1u : 0u) |
        (s_show_move.value ? 2u : 0u) |
        (s_show_block.value ? 4u : 0u) |
        (s_show_visual.value ? 8u : 0u) |
        (s_show_utility.value ? 16u : 0u) |
        (s_require_bound.value ? 32u : 0u);

    const bool force = s_force_resync;
    if (force || s_last_filter_mask != filter_mask) {
        resync_entries(ctx->module_manager);
        s_last_filter_mask = filter_mask;
        s_layout_dirty = true;
        s_force_resync = false;
    }

    if (force || !ctx->in_game_focus) {
        for (size_t i = 0; i < s_entry_count; ++i) {
            format_entry_strings(s_entries[i]);
        }
        s_layout_dirty = true;
    }
}

// Matches sub_1800559F0
void on_module_state_changed(void* raw_event) noexcept {
    const auto* ev = static_cast<const events::ModuleStateChangedEvent*>(raw_event);
    events::Module* mod = ev->module;
    s_redraw_timer = 0.2f;

    if (mod == get_mod_overlay_module()) {
        s_entry_count = 0;
        if (ev->enabled && ev->ctx != nullptr && ev->ctx->module_manager != nullptr) {
            events::ModuleManager* mgr = ev->ctx->module_manager;
            for (size_t i = 0; i < mgr->module_count; ++i) {
                events::Module* candidate = mgr->all_modules[i];
                if (candidate->enabled && passes_module_filter(candidate)) {
                    append_entry(candidate);
                }
            }
            s_layout_dirty = true;
        }
        return;
    }

    for (size_t i = 0; i < s_entry_count; ++i) {
        if (s_entries[i].mod == mod) {
            if (ev->enabled) {
                if (passes_module_filter(mod)) {
                    s_entries[i].active = true;
                }
            } else {
                s_entries[i].active = false;
            }
            return;
        }
    }

    if (ev->enabled && passes_module_filter(mod)) {
        append_entry(mod);
        s_layout_dirty = true;
    }
}

// Matches sub_180055C30
void on_gui_frame_poll(void* raw_event) noexcept {
    auto* ev = static_cast<events::GuiFramePollEvent*>(raw_event);
    ev->render_flags |= 1u;
    if (s_layout_dirty || s_redraw_timer > 0.0f) {
        ev->render_flags |= 3u;
    }
}

// Matches sub_180055C70
void on_gui_render_2d(void* raw_event) noexcept {
    const auto* ev = static_cast<const events::GuiRender2DEvent*>(raw_event);
    gui::GuiRenderer* renderer = ev->renderer;
    NVGcontext* vg = renderer->vg;

    nvgFontSize(vg, s_text_size.value);
    renderer->select_font(gui::kFontRegular);

    if (s_redraw_timer > 0.0f) {
        s_redraw_timer -= ev->delta_time;
    }

    if (s_entry_count > 0) {
        const double step = static_cast<double>(ev->delta_time) * 5.0;
        size_t i = 0;
        while (i < s_entry_count) {
            OverlayEntry& entry = s_entries[i];
            if (entry.active) {
                if (entry.anim != 1.0f) {
                    const double next = static_cast<double>(entry.anim) + step;
                    entry.anim = (next > 1.0000000596046448) ? 1.0f : static_cast<float>(next);
                }
                ++i;
            } else {
                const double next = static_cast<double>(entry.anim) - step;
                entry.anim = static_cast<float>(next);
                if (next > 7.006492321624085e-46) {
                    ++i;
                } else {
                    --s_entry_count;
                    std::memmove(
                        &s_entries[i],
                        &s_entries[i + 1],
                        (s_entry_count - i) * sizeof(OverlayEntry));
                }
            }
        }
    }

    if (s_layout_dirty) {
        nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
        for (size_t i = 0; i < s_entry_count; ++i) {
            OverlayEntry& entry = s_entries[i];
            float bounds[4]{};
            nvgTextBounds(vg, 0.0f, 0.0f, entry.display_name, nullptr, bounds);
            entry.total_width = bounds[2];
            entry.text_height = bounds[3];
            if (entry.details[0] != '\0') {
                nvgTextBounds(vg, 0.0f, 0.0f, entry.details, nullptr, bounds);
                entry.total_width += bounds[2] + 4.0f;
            }
        }
        std::qsort(s_entries, s_entry_count, sizeof(OverlayEntry), &compare_overlay_entries);
        s_layout_dirty = false;
    }

    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
    const NVGcolor bg_color = nvgRGBAf(0.0f, 0.0f, 0.0f, 0.7f);
    const NVGcolor name_color = nvgRGBf(
        s_name_color.value.r,
        s_name_color.value.g,
        s_name_color.value.b);
    const NVGcolor details_color = nvgRGBf(
        s_details_color.value.r,
        s_details_color.value.g,
        s_details_color.value.b);

    float cur_y = 0.0f;
    for (size_t i = 0; i < s_entry_count; ++i) {
        const OverlayEntry& entry = s_entries[i];
        const float eased = (entry.anim == 1.0f) ? 1.0f : math::smoothstep01(entry.anim);
        nvgGlobalAlpha(vg, eased);

        float radius_bl = s_bg_rounding.value;
        if (i + 1 < s_entry_count) {
            radius_bl = std::fmin(entry.total_width - s_entries[i + 1].total_width, radius_bl);
        }

        const float pad_x = s_padding_x.value;
        const float pad_y = s_padding_y.value;
        const float rect_w = pad_x * 2.0f + entry.total_width;
        const float rect_x = renderer->logical_width - pad_x * 2.0f - entry.total_width * eased;
        const float rect_h = pad_y * 2.0f + entry.text_height;

        nvgFillColor(vg, bg_color);
        nvgBeginPath(vg);
        nvgRoundedRectVarying(vg, rect_x, cur_y, rect_w, rect_h, 0.0f, 0.0f, 0.0f, radius_bl);
        nvgFill(vg);

        nvgFillColor(vg, name_color);
        const float text_x = renderer->logical_width - pad_x - entry.total_width * eased;
        const float text_y = cur_y + pad_y;
        const float adv_x = nvgText(vg, text_x, text_y, entry.display_name, nullptr);

        if (entry.details[0] != '\0') {
            nvgFillColor(vg, details_color);
            nvgText(vg, adv_x + 4.0f, text_y, entry.details, nullptr);
        }

        cur_y += eased * (pad_y * 2.0f + entry.text_height);
    }

    nvgGlobalAlpha(vg, 1.0f);
}

// Matches sub_180056160
void on_profile_action(void* raw_event) noexcept {
    const auto* ev = static_cast<const events::ProfileActionEvent*>(raw_event);
    if (ev->action == 0) {
        s_force_resync = true;
    }
}

// Matches sub_180056180
void on_module_keybind_changed(void* raw_event) noexcept {
    const auto* ev = static_cast<const events::ModuleKeybindChangedEvent*>(raw_event);
    if (s_require_bound.value) {
        resync_entries(ev->manager);
        s_layout_dirty = true;
    }
}

void render_settings(gui::ClickGui* gui, gui::GuiRenderer* renderer) noexcept {
    gui::widgets::render_section_header(gui, renderer, "Visible categories");
    s_show_combat.render(gui, renderer);
    s_show_move.render(gui, renderer);
    s_show_block.render(gui, renderer);
    s_show_visual.render(gui, renderer);
    s_show_utility.render(gui, renderer);
    gui::widgets::render_section_header(gui, renderer, "Customization");
    s_text_size.render(gui, renderer);
    s_padding_x.render(gui, renderer);
    s_padding_y.render(gui, renderer);
    s_bg_rounding.render(gui, renderer);
    s_name_color.render(gui, renderer);
    s_details_color.render(gui, renderer);
    s_lowercase.render(gui, renderer);
    s_show_details.render(gui, renderer);
    gui::widgets::render_section_header(gui, renderer, "Conditions");
    s_require_bound.render(gui, renderer);
}

void visit_config(config::ConfigArchive* ar) noexcept {
    s_show_combat.visit(ar, "show_combat");
    s_show_move.visit(ar, "show_move");
    s_show_block.visit(ar, "show_block");
    s_show_visual.visit(ar, "show_visual");
    s_show_utility.visit(ar, "show_utility");
    s_text_size.visit(ar, "text_size");
    s_padding_x.visit(ar, "padding_x");
    s_padding_y.visit(ar, "padding_y");
    s_bg_rounding.visit(ar, "bg_rounding");
    s_name_color.visit(ar, "name_color");
    s_details_color.visit(ar, "details_color");
    s_lowercase.visit(ar, "lowercase");
    s_show_details.visit(ar, "show_details");
    s_require_bound.visit(ar, "require_bound");
}

events::Module create_module() noexcept {
    events::Module mod{};
    mod.name = "Mod Overlay";
    mod.description = "Displays a list of all active mods.";
    mod.category = events::ModuleCategory::Visual;
    mod.flags = events::kModuleFlagHiddenFromHud;
    mod.events[events::kEventGameTick] = {&on_game_tick, 0};
    mod.events[events::kEventModuleStateChanged] = {&on_module_state_changed, 0};
    mod.events[events::kEventGuiFramePoll] = {&on_gui_frame_poll, 0};
    mod.events[events::kEventGuiRender2D] = {&on_gui_render_2d, 100};
    mod.events[events::kEventProfileAction] = {&on_profile_action, 0};
    mod.events[events::kEventModuleKeybindChanged] = {&on_module_keybind_changed, 0};
    mod.render_settings = &render_settings;
    mod.visit_config = &visit_config;
    return mod;
}

} // namespace

events::Module* get_mod_overlay_module() noexcept {
    static events::Module s_module = create_module();
    return &s_module;
}

} // namespace slinky::modules
