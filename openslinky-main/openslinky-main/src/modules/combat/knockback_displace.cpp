#include "modules/combat/knockback_displace.hpp"

#include <atomic>
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

constexpr const char* kDirectionOptions[] = {
    "Left",
    "Right",
    "Random",
    "Strafe",
    "Strafe (inverted)",
};

std::atomic<bool> s_hold_outbound{false};
bool s_displacing_this_tick = false;
bool s_displaced_prev_tick = false;
bool s_aimed_at_target_on_attack = false;
uint32_t s_target_hurt_cooldown_ticks = 0;
int32_t s_cooldown_ticks = 0;
math::Vec2f s_saved_rotation{};
int32_t s_strafe_sign = 0;

void on_outgoing_packet(void* event) noexcept;
void on_rotation_spoof_query(void* event) noexcept;
void on_pre_attack_or_use_entity(void* event) noexcept;
void on_post_attack_or_use_entity(void* event) noexcept;
void on_post_living_update(void* event) noexcept;
void render_settings(gui::ClickGui* gui, gui::GuiRenderer* renderer) noexcept;
void visit_config(config::ConfigArchive* ar) noexcept;
int format_hud_tag(char* out_buf) noexcept;

struct KnockbackDisplaceModule {
    events::Module base{};
    config::FloatSetting cooldown{{}, "Cooldown", "%.0f ms", 0.0f, 50.0f, 0.0f, 40.0f, 5.0f, 5.0f};
    config::FloatSetting angle{{}, "Angle", "%.0f\xC2\xB0", 0.0f, 0.0f, 45.0f, 180.0f, 90.0f, 90.0f};
    config::BoolSetting require_no_kb{{}, "Not taking knockback", false, false};
    config::BoolSetting require_weapon{{}, "Holding weapon", false, false};
    config::BoolSetting require_kb_enchant{{}, "Knockback enchantment", false, false};
    config::BoolSetting require_facing_away{{}, "Target facing away", false, false};
    config::ModeSetting direction{{}, "Direction", 5, 0, kDirectionOptions, 0, 0, false};
    config::BoolSetting hide_rotation{{}, "Hide rotation from players", false, false};
    config::BoolSetting use_last_strafe{{}, "Use last strafe direction", false, false};

    KnockbackDisplaceModule() noexcept {
        base.name = "Knockback Displace";
        base.description = "Allows dealing knockback at a different angle.";
        base.category = events::ModuleCategory::Combat;
        base.enabled = false;
        base.flags = events::kModuleFlagNone;
        base.keybind = 0;
        base.hold_mode = false;
        base.events[events::kEventOutgoingPacket] = {&on_outgoing_packet, 0};
        base.events[events::kEventRotationSpoofQuery] = {&on_rotation_spoof_query, -1000};
        base.events[events::kEventPreAttackOrUseEntity] = {&on_pre_attack_or_use_entity, 1000};
        base.events[events::kEventPostAttackOrUseEntity] = {&on_post_attack_or_use_entity, 0};
        base.events[events::kEventPostLivingUpdate] = {&on_post_living_update, 1000};
        base.render_settings = &render_settings;
        base.visit_config = &visit_config;
        base.format_hud_tag = &format_hud_tag;
    }
};

KnockbackDisplaceModule s_mod{};

// sub_18006C990
bool is_target_facing_away(events::GameContext* ctx, math::Vec2f local_rot, jobject target_mp) noexcept {
    const double target_yaw = mc::other_player_mp_get_target_yaw(ctx->jni, target_mp);
    return std::fabs(math::wrap_degrees_180(static_cast<float>(target_yaw) - local_rot.x)) < 100.0f;
}

// sub_18006C9E0
int32_t choose_displace_direction_sign() noexcept {
    switch (s_mod.direction.value) {
        case 0:
            return -1;
        case 1:
            return 1;
        case 2:
            return random::roll_chance(0.5f) ? 1 : -1;
        case 3:
            return s_strafe_sign;
        case 4:
            return -s_strafe_sign;
        default:
            return 0;
    }
}

