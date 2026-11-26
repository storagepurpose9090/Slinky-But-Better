#pragma once

#include <cstddef>
#include <cstdint>
#include <jni.h>

#include "commands/command_manager.hpp"
#include "events/events.hpp"
#include "events/module_manager.hpp"
#include "util/input_win32.hpp"
#include "util/math.hpp"
#include "util/random.hpp"

namespace slinky::modules {

// ============================================================================
// Module Instance Accessors Referenced by Core & Hooks
// ============================================================================
events::Module* get_gui_module() noexcept;
events::Module* get_mod_overlay_module() noexcept;
events::Module* get_no_hurt_cam_module() noexcept;
events::Module* get_safe_walk_module() noexcept;
events::Module* get_friends_module() noexcept;
events::Module* get_no_hit_delay_module() noexcept;

// ============================================================================
// GUI Module Interop (src/modules/visual/gui.cpp, 0x3087B0 / 0x53BD0..0x54050)
// ============================================================================

// Initializes the ClickGUI state owned by the GUI module (sub_180033DE0(&unk_180317030))
void gui_initialize_click_gui() noexcept;

// Returns true if user confirmed "Unload Slinky?" modal in ClickGUI (sub_180053BD0)
bool gui_is_unload_confirmed() noexcept;

// Returns current ClickGUI scale multiplier (0.75, 1.0, 1.25, 1.5, 2.0) (sub_180053BF0)
float gui_get_scale_factor() noexcept;

// Returns current ClickGUI accent RGB color (sub_180053C10)
math::Color3f gui_get_accent_color() noexcept;

// Routes Win32 window input to ClickGUI or toggles GUI keybind (sub_180053C30)
bool gui_handle_window_input(
    const input::InputEvent* event,
    bool was_already_down,
    events::ModuleManager* manager) noexcept;

// Opens ClickGUI from `!gui` chat command (sub_180053CF0)
void gui_command_open(events::GameContext* ctx, const commands::CommandArgValue* args) noexcept;

// Root `!gui` command specification (`off_18016DAC8`)
const commands::CommandSpec& get_gui_command_spec() noexcept;

// ============================================================================
// Notifications Module Interop (src/modules/visual/notifications.cpp, 0x56760)
// ============================================================================

// Resets the one-shot welcome toast flag (`byte_1803191B0 = 0`, sub_180056760)
void notifications_reset_welcome_toast() noexcept;

// ============================================================================
// Weapons Module Interop (src/modules/combat/weapons.cpp, 0x595D0..0x598F6)
// ============================================================================

// Checks whether `player` is holding a configured weapon; optionally outputs bitflags
// (bit 0 = sword, bit 1 = axe, bit 2 = stick/knockback) (sub_1800595D0)
bool weapons_is_holding_weapon(
    events::JniContext* jni,
    jobject player,
    uint32_t* out_weapon_flags = nullptr) noexcept;

// Convenience overload with `out_weapon_flags == nullptr` (sub_180059830)
bool weapons_is_holding_weapon_simple(events::JniContext* jni, jobject player) noexcept;

// Scans hotbar slots 0..8 and returns the first configured weapon slot, or -1 (sub_180059840)
int32_t weapons_find_hotbar_weapon_slot(events::GameContext* ctx, jobject inventory) noexcept;

// ============================================================================
// Timer Range & Piercing Interop (src/modules/combat/)
// ============================================================================

// Returns true while Timer Range is actively manipulating ticks/balance (sub_180064130)
bool timer_range_is_manipulating() noexcept;

// Compares `best_entity` vs `candidate_entity` during Reach/Piercing raycast (sub_180065FE0)
// Returns: 0 = prefer candidate_entity, 1 = keep best_entity, 2 = compare by ray distance
int32_t piercing_compare_targets(
    events::GameContext* ctx,
    jobject best_entity,
    jobject candidate_entity,
    bool inside_hitbox) noexcept;

// Returns true if Piercing is enabled and configured to raycast through blocks (sub_1800661E0)
bool piercing_should_ignore_blocks(events::GameContext* ctx) noexcept;

// ============================================================================
// Ghost Hand Interop (src/modules/world/ghost_hand.cpp, 0x682D0)
// ============================================================================

// Returns true if Ghost Hand should ignore `entity` so block interaction passes through (sub_1800682D0)
bool ghost_hand_should_ignore_entity(events::GameContext* ctx, jobject entity) noexcept;

// ============================================================================
// Safe Walk Interop (src/modules/movement/safe_walk.cpp, 0x631E0)
// ============================================================================

// Returns true when Safe Walk is actively holding/clipping at a block ledge (sub_1800631E0)
bool safe_walk_is_edge_sneaking() noexcept;

// ============================================================================
// Friends Interop (src/modules/utility/friends.cpp, 0x5C5D0 & off_18016F730)
// ============================================================================

// Handles middle-click friend add/remove on pointed player (sub_18005C5D0)
void friends_on_middle_click(events::JniContext* jni, events::ModuleManager* manager) noexcept;

// Root `!f` / `!friends` command specification (`off_18016F730`)
const commands::CommandSpec& get_friends_command_spec() noexcept;

// ============================================================================
// Visual Hook Callbacks (Name Tags, Chams, No Hurt Cam)
// ============================================================================

// RendererLivingEntity.canRenderName hook delegate (sub_18005D570)
bool nametags_can_render_vanilla_name(
    events::JniContext* jni,
    jobject renderer,
    jobject entity) noexcept;

// RenderPlayer.doRender hook delegate for wallhack polygon offset (sub_18005FB30)
void chams_render_living_model(
    events::JniContext* jni,
    jobject renderer,
    jobject entity,
    double x,
    double y,
    double z,
    float entity_yaw,
    float partial_ticks) noexcept;

// Resets local player maxHurtTime to suppress camera tilt when No Hurt Cam is enabled (sub_180063410)
void no_hurt_cam_apply(events::JniContext* jni) noexcept;

// ============================================================================
// Inventory / Container Utility Interop (Inv Manager, Chest Stealer, Refill)
// ============================================================================

// Evaluates an ItemStack's utility score and outputs its equipment/consumable category (sub_180068650)
int32_t inv_manager_score_useful_stack(
    events::GameContext* ctx,
    jobject stack,
    int32_t* out_category) noexcept;

// Humanized 9xN container slot pathing state shared by Chest Stealer & Refill (96 bytes = 0x60)
// Matches sub_18006DB20 & sub_18006DB90
struct ContainerSlotHumanizer {
    float speed_cps = 8.0f;                      // +0x00 (0)
    bool distance_based_delay = true;            // +0x04 (4)
    bool allow_misclicks = false;                // +0x05 (5)
    uint8_t pad0[2]{};                           // +0x06
    uint64_t pending_slot_mask = 0;              // +0x08 (8)
    random::DriftNoise misclick_gate{};          // +0x10 (16..31)
    random::DriftNoise skip_nearest_gate{};      // +0x20 (32..47)
    random::DriftNoise pause_spike_gate{};       // +0x30 (48..63)
    random::DriftNoise speed_drift{};            // +0x40 (64..79)
    random::NormalGenerator gaussian_jitter{};   // +0x50 (80..87)
    int32_t current_cursor_slot = 0;             // +0x58 (88)
    float last_step_distance = 0.0f;             // +0x5C (92)

    // Initializes bitmask of candidate slots and randomizes starting cursor position (sub_18006DB20)
    void reset(uint64_t slot_mask, uint64_t container_slot_count) noexcept;

    // Chooses next humanized slot index (0..63, or -1 if none) and adds step delay to `*inout_delay_sec` (sub_18006DB90)
    int32_t next_slot(float* inout_delay_sec) noexcept;
};
static_assert(sizeof(ContainerSlotHumanizer) == 0x60, "ContainerSlotHumanizer must match exact 0x60 layout");

} // namespace slinky::modules
