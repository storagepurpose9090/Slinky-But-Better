#include "modules/world/bridge_assist.hpp"

#include <cmath>
#include <cstdint>

#include "config/settings.hpp"
#include "events/events.hpp"
#include "gui/widgets.hpp"
#include "minecraft/game_helpers.hpp"
#include "minecraft/mc_wrappers.hpp"
#include "util/math.hpp"
#include "util/random.hpp"

namespace slinky::modules {

namespace {

constexpr const char* kSelectBlocksOptions[3] = {
    "No",
    "On depletion",
    "Always",
};

void on_module_state_changed(void* event) noexcept;
void on_pre_attack_or_use(void* event) noexcept;
void on_mid_living_update(void* event) noexcept;
void on_post_living_update(void* event) noexcept;
void render_settings(gui::ClickGui* gui, gui::GuiRenderer* renderer) noexcept;
void visit_config(config::ConfigArchive* archive) noexcept;

struct BridgeAssistState {
    events::Module base{};

    // Settings (@ 0x180306CD0 .. 0x180306E77)
    config::BoolSetting require_sneak{{}, "Sneak key pressed", false, false, {}};
    config::BoolSetting require_looking_down{{}, "Looking down", false, false, {}};
    config::BoolSetting require_not_forward{{}, "Not moving forward", false, false, {}};
    config::ModeSetting select_blocks{{}, "Select blocks", 3, 0, kSelectBlocksOptions, 1, 1, false, {}};
    config::BoolSetting require_holding_blocks{{}, "Holding blocks", false, false, {}};
    config::BoolSetting sneak_on_jump{{}, "Sneak on jump", false, false, {}};
    config::FloatSetting edge_offset{{}, "Edge offset", "%.2f blocks", 0.01f, 0.0f, 0.0f, 0.2999f, 0.2f, 0.2f};
    config::BoolSetting randomize{{}, "Randomize", true, true, {}};
    config::BoolSetting avoid_double_sneak{{}, "Avoid double-sneaking", false, false, {}};
    config::FloatSetting unsneak_delay{{}, "Unsneak delay", "%.0f ms", 0.1f, 50.0f, 1.0f, 6.0f, 3.0f, 3.0f};

    // Runtime state (@ 0x180314FB8 .. 0x180315000)
    bool sneak_overridden = false;
    int32_t recent_place_ticks = 0;
    bool avoid_double_sneak_active = false;
    bool should_select_blocks = false;
    float unsneak_timer = 0.0f;
    int32_t holding_block_grace_ticks = 0;
    random::DriftNoise edge_offset_drift{};
    random::DriftNoise unsneak_delay_drift{};
    random::DriftNoise unsneak_spike_gate{};

