#include "modules/combat/auto_clicker.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>

#include "config/settings.hpp"
#include "gui/widgets.hpp"
#include "minecraft/mc_wrappers.hpp"
#include "modules/module_interop.hpp"
#include "util/input_win32.hpp"
#include "util/keycodes.hpp"
#include "util/math.hpp"
#include "util/random.hpp"

namespace slinky::modules {

namespace {

constexpr const char* kPatternOptions[] = {
    "Jitter",
    "Butterfly",
};

int32_t s_last_hovered_slot = -1;
math::Vec2f s_last_tick_rot{};
bool s_attack_bound_to_rmb = false;
double s_click_timer_sec = 0.0;
double s_last_press_duration_sec = 0.0;
random::DriftNoise s_inv_spike_gate{};
bool s_inv_lmb_armed = false;
random::DriftNoise s_inv_empty_skip_gate{};
int32_t s_mop_type = 0;
random::DriftNoise s_butterfly_gate{};
random::DriftNoise s_sigma_drift{};
random::NormalGenerator s_cps_gauss{};
bool s_butterfly_double_next = false;
random::DriftNoise s_slow_drift{};
random::DriftNoise s_med_drift{};
random::DriftNoise s_fast_drift{};
random::DriftNoise s_exhaust_trigger_gate{};
random::DriftNoise s_exhaust_len_drift{};
random::NormalGenerator s_exhaust_gauss{};
int32_t s_exhaust_remaining_clicks = 0;

void on_game_tick(void* event) noexcept;
void on_frame_update(void* event) noexcept;
void on_mid_living_update(void* event) noexcept;
void render_settings(gui::ClickGui* gui, gui::GuiRenderer* renderer) noexcept;
void visit_config(config::ConfigArchive* ar) noexcept;
int format_hud_tag(char* out_buf) noexcept;

struct AutoClickerModule {
    events::Module base{};
    config::BoolSetting allow_inventory{{}, "Click in inventory", false, false};
    config::BoolSetting inventory_randomize{{}, "Randomize speed", true, true};
    config::BoolSetting allow_block_break{{}, "Allow breaking blocks", false, false};
    config::BoolSetting require_weapon{{}, "Holding weapon", false, false};
    config::BoolSetting require_not_using_item{{}, "Not using item", false, false};
    config::FloatSetting target_cps{{}, "Target CPS", "%0.1f CPS", 0.1f, 0.0f, 8.0f, 22.0f, 12.0f, 12.0f};
    config::BoolSetting randomize_cps{{}, "Randomize", true, true};
    config::ModeSetting pattern{{}, "Click pattern", 2, 0, kPatternOptions, 0, 0, false};
    config::BoolSetting exhaust{{}, "Simulate exhaust", true, true};

