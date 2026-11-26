#include "modules/utility/auto_pot.hpp"

#include <cmath>

#include "config/settings.hpp"
#include "events/module_manager.hpp"
#include "gui/widgets.hpp"
#include "minecraft/game_helpers.hpp"
#include "minecraft/mc_wrappers.hpp"
#include "util/random.hpp"

namespace slinky::modules {

namespace {

extern events::Module s_module;

constexpr int32_t kItemPotion = 373;
constexpr uint16_t kSplashPotionBit = 0x4000u;
constexpr uint16_t kPotionInstantHealthEffect = 5u;

config::BoolSetting s_use_non_health{
    {},
    "Use non-health potions",
    false,
    false,
    {},
};

config::FloatSetting s_delay{
    {},
    "Delay between actions",
    "%.0f ms",
    0.1f,
    50.0f,
    0.0f,
    2.0f,
    0.5f,
    0.0f,
};

config::BoolSetting s_prevent_waste{
    {},
    "Prevent wasting health",
    true,
    false,
    {},
};

config::BoolSetting s_double_splash{
    {},
    "Automatically do multiple splashes",
    false,
    false,
    {},
};

int32_t s_state = 0;
int32_t s_pot_slot = 0;
int32_t s_saved_slot = 0;
bool s_is_chained_splash = false;
float s_tick_delay = 0.0f;
int32_t s_queued_splashes = 0;
random::DriftNoise s_delay_drift{};
random::DriftNoise s_delay_spike_gate{};
int32_t s_extra_press_budget = 0;

// sub_18005AA80
float sample_action_delay() noexcept {
    float delay = std::fmax(0.0f, s_delay.value - s_delay_drift.sample_timed({0.0f, 0.5f}, 5.0));
    if (s_delay_spike_gate.sample_chance_timed({0.01f, 0.03f}, 30.0)) {
        delay += random::uniform_float(0.5f, 1.0f);
    }
    return delay;
}

// sub_18005A540
void on_game_tick(void* raw_event) noexcept {
    auto* event = static_cast<events::ContextEvent*>(raw_event);
    events::GameContext* ctx = event->ctx;
    events::JniContext* jni = ctx->jni;

    for (int32_t step = 0; step < 4; ++step) {
        if (s_tick_delay > 0.0f) {
            s_tick_delay -= 1.0f;
            break;
        }

        if (!ctx->in_game_focus) {
            s_is_chained_splash = false;
            s_state = 4;
            ctx->module_manager->request_set_enabled(&s_module, false);
            return;
        }

        switch (s_state) {
            case 0: {
                jobject inv = mc::player_get_inventory(jni, ctx->local_player);
                int32_t found_slot = -1;
                for (int32_t slot = 0; slot < 9; ++slot) {
                    jobject stack = mc::inventory_get_stack_in_slot(jni, inv, slot);
                    if (stack == nullptr) {
                        continue;
                    }
                    jobject item = mc::item_stack_get_item(jni, stack);
                    if (item == nullptr || mc::item_get_id(jni, item) != kItemPotion) {
                        continue;
                    }
                    const uint16_t meta = static_cast<uint16_t>(mc::item_stack_get_damage(jni, stack));
                    if ((meta & kSplashPotionBit) != 0 &&
                        ((meta & 0x0Fu) == kPotionInstantHealthEffect || s_use_non_health.value)) {
                        found_slot = slot;
                        break;
                    }
                }

                if (found_slot == -1) {
                    s_queued_splashes = 0;
                    s_state = 4 - static_cast<int32_t>(s_is_chained_splash);
                    s_is_chained_splash = false;
                    if (s_state == 4) {
                        ctx->module_manager->request_set_enabled(&s_module, false);
                    }
                    return;
                }

                s_pot_slot = found_slot;
                int32_t orig_slot = s_saved_slot;
                if (!s_is_chained_splash) {
                    orig_slot = mc::inv_player_get_current_item(jni, inv);
                    s_saved_slot = orig_slot;
                }
                s_is_chained_splash = false;
                s_state = (found_slot == orig_slot) ? 2 : 1;
                break;
            }

            case 1: {
                if (!mc::try_acquire_hotbar_lock(&s_module)) {
                    s_state = 4;
                    ctx->module_manager->request_set_enabled(&s_module, false);
                    return;
                }
                jobject inv = mc::player_get_inventory(jni, ctx->local_player);
                mc::inv_player_set_current_item(jni, inv, s_pot_slot);
                s_state = 2;
                s_tick_delay += sample_action_delay();
                break;
            }

            case 2: {
                if (mc::player_is_using_item(jni, ctx->local_player)) {
                    goto check_done;
                }
                jobject settings = mc::mc_get_game_settings(jni, ctx->minecraft);
                jobject use_key = mc::settings_get_key_use_item(jni, settings);
                mc::key_binding_increment_press_time(jni, use_key);
                --s_queued_splashes;
                s_state = 3;
                if (s_pot_slot != s_saved_slot) {
                    s_tick_delay += sample_action_delay() + 1.0f;
                }
                break;
            }

            case 3: {
                if (s_queued_splashes <= 0) {
                    jobject inv = mc::player_get_inventory(jni, ctx->local_player);
                    mc::inv_player_set_current_item(jni, inv, s_saved_slot);
                    s_state = 4;
                } else {
                    s_is_chained_splash = true;
                    s_state = 0;
                }
                break;
            }

            case 4: {
                mc::release_hotbar_lock(&s_module);
                goto check_done;
            }

            default:
                goto check_done;
        }
    }

check_done:
    if (s_state == 4) {
        ctx->module_manager->request_set_enabled(&s_module, false);
    }
}

// sub_18005A860
void on_pre_module_disable(void* raw_event) noexcept {
    auto* event = static_cast<events::PreModuleDisableEvent*>(raw_event);
    if (event->module == &s_module && s_state != 4 && s_extra_press_budget > 0) {
        event->cancel_disable = true;
        ++s_queued_splashes;
        --s_extra_press_budget;
    }
}

// sub_18005A8B0
void on_module_state_changed(void* raw_event) noexcept {
    auto* event = static_cast<events::ModuleStateChangedEvent*>(raw_event);
    if (event->module != &s_module) {
        return;
    }

    if (event->enabled) {
        events::GameContext* ctx = event->ctx;
        if (ctx->in_game_focus) {
            events::JniContext* jni = ctx->jni;
            const float cur_hp = mc::living_get_health(jni, ctx->local_player);
            const float max_hp = mc::living_get_max_health(jni, ctx->local_player);
            int32_t needed = static_cast<int32_t>(((max_hp - cur_hp) + 0.5f) * 0.125f);
            if (needed != 0 || (needed = 1, !s_prevent_waste.value)) {
                if (s_double_splash.value) {
                    s_queued_splashes = needed;
                    s_extra_press_budget = 0;
                } else {
                    s_queued_splashes = 1;
                    s_extra_press_budget = s_prevent_waste.value ? (needed - 1) : 5;
                }
            } else {
                s_state = 4;
            }
        }
    } else {
        s_state = 0;
        s_queued_splashes = 0;
    }
}

// sub_18005A9A0
void render_settings(gui::ClickGui* gui, gui::GuiRenderer* renderer) noexcept {
    gui::widgets::render_section_header(gui, renderer, "Main");
    s_delay.render(gui, renderer);
    s_prevent_waste.render(gui, renderer);
    s_double_splash.render(gui, renderer);
    s_use_non_health.render(gui, renderer);
}

// sub_18005AA10
void visit_config(config::ConfigArchive* archive) noexcept {
    s_delay.visit(archive, "delay");
    s_prevent_waste.visit(archive, "prevent_waste");
    s_double_splash.visit(archive, "double_splash");
    s_use_non_health.visit(archive, "use_non_health");
}

events::Module make_module() noexcept {
    events::Module mod{};
    mod.name = "Auto Pot";
    mod.description = "Uses healing potions for you.";
    mod.category = events::ModuleCategory::Utility;
    mod.enabled = false;
    mod.flags = events::kModuleFlagNoToggleSwitch | events::kModuleFlagHiddenFromHud | events::kModuleFlagNoHoldMode;
    mod.keybind = 0;
    mod.hold_mode = false;
    mod.events[events::kEventGameTick] = {on_game_tick, 0, 0};
    mod.events[events::kEventPreModuleDisable] = {on_pre_module_disable, 0, 0};
    mod.events[events::kEventModuleStateChanged] = {on_module_state_changed, 0, 0};
    mod.render_settings = render_settings;
    mod.visit_config = visit_config;
    return mod;
}

events::Module s_module = make_module();

} // namespace

events::Module* get_auto_pot_module() noexcept {
    return &s_module;
}

} // namespace slinky::modules