// sub_18006C360
void on_outgoing_packet(void* event) noexcept {
    auto* ev = static_cast<events::NettyPacketEvent*>(event);
    if (s_hold_outbound.load(std::memory_order_acquire)) {
        ev->action = 1;
    }
}

// sub_18006C380
void on_rotation_spoof_query(void* event) noexcept {
    auto* ev = static_cast<events::RotationSpoofEvent*>(event);
    if (s_displacing_this_tick || s_displaced_prev_tick) {
        ev->move_fix = false;
    }
}

// sub_18006C3A0
void on_pre_attack_or_use_entity(void* event) noexcept {
    auto* ev = static_cast<events::AttackOrUseEntityEvent*>(event);
    if (s_displacing_this_tick) {
        ev->cancelled = true;
        return;
    }
    if (!ev->is_attack) {
        return;
    }

    events::GameContext* ctx = ev->ctx;
    jobject mop = mc::mc_get_object_mouse_over(ctx->jni, ctx->minecraft);
    if (mop == nullptr || mc::mop_get_type_ordinal(ctx->jni, mop) != 2) {
        return;
    }

    jobject hit_entity = mc::mop_get_entity_hit(ctx->jni, mop);
    jobject target_mp = mc::as_entity_other_player_mp(ctx->jni, hit_entity);
    if (target_mp == nullptr || mc::player_has_tag_flags(ctx->jni, target_mp, mc::kPlayerTagExcludedMask)) {
        return;
    }

    s_aimed_at_target_on_attack = true;
    if (s_displaced_prev_tick ||
        s_target_hurt_cooldown_ticks < 9u ||
        s_mod.cooldown.value > static_cast<float>(s_cooldown_ticks) ||
        timer_range_is_manipulating() ||
        (s_mod.require_no_kb.value && mc::target_tracker_is_moving_away(ctx->target_tracker))) {
        return;
    }

    uint32_t weapon_flags = 0;
    const bool holding_weapon = weapons_is_holding_weapon(ctx->jni, ctx->local_player, &weapon_flags);
    if (s_mod.require_weapon.value && !holding_weapon) {
        return;
    }
    if ((weapon_flags & 2u) == 0 &&
        (s_mod.require_kb_enchant.value || !mc::entity_is_sprinting(ctx->jni, ctx->local_player))) {
        return;
    }

    s_saved_rotation = mc::entity_get_rotation(ctx->jni, ctx->local_player);
    if (s_mod.require_facing_away.value && !is_target_facing_away(ctx, s_saved_rotation, target_mp)) {
        return;
    }

    const int32_t dir_sign = choose_displace_direction_sign();
    if (dir_sign == 0 || !mc::try_claim_aim_priority(200u)) {
        return;
    }

    mc::update_rotation_spoof_state(ctx, true, false, false, s_saved_rotation);
    s_displacing_this_tick = true;

    const bool positive_pitch = random::roll_chance(0.5f);
    const float yaw_delta = (s_mod.angle.value - random::uniform_range({0.0f, 10.0f})) * static_cast<float>(dir_sign);
    float pitch_delta = random::uniform_range({10.0f, 20.0f});
    if (!positive_pitch) {
        pitch_delta = -pitch_delta;
    }

    mc::queue_aim_rotation_delta({yaw_delta, pitch_delta});
    mc::apply_pending_aim_rotation(ctx->jni, ctx->local_player, false);

    if (s_mod.hide_rotation.value) {
        s_hold_outbound.exchange(true, std::memory_order_seq_cst);
    }
    ev->cancelled = true;
    s_cooldown_ticks = 0;
}

// sub_18006C610
void on_post_attack_or_use_entity(void* event) noexcept {
    auto* ev = static_cast<events::AttackOrUseEntityEvent*>(event);
    if (ev->is_attack && s_target_hurt_cooldown_ticks >= 10u && s_aimed_at_target_on_attack) {
        s_target_hurt_cooldown_ticks = 0;
    }
}

