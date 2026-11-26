#include "modules/world/block_in.hpp"

#include <cmath>
#include <cstdint>

#include "config/settings.hpp"
#include "events/events.hpp"
#include "events/module_manager.hpp"
#include "gui/widgets.hpp"
#include "minecraft/game_helpers.hpp"
#include "minecraft/mc_wrappers.hpp"
#include "util/math.hpp"
#include "util/random.hpp"

namespace slinky::modules {

namespace {

struct BlockOffset3 {
    int8_t dx;
    int8_t dy;
    int8_t dz;
};

struct CardinalOffset2 {
    int8_t dx;
    int8_t dz;
};

// 10 roof & roof-support offsets relative to (base_x, feet_y - 1, base_z) (@ 0x180173A50)
constexpr BlockOffset3 kRoofCheckOffsets[10] = {
    { 0, 3,  0}, // [0] Roof block directly above head
    { 0, 3, -1}, // [1] North roof neighbor
    { 1, 3,  0}, // [2] East roof neighbor
    { 0, 3,  1}, // [3] South roof neighbor
    {-1, 3,  0}, // [4] West roof neighbor
    {-1, 3, -1}, // [5] NW diagonal roof support
    { 1, 3, -1}, // [6] NE diagonal roof support
    { 1, 3,  1}, // [7] SE diagonal roof support
    {-1, 3,  1}, // [8] SW diagonal roof support
    { 0, 4,  0}, // [9] Block directly above roof
};

// 4 cardinal wall offsets ordered clockwise from South (@ 0x180173A6E)
constexpr CardinalOffset2 kCardinalWalls[4] = {
    { 0,  1}, // 0: South (+Z)
    {-1,  0}, // 1: West  (-X)
    { 0, -1}, // 2: North (-Z)
    { 1,  0}, // 3: East  (+X)
};

// Replaceable block IDs in [0, 31]: Air (0), Flowing Water (8), Water (9), Tall Grass (31)
constexpr uint32_t kReplaceableBlockMask = 0x80000301u;

bool is_replaceable_block_id(int32_t block_id) noexcept {
    const auto id = static_cast<uint32_t>(block_id);
    return id <= 31u && ((kReplaceableBlockMask >> id) & 1u) != 0u;
}

bool is_centered_in_block(const math::Vec3d& pos) noexcept {
    const double frac_x = std::fabs(std::fmod(pos.x, 1.0));
    if (frac_x < 0.31 || frac_x > 0.69) {
        return false;
    }
    const double frac_z = std::fabs(std::fmod(pos.z, 1.0));
    return frac_z >= 0.31 && frac_z <= 0.69;
}

void on_game_tick(void* event) noexcept;
void on_module_state_changed(void* event) noexcept;
void on_rotation_spoof_query(void* event) noexcept;
void on_pre_living_update(void* event) noexcept;
void on_post_living_update(void* event) noexcept;
void render_settings(gui::ClickGui* gui, gui::GuiRenderer* renderer) noexcept;
void visit_config(config::ConfigArchive* archive) noexcept;

enum class CenterMoveMode : int32_t {
    None       = 0,
    WalkCenter = 1,
    StopKeys   = 2,
};

struct BlockInState {
    events::Module base{};

    // Settings (@ 0x1803134F8 .. 0x18031354F)
    config::BoolSetting require_ground{{}, "On ground", false, false, {}};
    config::FloatSetting speed{{}, "Speed", "%.0f%%", 0.0f, 100.0f, 0.3f, 1.0f, 0.5f, 0.5f};

    // Runtime state (@ 0x180321D80 .. 0x180321E08)
    uint64_t placed_mask = 0;
    int32_t start_wall_index = 0;
    bool spoof_rotation_active = false;
    CenterMoveMode movement_mode = CenterMoveMode::None;
    math::Vec2f center_walk_rot{0.0f, 0.0f};
    double last_player_y = 0.0;
    random::DriftNoise roof_drift_a{};
    random::DriftNoise roof_drift_b{};
    random::DriftNoise wall_x_drift{};
    random::DriftNoise wall_z_drift{};
    random::DriftNoise yaw_speed_drift{};
    random::DriftNoise pitch_speed_drift{};

