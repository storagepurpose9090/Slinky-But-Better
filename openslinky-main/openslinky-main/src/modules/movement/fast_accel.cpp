#include "modules/movement/fast_accel.hpp"

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

constinit config::FloatSetting s_acceleration        = make_float("Boost", "%.0f%%", 0.0f, 100.0f, 0.0f, 1.0f, 0.0f); // unk_18030FB00
constinit config::BoolSetting  s_require_weapon      = make_bool("Holding weapon");                                   // unk_18030FB38
constinit config::BoolSetting  s_require_not_damaged = make_bool("Not damaged");                                      // unk_18030FB58

// Matches sub_1800637F0
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

    if (s_require_not_damaged.value &&
        minecraft::living_get_hurt_time(ctx->jni, ctx->local_player) > 0) {
        return;
    }

    math::Vec3d motion = minecraft::entity_get_motion(ctx->jni, ctx->local_player);
    const double cur_speed = std::hypot(motion.x, motion.z);
    if (cur_speed == 0.0 || !minecraft::entity_is_on_ground(ctx->jni, ctx->local_player)) {
        return;
    }

    const float input_len = std::hypot(ev->move_strafe, ev->move_forward);
    const float factor = ev->friction / std::fmax(1.0f, input_len);
    const double accel_step = static_cast<double>(
        std::hypot(ev->move_strafe * factor, ev->move_forward * factor));

    double max_speed = 0.0;
    for (int i = 0; i < 20; ++i) {
        max_speed = max_speed * 0.546 + accel_step;
    }

    if (ev->is_jumping && minecraft::entity_is_sprinting(ctx->jni, ctx->local_player)) {
        max_speed += 0.2;
    }

    if (cur_speed <= max_speed) {
        const double boosted_speed = math::lerpd(
            cur_speed, max_speed, static_cast<double>(s_acceleration.value));
        motion.x = (motion.x / cur_speed) * boosted_speed;
        motion.z = (motion.z / cur_speed) * boosted_speed;
        minecraft::entity_set_motion(ctx->jni, ctx->local_player, motion);
    }
}

// Matches sub_180063A50
void render_settings(gui::ClickGui* gui, gui::GuiRenderer* renderer) noexcept {
    gui::widgets::render_section_header(gui, renderer, "Main");
    s_acceleration.render(gui, renderer);
    gui::widgets::render_section_header(gui, renderer, "Conditions");
    s_require_not_damaged.render(gui, renderer);
    s_require_weapon.render(gui, renderer);
}

// Matches sub_180063AC0
void visit_config(config::ConfigArchive* ar) noexcept {
    s_acceleration.visit(ar, "acceleration");
    s_require_not_damaged.visit(ar, "require_not_damaged");
    s_require_weapon.visit(ar, "require_weapon");
}

consteval events::Module make_module() noexcept {
    events::Module mod{};
    mod.name = "Fast Accel";
    mod.description = "Speeds up acceleration.";
    mod.category = events::ModuleCategory::Move;
    mod.events[events::kEventPostMoveWithHeading] = {&on_post_move_with_heading, 0, 0};
    mod.render_settings = &render_settings;
    mod.visit_config = &visit_config;
    return mod;
}

constinit events::Module s_module = make_module();

} // namespace

events::Module* get_fast_accel_module() noexcept {
    return &s_module;
}

} // namespace slinky::modules
