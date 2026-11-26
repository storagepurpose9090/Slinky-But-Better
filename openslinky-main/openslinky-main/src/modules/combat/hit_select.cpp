#include "modules/combat/hit_select.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>

#include "config/settings.hpp"
#include "gui/widgets.hpp"
#include "minecraft/game_helpers.hpp"
#include "minecraft/mc_wrappers.hpp"
#include "modules/module_interop.hpp"
#include "util/math.hpp"
#include "util/random.hpp"

namespace slinky::modules {

namespace {

constexpr const char* kModeOptions[] = {
    "Burst",
    "Criticals",
};

bool s_decided_this_tick = false;
uint32_t s_unhurt_ticks = 0;
bool s_cancel_decision = false;
uint32_t s_ticks_since_target_hit = 0;
uint32_t s_first_hit_wait_ticks = 0;
bool s_target_just_hurt_server = false;
int32_t s_tracked_target_id = 0;

void on_game_tick(void* event) noexcept;
void on_pre_attack_or_use_entity(void* event) noexcept;
void on_mid_living_update(void* event) noexcept;
void render_settings(gui::ClickGui* gui, gui::GuiRenderer* renderer) noexcept;
void visit_config(config::ConfigArchive* ar) noexcept;
int format_hud_tag(char* out_buf) noexcept;

struct HitSelectModule {
    events::Module base{};
    config::FloatSetting cancel_rate_air{{}, "Missed swings", "%.0f%%", 0.0f, 100.0f, 0.0f, 1.0f, 0.4f, 0.4f};
    config::FloatSetting pause_dur{{}, "Pause duration", "%.0f ms", 0.001f, 50.0f, 0.0f, 10.0f, 10.0f, 10.0f};
    config::FloatSetting wait_for_damage_dur{{}, "Wait for first hit", "%.0f ms", 0.001f, 50.0f, 0.0f, 8.0f, 0.0f, 0.0f};
    config::FloatSetting trades_delay{{}, "Hit later in trades", "%.0f ms", 0.001f, 50.0f, 0.0f, 8.0f, 0.0f, 0.0f};
    config::FloatSetting cancel_rate{{}, "In combat", "%.0f%%", 0.0f, 100.0f, 0.0f, 1.0f, 0.8f, 0.8f};
    config::BoolSetting require_sprint{{}, "Sprinting", false, false};
    config::BoolSetting require_weapon{{}, "Holding weapon", false, false};
    config::ModeSetting mode{{}, "Mode", 2, 0, kModeOptions, 0, 0, false};
    config::BoolSetting use_server_time{{}, "Use server attack time", false, false};
    config::BoolSetting crits_allow_reduce{{}, "Disable during knockback", true, true};
    config::BoolSetting require_damage{{}, "Only while damaged", true, true};
    config::BoolSetting fake_swing{{}, "Fake swing", false, false};

