#include "minecraft/game_helpers.hpp"

#include <glad/glad.h>

#include <bit>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace slinky::minecraft {

// ============================================================================
// Static Geometry Arrays (@ 0x18016C0A0..0x18016C188) & Move-Fix Tables
// ============================================================================

// 8 corners of [-1, 1]^3 (@ 0x18016C0A0)
const float kUnitCubeVertices[24] = {
    -1.0f, -1.0f, -1.0f,
    -1.0f, -1.0f,  1.0f,
    -1.0f,  1.0f, -1.0f,
    -1.0f,  1.0f,  1.0f,
     1.0f, -1.0f, -1.0f,
     1.0f, -1.0f,  1.0f,
     1.0f,  1.0f, -1.0f,
     1.0f,  1.0f,  1.0f,
};

// 14-index triangle strip covering all 6 cube faces (@ 0x18016C100)
const uint8_t kUnitCubeStripIndices[14] = {
    3, 7, 1, 5, 4, 7, 6, 3, 2, 1, 0, 4, 2, 6,
};

// 12 edges (24 indices) of the unit cube (@ 0x18016C110)
const uint8_t kUnitCubeLineIndices[24] = {
    0, 1, 1, 5, 5, 4, 4, 0,
    2, 3, 3, 7, 7, 6, 6, 2,
    0, 2, 1, 3, 4, 6, 5, 7,
};

// 2D unit quad [0, 1]^2 (@ 0x18016C130)
const float kUnitQuadVertices[8] = {
    0.0f, 0.0f,
    0.0f, 1.0f,
    1.0f, 1.0f,
    1.0f, 0.0f,
};

const uint8_t kUnitQuadStripIndices[4] = {0, 1, 3, 2}; // @ 0x18016C150
const uint8_t kUnitQuadLoopIndices[4]  = {0, 1, 2, 3}; // @ 0x18016C154

// 2D directional chevron / pointer arrow (@ 0x18016C160)
const float kChevronVertices[8] = {
     0.0f, -1.0f,
    -1.0f,  1.0f,
     0.0f,  0.5f,
     1.0f,  1.0f,
};

const uint8_t kChevronFanIndices[4]  = {0, 1, 2, 3}; // @ 0x18016C180
const uint8_t kChevronLoopIndices[4] = {0, 1, 2, 3}; // @ 0x18016C184

// Shared AimAssist state (@ 0x180314DD0 & @ 0x180314E08)
jobject g_aimassist_target_entity = nullptr;
math::Vec2f g_aimassist_mouse_delta{0.0f, 0.0f};

namespace {

// 8-direction WASD octant vectors (fwd, strafe) packed as {int8_t fwd, int8_t strafe} (@ 0x18016C1F0)
struct WasdOctant {
    int8_t forward;
    int8_t strafe;
};

constexpr WasdOctant kWasdOctants[8] = {
    { 1,  0}, // 0: W
    { 1,  1}, // 1: W + A
    { 0,  1}, // 2: A
    {-1,  1}, // 3: S + A
    {-1,  0}, // 4: S
    {-1, -1}, // 5: S + D
    { 0, -1}, // 6: D
    { 1, -1}, // 7: W + D
};

// Forward-row octant lookup indexed by (strafe + 1) for strafe in {-1, 0, +1} (@ 0x18016C200)
constexpr int32_t kForwardStrafeOctants[4] = {7, 0, 1, 0};

// Client chat formatting buffer (@ 0x180314C40)
char g_chat_format_buffer[256]{};

// Aim priority & mouse sensitivity quantization state (@ 0x180314D40..0x180314D54)
uint32_t g_aim_priority = 0;                 // dword_180314D40
math::Vec2f g_pending_aim_delta{0.0f, 0.0f}; // qword_180314D48
int32_t g_aim_idle_ticks = 0;                // dword_180314D50
float g_mouse_sensitivity_step = 0.0f;       // dword_180314D54

// Rotation spoofing & move-fix state (@ 0x180314D58..0x180314D91)
bool g_move_fix_enabled = false;                  // byte_180314D58
bool g_move_fix_applied = false;                  // byte_180314D59
bool g_spoof_silent_restore = false;              // byte_180314D5A
math::Vec2f g_spoof_target_rot{0.0f, 0.0f};       // qword_180314D60
math::Vec2f g_saved_render_rot{0.0f, 0.0f};       // qword_180314D68
math::Vec2f g_saved_prev_render_rot{0.0f, 0.0f};  // qword_180314D70
math::Vec2f g_server_spoof_rot{0.0f, 0.0f};       // qword_180314D78
bool g_rotation_spoof_active = false;             // byte_180314D80
bool g_server_spoof_dirty = false;                // byte_180314D81
math::Vec2f g_pre_living_saved_rot{0.0f, 0.0f};   // qword_180314D88
bool g_living_update_spoofed = false;             // byte_180314D90
uint8_t g_saved_wasd_mask = 0;                    // byte_180314D91

// Hotbar lock & auto-block selector state (@ 0x180314D98..0x180314DC0)
const void* g_hotbar_lock_owner = nullptr;        // qword_180314D98
const void* g_block_selector_owner = nullptr;     // qword_180314DA0
int32_t g_depleted_block_id = 0;                  // dword_180314DA8
uint32_t g_ticks_since_block_place = 0;           // dword_180314DAC
uint8_t g_advance_from_depleted_slot = 0;         // byte_180314DB0
int32_t g_recorded_place_block_id = 0;            // dword_180314DB4
int32_t g_recorded_place_stack_size = 0;          // dword_180314DB8
uint32_t g_last_placed_hotbar_slot = 0;           // dword_180314DBC
int32_t g_remaining_tracked_stack_count = 0;      // dword_180314DC0

constexpr bool is_valid_color_char(uint16_t ch) noexcept {
    return static_cast<uint16_t>(ch - u'0') < 10u ||
           static_cast<uint16_t>(ch - u'a') < 6u ||
           ch == u'r';
}

constexpr bool is_username_word_char(uint16_t ch) noexcept {
    return ch == u'_' ||
           static_cast<uint16_t>(ch - u'0') < 10u ||
           static_cast<uint16_t>((ch & 0xFFDFu) - u'A') < 26u;
}

} // namespace

// ============================================================================
// Item, Team Color, Raycast, Input, Chat & Movement Math Helpers
// ============================================================================

