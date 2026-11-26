#include "modules/movement/no_slow.hpp"

#include "config/settings.hpp"
#include "gui/widgets.hpp"
#include "minecraft/game_helpers.hpp"
#include "minecraft/mc_wrappers.hpp"

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

constinit config::FloatSetting s_mult_bow         = make_float("Bow", "%.0f%%", 0.0f, 100.0f, 0.2f, 1.0f, 1.0f);         // unk_18030FF98
constinit config::FloatSetting s_mult_sword       = make_float("Sword", "%.0f%%", 0.0f, 100.0f, 0.2f, 1.0f, 1.0f);       // unk_18030FFD0
constinit config::FloatSetting s_mult_consumables = make_float("Consumables", "%.0f%%", 0.0f, 100.0f, 0.2f, 1.0f, 1.0f); // unk_180310008

// Matches sub_180063F00
void on_pre_move_with_heading(void* raw_event) noexcept {
    auto* ev = static_cast<events::MoveWithHeadingEvent*>(raw_event);
    events::GameContext* ctx = ev->ctx;

    if (!minecraft::player_is_using_item(ctx->jni, ctx->local_player)) {
        return;
    }

    jobject held_item = minecraft::player_get_held_item(ctx->jni, ctx->local_player);
    if (held_item == nullptr) {
        return;
    }

    const int32_t item_id = minecraft::item_get_id(ctx->jni, held_item);
    float mult = s_mult_consumables.value;

    switch (item_id) {
        case 267: // Iron Sword
        case 268: // Wooden Sword
        case 272: // Stone Sword
        case 276: // Diamond Sword
        case 283: // Golden Sword
            mult = s_mult_sword.value;
            break;
        case 261: // Bow
            mult = s_mult_bow.value;
            break;
        default:
            mult = s_mult_consumables.value;
            break;
    }

    const double scale = static_cast<double>(mult) / 0.2;
    ev->move_strafe = static_cast<float>(static_cast<double>(ev->move_strafe) * scale);
    ev->move_forward = static_cast<float>(static_cast<double>(ev->move_forward) * scale);
}

// Matches sub_180063FC0
void render_settings(gui::ClickGui* gui, gui::GuiRenderer* renderer) noexcept {
    gui::widgets::render_section_header(gui, renderer, "Retained speed");
    s_mult_sword.render(gui, renderer);
    s_mult_bow.render(gui, renderer);
    s_mult_consumables.render(gui, renderer);
}

// Matches sub_180064020
void visit_config(config::ConfigArchive* ar) noexcept {
    s_mult_sword.visit(ar, "mult_sword");
    s_mult_bow.visit(ar, "mult_bow");
    s_mult_consumables.visit(ar, "mult_consumables");
}

consteval events::Module make_module() noexcept {
    events::Module mod{};
    mod.name = "No Slow";
    mod.description = "Removes slowdown while using items.";
    mod.category = events::ModuleCategory::Move;
    mod.events[events::kEventPreMoveWithHeading] = {&on_pre_move_with_heading, 200, 0};
    mod.render_settings = &render_settings;
    mod.visit_config = &visit_config;
    return mod;
}

constinit events::Module s_module = make_module();

} // namespace

events::Module* get_no_slow_module() noexcept {
    return &s_module;
}

} // namespace slinky::modules
