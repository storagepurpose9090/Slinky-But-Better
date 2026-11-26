#include "modules/combat/auto_block.hpp"

#include <atomic>
#include <cstdint>

#include "config/settings.hpp"
#include "gui/widgets.hpp"
#include "minecraft/game_helpers.hpp"
#include "minecraft/mc_wrappers.hpp"
#include "modules/module_interop.hpp"
#include "util/keycodes.hpp"
#include "util/random.hpp"

namespace slinky::modules {

namespace {

int32_t s_cooldown_ticks = 0;
int32_t s_hold_ticks = 0;
uint32_t s_ticks_since_attack = 0;
int32_t s_last_target_id = 0;
bool s_force_fake_anim = false;
bool s_fake_anim_applied = false;
bool s_suppress_manual_right_click = false;
std::atomic<int32_t> s_lag_ticks{0};
bool s_blocking = false;
bool s_use_key_overridden = false;
int32_t s_fake_use_count = 72000;

void on_game_tick(void* event) noexcept;
void on_outgoing_packet(void* event) noexcept;
void on_module_state_changed(void* event) noexcept;
void on_pre_attack_or_use_entity(void* event) noexcept;
void on_post_living_update(void* event) noexcept;
void render_settings(gui::ClickGui* gui, gui::GuiRenderer* renderer) noexcept;
void visit_config(config::ConfigArchive* ar) noexcept;

struct AutoBlockModule {
    events::Module base{};
    config::FloatSetting max_hold_time{{}, "Maximum hold duration", "%.0f ms", 0.0f, 50.0f, 1.0f, 10.0f, 5.0f, 5.0f};
    config::FloatSetting lag_chance{{}, "Chance", "%.0f %%", 0.0f, 100.0f, 0.0f, 1.0f, 0.0f, 0.0f};
    config::FloatSetting lag_duration{{}, "Maximum duration", "%.0f ms", 0.0f, 50.0f, 1.0f, 10.0f, 2.0f, 2.0f};
    config::FloatSetting max_hurt_time{{}, "Maximum hurt time", "%.0f ms", 0.0f, 50.0f, 1.0f, 10.0f, 6.0f, 6.0f};
    config::BoolSetting require_left_mouse_pressed{{}, "Left mouse pressed", true, true};
    config::BoolSetting require_right_mouse_pressed{{}, "Right mouse pressed", false, false};
    config::BoolSetting require_damage{{}, "Damaged", false, false};
    config::BoolSetting fake_block_animation{{}, "Enabled", false, false};
    config::BoolSetting fake_block_animation_require_target{{}, "Only within range", false, false};
    config::FloatSetting range{{}, "Range", "%.1f blocks", 0.1f, 0.0f, 2.0f, 6.0f, 3.1f, 3.1f};
    config::BoolSetting lag_flush_attack{{}, "Prevent delaying attacks", true, true};
    config::BoolSetting lag_reblock{{}, "Block again immediately", false, false};

