#pragma once

#include <cstddef>
#include <cstdint>

#include <jni.h>
#include <jvmti.h>

#include "bytecode/classfile_rewriter.hpp"
#include "events/events.hpp"

namespace slinky::bytecode {

// ============================================================================
//JNI Error Codes for Hook Class Definition (sub_18002CB90)
// ============================================================================
enum HookInstallerError : int {
    kInstallOk                  = 0,
    kErrRegisterNativesFailed   = 2001,
    kErrGetJvmtiEnvFailed       = 2004,
    kErrDefineClassFailed       = 2006,
};

// ============================================================================
// Minecraft Version & Mapping Environment Constants (Mappings +0x00 / +0x04)
// ============================================================================
enum class McVersion : uint32_t {
    V1_7_10 = 0,
    V1_8_9  = 1,
};

enum class MappingEnv : uint32_t {
    Notch = 0, // Vanilla obfuscated
    Mcp   = 1, // Lunar / MCP deobfuscated
    Srg   = 2, // Forge / OptiFine Searge
};

inline constexpr size_t kHookedClassCount      = 9;
inline constexpr size_t kSlinkyHookMethodCount = 23;

// ============================================================================
// 23 Native Hook Function Pointers (184 bytes @ 0x180314A00, sub_180042690)
// ============================================================================
struct HookCallbacks {
    jboolean (JNICALL *is_display_active_pre_render)(JNIEnv*, jclass) = nullptr;                                              // [0]  0x314A00
    void     (JNICALL *update_display)(JNIEnv*, jclass) = nullptr;                                                            // [1]  0x314A08
    void     (JNICALL *invalidate_chunk)(JNIEnv*, jclass, jobject, jint, jint, jint, jint, jint, jint) = nullptr;             // [2]  0x314A10
    jboolean (JNICALL *should_clip_ledge)(JNIEnv*, jclass, jobject) = nullptr;                                                // [3]  0x314A18
    void     (JNICALL *move_flying)(JNIEnv*, jclass, jobject, jfloat, jfloat, jfloat) = nullptr;                              // [4]  0x314A20
    void     (JNICALL *post_world_clear_1_8)(JNIEnv*, jclass, jint) = nullptr;                                                // [5]  0x314A28
    void     (JNICALL *click_mouse)(JNIEnv*, jclass, jobject) = nullptr;                                                      // [6]  0x314A30
    void     (JNICALL *orient_camera)(JNIEnv*, jclass, jobject, jfloat) = nullptr;                                            // [7]  0x314A38
    void     (JNICALL *get_mouse_over)(JNIEnv*, jclass, jobject, jfloat) = nullptr;                                           // [8]  0x314A40
    void     (JNICALL *update_active_render_info)(JNIEnv*, jclass, jobject, jboolean) = nullptr;                              // [9]  0x314A48
    void     (JNICALL *set_render_x)(JNIEnv*, jclass, jobject, jfloat) = nullptr;                                             // [10] 0x314A50
    void     (JNICALL *set_render_y)(JNIEnv*, jclass, jobject, jfloat) = nullptr;                                             // [11] 0x314A58
    void     (JNICALL *update_entities)(JNIEnv*, jclass, jobject) = nullptr;                                                  // [12] 0x314A60
    jboolean (JNICALL *can_render_name)(JNIEnv*, jclass, jobject, jobject) = nullptr;                                         // [13] 0x314A68
    void     (JNICALL *right_click_mouse)(JNIEnv*, jclass, jobject) = nullptr;                                                // [14] 0x314A70
    void     (JNICALL *render_player)(JNIEnv*, jclass, jobject, jobject, jdouble, jdouble, jdouble, jfloat, jfloat) = nullptr;// [15] 0x314A78
    void     (JNICALL *send_packet_1_7)(JNIEnv*, jclass, jobject, jobject, jobject) = nullptr;                                // [16] 0x314A80
    void     (JNICALL *send_packet_1_8)(JNIEnv*, jclass, jobject, jobject) = nullptr;                                         // [17] 0x314A88
    void     (JNICALL *post_world_clear_1_7)(JNIEnv*, jclass, jint) = nullptr;                                                // [18] 0x314A90
    void     (JNICALL *unload_chunk)(JNIEnv*, jclass, jobject, jint, jint) = nullptr;                                         // [19] 0x314A98
    jboolean (JNICALL *set_block_1_7)(JNIEnv*, jclass, jobject, jint, jint, jint, jobject, jint, jint) = nullptr;             // [20] 0x314AA0
    jboolean (JNICALL *set_block_1_8)(JNIEnv*, jclass, jobject, jobject, jobject, jint) = nullptr;                            // [21] 0x314AA8
    void     (JNICALL *update_controller)(JNIEnv*, jclass, jobject) = nullptr;                                                // [22] 0x314AB0
};
static_assert(sizeof(HookCallbacks) == 184, "HookCallbacks must match 184-byte table @ 0x180314A00");

// ============================================================================
// Resolved Target Handles & Dynamic Signatures Extracted from mc::Mappings
// ============================================================================
struct HookTargetContext {
    McVersion mc_version   = McVersion::V1_8_9; // Mappings + 0x000
    MappingEnv mapping_env = MappingEnv::Notch; // Mappings + 0x004

