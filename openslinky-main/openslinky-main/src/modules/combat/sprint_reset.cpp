#include "modules/combat/sprint_reset.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>

#include "config/settings.hpp"
#include "gui/widgets.hpp"
#include "minecraft/mc_wrappers.hpp"
#include "modules/module_interop.hpp"
#include "util/random.hpp"
#include "util/time.hpp"

namespace slinky::modules {

namespace {

constexpr const char* kModeOptions[] = {
    "WTap",
    "Sneak",
    "NoStop",
};

int32_t s_phase = 0;
float s_tick_timer = 0.0f;
double s_last_attack_time_sec = 0.0;
bool s_attack_pending = false;
int32_t s_ticks_since_damaged = 0;
random::DriftNoise s_delay_drift{};
random::DriftNoise s_duration_drift{};
random::DriftNoise s_duration_spike_gate{};

void on_pre_living_update(void* event) noexcept;
void render_settings(gui::ClickGui* gui, gui::GuiRenderer* renderer) noexcept;
void visit_config(config::ConfigArchive* ar) noexcept;
int format_hud_tag(char* out_buf) noexcept;

struct SprintResetModule {
    events::Module base{};
    config::ModeSetting mode{{}, "Mode", 3, 0, kModeOptions, 0, 0, false};
    config::FloatSetting delay{{}, "Delay", "%.0f ms", 0.0f, 50.0f, 0.0f, 8.0f, 4.0f, 4.0f};
    config::BoolSetting randomize{{}, "Randomize", true, true};
    config::BoolSetting wait_for_damage{{}, "Damaged", false, false};
    config::BoolSetting require_weapon{{}, "Holding weapon", false, false};
    config::FloatSetting duration{{}, "Stop duration", "%.0f ms", 0.0f, 50.0f, 1.0f, 6.0f, 2.0f, 2.0f};

    SprintResetModule() noexcept {
        base.name = "Sprint Reset";
        base.description = "Resets your sprint state to deal more knockback.";
        base.category = events::ModuleCategory::Combat;
        base.enabled = false;
        base.flags = events::kModuleFlagNone;
        base.keybind = 0;
        base.hold_mode = false;
        base.events[events::kEventPreLivingUpdate] = {&on_pre_living_update, -100};
        base.render_settings = &render_settings;
        base.visit_config = &visit_config;
        base.format_hud_tag = &format_hud_tag;
    }
};

SprintResetModule s_mod{};

// sub_18005C520
void apply_sprint_reset_input(events::GameContext* ctx, bool active) noexcept {
    jobject settings = mc::mc_get_game_settings(ctx->jni, ctx->minecraft);
    jobject key_binding = nullptr;
    if (s_mod.mode.value == 2) {
        key_binding = mc::settings_get_key_sprint(ctx->jni, settings);
    } else if (s_mod.mode.value == 1) {
        key_binding = mc::settings_get_key_sneak(ctx->jni, settings);
    } else {
        key_binding = mc::settings_get_key_forward(ctx->jni, settings);
    }

    if (s_mod.mode.value == 2) {
        mc::entity_set_sprinting(ctx->jni, ctx->local_player, !active);
    }

    const bool pressed = active
                             ? (s_mod.mode.value == 1)
                             : mc::key_binding_is_physically_down(ctx->jni, key_binding);
    mc::key_binding_set_pressed(ctx->jni, key_binding, pressed);
}

// sub_18005C0E0
void on_pre_living_update(void* event) noexcept {
    auto* ev = static_cast<events::ContextEvent*>(event);
    events::GameContext* ctx = ev->ctx;
    if (!ctx->in_game_focus) {
        if (s_phase == 2) {
            apply_sprint_reset_input(ctx, false);
        }
        s_phase = 0;
        s_attack_pending = false;
        s_tick_timer = 0.0f;
        return;
    }

    const double now_sec = time::now_seconds();
    if (now_sec - s_last_attack_time_sec >= 0.5) {
        const int32_t ticks_existed = mc::entity_get_ticks_existed(ctx->jni, ctx->local_player);
        if (ticks_existed == mc::living_get_last_attacker_time(ctx->jni, ctx->local_player)) {
            s_last_attack_time_sec = now_sec;
            s_attack_pending = true;
        }
    }

    ++s_ticks_since_damaged;
    if (mc::living_get_hurt_time(ctx->jni, ctx->local_player) == 10) {
        s_ticks_since_damaged = 0;
    }

    if (s_tick_timer > 0.0f) {
        s_tick_timer -= 1.0f;
        if (s_tick_timer > 0.0f) {
            if (s_phase == 2) {
                apply_sprint_reset_input(ctx, true);
            }
            return;
        }
    }

    if (s_phase == 2) {
        apply_sprint_reset_input(ctx, false);
        s_phase = 0;
    } else if (s_phase == 1) {
        apply_sprint_reset_input(ctx, true);
        s_phase = 2;
        s_attack_pending = false;

        float stop_ticks = s_mod.duration.value;
        if (s_mod.randomize.value) {
            stop_ticks = std::fmax(0.0f, stop_ticks - s_duration_drift.sample_timed({0.0f, 1.0f}, 30.0));
            if (s_duration_spike_gate.sample_chance_timed({0.05f, 0.1f}, 30.0)) {
                stop_ticks += random::uniform_range({1.0f, 2.0f});
            }
        }
        s_tick_timer += stop_ticks;
    } else {
        float wait_ticks = s_mod.delay.value;
        if (s_mod.randomize.value) {
            wait_ticks += s_delay_drift.sample_timed({0.0f, 2.0f}, 5.0);
        }

        if (s_attack_pending &&
            (!s_mod.wait_for_damage.value || s_ticks_since_damaged == 0) &&
            (!s_mod.require_weapon.value || weapons_is_holding_weapon_simple(ctx->jni, ctx->local_player))) {
            s_tick_timer += wait_ticks;
            s_phase = 1;
        }
    }
}

// sub_18005C390
void render_settings(gui::ClickGui* gui, gui::GuiRenderer* renderer) noexcept {
    gui::widgets::render_section_header(gui, renderer, "Main");
    s_mod.mode.render(gui, renderer);
    s_mod.delay.render(gui, renderer);
    s_mod.duration.render(gui, renderer);
    s_mod.randomize.render(gui, renderer);

    gui::widgets::render_section_header(gui, renderer, "Conditions");
    s_mod.wait_for_damage.render(gui, renderer);
    s_mod.require_weapon.render(gui, renderer);
}

// sub_18005C440
void visit_config(config::ConfigArchive* ar) noexcept {
    s_mod.mode.visit(ar, "mode");
    s_mod.delay.visit(ar, "delay");
    s_mod.duration.visit(ar, "duration");
    s_mod.randomize.visit(ar, "randomize");
    s_mod.wait_for_damage.visit(ar, "wait_for_damage");
    s_mod.require_weapon.visit(ar, "require_weapon");
}

// sub_18005C4E0
int format_hud_tag(char* out_buf) noexcept {
    return std::snprintf(out_buf, 32, "%s", kModeOptions[s_mod.mode.value]);
}

} // namespace

events::Module* get_sprint_reset_module() noexcept {
    return &s_mod.base;
}

} // namespace slinky::modules
