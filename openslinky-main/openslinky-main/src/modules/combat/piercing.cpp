#include "modules/combat/piercing.hpp"

#include <cstdint>

#include "config/settings.hpp"
#include "gui/widgets.hpp"
#include "minecraft/game_helpers.hpp"
#include "minecraft/mc_wrappers.hpp"
#include "modules/module_interop.hpp"

namespace slinky::modules {

namespace {

constexpr const char* kSortByOptions[2] = {
    "Hurt time",
    "Health",
};

void on_reach_raycast_query(void* event) noexcept;
void render_settings(gui::ClickGui* gui, gui::GuiRenderer* renderer) noexcept;
void visit_config(config::ConfigArchive* ar) noexcept;

struct PiercingModule {
    events::Module base{};
    config::BoolSetting require_inside_bb{{}, "Inside entity hitbox", false, false};
    config::BoolSetting require_weapon{{}, "Holding weapon", false, false};
    config::BoolSetting non_players{{}, "Non-player entities", true, true};
    config::BoolSetting friendlies{{}, "Friendlies", true, true};
    config::BoolSetting bots{{}, "Bots", true, true};
    config::BoolSetting enemies{{}, "Enemies", true, true};
    config::ModeSetting sort_by{{}, "Sort targets by", 2, 0, kSortByOptions, 0, 0, false};
    config::BoolSetting blocks{{}, "Blocks", true, true};

    PiercingModule() noexcept {
        base.name = "Piercing";
        base.description = "Allows hitting entities through obstructions.";
        base.category = events::ModuleCategory::Combat;
        base.enabled = false;
        base.flags = events::kModuleFlagNone;
        base.keybind = 0;
        base.hold_mode = false;
        base.events[events::kEventReachRaycastQuery] = {&on_reach_raycast_query, 0};
        base.render_settings = &render_settings;
        base.visit_config = &visit_config;
        base.format_hud_tag = nullptr;
    }
};

PiercingModule s_mod{};

int32_t hash_player_tiebreaker(events::JniContext* jni, jobject player) noexcept {
    uint32_t x = static_cast<uint32_t>(
        mc::entity_get_ticks_existed(jni, player) + mc::entity_get_id(jni, player));
    x = ((x >> 16) ^ x) * 0x045D9F3Bu;
    x = ((x >> 16) ^ x) * 0x045D9F3Bu;
    x = (x >> 16) ^ x;
    return static_cast<int32_t>(x);
}

// sub_180066230
void on_reach_raycast_query(void* event) noexcept {
    auto* ev = static_cast<events::ReachRaycastQueryEvent*>(event);
    ev->override_raycast = true;
}

// sub_180066240
void render_settings(gui::ClickGui* gui, gui::GuiRenderer* renderer) noexcept {
    gui::widgets::render_section_header(gui, renderer, "Always hit through");
    s_mod.sort_by.render(gui, renderer);
    s_mod.non_players.render(gui, renderer);
    s_mod.friendlies.render(gui, renderer);
    s_mod.bots.render(gui, renderer);
    s_mod.enemies.render(gui, renderer);
    s_mod.blocks.render(gui, renderer);

    gui::widgets::render_section_header(gui, renderer, "Conditions");
    s_mod.require_weapon.render(gui, renderer);
    s_mod.require_inside_bb.render(gui, renderer);
}

// sub_180066310
void visit_config(config::ConfigArchive* ar) noexcept {
    s_mod.sort_by.visit(ar, "sort_by");
    s_mod.non_players.visit(ar, "non_players");
    s_mod.friendlies.visit(ar, "friendlies");
    s_mod.bots.visit(ar, "bots");
    s_mod.enemies.visit(ar, "enemies");
    s_mod.blocks.visit(ar, "blocks");
    s_mod.require_weapon.visit(ar, "require_weapon");
    s_mod.require_inside_bb.visit(ar, "require_inside_bb");
}

} // namespace

// sub_180065FE0
int32_t piercing_compare_targets(
    events::GameContext* ctx,
    jobject best_entity,
    jobject candidate_entity,
    bool inside_hitbox) noexcept
{
    if (!s_mod.base.enabled || (!inside_hitbox && s_mod.require_inside_bb.value)) {
        return 2;
    }
    if (s_mod.require_weapon.value && !weapons_is_holding_weapon_simple(ctx->jni, ctx->local_player)) {
        return 2;
    }

    jobject cand_player = mc::as_entity_other_player_mp(ctx->jni, candidate_entity);
    if (cand_player == nullptr) {
        return s_mod.non_players.value ? 1 : 2;
    }

    const uint16_t cand_tags = mc::player_get_tag_flags(ctx->jni, cand_player);
    if ((s_mod.friendlies.value && (cand_tags & (mc::kPlayerTagFriend | mc::kPlayerTagTeammate)) != 0) ||
        (s_mod.bots.value && (cand_tags & mc::kPlayerTagBot) != 0))
    {
        return 1;
    }

    jobject best_player = mc::as_entity_other_player_mp(ctx->jni, best_entity);
    if (best_player == nullptr) {
        return 0;
    }

    if (!s_mod.enemies.value) {
        return 2;
    }

    if (s_mod.sort_by.value == 1) {
        const float best_hp = mc::living_get_health(ctx->jni, best_player);
        const float cand_hp = mc::living_get_health(ctx->jni, cand_player);
        if (cand_hp > best_hp) {
            return 1;
        }
        if (best_hp > cand_hp) {
            return 0;
        }
    } else if (s_mod.sort_by.value == 0) {
        const int32_t best_hurt = mc::living_get_hurt_time(ctx->jni, best_player);
        const int32_t cand_hurt = mc::living_get_hurt_time(ctx->jni, cand_player);
        if (cand_hurt > best_hurt) {
            return 1;
        }
        if (cand_hurt < best_hurt) {
            return 0;
        }
    }

    const int32_t best_hash = hash_player_tiebreaker(ctx->jni, best_player);
    const int32_t cand_hash = hash_player_tiebreaker(ctx->jni, cand_player);
    return cand_hash <= best_hash ? 1 : 0;
}

// sub_1800661E0
bool piercing_should_ignore_blocks(events::GameContext* ctx) noexcept {
    if (s_mod.base.enabled && s_mod.blocks.value) {
        return !s_mod.require_weapon.value ||
               weapons_is_holding_weapon_simple(ctx->jni, ctx->local_player);
    }
    return false;
}

events::Module* get_piercing_module() noexcept {
    return &s_mod.base;
}

} // namespace slinky::modules
