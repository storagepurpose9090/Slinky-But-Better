#include "modules/movement/safe_walk.hpp"

#include "config/settings.hpp"
#include "gui/widgets.hpp"
#include "minecraft/game_helpers.hpp"
#include "minecraft/mc_wrappers.hpp"
#include "modules/module_interop.hpp"

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

constinit config::FloatSetting s_motion_multiplier      = make_float("Motion", "%.2fx", 0.0f, 0.0f, 0.8f, 1.0f, 1.0f); // unk_18030F338
constinit config::BoolSetting  s_require_looking_down   = make_bool("Looking down");                                   // unk_18030F370
constinit config::BoolSetting  s_require_not_forward    = make_bool("Not moving forward");                             // unk_18030F390
constinit config::BoolSetting  s_require_holding_blocks = make_bool("Holding blocks");                                 // unk_18030F3B0

constinit bool s_edge_sneaking = false; // byte_1803215B8

// Matches sub_1800631F0
void on_game_tick(void* raw_event) noexcept {
    const auto* ev = static_cast<const events::ContextEvent*>(raw_event);
    events::GameContext* ctx = ev->ctx;
    if (!ctx->in_game_focus) {
        return;
    }

    if ((s_require_looking_down.value &&
         minecraft::entity_get_rotation(ctx->jni, ctx->local_player).y < 65.0f) ||
        (s_require_not_forward.value &&
         minecraft::living_get_move_forward(ctx->jni, ctx->local_player) > 0.0f) ||
        (s_require_holding_blocks.value &&
         !minecraft::is_block_item(
             ctx->jni,
             minecraft::player_get_held_item(ctx->jni, ctx->local_player)))) {
        s_edge_sneaking = false;
        return;
    }

    s_edge_sneaking = true;
    if (minecraft::entity_is_on_ground(ctx->jni, ctx->local_player) &&
        s_motion_multiplier.value != 1.0f) {
        math::Vec3d motion = minecraft::entity_get_motion(ctx->jni, ctx->local_player);
        const double mult = static_cast<double>(s_motion_multiplier.value);
        motion.x *= mult;
        motion.z *= mult;
        minecraft::entity_set_motion(ctx->jni, ctx->local_player, motion);
    }
}

// Matches sub_180063320
void render_settings(gui::ClickGui* gui, gui::GuiRenderer* renderer) noexcept {
    gui::widgets::render_section_header(gui, renderer, "Main");
    s_motion_multiplier.render(gui, renderer);
    gui::widgets::render_section_header(gui, renderer, "Conditions");
    s_require_holding_blocks.render(gui, renderer);
    s_require_looking_down.render(gui, renderer);
    s_require_not_forward.render(gui, renderer);
}

// Matches sub_1800633A0
void visit_config(config::ConfigArchive* ar) noexcept {
    s_motion_multiplier.visit(ar, "motion_multiplier");
    s_require_holding_blocks.visit(ar, "require_holding_blocks");
    s_require_looking_down.visit(ar, "require_looking_down");
    s_require_not_forward.visit(ar, "require_not_forward");
}

consteval events::Module make_module() noexcept {
    events::Module mod{};
    mod.name = "Safe Walk";
    mod.description = "Prevents you from walking off ledges.";
    mod.category = events::ModuleCategory::Move;
    mod.events[events::kEventGameTick] = {&on_game_tick, 0, 0};
    mod.render_settings = &render_settings;
    mod.visit_config = &visit_config;
    return mod;
}

constinit events::Module s_module = make_module();

} // namespace

// Matches sub_1800631E0
bool safe_walk_is_edge_sneaking() noexcept {
    return s_edge_sneaking;
}

events::Module* get_safe_walk_module() noexcept {
    return &s_module;
}

} // namespace slinky::modules
