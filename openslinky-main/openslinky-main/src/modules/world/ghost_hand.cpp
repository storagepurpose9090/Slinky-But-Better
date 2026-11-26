#include "modules/world/ghost_hand.hpp"

#include <cstdint>

#include "config/settings.hpp"
#include "events/events.hpp"
#include "gui/widgets.hpp"
#include "minecraft/game_helpers.hpp"
#include "minecraft/mc_wrappers.hpp"
#include "modules/module_interop.hpp"

namespace slinky::modules {

namespace {

void on_reach_raycast_query(void* event) noexcept;
void render_settings(gui::ClickGui* gui, gui::GuiRenderer* renderer) noexcept;
void visit_config(config::ConfigArchive* archive) noexcept;

struct GhostHandState {
    events::Module base{};

    // Settings (@ 0x180311BD0 .. 0x180311D4F)
    config::BoolSetting require_mouse_pressed{{}, "Mouse pressed", false, false, {}};
    config::BoolSetting allow_using_fists{{}, "Fists", true, true, {}};
    config::BoolSetting allow_using_sword{{}, "Sword", true, true, {}};
    config::BoolSetting allow_using_tools{{}, "Tool", true, true, {}};
    config::BoolSetting allow_using_buckets{{}, "Bucket", true, true, {}};
    config::BoolSetting allow_using_fns{{}, "Flint and steel", true, true, {}};
    config::BoolSetting allow_using_cobweb{{}, "Cobweb", true, true, {}};
    config::BoolSetting allow_using_other{{}, "Other", true, true, {}};
    config::BoolSetting non_players{{}, "Non-player entities", true, true, {}};
    config::BoolSetting bots{{}, "Bots", true, true, {}};
    config::BoolSetting friendlies{{}, "Friendlies", true, true, {}};
    config::BoolSetting enemies{{}, "Enemies", true, true, {}};

    GhostHandState() noexcept {
        base.name = "Ghost Hand";
        base.description = "Allows interacting with blocks through entities.";
        base.category = events::ModuleCategory::Block;
        base.enabled = false;
        base.flags = events::kModuleFlagNone;
        base.keybind = 0;
        base.hold_mode = false;
        base.events[events::kEventReachRaycastQuery] = {on_reach_raycast_query, 0, 0};
        base.render_settings = render_settings;
        base.visit_config = visit_config;
        base.format_hud_tag = nullptr;
    }
};

GhostHandState g_ghost_hand{};

// Requests custom entity/block raycast override when Ghost Hand is enabled (sub_1800683F0)
void on_reach_raycast_query(void* event) noexcept {
    static_cast<events::ReachRaycastQueryEvent*>(event)->override_raycast = true;
}

// Renders Ghost Hand settings in ClickGUI (sub_180068400)
void render_settings(gui::ClickGui* gui, gui::GuiRenderer* renderer) noexcept {
    gui::widgets::render_section_header(gui, renderer, "Interact through");
    g_ghost_hand.non_players.render(gui, renderer);
    g_ghost_hand.friendlies.render(gui, renderer);
    g_ghost_hand.bots.render(gui, renderer);
    g_ghost_hand.enemies.render(gui, renderer);

    gui::widgets::render_section_header(gui, renderer, "Allow while using");
    g_ghost_hand.allow_using_sword.render(gui, renderer);
    g_ghost_hand.allow_using_tools.render(gui, renderer);
    g_ghost_hand.allow_using_fists.render(gui, renderer);
    g_ghost_hand.allow_using_buckets.render(gui, renderer);
    g_ghost_hand.allow_using_fns.render(gui, renderer);
    g_ghost_hand.allow_using_cobweb.render(gui, renderer);
    g_ghost_hand.allow_using_other.render(gui, renderer);

    gui::widgets::render_section_header(gui, renderer, "Conditions");
    g_ghost_hand.require_mouse_pressed.render(gui, renderer);
}

// Serializes/deserializes Ghost Hand config (sub_180068530)
void visit_config(config::ConfigArchive* archive) noexcept {
    g_ghost_hand.non_players.visit(archive, "non_players");
    g_ghost_hand.friendlies.visit(archive, "friendlies");
    g_ghost_hand.bots.visit(archive, "bots");
    g_ghost_hand.enemies.visit(archive, "enemies");
    g_ghost_hand.allow_using_sword.visit(archive, "allow_using_sword");
    g_ghost_hand.allow_using_tools.visit(archive, "allow_using_tools");
    g_ghost_hand.allow_using_fists.visit(archive, "allow_using_fists");
    g_ghost_hand.allow_using_buckets.visit(archive, "allow_using_buckets");
    g_ghost_hand.allow_using_fns.visit(archive, "allow_using_fns");
    g_ghost_hand.allow_using_cobweb.visit(archive, "allow_using_cobweb");
    g_ghost_hand.allow_using_other.visit(archive, "allow_using_other");
    g_ghost_hand.require_mouse_pressed.visit(archive, "require_mouse_pressed");
}

} // namespace

// Returns true if Ghost Hand should ignore `entity` during custom raycast (sub_1800682D0)
bool ghost_hand_should_ignore_entity(events::GameContext* ctx, jobject entity) noexcept {
    if (!g_ghost_hand.base.enabled) {
        return false;
    }
    if (g_ghost_hand.require_mouse_pressed.value && !mc::is_attack_key_down(ctx)) {
        return false;
    }

    events::JniContext* jni = ctx->jni;
    jobject held_item = mc::player_get_held_item(jni, ctx->local_player);

    bool item_allowed = false;
    if (held_item == nullptr) {
        item_allowed = g_ghost_hand.allow_using_fists.value;
    } else {
        switch (mc::item_get_id(jni, held_item)) {
            // Shovels, pickaxes, axes (iron, wood, stone, diamond, gold) + shears (359)
            case 256:
            case 257:
            case 258:
            case 269:
            case 270:
            case 271:
            case 273:
            case 274:
            case 275:
            case 277:
            case 278:
            case 279:
            case 284:
            case 285:
            case 286:
            case 359:
                item_allowed = g_ghost_hand.allow_using_tools.value;
                break;

            // Flint and steel (259)
            case 259:
                item_allowed = g_ghost_hand.allow_using_fns.value;
                break;

            // Swords (iron, wood, stone, diamond, gold)
            case 267:
            case 268:
            case 272:
            case 276:
            case 283:
                item_allowed = g_ghost_hand.allow_using_sword.value;
                break;

            // Buckets (empty, water, lava)
            case 325:
            case 326:
            case 327:
                item_allowed = g_ghost_hand.allow_using_buckets.value;
                break;

            // Cobweb (30)
            case 30:
                item_allowed = g_ghost_hand.allow_using_cobweb.value;
                break;

            default:
                item_allowed = g_ghost_hand.allow_using_other.value;
                break;
        }
    }

    if (!item_allowed) {
        return false;
    }

    jobject other_player = mc::as_entity_other_player_mp(jni, entity);
    if (other_player == nullptr) {
        return g_ghost_hand.non_players.value;
    }

    const uint16_t tags = mc::player_get_tag_flags(jni, other_player);
    if ((tags & mc::kPlayerTagBot) != 0) {
        return g_ghost_hand.bots.value;
    }
    if ((tags & (mc::kPlayerTagFriend | mc::kPlayerTagTeammate)) != 0) {
        return g_ghost_hand.friendlies.value;
    }
    return g_ghost_hand.enemies.value;
}

events::Module* get_ghost_hand_module() noexcept {
    return &g_ghost_hand.base;
}

} // namespace slinky::modules