    AutoBlockModule() noexcept {
        base.name = "Auto Block";
        base.description = "Blocks your sword for you in combat.";
        base.category = events::ModuleCategory::Combat;
        base.enabled = false;
        base.flags = events::kModuleFlagNone;
        base.keybind = 0;
        base.hold_mode = false;
        base.events[events::kEventGameTick] = {&on_game_tick, -1000};
        base.events[events::kEventOutgoingPacket] = {&on_outgoing_packet, 0};
        base.events[events::kEventModuleStateChanged] = {&on_module_state_changed, 0};
        base.events[events::kEventPreAttackOrUseEntity] = {&on_pre_attack_or_use_entity, 0};
        base.events[events::kEventPostLivingUpdate] = {&on_post_living_update, 0};
        base.render_settings = &render_settings;
        base.visit_config = &visit_config;
        base.format_hud_tag = nullptr;
    }
};

AutoBlockModule s_mod{};

int32_t sample_fractional_ticks(float val) noexcept {
    const int32_t base_ticks = static_cast<int32_t>(val);
    return base_ticks + (random::roll_chance(val - static_cast<float>(base_ticks)) ? 1 : 0);
}

// sub_180052CC0
void on_game_tick(void* event) noexcept {
    s_force_fake_anim = false;
    auto* ev = static_cast<events::ContextEvent*>(event);
    events::GameContext* ctx = ev->ctx;

    if (s_fake_anim_applied) {
        if (ctx->local_player != nullptr && !mc::player_is_using_item(ctx->jni, ctx->local_player)) {
            mc::player_set_item_in_use_count(ctx->jni, ctx->local_player, 0);
        }
        s_fake_anim_applied = false;
    }

    if (s_cooldown_ticks != 0) {
        --s_cooldown_ticks;
    }

    if (!ctx->in_game_focus) {
        if (s_use_key_overridden) {
            jobject settings = mc::mc_get_game_settings(ctx->jni, ctx->minecraft);
            jobject key_use = mc::settings_get_key_use_item(ctx->jni, settings);
            mc::key_binding_set_pressed(ctx->jni, key_use, mc::key_binding_is_physically_down(ctx->jni, key_use));
            s_use_key_overridden = false;
        }
        s_suppress_manual_right_click = false;
        s_lag_ticks.exchange(0, std::memory_order_seq_cst);
        return;
    }

    const int32_t hurt_time = mc::living_get_hurt_time(ctx->jni, ctx->local_player);
    jobject held_item = mc::player_get_held_item(ctx->jni, ctx->local_player);
    const bool holding_sword = mc::is_sword_item(ctx->jni, held_item);
    s_suppress_manual_right_click = false;

    bool conditions_met = false;
    bool eligible_this_tick = false;
    bool fake_anim = false;

    if (holding_sword) {
        const bool left_down = ctx->key_states[input::KEY_MOUSE1] != 0;
        const bool right_down = ctx->key_states[input::KEY_MOUSE2] != 0;
        bool blocked_by_input = false;

        if ((s_mod.require_left_mouse_pressed.value && !left_down) || timer_range_is_manipulating()) {
            blocked_by_input = true;
        } else {
            s_suppress_manual_right_click = s_mod.require_right_mouse_pressed.value;
            if (s_mod.require_right_mouse_pressed.value != right_down) {
                blocked_by_input = true;
            }
        }

        if (!blocked_by_input) {
            conditions_met = true;
            if ((hurt_time != 0 || !s_mod.require_damage.value) &&
                hurt_time <= sample_fractional_ticks(s_mod.max_hurt_time.value)) {
                jobject settings = mc::mc_get_game_settings(ctx->jni, ctx->minecraft);
                jobject key_use = mc::settings_get_key_use_item(ctx->jni, settings);
                mc::key_binding_set_pressed(ctx->jni, key_use, s_blocking);
                if (s_blocking) {
                    mc::key_binding_increment_press_time(ctx->jni, key_use);
                }
                eligible_this_tick = true;
                s_use_key_overridden = true;
            }
        }

        if (!eligible_this_tick) {
            jobject settings = mc::mc_get_game_settings(ctx->jni, ctx->minecraft);
            jobject key_use = mc::settings_get_key_use_item(ctx->jni, settings);
            const bool passthrough = s_suppress_manual_right_click
                                         ? false
                                         : mc::key_binding_is_physically_down(ctx->jni, key_use);
            mc::key_binding_set_pressed(ctx->jni, key_use, passthrough);
            s_use_key_overridden = false;
        }

        if (conditions_met && s_mod.fake_block_animation.value) {
            if (s_mod.fake_block_animation_require_target.value) {
                events::TargetTracker* tracker = ctx->target_tracker;
                fake_anim = (tracker->target_entity != nullptr) &&
                            (static_cast<double>(s_mod.range.value * s_mod.range.value) > tracker->distance_sq);
            } else {
                fake_anim = true;
            }
        }
    } else if (s_use_key_overridden) {
        jobject settings = mc::mc_get_game_settings(ctx->jni, ctx->minecraft);
        jobject key_use = mc::settings_get_key_use_item(ctx->jni, settings);
        const bool passthrough = s_suppress_manual_right_click
                                     ? false
                                     : mc::key_binding_is_physically_down(ctx->jni, key_use);
        mc::key_binding_set_pressed(ctx->jni, key_use, passthrough);
        s_use_key_overridden = false;
    }

    s_force_fake_anim = fake_anim;
    if (hurt_time == 10) {
        s_cooldown_ticks = 0;
    }

    ++s_ticks_since_attack;
    bool reset_for_attack = false;
    if (s_ticks_since_attack >= 10u) {
        const int32_t ticks_existed = mc::entity_get_ticks_existed(ctx->jni, ctx->local_player);
        if (ticks_existed == mc::living_get_last_attacker_time(ctx->jni, ctx->local_player) + 1) {
            s_ticks_since_attack = 0;
            reset_for_attack = true;
        }
    }

    jobject target_entity = ctx->target_tracker->target_entity;
    const int32_t cur_target_id = (target_entity != nullptr) ? mc::entity_get_id(ctx->jni, target_entity) : 0;
    if (cur_target_id != s_last_target_id) {
        s_last_target_id = cur_target_id;
        s_ticks_since_attack = 0;
        reset_for_attack = true;
    }

    if (s_lag_ticks.load(std::memory_order_acquire) != 0) {
        if (s_mod.lag_flush_attack.value && reset_for_attack) {
            s_lag_ticks.exchange(0, std::memory_order_seq_cst);
        } else {
            s_lag_ticks.fetch_sub(1, std::memory_order_seq_cst);
        }
        if (s_mod.lag_reblock.value && s_lag_ticks.load(std::memory_order_acquire) == 0) {
            s_cooldown_ticks = 0;
        }
    }

    if (s_blocking) {
        const int32_t cur_hold = ++s_hold_ticks;
        const bool keep_holding =
            (cur_hold < sample_fractional_ticks(s_mod.max_hold_time.value)) && eligible_this_tick;
        if (!keep_holding) {
            jobject settings = mc::mc_get_game_settings(ctx->jni, ctx->minecraft);
            jobject key_use = mc::settings_get_key_use_item(ctx->jni, settings);
            mc::key_binding_set_pressed(ctx->jni, key_use, false);
            s_use_key_overridden = true;
            if (random::roll_chance(s_mod.lag_chance.value)) {
                s_lag_ticks.exchange(sample_fractional_ticks(s_mod.lag_duration.value), std::memory_order_seq_cst);
            }
            s_blocking = false;
        }
    } else if (s_cooldown_ticks == 0 && eligible_this_tick) {
        events::TargetTracker* tracker = ctx->target_tracker;
        if (tracker->target_entity != nullptr &&
            static_cast<double>(s_mod.range.value * s_mod.range.value) > tracker->distance_sq) {
            jobject settings = mc::mc_get_game_settings(ctx->jni, ctx->minecraft);
            jobject key_use = mc::settings_get_key_use_item(ctx->jni, settings);
            mc::key_binding_set_pressed(ctx->jni, key_use, true);
            mc::key_binding_increment_press_time(ctx->jni, key_use);
            s_use_key_overridden = true;
            s_blocking = true;
            s_cooldown_ticks = 10;
            s_hold_ticks = 0;
            s_lag_ticks.exchange(0, std::memory_order_seq_cst);
        }
    }
}

// sub_1800531B0
void on_outgoing_packet(void* event) noexcept {
    auto* ev = static_cast<events::NettyPacketEvent*>(event);
    if (s_lag_ticks.load(std::memory_order_acquire) != 0) {
        ev->action = 1;
    }
}

// sub_1800531D0
void on_module_state_changed(void* event) noexcept {
    auto* ev = static_cast<events::ModuleStateChangedEvent*>(event);
    if (ev->module == &s_mod.base && !ev->enabled && s_blocking) {
        if (s_use_key_overridden) {
            jobject settings = mc::mc_get_game_settings(ev->ctx->jni, ev->ctx->minecraft);
            jobject key_use = mc::settings_get_key_use_item(ev->ctx->jni, settings);
            mc::key_binding_set_pressed(ev->ctx->jni, key_use, mc::key_binding_is_physically_down(ev->ctx->jni, key_use));
            s_use_key_overridden = false;
        }
        s_blocking = false;
        s_lag_ticks.exchange(0, std::memory_order_seq_cst);
    }
}

// sub_180053260
void on_pre_attack_or_use_entity(void* event) noexcept {
    auto* ev = static_cast<events::AttackOrUseEntityEvent*>(event);
    if (!ev->is_attack) {
        events::GameContext* ctx = ev->ctx;
        jobject held_item = mc::player_get_held_item(ctx->jni, ctx->local_player);
        if (!mc::is_sword_item(ctx->jni, held_item)) {
            s_lag_ticks.exchange(0, std::memory_order_seq_cst);
        }
        if (s_suppress_manual_right_click && !s_blocking) {
            ev->cancelled = true;
        }
    }
}

// sub_1800532C0
void on_post_living_update(void* event) noexcept {
    auto* ev = static_cast<events::ContextEvent*>(event);
    if (s_force_fake_anim) {
        if (ev->ctx->local_player != nullptr) {
            s_fake_anim_applied = true;
            mc::player_set_item_in_use_count(ev->ctx->jni, ev->ctx->local_player, --s_fake_use_count);
        }
    } else {
        s_fake_use_count = 72000;
    }
}

// sub_180053310
void render_settings(gui::ClickGui* gui, gui::GuiRenderer* renderer) noexcept {
    gui::widgets::render_section_header(gui, renderer, "Main");
    s_mod.range.render(gui, renderer);
    s_mod.max_hurt_time.render(gui, renderer);
    s_mod.max_hold_time.render(gui, renderer);

    gui::widgets::render_section_header(gui, renderer, "Force block animation");
    s_mod.fake_block_animation.render(gui, renderer);
    if (s_mod.fake_block_animation.value) {
        s_mod.fake_block_animation_require_target.render(gui, renderer);
    }

    gui::widgets::render_section_header(gui, renderer, "Lag");
    s_mod.lag_chance.render(gui, renderer);
    s_mod.lag_duration.render(gui, renderer);
    s_mod.lag_flush_attack.render(gui, renderer);
    s_mod.lag_reblock.render(gui, renderer);

    gui::widgets::render_section_header(gui, renderer, "Conditions");
    s_mod.require_left_mouse_pressed.render(gui, renderer);
    s_mod.require_right_mouse_pressed.render(gui, renderer);
    s_mod.require_damage.render(gui, renderer);
}

// sub_180053450
void visit_config(config::ConfigArchive* ar) noexcept {
    s_mod.range.visit(ar, "range");
    s_mod.max_hurt_time.visit(ar, "max_hurt_time");
    s_mod.max_hold_time.visit(ar, "max_hold_time");
    s_mod.fake_block_animation.visit(ar, "fake_block_animation");
    s_mod.fake_block_animation_require_target.visit(ar, "fake_block_animation_require_target");
    s_mod.lag_chance.visit(ar, "lag_chance");
    s_mod.lag_duration.visit(ar, "lag_duration");
    s_mod.lag_flush_attack.visit(ar, "lag_flush_attack");
    s_mod.lag_reblock.visit(ar, "lag_reblock");
    s_mod.require_left_mouse_pressed.visit(ar, "require_left_mouse_pressed");
    s_mod.require_right_mouse_pressed.visit(ar, "require_right_mouse_pressed");
    s_mod.require_damage.visit(ar, "require_damage");
}

} // namespace

events::Module* get_auto_block_module() noexcept {
    return &s_mod.base;
}

} // namespace slinky::modules