    AutoClickerModule() noexcept {
        base.name = "Auto Clicker";
        base.description = "Clicks for you when holding down left-click.";
        base.category = events::ModuleCategory::Combat;
        base.enabled = false;
        base.flags = events::kModuleFlagNone;
        base.keybind = 0;
        base.hold_mode = false;
        base.events[events::kEventGameTick] = {&on_game_tick, 0};
        base.events[events::kEventFrameUpdate] = {&on_frame_update, 0};
        base.events[events::kEventMidLivingUpdate] = {&on_mid_living_update, 0};
        base.render_settings = &render_settings;
        base.visit_config = &visit_config;
        base.format_hud_tag = &format_hud_tag;
    }
};

AutoClickerModule s_mod{};

// sub_18004EA00
bool simulate_exhaust(float* inout_delay_sec, float base_chance) noexcept {
    if (s_exhaust_remaining_clicks <= 0) {
        if (!s_mod.exhaust.value) {
            return false;
        }
        const double bc = static_cast<double>(base_chance);
        if (!s_exhaust_trigger_gate.sample_chance_timed(
                {static_cast<float>(bc + 0.02), static_cast<float>(bc + 0.04)},
                300.0)) {
            return false;
        }

        const float roll = random::uniform_range({0.0f, 1.0f});
        int32_t count = 3;
        if (s_exhaust_len_drift.sample_timed({0.03f, 0.06f}, 120.0) <= roll) {
            count = (s_exhaust_len_drift.sample_timed({0.12f, 0.15f}, 120.0) > roll) ? 2 : 1;
        }
        s_exhaust_remaining_clicks = count;
    }

    --s_exhaust_remaining_clicks;
    *inout_delay_sec = static_cast<float>(
        static_cast<double>(*inout_delay_sec) +
        static_cast<double>(s_exhaust_gauss.next_standard_normal()) * 0.01 +
        0.02);
    return true;
}

// sub_18004E050
void on_game_tick(void* event) noexcept {
    auto* ev = static_cast<events::ContextEvent*>(event);
    events::GameContext* ctx = ev->ctx;
    jobject settings = mc::mc_get_game_settings(ctx->jni, ctx->minecraft);
    jobject key_attack = mc::settings_get_key_attack(ctx->jni, settings);
    s_attack_bound_to_rmb = (mc::key_binding_get_key_code(ctx->jni, key_attack) == -99);

    if (ctx->in_game_focus) {
        s_last_tick_rot = mc::entity_get_rotation(ctx->jni, ctx->local_player);
        events::Module* no_hit_delay = get_no_hit_delay_module();
        if (no_hit_delay == nullptr || !no_hit_delay->enabled) {
            mc::mc_set_left_click_counter(ctx->jni, ctx->minecraft, 0);
        }
    }
}

// sub_18004E0E0
void on_frame_update(void* event) noexcept {
    auto* ev = static_cast<events::FrameUpdateEvent*>(event);
    events::GameContext* ctx = ev->ctx;
    jobject timer = mc::mc_get_timer(ctx->jni, ctx->minecraft);
    const float timer_speed = mc::timer_get_speed(ctx->jni, timer);

    s_click_timer_sec = std::fmax(-0.2, s_click_timer_sec - static_cast<double>(timer_speed) * ev->delta_time);
    if (s_click_timer_sec > 0.0) {
        return;
    }

    const uint8_t raw_key = ctx->key_states[s_attack_bound_to_rmb ? input::KEY_MOUSE2 : input::KEY_MOUSE1];
    uint8_t btn_down = raw_key & 1u;
    uint8_t synth_toggled = static_cast<uint8_t>((raw_key & 2u) >> 1);
    double idle_delay = 0.01;

    while (ctx->in_game_focus) {
        if (((btn_down | synth_toggled) & 1u) == 0u) {
            s_click_timer_sec = idle_delay;
            s_last_hovered_slot = -1;
            return;
        }

        if ((s_mop_type == 1 && s_mod.allow_block_break.value) ||
            (s_mod.require_weapon.value && !weapons_is_holding_weapon_simple(ctx->jni, ctx->local_player))) {
            if ((synth_toggled & 1u) != 0u) {
                input::post_synthetic_mouse_button(!s_attack_bound_to_rmb, (btn_down & 1u) == 0u);
            }
            s_click_timer_sec = 0.0;
            s_last_hovered_slot = -1;
            return;
        }

        if (s_mod.require_not_using_item.value) {
            if (mc::player_is_using_item(ctx->jni, ctx->local_player) || ((btn_down & synth_toggled & 1u) != 0u)) {
                if ((synth_toggled & 1u) != 0u) {
                    input::post_synthetic_mouse_button(!s_attack_bound_to_rmb, (btn_down & 1u) == 0u);
                }
                s_click_timer_sec = 0.0;
                s_last_hovered_slot = -1;
                return;
            }
        } else if ((btn_down & synth_toggled & 1u) != 0u) {
            input::post_synthetic_mouse_button(!s_attack_bound_to_rmb, false);
            s_click_timer_sec = 0.0;
            s_last_hovered_slot = -1;
            return;
        }

        const bool left_button = !s_attack_bound_to_rmb;
        if ((btn_down & 1u) != 0u) {
            input::post_synthetic_mouse_button(left_button, false);
            s_click_timer_sec += s_last_press_duration_sec;
        } else {
            input::post_synthetic_mouse_button(left_button, true);
            float cps = s_mod.target_cps.value;
            if (s_mod.randomize_cps.value) {
                const float sigma = s_sigma_drift.sample_timed({0.2f, 0.3f}, 30.0);
                const float slow_wave = s_slow_drift.sample_timed({-0.1f, 0.1f}, 120.0);
                const float med_wave = s_med_drift.sample_timed({-0.1f, 0.1f}, 30.0);
                const float fast_wave = s_fast_drift.sample_timed({-0.05f, 0.05f}, 5.0);
                cps *= ((slow_wave + 1.0f) + med_wave) + fast_wave;
                cps *= (s_cps_gauss.next_standard_normal() * sigma) + 1.0f;
            }

            auto calc_exhaust_chance = [&]() noexcept -> float {
                const math::Vec2f cur_rot = mc::entity_get_rotation(ctx->jni, ctx->local_player);
                float chance = math::vec2_dist(cur_rot, s_last_tick_rot) / 20.0f;
                events::TargetTracker* tracker = ctx->target_tracker;
                if (tracker->target_entity == nullptr || tracker->distance_sq > 25.0) {
                    chance = static_cast<float>(static_cast<double>(chance) + 0.08);
                }
                return chance;
            };

            float step_delay = 0.0f;
            if (s_mod.pattern.value == 1) {
                if (s_butterfly_double_next) {
                    step_delay = 0.014f;
                    s_butterfly_double_next = false;
                } else {
                    step_delay = static_cast<float>(1.0 / static_cast<double>(cps) - 0.014000000432133675);
                    if (!simulate_exhaust(&step_delay, calc_exhaust_chance())) {
                        const bool allow_double = s_butterfly_gate.sample_chance_timed({0.94f, 0.97f}, 30.0);
                        s_butterfly_double_next = random::roll_chance(allow_double ? 1.0f : 0.0f);
                    }
                }
            } else {
                step_delay = 0.5f / cps;
                simulate_exhaust(&step_delay, calc_exhaust_chance());
            }

            s_click_timer_sec += static_cast<double>(step_delay);
            s_last_press_duration_sec = static_cast<double>(step_delay);
        }

        btn_down ^= 1u;
        synth_toggled ^= 1u;
        if (s_click_timer_sec > 0.0) {
            return;
        }
    }

    if ((synth_toggled & 1u) != 0u) {
        input::post_synthetic_mouse_button(!s_attack_bound_to_rmb, (btn_down & 1u) == 0u);
    }

    if (mc::mc_get_gui_container_screen(ctx->jni, ctx->minecraft) == nullptr) {
        s_click_timer_sec = idle_delay;
        s_last_hovered_slot = -1;
        return;
    }

    const uint8_t lmb_state = ctx->key_states[input::KEY_MOUSE1];
    double inv_delay = 0.0;
    bool custom_delay_set = false;

    if (lmb_state == 1u && s_inv_lmb_armed) {
        const bool shift_held =
            ((ctx->key_states[input::KEY_LSHIFT] & 1u) != 0u) ||
            ((ctx->key_states[input::KEY_RSHIFT] & 1u) != 0u);
        if (s_mod.allow_inventory.value && shift_held) {
            jobject gui_container = mc::mc_get_gui_container_screen(ctx->jni, ctx->minecraft);
            jobject player_inv = mc::player_get_inventory(ctx->jni, ctx->local_player);
            jobject hovered_slot = mc::gui_container_get_hovered_slot(ctx->jni, gui_container);
            if (hovered_slot != nullptr && mc::inv_player_get_dragged_stack(ctx->jni, player_inv) == nullptr) {
                const int32_t slot_num = mc::slot_get_number(ctx->jni, hovered_slot);
                if (slot_num != s_last_hovered_slot) {
                    s_last_hovered_slot = slot_num;
                    if (mc::slot_get_stack(ctx->jni, hovered_slot) != nullptr ||
                        !s_inv_empty_skip_gate.sample_chance_timed({0.6f, 0.8f}, 10.0)) {
                        input::post_synthetic_mouse_button(true, true);
                        if (s_mod.inventory_randomize.value) {
                            const math::Range2f range =
                                s_inv_spike_gate.sample_chance_timed({0.04f, 0.08f}, 10.0)
                                    ? math::Range2f{0.15f, 0.25f}
                                    : math::Range2f{0.06f, 0.075f};
                            inv_delay = static_cast<double>(random::uniform_range(range));
                        } else {
                            inv_delay = 0.001;
                        }
                        custom_delay_set = true;
                    }
                }
            }
        }
    } else {
        s_inv_lmb_armed = (lmb_state == 1u);
    }

    if (!custom_delay_set) {
        inv_delay = s_mod.inventory_randomize.value ? 0.05 : 0.001;
    }
    s_click_timer_sec += inv_delay;
}

// sub_18004E790
void on_mid_living_update(void* event) noexcept {
    auto* ev = static_cast<events::ContextEvent*>(event);
    events::GameContext* ctx = ev->ctx;
    if (ctx->in_game_focus) {
        jobject mop = mc::mc_get_object_mouse_over(ctx->jni, ctx->minecraft);
        s_mop_type = (mop != nullptr) ? mc::mop_get_type_ordinal(ctx->jni, mop) : 0;
    }
}

// sub_18004E7E0
void render_settings(gui::ClickGui* gui, gui::GuiRenderer* renderer) noexcept {
    gui::widgets::render_section_header(gui, renderer, "In-game");
    s_mod.pattern.render(gui, renderer);
    s_mod.target_cps.render(gui, renderer);
    s_mod.randomize_cps.render(gui, renderer);
    s_mod.exhaust.render(gui, renderer);
    s_mod.allow_block_break.render(gui, renderer);

    gui::widgets::render_section_header(gui, renderer, "Inventory");
    s_mod.allow_inventory.render(gui, renderer);
    if (s_mod.allow_inventory.value) {
        s_mod.inventory_randomize.render(gui, renderer);
    }

    gui::widgets::render_section_header(gui, renderer, "Conditions");
    s_mod.require_weapon.render(gui, renderer);
    s_mod.require_not_using_item.render(gui, renderer);
}

// sub_18004E8E0
void visit_config(config::ConfigArchive* ar) noexcept {
    s_mod.pattern.visit(ar, "pattern");
    s_mod.target_cps.visit(ar, "target_cps");
    s_mod.randomize_cps.visit(ar, "randomize_cps");
    s_mod.exhaust.visit(ar, "exhaust");
    s_mod.allow_block_break.visit(ar, "allow_block_break");
    s_mod.allow_inventory.visit(ar, "allow_inventory");
    s_mod.inventory_randomize.visit(ar, "inventory_randomize");
    s_mod.require_weapon.visit(ar, "require_weapon");
    s_mod.require_not_using_item.visit(ar, "require_not_using_item");
}

// sub_18004E9C0
int format_hud_tag(char* out_buf) noexcept {
    return std::snprintf(out_buf, 32, "%s", kPatternOptions[s_mod.pattern.value]);
}

} // namespace

events::Module* get_auto_clicker_module() noexcept {
    return &s_mod.base;
}

} // namespace slinky::modules