// sub_180049F00
jobject player_get_held_item(events::JniContext* jni, jobject player) noexcept {
    jobject inv = player_get_inventory(jni, player);
    const uint32_t slot = static_cast<uint32_t>(inv_player_get_current_item(jni, inv));
    if (slot <= 8u) {
        jobjectArray main_inv = inv_player_get_main_inventory(jni, inv);
        jobject stack = jni->env->GetObjectArrayElement(main_inv, static_cast<jsize>(slot));
        if (stack != nullptr) {
            return item_stack_get_item(jni, stack);
        }
    }
    return nullptr;
}

// sub_180049F70
bool is_sword_item(events::JniContext* jni, jobject item) noexcept {
    if (item != nullptr) {
        const uint32_t offset = static_cast<uint32_t>(item_get_id(jni, item) - 267);
        if (offset <= 0x10u) {
            // Matches 267 (iron), 268 (wood), 272 (stone), 276 (diamond), 283 (gold)
            return ((0x10223u >> offset) & 1u) != 0u;
        }
    }
    return false;
}

// sub_180049FB0
bool is_block_item(events::JniContext* jni, jobject item) noexcept {
    if (item == nullptr) {
        return false;
    }
    jobject block = block_get_from_item(jni, item);
    return block != nullptr && block_get_id(jni, block) != 0;
}

// sub_180049FF0
uint16_t extract_name_color_code_jstring(events::JniContext* jni, jstring text) noexcept {
    JNIEnv* env = jni->env;
    uint16_t color_code = 0;
    const jchar* chars = env->GetStringCritical(text, nullptr);
    const jsize len = env->GetStringLength(text);
    if (len > 0) {
        bool after_section = false;
        bool hit_word_char = false;
        for (jsize i = 0; i < len; ++i) {
            const bool prev_section = after_section;
            const uint16_t ch = static_cast<uint16_t>(chars[i]);
            after_section = (ch == 167u);
            if (ch != 167u) {
                if (prev_section) {
                    if (is_valid_color_char(ch)) {
                        color_code = ch;
                    }
                } else if (is_username_word_char(ch)) {
                    hit_word_char = true;
                    break;
                }
            }
        }
        if (!hit_word_char) {
            color_code = 0;
        }
    }
    env->ReleaseStringCritical(text, chars);
    return color_code;
}

// sub_18004A0D0
uint32_t extract_name_color_code_utf8(
    const char* text,
    uint32_t default_code,
    bool* out_found_word_char) noexcept {
    *out_found_word_char = false;
    const size_t len = std::strlen(text);
    if (len == 0) {
        return default_code;
    }

    bool saw_c2 = false;
    bool after_section = false;
    uint32_t code = default_code;

    for (size_t i = 0; i < len; ++i) {
        const uint8_t raw = static_cast<uint8_t>(text[i]);
        if (after_section) {
            const bool is_hex_lower = static_cast<uint16_t>(static_cast<int8_t>(raw) - 'a') < 6u;
            const bool is_digit = static_cast<uint16_t>(static_cast<int8_t>(raw) - '0') < 10u;
            const bool is_reset = (raw == static_cast<uint8_t>('r'));
            const uint32_t mapped = is_reset ? static_cast<uint32_t>('f')
                                             : static_cast<uint32_t>(static_cast<int32_t>(static_cast<int8_t>(raw)));
            if (is_digit || is_hex_lower || is_reset) {
                code = mapped;
            }
            after_section = false;
        } else if (saw_c2) {
            if (raw == 0xA7u) {
                after_section = true;
                saw_c2 = false;
            } else if (raw == 0xC2u) {
                saw_c2 = true;
                after_section = false;
            } else {
                saw_c2 = false;
                after_section = false;
            }
        } else if (raw == 0xC2u) {
            saw_c2 = true;
            after_section = false;
        } else {
            const uint16_t ch = static_cast<uint16_t>(static_cast<int16_t>(static_cast<int8_t>(raw)));
            if (raw == '_' ||
                static_cast<uint16_t>(ch - '0') < 10u ||
                static_cast<uint16_t>((ch & 0xFFDFu) - 'A') <= 25u) {
                *out_found_word_char = true;
                return code;
            }
            after_section = false;
            saw_c2 = false;
        }
    }
    return code;
}

// sub_18004A1D0
uint16_t score_player_team_get_color_code(events::JniContext* jni, jobject team) noexcept {
    jstring prefix = score_player_team_get_color_prefix(jni, team);
    uint16_t result = u'f';
    if (prefix == nullptr) {
        return result;
    }

    JNIEnv* env = jni->env;
    const jsize len = env->GetStringLength(prefix);
    if (len == 0) {
        return result;
    }

    const jchar* chars = env->GetStringCritical(prefix, nullptr);
    if (len > 0) {
        bool pending_code = false;
        uint16_t candidate = 0;
        for (jsize idx = len - 1; idx >= 0; --idx) {
            const uint16_t ch = static_cast<uint16_t>(chars[idx]);
            if (ch != 167u) {
                pending_code = is_valid_color_char(ch);
                if (pending_code) {
                    candidate = ch;
                }
            } else {
                if (pending_code) {
                    result = candidate;
                    break;
                }
                pending_code = false;
            }
        }
    }
    env->ReleaseStringCritical(prefix, chars);
    return result;
}

// sub_18004A2C0
uint16_t entity_get_team_color_code(events::JniContext* jni, jobject entity) noexcept {
    jstring name = entity_get_name(jni, entity);
    const uint16_t from_name = extract_name_color_code_jstring(jni, name);
    if (from_name != 0) {
        return from_name;
    }

    jobject living = as_entity_living_base(jni, entity);
    if (living == nullptr) {
        return u'f';
    }
    jobject team_base = living_get_team(jni, living);
    if (team_base == nullptr) {
        return u'f';
    }
    jobject score_team = as_score_player_team(jni, team_base);
    if (score_team == nullptr) {
        return u'f';
    }
    return score_player_team_get_color_code(jni, score_team);
}

// sub_18004A4B0
bool world_has_line_of_sight(
    events::JniContext* jni,
    jobject world,
    const math::Vec3d& from,
    const math::Vec3d& to) noexcept {
    jobject start_vec = vec3_new(jni, from);
    jobject end_vec = vec3_new(jni, to);
    return world_ray_trace_blocks(jni, world, start_vec, end_vec, false, true, true) == nullptr;
}

// sub_18004A530
bool is_attack_key_down(const events::GameContext* ctx) noexcept {
    jobject settings = mc_get_game_settings(ctx->jni, ctx->minecraft);
    jobject key_attack = settings_get_key_attack(ctx->jni, settings);
    const int32_t key_code = key_binding_get_key_code(ctx->jni, key_attack);
    // LWJGL button 0 = -100 -> key_states[1] (VK_LBUTTON), button 1 = -99 -> key_states[2] (VK_RBUTTON)
    const size_t vk_index = (key_code == -99) ? 2u : 1u;
    return ctx->key_states[vk_index] != 0;
}