    HitSelectModule() noexcept {
        base.name = "Hit Select";
        base.description = "Perfectly times your hits.";
        base.category = events::ModuleCategory::Combat;
        base.enabled = false;
        base.flags = events::kModuleFlagNone;
        base.keybind = 0;
        base.hold_mode = false;
        base.events[events::kEventGameTick] = {&on_game_tick, 0};
        base.events[events::kEventPreAttackOrUseEntity] = {&on_pre_attack_or_use_entity, -100};
        base.events[events::kEventMidLivingUpdate] = {&on_mid_living_update, 0};
        base.render_settings = &render_settings;
        base.visit_config = &visit_config;
        base.format_hud_tag = &format_hud_tag;
    }
};

HitSelectModule s_mod{};

uint32_t sample_fractional_ticks_u32(float val) noexcept {
    const int32_t base_ticks = static_cast<int32_t>(val);
    return static_cast<uint32_t>(base_ticks) +
           (random::roll_chance(val - static_cast<float>(base_ticks)) ? 1u : 0u);
}

// sub_180058E30
void on_game_tick(void* event) noexcept {
    s_decided_this_tick = false;
    auto* ev = static_cast<events::ContextEvent*>(event);
    events::GameContext* ctx = ev->ctx;
    uint32_t next_unhurt = 0;
    if (ctx->in_game_focus && mc::living_get_hurt_time(ctx->jni, ctx->local_player) == 0) {
        next_unhurt = s_unhurt_ticks + 1u;
    }
    s_unhurt_ticks = next_unhurt;
}

// sub_180058E80
void on_pre_attack_or_use_entity(void* event) noexcept {
    auto* ev = static_cast<events::AttackOrUseEntityEvent*>(event);
    if (!ev->is_attack) {
        return;
    }

    events::GameContext* ctx = ev->ctx;
    auto apply_cancel = [&]() noexcept {
        ev->cancelled = true;
        if (s_mod.fake_swing.value &&
            ctx->local_player != nullptr &&
            !mc::player_is_using_item(ctx->jni, ctx->local_player)) {
            mc::living_swing_item_super(ctx->jni, ctx->local_player);
        }
    };
    auto allow_attack = [&]() noexcept {
        s_cancel_decision = false;
        s_decided_this_tick = true;
    };
    auto force_cancel = [&]() noexcept {
        s_cancel_decision = true;
        s_decided_this_tick = true;
        apply_cancel();
    };

    if (s_decided_this_tick) {
        if (s_cancel_decision) {
            apply_cancel();
        }
        return;
    }

    if ((s_mod.require_sprint.value && !mc::entity_is_sprinting(ctx->jni, ctx->local_player)) ||
        (s_mod.require_weapon.value && !weapons_is_holding_weapon_simple(ctx->jni, ctx->local_player))) {
        allow_attack();
        return;
    }

    jobject mop = mc::mc_get_object_mouse_over(ctx->jni, ctx->minecraft);
    bool no_pointed_entity = true;
    if (mop != nullptr) {
        const int32_t mop_type = mc::mop_get_type_ordinal(ctx->jni, mop);
        if (mop_type == 2) {
            no_pointed_entity = mc::mop_get_entity_hit(ctx->jni, mop) == nullptr;
        } else if (mop_type == 1) {
            allow_attack();
            return;
        } else if (mop_type == 0 && random::roll_chance(s_mod.cancel_rate_air.value)) {
            force_cancel();
            return;
        }
    }

    uint32_t pause_ticks = sample_fractional_ticks_u32(s_mod.pause_dur.value);
    const bool on_ground = mc::entity_is_on_ground(ctx->jni, ctx->local_player);
    bool rising_or_air = true;
    if (on_ground) {
        const math::Vec3d motion = mc::entity_get_motion(ctx->jni, ctx->local_player);
        rising_or_air = motion.y > 0.0;
    }
    const bool taking_kb = mc::target_tracker_is_moving_away(ctx->target_tracker) && rising_or_air;

    if (s_mod.mode.value == 1) {
        const bool can_land_hit =
            s_mod.use_server_time.value || ((s_ticks_since_target_hit - 9u) < 0xFFFFFFF9u);
        bool falling = false;
        if (!on_ground) {
            falling = mc::entity_get_fall_distance(ctx->jni, ctx->local_player) > 0.0f;
            if (falling && can_land_hit) {
                allow_attack();
                return;
            }
        }

        if (taking_kb && (!can_land_hit || s_mod.crits_allow_reduce.value)) {
            allow_attack();
            return;
        }

        math::Vec3d feet_pos = mc::entity_get_position(ctx->jni, ctx->local_player);
        if (mc::is_version_1_7_10(ctx->jni)) {
            feet_pos = math::vec3_sub_eye_height(feet_pos);
        }
        jobject block = mc::world_get_block(
            ctx->jni,
            ctx->world,
            static_cast<int32_t>(std::floor(feet_pos.x)),
            static_cast<int32_t>(std::floor(feet_pos.y)),
            static_cast<int32_t>(std::floor(feet_pos.z)));
        if (mc::block_get_id(ctx->jni, block) != 0) {
            allow_attack();
            return;
        }

        if (!falling && !on_ground &&
            (!s_mod.require_damage.value || mc::living_get_hurt_time(ctx->jni, ctx->local_player) != 0)) {
            force_cancel();
            return;
        }
    } else if (s_mod.mode.value == 0) {
        if (taking_kb) {
            allow_attack();
            return;
        }

        if (s_ticks_since_target_hit < 15u) {
            s_first_hit_wait_ticks = 0;
        } else if (!no_pointed_entity) {
            ++s_first_hit_wait_ticks;
            if (s_first_hit_wait_ticks <= sample_fractional_ticks_u32(s_mod.wait_for_damage_dur.value)) {
                force_cancel();
                return;
            }
        }

        if (s_unhurt_ticks <= 9u) {
            pause_ticks += sample_fractional_ticks_u32(s_mod.trades_delay.value);
        }

        if (s_target_just_hurt_server) {
            s_target_just_hurt_server = false;
            allow_attack();
            return;
        }
    } else {
        allow_attack();
        return;
    }

    if (pause_ticks > s_ticks_since_target_hit + 1u) {
        s_cancel_decision = random::roll_chance(s_mod.cancel_rate.value);
        s_decided_this_tick = true;
        if (s_cancel_decision) {
            apply_cancel();
        }
    } else {
        allow_attack();
    }
}

// sub_180059260
void on_mid_living_update(void* event) noexcept {
    auto* ev = static_cast<events::ContextEvent*>(event);
    events::GameContext* ctx = ev->ctx;
    jobject target = ctx->target_tracker->target_entity;

    if (s_mod.use_server_time.value) {
        if (target != nullptr && mc::living_get_hurt_time(ctx->jni, target) == 10) {
            s_ticks_since_target_hit = 0;
            s_target_just_hurt_server = true;
        } else {
            ++s_ticks_since_target_hit;
        }
        return;
    }

    const int32_t cur_id = (target != nullptr) ? mc::entity_get_id(ctx->jni, target) : 0;
    if (cur_id == s_tracked_target_id) {
        ++s_ticks_since_target_hit;
        if (s_ticks_since_target_hit < 10u) {
            return;
        }
    } else {
        s_tracked_target_id = cur_id;
        s_ticks_since_target_hit = 1001u;
    }

    const int32_t ticks_existed = mc::entity_get_ticks_existed(ctx->jni, ctx->local_player);
    if (ticks_existed == mc::living_get_last_attacker_time(ctx->jni, ctx->local_player)) {
        s_ticks_since_target_hit = 0;
    }
}

// sub_180059330
void render_settings(gui::ClickGui* gui, gui::GuiRenderer* renderer) noexcept {
    gui::widgets::render_section_header(gui, renderer, "Main");
    s_mod.mode.render(gui, renderer);
    s_mod.pause_dur.render(gui, renderer);
    if (s_mod.mode.value == 0) {
        s_mod.wait_for_damage_dur.render(gui, renderer);
        s_mod.trades_delay.render(gui, renderer);
    }
    s_mod.use_server_time.render(gui, renderer);
    s_mod.fake_swing.render(gui, renderer);
    if (s_mod.mode.value == 1) {
        s_mod.crits_allow_reduce.render(gui, renderer);
        s_mod.require_damage.render(gui, renderer);
    }

    gui::widgets::render_section_header(gui, renderer, "Cancel rate");
    s_mod.cancel_rate.render(gui, renderer);
    s_mod.cancel_rate_air.render(gui, renderer);

    gui::widgets::render_section_header(gui, renderer, "Conditions");
    s_mod.require_sprint.render(gui, renderer);
    s_mod.require_weapon.render(gui, renderer);
}

// sub_180059470
void visit_config(config::ConfigArchive* ar) noexcept {
    s_mod.mode.visit(ar, "mode");
    s_mod.cancel_rate.visit(ar, "cancel_rate");
    s_mod.cancel_rate_air.visit(ar, "cancel_rate_air");
    s_mod.pause_dur.visit(ar, "pause_dur");
    s_mod.wait_for_damage_dur.visit(ar, "wait_for_damage_dur");
    s_mod.trades_delay.visit(ar, "trades_delay");
    s_mod.use_server_time.visit(ar, "use_server_time");
    s_mod.fake_swing.visit(ar, "fake_swing");
    s_mod.crits_allow_reduce.visit(ar, "crits_allow_reduce");
    s_mod.require_damage.visit(ar, "require_damage");
    s_mod.require_sprint.visit(ar, "require_sprint");
    s_mod.require_weapon.visit(ar, "require_weapon");
}

// sub_180059590
int format_hud_tag(char* out_buf) noexcept {
    return std::snprintf(out_buf, 32, "%s", kModeOptions[s_mod.mode.value]);
}

} // namespace

events::Module* get_hit_select_module() noexcept {
    return &s_mod.base;
}

} // namespace slinky::modules
