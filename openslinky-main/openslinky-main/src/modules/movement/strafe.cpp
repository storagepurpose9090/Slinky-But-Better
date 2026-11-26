#include "modules/movement/strafe.hpp"

#include <cmath>
#include "config/settings.hpp"
#include "gui/widgets.hpp"
#include "minecraft/mc_wrappers.hpp"
#include "modules/module_interop.hpp"
#include "util/math.hpp"

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

consteval config::BoolSetting make_bool(const char* label, bool initial = false) noexcept {
    config::BoolSetting s{};
    s.label = label;
    s.value = initial;
    s.default_value = initial;
    return s;
}

constinit config::FloatSetting s_max_hurt_time   = make_float("Maximum hurt time", "%.0f ms", 1.0f, 50.0f, 0.0f, 10.0f, 10.0f); // unk_18030F7F0
constinit config::FloatSetting s_strength_air    = make_float("Air", "%.0f%%", 0.0f, 100.0f, 0.0f, 1.0f, 0.0f);                 // unk_18030F828
constinit config::FloatSetting s_strength_jump   = make_float("Jump", "%.0f%%", 0.0f, 100.0f, 0.0f, 1.0f, 0.0f);                // unk_18030F860
constinit config::FloatSetting s_strength_ground = make_float("Ground", "%.0f%%", 0.0f, 100.0f, 0.0f, 1.0f, 0.0f);              // unk_18030F898
constinit config::BoolSetting  s_require_weapon  = make_bool("Holding weapon");                                                 // unk_18030F8D0

constinit bool s_was_sprinting_pre_move = false; // byte_1803215BC

// Matches sub_180063460
void on_pre_move_with_heading(void* raw_event) noexcept {
    const auto* ev = static_cast<const events::MoveWithHeadingEvent*>(raw_event);
    events::GameContext* ctx = ev->ctx;
    s_was_sprinting_pre_move = minecraft::entity_is_sprinting(ctx->jni, ctx->local_player);
}

// Matches sub_180063490
void on_post_move_with_heading(void* raw_event) noexcept {
    const auto* ev = static_cast<const events::MoveWithHeadingEvent*>(raw_event);
    if (ev->move_forward == 0.0f && ev->move_strafe == 0.0f) {
        return;
    }

    events::GameContext* ctx = ev->ctx;
    if (s_require_weapon.value &&
        !weapons_is_holding_weapon_simple(ctx->jni, ctx->local_player)) {
        return;
    }

    if (static_cast<float>(minecraft::living_get_hurt_time(ctx->jni, ctx->local_player)) >
        s_max_hurt_time.value) {
        return;
    }

    bool add_jump_sprint_boost = false;
    float strength = 0.0f;
    if (minecraft::entity_is_on_ground(ctx->jni, ctx->local_player)) {
        if (ev->is_jumping) {
            add_jump_sprint_boost = !s_was_sprinting_pre_move;
            strength = s_strength_jump.value;
        } else {
            strength = s_strength_ground.value;
        }
    } else {
        strength = s_strength_air.value;
    }

    if (strength == 0.0f) {
        return;
    }

    math::Vec3d motion = minecraft::entity_get_motion(ctx->jni, ctx->local_player);
    double speed = std::hypot(motion.x, motion.z);
    if (add_jump_sprint_boost) {
        speed += 0.2;
    }

    const math::Vec2f rot = minecraft::entity_get_rotation(ctx->jni, ctx->local_player);
    double strafe_offset = 0.0;
    if (ev->move_strafe < 0.0f) {
        strafe_offset = -1.0;
    }
    if (ev->move_strafe > 0.0f) {
        strafe_offset = 1.0;
    }

    double yaw_deg = static_cast<double>(rot.x);
    if (ev->move_forward > 0.0f) {
        strafe_offset *= -45.0;
    } else if (ev->move_forward < 0.0f) {
        yaw_deg += 180.0;
        strafe_offset *= 45.0;
    } else {
        strafe_offset *= -90.0;
    }

    const float move_yaw_rad = static_cast<float>((yaw_deg + strafe_offset) * math::kPi / 180.0);
    const double blend = static_cast<double>(strength);

    const double target_x = static_cast<double>(-std::sin(move_yaw_rad)) * speed;
    motion.x = math::lerpd(motion.x, target_x, blend);

    const double target_z = static_cast<double>(std::cos(move_yaw_rad)) * speed;
    motion.z = math::lerpd(motion.z, target_z, blend);

    minecraft::entity_set_motion(ctx->jni, ctx->local_player, motion);
}

// Matches sub_1800636D0
void render_settings(gui::ClickGui* gui, gui::GuiRenderer* renderer) noexcept {
    gui::widgets::render_section_header(gui, renderer, "Strength");
    s_strength_jump.render(gui, renderer);
    s_strength_ground.render(gui, renderer);
    s_strength_air.render(gui, renderer);
    gui::widgets::render_section_header(gui, renderer, "Conditions");
    s_max_hurt_time.render(gui, renderer);
    s_require_weapon.render(gui, renderer);
}

// Matches sub_180063770
void visit_config(config::ConfigArchive* ar) noexcept {
    s_strength_jump.visit(ar, "strength_jump");
    s_strength_ground.visit(ar, "strength_ground");
    s_strength_air.visit(ar, "strength_air");
    s_max_hurt_time.visit(ar, "max_hurt_time");
    s_require_weapon.visit(ar, "require_weapon");
}

consteval events::Module make_module() noexcept {
    events::Module mod{};
    mod.name = "Strafe";
    mod.description = "Allows faster strafing.";
    mod.category = events::ModuleCategory::Move;
    mod.events[events::kEventPreMoveWithHeading] = {&on_pre_move_with_heading, 1000, 0};
    mod.events[events::kEventPostMoveWithHeading] = {&on_post_move_with_heading, 100, 0};
    mod.render_settings = &render_settings;
    mod.visit_config = &visit_config;
    return mod;
}

constinit events::Module s_module = make_module();

} // namespace

events::Module* get_strafe_module() noexcept {
    return &s_module;
}

} // namespace slinky::modules