// sub_18004A580
math::Color3f chat_color_code_to_rgb(uint16_t color_code) noexcept {
    constexpr float kLow  = 0.33000001311302185f; // 0x3EA8F5C3
    constexpr float kMid  = 0.6700000166893005f;  // 0x3F2B851F
    constexpr float kHigh = 1.0f;                 // 0x3F800000

    switch (color_code) {
        case u'0': return {0.0f,  0.0f,  0.0f};
        case u'1': return {0.0f,  0.0f,  kMid};
        case u'2': return {0.0f,  kMid,  0.0f};
        case u'3': return {0.0f,  kMid,  kMid};
        case u'4': return {kMid,  0.0f,  0.0f};
        case u'5': return {kMid,  0.0f,  kMid};
        case u'6': return {kHigh, kMid,  0.0f};
        case u'7': return {kMid,  kMid,  kMid};
        case u'8': return {kLow,  kLow,  kLow};
        case u'9': return {kLow,  kLow,  kHigh};
        case u'a': return {kLow,  kHigh, kLow};
        case u'b': return {kLow,  kHigh, kHigh};
        case u'c': return {kHigh, kLow,  kLow};
        case u'd': return {kHigh, kLow,  kHigh};
        case u'e': return {kHigh, kHigh, kLow};
        default:   return {kHigh, kHigh, kHigh};
    }
}

// sub_18004A6A0
void print_client_chat(events::JniContext* jni, const char* fmt, ...) noexcept {
    jobject mc = mc_get_instance(jni);
    jobject player = mc_get_player(jni, mc);
    if (player == nullptr) {
        return;
    }

    va_list args;
    va_start(args, fmt);
    std::vsnprintf(g_chat_format_buffer, sizeof(g_chat_format_buffer), fmt, args);
    va_end(args);

    jstring text = jni->env->NewStringUTF(g_chat_format_buffer);
    if (text != nullptr) {
        jobject component = chat_component_text_new(jni, text);
        if (component != nullptr) {
            player_sp_add_chat_message(jni, player, component);
        }
    }
}

// sub_18004A740
math::Vec3d compute_strafe_motion(
    math::Vec2f rotation,
    float move_forward,
    float move_strafe,
    float movement_factor) noexcept {
    const float fwd_raw = move_forward * 0.9800000190734863f;
    const float str_raw = move_strafe * 0.9800000190734863f;
    const float dist_sq = str_raw * str_raw + fwd_raw * fwd_raw;
    if (dist_sq < 9.999999747378752e-05f) {
        return {0.0, 0.0, 0.0};
    }

    const float scale = (movement_factor * 0.9999998211860657f) / std::fmax(1.0f, std::sqrt(dist_sq));
    const float str = str_raw * scale;
    const float fwd = fwd_raw * scale;
    const float yaw_rad = static_cast<float>(static_cast<double>(rotation.x) * math::kPi / 180.0);
    const float sin_yaw = std::sin(yaw_rad);
    const float cos_yaw = std::cos(yaw_rad);

    return {
        static_cast<double>(str * cos_yaw - fwd * sin_yaw),
        0.0,
        static_cast<double>(fwd * cos_yaw + str * sin_yaw),
    };
}

// ============================================================================
// OpenGL Immediate Vertex-Array Primitives & World-to-Screen Projection
// ============================================================================

// sub_18004A870
void gl_bind_unit_cube_vertices() noexcept {
    glVertexPointer(3, GL_FLOAT, 0, kUnitCubeVertices);
    glEnableClientState(GL_VERTEX_ARRAY);
}

// sub_18004A8B0
void gl_draw_unit_cube_filled() noexcept {
    glDrawElements(GL_TRIANGLE_STRIP, 14, GL_UNSIGNED_BYTE, kUnitCubeStripIndices);
}

// sub_18004A8E0
void gl_draw_unit_cube_outline() noexcept {
    glDrawElements(GL_LINES, 24, GL_UNSIGNED_BYTE, kUnitCubeLineIndices);
}

// sub_18004A910
void gl_bind_unit_quad_vertices() noexcept {
    glVertexPointer(2, GL_FLOAT, 0, kUnitQuadVertices);
    glEnableClientState(GL_VERTEX_ARRAY);
}

// sub_18004A950
void gl_draw_unit_quad_filled() noexcept {
    glDrawElements(GL_TRIANGLE_STRIP, 4, GL_UNSIGNED_BYTE, kUnitQuadStripIndices);
}

// sub_18004A980
void gl_draw_unit_quad_outline() noexcept {
    glDrawElements(GL_LINE_LOOP, 4, GL_UNSIGNED_BYTE, kUnitQuadLoopIndices);
}

// sub_18004A9B0
void gl_bind_chevron_vertices() noexcept {
    glVertexPointer(2, GL_FLOAT, 0, kChevronVertices);
    glEnableClientState(GL_VERTEX_ARRAY);
}

// sub_18004A9F0
void gl_draw_chevron_filled() noexcept {
    glDrawElements(GL_TRIANGLE_FAN, 4, GL_UNSIGNED_BYTE, kChevronFanIndices);
}

// sub_18004AA20
void gl_draw_chevron_outline() noexcept {
    glDrawElements(GL_LINE_LOOP, 4, GL_UNSIGNED_BYTE, kChevronLoopIndices);
}

// sub_18004AA50
void gl_unbind_vertex_array() noexcept {
    glDisableClientState(GL_VERTEX_ARRAY);
}

// sub_18004AA70
bool project_world_to_screen(
    const events::RenderWorld2DProjEvent* proj,
    const math::Vec3d& rel_pos,
    math::Vec2f* out_screen) noexcept {
    return project_world_to_screen_depth(proj, rel_pos, out_screen, nullptr);
}

