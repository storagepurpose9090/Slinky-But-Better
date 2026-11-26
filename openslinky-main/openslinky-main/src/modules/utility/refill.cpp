#include "modules/utility/refill.hpp"
#include "modules/module_interop.hpp"

#include <cmath>

#include "config/settings.hpp"
#include "events/module_manager.hpp"
#include "gui/widgets.hpp"
#include "minecraft/mc_wrappers.hpp"
#include "util/random.hpp"

namespace slinky::modules {

namespace {

extern events::Module s_module;

constexpr int32_t kItemMushroomStew = 282;
constexpr int32_t kItemPotion = 373;
constexpr uint16_t kSplashPotionBit = 0x4000u;
constexpr uint16_t kPotionInstantHealthEffect = 5u;

constexpr const char* kSimulateButtonOptions[3] = {
    "Left",
    "Right",
    "Left + right",
};

config::FloatSetting s_speed{
    {},
    "Speed",
    "%.1fx",
    0.0f,
    0.0f,
    1.0f,
    10.0f,
    2.0f,
    0.0f,
};

config::FloatSetting s_delay_before_close{
    {},
    "Delay before close",
    "%.0f ms",
    0.0f,
    50.0f,
    0.0f,
    5.0f,
    1.0f,
    0.0f,
};

config::FloatSetting s_delay_after_open{
    {},
    "Delay after open",
    "%.0f ms",
    0.0f,
    50.0f,
    0.0f,
    10.0f,
    2.0f,
    0.0f,
};

config::BoolSetting s_smart_speed{
    {},
    "Smart speed",
    true,
    false,
    {},
};

config::BoolSetting s_allow_misclick{
    {},
    "Allow misclicking",
    true,
    false,
    {},
};

config::BoolSetting s_refill_soup{
    {},
    "Soups",
    true,
    false,
    {},
};

config::BoolSetting s_refill_potion{
    {},
    "Potions",
    true,
    false,
    {},
};

config::BoolSetting s_refill_potion_non_health{
    {},
    "Allow non-health potions",
    false,
    false,
    {},
};

config::ModeSetting s_simulate_button{
    {},
    "Simulate button",
    3,
    0,
    kSimulateButtonOptions,
    1,
    0,
    false,
    {},
};

int32_t s_state = 0;
ContainerSlotHumanizer s_humanizer{};
random::DriftNoise s_open_delay_drift{};
random::DriftNoise s_close_delay_drift{};
int32_t s_pending_slot = 0;
float s_tick_delay = 0.0f;
int32_t s_alternating_button = 0;
random::DriftNoise s_button_toggle_gate{};

// sub_18005A480
void close_inventory_and_disable(events::GameContext* ctx) noexcept {
    ctx->module_manager->request_set_enabled(&s_module, false);
    jobject screen = mc::mc_get_gui_container_screen(ctx->jni, ctx->minecraft);
    if (screen != nullptr) {
        mc::gui_screen_key_typed(ctx->jni, screen, 0, 1);
    }
}

// sub_18005A4E0
double sample_window_delay(random::DriftNoise* drift, double base_delay) noexcept {
    const double jitter = static_cast<double>(drift->sample_timed({0.0f, 2.0f}, 30.0));
    return std::fmax(base_delay - jitter, 0.0);
}

bool hotbar_has_empty_slot(events::JniContext* jni, jobject player) noexcept {
    if (player == nullptr) {
        return false;
    }
    jobject inv = mc::player_get_inventory(jni, player);
    for (int32_t slot = 0; slot < 9; ++slot) {
        jobject stack = mc::inventory_get_stack_in_slot(jni, inv, slot);
        if (stack == nullptr) {
            return true;
        }
        jobject item = mc::item_stack_get_item(jni, stack);
        if (item == nullptr || mc::item_get_id(jni, item) == 0) {
            return true;
        }
    }
    return false;
}

// sub_180059C40
void on_module_state_changed(void* raw_event) noexcept {
    auto* event = static_cast<events::ModuleStateChangedEvent*>(raw_event);
    if (event->module == &s_module && event->enabled) {
        s_state = 0;
    }
}

// sub_180059C70
void on_gui_screen_tick(void* raw_event) noexcept {
    auto* event = static_cast<events::GuiScreenTickEvent*>(raw_event);
    events::GameContext* ctx = event->ctx;
    events::JniContext* jni = ctx->jni;
    jobject gui_screen = mc::mc_get_gui_container_screen(jni, ctx->minecraft);

    if (s_state == 0) {
        if (!hotbar_has_empty_slot(jni, ctx->local_player)) {
            s_state = 4;
            return;
        }

        s_state = 1;
        s_humanizer.speed_cps = s_speed.value;
        s_humanizer.distance_based_delay = s_smart_speed.value;
        s_humanizer.allow_misclicks = s_allow_misclick.value;

        jobject inv = mc::player_get_inventory(jni, ctx->local_player);
        uint64_t candidate_mask = 0;
        for (uint32_t idx = 0; idx < 27u; ++idx) {
            jobject stack = mc::inventory_get_stack_in_slot(jni, inv, static_cast<int32_t>(idx + 9u));
            if (stack == nullptr) {
                continue;
            }
            jobject item = mc::item_stack_get_item(jni, stack);
            if (item == nullptr) {
                continue;
            }
            const int32_t item_id = mc::item_get_id(jni, item);
            if (item_id == kItemMushroomStew) {
                if (s_refill_soup.value) {
                    candidate_mask |= (1ULL << idx);
                }
            } else if (item_id == kItemPotion && s_refill_potion.value) {
                const uint16_t meta = static_cast<uint16_t>(mc::item_stack_get_damage(jni, stack));
                if ((meta & kSplashPotionBit) != 0 &&
                    ((meta & 0x0Fu) == kPotionInstantHealthEffect || s_refill_potion_non_health.value)) {
                    candidate_mask |= (1ULL << idx);
                }
            }
        }

        if (candidate_mask == 0ULL) {
            close_inventory_and_disable(ctx);
            return;
        }

        s_humanizer.reset(candidate_mask, 27ULL);
        if (gui_screen == nullptr) {
            jobject settings = mc::mc_get_game_settings(jni, ctx->minecraft);
            jobject inv_key = mc::settings_get_key_inventory(jni, settings);
            mc::key_binding_increment_press_time(jni, inv_key);
        }
        return;
    }

    if (gui_screen == nullptr || !mc::is_gui_inventory(jni, gui_screen)) {
        close_inventory_and_disable(ctx);
        return;
    }

    event->forward_movement_keys = true;
    if (s_tick_delay > 0.0f) {
        s_tick_delay -= 1.0f;
        return;
    }

    int32_t last_button = 0;
    while (true) {
        if (s_state == 4) {
            close_inventory_and_disable(ctx);
            return;
        }

        if (!hotbar_has_empty_slot(jni, ctx->local_player)) {
            const double close_wait =
                static_cast<double>(s_tick_delay) +
                sample_window_delay(&s_close_delay_drift, static_cast<double>(s_delay_before_close.value));
            s_tick_delay = static_cast<float>(close_wait);
            s_state = 4;
            if (close_wait <= 7.006492321624085e-46) {
                close_inventory_and_disable(ctx);
            }
            return;
        }

        switch (s_state) {
            case 1: {
                s_state = 2;
                const double open_wait =
                    static_cast<double>(s_tick_delay) +
                    sample_window_delay(&s_open_delay_drift, static_cast<double>(s_delay_after_open.value));
                s_tick_delay = static_cast<float>(open_wait);
                if (s_tick_delay > 0.0f) {
                    return;
                }
                break;
            }

            case 2: {
                s_pending_slot = s_humanizer.next_slot(&s_tick_delay);
                if (s_pending_slot != -1) {
                    s_state = 3;
                    if (s_tick_delay > 0.0f) {
                        return;
                    }
                    break;
                }
                s_tick_delay += static_cast<float>(
                    sample_window_delay(&s_close_delay_drift, static_cast<double>(s_delay_before_close.value)));
                s_state = 4;
                return;
            }

            case 3: {
                int32_t btn = s_simulate_button.value;
                if (static_cast<uint32_t>(btn) >= 2u) {
                    if (btn == 2) {
                        btn = s_alternating_button;
                        if (s_button_toggle_gate.sample_chance_timed({0.9f, 1.0f}, 10.0)) {
                            s_alternating_button = (s_alternating_button == 0) ? 1 : 0;
                        }
                    } else {
                        btn = last_button;
                    }
                }
                mc::gui_container_handle_mouse_click(jni, gui_screen, s_pending_slot + 9, btn, 1);
                last_button = btn;
                s_state = 2;
                if (s_tick_delay > 0.0f) {
                    return;
                }
                break;
            }

            default:
                close_inventory_and_disable(ctx);
                return;
        }
    }
}

// sub_18005A2B0
void render_settings(gui::ClickGui* gui, gui::GuiRenderer* renderer) noexcept {
    gui::widgets::render_section_header(gui, renderer, "Items");
    s_refill_potion.render(gui, renderer);
    if (s_refill_potion.value) {
        s_refill_potion_non_health.render(gui, renderer);
    }
    s_refill_soup.render(gui, renderer);

    gui::widgets::render_section_header(gui, renderer, "Clicks");
    s_simulate_button.render(gui, renderer);
    s_delay_after_open.render(gui, renderer);
    s_delay_before_close.render(gui, renderer);
    s_speed.render(gui, renderer);
    s_smart_speed.render(gui, renderer);
    s_allow_misclick.render(gui, renderer);
}

// sub_18005A3A0
void visit_config(config::ConfigArchive* archive) noexcept {
    s_refill_potion.visit(archive, "refill_potion");
    s_refill_potion_non_health.visit(archive, "refill_potion_non_health");
    s_refill_soup.visit(archive, "refill_soup");
    s_simulate_button.visit(archive, "simulate_button");
    s_delay_after_open.visit(archive, "delay_after_open");
    s_delay_before_close.visit(archive, "delay_before_close");
    s_speed.visit(archive, "speed");
    s_smart_speed.visit(archive, "smart_speed");
    s_allow_misclick.visit(archive, "allow_misclick");
}

events::Module make_module() noexcept {
    events::Module mod{};
    mod.name = "Refill";
    mod.description = "Automatically moves items into your hotbar.";
    mod.category = events::ModuleCategory::Utility;
    mod.enabled = false;
    mod.flags = events::kModuleFlagNoToggleSwitch | events::kModuleFlagHiddenFromHud | events::kModuleFlagNoHoldMode;
    mod.keybind = 0;
    mod.hold_mode = false;
    mod.events[events::kEventModuleStateChanged] = {on_module_state_changed, 0, 0};
    mod.events[events::kEventGuiScreenTick] = {on_gui_screen_tick, 0, 0};
    mod.render_settings = render_settings;
    mod.visit_config = visit_config;
    return mod;
}

events::Module s_module = make_module();

} // namespace

events::Module* get_refill_module() noexcept {
    return &s_module;
}

} // namespace slinky::modules
