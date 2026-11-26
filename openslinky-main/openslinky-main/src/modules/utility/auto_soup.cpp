#include "modules/utility/auto_soup.hpp"

#include "config/settings.hpp"
#include "events/module_manager.hpp"
#include "gui/widgets.hpp"
#include "minecraft/game_helpers.hpp"
#include "minecraft/item_database.hpp"
#include "minecraft/mc_wrappers.hpp"
#include "util/containers.hpp"
#include "util/random.hpp"

namespace slinky::modules {

namespace {

extern events::Module s_module;

constexpr int32_t kItemMushroomStew = 282;
constexpr int32_t kItemRabbitStew = 413;

util::PodVector<mc::ItemIdEntry> s_allowed_items{};

void allowed_items_get_options(const char* const** out_items, size_t* out_count) noexcept {
    mc::get_all_item_names(out_items, out_count);
}

config::FloatSetting s_single_soup_max_hp{
    {},
    "Single use",
    "%.1f\xE2\x99\xA5",
    1.0f,
    0.5f,
    0.0f,
    20.0f,
    13.0f,
    0.0f,
};

config::FloatSetting s_delay_before{
    {},
    "Before switching slots",
    "%.0f ms",
    0.1f,
    50.0f,
    0.0f,
    10.0f,
    2.0f,
    0.0f,
};

config::FloatSetting s_double_soup_max_hp{
    {},
    "Double use",
    "%.1f\xE2\x99\xA5",
    1.0f,
    0.5f,
    0.0f,
    20.0f,
    6.0f,
    0.0f,
};

config::FloatSetting s_delay_between{
    {},
    "Between actions",
    "%.0f ms",
    0.1f,
    50.0f,
    0.0f,
    2.0f,
    0.5f,
    0.0f,
};

config::FloatSetting s_delay_after{
    {},
    "Cooldown",
    "%.0f ms",
    0.0f,
    50.0f,
    5.0f,
    20.0f,
    10.0f,
    0.0f,
};

config::FloatSetting s_randomization{
    {},
    "Randomization",
    "%.0f%%",
    0.0f,
    100.0f,
    0.0f,
    1.0f,
    0.5f,
    0.0f,
};

config::BoolSetting s_override_item{
    {},
    "Override item type",
    false,
    false,
    {},
};

config::BoolSetting s_drop_bowls{
    {},
    "Drop bowls",
    false,
    false,
    {},
};

config::DynamicListSetting s_allowed_items_setting{
    "Items",
    "Search item",
    mc::validate_item_name_input,
    config::DynamicListItemKind::StringOnly,
    0,
    &s_allowed_items,
    mc::item_filter_list_count,
    mc::item_filter_list_get_name,
    nullptr,
    mc::item_filter_list_add,
    mc::item_filter_list_remove,
    mc::item_filter_list_clear,
    allowed_items_get_options,
    false,
    {},
};

bool s_pending_disable = false;
int32_t s_state = 0;
bool s_is_second_soup = false;
int32_t s_soup_slot = 0;
int32_t s_saved_slot = 0;
bool s_needs_double_soup = false;
bool s_selected_is_stew = false;
float s_tick_delay = 0.0f;

random::DriftNoise s_delay_drift{};
random::DriftNoise s_delay_spike_gate{};
random::DriftNoise s_skip_first_slot_gate{};

// sub_18004F3B0
float sample_randomized_delay(float base_ticks) noexcept {
    const float slow_drift = s_delay_drift.sample_timed({0.0f, 0.5f}, 60.0);
    const float fast_drift = s_delay_drift.sample_timed({0.0f, 0.5f}, 5.0);
    float jitter = fast_drift + slow_drift;
    if (s_delay_spike_gate.sample_chance_timed({0.03f, 0.06f}, 60.0)) {
        jitter += random::uniform_float(0.5f, 2.5f);
    }
    return jitter * s_randomization.value + base_ticks;
}

// sub_18004EB60
void on_game_tick(void* raw_event) noexcept {
    auto* event = static_cast<events::ContextEvent*>(raw_event);
    events::GameContext* ctx = event->ctx;

    if (s_pending_disable && (s_state == 6 || s_state == 0)) {
        ctx->module_manager->request_set_enabled(&s_module, false);
        return;
    }

    if (!ctx->in_game_focus) {
        s_is_second_soup = false;
        s_state = 0;
        return;
    }

    events::JniContext* jni = ctx->jni;
    for (int32_t step = 0; step < 3; ++step) {
        if (s_tick_delay > 0.0f) {
            s_tick_delay -= 1.0f;
            return;
        }

        switch (s_state) {
            case 0: {
                const float health = mc::living_get_health(jni, ctx->local_player);
                if (health > s_single_soup_max_hp.value) {
                    return;
                }
                s_tick_delay += sample_randomized_delay(s_delay_before.value);
                s_needs_double_soup = (s_double_soup_max_hp.value >= health);
                s_state = 1;
                break;
            }

            case 1: {
                jobject inv = mc::player_get_inventory(jni, ctx->local_player);
                const bool skip_current = s_is_second_soup;
                const int32_t cur_slot = mc::inv_player_get_current_item(jni, inv);
                jobjectArray main_inv = mc::inv_player_get_main_inventory(jni, inv);

                int32_t chosen_slot = -1;
                for (int32_t offset = 0; offset < 9; ++offset) {
                    const int32_t slot = (cur_slot + offset) % 9;
                    if (skip_current && slot == cur_slot) {
                        continue;
                    }

                    jobject stack = jni->env->GetObjectArrayElement(main_inv, static_cast<jsize>(slot));
                    int32_t item_id = 0;
                    if (stack != nullptr) {
                        jobject item = mc::item_stack_get_item(jni, stack);
                        item_id = mc::item_get_id(jni, item);
                    }

                    bool allowed = false;
                    if (s_override_item.value) {
                        allowed = mc::item_filter_list_contains_id(&s_allowed_items, item_id);
                    } else {
                        allowed = (item_id == kItemRabbitStew || item_id == kItemMushroomStew);
                    }
                    if (!allowed) {
                        continue;
                    }

                    s_selected_is_stew = (item_id == kItemMushroomStew || item_id == kItemRabbitStew);
                    chosen_slot = slot;
                    if (!s_skip_first_slot_gate.sample_chance_timed({0.01f, 0.08f}, 30.0)) {
                        break;
                    }
                }

                if (chosen_slot == -1) {
                    s_state = 6 - static_cast<int32_t>(s_is_second_soup);
                    s_is_second_soup = false;
                    return;
                }

                s_soup_slot = chosen_slot;
                int32_t orig_slot = s_saved_slot;
                if (!s_is_second_soup) {
                    orig_slot = mc::inv_player_get_current_item(jni, inv);
                    s_saved_slot = orig_slot;
                }
                s_is_second_soup = false;
                s_state = (chosen_slot == orig_slot) ? 3 : 2;
                break;
            }

            case 2: {
                if (!mc::try_acquire_hotbar_lock(&s_module)) {
                    s_state = 0;
                    return;
                }
                jobject inv = mc::player_get_inventory(jni, ctx->local_player);
                mc::inv_player_set_current_item(jni, inv, s_soup_slot);
                s_state = 3;
                s_tick_delay += sample_randomized_delay(s_delay_between.value);
                if (mc::player_is_using_item(jni, ctx->local_player) && s_tick_delay <= 0.0f) {
                    s_tick_delay += 1.0f;
                }
                break;
            }

            case 3: {
                jobject settings = mc::mc_get_game_settings(jni, ctx->minecraft);
                jobject use_key = mc::settings_get_key_use_item(jni, settings);
                mc::key_binding_increment_press_time(jni, use_key);

                if (s_drop_bowls.value && s_selected_is_stew) {
                    s_state = 4;
                    s_tick_delay += sample_randomized_delay(s_delay_between.value);
                    if (s_tick_delay <= 0.0f) {
                        s_tick_delay += 1.0f;
                    }
                } else {
                    s_state = 5;
                    if (s_soup_slot != s_saved_slot) {
                        s_tick_delay += sample_randomized_delay(s_delay_between.value);
                        if (s_tick_delay <= 0.0f) {
                            s_tick_delay += 1.0f;
                        }
                    }
                }
                break;
            }

            case 4: {
                jobject settings = mc::mc_get_game_settings(jni, ctx->minecraft);
                jobject drop_key = mc::settings_get_key_drop(jni, settings);
                mc::key_binding_increment_press_time(jni, drop_key);
                s_state = 5;
                if (s_soup_slot != s_saved_slot) {
                    s_tick_delay += sample_randomized_delay(s_delay_between.value);
                    if (s_tick_delay <= 0.0f) {
                        s_tick_delay += 1.0f;
                    }
                }
                break;
            }

            case 5: {
                mc::release_hotbar_lock(&s_module);
                if (s_needs_double_soup) {
                    s_needs_double_soup = false;
                    s_is_second_soup = true;
                    s_state = 1;
                } else {
                    jobject inv = mc::player_get_inventory(jni, ctx->local_player);
                    mc::inv_player_set_current_item(jni, inv, s_saved_slot);
                    s_state = 6;
                }
                break;
            }

            case 6: {
                s_is_second_soup = false;
                s_tick_delay += sample_randomized_delay(s_delay_after.value);
                s_state = 0;
                break;
            }

            default:
                return;
        }
    }
}

// sub_18004F140
void on_pre_module_disable(void* raw_event) noexcept {
    auto* event = static_cast<events::PreModuleDisableEvent*>(raw_event);
    if (event->module == &s_module && s_state != 0 && s_state != 6) {
        event->cancel_disable = true;
        s_pending_disable = true;
    }
}

// sub_18004F170
void on_module_state_changed(void* raw_event) noexcept {
    auto* event = static_cast<events::ModuleStateChangedEvent*>(raw_event);
    if (event->module == &s_module && event->enabled) {
        s_state = 0;
        s_is_second_soup = false;
        s_tick_delay = 0.0f;
        s_pending_disable = false;
    }
}

// sub_18004F1B0
void render_settings(gui::ClickGui* gui, gui::GuiRenderer* renderer) noexcept {
    gui::widgets::render_section_header(gui, renderer, "Maximum health");
    s_single_soup_max_hp.render(gui, renderer);
    if (s_double_soup_max_hp.value > s_single_soup_max_hp.value) {
        s_double_soup_max_hp.value = s_single_soup_max_hp.value;
    }
    s_double_soup_max_hp.render(gui, renderer);

    gui::widgets::render_section_header(gui, renderer, "Delays");
    s_randomization.render(gui, renderer);
    s_delay_before.render(gui, renderer);
    s_delay_between.render(gui, renderer);
    s_delay_after.render(gui, renderer);

    gui::widgets::render_section_header(gui, renderer, "Extra");
    s_drop_bowls.render(gui, renderer);
    s_override_item.render(gui, renderer);
    if (s_override_item.value) {
        s_allowed_items_setting.render(gui, renderer);
    }
}

// sub_18004F2D0
void visit_config(config::ConfigArchive* archive) noexcept {
    s_single_soup_max_hp.visit(archive, "single_soup_max_hp");
    s_double_soup_max_hp.visit(archive, "double_soup_max_hp");
    s_randomization.visit(archive, "randomization");
    s_delay_before.visit(archive, "delay_before");
    s_delay_between.visit(archive, "delay_between");
    s_delay_after.visit(archive, "delay_after");
    s_drop_bowls.visit(archive, "drop_bowls");
    s_override_item.visit(archive, "override_item");
    s_allowed_items_setting.visit(archive, "allowed_items");
}

events::Module make_module() noexcept {
    events::Module mod{};
    mod.name = "Auto Soup";
    mod.description = "Uses soups for you.";
    mod.category = events::ModuleCategory::Utility;
    mod.enabled = false;
    mod.flags = events::kModuleFlagNone;
    mod.keybind = 0;
    mod.hold_mode = false;
    mod.events[events::kEventGameTick] = {on_game_tick, -100, 0};
    mod.events[events::kEventPreModuleDisable] = {on_pre_module_disable, 0, 0};
    mod.events[events::kEventModuleStateChanged] = {on_module_state_changed, 0, 0};
    mod.render_settings = render_settings;
    mod.visit_config = visit_config;
    return mod;
}

events::Module s_module = make_module();

} // namespace

events::Module* get_auto_soup_module() noexcept {
    return &s_module;
}

} // namespace slinky::modules