// sub_18004AAA0
bool project_world_to_screen_depth(
    const events::RenderWorld2DProjEvent* proj,
    const math::Vec3d& rel_pos,
    math::Vec2f* out_screen,
    float* out_inv_w_half) noexcept {
    if (proj == nullptr || proj->modelview_matrix == nullptr || proj->projection_matrix == nullptr) {
        return false;
    }

    const float rx = static_cast<float>(rel_pos.x);
    const float ry = static_cast<float>(rel_pos.y);
    const float rz = static_cast<float>(rel_pos.z);

    const float* mv = proj->modelview_matrix;
    const float* pr = proj->projection_matrix;

    const float ex = (mv[0] * rx + mv[4] * ry + mv[8]  * rz) + mv[12];
    const float ey = (mv[1] * rx + mv[5] * ry + mv[9]  * rz) + mv[13];
    const float ez = (mv[2] * rx + mv[6] * ry + mv[10] * rz) + mv[14];
    const float ew = (mv[3] * rx + mv[7] * ry + mv[11] * rz) + mv[15];

    const float cw = pr[3] * ex + pr[7] * ey + pr[11] * ez + pr[15] * ew;
    if (cw == 0.0f) {
        return false;
    }

    const float inv_w_half = (1.0f / cw) * 0.5f;
    const float cz = pr[2] * ex + pr[6] * ey + pr[10] * ez + pr[14] * ew;
    const float depth_01 = cz * inv_w_half + 0.5f;
    if (depth_01 <= 0.0f || depth_01 >= 1.0f) {
        return false;
    }

    const float cy = pr[1] * ex + pr[5] * ey + pr[9] * ez + pr[13] * ew;
    const float win_y_norm = cy * inv_w_half + 0.5f;
    const float cx = pr[0] * ex + pr[4] * ey + pr[8] * ez + pr[12] * ew;
    const float win_x_norm = cx * inv_w_half + 0.5f;

    out_screen->x = win_x_norm * proj->viewport_size.x;
    out_screen->y = proj->viewport_size.y - (win_y_norm * proj->viewport_size.y);
    if (out_inv_w_half != nullptr) {
        *out_inv_w_half = inv_w_half;
    }
    return true;
}

// ============================================================================
// Aim Priority & Mouse Sensitivity GCD Quantizer (@ 0x180314D40..0x180314D54)
// ============================================================================

// sub_18004ACB0
bool try_claim_aim_priority(uint32_t priority) noexcept {
    const uint32_t prev = g_aim_priority;
    if (priority >= prev) {
        g_aim_priority = priority;
    }
    return priority >= prev;
}

// sub_18004ACD0
math::Vec2f queue_aim_rotation_delta(math::Vec2f delta) noexcept {
    g_pending_aim_delta = math::vec2_add(g_pending_aim_delta, delta);
    g_aim_idle_ticks = 0;
    return g_pending_aim_delta;
}

// sub_18004AD00
math::Vec2f get_pending_aim_rotation_delta() noexcept {
    return g_pending_aim_delta;
}

// sub_18004AD10
void update_mouse_sensitivity_step(events::JniContext* jni, jobject mc) noexcept {
    jobject settings = mc_get_game_settings(jni, mc);
    const float sens = settings_get_mouse_sensitivity(jni, settings);
    const float f = sens * 0.6000000238418579f + 0.20000000298023224f;
    g_mouse_sensitivity_step = (f * f * f * 8.0f) * 0.15000000596046448f;

    const int32_t prev_idle = g_aim_idle_ticks++;
    if (prev_idle > 0) {
        g_pending_aim_delta = {0.0f, 0.0f};
    }
    g_aim_priority = 0;
}

// sub_18004ADA0
void quantize_aim_axis_remainder(float* in_out_delta) noexcept {
    const float val = *in_out_delta;
    const float step = g_mouse_sensitivity_step;
    float snapped = 0.0f;
    if (val <= -step || val >= step) {
        snapped = std::trunc(val / step) * step;
    }
    *in_out_delta = val - snapped;
}

// sub_18004AE00
void apply_pending_aim_rotation(
    events::JniContext* jni,
    jobject player,
    bool update_render_arm) noexcept {
    const float step = g_mouse_sensitivity_step;
    if (step == 0.0f) {
        return;
    }

    const float raw_yaw = g_pending_aim_delta.x;
    float yaw_step = 0.0f;
    if (raw_yaw <= -step || raw_yaw >= step) {
        yaw_step = std::trunc(raw_yaw / step) * step;
    }
    g_pending_aim_delta.x = raw_yaw - yaw_step;

    const float raw_pitch = g_pending_aim_delta.y;
    float pitch_step = 0.0f;
    if (raw_pitch <= -step || raw_pitch >= step) {
        pitch_step = std::trunc(raw_pitch / step) * step;
    }
    g_pending_aim_delta.y = raw_pitch - pitch_step;

    if (yaw_step != 0.0f || pitch_step != 0.0f) {
        const math::Vec2f cur_rot = entity_get_rotation(jni, player);
        const math::Vec2f next_rot = math::vec2_add(cur_rot, {yaw_step, pitch_step});
        entity_set_rotation(jni, player, next_rot);
        entity_set_prev_rotation(jni, player, next_rot);
        if (update_render_arm) {
            player_sp_set_prev_render_arm_rotation(jni, player, next_rot);
            player_sp_set_render_arm_rotation(jni, player, next_rot);
        }
    }
}

// ============================================================================
// Silent Rotation Spoofing & WASD Move-Fix State Machine (@ 0x180314D58..0x180314D91)
// ============================================================================

// sub_18004AF70
void apply_move_fix_if_active(events::GameContext* ctx) noexcept {
    if (g_move_fix_enabled) {
        apply_wasd_move_fix(ctx->jni, true);
        g_move_fix_applied = true;
    }
}

