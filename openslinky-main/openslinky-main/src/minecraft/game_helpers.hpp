#pragma once

#include <cstddef>
#include <cstdint>
#include <jni.h>

#include "events/events.hpp"
#include "minecraft/item_database.hpp"
#include "minecraft/mappings.hpp"
#include "minecraft/mc_wrappers.hpp"
#include "util/containers.hpp"
#include "util/math.hpp"

namespace slinky {

namespace minecraft {

// Magic high-word sentinel encoded into EntityPlayer.speedOnGround for client-side entity tags
constexpr uint32_t kPlayerTagMagicPrefix = 0xB3150000u;
constexpr uint32_t kPlayerTagMagicMask   = 0xFFFF0000u;

enum PlayerTagFlags : uint32_t {
    kPlayerTagNone         = 0u,
    kPlayerTagTeammate     = 1u << 0, // Teams module (bit 0 = 1)
    kPlayerTagBot          = 1u << 1, // AntiBot module (bit 1 = 2)
    kPlayerTagFriend       = 1u << 2, // Friends module (bit 2 = 4)
    kPlayerTagExcludedMask = kPlayerTagTeammate | kPlayerTagBot | kPlayerTagFriend, // 0x07
};

// TargetTracker::flags bit definitions (sub_18004B5B0 & sub_18004B970)
enum TargetTrackerFlags : uint32_t {
    kTargetTrackerSlowdownEvaluated = 1u << 0,
    kTargetTrackerMovingAway        = 1u << 1,
};

using HotbarItemScoreFn = int (*)(int item_id);

// Shared AimAssist target GlobalRef (@ 0x180314DD0) & accumulated mouse delta (@ 0x180314E08)
extern jobject g_aimassist_target_entity;
extern math::Vec2f g_aimassist_mouse_delta;

// Static OpenGL client-array geometry tables (@ 0x18016C0A0..0x18016C188)
extern const float kUnitCubeVertices[24];
extern const uint8_t kUnitCubeStripIndices[14];
extern const uint8_t kUnitCubeLineIndices[24];
extern const float kUnitQuadVertices[8];
extern const uint8_t kUnitQuadStripIndices[4];
extern const uint8_t kUnitQuadLoopIndices[4];
extern const float kChevronVertices[8];
extern const uint8_t kChevronFanIndices[4];
extern const uint8_t kChevronLoopIndices[4];

// ============================================================================
// Item, Team Color, Raycast, Input, Chat & Movement Math Helpers
// ============================================================================

jobject player_get_held_item(events::JniContext* jni, jobject player) noexcept; // sub_180049F00
bool is_sword_item(events::JniContext* jni, jobject item) noexcept; // sub_180049F70
bool is_block_item(events::JniContext* jni, jobject item) noexcept; // sub_180049FB0
uint16_t extract_name_color_code_jstring(events::JniContext* jni, jstring text) noexcept; // sub_180049FF0
uint32_t extract_name_color_code_utf8(const char* text, uint32_t default_code, bool* out_found_word_char) noexcept; // sub_18004A0D0
uint16_t score_player_team_get_color_code(events::JniContext* jni, jobject team) noexcept; // sub_18004A1D0
uint16_t entity_get_team_color_code(events::JniContext* jni, jobject entity) noexcept; // sub_18004A2C0
bool world_has_line_of_sight(events::JniContext* jni, jobject world, const math::Vec3d& from, const math::Vec3d& to) noexcept; // sub_18004A4B0
bool is_attack_key_down(const events::GameContext* ctx) noexcept; // sub_18004A530
math::Color3f chat_color_code_to_rgb(uint16_t color_code) noexcept; // sub_18004A580
void print_client_chat(events::JniContext* jni, const char* fmt, ...) noexcept; // sub_18004A6A0
math::Vec3d compute_strafe_motion(math::Vec2f rotation, float move_forward, float move_strafe, float movement_factor) noexcept; // sub_18004A740

// ============================================================================
// OpenGL Immediate Vertex-Array Primitives & World-to-Screen Projection
// ============================================================================

void gl_bind_unit_cube_vertices() noexcept; // sub_18004A870
void gl_draw_unit_cube_filled() noexcept; // sub_18004A8B0
void gl_draw_unit_cube_outline() noexcept; // sub_18004A8E0
void gl_bind_unit_quad_vertices() noexcept; // sub_18004A910
void gl_draw_unit_quad_filled() noexcept; // sub_18004A950
void gl_draw_unit_quad_outline() noexcept; // sub_18004A980
void gl_bind_chevron_vertices() noexcept; // sub_18004A9B0
void gl_draw_chevron_filled() noexcept; // sub_18004A9F0
void gl_draw_chevron_outline() noexcept; // sub_18004AA20
void gl_unbind_vertex_array() noexcept; // sub_18004AA50

bool project_world_to_screen(
    const events::RenderWorld2DProjEvent* proj,
    const math::Vec3d& rel_pos,
    math::Vec2f* out_screen) noexcept; // sub_18004AA70

bool project_world_to_screen_depth(
    const events::RenderWorld2DProjEvent* proj,
    const math::Vec3d& rel_pos,
    math::Vec2f* out_screen,
    float* out_inv_w_half = nullptr) noexcept; // sub_18004AAA0

// ============================================================================
// Aim Priority & Mouse Sensitivity GCD Quantizer (@ 0x180314D40..0x180314D54)
// ============================================================================

bool try_claim_aim_priority(uint32_t priority) noexcept; // sub_18004ACB0
math::Vec2f queue_aim_rotation_delta(math::Vec2f delta) noexcept; // sub_18004ACD0
math::Vec2f get_pending_aim_rotation_delta() noexcept; // sub_18004AD00
void update_mouse_sensitivity_step(events::JniContext* jni, jobject mc) noexcept; // sub_18004AD10
void quantize_aim_axis_remainder(float* in_out_delta) noexcept; // sub_18004ADA0
void apply_pending_aim_rotation(events::JniContext* jni, jobject player, bool update_render_arm) noexcept; // sub_18004AE00

// ============================================================================
// Silent Rotation Spoofing & WASD Move-Fix State Machine (@ 0x180314D58..0x180314D91)
// ============================================================================

void apply_move_fix_if_active(events::GameContext* ctx) noexcept; // sub_18004AF70
void apply_wasd_move_fix(events::JniContext* jni, bool enable) noexcept; // sub_18004AFA0
void update_rotation_spoof_state(
    events::GameContext* ctx,
    bool active,
    bool move_fix,
    bool silent_restore,
    math::Vec2f target_rotation) noexcept; // sub_18004B230
void swap_render_spoof_rotations(events::JniContext* jni, jobject player, bool enter_render) noexcept; // sub_18004B3A0
bool is_rotation_spoof_active() noexcept; // sub_18004B420
math::Vec2f get_server_spoof_rotation() noexcept; // sub_18004B430
void set_server_spoof_rotation(math::Vec2f rotation) noexcept; // sub_18004B440
math::Vec2f get_target_spoof_rotation() noexcept; // sub_18004B490
void pre_living_update_spoof_rotation(events::JniContext* jni) noexcept; // sub_18004B4A0
void post_living_update_restore_rotation(events::JniContext* jni, jobject player) noexcept; // sub_18004B540

// ============================================================================
// Combat Target Tracker & Sprint-Slowdown Reach Evaluator
// ============================================================================

void evaluate_attack_slowdown(events::GameContext* ctx, events::TargetTracker* tracker) noexcept; // sub_18004B5B0
bool update_target_tracker(events::GameContext* ctx, events::TargetTracker* tracker) noexcept; // sub_18004B970
void clear_target_tracker(events::GameContext* ctx, events::TargetTracker* tracker) noexcept; // sub_18004BF00

// ============================================================================
// Hotbar Lock, Inventory Grid Coordinates & Item Filter List Callbacks
// ============================================================================

bool try_acquire_hotbar_lock(const void* owner) noexcept; // sub_18004BF40
void release_hotbar_lock(const void* owner) noexcept; // sub_18004BF70
math::Vec2f get_inventory_slot_grid_pos(uint32_t slot_index) noexcept; // sub_18004BF90

size_t validate_item_name_input(char* buffer, char ch) noexcept; // sub_18004BFE0
size_t item_filter_list_count(void* ctx) noexcept; // sub_18004C030
const char* item_filter_list_get_name(void* ctx, size_t index) noexcept; // sub_18004C040
bool item_filter_list_add(void* ctx, const char* name, const math::Color3f* color = nullptr) noexcept; // sub_18004C070
int compare_item_filter_entry(const void* unused_ctx, const ItemIdEntry* a, const ItemIdEntry* b) noexcept; // sub_18004C0E0
void item_filter_list_remove(void* ctx, size_t index) noexcept; // sub_18004C0F0
void item_filter_list_clear(void* ctx) noexcept; // sub_18004C110
bool item_filter_list_contains_id(const util::PodVector<ItemIdEntry>* list, int32_t item_id) noexcept; // sub_18004C120

// ============================================================================
// Client-Side Player Entity Tags (Friend / AntiBot / Teammate)
// ============================================================================

uint16_t player_get_tag_flags(events::JniContext* jni, jobject player) noexcept; // sub_18004C170
bool player_has_tag_flags(events::JniContext* jni, jobject player, uint32_t mask) noexcept; // sub_18004C1A0
void player_add_tag_flags(events::JniContext* jni, jobject player, uint32_t flags) noexcept; // sub_18004C1E0
void player_remove_tag_flags(events::JniContext* jni, jobject player, uint32_t flags) noexcept; // sub_18004C240

// ============================================================================
// Auto-Block / Scaffold Hotbar Slot Selector & Depletion Tracker
// ============================================================================

void select_hotbar_slot_by_scorer(
    events::GameContext* ctx,
    const void* owner,
    bool allow_any_scored,
    HotbarItemScoreFn score_fn) noexcept; // sub_18004C2A0
void select_hotbar_block_slot(events::GameContext* ctx, const void* owner, bool allow_any_block) noexcept; // sub_18004C410
int score_placeable_block_id(int item_id) noexcept; // sub_18004C430
void release_hotbar_block_selector() noexcept; // sub_18004C480
void pre_place_record_held_block(events::GameContext* ctx) noexcept; // sub_18004C4D0
void post_place_check_held_block_depleted(events::GameContext* ctx) noexcept; // sub_18004C5B0

// ============================================================================
// AimAssist Target & Mouse Delta Interop
// ============================================================================

jobject get_aimassist_target_entity() noexcept; // sub_18004C680
math::Vec2f accumulate_aimassist_mouse_delta(math::Vec2f delta) noexcept; // sub_18004C690

// Convenience inline accessors for TargetTracker slowdown result (stored safely in flags/pad)
inline bool target_tracker_is_moving_away(const events::TargetTracker* tracker) noexcept {
    return tracker != nullptr && (tracker->flags & kTargetTrackerMovingAway) != 0;
}

inline bool target_tracker_query_moving_away(events::GameContext* ctx, events::TargetTracker* tracker) noexcept {
    if (tracker == nullptr) {
        return true;
    }
    if ((tracker->flags & kTargetTrackerSlowdownEvaluated) == 0 && ctx != nullptr) {
        evaluate_attack_slowdown(ctx, tracker);
    }
    return (tracker->flags & kTargetTrackerMovingAway) != 0;
}

} // namespace minecraft

namespace mc {
using namespace ::slinky::minecraft;
} // namespace mc

} // namespace slinky
