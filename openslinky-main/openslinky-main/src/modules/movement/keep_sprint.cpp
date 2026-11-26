#include "modules/movement/keep_sprint.hpp"

#include <cstdio>
#include "config/settings.hpp"
#include "gui/widgets.hpp"
#include "minecraft/game_helpers.hpp"
#include "minecraft/mc_wrappers.hpp"
#include "util/random.hpp"

namespace slinky::modules {

namespace {

consteval config::FloatSetting make_float(
    const char* label,
    const char* format,
    float step,
    float display_scale,
    float min_val,
    float max_val,
    float initial) noexcept
{
    config::FloatSetting s{};
    s.label = label;
    s.format = format;
    s.step = step;
    s.display_scale = display_scale;
    s.min_val = min_val;
    s.max_val = max_val;
    s.value = initial;
    s.default_value = initial;
    return s;
}

consteval config::BoolSetting make_bool(const char* label, bool initial) noexcept {
    config::BoolSetting s{};
    s.label = label;
    s.value = initial;
    s.default_value = initial;
    return s;
}

constinit config::FloatSetting s_chance            = make_float("Chance", "%.0f%%", 0.01f, 100.0f, 0.0f, 1.0f, 1.0f);         // unk_18030C9A0
constinit config::FloatSetting s_motion_mult       = make_float("Retained speed", "%.0f%%", 0.01f, 100.0f, 0.1f, 1.0f, 1.0f); // unk_18030C9D8
constinit config::BoolSetting  s_require_on_ground = make_bool("On ground", false);                                           // unk_18030CA10
constinit config::BoolSetting  s_allow_reduce      = make_bool("Allow reducing", true);                                       // unk_18030CA30

constinit math::Vec3d s_saved_motion{};                    // xmmword_180319980 + qword_180319990
constinit bool        s_has_saved_motion = false;          // byte_180319998
constinit bool        s_restore_sprint_next_tick = false;  // byte_180319999

// Matches sub_18005B4C0
void on_pre_attack_or_use_entity(void* raw_event) noexcept {
    const auto* ev = static_cast<const events::AttackOrUseEntityEvent*>(raw_event);
    if (!ev->is_attack) {
        return;
    }

    events::GameContext* ctx = ev->ctx;
    if (ctx->in_game_focus &&
        minecraft::entity_is_sprinting(ctx->jni, ctx->local_player) &&
        random::roll_chance(s_chance.value)) {
        s_saved_motion = minecraft::entity_get_motion(ctx->jni, ctx->local_player);
        s_has_saved_motion = true;
    } else {
        s_has_saved_motion = false;
    }
}

// Matches sub_18005B540
void on_post_attack_or_use_entity(void* raw_event) noexcept {
    const auto* ev = static_cast<const events::AttackOrUseEntityEvent*>(raw_event);
    if (!s_has_saved_motion || !ev->is_attack) {
        return;
    }

    events::GameContext* ctx = ev->ctx;
    if (ctx->local_player == nullptr) {
        return;
    }

    if (s_require_on_ground.value && !minecraft::entity_is_on_ground(ctx->jni, ctx->local_player)) {
        return;
    }

    const int32_t ticks_existed = minecraft::entity_get_ticks_existed(ctx->jni, ctx->local_player);
    if (ticks_existed != minecraft::living_get_last_attacker_time(ctx->jni, ctx->local_player)) {
        return;
    }

    if (s_allow_reduce.value && minecraft::target_tracker_is_moving_away(ctx->target_tracker)) {
        return;
    }

    const double mult = static_cast<double>(s_motion_mult.value);
    s_saved_motion.x *= mult;
    s_saved_motion.z *= mult;
    minecraft::entity_set_motion(ctx->jni, ctx->local_player, s_saved_motion);
    s_restore_sprint_next_tick = true;
}

// Matches sub_18005B630
void on_mid_living_update(void* raw_event) noexcept {
    if (!s_restore_sprint_next_tick) {
        return;
    }
    s_restore_sprint_next_tick = false;

    const auto* ev = static_cast<const events::ContextEvent*>(raw_event);
    events::GameContext* ctx = ev->ctx;
    if (ctx->local_player != nullptr) {
        minecraft::entity_set_sprinting(ctx->jni, ctx->local_player, true);
    }
}

// Matches sub_18005B660
void render_settings(gui::ClickGui* gui, gui::GuiRenderer* renderer) noexcept {
    gui::widgets::render_section_header(gui, renderer, "Main");
    s_chance.render(gui, renderer);
    s_motion_mult.render(gui, renderer);
    s_allow_reduce.render(gui, renderer);
    gui::widgets::render_section_header(gui, renderer, "Conditions");
    s_require_on_ground.render(gui, renderer);
}

// Matches sub_18005B6E0
void visit_config(config::ConfigArchive* ar) noexcept {
    s_chance.visit(ar, "chance");
    s_motion_mult.visit(ar, "motion_mult");
    s_allow_reduce.visit(ar, "allow_reduce");
    s_require_on_ground.visit(ar, "require_on_ground");
}

// Matches sub_18005B750
int format_hud_tag(char* out_buf_32) noexcept {
    return std::snprintf(out_buf_32, 32, "%.0f%%", static_cast<double>(s_motion_mult.value * 100.0f));
}

consteval events::Module make_module() noexcept {
    events::Module mod{};
    mod.name = "Keep Sprint";
    mod.description = "Keeps your sprint speed when attacking players.";
    mod.category = events::ModuleCategory::Move;
    mod.events[events::kEventPreAttackOrUseEntity] = {&on_pre_attack_or_use_entity, 0, 0};
    mod.events[events::kEventPostAttackOrUseEntity] = {&on_post_attack_or_use_entity, 0, 0};
    mod.events[events::kEventMidLivingUpdate] = {&on_mid_living_update, 0, 0};
    mod.render_settings = &render_settings;
    mod.visit_config = &visit_config;
    mod.format_hud_tag = &format_hud_tag;
    return mod;
}

constinit events::Module s_module = make_module();

} // namespace

events::Module* get_keep_sprint_module() noexcept {
    return &s_module;
}

} // namespace slinky::modules