// sub_18004AFA0
void apply_wasd_move_fix(events::JniContext* jni, bool enable) noexcept {
    jobject mc = mc_get_instance(jni);
    if (!mc_has_ingame_focus(jni, mc)) {
        return;
    }

    jobject settings = mc_get_game_settings(jni, mc);
    jobject key_fwd   = settings_get_key_forward(jni, settings);
    jobject key_back  = settings_get_key_back(jni, settings);
    jobject key_left  = settings_get_key_left(jni, settings);
    jobject key_right = settings_get_key_right(jni, settings);

    if (enable) {
        const uint8_t mask =
            (key_binding_is_pressed(jni, key_fwd)   ? 1u : 0u) |
            (key_binding_is_pressed(jni, key_back)  ? 2u : 0u) |
            (key_binding_is_pressed(jni, key_left)  ? 4u : 0u) |
            (key_binding_is_pressed(jni, key_right) ? 8u : 0u);
        g_saved_wasd_mask = mask;

        jobject player = mc_get_player(jni, mc);
        const math::Vec2f player_rot = entity_get_rotation(jni, player);
        const float yaw_diff = math::wrap_degrees_180(player_rot.x - g_spoof_target_rot.x);

        const int8_t fwd_in = static_cast<int8_t>((mask & 1u) != 0 ? 1 : 0) -
                              static_cast<int8_t>((mask & 2u) != 0 ? 1 : 0);
        const int8_t str_in = static_cast<int8_t>((mask & 4u) != 0 ? 1 : 0) -
                              static_cast<int8_t>((mask & 8u) != 0 ? 1 : 0);

        WasdOctant out_dir{0, 0};
        if (fwd_in != 0 || str_in != 0) {
            int32_t base_octant = 0;
            if (fwd_in != 0) {
                if (fwd_in == -1) {
                    base_octant = 5 - (static_cast<int32_t>(str_in) + 1);
                } else {
                    base_octant = kForwardStrafeOctants[static_cast<int32_t>(str_in) + 1];
                }
            } else {
                base_octant = (str_in == 1) ? 2 : ((str_in == -1) ? 6 : 0);
            }

            const int32_t rot_steps = static_cast<int32_t>(std::round(yaw_diff / 45.0f));
            out_dir = kWasdOctants[(base_octant + rot_steps) & 7];
        }

        key_binding_set_pressed(jni, key_fwd,   out_dir.forward ==  1);
        key_binding_set_pressed(jni, key_back,  out_dir.forward == -1);
        key_binding_set_pressed(jni, key_left,  out_dir.strafe  ==  1);
        key_binding_set_pressed(jni, key_right, out_dir.strafe  == -1);
    } else {
        key_binding_set_pressed(jni, key_fwd,   (g_saved_wasd_mask & 1u) != 0);
        key_binding_set_pressed(jni, key_back,  (g_saved_wasd_mask & 2u) != 0);
        key_binding_set_pressed(jni, key_left,  (g_saved_wasd_mask & 4u) != 0);
        key_binding_set_pressed(jni, key_right, (g_saved_wasd_mask & 8u) != 0);
    }
}

// sub_18004B230
void update_rotation_spoof_state(
    events::GameContext* ctx,
    bool active,
    bool move_fix,
    bool silent_restore,
    math::Vec2f target_rotation) noexcept {
    if (g_move_fix_applied) {
        apply_wasd_move_fix(ctx->jni, false);
        g_move_fix_applied = false;
        g_move_fix_enabled = false;
    }

    if (active) {
        if (!g_rotation_spoof_active && ctx->local_player != nullptr) {
            g_rotation_spoof_active = true;
            g_server_spoof_rot = entity_get_rotation(ctx->jni, ctx->local_player);
        }
        g_spoof_silent_restore = silent_restore;
        g_spoof_target_rot = target_rotation;
        g_move_fix_enabled = move_fix;
    } else if (g_rotation_spoof_active) {
        g_rotation_spoof_active = false;
        const math::Vec2f restore_src = g_spoof_silent_restore ? g_spoof_target_rot : g_server_spoof_rot;
        const math::Vec2f cur_rot = entity_get_rotation(ctx->jni, ctx->local_player);
        const float synced_yaw = cur_rot.x + math::wrap_degrees_180(restore_src.x - cur_rot.x);
        const math::Vec2f final_rot{synced_yaw, restore_src.y};

        entity_set_rotation(ctx->jni, ctx->local_player, final_rot);
        entity_set_prev_rotation(ctx->jni, ctx->local_player, final_rot);
        player_sp_set_prev_render_arm_rotation(ctx->jni, ctx->local_player, final_rot);
        player_sp_set_render_arm_rotation(ctx->jni, ctx->local_player, final_rot);
    }
}

// sub_18004B3A0
void swap_render_spoof_rotations(
    events::JniContext* jni,
    jobject player,
    bool enter_render) noexcept {
    math::Vec2f next_rot{};
    math::Vec2f next_prev_rot{};
    if (enter_render) {
        g_saved_render_rot = entity_get_rotation(jni, player);
        g_saved_prev_render_rot = entity_get_prev_rotation(jni, player);
        next_rot = g_server_spoof_rot;
        next_prev_rot = g_server_spoof_rot;
    } else {
        next_rot = g_saved_render_rot;
        next_prev_rot = g_saved_prev_render_rot;
    }
    entity_set_rotation(jni, player, next_rot);
    entity_set_prev_rotation(jni, player, next_prev_rot);
}

// sub_18004B420
bool is_rotation_spoof_active() noexcept {
    return g_rotation_spoof_active;
}

// sub_18004B430
math::Vec2f get_server_spoof_rotation() noexcept {
    return g_server_spoof_rot;
}

// sub_18004B440
void set_server_spoof_rotation(math::Vec2f rotation) noexcept {
    const float yaw_delta = math::wrap_degrees_180(rotation.x - g_server_spoof_rot.x);
    g_server_spoof_dirty = true;
    g_server_spoof_rot.x += yaw_delta;
    g_server_spoof_rot.y = rotation.y;
}

// sub_18004B490
math::Vec2f get_target_spoof_rotation() noexcept {
    return g_spoof_target_rot;
}

// sub_18004B4A0
void pre_living_update_spoof_rotation(events::JniContext* jni) noexcept {
    jobject mc = mc_get_instance(jni);
    jobject player = mc_get_player(jni, mc);
    if (player == nullptr) {
        return;
    }

    g_pre_living_saved_rot = entity_get_rotation(jni, player);
    if (g_server_spoof_dirty) {
        g_server_spoof_dirty = false;
        entity_set_rotation(jni, player, g_server_spoof_rot);
        entity_set_prev_rotation(jni, player, g_server_spoof_rot);
    }
    if (g_rotation_spoof_active) {
        g_living_update_spoofed = true;
        entity_set_rotation(jni, player, g_server_spoof_rot);
    }
}

// sub_18004B540
void post_living_update_restore_rotation(events::JniContext* jni, jobject player) noexcept {
    if (g_living_update_spoofed) {
        g_server_spoof_rot = entity_get_rotation(jni, player);
        g_living_update_spoofed = false;
        entity_set_rotation(jni, player, g_pre_living_saved_rot);
        entity_set_prev_rotation(jni, player, g_pre_living_saved_rot);
    }
}

// ============================================================================
// Combat Target Tracker & Sprint-Slowdown Reach Evaluator
// ============================================================================