    BridgeAssistState() noexcept {
        base.name = "Bridge Assist";
        base.description = "Sneaks and unsneaks for you while bridging.";
        base.category = events::ModuleCategory::Block;
        base.enabled = false;
        base.flags = events::kModuleFlagNone;
        base.keybind = 0;
        base.hold_mode = false;
        base.events[events::kEventModuleStateChanged] = {on_module_state_changed, 0, 0};
        base.events[events::kEventPreAttackOrUseEntity] = {on_pre_attack_or_use, 0, 0};
        base.events[events::kEventMidLivingUpdate] = {on_mid_living_update, 0, 0};
        base.events[events::kEventPostLivingUpdate] = {on_post_living_update, 0, 0};
        base.render_settings = render_settings;
        base.visit_config = visit_config;
        base.format_hud_tag = nullptr;
    }
};

BridgeAssistState g_bridge_assist{};

// Predicts next-tick foot position and returns true if all overlapping support blocks are air (sub_18004FE40)
bool is_over_air_edge(
    events::JniContext* jni,
    jobject settings,
    jobject world,
    jobject player) noexcept
{
    jobject bbox_obj = mc::entity_get_bounding_box(jni, player);
    const math::AABB bbox = mc::aabb_to_aabb(jni, bbox_obj);
    const math::Vec3d feet_center = math::make_vec3(
        (bbox.min.x + bbox.max.x) * 0.5,
        bbox.min.y - 0.01,
        (bbox.min.z + bbox.max.z) * 0.5);

    const math::Vec3d motion = mc::entity_get_motion(jni, player);
    double offset = static_cast<double>(g_bridge_assist.edge_offset.value);
    if (g_bridge_assist.randomize.value) {
        const float drift = g_bridge_assist.edge_offset_drift.sample_timed({0.0f, 0.15f}, 5.0);
        offset -= static_cast<double>(drift);
        if (offset < 0.0) {
            offset = 0.0;
        }
    }

    jobject fwd_key = mc::settings_get_key_forward(jni, settings);
    jobject back_key = mc::settings_get_key_back(jni, settings);
    jobject left_key = mc::settings_get_key_left(jni, settings);
    jobject right_key = mc::settings_get_key_right(jni, settings);

    float move_forward = mc::key_binding_is_pressed(jni, fwd_key) ? 1.0f : 0.0f;
    if (mc::key_binding_is_pressed(jni, back_key)) {
        move_forward -= 1.0f;
    }
    float move_strafe = mc::key_binding_is_pressed(jni, left_key) ? 1.0f : 0.0f;
    if (mc::key_binding_is_pressed(jni, right_key)) {
        move_strafe -= 1.0f;
    }

    const math::Vec2f rot = mc::entity_get_rotation(jni, player);
    const float land_factor = mc::living_get_land_movement_factor(jni, player);
    const math::Vec3d strafe_delta = mc::compute_strafe_motion(rot, move_forward, move_strafe, land_factor);
    const math::Vec3d next_vel = math::vec3_add(motion, strafe_delta);
    const math::Vec3d pred_pos = math::vec3_add(feet_center, next_vel);

    const int32_t min_x = static_cast<int32_t>(std::floor(pred_pos.x - offset));
    const int32_t min_z = static_cast<int32_t>(std::floor(pred_pos.z - offset));
    const int32_t block_y = static_cast<int32_t>(std::floor(pred_pos.y));
    const int32_t max_x = static_cast<int32_t>(std::floor(pred_pos.x + offset));
    const int32_t max_z = static_cast<int32_t>(std::floor(pred_pos.z + offset));

    if (mc::block_get_id(jni, mc::world_get_block(jni, world, min_x, block_y, min_z)) != 0) {
        return false;
    }
    if (min_z != max_z &&
        mc::block_get_id(jni, mc::world_get_block(jni, world, min_x, block_y, max_z)) != 0) {
        return false;
    }
    if (min_x == max_x) {
        return true;
    }
    if (mc::block_get_id(jni, mc::world_get_block(jni, world, max_x, block_y, min_z)) != 0) {
        return false;
    }
    if (min_z == max_z) {
        return true;
    }
    return mc::block_get_id(jni, mc::world_get_block(jni, world, max_x, block_y, max_z)) == 0;
}

// Restores physical sneak key state on module disable (sub_18004F4B0)
void on_module_state_changed(void* event) noexcept {
    auto* ev = static_cast<events::ModuleStateChangedEvent*>(event);
    if (ev->module == &g_bridge_assist.base && !ev->enabled && g_bridge_assist.sneak_overridden) {
        events::GameContext* ctx = ev->ctx;
        jobject settings = mc::mc_get_game_settings(ctx->jni, ctx->minecraft);
        jobject sneak_key = mc::settings_get_key_sneak(ctx->jni, settings);
        const bool phys_down = mc::key_binding_is_physically_down(ctx->jni, sneak_key);
        mc::key_binding_set_pressed(ctx->jni, sneak_key, phys_down);
        g_bridge_assist.sneak_overridden = false;
    }
}

// Suppresses diagonal side-extension misplacements and tracks recent block placements (sub_18004F530)
void on_pre_attack_or_use(void* event) noexcept {
    auto* ev = static_cast<events::AttackOrUseEntityEvent*>(event);
    if (ev->is_attack || g_bridge_assist.recent_place_ticks == 20) {
        return;
    }

    events::GameContext* ctx = ev->ctx;
    events::JniContext* jni = ctx->jni;

    const auto record_held_block_place = [&]() noexcept {
        jobject held = mc::player_get_held_item(jni, ctx->local_player);
        if (mc::is_block_item(jni, held)) {
            g_bridge_assist.recent_place_ticks = 20;
        }
    };

    if (!g_bridge_assist.avoid_double_sneak_active) {
        record_held_block_place();
        return;
    }

    jobject mop = mc::mc_get_object_mouse_over(jni, ctx->minecraft);
    if (mop == nullptr ||
        mc::mop_get_type_ordinal(jni, mop) != 1 ||
        static_cast<uint32_t>(mc::mop_get_side_hit(jni, mop)) < 2u ||
        !mc::entity_is_on_ground(jni, ctx->local_player)) {
        record_held_block_place();
        return;
    }

    jobject settings = mc::mc_get_game_settings(jni, ctx->minecraft);
    jobject back_key = mc::settings_get_key_back(jni, settings);
    jobject left_key = mc::settings_get_key_left(jni, settings);
    jobject right_key = mc::settings_get_key_right(jni, settings);
    if (!mc::key_binding_is_pressed(jni, back_key) ||
        mc::key_binding_is_pressed(jni, left_key) ||
        mc::key_binding_is_pressed(jni, right_key)) {
        record_held_block_place();
        return;
    }

    const math::Vec2f rot = mc::entity_get_rotation(jni, ctx->local_player);
    const float yaw_mod_90 = std::fabs(static_cast<float>(std::fmod(static_cast<double>(rot.x), 90.0)));
    if (yaw_mod_90 < 30.0f || yaw_mod_90 > 60.0f) {
        record_held_block_place();
        return;
    }

    math::Vec3d player_pos = mc::entity_get_position(jni, ctx->local_player);
    if (mc::is_version_1_7_10(jni)) {
        player_pos = math::vec3_sub_eye_height(player_pos);
    }

    const math::Vec3d hit_vec = mc::vec3_to_vec3d(jni, mc::mop_get_hit_vec(jni, mop));
    const double rel_y = hit_vec.y - player_pos.y;
    if (std::fabs(hit_vec.x - player_pos.x) > 1.0 ||
        rel_y < -1.0 ||
        rel_y > 0.0 ||
        std::fabs(hit_vec.z - player_pos.z) > 1.0) {
        record_held_block_place();
        return;
    }

    const math::Vec3d offset_pos = mc::mop_get_offset_block_pos_vec3d(jni, mop);
    const bool z_inside = (offset_pos.z < player_pos.z) && (player_pos.z < offset_pos.z + 1.0);
    const bool x_inside = (offset_pos.x < player_pos.x) && (player_pos.x < offset_pos.x + 1.0);
    const double margin = (x_inside && z_inside) ? 0.25 : 0.15;
    const double upper = 1.0 - margin;

    const double frac_x = std::fabs(std::fmod(hit_vec.x, 1.0));
    const double frac_z = std::fabs(std::fmod(hit_vec.z, 1.0));
    if ((frac_x >= margin && frac_x <= upper) || (frac_z >= margin && frac_z <= upper)) {
        ev->cancelled = true;
        return;
    }

    record_held_block_place();
}

// Updates automatic sneaking/unsneaking during EntityPlayerSP living update (sub_18004F8E0)
void on_mid_living_update(void* event) noexcept {
    g_bridge_assist.avoid_double_sneak_active = false;
    g_bridge_assist.should_select_blocks = false;

    events::GameContext* ctx = static_cast<events::ContextEvent*>(event)->ctx;
    if (!ctx->in_game_focus) {
        return;
    }

    events::JniContext* jni = ctx->jni;
    jobject settings = mc::mc_get_game_settings(jni, ctx->minecraft);
    if (g_bridge_assist.recent_place_ticks > 0) {
        --g_bridge_assist.recent_place_ticks;
    }

    jobject sneak_key = mc::settings_get_key_sneak(jni, settings);
    const bool phys_sneak = mc::key_binding_is_physically_down(jni, sneak_key);

    const auto release_sneak_override = [&]() noexcept {
        if (g_bridge_assist.sneak_overridden) {
            mc::key_binding_set_pressed(jni, sneak_key, phys_sneak);
        }
        g_bridge_assist.sneak_overridden = false;
    };

    if (g_bridge_assist.require_sneak.value != phys_sneak) {
        release_sneak_override();
        return;
    }

    jobject player = ctx->local_player;
    if (g_bridge_assist.require_looking_down.value) {
        if (mc::entity_get_rotation(jni, player).y < 65.0f) {
            release_sneak_override();
            return;
        }
    }

    if (g_bridge_assist.require_not_forward.value) {
        if (mc::living_get_move_forward(jni, player) > 0.0f) {
            release_sneak_override();
            return;
        }
    }

    g_bridge_assist.should_select_blocks = (g_bridge_assist.select_blocks.value != 0);

    if (g_bridge_assist.require_holding_blocks.value) {
        jobject held = mc::player_get_held_item(jni, player);
        if (mc::is_block_item(jni, held)) {
            g_bridge_assist.holding_block_grace_ticks = 2;
        } else if (g_bridge_assist.holding_block_grace_ticks > 0) {
            --g_bridge_assist.holding_block_grace_ticks;
        } else {
            release_sneak_override();
            return;
        }
    }

    g_bridge_assist.sneak_overridden = true;
    const bool currently_sneaking = mc::key_binding_is_pressed(jni, sneak_key);
    jobject world = ctx->world;

    if (currently_sneaking) {
        if (g_bridge_assist.require_sneak.value) {
            if (mc::living_get_move_forward(jni, player) >= 0.0f || g_bridge_assist.recent_place_ticks <= 0) {
                g_bridge_assist.avoid_double_sneak_active = g_bridge_assist.avoid_double_sneak.value;
                return;
            }
            if (is_over_air_edge(jni, settings, world, player)) {
                g_bridge_assist.avoid_double_sneak_active = g_bridge_assist.avoid_double_sneak.value;
                return;
            }
        } else {
            const bool can_keep_sneak =
                mc::entity_is_on_ground(jni, player) ||
                (g_bridge_assist.sneak_on_jump.value && mc::entity_get_motion(jni, player).y >= 0.0);
            if (can_keep_sneak && is_over_air_edge(jni, settings, world, player)) {
                g_bridge_assist.avoid_double_sneak_active = g_bridge_assist.avoid_double_sneak.value;
                return;
            }
        }

        g_bridge_assist.unsneak_timer -= 1.0f;
        if (g_bridge_assist.unsneak_timer > 0.0f) {
            g_bridge_assist.avoid_double_sneak_active = g_bridge_assist.avoid_double_sneak.value;
            return;
        }

        mc::key_binding_set_pressed(jni, sneak_key, false);
        float next_delay = g_bridge_assist.unsneak_delay.value;
        if (g_bridge_assist.randomize.value) {
            next_delay += g_bridge_assist.unsneak_delay_drift.sample_timed({-0.5f, 0.5f}, 30.0);
            if (g_bridge_assist.unsneak_spike_gate.sample_chance_timed({0.04f, 0.08f}, 60.0)) {
                next_delay += random::uniform_range({0.0f, 3.0f});
            }
        }
        g_bridge_assist.unsneak_timer += next_delay;
        return;
    }

    const bool can_engage_sneak =
        mc::entity_is_on_ground(jni, player) ||
        (g_bridge_assist.sneak_on_jump.value && mc::entity_get_motion(jni, player).y >= 0.0);
    if (can_engage_sneak && is_over_air_edge(jni, settings, world, player)) {
        mc::key_binding_set_pressed(jni, sneak_key, true);
        g_bridge_assist.avoid_double_sneak_active = g_bridge_assist.avoid_double_sneak.value;
    }
}

// Auto-selects blocks on depletion or continuously after living update (sub_18004FC30)
void on_post_living_update(void* event) noexcept {
    if (g_bridge_assist.should_select_blocks) {
        auto* ev = static_cast<events::ContextEvent*>(event);
        mc::select_hotbar_block_slot(ev->ctx, &g_bridge_assist.base, g_bridge_assist.select_blocks.value == 2);
    }
}

// Renders Bridge Assist settings in ClickGUI (sub_18004FC60)
void render_settings(gui::ClickGui* gui, gui::GuiRenderer* renderer) noexcept {
    gui::widgets::render_section_header(gui, renderer, "Main");
    g_bridge_assist.edge_offset.render(gui, renderer);
    g_bridge_assist.unsneak_delay.render(gui, renderer);
    g_bridge_assist.select_blocks.render(gui, renderer);
    g_bridge_assist.randomize.render(gui, renderer);
    g_bridge_assist.sneak_on_jump.render(gui, renderer);
    g_bridge_assist.avoid_double_sneak.render(gui, renderer);

    gui::widgets::render_section_header(gui, renderer, "Conditions");
    g_bridge_assist.require_sneak.render(gui, renderer);
    g_bridge_assist.require_holding_blocks.render(gui, renderer);
    g_bridge_assist.require_looking_down.render(gui, renderer);
    g_bridge_assist.require_not_forward.render(gui, renderer);
}

// Serializes/deserializes Bridge Assist config (sub_18004FD50)
void visit_config(config::ConfigArchive* archive) noexcept {
    g_bridge_assist.edge_offset.visit(archive, "edge_offset");
    g_bridge_assist.unsneak_delay.visit(archive, "unsneak_delay");
    g_bridge_assist.select_blocks.visit(archive, "select_blocks");
    g_bridge_assist.randomize.visit(archive, "randomize");
    g_bridge_assist.sneak_on_jump.visit(archive, "sneak_on_jump");
    g_bridge_assist.avoid_double_sneak.visit(archive, "avoid_double_sneak");
    g_bridge_assist.require_sneak.visit(archive, "require_sneak");
    g_bridge_assist.require_holding_blocks.visit(archive, "require_holding_blocks");
    g_bridge_assist.require_looking_down.visit(archive, "require_looking_down");
    g_bridge_assist.require_not_forward.visit(archive, "require_not_forward");
}

} // namespace

events::Module* get_bridge_assist_module() noexcept {
    return &g_bridge_assist.base;
}

} // namespace slinky::modules
