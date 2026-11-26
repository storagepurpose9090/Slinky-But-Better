#include "modules/visual/notifications.hpp"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "config/settings.hpp"
#include "gui/gui_renderer.hpp"
#include "gui/widgets.hpp"
#include "modules/module_interop.hpp"
#include "modules/visual/gui.hpp"
#include "nanovg.h"
#include "util/keycodes.hpp"
#include "util/math.hpp"

namespace slinky::modules {

namespace {

constexpr size_t kMaxToasts = 10;
constexpr size_t kToastTextMaxChars = 101; // 0x65 passed to snprintf

constexpr const char* kProfileActionNames[4] = {
    "Loaded profile",
    "Saved profile",
    nullptr,
    "Deleted profile",
};

// 112-byte toast slot (@ 0x1803191E0)
struct ToastEntry {
    int32_t kind = 0;        // +0x00 (0 = success, 1 = accent, 2 = error)
    char text[104]{};        // +0x04..+0x6B
    float elapsed = 0.0f;    // +0x6C
};
static_assert(sizeof(ToastEntry) == 0x70, "ToastEntry must be 112 bytes");

config::BoolSetting s_hide_ingame{{}, "In-game", false, false};
config::BoolSetting s_hide_hold_notifs{{}, "Hold bind is used", true, false};
config::BoolSetting s_hide_bind_only{{}, "Module is bind-only", true, false};
config::BoolSetting s_show_combat{{}, "Combat", true, false};
config::BoolSetting s_show_move{{}, "Move", true, false};
config::BoolSetting s_show_block{{}, "Block", true, false};
config::BoolSetting s_show_visual{{}, "Visual", true, false};
config::BoolSetting s_show_utility{{}, "Utility", true, false};

std::atomic<uint8_t> s_welcome_shown{0};  // byte_1803191B0
const char* s_pending_enabled_name = nullptr;  // qword_1803191B8
uint32_t s_pending_enabled_count = 0;          // dword_1803191C0
const char* s_pending_disabled_name = nullptr; // qword_1803191C8
uint32_t s_pending_disabled_count = 0;         // dword_1803191D0
size_t s_toast_count = 0;                      // qword_1803191D8
ToastEntry s_toasts[kMaxToasts]{};             // unk_1803191E0
size_t s_toast_head = 0;                       // qword_180319640

ToastEntry& push_toast_slot(int32_t kind) noexcept {
    const size_t slot = (s_toast_head + s_toast_count) % kMaxToasts;
    ToastEntry& dst = s_toasts[slot];
    dst.kind = kind;
    dst.elapsed = 0.0f;
    if (s_toast_count == kMaxToasts) {
        ++s_toast_head;
    } else {
        ++s_toast_count;
    }
    return dst;
}

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

// Matches sub_180056770
void on_module_state_changed(void* raw_event) noexcept {
    const auto* ev = static_cast<const events::ModuleStateChangedEvent*>(raw_event);
    events::Module* mod = ev->module;

    if (mod == get_notifications_module()) {
        if (!ev->enabled) {
            s_toast_count = 0;
            return;
        }
        uint8_t expected = 0;
        if (s_welcome_shown.compare_exchange_strong(expected, 1, std::memory_order_acq_rel)) {
            ToastEntry& toast = push_toast_slot(1);
            const char* key_name = input::get_key_name(get_gui_module()->keybind);
            std::snprintf(toast.text, kToastTextMaxChars, "Press %s to open GUI", key_name);
        }
        return;
    }

    const uint32_t flags = mod->flags;
    if ((flags & events::kModuleFlagGlobalSettings) != 0) {
        return;
    }
    if ((flags & events::kModuleFlagNoToggleSwitch) != 0 && s_hide_bind_only.value) {
        return;
    }
    if (mod->hold_mode && s_hide_hold_notifs.value) {
        return;
    }
    if (!is_category_visible(mod->category)) {
        return;
    }

    if (ev->enabled) {
        s_pending_enabled_name = mod->name;
        ++s_pending_enabled_count;
    } else {
        s_pending_disabled_name = mod->name;
        ++s_pending_disabled_count;
    }
}

// Matches sub_180056910
void on_gui_frame_poll(void* raw_event) noexcept {
    auto* ev = static_cast<events::GuiFramePollEvent*>(raw_event);

    if (s_hide_ingame.value && !get_gui_module()->enabled) {
        s_toast_count = 0;
        s_pending_enabled_count = 0;
        s_pending_disabled_count = 0;
        return;
    }

    while (s_toast_count > 0 && s_toasts[s_toast_head % kMaxToasts].elapsed > 5.0f) {
        ++s_toast_head;
        --s_toast_count;
    }

    if (s_pending_enabled_count > 0) {
        ToastEntry& toast = push_toast_slot(1);
        if (s_pending_enabled_count == 1) {
            std::snprintf(toast.text, kToastTextMaxChars, "%s %s", "Enabled", s_pending_enabled_name);
        } else {
            std::snprintf(toast.text, kToastTextMaxChars, "%s %u mods", "Enabled", s_pending_enabled_count);
        }
        s_pending_enabled_count = 0;
    }

    if (s_pending_disabled_count > 0) {
        ToastEntry& toast = push_toast_slot(1);
        if (s_pending_disabled_count == 1) {
            std::snprintf(toast.text, kToastTextMaxChars, "%s %s", "Disabled", s_pending_disabled_name);
        } else {
            std::snprintf(toast.text, kToastTextMaxChars, "%s %u mods", "Disabled", s_pending_disabled_count);
        }
        s_pending_disabled_count = 0;
    }

    if (s_toast_count == 0) {
        return;
    }

    ev->render_flags |= 1u;
    bool animating = false;
    for (size_t i = 0; i < s_toast_count; ++i) {
        ToastEntry& toast = s_toasts[(s_toast_head + i) % kMaxToasts];
        toast.elapsed += ev->delta_time;
        const double t = static_cast<double>(toast.elapsed);
        if (t < 0.2 || t > 4.8) {
            animating = true;
        }
    }

    if (animating) {
        ev->render_flags |= 3u;
    }
}

// Matches sub_180056C20
void on_gui_render_2d(void* raw_event) noexcept {
    if (s_toast_count == 0) {
        return;
    }
    if (s_hide_ingame.value && !get_gui_module()->enabled) {
        return;
    }

    const auto* ev = static_cast<const events::GuiRender2DEvent*>(raw_event);
    gui::GuiRenderer* renderer = ev->renderer;
    NVGcontext* vg = renderer->vg;

    nvgFontSize(vg, 14.0f);

    float cur_y = 12.0f;
    constexpr float kBaseX = 12.0f;
    for (size_t i = 0; i < s_toast_count; ++i) {
        const ToastEntry& toast = s_toasts[(s_toast_head + i) % kMaxToasts];
        const double t = static_cast<double>(toast.elapsed);

        float anim = 1.0f;
        if (t < 0.2) {
            anim = math::smoothstep01(static_cast<float>(t / 0.2));
        } else if (t > 4.8) {
            anim = math::smoothstep01(static_cast<float>((5.0 - t) / 0.2));
        }
        anim = std::fmin(1.0f, anim);

        nvgGlobalAlpha(vg, anim);
        const float x = kBaseX + (1.0f - anim) * -100.0f;

        NVGcolor badge_color{};
        if (toast.kind == 2) {
            badge_color = nvgRGBAf(0.8666667f, 0.2784314f, 0.2784314f, 1.0f);
        } else if (toast.kind == 1) {
            const math::Color3f acc = gui_get_accent_color();
            badge_color = nvgRGBf(acc.r, acc.g, acc.b);
        } else if (toast.kind == 0) {
            badge_color = nvgRGBAf(0.5490196f, 0.9254902f, 0.16862746f, 1.0f);
        } else {
            badge_color = nvgRGBAf(0.8666667f, 0.8666667f, 0.8666667f, 1.0f);
        }

        const float text_w = nvgTextBounds(vg, 0.0f, 0.0f, toast.text, nullptr, nullptr);
        const math::Vec4f outer_rect = math::make_rect(x, cur_y, text_w + 102.0f, 42.0f);

        nvgFillColor(vg, nvgRGBAf(0.078431375f, 0.078431375f, 0.078431375f, 1.0f));
        renderer->fill_rounded_rect(outer_rect, 20.0f);

        const math::Vec4f badge_rect = math::make_rect(
            outer_rect.x + 8.0f,
            outer_rect.y + 8.0f,
            70.0f,
            outer_rect.h - 16.0f);

        nvgFillColor(vg, nvgTransRGBAf(badge_color, 0.15f));
        renderer->fill_rounded_rect(badge_rect, 10.0f);

        nvgFillColor(vg, badge_color);
        renderer->draw_text(badge_rect, gui::kTextAlignCenterMiddle, gui::kFontRegular, "SLINKY");

        const math::Vec4f msg_rect = math::make_rect(
            badge_rect.x + badge_rect.w + 8.0f,
            outer_rect.y,
            outer_rect.w - badge_rect.w - 16.0f,
            outer_rect.h);

        nvgFillColor(vg, nvgRGBAf(0.8666667f, 0.8666667f, 0.8666667f, 1.0f));
        renderer->draw_text(msg_rect, gui::kTextAlignLeftMiddle, gui::kFontRegular, toast.text);

        cur_y += anim * 50.0f;
    }

    nvgGlobalAlpha(vg, 1.0f);
}

// Matches sub_1800570D0
void on_profile_action(void* raw_event) noexcept {
    const auto* ev = static_cast<const events::ProfileActionEvent*>(raw_event);
    if (ev->action == 2 || ev->action < 0 || ev->action > 3) {
        return;
    }

    const char* action_str = kProfileActionNames[ev->action];
    const char* profile_name = (ev->profile_name != nullptr) ? ev->profile_name : "Default";
    ToastEntry& toast = push_toast_slot(0);
    std::snprintf(toast.text, kToastTextMaxChars, "%s %s", action_str, profile_name);
}

// Matches sub_1800571A0
void on_error_notification(void* raw_event) noexcept {
    const auto* ev = static_cast<const events::ErrorNotificationEvent*>(raw_event);
    ToastEntry& toast = push_toast_slot(2);
    if (ev->custom_message != nullptr) {
        std::snprintf(toast.text, kToastTextMaxChars, "%s", ev->custom_message);
    } else {
        std::snprintf(toast.text, kToastTextMaxChars, "Error code %d", ev->error_code);
    }
}

// Matches sub_180057260
void on_friend_changed(void* raw_event) noexcept {
    const auto* ev = static_cast<const events::FriendChangedEvent*>(raw_event);
    ToastEntry& toast = push_toast_slot(1);
    const char* msg = ev->added ? "Friend added" : "Friend removed";
    std::memcpy(toast.text, msg, std::strlen(msg) + 1);
}

void render_settings(gui::ClickGui* gui, gui::GuiRenderer* renderer) noexcept {
    gui::widgets::render_section_header(gui, renderer, "Hide if");
    s_hide_ingame.render(gui, renderer);
    s_hide_hold_notifs.render(gui, renderer);
    s_hide_bind_only.render(gui, renderer);
    gui::widgets::render_section_header(gui, renderer, "Visible mod categories");
    s_show_combat.render(gui, renderer);
    s_show_move.render(gui, renderer);
    s_show_block.render(gui, renderer);
    s_show_visual.render(gui, renderer);
    s_show_utility.render(gui, renderer);
}

void visit_config(config::ConfigArchive* ar) noexcept {
    s_hide_ingame.visit(ar, "hide_ingame");
    s_hide_hold_notifs.visit(ar, "hide_hold_notifs");
    s_hide_bind_only.visit(ar, "hide_bind_only");
    s_show_combat.visit(ar, "show_combat");
    s_show_move.visit(ar, "show_move");
    s_show_block.visit(ar, "show_block");
    s_show_visual.visit(ar, "show_visual");
    s_show_utility.visit(ar, "show_utility");
}

events::Module create_module() noexcept {
    events::Module mod{};
    mod.name = "Notifications";
    mod.description = "Informs you when certain actions are performed.";
    mod.category = events::ModuleCategory::Visual;
    mod.flags = events::kModuleFlagNoKeybind |
                events::kModuleFlagNoToggleSwitch |
                events::kModuleFlagHiddenFromHud;
    mod.events[events::kEventModuleStateChanged] = {&on_module_state_changed, 0};
    mod.events[events::kEventGuiFramePoll] = {&on_gui_frame_poll, 0};
    mod.events[events::kEventGuiRender2D] = {&on_gui_render_2d, -100};
    mod.events[events::kEventProfileAction] = {&on_profile_action, 0};
    mod.events[events::kEventErrorNotification] = {&on_error_notification, 0};
    mod.events[events::kEventFriendChanged] = {&on_friend_changed, 0};
    mod.render_settings = &render_settings;
    mod.visit_config = &visit_config;
    return mod;
}

} // namespace

// Matches sub_180056760
void notifications_reset_welcome_toast() noexcept {
    s_welcome_shown.exchange(0, std::memory_order_acq_rel);
}

events::Module* get_notifications_module() noexcept {
    static events::Module s_module = create_module();
    return &s_module;
}

} // namespace slinky::modules