// sub_18004B5B0
void evaluate_attack_slowdown(events::GameContext* ctx, events::TargetTracker* tracker) noexcept {
    if (tracker == nullptr || (tracker->flags & kTargetTrackerSlowdownEvaluated) != 0) {
        return;
    }

    bool moving_away = true;
    events::JniContext* jni = ctx->jni;
    jobject mop = mc_get_object_mouse_over(jni, ctx->minecraft);
    if (mop != nullptr && mop_get_type_ordinal(jni, mop) == 2) {
        jobject hit_vec_obj = mop_get_hit_vec(jni, mop);
        const math::Vec3d hit_vec = vec3_to_vec3d(jni, hit_vec_obj);

        math::Vec3d eye_pos = entity_get_position(jni, ctx->local_player);
        if (!is_version_1_7_10(jni)) {
            eye_pos = math::vec3_add_eye_height(eye_pos);
        }

        const float movement_factor = entity_is_on_ground(jni, ctx->local_player)
                                          ? living_get_land_movement_factor(jni, ctx->local_player)
                                          : 0.026000000536441803f;

        const math::Vec2f rot = entity_get_rotation(jni, ctx->local_player);
        const float move_fwd = living_get_move_forward(jni, ctx->local_player);
        const float move_str = living_get_move_strafing(jni, ctx->local_player);
        const math::Vec3d strafe_delta = compute_strafe_motion(rot, move_fwd, move_str, movement_factor);

        const math::Vec3d motion_normal = entity_get_motion(jni, ctx->local_player);
        math::Vec3d motion_slowed = motion_normal;
        motion_slowed.x *= 0.6;
        motion_slowed.z *= 0.6;

        const math::Vec3d next_eye_normal = math::vec3_add(eye_pos, math::vec3_add(motion_normal, strafe_delta));
        const math::Vec3d next_eye_slowed = math::vec3_add(eye_pos, math::vec3_add(motion_slowed, strafe_delta));

        const double dist_sq_slowed = math::vec3_dist_sq(next_eye_slowed, hit_vec);
        const double dist_sq_normal = math::vec3_dist_sq(next_eye_normal, hit_vec);
        moving_away = dist_sq_normal > dist_sq_slowed;
    }

    tracker->pad = moving_away ? 1u : 0u;
    tracker->flags = (tracker->flags & ~kTargetTrackerMovingAway) |
                     kTargetTrackerSlowdownEvaluated |
                     (moving_away ? kTargetTrackerMovingAway : 0u);
}

// sub_18004B970
bool update_target_tracker(events::GameContext* ctx, events::TargetTracker* tracker) noexcept {
    tracker->flags = 0;
    tracker->pad = 0;

    events::JniContext* jni = ctx->jni;
    JNIEnv* env = jni->env;

    if (ctx->world == nullptr) {
        if (tracker->target_entity != nullptr) {
            env->DeleteGlobalRef(tracker->target_entity);
        }
        tracker->target_entity = nullptr;
        return true;
    }

    math::Vec3d eye_pos = entity_get_position(jni, ctx->local_player);
    if (!is_version_1_7_10(jni)) {
        eye_pos = math::vec3_add_eye_height(eye_pos);
    }

    auto assign_priority_target = [&](jobject entity) noexcept -> bool {
        if (tracker->target_entity != nullptr) {
            env->DeleteGlobalRef(tracker->target_entity);
        }
        jobject global_ref = env->NewGlobalRef(entity);
        if (global_ref != nullptr) {
            tracker->target_entity = global_ref;
        }
        const math::AABB raw_box = aabb_to_aabb(jni, entity_get_bounding_box(jni, entity));
        const math::AABB expanded_box = math::aabb_expand(raw_box, 0.1);
        const math::Vec3d closest_pt = math::aabb_closest_point(expanded_box, eye_pos);
        tracker->distance_sq = math::vec3_dist_sq(eye_pos, closest_pt);
        return true;
    };

    // 1. Pointed entity under crosshair (EntityOtherPlayerMP, alive, not friend/bot/teammate)
    jobject mop = mc_get_object_mouse_over(jni, ctx->minecraft);
    if (mop != nullptr && mop_get_type_ordinal(jni, mop) == 2) {
        jobject hit_entity = mop_get_entity_hit(jni, mop);
        jobject other_player = as_entity_other_player_mp(jni, hit_entity);
        if (other_player != nullptr &&
            !player_has_tag_flags(jni, other_player, kPlayerTagExcludedMask) &&
            living_get_health(jni, other_player) > 0.0f) {
            return assign_priority_target(other_player);
        }
    }

    // 2. Active AimAssist target entity
    jobject aim_target = get_aimassist_target_entity();
    if (aim_target != nullptr) {
        return assign_priority_target(aim_target);
    }

    // 3. Closest valid player within 15 blocks (225.0 sq) and ~70-degree FOV cone
    const math::Vec2f player_rot = entity_get_rotation(jni, ctx->local_player);
    jobject player_list = world_get_player_entities(jni, ctx->world);
    jobject best_entity = nullptr;
    double best_dist_sq = 225.0;

    for (int32_t i = 0; i < java_list_size(jni, player_list); ++i) {
        jobject candidate = java_list_get(jni, player_list, i);
        if (env->IsSameObject(ctx->local_player, candidate) ||
            player_has_tag_flags(jni, candidate, kPlayerTagExcludedMask) ||
            living_get_health(jni, candidate) <= 0.0f ||
            player_is_sleeping(jni, candidate)) {
            continue;
        }

        const math::AABB raw_box = aabb_to_aabb(jni, entity_get_bounding_box(jni, candidate));
        const math::AABB expanded_box = math::aabb_expand(raw_box, 0.1);
        const math::Vec3d closest_pt = math::aabb_closest_point(expanded_box, eye_pos);
        const double dist_sq = math::vec3_dist_sq(eye_pos, closest_pt);
        if (dist_sq <= best_dist_sq) {
            const math::Vec3d cand_pos = entity_get_position(jni, candidate);
            const math::Vec2f aim_angles = math::calc_yaw_pitch(eye_pos, cand_pos);
            const float dyaw = math::wrap_degrees_180(aim_angles.x - player_rot.x);
            const float dpitch = aim_angles.y - player_rot.y;
            if ((dyaw * dyaw + dpitch + dpitch) <= 4900.0f) {
                best_dist_sq = dist_sq;
                best_entity = candidate;
            }
        }
    }

    if (best_entity != nullptr) {
        tracker->distance_sq = best_dist_sq;
    }

    if (tracker->target_entity != nullptr) {
        env->DeleteGlobalRef(tracker->target_entity);
    }

    jobject new_global = nullptr;
    if (best_entity != nullptr) {
        new_global = env->NewGlobalRef(best_entity);
        if (new_global == nullptr) {
            return false;
        }
    }
    tracker->target_entity = new_global;
    return true;
}

