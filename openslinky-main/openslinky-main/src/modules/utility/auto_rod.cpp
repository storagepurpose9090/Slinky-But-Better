#include "modules/utility/auto_rod.hpp"
#include "modules/module_interop.hpp"

#include <cmath>

#include "config/settings.hpp"
#include "events/module_manager.hpp"
#include "gui/widgets.hpp"
#include "minecraft/game_helpers.hpp"
#include "minecraft/mc_wrappers.hpp"
#include "util/math.hpp"
#include "util/random.hpp"

namespace slinky::modules {

namespace {

extern events::Module s_module;

constexpr int32_t kItemSnowball = 332;
constexpr int32_t kItemEgg = 344;
constexpr int32_t kItemFishingRod = 346;
constexpr int32_t kMopTypeEntity = 2;

config::BoolSetting s_require_not_mouse_over{
    {},
    "Not within melee range",
    false,
    false,
    {},
};

config::BoolSetting s_allow_throwables{
    {},
    "Allow using snowballs/eggs",
    false,
    false,
    {},
};

config::BoolSetting s_always_switch_to_weapon{
    {},
    "Always switch back to weapon",
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

config::BoolSetting s_randomize_delays{
    {},
    "Randomize delays",
    true,
    false,
    {},
};

config::FloatSetting s_retract_speed{
    {},
    "Retract speed",
    "%.2fx",
    0.01f,
    0.0f,
    0.5f,
    2.0f,
    1.0f,
    0.0f,
};

config::BoolSetting s_dynamic{
    {},
    "Dynamic speed",
    true,
    false,
    {},
};

config::FloatSetting s_throw_speed{
    {},
    "Throw speed",
    "%.1f CPS",
    0.1f,
    0.0f,
    4.0f,
    20.0f,
    5.0f,
    0.0f,
};

int32_t s_saved_slot = -1;
bool s_pending_hold_release = false;
int32_t s_state = 0;
int32_t s_projectile_slot = 0;
float s_tick_delay = 0.0f;
int32_t s_retract_ticks = 0;
bool s_using_fishing_rod = false;
bool s_allow_single_rod_use = false;

random::DriftNoise s_action_delay_drift{};
random::DriftNoise s_action_spike_gate{};
random::DriftNoise s_throw_cps_drift{};
random::DriftNoise s_throw_spike_gate{};

// sub_180065B20
float sample_action_delay() noexcept {
    float delay = s_delay.value;
    if (s_randomize_delays.value) {
        delay = std::fmax(0.0f, delay - s_action_delay_drift.sample_timed({0.0f, 0.5f}, 5.0));
        if (s_action_spike_gate.sample_chance_timed({0.01f, 0.03f}, 30.0)) {
            delay += random::uniform_float(0.5f, 1.0f);
        }
    }
    return delay;
}

// sub_180065BE0
int32_t compute_retract_ticks(events::GameContext* ctx) noexcept {
    jobject target = (ctx->target_tracker != nullptr) ? ctx->target_tracker->target_entity : nullptr;
    float raw_ticks = 0.0f;

    if (target != nullptr && s_dynamic.value) {
        events::JniContext* jni = ctx->jni;
        math::Vec3d eye_pos = mc::entity_get_position(jni, ctx->local_player);
        if (!mc::is_version_1_7_10(jni)) {
            eye_pos = math::vec3_add_eye_height(eye_pos);
        }

        const math::Vec3d target_pos = mc::other_player_mp_get_target_pos(jni, target);
        const math::Vec3d min_pt = math::vec3_sub(target_pos, {0.3, 0.0, 0.3});
        const math::Vec3d max_pt = math::vec3_add(target_pos, {0.3, 1.8, 0.3});
        const math::AABB target_box = math::aabb_from_points(min_pt, max_pt);
        const math::Vec3d closest_pt = math::aabb_closest_point(target_box, eye_pos);
        const double dist = math::vec3_dist(eye_pos, closest_pt);
        raw_ticks = static_cast<float>(dist / static_cast<double>(s_retract_speed.value));
    } else {
        raw_ticks = 10.0f / s_retract_speed.value;
    }

    return static_cast<int32_t>(std::ceil(raw_ticks));
}

// sub_180065DF0
float sample_throwable_interval_ticks() noexcept {
    const float cps = s_throw_speed.value;
    if (!s_randomize_delays.value) {
        return 20.0f / cps;
    }

    float ticks = 20.0f / (cps * s_throw_cps_drift.sample_timed({0.5f, 1.0f}, 1.0));
    if (s_throw_spike_gate.sample_chance_timed({0.01f, 0.03f}, 30.0)) {
        ticks += random::uniform_float(1.0f, 2.0f);
    }
    return ticks;
}

void restore_saved_slot(events::GameContext* ctx) noexcept {
    if (s_saved_slot != -1) {
        jobject inv = mc::player_get_inventory(ctx->jni, ctx->local_player);
        mc::inv_player_set_current_item(ctx->jni, inv, s_saved_slot);
        s_saved_slot = -1;
    }
}

void finish_or_repeat_cycle(events::GameContext* ctx) noexcept {
    if (s_module.hold_mode) {
        s_state = 0;
    } else {
        ctx->module_manager->request_set_enabled(&s_module, false);
    }
}

// sub_1800652C0
void on_game_tick(void* raw_event) noexcept {
    auto* event = static_cast<events::ContextEvent*>(raw_event);
    events::GameContext* ctx = event->ctx;
    if (!ctx->in_game_focus) {
        return;
    }

    events::JniContext* jni = ctx->jni;
    if (s_pending_hold_release) {
        restore_saved_slot(ctx);
        mc::release_hotbar_lock(&s_module);
        ctx->module_manager->request_set_enabled(&s_module, false);
        s_pending_hold_release = false;
        return;
    }

    for (int32_t step = 0; step < 3; ++step) {
        if (s_require_not_mouse_over.value) {
            jobject mop = mc::mc_get_object_mouse_over(jni, ctx->minecraft);
            if (mop != nullptr && mc::mop_get_type_ordinal(jni, mop) == kMopTypeEntity) {
                restore_saved_slot(ctx);
                mc::release_hotbar_lock(&s_module);
                s_state = 6;
                finish_or_repeat_cycle(ctx);
                return;
            }
        }

        if (s_state == 7) {
            restore_saved_slot(ctx);
            mc::release_hotbar_lock(&s_module);
            s_state = 0;
            return;
        }

        if (s_retract_ticks != 0) {
            --s_retract_ticks;
        }

        if (s_tick_delay > 0.0f) {
            s_tick_delay -= 1.0f;
            if (s_tick_delay > 0.0f) {
                break;
            }
        }

        jobject inv = mc::player_get_inventory(jni, ctx->local_player);
        int32_t cur_slot = mc::inv_player_get_current_item(jni, inv);
        if (static_cast<uint32_t>(s_state) >= 2u && cur_slot != s_projectile_slot) {
            mc::release_hotbar_lock(&s_module);
            s_state = 6;
            finish_or_repeat_cycle(ctx);
            return;
        }

        switch (s_state) {
            case 0: {
                s_using_fishing_rod = false;
                int32_t chosen_slot = -1;
                for (int32_t slot = 0; slot < 9; ++slot) {
                    jobject stack = mc::inventory_get_stack_in_slot(jni, inv, slot);
                    if (stack == nullptr) {
                        continue;
                    }
                    jobject item = mc::item_stack_get_item(jni, stack);
                    if (item == nullptr) {
                        continue;
                    }
                    const int32_t item_id = mc::item_get_id(jni, item);
                    if (item_id == kItemSnowball || item_id == kItemEgg) {
                        if (s_allow_throwables.value) {
                            s_using_fishing_rod = false;
                            chosen_slot = slot;
                            break;
                        }
                    } else if (item_id == kItemFishingRod) {
                        s_using_fishing_rod = true;
                        chosen_slot = slot;
                        if (!s_allow_throwables.value) {
                            break;
                        }
                    }
                }

                if (chosen_slot == -1 || !mc::try_acquire_hotbar_lock(&s_module)) {
                    s_state = 6;
                    finish_or_repeat_cycle(ctx);
                    return;
                }

                s_projectile_slot = chosen_slot;
                s_saved_slot = cur_slot;
                if (s_always_switch_to_weapon.value) {
                    const int32_t weapon_slot = weapons_find_hotbar_weapon_slot(ctx, inv);
                    if (weapon_slot != -1) {
                        cur_slot = weapon_slot;
                        s_saved_slot = weapon_slot;
                    }
                }

                s_state = (s_projectile_slot == cur_slot) ? 2 : 1;
                continue;
            }

            case 1: {
                mc::inv_player_set_current_item(jni, inv, s_projectile_slot);
                s_state = 2;
                s_allow_single_rod_use = false;
                s_tick_delay += sample_action_delay();
                continue;
            }

            case 2: {
                s_using_fishing_rod = false;
                int32_t chosen_slot = -1;
                for (int32_t slot = 0; slot < 9; ++slot) {
                    jobject stack = mc::inventory_get_stack_in_slot(jni, inv, slot);
                    if (stack == nullptr) {
                        continue;
                    }
                    jobject item = mc::item_stack_get_item(jni, stack);
                    if (item == nullptr) {
                        continue;
                    }
                    const int32_t item_id = mc::item_get_id(jni, item);
                    if (item_id == kItemSnowball || item_id == kItemEgg) {
                        if (s_allow_throwables.value) {
                            s_using_fishing_rod = false;
                            chosen_slot = slot;
                            break;
                        }
                    } else if (item_id == kItemFishingRod) {
                        s_using_fishing_rod = true;
                        chosen_slot = slot;
                        if (!s_allow_throwables.value) {
                            break;
                        }
                    }
                }

                if (chosen_slot == -1) {
                    s_state = 5;
                    return;
                }

                if (chosen_slot != cur_slot) {
                    s_projectile_slot = chosen_slot;
                    s_state = 1;
                    s_tick_delay += sample_action_delay();
                    continue;
                }

                jobject settings = mc::mc_get_game_settings(jni, ctx->minecraft);
                jobject use_key = mc::settings_get_key_use_item(jni, settings);
                mc::key_binding_set_press_time(jni, use_key, 1);

                float added_delay = 0.0f;
                if (s_using_fishing_rod) {
                    s_state = 3;
                    s_retract_ticks = compute_retract_ticks(ctx);
                    added_delay = sample_action_delay();
                } else if (s_module.hold_mode) {
                    added_delay = sample_throwable_interval_ticks();
                } else {
                    s_state = 5;
                    added_delay = sample_action_delay();
                }
                s_tick_delay += added_delay;
                break;
            }

            case 3: {
                if (s_retract_ticks != 0) {
                    break;
                }
                s_state = (s_saved_slot == s_projectile_slot) ? 4 : 5;
                s_tick_delay += sample_action_delay();
                continue;
            }

            case 4: {
                const int32_t bounce_slot = s_saved_slot - 9 * ((s_saved_slot + 1) / 9) + 1;
                mc::inv_player_set_current_item(jni, inv, bounce_slot);
                s_state = 5;
                return;
            }

            case 5: {
                s_state = 6;
                restore_saved_slot(ctx);
                mc::release_hotbar_lock(&s_module);
                s_tick_delay += sample_action_delay();
                break;
            }

            default:
                break;
        }
        break;
    }

    if (s_state == 6) {
        finish_or_repeat_cycle(ctx);
    }
}

// sub_180065890
void on_pre_module_disable(void* raw_event) noexcept {
    auto* event = static_cast<events::PreModuleDisableEvent*>(raw_event);
    if (!s_pending_hold_release && event->module == &s_module) {
        if (s_module.hold_mode) {
            s_pending_hold_release = true;
        } else {
            if (s_state == 6) {
                return;
            }
            if (s_state == 3) {
                s_state = 7;
            }
        }
        event->cancel_disable = true;
    }
}

// sub_1800658E0
void on_module_state_changed(void* raw_event) noexcept {
    auto* event = static_cast<events::ModuleStateChangedEvent*>(raw_event);
    if (event->module == &s_module && event->enabled) {
        s_state = 0;
    }
}

// sub_180065910
void on_pre_attack_or_use(void* raw_event) noexcept {
    auto* event = static_cast<events::AttackOrUseEntityEvent*>(raw_event);
    if (event->is_attack) {
        return;
    }

    events::GameContext* ctx = event->ctx;
    jobject held_item = mc::player_get_held_item(ctx->jni, ctx->local_player);
    if (held_item != nullptr && mc::item_get_id(ctx->jni, held_item) == kItemFishingRod) {
        if (s_allow_single_rod_use) {
            event->cancelled = true;
        } else {
            s_allow_single_rod_use = true;
        }
    }
}

// sub_180065970
void render_settings(gui::ClickGui* gui, gui::GuiRenderer* renderer) noexcept {
    gui::widgets::render_section_header(gui, renderer, "Main");
    s_retract_speed.render(gui, renderer);
    s_delay.render(gui, renderer);
    s_dynamic.render(gui, renderer);
    s_randomize_delays.render(gui, renderer);
    s_always_switch_to_weapon.render(gui, renderer);

    gui::widgets::render_section_header(gui, renderer, "Throwables");
    s_allow_throwables.render(gui, renderer);
    s_throw_speed.render(gui, renderer);

    gui::widgets::render_section_header(gui, renderer, "Main");
    s_require_not_mouse_over.render(gui, renderer);
}

// sub_180065A50
void visit_config(config::ConfigArchive* archive) noexcept {
    s_retract_speed.visit(archive, "retract_speed");
    s_delay.visit(archive, "delay");
    s_dynamic.visit(archive, "dynamic");
    s_randomize_delays.visit(archive, "randomize_delays");
    s_always_switch_to_weapon.visit(archive, "always_switch_to_weapon");
    s_allow_throwables.visit(archive, "allow_throwables");
    s_throw_speed.visit(archive, "throw_speed");
    s_require_not_mouse_over.visit(archive, "require_not_mouse_over");
}

events::Module make_module() noexcept {
    events::Module mod{};
    mod.name = "Auto Rod";
    mod.description = "Throws your fishing rod for you.";
    mod.category = events::ModuleCategory::Utility;
    mod.enabled = false;
    mod.flags = events::kModuleFlagNoToggleSwitch | events::kModuleFlagHiddenFromHud;
    mod.keybind = 0;
    mod.hold_mode = false;
    mod.events[events::kEventGameTick] = {on_game_tick, 0, 0};
    mod.events[events::kEventPreModuleDisable] = {on_pre_module_disable, 0, 0};
    mod.events[events::kEventModuleStateChanged] = {on_module_state_changed, 0, 0};
    mod.events[events::kEventPreAttackOrUseEntity] = {on_pre_attack_or_use, 0, 0};
    mod.render_settings = render_settings;
    mod.visit_config = visit_config;
    return mod;
}

events::Module s_module = make_module();

} // namespace

events::Module* get_auto_rod_module() noexcept {
    return &s_module;
}

} // namespace slinky::modules
