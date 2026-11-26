#pragma once

#include <cstdint>
#include <jni.h>

#include "bytecode/jvmti_installer.hpp"
#include "events/events.hpp"

namespace slinky::hooks {

// Returns the 23-entry JNI hook callback table matching `off_18015A770`
const bytecode::HookCallbacks& get_jni_hook_callbacks() noexcept;

// Netty `SlinkyChannelHandler.onPacket(Object, boolean, long, int)` native callback (sub_180029060)
jint JNICALL on_netty_packet(
    JNIEnv* env,
    jobject handler_this,
    jobject packet,
    jboolean outbound,
    jlong elapsed_ms,
    jint packet_id) noexcept;

// Custom Reach / Hitboxes / Piercing / GhostHand raycast override (sub_18002E7F0)
void compute_reach_raycast(const events::ReachRaycastQueryEvent* query) noexcept;

// Shared 3D / projected-2D world render pass invoked after glClear(GL_DEPTH_BUFFER_BIT) (sub_18002A8B0)
void render_world_gl_pass(JNIEnv* env, jint clear_mask, bool clear_gl_state_1_8) noexcept;

// Intercepts outgoing packets on the client thread for `!` commands and kEventMainThreadPacket (sub_18002AEE0)
bool handle_client_outgoing_packet(
    events::JniContext* jni,
    jobject net_handler_or_mgr,
    jobject packet) noexcept;

// ============================================================================
// 23 SlinkyHooks13 Static Native Callbacks (0x29110 .. 0x2A700)
// ============================================================================
jboolean JNICALL hook_is_display_active_pre_render(JNIEnv* env, jclass cls) noexcept; // [0]  0x29110
void JNICALL hook_update_display(JNIEnv* env, jclass cls) noexcept;                   // [1]  0x29160
void JNICALL hook_invalidate_chunk(
    JNIEnv* env, jclass cls, jobject world,
    jint min_x, jint min_y, jint min_z,
    jint max_x, jint max_y, jint max_z) noexcept;                                     // [2]  0x296A0
jboolean JNICALL hook_should_clip_ledge(JNIEnv* env, jclass cls, jobject entity) noexcept; // [3]  0x29770
void JNICALL hook_move_flying(
    JNIEnv* env, jclass cls, jobject entity,
    jfloat strafe, jfloat forward, jfloat friction) noexcept;                         // [4]  0x29800
void JNICALL hook_post_world_clear_1_8(JNIEnv* env, jclass cls, jint mask) noexcept;  // [5]  0x29A10
void JNICALL hook_click_mouse(JNIEnv* env, jclass cls, jobject mc) noexcept;          // [6]  0x29A20
void JNICALL hook_orient_camera(
    JNIEnv* env, jclass cls, jobject entity_renderer, jfloat partial_ticks) noexcept; // [7]  0x29B50
void JNICALL hook_get_mouse_over(
    JNIEnv* env, jclass cls, jobject entity_renderer, jfloat partial_ticks) noexcept; // [8]  0x29C10
void JNICALL hook_update_active_render_info(
    JNIEnv* env, jclass cls, jobject player, jboolean third_person_front) noexcept;   // [9]  0x29E90
void JNICALL hook_set_render_x(
    JNIEnv* env, jclass cls, jobject render_manager, jfloat angle_x) noexcept;        // [10] 0x29F80
void JNICALL hook_set_render_y(
    JNIEnv* env, jclass cls, jobject render_manager, jfloat angle_y) noexcept;        // [11] 0x29FE0
void JNICALL hook_update_entities(JNIEnv* env, jclass cls, jobject world) noexcept;   // [12] 0x2A040
jboolean JNICALL hook_can_render_name(
    JNIEnv* env, jclass cls, jobject renderer, jobject entity) noexcept;              // [13] 0x2A230
void JNICALL hook_right_click_mouse(JNIEnv* env, jclass cls, jobject mc) noexcept;    // [14] 0x2A260
void JNICALL hook_render_player(
    JNIEnv* env, jclass cls, jobject render_player, jobject entity,
    jdouble x, jdouble y, jdouble z,
    jfloat entity_yaw, jfloat partial_ticks) noexcept;                                // [15] 0x2A390
void JNICALL hook_send_packet_1_7(
    JNIEnv* env, jclass cls, jobject net_mgr,
    jobject packet, jobject listeners) noexcept;                                      // [16] 0x2A3F0
void JNICALL hook_send_packet_1_8(
    JNIEnv* env, jclass cls, jobject net_mgr, jobject packet) noexcept;               // [17] 0x2A440
void JNICALL hook_post_world_clear_1_7(JNIEnv* env, jclass cls, jint mask) noexcept;  // [18] 0x2A490
void JNICALL hook_unload_chunk(
    JNIEnv* env, jclass cls, jobject chunk_provider,
    jint chunk_x, jint chunk_z) noexcept;                                             // [19] 0x2A4A0
jboolean JNICALL hook_set_block_1_7(
    JNIEnv* env, jclass cls, jobject world,
    jint x, jint y, jint z, jobject block,
    jint metadata, jint flags) noexcept;                                              // [20] 0x2A550
jboolean JNICALL hook_set_block_1_8(
    JNIEnv* env, jclass cls, jobject world,
    jobject block_pos, jobject block_state, jint flags) noexcept;                     // [21] 0x2A630
void JNICALL hook_update_controller(
    JNIEnv* env, jclass cls, jobject player_controller) noexcept;                     // [22] 0x2A700

} // namespace slinky::hooks