// sub_18004BF00
void clear_target_tracker(events::GameContext* ctx, events::TargetTracker* tracker) noexcept {
    if (tracker->target_entity != nullptr) {
        ctx->jni->env->DeleteGlobalRef(tracker->target_entity);
    }
    tracker->target_entity = nullptr;
}

// ============================================================================
// Hotbar Lock, Inventory Grid Coordinates & Item Filter List Callbacks
// ============================================================================

// sub_18004BF40
bool try_acquire_hotbar_lock(const void* owner) noexcept {
    if (g_hotbar_lock_owner != owner) {
        if (g_hotbar_lock_owner != nullptr) {
            return false;
        }
        g_hotbar_lock_owner = owner;
    }
    return true;
}

// sub_18004BF70
void release_hotbar_lock(const void* owner) noexcept {
    if (g_hotbar_lock_owner == owner) {
        g_hotbar_lock_owner = nullptr;
    }
}

// sub_18004BF90
math::Vec2f get_inventory_slot_grid_pos(uint32_t slot_index) noexcept {
    if (slot_index > 8u) {
        const float col = static_cast<float>(slot_index % 9u);
        const float row = static_cast<float>(static_cast<int32_t>(slot_index / 9u + 3u));
        return {row, col};
    }
    const float armor_craft_row = static_cast<float>(static_cast<int32_t>(slot_index) - 5);
    return {armor_craft_row, 1.0f};
}

// sub_18004BFE0
size_t validate_item_name_input(char* buffer, char ch) noexcept {
    const size_t len = std::strlen(buffer);
    const uint8_t uch = static_cast<uint8_t>(ch);
    if (len <= 0x63u &&
        (static_cast<uint8_t>((uch & 0xDFu) - 'A') < 26u ||
         static_cast<uint8_t>(uch - '0') < 10u ||
         ch == ' ')) {
        buffer[len] = ch;
    }
    return len;
}

// sub_18004C030
size_t item_filter_list_count(void* ctx) noexcept {
    const auto* vec = static_cast<const util::PodVector<ItemIdEntry>*>(ctx);
    return static_cast<size_t>(static_cast<uint32_t>(vec->count));
}

// sub_18004C040
const char* item_filter_list_get_name(void* ctx, size_t index) noexcept {
    const auto* vec = static_cast<const util::PodVector<ItemIdEntry>*>(ctx);
    return vec->data[index].name;
}

// sub_18004C0E0
int compare_item_filter_entry(
    const void* /*unused_ctx*/,
    const ItemIdEntry* a,
    const ItemIdEntry* b) noexcept {
    return a->id - b->id;
}

// sub_18004C070
bool item_filter_list_add(
    void* ctx,
    const char* name,
    const math::Color3f* /*color*/) noexcept {
    const int item_id = find_item_id_by_name(name);
    if (item_id == -1) {
        return false;
    }

    auto* vec = static_cast<util::PodVector<ItemIdEntry>*>(ctx);
    ItemIdEntry entry{};
    entry.name = find_item_name_by_id(item_id);
    entry.id = item_id;
    entry.pad = 0;

    // Sorted unique binary-search insert matching sub_18002F380
    uint32_t lo = 0;
    uint32_t hi = static_cast<uint32_t>(vec->count);
    while (lo < hi) {
        const uint32_t mid = lo + ((hi - lo) >> 1);
        const int cmp = compare_item_filter_entry(nullptr, &entry, &vec->data[mid]);
        if (cmp < 0) {
            hi = mid;
        } else if (cmp == 0) {
            return true;
        } else {
            lo = mid + 1;
        }
    }
    vec->insert_at(static_cast<int>(lo), entry);
    return true;
}

// sub_18004C0F0
void item_filter_list_remove(void* ctx, size_t index) noexcept {
    auto* vec = static_cast<util::PodVector<ItemIdEntry>*>(ctx);
    vec->erase_at(static_cast<int>(index));
}

// sub_18004C110
void item_filter_list_clear(void* ctx) noexcept {
    auto* vec = static_cast<util::PodVector<ItemIdEntry>*>(ctx);
    vec->count = 0;
}

// sub_18004C120
bool item_filter_list_contains_id(const util::PodVector<ItemIdEntry>* list, int32_t item_id) noexcept {
    if (list == nullptr || list->count <= 0) {
        return false;
    }

    ItemIdEntry key{nullptr, item_id, 0};
    uint32_t lo = 0;
    uint32_t hi = static_cast<uint32_t>(list->count);
    while (lo < hi) {
        const uint32_t mid = lo + ((hi - lo) >> 1);
        const int cmp = compare_item_filter_entry(nullptr, &key, &list->data[mid]);
        if (cmp < 0) {
            hi = mid;
        } else if (cmp == 0) {
            return true;
        } else {
            lo = mid + 1;
        }
    }
    return false;
}

// ============================================================================
// Client-Side Player Entity Tags (Friend / AntiBot / Teammate)
// ============================================================================

// sub_18004C170
uint16_t player_get_tag_flags(events::JniContext* jni, jobject player) noexcept {
    const uint32_t raw = std::bit_cast<uint32_t>(player_get_speed_on_ground(jni, player));
    if ((raw & kPlayerTagMagicMask) != kPlayerTagMagicPrefix) {
        return 0;
    }
    return static_cast<uint16_t>(raw & 0xFFFFu);
}

// sub_18004C1A0
bool player_has_tag_flags(events::JniContext* jni, jobject player, uint32_t mask) noexcept {
    return (static_cast<uint32_t>(player_get_tag_flags(jni, player)) & mask) != 0;
}

// sub_18004C1E0
void player_add_tag_flags(events::JniContext* jni, jobject player, uint32_t flags) noexcept {
    const uint32_t raw = std::bit_cast<uint32_t>(player_get_speed_on_ground(jni, player));
    const uint32_t base = ((raw & kPlayerTagMagicMask) == kPlayerTagMagicPrefix) ? raw : kPlayerTagMagicPrefix;
    const uint32_t updated = kPlayerTagMagicPrefix | ((base | flags) & 0xFFFFu);
    player_set_speed_on_ground(jni, player, std::bit_cast<float>(updated));
}

// sub_18004C240
void player_remove_tag_flags(events::JniContext* jni, jobject player, uint32_t flags) noexcept {
    const uint32_t raw = std::bit_cast<uint32_t>(player_get_speed_on_ground(jni, player));
    const uint32_t current = ((raw & kPlayerTagMagicMask) == kPlayerTagMagicPrefix) ? (raw & 0xFFFFu) : 0u;
    const uint32_t updated = kPlayerTagMagicPrefix | (current & ~flags & 0xFFFFu);
    player_set_speed_on_ground(jni, player, std::bit_cast<float>(updated));
}