    jobject class_loader       = nullptr;       // Mappings + 0x010 (slot 2)
    jmethodID load_class_mid   = nullptr;       // Mappings + 0x018 (slot 3)

    // 9 retransformed Minecraft classes in exact RetransformClasses order:
    jclass cls_minecraft              = nullptr; // [0] Mappings + 0x178 (slot 47)
    jclass cls_entity_renderer        = nullptr; // [1] Mappings + 0x980 (slot 304)
    jclass cls_render_manager         = nullptr; // [2] Mappings + 0x848 (slot 265)
    jclass cls_renderer_living_entity = nullptr; // [3] Mappings + 0xB40 (slot 360)
    jclass cls_render_player          = nullptr; // [4] Mappings + 0xC80 (slot 400)
    jclass cls_net_handler_play       = nullptr; // [5] Mappings + 0xB08 (slot 353)
    jclass cls_world_client           = nullptr; // [6] Mappings + 0xA30 (slot 326)
    jclass cls_entity                 = nullptr; // [7] Mappings + 0x248 (slot 73)
    jclass cls_entity_living_base     = nullptr; // [8] Mappings + 0x610 (slot 194)

    // Dynamically resolved RendererLivingEntity / RenderPlayer method names & descriptors
    // (populated by Mappings loader; fallback defaults used if nullptr):
    const char* can_render_name_name          = nullptr; // Mappings + 0xB60 (slot 364)
    const char* can_render_name_desc          = nullptr; // Mappings + 0xB68 (slot 365)
    const char* living_do_render_name         = nullptr; // Mappings + 0xB78 (slot 367)
    const char* living_do_render_desc         = nullptr; // Mappings + 0xB80 (slot 368)
    const char* render_player_do_render_name  = nullptr; // Mappings + 0xC88 (slot 401)
    const char* render_player_do_render_desc  = nullptr; // Mappings + 0xC90 (slot 402)
};

// Extracts HookTargetContext directly from the raw binary layout of `mc::Mappings`.
HookTargetContext extract_hook_targets_from_mappings(const mc::Mappings* mappings) noexcept;

// ============================================================================
// Public JVMTI Hook Installer / Uninstaller API
// ============================================================================
void set_hook_callbacks(const HookCallbacks& callbacks) noexcept; // sub_180042690
void set_hook_callbacks(const void* callbacks_184_bytes) noexcept; // sub_180042690

int define_and_register_hooks_class(
    JNIEnv* env,
    const char* class_name,
    jobject class_loader,
    jmethodID load_class_mid,
    const uint8_t* class_bytes,
    jsize class_len,
    const JNINativeMethod* methods,
    jint method_count,
    jclass* out_local_class); // sub_18002CB90

void JNICALL jvmti_class_file_load_hook(
    jvmtiEnv* jvmti_env,
    JNIEnv* jni_env,
    jclass class_being_redefined,
    jobject loader,
    const char* name,
    jobject protection_domain,
    jint class_data_len,
    const unsigned char* class_data,
    jint* new_class_data_len,
    unsigned char** new_class_data); // sub_180043A30

int install_jvmti_hooks(JNIEnv* env, jvmtiEnv* jvmti, const HookTargetContext& target); // sub_1800426B0
int install_jvmti_hooks(const events::JniContext* ctx, jvmtiEnv* jvmti);                // sub_1800426B0
int install_jvmti_hooks(JavaVM* vm, JNIEnv* env, const HookTargetContext& target);      // sub_1800426B0
int install_jvmti_hooks(JavaVM* vm, const events::JniContext* ctx);                     // sub_1800426B0

int uninstall_jvmti_hooks(JNIEnv* env, jvmtiEnv* jvmti);            // sub_180043C80
int uninstall_jvmti_hooks(JavaVM* vm, JNIEnv* env = nullptr);       // sub_180043C80

bool are_jvmti_hooks_installed() noexcept;
jclass get_slinky_hooks_class() noexcept;
int get_last_hook_rewrite_error() noexcept;

} // namespace slinky::bytecode