    BlockInState() noexcept {
        base.name = "Block In";
        base.description = "Places blocks around you.";
        base.category = events::ModuleCategory::Block;
        base.enabled = false;
        base.flags = events::kModuleFlagNoToggleSwitch | events::kModuleFlagHiddenFromHud;
        base.keybind = 0;
        base.hold_mode = false;
        base.events[events::kEventGameTick] = {on_game_tick, 0, 0};
        base.events[events::kEventModuleStateChanged] = {on_module_state_changed, 0, 0};
        base.events[events::kEventRotationSpoofQuery] = {on_rotation_spoof_query, -2000, 0};
        base.events[events::kEventPreLivingUpdate] = {on_pre_living_update, -1000, 0};
        base.events[events::kEventPostLivingUpdate] = {on_post_living_update, -500, 0};
        base.render_settings = render_settings;
        base.visit_config = visit_config;
        base.format_hud_tag = nullptr;
    }
};

BlockInState g_block_in{};

// Scores defensive hardness of placeable blocks for Block In: Obsidian > Wood/EndStone/Cobble > Clay/Stone/Glass > Wool (sub_18006DAD0)
int score_block_in_item(int item_id) noexcept {
    switch (item_id) {
        case 49: // Obsidian
            return 4;

        case 4:   // Cobblestone
        case 5:   // Planks
        case 17:  // Log
        case 121: // End Stone
        case 162: // Log2
            return 3;

        case 1:   // Stone
        case 2:   // Grass
        case 3:   // Dirt
        case 20:  // Glass
        case 24:  // Sandstone
        case 45:  // Brick
        case 87:  // Netherrack
        case 159: // Stained Hardened Clay
            return 2;

        case 35:  // Wool
            return 1;

        default:
            return -1;
    }
}

// Intentional empty game-tick listener registered in slot 0 (sub_18006CA40)
void on_game_tick(void* /*event*/) noexcept {
}

// Initializes starting wall quadrant on enable and restores physical WASD states on disable (sub_18006CA50)
void on_module_state_changed(void* event) noexcept {
    auto* ev = static_cast<events::ModuleStateChangedEvent*>(event);
    if (ev->module != &g_block_in.base) {
        return;
    }

    events::GameContext* ctx = ev->ctx;
    events::JniContext* jni = ctx->jni;
    if (ev->enabled) {
        g_block_in.placed_mask = 0;
        const float yaw = mc::entity_get_rotation(jni, ctx->local_player).x;
        const int32_t quadrant = static_cast<int32_t>(std::round(yaw / 90.0f));
        g_block_in.start_wall_index = quadrant % 4;
    } else {
        jobject settings = mc::mc_get_game_settings(jni, ctx->minecraft);
        jobject fwd_key = mc::settings_get_key_forward(jni, settings);
        jobject back_key = mc::settings_get_key_back(jni, settings);
        jobject left_key = mc::settings_get_key_left(jni, settings);
        jobject right_key = mc::settings_get_key_right(jni, settings);

        mc::key_binding_set_pressed(jni, fwd_key, mc::key_binding_is_physically_down(jni, fwd_key));
        mc::key_binding_set_pressed(jni, back_key, mc::key_binding_is_physically_down(jni, back_key));
        mc::key_binding_set_pressed(jni, left_key, mc::key_binding_is_physically_down(jni, left_key));
        mc::key_binding_set_pressed(jni, right_key, mc::key_binding_is_physically_down(jni, right_key));
    }
}

// Spoofs silent rotation and steers move-fix toward block center when centering (sub_18006CBB0)
void on_rotation_spoof_query(void* event) noexcept {
    if (g_block_in.spoof_rotation_active) {
        auto* ev = static_cast<events::RotationSpoofEvent*>(event);
        ev->active = true;
        ev->move_fix = true;
        if (g_block_in.movement_mode == CenterMoveMode::WalkCenter) {
            ev->rotation = g_block_in.center_walk_rot;
        }
    }
}

// Overrides WASD keys to walk toward block center or hold still while placing (sub_18006CBE0)
void on_pre_living_update(void* event) noexcept {
    if (g_block_in.movement_mode == CenterMoveMode::None) {
        return;
    }

    events::GameContext* ctx = static_cast<events::ContextEvent*>(event)->ctx;
    events::JniContext* jni = ctx->jni;
    jobject settings = mc::mc_get_game_settings(jni, ctx->minecraft);
    jobject fwd_key = mc::settings_get_key_forward(jni, settings);
    jobject back_key = mc::settings_get_key_back(jni, settings);
    jobject left_key = mc::settings_get_key_left(jni, settings);
    jobject right_key = mc::settings_get_key_right(jni, settings);

    mc::key_binding_set_pressed(jni, fwd_key, g_block_in.movement_mode == CenterMoveMode::WalkCenter);
    mc::key_binding_set_pressed(jni, back_key, false);
    mc::key_binding_set_pressed(jni, left_key, false);
    mc::key_binding_set_pressed(jni, right_key, false);
}

// Executes roof + 4-wall block-in sequence and centers player inside current block (sub_18006CCB0)
void on_post_living_update(void* event) noexcept {
    g_block_in.spoof_rotation_active = false;
    g_block_in.movement_mode = CenterMoveMode::None;

    events::GameContext* ctx = static_cast<events::ContextEvent*>(event)->ctx;
    events::JniContext* jni = ctx->jni;

    const auto disable_self = [&]() noexcept {
        ctx->module_manager->request_set_enabled(&g_block_in.base, false);
    };

    if (!ctx->in_game_focus) {
        disable_self();
        return;
    }

    jobject player = ctx->local_player;
    const bool on_ground = mc::entity_is_on_ground(jni, player);
    if (g_block_in.require_ground.value && !on_ground) {
        disable_self();
        return;
    }

    math::Vec3d player_pos = mc::entity_get_position(jni, player);
    if (mc::is_version_1_7_10(jni)) {
        player_pos = math::vec3_sub_eye_height(player_pos);
    }

    const int32_t base_x = static_cast<int32_t>(std::floor(player_pos.x));
    const int32_t base_y = static_cast<int32_t>(std::floor(player_pos.y) - 1.0);
    const int32_t base_z = static_cast<int32_t>(std::floor(player_pos.z));

    if (std::fabs(player_pos.y - g_block_in.last_player_y) > 0.01) {
        g_block_in.placed_mask = 0;
    }
    g_block_in.last_player_y = player_pos.y;

    jobject mutable_pos = (jni->mappings->version != 0) ? mc::block_pos_new(jni, 0, 0, 0) : nullptr;

    bool has_target = false;
    math::Vec3d aim_target{};
    uint8_t placed_bit_base = 0;
    uint8_t placed_bit_index = 0;

    // 1. Check roof block (idx 0) and overhead/neighbor supports (idx 1..9)
    for (uint32_t idx = 0; idx < 10; ++idx) {
        const bool marked_placed = ((g_block_in.placed_mask >> idx) & (uint64_t{1} << 32)) != 0;
        bool is_solid = marked_placed;
        if (!is_solid) {
            const BlockOffset3& off = kRoofCheckOffsets[idx];
            jobject blk = mc::world_get_block_mutable_pos(
                jni, ctx->world, mutable_pos, base_x + off.dx, base_y + off.dy, base_z + off.dz);
            is_solid = !is_replaceable_block_id(mc::block_get_id(jni, blk));
        }

        if (!is_solid) {
            continue;
        }

        if (idx == 0) {
            // Roof block (0, 3, 0) is already solid; proceed to 4-wall check
            break;
        }

        placed_bit_base = 32;
        placed_bit_index = (idx >= 5 && idx <= 8) ? static_cast<uint8_t>(idx - 4) : 0;

        switch (idx) {
            case 1:
            case 5: {
                const double y = static_cast<double>(
                    static_cast<float>(base_y) + g_block_in.roof_drift_a.sample_timed({3.6f, 3.8f}, 0.5));
                const double z = static_cast<double>(
                    static_cast<float>(base_z) + g_block_in.roof_drift_b.sample_timed({-0.1f, -0.2f}, 0.5));
                aim_target = math::make_vec3(static_cast<double>(base_x), y, z);
                break;
            }
            case 2:
            case 6: {
                const double x = static_cast<double>(
                    static_cast<float>(base_x) + g_block_in.roof_drift_a.sample_timed({1.1f, 1.2f}, 0.5));
                const double y = static_cast<double>(
                    static_cast<float>(base_y) + g_block_in.roof_drift_b.sample_timed({3.6f, 3.8f}, 0.5));
                aim_target = math::make_vec3(x, y, static_cast<double>(base_z));
                break;
            }
            case 3:
            case 7: {
                const double y = static_cast<double>(
                    static_cast<float>(base_y) + g_block_in.roof_drift_a.sample_timed({3.6f, 3.8f}, 0.5));
                const double z = static_cast<double>(
                    static_cast<float>(base_z) + g_block_in.roof_drift_b.sample_timed({1.1f, 1.2f}, 0.5));
                aim_target = math::make_vec3(static_cast<double>(base_x + 1), y, z);
                break;
            }
            case 4:
            case 8: {
                const double x = static_cast<double>(
                    static_cast<float>(base_x) + g_block_in.roof_drift_a.sample_timed({-0.1f, -0.2f}, 0.5));
                const double y = static_cast<double>(
                    static_cast<float>(base_y) + g_block_in.roof_drift_b.sample_timed({3.6f, 3.8f}, 0.5));
                aim_target = math::make_vec3(x, y, static_cast<double>(base_z + 1));
                break;
            }
            case 9: {
                const double x = static_cast<double>(
                    static_cast<float>(base_x) + g_block_in.roof_drift_a.sample_timed({0.4f, 0.6f}, 0.5));
                const double z = static_cast<double>(
                    static_cast<float>(base_z) + g_block_in.roof_drift_b.sample_timed({0.4f, 0.6f}, 0.5));
                aim_target = math::make_vec3(x, static_cast<double>(base_y + 4), z);
                break;
            }
            default:
                break;
        }

        has_target = true;
        break;
    }

    // 2. Check the 4 cardinal walls around the player (levels: floor support, lower wall, upper wall)
    if (!has_target) {
        for (int32_t step = 0; step < 4; ++step) {
            const uint32_t dir = static_cast<uint32_t>(g_block_in.start_wall_index + step) & 3u;
            const CardinalOffset2 wall = kCardinalWalls[dir];
            const int32_t wx = base_x + wall.dx;
            const int32_t wz = base_z + wall.dz;

            const math::AABB player_box = mc::aabb_to_aabb(jni, mc::entity_get_bounding_box(jni, player));
            const math::Vec3d col_min = math::make_vec3(
                static_cast<double>(wx), static_cast<double>(base_y), static_cast<double>(wz));
            const math::Vec3d col_max = math::vec3_add(col_min, math::make_vec3(1.0, 3.0, 1.0));
            const math::AABB col_box = math::make_aabb(col_min, col_max);
            if (math::aabb_intersects(player_box, col_box)) {
                continue;
            }

            const uint32_t base_bit = dir * 3u;
            int32_t target_level = -1;
            math::Range2f drift_range{};

            // Level 0: Floor support at (wx, base_y, wz)
            if ((g_block_in.placed_mask & (uint64_t{1} << base_bit)) == 0) {
                jobject blk_cur = mc::world_get_block_mutable_pos(jni, ctx->world, mutable_pos, wx, base_y, wz);
                if (is_replaceable_block_id(mc::block_get_id(jni, blk_cur))) {
                    jobject blk_below =
                        mc::world_get_block_mutable_pos(jni, ctx->world, mutable_pos, wx, base_y - 1, wz);
                    if (!is_replaceable_block_id(mc::block_get_id(jni, blk_below))) {
                        target_level = 0;
                        placed_bit_index = static_cast<uint8_t>(base_bit);
                        drift_range = {-0.25f, -0.4f};
                    }
                }
            }

            // Level 1: Lower wall block at (wx, base_y + 1, wz)
            if (target_level == -1 && (g_block_in.placed_mask & (uint64_t{1} << (base_bit + 1u))) == 0) {
                jobject blk_cur = mc::world_get_block_mutable_pos(jni, ctx->world, mutable_pos, wx, base_y + 1, wz);
                if (is_replaceable_block_id(mc::block_get_id(jni, blk_cur))) {
                    jobject blk_below = mc::world_get_block_mutable_pos(jni, ctx->world, mutable_pos, wx, base_y, wz);
                    if (!is_replaceable_block_id(mc::block_get_id(jni, blk_below))) {
                        target_level = 1;
                        placed_bit_index = static_cast<uint8_t>(base_bit + 1u);
                        drift_range = {0.25f, 0.4f};
                    }
                }
            }

            // Level 2: Upper wall block at (wx, base_y + 2, wz)
            if (target_level == -1 && (g_block_in.placed_mask & (uint64_t{1} << (base_bit + 2u))) == 0) {
                jobject blk_cur = mc::world_get_block_mutable_pos(jni, ctx->world, mutable_pos, wx, base_y + 2, wz);
                if (is_replaceable_block_id(mc::block_get_id(jni, blk_cur))) {
                    jobject blk_below =
                        mc::world_get_block_mutable_pos(jni, ctx->world, mutable_pos, wx, base_y + 1, wz);
                    if (!is_replaceable_block_id(mc::block_get_id(jni, blk_below))) {
                        target_level = 2;
                        placed_bit_index = static_cast<uint8_t>(base_bit + 2u);
                        drift_range = {0.25f, 0.4f};
                    }
                }
            }

            if (target_level != -1) {
                const double x_off = 0.5 - static_cast<double>(
                    g_block_in.wall_x_drift.sample_timed(drift_range, 0.5) * static_cast<float>(wall.dx));
                const double z_off = 0.5 - static_cast<double>(
                    g_block_in.wall_z_drift.sample_timed(drift_range, 0.5) * static_cast<float>(wall.dz));
                aim_target = math::make_vec3(
                    static_cast<double>(wx) + x_off,
                    static_cast<double>(base_y + target_level),
                    static_cast<double>(wz) + z_off);
                placed_bit_base = 0;
                has_target = true;
                break;
            }
        }
    }

    if (!has_target) {
        if (on_ground && is_centered_in_block(player_pos)) {
            disable_self();
        }
        return;
    }

    if (!mc::try_claim_aim_priority(500u)) {
        disable_self();
        return;
    }

    mc::select_hotbar_slot_by_scorer(ctx, &g_block_in.base, true, score_block_in_item);
    jobject held_item = mc::player_get_held_item(jni, player);
    if (!mc::is_block_item(jni, held_item)) {
        disable_self();
        return;
    }

    const math::Vec2f cur_rot = math::vec2_add(
        mc::entity_get_rotation(jni, player),
        mc::get_pending_aim_rotation_delta());
    const math::Vec3d eye_pos = mc::vec3_to_vec3d(jni, mc::living_get_position_eyes(jni, player));
    const math::Vec2f desired_rot = math::calc_yaw_pitch(eye_pos, aim_target);
    const math::Vec2f raw_diff = math::vec2_sub(desired_rot, cur_rot);
    const float yaw_diff = math::wrap_degrees_180(raw_diff.x);
    const float pitch_diff = raw_diff.y;

    const float prop_factor = static_cast<float>(static_cast<double>(g_block_in.speed.value) * 0.5 + 0.5);
    const math::Vec2f prop_step = math::vec2_scale({yaw_diff, pitch_diff}, prop_factor);

    const float max_yaw_step =
        g_block_in.yaw_speed_drift.sample_timed({0.8f, 1.2f}, 0.2) * 180.0f * g_block_in.speed.value;
    const float max_pitch_step =
        g_block_in.pitch_speed_drift.sample_timed({0.8f, 1.2f}, 0.2) * 180.0f * g_block_in.speed.value;
    const math::Vec2f clamped_step{
        math::clampf(-max_yaw_step, max_yaw_step, yaw_diff),
        math::clampf(-max_pitch_step, max_pitch_step, pitch_diff),
    };

    const math::Vec2f final_step = math::vec2_lerp(prop_step, clamped_step, 0.5f);
    mc::queue_aim_rotation_delta(final_step);

    const float rem_yaw = std::fabs(yaw_diff - final_step.x);
    const float rem_pitch = std::fabs(pitch_diff - final_step.y);
    if ((rem_yaw + rem_pitch) < 1.0f) {
        jobject settings = mc::mc_get_game_settings(jni, ctx->minecraft);
        jobject use_key = mc::settings_get_key_use_item(jni, settings);
        mc::key_binding_increment_press_time(jni, use_key);
        g_block_in.placed_mask |= (uint64_t{1} << static_cast<uint32_t>(placed_bit_base + placed_bit_index));
    }

    g_block_in.spoof_rotation_active = true;
    if (is_centered_in_block(player_pos)) {
        g_block_in.movement_mode = CenterMoveMode::StopKeys;
    } else {
        const math::Vec3d block_center = math::make_vec3(
            std::floor(player_pos.x) + 0.5,
            player_pos.y,
            std::floor(player_pos.z) + 0.5);
        g_block_in.movement_mode = CenterMoveMode::WalkCenter;
        g_block_in.center_walk_rot = math::calc_yaw_pitch(player_pos, block_center);
    }
}

// Renders Block In settings in ClickGUI (sub_18006DA30)
void render_settings(gui::ClickGui* gui, gui::GuiRenderer* renderer) noexcept {
    gui::widgets::render_section_header(gui, renderer, "Main");
    g_block_in.speed.render(gui, renderer);

    gui::widgets::render_section_header(gui, renderer, "Conditions");
    g_block_in.require_ground.render(gui, renderer);
}

// Serializes/deserializes Block In config (sub_18006DA90)
void visit_config(config::ConfigArchive* archive) noexcept {
    g_block_in.speed.visit(archive, "speed");
    g_block_in.require_ground.visit(archive, "require_ground");
}

} // namespace

events::Module* get_block_in_module() noexcept {
    return &g_block_in.base;
}

} // namespace slinky::modules