// ============================================================================
// Auto-Block / Scaffold Hotbar Slot Selector & Depletion Tracker
// ============================================================================

// sub_18004C2A0
void select_hotbar_slot_by_scorer(
    events::GameContext* ctx,
    const void* owner,
    bool allow_any_scored,
    HotbarItemScoreFn score_fn) noexcept {
    if (g_block_selector_owner != nullptr || (!allow_any_scored && g_depleted_block_id == 0)) {
        return;
    }

    events::JniContext* jni = ctx->jni;
    jobject inv = player_get_inventory(jni, ctx->local_player);
    const int32_t wanted_id = g_depleted_block_id;
    const int32_t start_slot = inv_player_get_current_item(jni, inv) + static_cast<int32_t>(g_advance_from_depleted_slot);
    jobjectArray main_inv = inv_player_get_main_inventory(jni, inv);
    JNIEnv* env = jni->env;

    int32_t chosen_slot = -1;
    int32_t best_score = -1;

    for (int32_t offset = 0; offset < 9; ++offset) {
        const int32_t slot = (start_slot + offset) % 9;
        jobject stack = env->GetObjectArrayElement(main_inv, static_cast<jsize>(slot));
        if (stack == nullptr) {
            continue;
        }
        jobject item = item_stack_get_item(jni, stack);
        if (item == nullptr) {
            continue;
        }
        const int32_t item_id = item_get_id(jni, item);
        if (wanted_id != 0 && item_id == wanted_id) {
            chosen_slot = slot;
            break;
        }
        const int32_t score = score_fn(item_id);
        if (score > best_score) {
            best_score = score;
            chosen_slot = slot;
        }
    }

    if (chosen_slot != -1 && try_acquire_hotbar_lock(owner)) {
        inv_player_set_current_item(jni, inv, chosen_slot);
        g_block_selector_owner = owner;
    }
}

// sub_18004C410
void select_hotbar_block_slot(
    events::GameContext* ctx,
    const void* owner,
    bool allow_any_block) noexcept {
    select_hotbar_slot_by_scorer(ctx, owner, allow_any_block, score_placeable_block_id);
}

// sub_18004C430
int score_placeable_block_id(int item_id) noexcept {
    switch (item_id) {
        // Primary solid PvP/BedWars/SkyWars blocks: Stone, Grass, Dirt, Cobblestone, Wool, Brick
        case 1:
        case 2:
        case 3:
        case 4:
        case 35:
        case 45:
            return 2;

        // Secondary solid blocks: Planks, Log, Glass, Sandstone, Netherrack
        case 5:
        case 17:
        case 20:
        case 24:
        case 87:
            return 1;

        default: {
            // Bitmask 0x24000000001ULL for End Stone (121), Stained Clay (159), Log2 (162)
            const uint32_t offset = static_cast<uint32_t>(item_id - 121);
            constexpr uint64_t kHighSolidMask = 0x24000000001ULL;
            if (offset <= 0x29u && ((kHighSolidMask >> offset) & 1ULL) != 0ULL) {
                return 1;
            }
            return -1;
        }
    }
}

// sub_18004C480
void release_hotbar_block_selector() noexcept {
    if (g_block_selector_owner != nullptr) {
        release_hotbar_lock(g_block_selector_owner);
        g_block_selector_owner = nullptr;
    }
    ++g_ticks_since_block_place;
    g_depleted_block_id = 0;
    g_advance_from_depleted_slot = 0;
}

// sub_18004C4D0
void pre_place_record_held_block(events::GameContext* ctx) noexcept {
    if (g_recorded_place_block_id != 0) {
        return;
    }

    events::JniContext* jni = ctx->jni;
    jobject inv = player_get_inventory(jni, ctx->local_player);
    const uint32_t slot = static_cast<uint32_t>(inv_player_get_current_item(jni, inv));
    jobjectArray main_inv = inv_player_get_main_inventory(jni, inv);
    jobject stack = jni->env->GetObjectArrayElement(main_inv, static_cast<jsize>(slot));
    if (stack == nullptr) {
        return;
    }

    jobject item = item_stack_get_item(jni, stack);
    if (is_block_item(jni, item)) {
        g_recorded_place_block_id = item_get_id(jni, item);
        const int32_t size = item_stack_get_size(jni, stack);
        g_recorded_place_stack_size = size;
        if (g_ticks_since_block_place > 20u || g_last_placed_hotbar_slot != slot) {
            g_remaining_tracked_stack_count = size;
        }
        g_ticks_since_block_place = 0;
        g_last_placed_hotbar_slot = slot;
    }
}

// sub_18004C5B0
void post_place_check_held_block_depleted(events::GameContext* ctx) noexcept {
    if (g_recorded_place_block_id == 0) {
        return;
    }

    events::JniContext* jni = ctx->jni;
    jobject inv = player_get_inventory(jni, ctx->local_player);
    const uint32_t slot = static_cast<uint32_t>(inv_player_get_current_item(jni, inv));
    jobjectArray main_inv = inv_player_get_main_inventory(jni, inv);
    jobject stack = jni->env->GetObjectArrayElement(main_inv, static_cast<jsize>(slot));

    bool depleted = false;
    if (stack == nullptr) {
        depleted = true;
    } else {
        jobject item = item_stack_get_item(jni, stack);
        if (item == nullptr || item_get_id(jni, item) == 0) {
            depleted = true;
        } else if (item_stack_get_size(jni, stack) < g_recorded_place_stack_size) {
            --g_remaining_tracked_stack_count;
            if (g_remaining_tracked_stack_count == 0) {
                depleted = true;
            }
        }
    }

    if (depleted) {
        g_depleted_block_id = g_recorded_place_block_id;
        g_advance_from_depleted_slot = 1;
    }
    g_recorded_place_block_id = 0;
}

// ============================================================================
// AimAssist Target & Mouse Delta Interop
// ============================================================================

// sub_18004C680
jobject get_aimassist_target_entity() noexcept {
    return g_aimassist_target_entity;
}

// sub_18004C690
math::Vec2f accumulate_aimassist_mouse_delta(math::Vec2f delta) noexcept {
    g_aimassist_mouse_delta = math::vec2_add(g_aimassist_mouse_delta, delta);
    return g_aimassist_mouse_delta;
}

} // namespace slinky::minecraft
