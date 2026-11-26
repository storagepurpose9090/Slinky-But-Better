#pragma once

#include <cstddef>
#include <cstdint>
#include <jni.h>
#include "util/math.hpp"

namespace slinky {

namespace mc {
struct Mappings;
}

namespace config {
struct ProfileRegistry;
}

namespace gui {
struct GuiRenderer;
struct ClickGui;
}

namespace events {

struct Module;
class ModuleManager;

// JNI call context passed as { Mappings*, JNIEnv* } - matches v39[2] / v36[2] in hooks
struct JniContext {
    mc::Mappings* mappings = nullptr;
    JNIEnv* env = nullptr;
};

// Global combat target tracker (24 bytes @ 0x180314440) - matches sub_18004B970 & sub_18004BF00
struct TargetTracker {
    uint32_t flags = 0;
    uint32_t pad = 0;
    jobject target_entity = nullptr; // JNI GlobalRef to closest/pointed valid entity
    double distance_sq = 0.0;
};

// Per-hook / per-frame game state bundle (72 bytes = 0x48) - matches v26[0..8] in sub_180029160
struct GameContext {
    config::ProfileRegistry* profile_registry = nullptr; // +0x00
    jobject local_player = nullptr;                      // +0x08 (EntityPlayerSP)
    jobject world = nullptr;                             // +0x10 (WorldClient)
    JniContext* jni = nullptr;                           // +0x18
    bool in_game_focus = false;                          // +0x20 (!click_gui_open && mc.inGameHasFocus)
    uint8_t pad_focus[7]{};                              // +0x21
    jobject minecraft = nullptr;                         // +0x28 (Minecraft singleton)
    uint8_t* key_states = nullptr;                       // +0x30 (104-byte key state table)
    ModuleManager* module_manager = nullptr;             // +0x38
    TargetTracker* target_tracker = nullptr;             // +0x40
};
static_assert(sizeof(GameContext) == 0x48, "GameContext must match exact 0x48 layout");

// All 27 EventBus event IDs (0..26) - matches Module::events[0..26] & ModuleManager::active_callbacks[0..26]
enum EventId : uint32_t {
    kEventGameTick               = 0,  // Per-tick client update (24 subscribers)
    kEventFrameUpdate            = 1,  // Per-frame render/aim interpolation update (sub_180028BB0)
    kEventIncomingPacket         = 2,  // Netty inbound packet before channelRead (sub_180029060, dir=0)
    kEventOutgoingPacket         = 3,  // Netty outbound packet before write (sub_180029060, dir=1)
    kEventPreModuleDisable       = 4,  // Cancellable check before disabling a module (sub_180049970)
    kEventModuleStateChanged     = 5,  // Fired when a module enables or disables (sub_180049A60)
    kEventRenderWorld3D          = 6,  // 3D OpenGL world render pass (sub_18002A8B0)
    kEventRenderWorld2DProj      = 7,  // 3D-to-2D projected overlay pass with GL matrices (sub_18002A8B0)
    kEventReachRaycastQuery      = 8,  // Reach / Hitboxes / Piercing / GhostHand query (getMouseOver)
    kEventGuiFramePoll           = 9,  // Poll whether 2D NanoVG HUD/ClickGui needs redraw (sub_180029160)
    kEventGuiRender2D            = 10, // Render NanoVG HUD / ClickGui / Notifications (sub_180029160)
    kEventProfileAction          = 11, // Profile loaded / saved / opened / deleted notification
    kEventErrorNotification      = 12, // Error code + message notification
    kEventRotationSpoofQuery     = 13, // Query spoofed yaw/pitch & move-fix flags (sub_180028D00 / 0x2A040)
    kEventPreAttackOrUseEntity   = 14, // Before attackEntity or interactWithEntity (0x29A20 / 0x2A260)
    kEventPostAttackOrUseEntity  = 15, // After attackEntity or interactWithEntity (0x29A20 / 0x2A260)
    kEventPreLivingUpdate        = 16, // Start of EntityPlayerSP.onUpdate before move-fix (0x2A040)
    kEventMidLivingUpdate        = 17, // Inside EntityPlayerSP.onUpdate after move-fix (0x2A040)
    kEventPostLivingUpdate       = 18, // After EntityPlayerSP.onUpdate super call (0x2A040)
    kEventFriendChanged          = 19, // Friend added or removed notification (sub_18005C5D0)
    kEventWorldBlockUpdate       = 20, // Chunk load/unload or block change (0x296A0 / 0x2A4A0 / 0x2A550 / 0x2A630)
    kEventPreMoveWithHeading     = 21, // Before EntityLivingBase.moveEntityWithHeading (0x29800)
    kEventPostMoveWithHeading    = 22, // After EntityLivingBase.moveEntityWithHeading (0x29800)
    kEventMainThreadPacket       = 23, // Packet processed on client main thread (sub_18002AEE0)
    kEventModuleKeybindChanged   = 24, // Module keybind updated (sub_180028D90)
    kEventGuiScreenTick          = 25, // GuiScreen.handleInput / inventory tick (sub_18002A700)
    kEventClientShutdown         = 26, // Unload / shutdown cleanup (sub_180029160)
    kEventCount                  = 27
};

struct EventBase {
    uint32_t id = 0;
    uint32_t pad0 = 0;
};

// Event 0: kEventGameTick & Event 16/17/18/26 (context-only events)
struct ContextEvent : EventBase {
    GameContext* ctx = nullptr; // +0x08
    uint8_t extra[56]{};
};

// Event 1: kEventFrameUpdate (sub_180028BB0)
struct FrameUpdateEvent : EventBase {
    GameContext* ctx = nullptr; // +0x08
    float partial_ticks = 0.0f; // +0x10
    uint32_t pad1 = 0;          // +0x14
    double delta_time = 0.0;    // +0x18
    uint8_t reserved[40]{};
};

// Event 2 & 3: kEventIncomingPacket / kEventOutgoingPacket (sub_180029060 & SlinkyChannelHandler.onPacket)
// Action codes returned to SlinkyChannelHandler: 0 = pass/release, 1 = delay/hold in queue, 2 = drop/cancel
struct NettyPacketEvent : EventBase {
    JniContext* jni = nullptr;   // +0x08
    jobject packet = nullptr;    // +0x10
    union {
        int64_t elapsed_ms = 0;  // +0x18 (milliseconds held in SlinkyChannelHandler queue, 0 on initial arrival)
        jobject channel_ctx;
    };
    int32_t packet_id = 0;       // +0x20 (monotonic SlinkyChannelHandler counter)
    union {
        int32_t action = 0;      // +0x24 (0 = forward, 1 = hold/queue, 2 = drop)
        int32_t cancelled;
    };
    uint8_t reserved[32]{};
};

// Event 4: kEventPreModuleDisable (sub_180049970)
struct PreModuleDisableEvent : EventBase {
    Module* module = nullptr;    // +0x08
    bool cancel_disable = false; // +0x10
    uint8_t reserved[55]{};
};

// Event 5: kEventModuleStateChanged (sub_180049A60)
struct ModuleStateChangedEvent : EventBase {
    GameContext* ctx = nullptr;  // +0x08
    Module* module = nullptr;    // +0x10
    bool enabled = false;        // +0x18
    uint8_t reserved[47]{};
};

// Event 6: kEventRenderWorld3D (sub_18002A8B0)
struct RenderWorld3DEvent : EventBase {
    math::Vec3d render_pos{};    // +0x08..+0x1F (interpolated RenderManager viewer pos)
    GameContext* ctx = nullptr;  // +0x20
    float partial_ticks = 0.0f;  // +0x28
    bool is_hand_pass = false;   // +0x2C
    uint8_t reserved[27]{};
};

// Event 7: kEventRenderWorld2DProj (sub_18002A8B0)
struct RenderWorld2DProjEvent : EventBase {
    math::Vec3d render_pos{};            // +0x08..+0x1F
    GameContext* ctx = nullptr;          // +0x20
    math::Vec2f viewport_size{};         // +0x28..+0x2F
    const float* modelview_matrix = nullptr;  // +0x30 (16 floats)
    const float* projection_matrix = nullptr; // +0x38 (16 floats)
    float partial_ticks = 0.0f;          // +0x40
    bool is_hand_pass = false;           // +0x44
    uint8_t pad_end[3]{};
};

// Event 8: kEventReachRaycastQuery (getMouseOver -> sub_18002E7F0)
struct ReachRaycastQueryEvent : EventBase {
    GameContext* ctx = nullptr;      // +0x08
    bool override_raycast = false;   // +0x10
    uint8_t pad1[7]{};
    double reach_distance = 3.0;     // +0x18
    double hitbox_expansion = 0.1;   // +0x20
    uint8_t reserved[24]{};
};

// Event 9: kEventGuiFramePoll (sub_180029160)
struct GuiFramePollEvent : EventBase {
    GameContext* ctx = nullptr;      // +0x08
    uint32_t render_flags = 0;       // +0x10 (bit 0 = visible, bit 1 = needs redraw)
    float delta_time = 0.0f;         // +0x14
    uint8_t reserved[48]{};
};

// Event 10: kEventGuiRender2D (sub_180029160)
struct GuiRender2DEvent : EventBase {
    GameContext* ctx = nullptr;           // +0x08
    gui::GuiRenderer* renderer = nullptr; // +0x10
    float delta_time = 0.0f;              // +0x18
    uint8_t reserved[44]{};
};

// Event 11: kEventProfileAction
struct ProfileActionEvent : EventBase {
    const char* profile_name = nullptr; // +0x08 (nullptr = "Default")
    int32_t action = 0;                 // +0x10 (0=loaded, 1=saved, 2=opened folder, 3=deleted)
    uint8_t reserved[52]{};
};

// Event 12: kEventErrorNotification
struct ErrorNotificationEvent : EventBase {
    int32_t error_code = 0;               // +0x08
    int32_t pad1 = 0;                     // +0x0C
    const char* custom_message = nullptr; // +0x10
    uint8_t reserved[48]{};
};

// Event 13: kEventRotationSpoofQuery (sub_180028D00 & sub_18002A040)
struct RotationSpoofEvent {
    uint32_t id = kEventRotationSpoofQuery; // +0x00
    uint32_t pad0 = 0;                      // +0x04
    bool active = false;                    // +0x08
    bool move_fix = false;                  // +0x09
    bool silent_restore = false;            // +0x0A
    uint8_t pad1 = 0;                       // +0x0B
    math::Vec2f rotation{};                 // +0x0C (yaw, pitch)
    uint8_t reserved[52]{};
};

// Event 14 & 15: kEventPreAttackOrUseEntity / kEventPostAttackOrUseEntity (0x29A20 / 0x2A260)
struct AttackOrUseEntityEvent : EventBase {
    GameContext* ctx = nullptr;  // +0x08
    bool is_attack = false;      // +0x10 (true = attackEntity, false = interactWithEntity)
    bool cancelled = false;      // +0x11
    uint8_t reserved[54]{};
};

// Event 19: kEventFriendChanged (sub_18005C5D0)
struct FriendChangedEvent : EventBase {
    bool added = false;              // +0x08 (true = added, false = removed)
    uint8_t pad1[7]{};
    const char* player_name = nullptr; // +0x10
    uint8_t reserved[48]{};
};

// Event 20: kEventWorldBlockUpdate (0x296A0 / 0x2A4A0 / 0x2A550 / 0x2A630)
struct WorldBlockUpdateEvent : EventBase {
    JniContext* jni = nullptr;   // +0x08
    jobject world = nullptr;     // +0x10
    int32_t update_kind = 0;     // +0x18 (0=chunk load/unload, 1=render range, 2=setBlock)
    int32_t x = 0;               // +0x1C (chunk_x for kind 0/1, block_x for kind 2)
    int32_t y_or_cz = 0;         // +0x20 (chunk_z/mode for kind 0/1, block_y for kind 2)
    int32_t z = 0;               // +0x24 (block_z for kind 2)
    int32_t block_id = 0;        // +0x28 (block id for kind 2)
    uint8_t reserved[28]{};
};

// Event 21 & 22: kEventPreMoveWithHeading / kEventPostMoveWithHeading (0x29800: Entity.moveFlying)
struct MoveWithHeadingEvent : EventBase {
    GameContext* ctx = nullptr;  // +0x08
    float move_strafe = 0.0f;    // +0x10 (strafe)
    float move_forward = 0.0f;   // +0x14 (forward)
    float friction = 0.0f;       // +0x18 (friction)
    bool is_jumping = false;     // +0x1C (jump key pressed while jumpTicks > 0)
    uint8_t reserved[43]{};
};

// Event 23: kEventMainThreadPacket (sub_18002AEE0)
struct MainThreadPacketEvent : EventBase {
    JniContext* jni = nullptr;       // +0x08
    jobject net_handler = nullptr;   // +0x10
    jobject packet = nullptr;        // +0x18
    bool cancelled = false;          // +0x20
    uint8_t reserved[39]{};
};

// Event 24: kEventModuleKeybindChanged (sub_180028D90)
struct ModuleKeybindChangedEvent : EventBase {
    ModuleManager* manager = nullptr; // +0x08
    Module* module = nullptr;         // +0x10
    uint8_t reserved[48]{};
};

// Event 25: kEventGuiScreenTick (sub_18002A700)
struct GuiScreenTickEvent : EventBase {
    GameContext* ctx = nullptr;          // +0x08
    bool forward_movement_keys = false;  // +0x10
    uint8_t reserved[55]{};
};

using EventCallbackFn = void (*)(void* event);

} // namespace events
} // namespace slinky