// sub_18006C640
void on_post_living_update(void* event) noexcept {
    ++s_target_hurt_cooldown_ticks;
    ++s_cooldown_ticks;
    s_displaced_prev_tick = s_displacing_this_tick;
    s_aimed_at_target_on_attack = false;

    auto* ev = static_cast<events::ContextEvent*>(event);
    events::GameContext* ctx = ev->ctx;
    if (ctx->in_game_focus) {
        const float move_strafe = mc::living_get_move_strafing(ctx->jni, ctx->local_player);
        const int32_t cur_strafe_sign = (move_strafe > 0.0f ? 1 : 0) - (0.0f > move_strafe ? 1 : 0);
        if (!s_mod.use_last_strafe.value || cur_strafe_sign != 0) {
            s_strafe_sign = cur_strafe_sign;
        }

        if (s_displacing_this_tick) {
            if (mc::try_claim_aim_priority(200u)) {
                const math::Vec2f cur_rot = mc::entity_get_rotation(ctx->jni, ctx->local_player);
                const math::Vec2f pending = mc::get_pending_aim_rotation_delta();
                const math::Vec2f projected = math::vec2_add(cur_rot, pending);
                mc::queue_aim_rotation_delta(math::vec2_sub(s_saved_rotation, projected));
                mc::apply_pending_aim_rotation(ctx->jni, ctx->local_player, false);

                jobject settings = mc::mc_get_game_settings(ctx->jni, ctx->minecraft);
                mc::mc_set_left_click_counter(ctx->jni, ctx->minecraft, 0);
                jobject key_attack = mc::settings_get_key_attack(ctx->jni, settings);
                mc::key_binding_increment_press_time(ctx->jni, key_attack);
            }
            s_displacing_this_tick = false;
        }
    } else {
        s_displacing_this_tick = false;
    }

    const bool hold_next = s_displaced_prev_tick ? s_mod.hide_rotation.value : false;
    s_hold_outbound.exchange(hold_next, std::memory_order_seq_cst);
}

// sub_18006C780
void render_settings(gui::ClickGui* gui, gui::GuiRenderer* renderer) noexcept {
    gui::widgets::render_section_header(gui, renderer, "Main");
    s_mod.direction.render(gui, renderer);
    if (static_cast<uint32_t>(s_mod.direction.value - 3) <= 1u) {
        s_mod.use_last_strafe.render(gui, renderer);
    }
    s_mod.cooldown.render(gui, renderer);
    s_mod.angle.render(gui, renderer);
    s_mod.hide_rotation.render(gui, renderer);

    gui::widgets::render_section_header(gui, renderer, "Condition");
    s_mod.require_no_kb.render(gui, renderer);
    s_mod.require_weapon.render(gui, renderer);
    s_mod.require_kb_enchant.render(gui, renderer);
    s_mod.require_facing_away.render(gui, renderer);
}

// sub_18006C870
void visit_config(config::ConfigArchive* ar) noexcept {
    s_mod.direction.visit(ar, "direction");
    s_mod.use_last_strafe.visit(ar, "use_last_strafe");
    s_mod.angle.visit(ar, "angle");
    s_mod.cooldown.visit(ar, "cooldown");
    s_mod.hide_rotation.visit(ar, "hide_rotation");
    s_mod.require_no_kb.visit(ar, "require_no_kb");
    s_mod.require_weapon.visit(ar, "require_weapon");
    s_mod.require_kb_enchant.visit(ar, "require_kb_enchant");
    s_mod.require_facing_away.visit(ar, "require_facing_away");
}

// sub_18006C950
int format_hud_tag(char* out_buf) noexcept {
    return std::snprintf(out_buf, 32, "%.0f\xC2\xB0", static_cast<double>(s_mod.angle.value));
}

} // namespace

events::Module* get_knockback_displace_module() noexcept {
    return &s_mod.base;
}

} // namespace slinky::modules
