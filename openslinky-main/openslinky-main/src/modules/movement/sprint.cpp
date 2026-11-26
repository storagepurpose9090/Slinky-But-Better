#include "modules/movement/sprint.hpp"

#include "config/settings.hpp"
#include "gui/widgets.hpp"
#include "minecraft/mc_wrappers.hpp"

namespace slinky::modules {

namespace {

consteval config::BoolSetting make_bool(const char* label, bool initial = false) noexcept {
    config::BoolSetting s{};
    s.label = label;
    s.value = initial;
    s.default_value = initial;
    return s;
}

constinit config::BoolSetting s_while_in_gui     = make_bool("In inventory"); // unk_18030AAC0
constinit config::BoolSetting s_while_using_item = make_bool("Using item");   // unk_18030AAE0
constinit config::BoolSetting s_while_backwards  = make_bool("Backwards");    // unk_18030AB00
constinit config::BoolSetting s_while_sideways   = make_bool("Sideways");     // unk_18030AB20

constinit bool s_was_holding_sprint = false; // byte_180319648

// Matches sub_1800574F0
void on_pre_living_update(void* raw_event) noexcept {
    const auto* ev = static_cast<const events::ContextEvent*>(raw_event);
    events::GameContext* ctx = ev->ctx;

    const bool should_sprint =
        s_while_in_gui.value ||
        (minecraft::mc_get_gui_container_screen(ctx->jni, ctx->minecraft) == nullptr);

    jobject settings = minecraft::mc_get_game_settings(ctx->jni, ctx->minecraft);
    jobject sprint_key = minecraft::settings_get_key_sprint(ctx->jni, settings);
    minecraft::key_binding_set_pressed(ctx->jni, sprint_key, should_sprint);

    jobject player = ctx->local_player;
    if (player != nullptr) {
        if (!should_sprint) {
            if (s_was_holding_sprint) {
                minecraft::entity_set_sprinting(ctx->jni, player, false);
            }
            return;
        }
        if (minecraft::entity_is_sprinting(ctx->jni, player)) {
            minecraft::entity_set_sprinting(ctx->jni, ctx->local_player, true);
        }
    }

    s_was_holding_sprint = should_sprint;
}

// Matches sub_1800575A0
void on_pre_move_with_heading(void* raw_event) noexcept {
    auto* ev = static_cast<events::MoveWithHeadingEvent*>(raw_event);
    events::GameContext* ctx = ev->ctx;

    if (ev->move_forward == 0.0f && ev->move_strafe == 0.0f) {
        return;
    }

    const bool using_item = minecraft::player_is_using_item(ctx->jni, ctx->local_player);
    if (using_item && !s_while_using_item.value) {
        return;
    }

    if (ev->move_forward < 0.0f) {
        if (!s_while_backwards.value) {
            return;
        }
    } else if (ev->move_forward == 0.0f && ev->move_strafe != 0.0f) {
        if (!s_while_sideways.value) {
            return;
        }
    } else if (!using_item) {
        return;
    }

    ev->friction = static_cast<float>(static_cast<double>(ev->friction) * 1.3);
    minecraft::entity_set_sprinting(ctx->jni, ctx->local_player, true);
}

// Matches sub_180057660
void render_settings(gui::ClickGui* gui, gui::GuiRenderer* renderer) noexcept {
    gui::widgets::render_section_header(gui, renderer, "Allow while");
    s_while_using_item.render(gui, renderer);
    s_while_backwards.render(gui, renderer);
    s_while_sideways.render(gui, renderer);
    s_while_in_gui.render(gui, renderer);
}

// Matches sub_1800576D0
void visit_config(config::ConfigArchive* ar) noexcept {
    s_while_using_item.visit(ar, "while_using_item");
    s_while_backwards.visit(ar, "while_backwards");
    s_while_sideways.visit(ar, "while_sideways");
    s_while_in_gui.visit(ar, "while_in_gui");
}

consteval events::Module make_module() noexcept {
    events::Module mod{};
    mod.name = "Sprint";
    mod.description = "Automatically sprints for you.";
    mod.category = events::ModuleCategory::Move;
    mod.events[events::kEventPreLivingUpdate] = {&on_pre_living_update, 100, 0};
    mod.events[events::kEventPreMoveWithHeading] = {&on_pre_move_with_heading, 100, 0};
    mod.render_settings = &render_settings;
    mod.visit_config = &visit_config;
    return mod;
}

constinit events::Module s_module = make_module();

} // namespace

events::Module* get_sprint_module() noexcept {
    return &s_module;
}

} // namespace slinky::modules
