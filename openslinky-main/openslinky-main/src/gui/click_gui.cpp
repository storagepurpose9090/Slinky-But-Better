#include "gui/click_gui.hpp"
#include "gui/gui_internal.hpp"
#include "gui/widgets.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "account/alt_manager.hpp"
#include "config/profile_manager.hpp"
#include "events/events.hpp"
#include "util/keycodes.hpp"

namespace slinky::gui {

namespace {

// Profile action icon atlas indices matching unk_18015CEB0[4] = {3, 0, 2, 1}
constexpr uint32_t kProfileBtnIcons[4] = {
    3u, // Load
    0u, // Save
    2u, // Open folder
    1u, // Delete
};

// Profile action button tooltips matching 0x18015CE94
constexpr const char* kProfileBtnTooltips[4] = {
    "Click to load",
    "Click to save",
    "Click to open folder",
    "Click to delete",
};

// Profile confirmation modal format strings matching dword_18015CFF0[4]
constexpr const char* kProfileConfirmFormats[4] = {
    "Load profile '%s'?",
    "Save profile '%s'?",
    "Open folder for profile '%s'?",
    "Delete profile '%s'?",
};

// Single active profile confirmation modal context (matches Block in sub_180034D20 / sub_180034E00)
struct ProfileConfirmContext {
    ClickGui* gui = nullptr;
    events::ModuleManager* mgr = nullptr;
    config::ProfileRegistry* profiles = nullptr;
    char profile_name_buf[64]{};
    bool has_profile_name = false;
    int32_t action = 0;
};

ProfileConfirmContext s_profile_confirm_ctx{};

// Distinct owner tag so the alt-manager input does not collide with the
// "Create new profile" input (which uses nullptr).
int s_alt_input_owner = 0;

constexpr float kAltCardHeight = 88.0f;

inline float get_tooltip_anim(const ClickGui* gui) noexcept {
    float v = 0.0f;
    std::memcpy(&v, &gui->pad_tooltip, sizeof(float));
    return v;
}

inline void set_tooltip_anim(ClickGui* gui, float v) noexcept {
    std::memcpy(&gui->pad_tooltip, &v, sizeof(float));
}

inline float get_profile_card_height(const ClickGui* gui) noexcept {
    float v = 0.0f;
    std::memcpy(&v, &gui->pad_scroll, sizeof(float));
    return v;
}

inline void set_profile_card_height(ClickGui* gui, float v) noexcept {
    std::memcpy(&gui->pad_scroll, &v, sizeof(float));
}

// Flushes the current multi-column row bottom into `frame.cursor_origin.y`
inline void flush_layout_row(LayoutFrame& frame) noexcept {
    if (frame.item_index == 0u || frame.columns == 0u) {
        return;
    }
    const size_t last = frame.item_index - 1u;
    const size_t row_start = last - (last % frame.columns);
    float max_bottom = -1.0f;
    for (size_t i = row_start; i <= last; ++i) {
        const math::Vec4f& cell = frame.prev_cells[i & 3u];
        max_bottom = std::fmax(max_bottom, cell.y + cell.h);
    }
    if (max_bottom != -1.0f) {
        frame.cursor_origin.y = max_bottom + frame.gap;
    }
}

void dispatch_error_event(
    events::ModuleManager* mgr,
    int32_t error_code,
    const char* custom_message = nullptr) noexcept
{
    if (mgr == nullptr) {
        return;
    }
    events::ErrorNotificationEvent ev{};
    ev.id = events::kEventErrorNotification;
    ev.error_code = error_code;
    ev.custom_message = custom_message;
    mgr->dispatch(&ev);
}

void dispatch_profile_action_event(
    events::ModuleManager* mgr,
    const char* profile_name,
    int32_t action) noexcept
{
    if (mgr == nullptr) {
        return;
    }
    events::ProfileActionEvent ev{};
    ev.id = events::kEventProfileAction;
    ev.profile_name = profile_name;
    ev.action = action;
    mgr->dispatch(&ev);
}

const char* default_profile_error_msg(int32_t err) noexcept {
    if (err == 6004) {
        return "Default profile cannot be deleted";
    }
    if (err == 6003) {
        return "Default profile cannot be overwritten";
    }
    return nullptr;
}

// Matches sub_180032C00
void on_confirm_unload(void* ctx) noexcept {
    if (ctx != nullptr) {
        static_cast<ClickGui*>(ctx)->unload_requested = true;
    }
}

// Matches sub_180034E00
void on_confirm_profile_action(void* ctx) noexcept {
    auto* pctx = static_cast<ProfileConfirmContext*>(ctx);
    if (pctx == nullptr) {
        return;
    }

    const char* name_arg = pctx->has_profile_name ? pctx->profile_name_buf : nullptr;
    int err = 0;

    switch (pctx->action) {
        case 0:
            err = config::load_or_reset_profile(name_arg, pctx->mgr);
            break;
        case 1:
            err = config::save_profile(name_arg, true, pctx->mgr);
            break;
        case 2:
            err = config::open_profiles_directory();
            break;
        case 3:
            err = config::delete_profile_file(name_arg);
            break;
        default:
            break;
    }

    if (err != 0) {
        dispatch_error_event(pctx->mgr, err, default_profile_error_msg(err));
    } else {
        dispatch_profile_action_event(pctx->mgr, name_arg, pctx->action);
    }

    if (pctx->gui != nullptr) {
        pctx->gui->last_profile_refresh_time = -1.0;
    }
}

// Matches sub_180034D20
void open_profile_confirm_modal(
    ClickGui* gui,
    GuiRenderer* renderer,
    events::ModuleManager* mgr,
    config::ProfileRegistry* profiles,
    const char* profile_name,
    int32_t action) noexcept
{
    if (action < 0 || action >= 4) {
        return;
    }
    s_profile_confirm_ctx.gui = gui;
    s_profile_confirm_ctx.mgr = mgr;
    s_profile_confirm_ctx.profiles = profiles;
    s_profile_confirm_ctx.action = action;

    if (profile_name != nullptr) {
        std::snprintf(
            s_profile_confirm_ctx.profile_name_buf,
            sizeof(s_profile_confirm_ctx.profile_name_buf),
            "%s",
            profile_name);
        s_profile_confirm_ctx.has_profile_name = true;
    } else {
        s_profile_confirm_ctx.profile_name_buf[0] = '\0';
        s_profile_confirm_ctx.has_profile_name = false;
    }

    const char* shown_name = (profile_name != nullptr) ? profile_name : "Default";
    char msg_buf[100]{};
    std::snprintf(msg_buf, sizeof(msg_buf), kProfileConfirmFormats[action], shown_name);
    gui->open_confirm_modal(renderer, &s_profile_confirm_ctx, on_confirm_profile_action, msg_buf);
}

// Matches sub_180032800
void render_tab_bar(ClickGui* gui, GuiRenderer* renderer, float dt) noexcept {
    NVGcontext* vg = renderer->vg;
    const math::Vec4f bar_rect = gui->next_cell(42.0f);

    nvgFillColor(vg, detail::color_card_bg());
    renderer->fill_rounded_rect(bar_rect, 20.0f);

    const float tab_w = bar_rect.w / 7.0f;
    const float target_tab = static_cast<float>(gui->selected_tab_index);
    const auto step_t = static_cast<float>(static_cast<double>(dt) / 0.05);
    const float ind_pos = math::lerpf_clamped(gui->tab_indicator_anim, target_tab, step_t);
    gui->tab_indicator_anim = ind_pos;

    const math::Vec4f raw_ind = math::make_rect(
        bar_rect.x + ind_pos * tab_w,
        bar_rect.y,
        tab_w,
        bar_rect.h);
    const math::Vec4f ind_rect = math::rect_inset(raw_ind, 6.0f);
    nvgFillColor(vg, detail::color_widget_bg());
    renderer->fill_rounded_rect(ind_rect, 14.0f);

    nvgFontSize(vg, 16.0f);
    for (size_t i = 0; i < 7u; ++i) {
        const math::Vec4f tab_cell = math::make_rect(
            bar_rect.x + static_cast<float>(i) * tab_w,
            bar_rect.y,
            tab_w,
            bar_rect.h);

        if (gui->consume_click_in(tab_cell)) {
            if (gui->tabs[i].tab_id == 6u) {
                gui->open_confirm_modal(renderer, gui, on_confirm_unload, "Unload Slinky?");
            } else {
                gui->selected_tab_index = i;
                gui->focus_mode = GuiFocusMode::None;
            }
        }

        const math::Vec4f hover_box = math::rect_inset(tab_cell, 6.0f);
        const bool hovered = gui->is_hovered(hover_box);

        NVGcolor text_col{};
        if (gui->tabs[i].tab_id == 6u) {
            text_col = hovered ? detail::color_danger_hover() : detail::color_danger();
        } else {
            text_col = hovered ? detail::color_text_hover() : detail::color_text_primary();
        }

        nvgFillColor(vg, text_col);
        renderer->draw_text(hover_box, kTextAlignCenterMiddle, kFontRegular, gui->tabs[i].label);
    }
}

// Matches sub_1800344F0 (returns right edge X of rendered title/expandable row)
float render_expandable_row(
    ClickGui* gui,
    GuiRenderer* renderer,
    events::Module* mod,
    const math::Vec4f& row,
    float font_size,
    const NVGcolor& normal_col,
    const NVGcolor& hover_col,
    const char* text) noexcept
{
    NVGcontext* vg = renderer->vg;
    const char* safe_text = (text != nullptr) ? text : "";

    if (mod->render_settings != nullptr) {
        const math::Vec4f icon_box = math::make_rect(row.x, row.y, row.h, row.h);
        const float text_x = (icon_box.x + icon_box.w) + 8.0f;

        nvgFontSize(vg, font_size);
        renderer->select_font(kFontRegular);
        const float text_w = nvgTextBounds(vg, text_x, row.y, safe_text, nullptr, nullptr);
        const math::Vec4f text_rect = math::make_rect(text_x, row.y, text_w, row.h);
        const math::Vec4f hit_rect = math::make_rect(row.x, row.y, icon_box.w + text_w + 8.0f, row.h);

        if (gui->consume_click_in(hit_rect)) {
            mod->settings_expanded = !mod->settings_expanded;
        }

        const bool hovered = gui->is_hovered(hit_rect);
        nvgFillColor(vg, hovered ? hover_col : normal_col);
        renderer->draw_text(text_rect, kTextAlignLeftMiddle, kFontRegular, safe_text);

        nvgFillColor(vg, hovered ? detail::color_widget_hover() : detail::color_widget_bg());
        renderer->fill_rounded_rect(icon_box, 5.0f);

        const math::Vec4f cross = math::rect_inset(icon_box, 6.0f);
        nvgStrokeColor(vg, hovered ? detail::color_text_hover() : detail::color_text_primary());
        nvgBeginPath(vg);

        const float mid_y = cross.y + cross.h * 0.5f;
        nvgMoveTo(vg, cross.x, mid_y);
        nvgLineTo(vg, cross.x + cross.w, mid_y);

        if (!mod->settings_expanded) {
            const float mid_x = cross.x + cross.w * 0.5f;
            nvgMoveTo(vg, mid_x, cross.y);
            nvgLineTo(vg, mid_x, cross.y + cross.h);
        }

        nvgStrokeWidth(vg, 2.0f);
        nvgStroke(vg);
        return hit_rect.x + hit_rect.w;
    }

    nvgFontSize(vg, font_size);
    nvgFillColor(vg, normal_col);
    return detail::draw_text_adv(renderer, row, kTextAlignLeftMiddle, kFontRegular, safe_text);
}

// Matches sub_180033F50
void render_category_cards(
    ClickGui* gui,
    GuiRenderer* renderer,
    events::ModuleManager* mgr,
    uint32_t category_index,
    float dt) noexcept
{
    if (mgr == nullptr || category_index >= static_cast<uint32_t>(events::ModuleCategory::Count)) {
        return;
    }

    NVGcontext* vg = renderer->vg;
    gui->set_auto_columns(430.0f);

    const events::ModuleSpan& span = mgr->categories[category_index];
    if (span.count == 0u || span.data == nullptr) {
        return;
    }

    const auto anim_step = static_cast<float>(static_cast<double>(dt) / 0.05);
    const math::Vec4f viewport = math::make_rect(
        0.0f,
        gui->scroll_y,
        renderer->logical_width,
        renderer->logical_height);

    for (size_t i = 0; i < span.count; ++i) {
        events::Module* mod = span.data[i];
        if (mod == nullptr) {
            continue;
        }

        const math::Vec4f card_rect = gui->next_cell(mod->card_height_anim);
        if (!math::rect_overlaps(viewport, card_rect)) {
            continue;
        }

        nvgFillColor(vg, detail::color_card_bg());
        renderer->fill_rounded_rect(card_rect, 20.0f);

        math::Vec4f clipped_card = card_rect;
        gui->push_Clipped_card(renderer, clipped_card, 8.0f, 12.0f);
        gui->set_columns(1);

        const math::Vec4f header_row = gui->next_cell(20.0f);
        float title_end_x = header_row.x;

        if (gui->hide_descriptions) {
            title_end_x = render_expandable_row(
                gui,
                renderer,
                mod,
                header_row,
                16.0f,
                detail::color_text_primary(),
                detail::color_text_hover(),
                mod->name);
        } else {
            nvgFontSize(vg, 16.0f);
            nvgFillColor(vg, detail::color_text_primary());
            title_end_x = detail::draw_text_adv(
                renderer,
                header_row,
                kTextAlignLeftMiddle,
                kFontRegular,
                mod->name != nullptr ? mod->name : "");
        }

        if ((mod->flags & events::kModuleFlagNoKeybind) == 0u) {
            const math::Vec4f bind_bounds = math::make_rect(
                title_end_x + 5.0f,
                header_row.y,
                (header_row.x + header_row.w) - title_end_x,
                header_row.h);
            bool* hold_ptr = ((mod->flags & events::kModuleFlagNoHoldMode) != 0u)
                ? nullptr
                : &mod->hold_mode;
            widgets::render_keybind_pill(
                gui,
                renderer,
                reinterpret_cast<SetKeybindTargetFn>(mgr->set_keybind_fn),
                mod,
                static_cast<uint32_t>(mod->keybind),
                hold_ptr,
                bind_bounds);
        }

        if ((mod->flags & events::kModuleFlagNoToggleSwitch) == 0u) {
            const math::Vec4f toggle_rect = math::make_rect(
                (header_row.x + header_row.w) - 40.0f,
                header_row.y,
                40.0f,
                header_row.h);
            bool desired = mod->enabled;
            widgets::render_toggle_pill(gui, renderer, toggle_rect, &mod->toggle_anim, &desired);
            if (desired != mod->enabled) {
                mgr->request_set_enabled(mod, desired);
            }
        }

        if (!gui->hide_descriptions) {
            const math::Vec4f desc_row = gui->next_cell(20.0f);
            render_expandable_row(
                gui,
                renderer,
                mod,
                desc_row,
                14.0f,
                detail::color_text_muted(),
                detail::color_text_muted_hover(),
                mod->description);
        }

        if (mod->settings_expanded && mod->render_settings != nullptr) {
            mod->render_settings(gui, renderer);
        }

        const auto measured_h = static_cast<float>(static_cast<int64_t>(gui->pop_layout(renderer)));
        if (mod->card_height_anim == 0.0f) {
            mod->card_height_anim = measured_h;
        } else {
            mod->card_height_anim = math::lerpf_clamped(mod->card_height_anim, measured_h, anim_step);
        }
    }
}

// Alt manager / cracked account card shown above the profile list.
void render_alt_manager_card(
    ClickGui* gui,
    GuiRenderer* renderer,
    events::ModuleManager* mgr,
    events::JniContext* jni) noexcept
{
    NVGcontext* vg = renderer->vg;
    const math::Vec4f card_rect = gui->next_cell(kAltCardHeight);

    nvgFillColor(vg, detail::color_card_bg());
    renderer->fill_rounded_rect(card_rect, 20.0f);

    math::Vec4f clipped_card = card_rect;
    gui->push_Clipped_card(renderer, clipped_card, 8.0f, 14.0f);
    gui->set_columns(1);

    const math::Vec4f header_row = gui->next_cell(16.0f);
    nvgFontSize(vg, 13.0f);
    nvgFillColor(vg, detail::color_text_muted());
    renderer->draw_text(header_row, kTextAlignLeftMiddle, kFontBold, "ALT MANAGER  /  MC USERNAME");

    const math::Vec4f value_row = gui->next_cell(24.0f);
    const bool editing = gui->is_editing_text(&s_alt_input_owner);

    if (!editing && gui->consume_click_in(card_rect)) {
        gui->begin_text_input(widgets::validate_mc_username_char, &s_alt_input_owner);
        const char* current = account::get_username();
        std::snprintf(
            gui->text_input.buffer,
            sizeof(gui->text_input.buffer),
            "%s",
            (current != nullptr) ? current : "");
        gui->text_input.submitted = false;
    }

    if (gui->is_editing_text(&s_alt_input_owner)) {
        nvgFontSize(vg, 18.0f);
        nvgFillColor(vg, detail::color_text_hover());
        detail::render_aligned_text_input(gui, renderer, value_row, kTextAlignLeftMiddle);

        const char* submitted = gui->consume_submitted_text();
        if (submitted != nullptr) {
            const int err = account::set_username(submitted, jni);
            char message[96]{};
            if (err == 0) {
                std::snprintf(
                    message,
                    sizeof(message),
                    "MC username set to %s - rejoin servers to use it",
                    account::get_username());
            } else if (err == 7001) {
                std::snprintf(message, sizeof(message), "Use 3-16 letters, numbers or _");
            } else if (err == 7004) {
                std::snprintf(message, sizeof(message), "Username applied but could not be saved");
            } else {
                std::snprintf(message, sizeof(message), "Username saved but session could not be updated");
            }

            if (mgr != nullptr) {
                events::ErrorNotificationEvent ev{};
                ev.id = events::kEventErrorNotification;
                ev.error_code = err;
                ev.custom_message = message;
                mgr->dispatch(&ev);
            }
        }
    } else {
        const char* current = account::get_username();
        const bool has_name = current != nullptr && current[0] != '\0';
        nvgFontSize(vg, 18.0f);
        nvgFillColor(vg, gui->is_hovered(card_rect) ? detail::color_text_hover()
                                                    : detail::color_text_primary());
        renderer->draw_text(
            value_row,
            kTextAlignLeftMiddle,
            kFontRegular,
            has_name ? current : "Click to set a cracked username");
    }

    const math::Vec4f hint_row = gui->next_cell(16.0f);
    nvgFontSize(vg, 12.0f);
    nvgFillColor(vg, detail::color_text_muted());
    renderer->draw_text(
        hint_row,
        kTextAlignLeftMiddle,
        kFontRegular,
        editing ? "Type a name - Enter to apply, Esc to cancel"
                : "Click to edit - offline-mode name, 3-16 chars");

    gui->pop_layout(renderer);
}

void render_profiles_tab(
    ClickGui* gui,
    GuiRenderer* renderer,
    events::ModuleManager* mgr,
    config::ProfileRegistry* profiles,
    events::JniContext* jni) noexcept
{
    if (profiles == nullptr) {
        return;
    }

    NVGcontext* vg = renderer->vg;

    if (gui->focus_mode == GuiFocusMode::None &&
        renderer->frame_time > (gui->last_profile_refresh_time + 2.0))
    {
        const int refresh_err = profiles->refresh_from_disk();
        if (refresh_err != 0) {
            dispatch_error_event(mgr, refresh_err);
        }
        gui->last_profile_refresh_time = renderer->frame_time;
    }

    gui->set_columns(1);
    render_alt_manager_card(gui, renderer, mgr, jni);
    if (gui->layout_depth > 0u) {
        // The account card spans its own full-width row; profiles start below it.
        flush_layout_row(gui->layout_stack[gui->layout_depth - 1u]);
        gui->set_auto_columns(430.0f);
    }

    size_t entry_count = 0u;
    const config::ProfileEntry* entries = profiles->get_entries(&entry_count);

    const math::Vec4f viewport = math::make_rect(
        0.0f,
        gui->scroll_y,
        renderer->logical_width,
        renderer->logical_height);

    // Index 0 renders "Default", indices 1..entry_count render entries[0..entry_count-1]
    for (size_t idx = 0; idx <= entry_count; ++idx) {
        const math::Vec4f card_rect = gui->next_cell(get_profile_card_height(gui));
        if (!math::rect_overlaps(viewport, card_rect)) {
            continue;
        }

        const config::ProfileEntry* entry = (idx == 0u || entries == nullptr)
            ? nullptr
            : &entries[idx - 1u];

        nvgFillColor(vg, detail::color_card_bg());
        renderer->fill_rounded_rect(card_rect, 20.0f);

        math::Vec4f clipped_card = card_rect;
        gui->push_Clipped_card(renderer, clipped_card, 8.0f, 12.0f);
        gui->set_columns(1);

        const math::Vec4f header_row = gui->next_cell(20.0f);
        nvgFontSize(vg, 16.0f);
        nvgFillColor(vg, detail::color_text_primary());

        if (entry != nullptr) {
            const float end_x = detail::draw_text_range_adv(
                renderer,
                header_row,
                kTextAlignLeftMiddle,
                kFontRegular,
                entry->name,
                entry->display_name_end);
            const math::Vec4f bind_bounds = math::make_rect(
                end_x + 5.0f,
                header_row.y,
                (header_row.x + header_row.w) - end_x,
                header_row.h);
            widgets::render_keybind_pill(
                gui,
                renderer,
                reinterpret_cast<SetKeybindTargetFn>(profiles->bind_callback()),
                const_cast<config::ProfileEntry*>(entry),
                entry->keybind,
                nullptr,
                bind_bounds);
        } else {
            detail::draw_text_range_adv(
                renderer,
                header_row,
                kTextAlignLeftMiddle,
                kFontRegular,
                "Default",
                nullptr);
        }

        int offset_gap = 36;
        int offset_icons = 80;
        for (int btn = 0; btn < 4; ++btn) {
            const float btn_x = ((header_row.x + header_row.w) - static_cast<float>(offset_icons)) -
                                static_cast<float>(offset_gap);
            const math::Vec4f btn_rect = math::make_rect(btn_x, header_row.y, 20.0f, 20.0f);

            const bool disabled = (entry == nullptr) && ((btn & 1) != 0);
            float icon_alpha = 0.5f;

            if (!disabled) {
                icon_alpha = 1.0f;
                if (gui->is_hovered(btn_rect)) {
                    const auto tip_x = static_cast<float>(
                        static_cast<int32_t>(btn_rect.x + btn_rect.w * 0.5f));
                    const auto tip_y = static_cast<float>(static_cast<int32_t>(btn_rect.y));
                    gui->show_tooltip(kProfileBtnTooltips[btn], tip_x, tip_y);

                    if (gui->consume_click_in(btn_rect)) {
                        const char* pname = (entry != nullptr) ? entry->name : nullptr;
                        switch (btn) {
                            case 0: { // Load
                                if (!config::is_profile_dirty()) {
                                    const int err = config::load_or_reset_profile(pname, mgr);
                                    if (err == 0) {
                                        dispatch_profile_action_event(mgr, pname, 0);
                                        gui->last_profile_refresh_time = -1.0;
                                    } else {
                                        dispatch_error_event(mgr, err, default_profile_error_msg(err));
                                    }
                                } else {
                                    open_profile_confirm_modal(gui, renderer, mgr, profiles, pname, 0);
                                }
                                break;
                            }
                            case 1: { // Save
                                if (entry == nullptr) {
                                    dispatch_error_event(mgr, 6003, "Default profile cannot be overwritten");
                                } else {
                                    const char* active_name = config::get_active_profile_name();
                                    if (active_name != nullptr && std::strcmp(pname, active_name) == 0) {
                                        const int err = config::save_profile(pname, true, mgr);
                                        if (err == 0) {
                                            dispatch_profile_action_event(mgr, pname, 1);
                                            gui->last_profile_refresh_time = -1.0;
                                        } else {
                                            dispatch_error_event(mgr, err, default_profile_error_msg(err));
                                        }
                                    } else {
                                        open_profile_confirm_modal(gui, renderer, mgr, profiles, pname, 1);
                                    }
                                }
                                break;
                            }
                            case 2: { // Open folder
                                open_profile_confirm_modal(gui, renderer, mgr, profiles, pname, 2);
                                break;
                            }
                            case 3: { // Delete
                                if (entry == nullptr) {
                                    dispatch_error_event(mgr, 6004, "Default profile cannot be deleted");
                                } else {
                                    open_profile_confirm_modal(gui, renderer, mgr, profiles, pname, 3);
                                }
                                break;
                            }
                            default:
                                break;
                        }
                    }
                }
            }

            detail::draw_atlas_icon(renderer, btn_rect, kProfileBtnIcons[btn], icon_alpha);
            offset_gap -= 12;
            offset_icons -= 20;
        }

        if (!gui->hide_descriptions) {
            const math::Vec4f desc_row = gui->next_cell(20.0f);
            nvgFontSize(vg, 14.0f);
            nvgFillColor(vg, detail::color_text_muted());
            const char* modified_str = (entry != nullptr)
                ? entry->modified_label
                : "Contains the default configuration.";
            renderer->draw_text(desc_row, kTextAlignLeftMiddle, kFontRegular, modified_str);
        }

        set_profile_card_height(gui, gui->pop_layout(renderer));
    }

    // "CREATE NEW PROFILE" card
    const math::Vec4f create_card = gui->next_cell(get_profile_card_height(gui));
    nvgFillColor(vg, nvgTransRGBAf(detail::color_card_bg(), 0.5f));
    renderer->fill_rounded_rect(create_card, 20.0f);

    if (detail::activate_or_check_text_input(
            gui,
            renderer,
            create_card,
            widgets::validate_profile_name_char,
            nullptr))
    {
        nvgFontSize(vg, 16.0f);
        nvgFillColor(vg, detail::color_text_hover());
        const math::Vec4f input_rect = math::make_rect(
            create_card.x,
            (create_card.y + create_card.h * 0.5f) - 8.0f,
            create_card.w,
            16.0f);
        detail::render_centered_text_input(gui, renderer, input_rect);

        const char* submitted_name = gui->consume_submitted_text();
        if (submitted_name != nullptr) {
            const int err = config::save_profile(submitted_name, false, mgr);
            if (err != 0) {
                const char* msg = (err == 1012) ? "Profile name is taken." : nullptr;
                dispatch_error_event(mgr, err, msg);
            } else {
                dispatch_profile_action_event(mgr, submitted_name, 1);
                gui->last_profile_refresh_time = -1.0;
            }
        }
    } else {
        nvgFontSize(vg, 15.0f);
        const NVGcolor label_col = gui->is_hovered(create_card)
            ? detail::color_text_hover()
            : detail::color_text_primary();
        nvgFillColor(vg, label_col);
        renderer->draw_text(create_card, kTextAlignCenterMiddle, kFontBold, "CREATE NEW PROFILE");
    }
}

} // namespace

// Matches sub_180033DE0
void ClickGui::initialize() noexcept {
    if (modal_kind == GuiModalKind::ItemPicker && item_picker.filtered != nullptr) {
        std::free(item_picker.filtered->data);
        std::free(item_picker.filtered);
    }

    std::memset(static_cast<void*>(this), 0, sizeof(ClickGui));

    tabs[0] = GuiTab{0u, 0u, "Combat"};
    tabs[1] = GuiTab{1u, 0u, "Move"};
    tabs[2] = GuiTab{2u, 0u, "Visual"};
    tabs[3] = GuiTab{3u, 0u, "Block"};
    tabs[4] = GuiTab{4u, 0u, "Utility"};
    tabs[5] = GuiTab{5u, 0u, "Profiles"};
    tabs[6] = GuiTab{6u, 0u, "Unload"};

    last_profile_refresh_time = -1.0;
    accent_color = nvgRGBAf(0.89f, 0.31f, 0.31f, 1.0f);
}

// Matches sub_180032C10
void ClickGui::on_open(double current_time) noexcept {
    open_fade_anim = 0.0f;
    open_time = current_time;
}

// Matches sub_180033EC0
void ClickGui::reset_interaction() noexcept {
    focus_mode = GuiFocusMode::None;
    mouse_left_down = false;
}

// Matches sub_180032340
bool ClickGui::handle_input(const input::InputEvent* ev, double current_time) noexcept {
    if (ev == nullptr) {
        return false;
    }

    switch (ev->kind) {
        case input::InputEventKind::MouseScroll: {
            if (!has_active_modal()) {
                scroll_y += ev->scroll.delta * -30.0f;
            }
            return false;
        }

        case input::InputEventKind::MouseMove: {
            cursor_fb.x = static_cast<float>(ev->move.x);
            cursor_fb.y = static_cast<float>(ev->move.y);
            return false;
        }

        case input::InputEventKind::KeyOrButton: {
            if (ev->pressed == 1u) {
                if (focus_mode == GuiFocusMode::InlineTextInput ||
                    focus_mode == GuiFocusMode::ModalTextInput)
                {
                    const int32_t kc = ev->key.key_code;
                    if (kc == input::KEY_ENTER) {
                        if (text_input.buffer[0] != '\0') {
                            text_input.submitted = true;
                        }
                        return true;
                    }
                    if (kc == input::KEY_ESC) {
                        focus_mode = GuiFocusMode::None;
                        return true;
                    }
                    if (kc == input::KEY_MOUSE1) {
                        if (focus_mode != GuiFocusMode::ModalTextInput) {
                            focus_mode = GuiFocusMode::None;
                            return true;
                        }
                    } else if (kc == input::KEY_BACKSPACE) {
                        const size_t len = std::strlen(text_input.buffer);
                        if (len > 0u) {
                            text_input.buffer[len - 1u] = '\0';
                        }
                        return true;
                    } else {
                        if (text_input.validator != nullptr) {
                            for (int i = 0; i < 4 && ev->key.utf8_text[i] != '\0'; ++i) {
                                text_input.validator(text_input.buffer, ev->key.utf8_text[i]);
                            }
                        }
                        return true;
                    }
                } else if (focus_mode == GuiFocusMode::KeybindCapture) {
                    const auto raw_kc = static_cast<uint32_t>(ev->key.key_code);
                    const uint32_t target_kc =
                        (raw_kc != static_cast<uint32_t>(input::KEY_MOUSE1) &&
                         raw_kc != static_cast<uint32_t>(input::KEY_ESC))
                        ? raw_kc
                        : 0u;
                    if (keybind.callback != nullptr) {
                        keybind.callback(keybind.target, target_kc);
                    }
                    focus_mode = GuiFocusMode::None;
                    return true;
                } else if (focus_mode == GuiFocusMode::None) {
                    if (has_active_modal() && ev->key.key_code == input::KEY_ESC) {
                        close_modal(current_time);
                        return true;
                    }
                }
            }

            const int32_t kc = ev->key.key_code;
            if (kc == input::KEY_LCONTROL || kc == input::KEY_RCONTROL) {
                ctrl_held = (ev->pressed != 0u);
            } else if (kc == input::KEY_MOUSE1) {
                mouse_left_down = (ev->pressed != 0u);
            }
            return false;
        }

        case input::InputEventKind::RawInput:
        default:
            return false;
    }
}

// Matches sub_1800317A0
void ClickGui::reset_layout() noexcept {
    layout_depth = 0u;
    max_content_bottom = 0;
}

// Matches sub_1800317C0
void ClickGui::push_layout(math::Vec4f& inout_bounds, float gap, float margin) noexcept {
    const size_t depth = layout_depth;
    if (depth > 0u) {
        const math::Vec4f& parent = layout_stack[depth - 1u].bounds;
        if (parent.x > inout_bounds.x) {
            inout_bounds.x = parent.x;
        }
        if (parent.y > inout_bounds.y) {
            inout_bounds.y = parent.y;
        }
        const float parent_right = parent.x + parent.w;
        if ((inout_bounds.x + inout_bounds.w) > parent_right) {
            inout_bounds.w = std::fmax(0.0f, parent_right - inout_bounds.x);
        }
        const float parent_bottom = parent.y + parent.h;
        if ((inout_bounds.y + inout_bounds.h) > parent_bottom) {
            inout_bounds.h = std::fmax(0.0f, parent_bottom - inout_bounds.y);
        }
    }

    layout_depth = depth + 1u;
    inout_bounds = math::rect_inset(inout_bounds, margin);

    LayoutFrame& frame = layout_stack[depth];
    frame = LayoutFrame{};
    frame.bounds = inout_bounds;
    frame.gap = gap;
    frame.outer_margin = margin;
    frame.has_scissor = false;
    frame.cursor_origin = math::make_vec2(inout_bounds.x, inout_bounds.y);
    frame.columns = 1u;
    frame.item_index = 0u;
}

// Matches sub_180031930
void ClickGui::push_Clipped_card(
    GuiRenderer* renderer,
    math::Vec4f& inout_bounds,
    float gap,
    float margin) noexcept
{
    NVGcontext* vg = renderer->vg;
    math::Vec4f inner = inout_bounds;
    push_layout(inner, gap, margin);
    layout_stack[layout_depth - 1u].has_scissor = true;

    inout_bounds = math::rect_round(inout_bounds);
    nvgSave(vg);
    nvgScissor(
        vg,
        inout_bounds.x + gap,
        inout_bounds.y + gap,
        inout_bounds.w - gap * 2.0f,
        inout_bounds.h - gap * 2.0f);
}

// Matches sub_1800319F0
float ClickGui::pop_layout(GuiRenderer* renderer) noexcept {
    if (layout_depth == 0u) {
        return 0.0f;
    }

    LayoutFrame& frame = layout_stack[layout_depth - 1u];
    flush_layout_row(frame);

    if (frame.has_scissor) {
        nvgRestore(renderer->vg);
    }

    if (frame.cursor_origin.y > static_cast<float>(max_content_bottom)) {
        max_content_bottom = static_cast<int32_t>(frame.cursor_origin.y);
    }

    --layout_depth;
    return (frame.gap + frame.gap) + (frame.cursor_origin.y - frame.bounds.y);
}

// Matches sub_180031AE0
size_t ClickGui::set_columns(int32_t cols) noexcept {
    if (layout_depth == 0u) {
        return 1u;
    }
    LayoutFrame& frame = layout_stack[layout_depth - 1u];
    flush_layout_row(frame);

    const int32_t clamped = (cols <= 0) ? 1 : std::min<int32_t>(cols, 4);
    frame.columns = static_cast<size_t>(clamped);
    frame.item_index = 0u;
    return frame.columns;
}

// Matches sub_180031BB0
size_t ClickGui::set_auto_columns(float min_col_width) noexcept {
    if (layout_depth == 0u) {
        return 1u;
    }
    LayoutFrame& frame = layout_stack[layout_depth - 1u];
    flush_layout_row(frame);

    const auto fit = static_cast<int32_t>((frame.bounds.w + frame.gap) / (min_col_width + frame.gap));
    const int32_t clamped = (fit <= 0) ? 1 : std::min<int32_t>(fit, 4);
    frame.columns = static_cast<size_t>(clamped);
    frame.item_index = 0u;
    return frame.columns;
}

// Matches sub_180031C80
math::Vec4f ClickGui::next_cell(float height) noexcept {
    if (layout_depth == 0u) {
        return math::make_rect(0.0f, 0.0f, 0.0f, height);
    }

    LayoutFrame& frame = layout_stack[layout_depth - 1u];
    const size_t cols = (frame.columns > 0u) ? frame.columns : 1u;
    const size_t idx = frame.item_index++;
    const size_t col = idx % cols;

    math::Vec4f& cell = frame.prev_cells[idx & 3u];
    const math::Vec4f& above = frame.prev_cells[(idx - cols) & 3u];

    if (col != 0u) {
        const math::Vec4f& left = frame.prev_cells[(idx - 1u) & 3u];
        cell.x = (left.x + left.w) + frame.gap;
        cell.w = left.w;
        cell.y = (idx >= cols) ? ((above.y + above.h) + frame.gap) : frame.cursor_origin.y;
    } else {
        cell.x = frame.cursor_origin.x;
        if (idx >= cols) {
            cell.y = (above.y + above.h) + frame.gap;
            cell.w = above.w;
        } else {
            cell.y = frame.cursor_origin.y;
            cell.w = (frame.bounds.w - static_cast<float>(cols - 1u) * frame.gap) /
                     static_cast<float>(cols);
        }
    }
    cell.h = height;
    return math::rect_round(cell);
}

// Matches sub_180032590
bool ClickGui::is_hovered(const math::Vec4f& rect) const noexcept {
    if (has_active_modal() && !is_rendering_modal()) {
        return false;
    }
    if (!math::rect_contains(rect, cursor_logical_scrolled)) {
        return false;
    }
    return focus_mode == GuiFocusMode::None || focus_mode == GuiFocusMode::ModalTextInput;
}

// Matches sub_180032600
bool ClickGui::consume_click_in(const math::Vec4f& rect) noexcept {
    if (has_active_modal() && !is_rendering_modal()) {
        return false;
    }
    if (!math::rect_contains(rect, cursor_logical_scrolled)) {
        return false;
    }
    if (focus_mode != GuiFocusMode::None && focus_mode != GuiFocusMode::ModalTextInput) {
        return false;
    }
    if (!mouse_left_down || mouse_click_consumed) {
        return false;
    }
    mouse_click_consumed = true;
    return true;
}

// Matches sub_180032680
void ClickGui::consume_any_click() noexcept {
    if (mouse_left_down && !mouse_click_consumed) {
        mouse_click_consumed = true;
    }
}

// Matches sub_1800326A0
void ClickGui::begin_keybind_capture(SetKeybindTargetFn cb, void* target) noexcept {
    focus_mode = GuiFocusMode::KeybindCapture;
    keybind.callback = cb;
    keybind.target = target;
}

// Matches sub_1800326C0
bool ClickGui::is_capturing_keybind(const void* target) const noexcept {
    return focus_mode == GuiFocusMode::KeybindCapture && keybind.target == target;
}

// Matches sub_1800326E0
void ClickGui::begin_text_input(config::ListInputValidatorFn validator, void* owner) noexcept {
    const bool already_text =
        (focus_mode == GuiFocusMode::InlineTextInput || focus_mode == GuiFocusMode::ModalTextInput);
    if (!already_text || text_input.owner != owner) {
        focus_mode = GuiFocusMode::InlineTextInput;
        std::memset(&text_input, 0, sizeof(text_input));
        text_input.validator = validator;
        text_input.owner = owner;
    }
}

// Matches sub_180032750
bool ClickGui::is_editing_text(const void* owner) const noexcept {
    const bool in_text =
        (focus_mode == GuiFocusMode::InlineTextInput || focus_mode == GuiFocusMode::ModalTextInput);
    return in_text && text_input.owner == owner;
}

// Matches sub_180032780
void ClickGui::promote_text_input_to_modal() noexcept {
    if (focus_mode == GuiFocusMode::InlineTextInput) {
        focus_mode = GuiFocusMode::ModalTextInput;
    }
}

// Matches sub_1800327A0
const char* ClickGui::consume_submitted_text() noexcept {
    const bool in_text =
        (focus_mode == GuiFocusMode::InlineTextInput || focus_mode == GuiFocusMode::ModalTextInput);
    if (!in_text || !text_input.submitted) {
        return nullptr;
    }
    focus_mode = GuiFocusMode::None;
    text_input.submitted = false;
    return text_input.buffer;
}

// Matches sub_1800327E0
const char* ClickGui::current_text_buffer() const noexcept {
    const bool in_text =
        (focus_mode == GuiFocusMode::InlineTextInput || focus_mode == GuiFocusMode::ModalTextInput);
    return in_text ? text_input.buffer : nullptr;
}

// Matches sub_1800360E0
void ClickGui::show_tooltip(const char* text, float x, float y) noexcept {
    tooltip_active = true;
    if (text != nullptr) {
        std::strncpy(tooltip_text, text, 100u);
        tooltip_text[100] = '\0';
    } else {
        tooltip_text[0] = '\0';
    }
    tooltip_pos = math::make_vec2(x, y);
}

// Matches sub_180032C30
void ClickGui::render(
    GuiRenderer* renderer,
    events::ModuleManager* mgr,
    config::ProfileRegistry* profiles,
    events::JniContext* jni) noexcept
{
    NVGcontext* vg = renderer->vg;

    nvgSave(vg);
    detail::apply_fade_and_dim_slide(renderer, open_fade_anim);

    const float max_container_w = wide_layout ? 1500.0f : 900.0f;
    const float container_w = std::fmin(renderer->logical_width - 16.0f, max_container_w);
    const float container_x = (renderer->logical_width - container_w) * 0.5f;

    if (scroll_y < 0.0f) {
        scroll_y = 0.0f;
    } else if (scroll_y > max_scroll_y) {
        scroll_y = max_scroll_y;
    }

    cursor_logical_scrolled = math::vec2_scale(cursor_fb, 1.0f / renderer->scale);
    cursor_logical_scrolled.y += scroll_y;

    nvgTranslate(vg, 0.0f, -scroll_y);
    reset_layout();

    const math::Vec4f uninset_main_bounds = math::make_rect(
        container_x,
        0.0f,
        container_w,
        renderer->logical_height);

    const auto dt = static_cast<float>(renderer->frame_time - open_time);
    const double next_fade = static_cast<double>(open_fade_anim) + static_cast<double>(dt) / 0.3;
    open_fade_anim = (next_fade <= 1.000000059604645) ? static_cast<float>(next_fade) : 1.0f;

    math::Vec4f main_bounds = uninset_main_bounds;
    push_layout(main_bounds, 8.0f, 12.0f);
    set_columns(1);

    render_tab_bar(this, renderer, dt);

    const float tab_diff = static_cast<float>(selected_tab_index) - tab_indicator_anim;
    if (tab_diff != 0.0f) {
        const float abs_diff = std::fabs(tab_diff);
        const float tab_alpha = (abs_diff > 0.5f) ? 0.0f : (1.0f - (abs_diff + abs_diff));
        nvgGlobalAlpha(vg, tab_alpha);
        const auto slide_x = static_cast<float>(
            static_cast<double>(tab_diff) * (static_cast<double>(renderer->logical_width) * -0.1));
        nvgTranslate(vg, slide_x, 0.0f);
    }

    const size_t safe_tab = (selected_tab_index < 7u) ? selected_tab_index : 0u;
    const uint32_t active_tab_id = tabs[safe_tab].tab_id;
    switch (active_tab_id) {
        case 0u:
        case 1u:
        case 2u:
        case 3u:
        case 4u:
            render_category_cards(this, renderer, mgr, active_tab_id, dt);
            break;
        case 5u:
            render_profiles_tab(this, renderer, mgr, profiles, jni);
            break;
        default:
            break;
    }

    pop_layout(renderer);

    // Animated tooltip bubble (in scrolled coordinate space)
    if (tooltip_active) {
        const float next_tip_anim = get_tooltip_anim(this) +
                                    static_cast<float>(static_cast<double>(dt) / 0.1);
        set_tooltip_anim(this, next_tip_anim);

        nvgSave(vg);
        nvgTranslate(vg, tooltip_pos.x, tooltip_pos.y);

        if (next_tip_anim <= 1.0f) {
            const float s = math::smoothstep01(next_tip_anim);
            nvgGlobalAlpha(vg, s);
            nvgScale(vg, s, s);
        } else {
            set_tooltip_anim(this, 1.0f);
        }

        nvgFontSize(vg, 14.0f);
        renderer->select_font(kFontRegular);
        const float tip_w = nvgTextBounds(vg, 0.0f, 0.0f, tooltip_text, nullptr, nullptr);
        const math::Vec4f tip_box = math::make_rect(
            tip_w * -0.5f - 5.0f,
            -28.0f,
            tip_w + 10.0f,
            20.0f);

        nvgFillColor(vg, detail::color_widget_bg());
        renderer->fill_rounded_rect(tip_box, 5.0f);

        nvgFillColor(vg, detail::color_text_primary());
        renderer->draw_text(tip_box, kTextAlignCenterMiddle, kFontRegular, tooltip_text);

        nvgRestore(vg);
        tooltip_active = false;
    } else {
        set_tooltip_anim(this, 0.0f);
    }

    nvgRestore(vg);

    max_scroll_y = std::fmax(
        0.0f,
        (static_cast<float>(max_content_bottom) - renderer->logical_height) + 12.0f);

    // Modal overlay pass (unscrolled screen space, max 600px centered card)
    if (has_active_modal()) {
        nvgSave(vg);
        cursor_logical_scrolled.y -= scroll_y;
        detail::apply_fade_and_dim_slide(renderer, modal_anim_progress);
        reset_layout();

        math::Vec4f modal_bounds = uninset_main_bounds;
        if (modal_bounds.w > 600.0f) {
            modal_bounds.x += (modal_bounds.w - 600.0f) * 0.5f;
            modal_bounds.w = 600.0f;
        }
        push_layout(modal_bounds, 8.0f, 12.0f);
        render_modal_overlay(renderer);
        pop_layout(renderer);
        nvgRestore(vg);
    }

    consume_any_click();
    mouse_click_consumed = mouse_left_down;
    open_time = renderer->frame_time;
}

} // namespace slinky::gui
