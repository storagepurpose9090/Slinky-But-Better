#include "minecraft/mc_wrappers.hpp"

#include <cmath>

namespace slinky::minecraft {

// @ 0x18016BC00 - EnumFacing unit vectors: DOWN(0), UP(1), NORTH(2), SOUTH(3), WEST(4), EAST(5)
const math::Vec3d kEnumFacingOffsets[6] = {
    { 0.0, -1.0,  0.0},
    { 0.0,  1.0,  0.0},
    { 0.0,  0.0, -1.0},
    { 0.0,  0.0,  1.0},
    {-1.0,  0.0,  0.0},
    { 1.0,  0.0,  0.0},
};

// sub_180044040
jobject mc_get_instance(events::JniContext* jni) noexcept {
    return (jni->env->GetStaticObjectField(jni->mappings->cls_minecraft, jni->mappings->sfid_mc_the_minecraft));
}

// sub_180044070
jobject mc_get_player(events::JniContext* jni, jobject mc) noexcept {
    return (jni->env->GetObjectField(mc, jni->mappings->fid_mc_the_player));
}

// sub_180044090
jobject mc_get_world(events::JniContext* jni, jobject mc) noexcept {
    return (jni->env->GetObjectField(mc, jni->mappings->fid_mc_the_world));
}

// sub_1800440B0
jobject mc_get_timer(events::JniContext* jni, jobject mc) noexcept {
    return (jni->env->GetObjectField(mc, jni->mappings->fid_mc_timer));
}

// sub_1800440D0
jobject mc_get_gui_container_screen(events::JniContext* jni, jobject mc) noexcept {
    jobject screen = jni->env->GetObjectField(mc, jni->mappings->fid_mc_current_screen);
    return jni->env->IsInstanceOf(screen, jni->mappings->cls_gui_container) ? screen : nullptr;
}

// sub_180044130
jobject mc_get_game_settings(events::JniContext* jni, jobject mc) noexcept {
    return (jni->env->GetObjectField(mc, jni->mappings->fid_mc_game_settings));
}

// sub_180044150
jobject mc_get_net_handler(events::JniContext* jni, jobject mc) noexcept {
    return (jni->env->CallObjectMethod(mc, jni->mappings->mid_mc_get_net_handler));
}

// sub_180044180
jobject mc_get_render_manager(events::JniContext* jni, jobject mc) noexcept {
    if (jni->mappings->version != 0) {
        return jni->env->GetObjectField(mc, jni->mappings->fid_mc_render_manager);
    }
    return render_manager_get_instance_1_7(jni);
}

// sub_1800441B0
jobject mc_get_render_global(events::JniContext* jni, jobject mc) noexcept {
    return (jni->env->GetObjectField(mc, jni->mappings->fid_mc_render_global));
}

// sub_1800441D0
jobject mc_get_entity_renderer(events::JniContext* jni, jobject mc) noexcept {
    return (jni->env->GetObjectField(mc, jni->mappings->fid_mc_entity_renderer));
}

// sub_1800441F0
jobject mc_get_render_item(events::JniContext* jni, jobject mc) noexcept {
    return (jni->env->GetObjectField(mc, jni->mappings->fid_mc_render_item));
}

// sub_180044210
jobject mc_get_texture_manager(events::JniContext* jni, jobject mc) noexcept {
    return (jni->env->GetObjectField(mc, jni->mappings->fid_mc_render_engine));
}

// sub_180044230
jobject mc_get_player_controller(events::JniContext* jni, jobject mc) noexcept {
    return (jni->env->GetObjectField(mc, jni->mappings->fid_mc_player_controller));
}

// sub_180044250
jobject mc_get_object_mouse_over(events::JniContext* jni, jobject mc) noexcept {
    return (jni->env->GetObjectField(mc, jni->mappings->fid_mc_object_mouse_over));
}

// sub_180044270
void mc_set_object_mouse_over(events::JniContext* jni, jobject mc, jobject mop) noexcept {
    jni->env->SetObjectField(mc, jni->mappings->fid_mc_object_mouse_over, mop);
}

// sub_1800442A0
bool mc_has_ingame_focus(events::JniContext* jni, jobject mc) noexcept {
    return jni->env->GetBooleanField(mc, jni->mappings->fid_mc_in_game_has_focus) != JNI_FALSE;
}

// sub_1800442D0
int32_t mc_get_left_click_counter(events::JniContext* jni, jobject mc) noexcept {
    return jni->env->GetIntField(mc, jni->mappings->fid_mc_left_click_counter);
}

// sub_1800442F0
void mc_set_left_click_counter(events::JniContext* jni, jobject mc, int32_t count) noexcept {
    jni->env->SetIntField(mc, jni->mappings->fid_mc_left_click_counter, count);
}

// sub_180044320
int32_t mc_get_right_click_delay_timer(events::JniContext* jni, jobject mc) noexcept {
    return jni->env->GetIntField(mc, jni->mappings->fid_mc_right_click_delay_timer);
}

// sub_180044340
void mc_set_right_click_delay_timer(events::JniContext* jni, jobject mc, int32_t delay) noexcept {
    jni->env->SetIntField(mc, jni->mappings->fid_mc_right_click_delay_timer, delay);
}

// sub_180044370
int32_t mc_get_display_width(events::JniContext* jni, jobject mc) noexcept {
    return jni->env->GetIntField(mc, jni->mappings->fid_mc_display_width);
}

// sub_180044390
int32_t mc_get_display_height(events::JniContext* jni, jobject mc) noexcept {
    return jni->env->GetIntField(mc, jni->mappings->fid_mc_display_height);
}

// sub_1800443B0
jobject mc_get_render_view_entity(events::JniContext* jni, jobject mc) noexcept {
    return (jni->env->GetObjectField(mc, jni->mappings->fid_mc_render_view_entity));
}

// sub_1800443D0
jobject mc_add_scheduled_task(events::JniContext* jni, jobject mc, jobject runnable) noexcept {
    return jni->env->CallObjectMethod(mc, jni->mappings->mid_mc_add_scheduled_task, runnable);
}

// sub_180044400
void mc_click_mouse(events::JniContext* jni, jobject mc) noexcept {
    jni->env->CallVoidMethod(mc, jni->mappings->mid_mc_click_mouse);
}

// sub_180044430
void mc_right_click_mouse(events::JniContext* jni, jobject mc) noexcept {
    jni->env->CallVoidMethod(mc, jni->mappings->mid_mc_right_click_mouse);
}

// sub_180044460
bool is_version_1_7_10(events::JniContext* jni) noexcept {
    return jni->mappings->version == 0;
}

// sub_180044470
math::Vec3d entity_get_position(events::JniContext* jni, jobject entity) noexcept {
    const double x = jni->env->GetDoubleField(entity, jni->mappings->fid_entity_pos_x);
    const double y = jni->env->GetDoubleField(entity, jni->mappings->fid_entity_pos_y);
    const double z = jni->env->GetDoubleField(entity, jni->mappings->fid_entity_pos_z);
    return math::make_vec3(x, y, z);
}

// sub_180044510
math::Vec3d entity_get_prev_position(events::JniContext* jni, jobject entity) noexcept {
    const double x = jni->env->GetDoubleField(entity, jni->mappings->fid_entity_prev_pos_x);
    const double y = jni->env->GetDoubleField(entity, jni->mappings->fid_entity_prev_pos_y);
    const double z = jni->env->GetDoubleField(entity, jni->mappings->fid_entity_prev_pos_z);
    return math::make_vec3(x, y, z);
}

// sub_1800445B0
math::Vec2f entity_get_rotation(events::JniContext* jni, jobject entity) noexcept {
    const float yaw   = jni->env->GetFloatField(entity, jni->mappings->fid_entity_rotation_yaw);
    const float pitch = jni->env->GetFloatField(entity, jni->mappings->fid_entity_rotation_pitch);
    return math::make_vec2(yaw, pitch);
}

// sub_180044620
void entity_set_rotation(events::JniContext* jni, jobject entity, math::Vec2f rotation) noexcept {
    jni->env->SetFloatField(entity, jni->mappings->fid_entity_rotation_yaw, rotation.x);
    jni->env->SetFloatField(entity, jni->mappings->fid_entity_rotation_pitch, math::clampf(-90.0f, 90.0f, rotation.y));
}

// sub_1800446B0
math::Vec2f entity_get_prev_rotation(events::JniContext* jni, jobject entity) noexcept {
    const float yaw   = jni->env->GetFloatField(entity, jni->mappings->fid_entity_prev_rotation_yaw);
    const float pitch = jni->env->GetFloatField(entity, jni->mappings->fid_entity_prev_rotation_pitch);
    return math::make_vec2(yaw, pitch);
}

// sub_180044720
void entity_set_prev_rotation(events::JniContext* jni, jobject entity, math::Vec2f rotation) noexcept {
    jni->env->SetFloatField(entity, jni->mappings->fid_entity_prev_rotation_yaw, rotation.x);
    jni->env->SetFloatField(entity, jni->mappings->fid_entity_prev_rotation_pitch, math::clampf(-90.0f, 90.0f, rotation.y));
}

// sub_1800447B0
void entity_set_angles(events::JniContext* jni, jobject entity, float yaw_delta, float pitch_delta) noexcept {
    jni->env->CallVoidMethod(entity, jni->mappings->mid_entity_set_angles, yaw_delta, pitch_delta);
}

// sub_1800447F0
math::Vec3d entity_get_look_vector(events::JniContext* jni, jobject entity) noexcept {
    const float yaw   = jni->env->GetFloatField(entity, jni->mappings->fid_entity_rotation_yaw);
    const float pitch = jni->env->GetFloatField(entity, jni->mappings->fid_entity_rotation_pitch);
    const float yaw_rad   = static_cast<float>((static_cast<double>(yaw) * -math::kPi / 180.0) - math::kPi);
    const float cos_yaw   = std::cos(yaw_rad);
    const float sin_yaw   = std::sin(yaw_rad);
    const float pitch_rad = static_cast<float>(static_cast<double>(pitch) * -math::kPi / 180.0);
    const float neg_cos_p = -std::cos(pitch_rad);
    const float sin_p     = std::sin(pitch_rad);
    return math::make_vec3(static_cast<double>(sin_yaw * neg_cos_p), static_cast<double>(sin_p), static_cast<double>(neg_cos_p * cos_yaw));
}

// sub_180044930
double entity_get_motion_x(events::JniContext* jni, jobject entity) noexcept {
    return jni->env->GetDoubleField(entity, jni->mappings->fid_entity_motion_x);
}

// sub_180044950
double entity_get_motion_y(events::JniContext* jni, jobject entity) noexcept {
    return jni->env->GetDoubleField(entity, jni->mappings->fid_entity_motion_y);
}

// sub_180044970
double entity_get_motion_z(events::JniContext* jni, jobject entity) noexcept {
    return jni->env->GetDoubleField(entity, jni->mappings->fid_entity_motion_z);
}

// sub_180044990
math::Vec3d entity_get_motion(events::JniContext* jni, jobject entity) noexcept {
    const double x = jni->env->GetDoubleField(entity, jni->mappings->fid_entity_motion_x);
    const double y = jni->env->GetDoubleField(entity, jni->mappings->fid_entity_motion_y);
    const double z = jni->env->GetDoubleField(entity, jni->mappings->fid_entity_motion_z);
    return math::make_vec3(x, y, z);
}

// sub_180044A30
void entity_set_motion(events::JniContext* jni, jobject entity, const math::Vec3d& motion) noexcept {
    jni->env->SetDoubleField(entity, jni->mappings->fid_entity_motion_x, motion.x);
    jni->env->SetDoubleField(entity, jni->mappings->fid_entity_motion_y, motion.y);
    jni->env->SetDoubleField(entity, jni->mappings->fid_entity_motion_z, motion.z);
}

// sub_180044AB0
jobject entity_get_bounding_box(events::JniContext* jni, jobject entity) noexcept {
    return (jni->env->GetObjectField(entity, jni->mappings->fid_entity_bounding_box));
}

// sub_180044AD0
bool entity_is_sprinting(events::JniContext* jni, jobject entity) noexcept {
    return jni->env->CallBooleanMethod(entity, jni->mappings->mid_entity_is_sprinting) != JNI_FALSE;
}

// sub_180044B00
void entity_set_sprinting(events::JniContext* jni, jobject entity, bool sprinting) noexcept {
    jni->env->CallVoidMethod(entity, jni->mappings->mid_entity_set_sprinting, static_cast<jboolean>(sprinting));
}

// sub_180044B30
bool entity_is_sneaking(events::JniContext* jni, jobject entity) noexcept {
    return jni->env->CallBooleanMethod(entity, jni->mappings->mid_entity_is_sneaking) != JNI_FALSE;
}

// sub_180044B60
int32_t entity_get_id(events::JniContext* jni, jobject entity) noexcept {
    return jni->env->GetIntField(entity, jni->mappings->fid_entity_id);
}

// sub_180044B80
jobject entity_get_unique_id(events::JniContext* jni, jobject entity) noexcept {
    return (jni->env->GetObjectField(entity, jni->mappings->fid_entity_unique_id));
}

// sub_180044BA0
int32_t entity_get_ticks_existed(events::JniContext* jni, jobject entity) noexcept {
    return jni->env->GetIntField(entity, jni->mappings->fid_entity_ticks_existed);
}

// sub_180044BC0
bool entity_is_on_ground(events::JniContext* jni, jobject entity) noexcept {
    return jni->env->GetBooleanField(entity, jni->mappings->fid_entity_on_ground) != JNI_FALSE;
}

// sub_180044BF0
bool entity_is_dead(events::JniContext* jni, jobject entity) noexcept {
    return jni->env->GetBooleanField(entity, jni->mappings->fid_entity_is_dead) != JNI_FALSE;
}

// sub_180044C20
float entity_get_fall_distance(events::JniContext* jni, jobject entity) noexcept {
    return jni->env->GetFloatField(entity, jni->mappings->fid_entity_fall_distance);
}

// sub_180044C40
jobject entity_get_display_name(events::JniContext* jni, jobject entity) noexcept {
    return (jni->env->CallObjectMethod(entity, jni->mappings->mid_entity_get_display_name));
}

// sub_180044C70
jstring entity_get_name(events::JniContext* jni, jobject entity) noexcept {
    return static_cast<jstring>(jni->env->CallObjectMethod(entity, jni->mappings->mid_entity_get_name));
}

// sub_180044CA0
int32_t entity_get_type_id(events::JniContext* jni, jobject entity) noexcept {
    return jni->env->CallStaticIntMethod(jni->mappings->cls_entity_list, jni->mappings->smid_entity_list_get_entity_id, entity);
}

// sub_180044CE0
void entity_move_flying(events::JniContext* jni, jobject entity, float strafe, float forward, float friction) noexcept {
    jni->env->CallVoidMethod(entity, jni->mappings->mid_entity_move_flying, strafe, forward, friction);
}

// sub_180044D40
bool entity_can_be_collided_with(events::JniContext* jni, jobject entity) noexcept {
    return jni->env->CallBooleanMethod(entity, jni->mappings->mid_entity_can_be_collided_with) != JNI_FALSE;
}

// sub_180044D70
jobject entity_get_riding_entity(events::JniContext* jni, jobject entity) noexcept {
    return (jni->env->GetObjectField(entity, jni->mappings->fid_entity_riding_entity));
}

// sub_180044D90
jobject as_entity_living_base(events::JniContext* jni, jobject obj) noexcept {
    return jni->env->IsInstanceOf(obj, jni->mappings->cls_entity_living_base) ? obj : nullptr;
}

// sub_180044DD0
jobject living_get_position_eyes(events::JniContext* jni, jobject living) noexcept {
    return jni->env->CallObjectMethod(living, jni->mappings->mid_living_get_position_eyes, 1.0f);
}

// sub_180044E10
int32_t living_get_last_attacker_time(events::JniContext* jni, jobject living) noexcept {
    return jni->env->GetIntField(living, jni->mappings->fid_living_last_attacker_time);
}

// sub_180044E30
int32_t living_get_hurt_time(events::JniContext* jni, jobject living) noexcept {
    return jni->env->GetIntField(living, jni->mappings->fid_living_hurt_time);
}

// sub_180044E50
void living_set_max_hurt_time(events::JniContext* jni, jobject living, int32_t ticks) noexcept {
    jni->env->SetIntField(living, jni->mappings->fid_living_max_hurt_time, ticks);
}

// sub_180044E80
float living_get_health(events::JniContext* jni, jobject living) noexcept {
    const float hp = jni->env->CallFloatMethod(living, jni->mappings->mid_living_get_health);
    return std::isnan(hp) ? 20.0f : hp;
}

// sub_180044ED0
float living_get_max_health(events::JniContext* jni, jobject living) noexcept {
    const float max_hp = jni->env->CallFloatMethod(living, jni->mappings->mid_living_get_max_health);
    return std::isnan(max_hp) ? 20.0f : max_hp;
}

// sub_180044F20
float living_get_move_forward(events::JniContext* jni, jobject living) noexcept {
    return jni->env->GetFloatField(living, jni->mappings->fid_living_move_forward);
}

// sub_180044F40
float living_get_move_strafing(events::JniContext* jni, jobject living) noexcept {
    return jni->env->GetFloatField(living, jni->mappings->fid_living_move_strafing);
}

// sub_180044F60
jobject living_ray_trace(events::JniContext* jni, jobject living, double reach, float partial_ticks) noexcept {
    return jni->env->CallObjectMethod(living, jni->mappings->mid_living_ray_trace, reach, partial_ticks);
}

// sub_180044FA0
float living_get_land_movement_factor(events::JniContext* jni, jobject living) noexcept {
    return jni->env->GetFloatField(living, jni->mappings->fid_living_land_movement_factor);
}

// sub_180044FC0
int32_t living_get_jump_ticks(events::JniContext* jni, jobject living) noexcept {
    return jni->env->GetIntField(living, jni->mappings->fid_living_jump_ticks);
}

// sub_180044FE0
void living_set_jump_ticks(events::JniContext* jni, jobject living, int32_t ticks) noexcept {
    jni->env->SetIntField(living, jni->mappings->fid_living_jump_ticks, ticks);
}

// sub_180045010
void living_swing_item_super(events::JniContext* jni, jobject living) noexcept {
    jni->env->CallNonvirtualVoidMethod(living, jni->mappings->cls_entity_living_base, jni->mappings->mid_living_swing_item);
}

// sub_180045040
jobject living_get_team(events::JniContext* jni, jobject living) noexcept {
    return (jni->env->CallObjectMethod(living, jni->mappings->mid_living_get_team));
}

// sub_180045070
jobject living_get_active_potion_effect(events::JniContext* jni, jobject living, jobject potion) noexcept {
    return jni->env->CallObjectMethod(living, jni->mappings->mid_living_get_active_potion_effect, potion);
}

// sub_1800450A0
void living_jump(events::JniContext* jni, jobject living) noexcept {
    jni->env->CallVoidMethod(living, jni->mappings->mid_living_jump);
}

// sub_1800450D0
math::Vec2f player_sp_get_render_arm_rotation(events::JniContext* jni, jobject player_sp) noexcept {
    const float yaw   = jni->env->GetFloatField(player_sp, jni->mappings->fid_player_sp_render_arm_yaw);
    const float pitch = jni->env->GetFloatField(player_sp, jni->mappings->fid_player_sp_render_arm_pitch);
    return math::make_vec2(yaw, pitch);
}

// sub_180045140
void player_sp_set_render_arm_rotation(events::JniContext* jni, jobject player_sp, math::Vec2f rotation) noexcept {
    jni->env->SetFloatField(player_sp, jni->mappings->fid_player_sp_render_arm_yaw, rotation.x);
    jni->env->SetFloatField(player_sp, jni->mappings->fid_player_sp_render_arm_pitch, rotation.y);
}

// sub_1800451B0
math::Vec2f player_sp_get_prev_render_arm_rotation(events::JniContext* jni, jobject player_sp) noexcept {
    const float yaw   = jni->env->GetFloatField(player_sp, jni->mappings->fid_player_sp_prev_render_arm_yaw);
    const float pitch = jni->env->GetFloatField(player_sp, jni->mappings->fid_player_sp_prev_render_arm_pitch);
    return math::make_vec2(yaw, pitch);
}

// sub_180045220
void player_sp_set_prev_render_arm_rotation(events::JniContext* jni, jobject player_sp, math::Vec2f rotation) noexcept {
    jni->env->SetFloatField(player_sp, jni->mappings->fid_player_sp_prev_render_arm_yaw, rotation.x);
    jni->env->SetFloatField(player_sp, jni->mappings->fid_player_sp_prev_render_arm_pitch, rotation.y);
}

// sub_180045290
void player_sp_add_chat_message(events::JniContext* jni, jobject player_sp, jobject chat_component) noexcept {
    jni->env->CallVoidMethod(player_sp, jni->mappings->mid_player_sp_add_chat_message, chat_component);
}

// sub_1800452C0
void player_sp_set_time_in_portal(events::JniContext* jni, jobject player_sp, float value) noexcept {
    jni->env->SetFloatField(player_sp, jni->mappings->fid_player_sp_time_in_portal, value);
}

// sub_1800452F0
void player_sp_set_prev_time_in_portal(events::JniContext* jni, jobject player_sp, float value) noexcept {
    jni->env->SetFloatField(player_sp, jni->mappings->fid_player_sp_prev_time_in_portal, value);
}

// sub_180045320
jobject as_entity_player_sp(events::JniContext* jni, jobject obj) noexcept {
    return jni->env->IsInstanceOf(obj, jni->mappings->cls_entity_player_sp) ? obj : nullptr;
}

// sub_180045360
jobject as_entity_player(events::JniContext* jni, jobject obj) noexcept {
    return jni->env->IsInstanceOf(obj, jni->mappings->cls_entity_player) ? obj : nullptr;
}

// sub_1800453A0
jobject player_get_inventory(events::JniContext* jni, jobject player) noexcept {
    return (jni->env->GetObjectField(player, jni->mappings->fid_player_inventory));
}

// sub_1800453C0
bool player_is_invisible_to_player(events::JniContext* jni, jobject player, jobject viewer) noexcept {
    return jni->env->CallBooleanMethod(player, jni->mappings->mid_player_is_invisible_to_player, viewer) != JNI_FALSE;
}

// sub_1800453F0
bool player_is_using_item(events::JniContext* jni, jobject player) noexcept {
    return jni->env->CallBooleanMethod(player, jni->mappings->mid_player_is_using_item) != JNI_FALSE;
}

// sub_180045420
int32_t player_get_item_in_use_count(events::JniContext* jni, jobject player) noexcept {
    return jni->env->GetIntField(player, jni->mappings->fid_player_item_in_use_count);
}

// sub_180045440
float player_get_absorption_amount(events::JniContext* jni, jobject player) noexcept {
    const float abs_amt = jni->env->CallFloatMethod(player, jni->mappings->mid_player_get_absorption_amount);
    return std::isnan(abs_amt) ? 0.0f : abs_amt;
}

// sub_180045490
void player_set_item_in_use_count(events::JniContext* jni, jobject player, int32_t count) noexcept {
    jni->env->SetIntField(player, jni->mappings->fid_player_item_in_use_count, count);
}

// sub_1800454C0
jobject player_get_equipment_in_slot(events::JniContext* jni, jobject player, int32_t slot) noexcept {
    return jni->env->CallObjectMethod(player, jni->mappings->mid_player_get_equipment_in_slot, slot);
}

// sub_1800454F0
jobject player_get_inventory_container(events::JniContext* jni, jobject player) noexcept {
    return (jni->env->GetObjectField(player, jni->mappings->fid_player_inventory_container));
}

// sub_180045510
bool player_is_sleeping(events::JniContext* jni, jobject player) noexcept {
    return jni->env->GetBooleanField(player, jni->mappings->fid_player_sleeping) != JNI_FALSE;
}

// sub_180045540
float player_get_speed_on_ground(events::JniContext* jni, jobject player) noexcept {
    return jni->env->GetFloatField(player, jni->mappings->fid_player_speed_on_ground);
}

// sub_180045560
void player_set_speed_on_ground(events::JniContext* jni, jobject player, float speed) noexcept {
    jni->env->SetFloatField(player, jni->mappings->fid_player_speed_on_ground, speed);
}

// sub_180045590
jobject as_entity_other_player_mp(events::JniContext* jni, jobject obj) noexcept {
    return jni->env->IsInstanceOf(obj, jni->mappings->cls_entity_other_player_mp) ? obj : nullptr;
}

// sub_1800455D0
math::Vec3d other_player_mp_get_target_pos(events::JniContext* jni, jobject other_player) noexcept {
    const double x = jni->env->GetDoubleField(other_player, jni->mappings->fid_other_player_mp_x);
    const double y = jni->env->GetDoubleField(other_player, jni->mappings->fid_other_player_mp_y);
    const double z = jni->env->GetDoubleField(other_player, jni->mappings->fid_other_player_mp_z);
    return math::make_vec3(x, y, z);
}

// sub_180045670
int32_t other_player_mp_get_pos_rot_increments(events::JniContext* jni, jobject other_player) noexcept {
    return jni->env->GetIntField(other_player, jni->mappings->fid_other_player_pos_rot_increments);
}

// sub_180045690
double other_player_mp_get_target_yaw(events::JniContext* jni, jobject other_player) noexcept {
    return jni->env->GetDoubleField(other_player, jni->mappings->fid_other_player_mp_yaw);
}

// sub_1800456B0
jobject block_get_from_item(events::JniContext* jni, jobject item) noexcept {
    return jni->env->CallStaticObjectMethod(jni->mappings->cls_block, jni->mappings->smid_block_get_block_from_item, item);
}

// sub_1800456F0
int32_t block_get_id(events::JniContext* jni, jobject block) noexcept {
    return jni->env->CallStaticIntMethod(jni->mappings->cls_block, jni->mappings->smid_block_get_id_from_block, block);
}

// sub_180045730
jobject block_pos_new(events::JniContext* jni, int32_t x, int32_t y, int32_t z) noexcept {
    return jni->env->NewObject(jni->mappings->cls_block_pos, jni->mappings->mid_block_pos_init, x, y, z);
}

// sub_180045780
math::Vec3d block_pos_to_vec3d(events::JniContext* jni, jobject block_pos) noexcept {
    const double x = static_cast<double>(jni->env->GetIntField(block_pos, jni->mappings->fid_block_pos_x));
    const double y = static_cast<double>(jni->env->GetIntField(block_pos, jni->mappings->fid_block_pos_y));
    const double z = static_cast<double>(jni->env->GetIntField(block_pos, jni->mappings->fid_block_pos_z));
    return math::make_vec3(x, y, z);
}

// sub_180045830
void block_pos_set_xyz(events::JniContext* jni, jobject block_pos, int32_t x, int32_t y, int32_t z) noexcept {
    jni->env->SetIntField(block_pos, jni->mappings->fid_block_pos_x, x);
    jni->env->SetIntField(block_pos, jni->mappings->fid_block_pos_y, y);
    jni->env->SetIntField(block_pos, jni->mappings->fid_block_pos_z, z);
}

// sub_1800458B0
Vec3i block_pos_to_vec3i(events::JniContext* jni, jobject block_pos) noexcept {
    Vec3i out{};
    out.x = jni->env->GetIntField(block_pos, jni->mappings->fid_block_pos_x);
    out.y = jni->env->GetIntField(block_pos, jni->mappings->fid_block_pos_y);
    out.z = jni->env->GetIntField(block_pos, jni->mappings->fid_block_pos_z);
    return out;
}

// sub_180045930
jobject iblock_state_get_block(events::JniContext* jni, jobject block_state) noexcept {
    return (jni->env->CallObjectMethod(block_state, jni->mappings->mid_iblock_state_get_block));
}

// sub_180045960
jobject tile_entity_get_block_type(events::JniContext* jni, jobject tile_entity) noexcept {
    return (jni->env->GetObjectField(tile_entity, jni->mappings->fid_tile_entity_block_type));
}

// sub_180045980
math::Vec3d tile_entity_get_position(events::JniContext* jni, jobject tile_entity) noexcept {
    if (jni->mappings->version != 0) {
        jobject pos = jni->env->GetObjectField(tile_entity, jni->mappings->fid_tile_entity_pos_1_8);
        const double x = static_cast<double>(jni->env->GetIntField(pos, jni->mappings->fid_block_pos_x));
        const double y = static_cast<double>(jni->env->GetIntField(pos, jni->mappings->fid_block_pos_y));
        const double z = static_cast<double>(jni->env->GetIntField(pos, jni->mappings->fid_block_pos_z));
        return math::make_vec3(x, y, z);
    }
    const double x = static_cast<double>(jni->env->GetIntField(tile_entity, jni->mappings->fid_tile_entity_x_1_7));
    const double y = static_cast<double>(jni->env->GetIntField(tile_entity, jni->mappings->fid_tile_entity_y_1_7));
    const double z = static_cast<double>(jni->env->GetIntField(tile_entity, jni->mappings->fid_tile_entity_z_1_7));
    return math::make_vec3(x, y, z);
}

// sub_180045A70
jobject world_get_player_entities(events::JniContext* jni, jobject world) noexcept {
    return (jni->env->GetObjectField(world, jni->mappings->fid_world_player_entities));
}

// sub_180045A90
jobject world_get_loaded_entity_list(events::JniContext* jni, jobject world) noexcept {
    return (jni->env->GetObjectField(world, jni->mappings->fid_world_loaded_entity_list));
}

// sub_180045AB0
jobject world_get_loaded_tile_entity_list(events::JniContext* jni, jobject world) noexcept {
    return (jni->env->GetObjectField(world, jni->mappings->fid_world_loaded_tile_entity_list));
}

// sub_180045AD0
jobject world_get_entity_by_id(events::JniContext* jni, jobject world, int32_t entity_id) noexcept {
    return jni->env->CallObjectMethod(world, jni->mappings->mid_world_get_entity_by_id, entity_id);
}

// sub_180045B00
jobject world_get_block(events::JniContext* jni, jobject world, int32_t x, int32_t y, int32_t z) noexcept {
    if (jni->mappings->version == 0) {
        return jni->env->CallObjectMethod(world, jni->mappings->mid_world_get_block_1_7, x, y, z);
    }
    jobject pos   = block_pos_new(jni, x, y, z);
    jobject state = jni->env->CallObjectMethod(world, jni->mappings->mid_world_get_block_state_1_8, pos);
    return iblock_state_get_block(jni, state);
}

// sub_180045B90
jobject world_get_block_mutable_pos(events::JniContext* jni, jobject world, jobject reusable_pos, int32_t x, int32_t y, int32_t z) noexcept {
    if (reusable_pos == nullptr) {
        return jni->env->CallObjectMethod(world, jni->mappings->mid_world_get_block_1_7, x, y, z);
    }
    block_pos_set_xyz(jni, reusable_pos, x, y, z);
    jobject state = jni->env->CallObjectMethod(world, jni->mappings->mid_world_get_block_state_1_8, reusable_pos);
    return iblock_state_get_block(jni, state);
}

// sub_180045C30
bool world_set_block_1_7(events::JniContext* jni, jobject world, int32_t x, int32_t y, int32_t z, jobject block, int32_t meta, int32_t flags) noexcept {
    return jni->env->CallBooleanMethod(world, jni->mappings->mid_world_set_block_1_7, x, y, z, block, meta, flags) != JNI_FALSE;
}

// sub_180045C90
bool world_set_block_state_1_8(events::JniContext* jni, jobject world, jobject pos, jobject state, int32_t flags) noexcept {
    return jni->env->CallBooleanMethod(world, jni->mappings->mid_world_set_block_state_1_8, pos, state, flags) != JNI_FALSE;
}

// sub_180045CD0
jobject world_ray_trace_blocks(events::JniContext* jni, jobject world, jobject start_vec, jobject end_vec, bool stop_on_liquid, bool ignore_block_without_bbox, bool return_last_uncollidable) noexcept {
    return jni->env->CallObjectMethod(world, jni->mappings->mid_world_ray_trace_blocks, start_vec, end_vec, static_cast<jboolean>(stop_on_liquid), static_cast<jboolean>(ignore_block_without_bbox), static_cast<jboolean>(return_last_uncollidable));
}

// sub_180045D30
void world_update_entities(events::JniContext* jni, jobject world) noexcept {
    jni->env->CallVoidMethod(world, jni->mappings->mid_world_update_entities);
}

// sub_180045D60
jobject world_get_chunk_from_chunk_coords(events::JniContext* jni, jobject world, int32_t chunk_x, int32_t chunk_z) noexcept {
    return jni->env->CallObjectMethod(world, jni->mappings->mid_world_get_chunk_from_chunk_coords, chunk_x, chunk_z);
}

// sub_180045DA0
void world_mark_block_range_for_render_update(events::JniContext* jni, jobject world, int32_t x1, int32_t y1, int32_t z1, int32_t x2, int32_t y2, int32_t z2) noexcept {
    jni->env->CallVoidMethod(world, jni->mappings->mid_world_mark_block_range_for_render_update, x1, y1, z1, x2, y2, z2);
}

// sub_180045E00
jobject world_get_chunk_provider_client(events::JniContext* jni, jobject world) noexcept {
    jobject provider = jni->env->GetObjectField(world, jni->mappings->fid_world_chunk_provider);
    return jni->env->IsInstanceOf(provider, jni->mappings->cls_chunk_provider_client) ? provider : nullptr;
}

// sub_180045E60
jobjectArray chunk_get_storage_arrays(events::JniContext* jni, jobject chunk) noexcept {
    return static_cast<jobjectArray>(jni->env->GetObjectField(chunk, jni->mappings->fid_chunk_storage_arrays));
}

// sub_180045E80
int32_t chunk_get_x(events::JniContext* jni, jobject chunk) noexcept {
    return jni->env->GetIntField(chunk, jni->mappings->fid_chunk_x_position);
}

// sub_180045EA0
int32_t chunk_get_z(events::JniContext* jni, jobject chunk) noexcept {
    return jni->env->GetIntField(chunk, jni->mappings->fid_chunk_z_position);
}

// sub_180045EC0
jobject extended_block_storage_get_data(events::JniContext* jni, jobject ebs) noexcept {
    return (jni->env->GetObjectField(ebs, jni->mappings->fid_ebs_block_data));
}

// sub_180045EE0
void chunk_provider_unload_chunk(events::JniContext* jni, jobject provider, int32_t chunk_x, int32_t chunk_z) noexcept {
    jni->env->CallVoidMethod(provider, jni->mappings->mid_chunk_provider_unload_chunk, chunk_x, chunk_z);
}

// sub_180045F20
jobject chunk_provider_get_chunk_listing(events::JniContext* jni, jobject provider) noexcept {
    return (jni->env->GetObjectField(provider, jni->mappings->fid_chunk_provider_chunk_listing));
}

// sub_180045F40
jobject mop_new_entity_hit(events::JniContext* jni, jobject entity, jobject hit_vec) noexcept {
    return jni->env->NewObject(jni->mappings->cls_moving_object_position, jni->mappings->mid_mop_init_entity_vec, entity, hit_vec);
}

// sub_180045F80
jobject aabb_calculate_intercept(events::JniContext* jni, jobject aabb, jobject vec_a, jobject vec_b) noexcept {
    return jni->env->CallObjectMethod(aabb, jni->mappings->mid_aabb_calculate_intercept, vec_a, vec_b);
}

// sub_180045FC0
int32_t mop_get_type_ordinal(events::JniContext* jni, jobject mop) noexcept {
    jobject hit_type = jni->env->GetObjectField(mop, jni->mappings->fid_mop_type_of_hit);
    return enum_get_ordinal(jni, hit_type);
}

// sub_180046000
int32_t mop_get_side_hit(events::JniContext* jni, jobject mop) noexcept {
    if (jni->mappings->version == 0) {
        return jni->env->GetIntField(mop, jni->mappings->fid_mop_side_hit_1_7);
    }
    jobject facing = jni->env->GetObjectField(mop, jni->mappings->fid_mop_side_hit_1_8);
    return enum_get_ordinal(jni, facing);
}

// sub_180046060
jobject mop_get_hit_vec(events::JniContext* jni, jobject mop) noexcept {
    return (jni->env->GetObjectField(mop, jni->mappings->fid_mop_hit_vec));
}

// sub_180046080
math::Vec3d mop_get_block_pos_vec3d(events::JniContext* jni, jobject mop) noexcept {
    if (jni->mappings->version != 0) {
        jobject pos = jni->env->GetObjectField(mop, jni->mappings->fid_mop_block_pos_1_8);
        return block_pos_to_vec3d(jni, pos);
    }
    const double x = static_cast<double>(jni->env->GetIntField(mop, jni->mappings->fid_mop_block_x_1_7));
    const double y = static_cast<double>(jni->env->GetIntField(mop, jni->mappings->fid_mop_block_y_1_7));
    const double z = static_cast<double>(jni->env->GetIntField(mop, jni->mappings->fid_mop_block_z_1_7));
    return math::make_vec3(x, y, z);
}

// sub_180046150
Vec3i mop_get_block_pos_vec3i(events::JniContext* jni, jobject mop) noexcept {
    if (jni->mappings->version != 0) {
        jobject pos = jni->env->GetObjectField(mop, jni->mappings->fid_mop_block_pos_1_8);
        return block_pos_to_vec3i(jni, pos);
    }
    Vec3i out{};
    out.x = jni->env->GetIntField(mop, jni->mappings->fid_mop_block_x_1_7);
    out.y = jni->env->GetIntField(mop, jni->mappings->fid_mop_block_y_1_7);
    out.z = jni->env->GetIntField(mop, jni->mappings->fid_mop_block_z_1_7);
    return out;
}

// sub_1800461F0
math::Vec3d mop_get_offset_block_pos_vec3d(events::JniContext* jni, jobject mop) noexcept {
    const math::Vec3d base = mop_get_block_pos_vec3d(jni, mop);
    const uint32_t side = static_cast<uint32_t>(mop_get_side_hit(jni, mop));
    return math::vec3_add(base, kEnumFacingOffsets[side]);
}

// sub_1800462A0
jobject mop_get_entity_hit(events::JniContext* jni, jobject mop) noexcept {
    return (jni->env->GetObjectField(mop, jni->mappings->fid_mop_entity_hit));
}

// sub_1800462C0
int32_t java_list_size(events::JniContext* jni, jobject list) noexcept {
    return jni->env->CallIntMethod(list, jni->mappings->mid_list_size);
}

// sub_1800462F0
jobject java_list_get(events::JniContext* jni, jobject list, int32_t index) noexcept {
    return jni->env->CallObjectMethod(list, jni->mappings->mid_list_get, index);
}

// sub_180046320
bool java_list_add(events::JniContext* jni, jobject list, jobject elem) noexcept {
    return jni->env->CallBooleanMethod(list, jni->mappings->mid_list_add, elem) != JNI_FALSE;
}

// sub_180046350
bool java_list_remove(events::JniContext* jni, jobject list, jobject elem) noexcept {
    return jni->env->CallBooleanMethod(list, jni->mappings->mid_list_remove, elem) != JNI_FALSE;
}

// sub_180046380
bool java_map_contains_key(events::JniContext* jni, jobject map_obj, jobject key) noexcept {
    return jni->env->CallBooleanMethod(map_obj, jni->mappings->mid_map_contains_key, key) != JNI_FALSE;
}

// sub_1800463B0
jobject java_map_get(events::JniContext* jni, jobject map_obj, jobject key) noexcept {
    return jni->env->CallObjectMethod(map_obj, jni->mappings->mid_map_get, key);
}

// sub_1800463E0
float settings_get_mouse_sensitivity(events::JniContext* jni, jobject settings) noexcept {
    return jni->env->GetFloatField(settings, jni->mappings->fid_settings_mouse_sensitivity);
}

// sub_180046400
jobject settings_get_key_use_item(events::JniContext* jni, jobject settings) noexcept {
    auto arr = static_cast<jobjectArray>(jni->env->GetObjectField(settings, jni->mappings->fid_settings_key_bindings));
    return jni->env->GetObjectArrayElement(arr, 1);
}

// sub_180046450
jobject settings_get_key_drop(events::JniContext* jni, jobject settings) noexcept {
    auto arr = static_cast<jobjectArray>(jni->env->GetObjectField(settings, jni->mappings->fid_settings_key_bindings));
    const jsize idx = (jni->mappings->version == 0) ? 8 : 9;
    return jni->env->GetObjectArrayElement(arr, idx);
}

// sub_1800464A0
jobject settings_get_key_sneak(events::JniContext* jni, jobject settings) noexcept {
    auto arr = static_cast<jobjectArray>(jni->env->GetObjectField(settings, jni->mappings->fid_settings_key_bindings));
    return jni->env->GetObjectArrayElement(arr, 7);
}

// sub_1800464F0
jobject settings_get_key_jump(events::JniContext* jni, jobject settings) noexcept {
    auto arr = static_cast<jobjectArray>(jni->env->GetObjectField(settings, jni->mappings->fid_settings_key_bindings));
    return jni->env->GetObjectArrayElement(arr, 6);
}

// sub_180046540
jobject settings_get_key_forward(events::JniContext* jni, jobject settings) noexcept {
    auto arr = static_cast<jobjectArray>(jni->env->GetObjectField(settings, jni->mappings->fid_settings_key_bindings));
    return jni->env->GetObjectArrayElement(arr, 2);
}

// sub_180046590
jobject settings_get_key_back(events::JniContext* jni, jobject settings) noexcept {
    auto arr = static_cast<jobjectArray>(jni->env->GetObjectField(settings, jni->mappings->fid_settings_key_bindings));
    return jni->env->GetObjectArrayElement(arr, 4);
}

// sub_1800465E0
jobject settings_get_key_left(events::JniContext* jni, jobject settings) noexcept {
    auto arr = static_cast<jobjectArray>(jni->env->GetObjectField(settings, jni->mappings->fid_settings_key_bindings));
    return jni->env->GetObjectArrayElement(arr, 3);
}

// sub_180046630
jobject settings_get_key_right(events::JniContext* jni, jobject settings) noexcept {
    auto arr = static_cast<jobjectArray>(jni->env->GetObjectField(settings, jni->mappings->fid_settings_key_bindings));
    return jni->env->GetObjectArrayElement(arr, 5);
}

// sub_180046680
jobject settings_get_key_sprint(events::JniContext* jni, jobject settings) noexcept {
    auto arr = static_cast<jobjectArray>(jni->env->GetObjectField(settings, jni->mappings->fid_settings_key_bindings));
    const jsize idx = (jni->mappings->version == 0) ? 17 : 8;
    return jni->env->GetObjectArrayElement(arr, idx);
}

// sub_1800466E0
jobject settings_get_key_attack(events::JniContext* jni, jobject settings) noexcept {
    auto arr = static_cast<jobjectArray>(jni->env->GetObjectField(settings, jni->mappings->fid_settings_key_bindings));
    return jni->env->GetObjectArrayElement(arr, 0);
}

// sub_180046730
jobject settings_get_key_inventory(events::JniContext* jni, jobject settings) noexcept {
    auto arr = static_cast<jobjectArray>(jni->env->GetObjectField(settings, jni->mappings->fid_settings_key_bindings));
    const jsize idx = (jni->mappings->version == 0) ? 9 : 10;
    return jni->env->GetObjectArrayElement(arr, idx);
}

// sub_180046780
int32_t settings_get_third_person_view(events::JniContext* jni, jobject settings) noexcept {
    return jni->env->GetIntField(settings, jni->mappings->fid_settings_third_person_view);
}

// sub_1800467A0
void settings_set_third_person_view(events::JniContext* jni, jobject settings, int32_t mode) noexcept {
    jni->env->SetIntField(settings, jni->mappings->fid_settings_third_person_view, mode);
}

// sub_1800467D0
int32_t key_binding_get_press_time(events::JniContext* jni, jobject key_binding) noexcept {
    return jni->env->GetIntField(key_binding, jni->mappings->fid_key_binding_press_time);
}

// sub_1800467F0
void key_binding_increment_press_time(events::JniContext* jni, jobject key_binding) noexcept {
    const int32_t cur = jni->env->GetIntField(key_binding, jni->mappings->fid_key_binding_press_time);
    jni->env->SetIntField(key_binding, jni->mappings->fid_key_binding_press_time, cur + 1);
}

// sub_180046850
void key_binding_set_press_time(events::JniContext* jni, jobject key_binding, int32_t press_time) noexcept {
    jni->env->SetIntField(key_binding, jni->mappings->fid_key_binding_press_time, press_time);
}

// sub_180046880
int32_t key_binding_get_key_code(events::JniContext* jni, jobject key_binding) noexcept {
    return jni->env->GetIntField(key_binding, jni->mappings->fid_key_binding_key_code);
}

// sub_1800468A0
bool key_binding_is_pressed(events::JniContext* jni, jobject key_binding) noexcept {
    return jni->env->GetBooleanField(key_binding, jni->mappings->fid_key_binding_pressed) != JNI_FALSE;
}

// sub_1800468D0
void key_binding_set_pressed(events::JniContext* jni, jobject key_binding, bool pressed) noexcept {
    jni->env->SetBooleanField(key_binding, jni->mappings->fid_key_binding_pressed, static_cast<jboolean>(pressed));
}

// sub_180046900
bool key_binding_is_physically_down(events::JniContext* jni, jobject key_binding) noexcept {
    const int32_t code = jni->env->GetIntField(key_binding, jni->mappings->fid_key_binding_key_code);
    if (code < 0) {
        return lwjgl_mouse_is_button_down(jni, code + 100);
    }
    if (static_cast<uint32_t>(code) <= 0x100u) {
        return lwjgl_keyboard_is_key_down(jni, code);
    }
    return false;
}

// sub_180046960
jobject vec3_new(events::JniContext* jni, const math::Vec3d& v) noexcept {
    return jni->env->NewObject(jni->mappings->cls_vec3, jni->mappings->mid_vec3_init, v.x, v.y, v.z);
}

// sub_1800469B0
math::Vec3d vec3_to_vec3d(events::JniContext* jni, jobject vec3_obj) noexcept {
    const double x = jni->env->GetDoubleField(vec3_obj, jni->mappings->fid_vec3_x_coord);
    const double y = jni->env->GetDoubleField(vec3_obj, jni->mappings->fid_vec3_y_coord);
    const double z = jni->env->GetDoubleField(vec3_obj, jni->mappings->fid_vec3_z_coord);
    return math::make_vec3(x, y, z);
}

// sub_180046A50
jobject aabb_new(events::JniContext* jni, const math::AABB& box) noexcept {
    return jni->env->NewObject(jni->mappings->cls_axis_aligned_bb, jni->mappings->mid_aabb_init, box.min.x, box.min.y, box.min.z, box.max.x, box.max.y, box.max.z);
}

// sub_180046AB0
math::AABB aabb_to_aabb(events::JniContext* jni, jobject aabb_obj) noexcept {
    const math::Vec3d min_pt{
        jni->env->GetDoubleField(aabb_obj, jni->mappings->fid_aabb_min_x),
        jni->env->GetDoubleField(aabb_obj, jni->mappings->fid_aabb_min_y),
        jni->env->GetDoubleField(aabb_obj, jni->mappings->fid_aabb_min_z),
    };
    const math::Vec3d max_pt{
        jni->env->GetDoubleField(aabb_obj, jni->mappings->fid_aabb_max_x),
        jni->env->GetDoubleField(aabb_obj, jni->mappings->fid_aabb_max_y),
        jni->env->GetDoubleField(aabb_obj, jni->mappings->fid_aabb_max_z),
    };
    return math::aabb_from_points(min_pt, max_pt);
}

// sub_180046BD0
bool lwjgl_keyboard_is_key_down(events::JniContext* jni, int32_t key_code) noexcept {
    return jni->env->CallStaticBooleanMethod(jni->mappings->cls_lwjgl_keyboard, jni->mappings->smid_keyboard_is_key_down, key_code) != JNI_FALSE;
}

// sub_180046C10
bool lwjgl_keyboard_next(events::JniContext* jni) noexcept {
    return jni->env->CallStaticBooleanMethod(jni->mappings->cls_lwjgl_keyboard, jni->mappings->smid_keyboard_next) != JNI_FALSE;
}

// sub_180046C50
bool lwjgl_keyboard_get_event_key_state(events::JniContext* jni) noexcept {
    return jni->env->CallStaticBooleanMethod(jni->mappings->cls_lwjgl_keyboard, jni->mappings->smid_keyboard_get_event_key_state) != JNI_FALSE;
}

// sub_180046C90
int32_t lwjgl_keyboard_get_event_key(events::JniContext* jni) noexcept {
    return jni->env->CallStaticIntMethod(jni->mappings->cls_lwjgl_keyboard, jni->mappings->smid_keyboard_get_event_key);
}

// sub_180046CC0
bool lwjgl_mouse_is_grabbed(events::JniContext* jni) noexcept {
    return jni->env->CallStaticBooleanMethod(jni->mappings->cls_lwjgl_mouse, jni->mappings->smid_mouse_is_grabbed) != JNI_FALSE;
}

// sub_180046D00
void lwjgl_mouse_set_grabbed(events::JniContext* jni, bool grabbed) noexcept {
    jni->env->CallStaticVoidMethod(jni->mappings->cls_lwjgl_mouse, jni->mappings->smid_mouse_set_grabbed, static_cast<jboolean>(grabbed));
}

// sub_180046D40
bool lwjgl_mouse_is_button_down(events::JniContext* jni, int32_t button) noexcept {
    return jni->env->CallStaticBooleanMethod(jni->mappings->cls_lwjgl_mouse, jni->mappings->smid_mouse_is_button_down, button) != JNI_FALSE;
}

// sub_180046D80
bool lwjgl_mouse_next(events::JniContext* jni) noexcept {
    return jni->env->CallStaticBooleanMethod(jni->mappings->cls_lwjgl_mouse, jni->mappings->smid_mouse_next) != JNI_FALSE;
}

// sub_180046DC0
void lwjgl_mouse_set_cursor_position(events::JniContext* jni, int32_t x, int32_t y) noexcept {
    jni->env->CallStaticVoidMethod(jni->mappings->cls_lwjgl_mouse, jni->mappings->smid_mouse_set_cursor_position, x, y);
}

// sub_180046E00
float timer_get_render_partial_ticks(events::JniContext* jni, jobject timer) noexcept {
    return jni->env->GetFloatField(timer, jni->mappings->fid_timer_render_partial_ticks);
}

// sub_180046E20
float timer_get_speed(events::JniContext* jni, jobject timer) noexcept {
    return jni->env->GetFloatField(timer, jni->mappings->fid_timer_speed);
}

// sub_180046E40
void timer_set_speed(events::JniContext* jni, jobject timer, float speed) noexcept {
    jni->env->SetFloatField(timer, jni->mappings->fid_timer_speed, speed);
}

// sub_180046E70
float timer_get_elapsed_partial_ticks(events::JniContext* jni, jobject timer) noexcept {
    return jni->env->GetFloatField(timer, jni->mappings->fid_timer_elapsed_partial_ticks);
}

// sub_180046E90
void timer_set_elapsed_partial_ticks(events::JniContext* jni, jobject timer, float partial_ticks) noexcept {
    jni->env->SetFloatField(timer, jni->mappings->fid_timer_elapsed_partial_ticks, partial_ticks);
}

// sub_180046EC0
int32_t timer_get_elapsed_ticks(events::JniContext* jni, jobject timer) noexcept {
    return jni->env->GetIntField(timer, jni->mappings->fid_timer_elapsed_ticks);
}

// sub_180046EE0
int32_t inv_player_get_current_item(events::JniContext* jni, jobject inv_player) noexcept {
    return jni->env->GetIntField(inv_player, jni->mappings->fid_inv_player_current_item);
}

// sub_180046F00
void inv_player_set_current_item(events::JniContext* jni, jobject inv_player, int32_t slot) noexcept {
    jni->env->SetIntField(inv_player, jni->mappings->fid_inv_player_current_item, slot);
}

// sub_180046F30
jobjectArray inv_player_get_main_inventory(events::JniContext* jni, jobject inv_player) noexcept {
    return static_cast<jobjectArray>(jni->env->GetObjectField(inv_player, jni->mappings->fid_inv_player_main_inventory));
}

// sub_180046F50
jobject inv_player_get_dragged_stack(events::JniContext* jni, jobject inv_player) noexcept {
    return (jni->env->GetObjectField(inv_player, jni->mappings->fid_inv_player_dragged_item_stack));
}

// sub_180046F70
void gui_screen_key_typed(events::JniContext* jni, jobject gui_screen, jchar typed_char, int32_t key_code) noexcept {
    jni->env->CallVoidMethod(gui_screen, jni->mappings->mid_gui_screen_key_typed, typed_char, key_code);
}

// sub_180046FB0
void gui_container_set_last_click_time(events::JniContext* jni, jobject gui_container, int64_t time_ms) noexcept {
    jni->env->SetLongField(gui_container, jni->mappings->fid_gui_container_last_click_time, time_ms);
}

// sub_180046FE0
jobject gui_container_get_hovered_slot(events::JniContext* jni, jobject gui_container) noexcept {
    return (jni->env->GetObjectField(gui_container, jni->mappings->fid_gui_container_the_slot));
}

// sub_180047000
bool is_gui_inventory(events::JniContext* jni, jobject obj) noexcept {
    return jni->env->IsInstanceOf(obj, jni->mappings->cls_gui_inventory) != JNI_FALSE;
}

// sub_180047030
bool is_gui_chest(events::JniContext* jni, jobject obj) noexcept {
    return jni->env->IsInstanceOf(obj, jni->mappings->cls_gui_chest) != JNI_FALSE;
}

// sub_180047060
void gui_container_handle_mouse_click(events::JniContext* jni, jobject gui_container, int32_t slot_id, int32_t mouse_button, int32_t click_mode) noexcept {
    jni->env->CallVoidMethod(gui_container, jni->mappings->mid_gui_container_handle_mouse_click, static_cast<jobject>(nullptr), slot_id, mouse_button, click_mode);
}

// sub_1800470B0
jobject gui_chest_get_lower_inventory(events::JniContext* jni, jobject gui_chest) noexcept {
    return (jni->env->GetObjectField(gui_chest, jni->mappings->fid_gui_chest_lower_chest_inventory));
}

// sub_1800470D0
int32_t item_get_id(events::JniContext* jni, jobject item) noexcept {
    return jni->env->CallStaticIntMethod(jni->mappings->cls_item, jni->mappings->smid_item_get_id_from_item, item);
}

// sub_180047110
jobject item_get_by_id(events::JniContext* jni, int32_t item_id) noexcept {
    return jni->env->CallStaticObjectMethod(jni->mappings->cls_item, jni->mappings->smid_item_get_item_by_id, item_id);
}

// sub_180047150
jobject item_stack_new_from_block(events::JniContext* jni, jobject block) noexcept {
    return jni->env->NewObject(jni->mappings->cls_item_stack, jni->mappings->mid_item_stack_init_block, block);
}

// sub_180047190
jobject item_stack_get_item(events::JniContext* jni, jobject stack) noexcept {
    return (jni->env->CallObjectMethod(stack, jni->mappings->mid_item_stack_get_item));
}

// sub_1800471C0
float item_stack_get_str_vs_block(events::JniContext* jni, jobject stack, jobject block) noexcept {
    return jni->env->CallFloatMethod(stack, jni->mappings->mid_item_stack_get_str_vs_block, block);
}

// sub_1800471F0
int32_t item_stack_get_size(events::JniContext* jni, jobject stack) noexcept {
    return jni->env->GetIntField(stack, jni->mappings->fid_item_stack_size);
}

// sub_180047210
void item_stack_set_size(events::JniContext* jni, jobject stack, int32_t size) noexcept {
    jni->env->SetIntField(stack, jni->mappings->fid_item_stack_size, size);
}

// sub_180047240
int32_t item_stack_get_damage(events::JniContext* jni, jobject stack) noexcept {
    return jni->env->GetIntField(stack, jni->mappings->fid_item_stack_item_damage);
}

// sub_180047260
jobject item_stack_get_enchantment_tag_list(events::JniContext* jni, jobject stack) noexcept {
    return (jni->env->CallObjectMethod(stack, jni->mappings->mid_item_stack_get_enchantment_tag_list));
}

// sub_180047290
int32_t item_stack_get_max_stack_size(events::JniContext* jni, jobject stack) noexcept {
    return jni->env->CallIntMethod(stack, jni->mappings->mid_item_stack_get_max_stack_size);
}

// sub_1800472C0
bool item_stack_are_tags_equal(events::JniContext* jni, jobject stack_a, jobject stack_b) noexcept {
    return jni->env->CallStaticBooleanMethod(jni->mappings->cls_item_stack, jni->mappings->smid_item_stack_are_tags_equal, stack_a, stack_b) != JNI_FALSE;
}

// sub_180047300
bool item_stack_has_display_name(events::JniContext* jni, jobject stack) noexcept {
    return jni->env->CallBooleanMethod(stack, jni->mappings->mid_item_stack_has_display_name) != JNI_FALSE;
}

// sub_180047330
jobject net_handler_get_player_info_map(events::JniContext* jni, jobject net_handler) noexcept {
    return (jni->env->GetObjectField(net_handler, jni->mappings->fid_net_handler_player_info_map));
}

// sub_180047350
jobject net_handler_get_network_manager(events::JniContext* jni, jobject net_handler) noexcept {
    return (jni->env->GetObjectField(net_handler, jni->mappings->fid_net_handler_net_manager));
}

// sub_180047370
jobject network_manager_get_channel(events::JniContext* jni, jobject net_manager) noexcept {
    return (jni->env->GetObjectField(net_manager, jni->mappings->fid_network_manager_channel));
}

// sub_180047390
void network_manager_send_packet_1_7(events::JniContext* jni, jobject net_manager, jobject packet, jobjectArray listeners) noexcept {
    jni->env->CallVoidMethod(net_manager, jni->mappings->mid_network_manager_send_packet_listeners, packet, listeners);
}

// sub_1800473D0
void network_manager_send_packet_1_8(events::JniContext* jni, jobject net_manager, jobject packet) noexcept {
    jni->env->CallVoidMethod(net_manager, jni->mappings->mid_network_manager_send_packet, packet);
}

// sub_180047400
void network_manager_send_packet_no_event(events::JniContext* jni, jobject net_manager, jobject packet) noexcept {
    if (jni->mappings->mid_network_manager_send_packet_listeners == nullptr) {
        jni->env->CallVoidMethod(net_manager, jni->mappings->mid_network_manager_send_packet, packet);
        return;
    }
    jobjectArray empty_listeners = jni->env->NewObjectArray(0, jni->mappings->cls_netty_generic_future_listener, nullptr);
    jni->env->CallVoidMethod(net_manager, jni->mappings->mid_network_manager_send_packet_listeners, packet, empty_listeners);
}

// sub_180047490
jobject network_player_info_get_display_name(events::JniContext* jni, jobject player_info) noexcept {
    return (jni->env->GetObjectField(player_info, jni->mappings->fid_network_player_info_display_name));
}

// sub_1800474B0
jobject network_player_info_get_player_team(events::JniContext* jni, jobject player_info) noexcept {
    return (jni->env->CallObjectMethod(player_info, jni->mappings->mid_network_player_info_get_player_team));
}

// sub_1800474E0
int32_t java_object_hash_code(events::JniContext* jni, jobject obj) noexcept {
    return jni->env->CallIntMethod(obj, jni->mappings->mid_object_hash_code);
}

// sub_180047510
jstring chat_component_get_formatted_text(events::JniContext* jni, jobject chat_component) noexcept {
    return static_cast<jstring>(jni->env->CallObjectMethod(chat_component, jni->mappings->mid_ichat_component_get_formatted_text));
}

// sub_180047540
jobject chat_component_text_new(events::JniContext* jni, jstring text) noexcept {
    return jni->env->NewObject(jni->mappings->cls_chat_component_text, jni->mappings->mid_chat_component_text_init, text);
}

// sub_180047580
jobject chat_serializer_json_to_component(events::JniContext* jni, jstring json) noexcept {
    return jni->env->CallStaticObjectMethod(jni->mappings->cls_ichat_component_serializer, jni->mappings->smid_ichat_serializer_json_to_component, json);
}

// sub_1800475C0
jobject inventory_get_stack_in_slot(events::JniContext* jni, jobject inventory, int32_t slot) noexcept {
    return jni->env->CallObjectMethod(inventory, jni->mappings->mid_iinventory_get_stack_in_slot, slot);
}

// sub_1800475F0
int32_t inventory_get_size(events::JniContext* jni, jobject inventory) noexcept {
    return jni->env->CallIntMethod(inventory, jni->mappings->mid_iinventory_get_size_inventory);
}

// sub_180047620
jobject container_player_get_craft_matrix(events::JniContext* jni, jobject container_player) noexcept {
    return (jni->env->GetObjectField(container_player, jni->mappings->fid_container_player_craft_matrix));
}

// sub_180047640
int32_t slot_get_number(events::JniContext* jni, jobject slot_obj) noexcept {
    return jni->env->GetIntField(slot_obj, jni->mappings->fid_slot_number);
}

// sub_180047660
jobject slot_get_stack(events::JniContext* jni, jobject slot_obj) noexcept {
    return (jni->env->CallObjectMethod(slot_obj, jni->mappings->mid_slot_get_stack));
}

// sub_180047690
jobject as_inventory_basic(events::JniContext* jni, jobject obj) noexcept {
    return jni->env->IsInstanceOf(obj, jni->mappings->cls_inventory_basic) ? obj : nullptr;
}

// sub_1800476D0
jstring inventory_basic_get_title(events::JniContext* jni, jobject inv_basic) noexcept {
    return static_cast<jstring>(jni->env->GetObjectField(inv_basic, jni->mappings->fid_inventory_basic_title));
}

// sub_1800476F0
jobject netty_channel_get_pipeline(events::JniContext* jni, jobject channel) noexcept {
    return (jni->env->CallObjectMethod(channel, jni->mappings->mid_channel_pipeline));
}

// sub_180047720
jobject netty_pipeline_add_before(events::JniContext* jni, jobject pipeline, const char* base_name, const char* name, jobject handler) noexcept {
    jstring jbase = jni->env->NewStringUTF(base_name);
    jstring jname = jni->env->NewStringUTF(name);
    return jni->env->CallObjectMethod(pipeline, jni->mappings->mid_pipeline_add_before, jbase, jname, handler);
}

// sub_1800477A0
jobject netty_pipeline_get_by_class(events::JniContext* jni, jobject pipeline, jclass handler_cls) noexcept {
    return jni->env->CallObjectMethod(pipeline, jni->mappings->mid_pipeline_get_by_class, handler_cls);
}

// sub_1800477D0
jobject netty_pipeline_remove_by_class(events::JniContext* jni, jobject pipeline, jclass handler_cls) noexcept {
    return jni->env->CallObjectMethod(pipeline, jni->mappings->mid_pipeline_remove_by_class, handler_cls);
}

// sub_180047800
jobject as_packet_c03_player(events::JniContext* jni, jobject obj) noexcept {
    return jni->env->IsInstanceOf(obj, jni->mappings->cls_c03_packet_player) ? obj : nullptr;
}

// sub_180047840
bool packet_c03_is_moving(events::JniContext* jni, jobject packet) noexcept {
    return jni->env->GetBooleanField(packet, jni->mappings->fid_c03_moving) != JNI_FALSE;
}

// sub_180047870
bool packet_c03_is_on_ground(events::JniContext* jni, jobject packet) noexcept {
    return jni->env->GetBooleanField(packet, jni->mappings->fid_c03_on_ground) != JNI_FALSE;
}

// sub_1800478A0
double packet_c03_get_x(events::JniContext* jni, jobject packet) noexcept {
    return jni->env->GetDoubleField(packet, jni->mappings->fid_c03_x);
}

// sub_1800478C0
double packet_c03_get_y(events::JniContext* jni, jobject packet) noexcept {
    return jni->env->GetDoubleField(packet, jni->mappings->fid_c03_y);
}

// sub_1800478E0
double packet_c03_get_z(events::JniContext* jni, jobject packet) noexcept {
    return jni->env->GetDoubleField(packet, jni->mappings->fid_c03_z);
}

// sub_180047900
math::Vec3d packet_c03_get_position(events::JniContext* jni, jobject packet) noexcept {
    const double x = jni->env->GetDoubleField(packet, jni->mappings->fid_c03_x);
    const double y = jni->env->GetDoubleField(packet, jni->mappings->fid_c03_y);
    const double z = jni->env->GetDoubleField(packet, jni->mappings->fid_c03_z);
    return math::make_vec3(x, y, z);
}

// sub_1800479A0
jobject as_packet_c07_digging(events::JniContext* jni, jobject obj) noexcept {
    return jni->env->IsInstanceOf(obj, jni->mappings->cls_c07_packet_player_digging) ? obj : nullptr;
}

// sub_1800479E0
int32_t packet_c07_get_status(events::JniContext* jni, jobject packet) noexcept {
    if (jni->mappings->version == 0) {
        return jni->env->GetIntField(packet, jni->mappings->fid_c07_status);
    }
    jobject action_enum = jni->env->GetObjectField(packet, jni->mappings->fid_c07_status);
    return enum_get_ordinal(jni, action_enum);
}

// sub_180047A30
jobject as_packet_c08_block_placement(events::JniContext* jni, jobject obj) noexcept {
    return jni->env->IsInstanceOf(obj, jni->mappings->cls_c08_packet_player_block_placement) ? obj : nullptr;
}

// sub_180047A70
jobject packet_c08_get_stack(events::JniContext* jni, jobject packet) noexcept {
    return (jni->env->GetObjectField(packet, jni->mappings->fid_c08_stack));
}

// sub_180047A90
jobject as_packet_c02_use_entity(events::JniContext* jni, jobject obj) noexcept {
    return jni->env->IsInstanceOf(obj, jni->mappings->cls_c02_packet_use_entity) ? obj : nullptr;
}

// sub_180047AD0
jobject as_packet_c0b_entity_action(events::JniContext* jni, jobject obj) noexcept {
    return jni->env->IsInstanceOf(obj, jni->mappings->cls_c0b_packet_entity_action) ? obj : nullptr;
}

// sub_180047B10
int32_t packet_c0b_get_action(events::JniContext* jni, jobject packet) noexcept {
    if (jni->mappings->version == 0) {
        return jni->env->GetIntField(packet, jni->mappings->fid_c0b_action);
    }
    jobject action_enum = jni->env->GetObjectField(packet, jni->mappings->fid_c0b_action);
    return enum_get_ordinal(jni, action_enum);
}

// sub_180047B60
jobject as_packet_s12_entity_velocity(events::JniContext* jni, jobject obj) noexcept {
    return jni->env->IsInstanceOf(obj, jni->mappings->cls_s12_packet_entity_velocity) ? obj : nullptr;
}

// sub_180047BA0
int32_t packet_s12_get_entity_id(events::JniContext* jni, jobject packet) noexcept {
    return jni->env->GetIntField(packet, jni->mappings->fid_s12_entity_id);
}

// sub_180047BC0
int32_t packet_s12_get_motion_x(events::JniContext* jni, jobject packet) noexcept {
    return jni->env->GetIntField(packet, jni->mappings->fid_s12_motion_x);
}

// sub_180047BE0
void packet_s12_set_motion_x(events::JniContext* jni, jobject packet, int32_t motion_x) noexcept {
    jni->env->SetIntField(packet, jni->mappings->fid_s12_motion_x, motion_x);
}

// sub_180047C10
int32_t packet_s12_get_motion_y(events::JniContext* jni, jobject packet) noexcept {
    return jni->env->GetIntField(packet, jni->mappings->fid_s12_motion_y);
}

// sub_180047C30
void packet_s12_set_motion_y(events::JniContext* jni, jobject packet, int32_t motion_y) noexcept {
    jni->env->SetIntField(packet, jni->mappings->fid_s12_motion_y, motion_y);
}

// sub_180047C60
int32_t packet_s12_get_motion_z(events::JniContext* jni, jobject packet) noexcept {
    return jni->env->GetIntField(packet, jni->mappings->fid_s12_motion_z);
}

// sub_180047C80
void packet_s12_set_motion_z(events::JniContext* jni, jobject packet, int32_t motion_z) noexcept {
    jni->env->SetIntField(packet, jni->mappings->fid_s12_motion_z, motion_z);
}

// sub_180047CB0
jobject as_packet_s14_entity(events::JniContext* jni, jobject obj) noexcept {
    return jni->env->IsInstanceOf(obj, jni->mappings->cls_s14_packet_entity) ? obj : nullptr;
}

// sub_180047CF0
int32_t packet_s14_get_entity_id(events::JniContext* jni, jobject packet) noexcept {
    return jni->env->GetIntField(packet, jni->mappings->fid_s14_entity_id);
}

// sub_180047D10
int8_t packet_s14_get_pos_x(events::JniContext* jni, jobject packet) noexcept {
    return static_cast<int8_t>(jni->env->GetByteField(packet, jni->mappings->fid_s14_pos_x));
}

// sub_180047D30
int8_t packet_s14_get_pos_y(events::JniContext* jni, jobject packet) noexcept {
    return static_cast<int8_t>(jni->env->GetByteField(packet, jni->mappings->fid_s14_pos_y));
}

// sub_180047D50
int8_t packet_s14_get_pos_z(events::JniContext* jni, jobject packet) noexcept {
    return static_cast<int8_t>(jni->env->GetByteField(packet, jni->mappings->fid_s14_pos_z));
}

// sub_180047D70
int8_t packet_s14_get_yaw(events::JniContext* jni, jobject packet) noexcept {
    return static_cast<int8_t>(jni->env->GetByteField(packet, jni->mappings->fid_s14_yaw));
}

// sub_180047D90
int8_t packet_s14_get_pitch(events::JniContext* jni, jobject packet) noexcept {
    return static_cast<int8_t>(jni->env->GetByteField(packet, jni->mappings->fid_s14_pitch));
}

// sub_180047DB0
bool packet_s14_has_rotation(events::JniContext* jni, jobject packet) noexcept {
    return jni->env->GetBooleanField(packet, jni->mappings->fid_s14_has_rot) != JNI_FALSE;
}

// sub_180047DE0
jobject as_packet_s18_entity_teleport(events::JniContext* jni, jobject obj) noexcept {
    return jni->env->IsInstanceOf(obj, jni->mappings->cls_s18_packet_entity_teleport) ? obj : nullptr;
}

// sub_180047E20
int32_t packet_s18_get_entity_id(events::JniContext* jni, jobject packet) noexcept {
    return jni->env->GetIntField(packet, jni->mappings->fid_s18_entity_id);
}

// sub_180047E40
int32_t packet_s18_get_pos_x(events::JniContext* jni, jobject packet) noexcept {
    return jni->env->GetIntField(packet, jni->mappings->fid_s18_pos_x);
}

// sub_180047E60
int32_t packet_s18_get_pos_y(events::JniContext* jni, jobject packet) noexcept {
    return jni->env->GetIntField(packet, jni->mappings->fid_s18_pos_y);
}

// sub_180047E80
int32_t packet_s18_get_pos_z(events::JniContext* jni, jobject packet) noexcept {
    return jni->env->GetIntField(packet, jni->mappings->fid_s18_pos_z);
}

// sub_180047EA0
int8_t packet_s18_get_yaw(events::JniContext* jni, jobject packet) noexcept {
    return static_cast<int8_t>(jni->env->GetByteField(packet, jni->mappings->fid_s18_yaw));
}

// sub_180047EC0
int8_t packet_s18_get_pitch(events::JniContext* jni, jobject packet) noexcept {
    return static_cast<int8_t>(jni->env->GetByteField(packet, jni->mappings->fid_s18_pitch));
}

// sub_180047EE0
jobject as_packet_s19_entity_status(events::JniContext* jni, jobject obj) noexcept {
    return jni->env->IsInstanceOf(obj, jni->mappings->cls_s19_packet_entity_status) ? obj : nullptr;
}

// sub_180047F20
int32_t packet_s19_get_entity_id(events::JniContext* jni, jobject packet) noexcept {
    return jni->env->GetIntField(packet, jni->mappings->fid_s19_entity_id);
}

// sub_180047F40
jobject as_packet_s29_sound_effect(events::JniContext* jni, jobject obj) noexcept {
    return jni->env->IsInstanceOf(obj, jni->mappings->cls_s29_packet_sound_effect) ? obj : nullptr;
}

// sub_180047F80
jobject as_packet_s06_update_health(events::JniContext* jni, jobject obj) noexcept {
    return jni->env->IsInstanceOf(obj, jni->mappings->cls_s06_packet_update_health) ? obj : nullptr;
}

// sub_180047FC0
jobject as_packet_s08_player_pos_look(events::JniContext* jni, jobject obj) noexcept {
    return jni->env->IsInstanceOf(obj, jni->mappings->cls_s08_packet_player_pos_look) ? obj : nullptr;
}

// sub_180048000
jobject as_packet_s27_explosion(events::JniContext* jni, jobject obj) noexcept {
    return jni->env->IsInstanceOf(obj, jni->mappings->cls_s27_packet_explosion) ? obj : nullptr;
}

// sub_180048040
float packet_s27_get_motion_x(events::JniContext* jni, jobject packet) noexcept {
    return jni->env->GetFloatField(packet, jni->mappings->fid_s27_motion_x);
}

// sub_180048060
void packet_s27_set_motion_x(events::JniContext* jni, jobject packet, float motion_x) noexcept {
    jni->env->SetFloatField(packet, jni->mappings->fid_s27_motion_x, motion_x);
}

// sub_180048090
float packet_s27_get_motion_y(events::JniContext* jni, jobject packet) noexcept {
    return jni->env->GetFloatField(packet, jni->mappings->fid_s27_motion_y);
}

// sub_1800480B0
void packet_s27_set_motion_y(events::JniContext* jni, jobject packet, float motion_y) noexcept {
    jni->env->SetFloatField(packet, jni->mappings->fid_s27_motion_y, motion_y);
}

// sub_1800480E0
float packet_s27_get_motion_z(events::JniContext* jni, jobject packet) noexcept {
    return jni->env->GetFloatField(packet, jni->mappings->fid_s27_motion_z);
}

// sub_180048100
void packet_s27_set_motion_z(events::JniContext* jni, jobject packet, float motion_z) noexcept {
    jni->env->SetFloatField(packet, jni->mappings->fid_s27_motion_z, motion_z);
}

// sub_180048130
jobject as_packet_s00_keep_alive(events::JniContext* jni, jobject obj) noexcept {
    return jni->env->IsInstanceOf(obj, jni->mappings->cls_s00_packet_keep_alive) ? obj : nullptr;
}

// sub_180048170
jobject as_packet_c00_keep_alive(events::JniContext* jni, jobject obj) noexcept {
    return jni->env->IsInstanceOf(obj, jni->mappings->cls_c00_packet_keep_alive) ? obj : nullptr;
}

// sub_1800481B0
jobject as_packet_s3f_custom_payload(events::JniContext* jni, jobject obj) noexcept {
    return jni->env->IsInstanceOf(obj, jni->mappings->cls_s3f_packet_custom_payload) ? obj : nullptr;
}

// sub_1800481F0
jstring packet_s3f_get_channel(events::JniContext* jni, jobject packet) noexcept {
    return static_cast<jstring>(jni->env->GetObjectField(packet, jni->mappings->fid_s3f_channel));
}

// sub_180048210
jbyteArray packet_s3f_get_payload_bytes(events::JniContext* jni, jobject packet) noexcept {
    jobject data_obj = jni->env->GetObjectField(packet, jni->mappings->fid_s3f_data);
    if (jni->mappings->version != 0) {
        const jsize len = jni->env->CallIntMethod(data_obj, jni->mappings->mid_packet_buffer_readable_bytes);
        const jint reader_idx = jni->env->CallIntMethod(data_obj, jni->mappings->mid_packet_buffer_reader_index);
        jbyteArray out = jni->env->NewByteArray(len);
        if (out == nullptr) {
            return nullptr;
        }
        jni->env->CallObjectMethod(data_obj, jni->mappings->mid_packet_buffer_get_bytes, reader_idx, out);
        return out;
    }
    return static_cast<jbyteArray>(data_obj);
}

// sub_1800482D0
int32_t packet_buffer_readable_bytes(events::JniContext* jni, jobject buf) noexcept {
    return jni->env->CallIntMethod(buf, jni->mappings->mid_packet_buffer_readable_bytes);
}

// sub_180048300
int32_t packet_buffer_reader_index(events::JniContext* jni, jobject buf) noexcept {
    return jni->env->CallIntMethod(buf, jni->mappings->mid_packet_buffer_reader_index);
}

// sub_180048330
jobject packet_buffer_get_bytes(events::JniContext* jni, jobject buf, int32_t index, jbyteArray dst) noexcept {
    return jni->env->CallObjectMethod(buf, jni->mappings->mid_packet_buffer_get_bytes, index, dst);
}

// sub_180048370
jobject as_packet_c0e_click_window(events::JniContext* jni, jobject obj) noexcept {
    return jni->env->IsInstanceOf(obj, jni->mappings->cls_c0e_packet_click_window) ? obj : nullptr;
}

// sub_1800483B0
jobject as_packet_c16_client_status(events::JniContext* jni, jobject obj) noexcept {
    return jni->env->IsInstanceOf(obj, jni->mappings->cls_c16_packet_client_status) ? obj : nullptr;
}

// sub_1800483F0
jobject c16_get_open_inventory_state(events::JniContext* jni) noexcept {
    return (jni->env->GetStaticObjectField(jni->mappings->cls_c16_enum_state, jni->mappings->sfid_c16_open_inventory_achievement));
}

// sub_180048420
jobject packet_c16_new(events::JniContext* jni, jobject enum_state) noexcept {
    return jni->env->NewObject(jni->mappings->cls_c16_packet_client_status, jni->mappings->mid_c16_packet_init, enum_state);
}

// sub_180048460
int32_t packet_c16_get_status_ordinal(events::JniContext* jni, jobject packet) noexcept {
    jobject state = jni->env->GetObjectField(packet, jni->mappings->fid_c16_packet_status);
    return enum_get_ordinal(jni, state);
}

// sub_1800484A0
jobject as_packet_c0d_close_window(events::JniContext* jni, jobject obj) noexcept {
    return jni->env->IsInstanceOf(obj, jni->mappings->cls_c0d_packet_close_window) ? obj : nullptr;
}

// sub_1800484E0
jobject packet_c0d_new(events::JniContext* jni, int32_t window_id) noexcept {
    return jni->env->NewObject(jni->mappings->cls_c0d_packet_close_window, jni->mappings->mid_c0d_packet_init, window_id);
}

// sub_180048520
jobject as_packet_c01_chat_message(events::JniContext* jni, jobject obj) noexcept {
    return jni->env->IsInstanceOf(obj, jni->mappings->cls_c01_packet_chat_message) ? obj : nullptr;
}

// sub_180048560
jstring packet_chat_get_message(events::JniContext* jni, jobject chat_packet) noexcept {
    return static_cast<jstring>(jni->env->GetObjectField(chat_packet, jni->mappings->fid_c01_message));
}

// sub_180048580
void packet_chat_set_message(events::JniContext* jni, jobject chat_packet, jstring message) noexcept {
    jni->env->SetObjectField(chat_packet, jni->mappings->fid_c01_message, message);
}

// sub_1800485B0
jobject as_packet_c14_tab_complete(events::JniContext* jni, jobject obj) noexcept {
    return jni->env->IsInstanceOf(obj, jni->mappings->cls_c14_packet_tab_complete) ? obj : nullptr;
}

// sub_1800485F0
jstring packet_tab_complete_get_message(events::JniContext* jni, jobject tab_packet) noexcept {
    return static_cast<jstring>(jni->env->GetObjectField(tab_packet, jni->mappings->fid_c14_message));
}

// sub_180048610
jobject as_packet_s2a_particles(events::JniContext* jni, jobject obj) noexcept {
    return jni->env->IsInstanceOf(obj, jni->mappings->cls_s2a_packet_particles) ? obj : nullptr;
}

// sub_180048650
int32_t enum_get_ordinal(events::JniContext* jni, jobject enum_obj) noexcept {
    return jni->env->CallIntMethod(enum_obj, jni->mappings->mid_enum_ordinal);
}

// sub_180048680
jobject render_manager_get_instance_1_7(events::JniContext* jni) noexcept {
    return (jni->env->GetStaticObjectField(jni->mappings->cls_render_manager, jni->mappings->sfid_render_manager_instance_1_7));
}

// sub_1800486B0
float render_manager_get_player_view_x(events::JniContext* jni, jobject render_manager) noexcept {
    return jni->env->GetFloatField(render_manager, jni->mappings->fid_render_manager_player_view_x);
}

// sub_1800486D0
void render_manager_set_player_view_x(events::JniContext* jni, jobject render_manager, float view_x) noexcept {
    jni->env->SetFloatField(render_manager, jni->mappings->fid_render_manager_player_view_x, view_x);
}

// sub_180048700
float render_manager_get_player_view_y(events::JniContext* jni, jobject render_manager) noexcept {
    return jni->env->GetFloatField(render_manager, jni->mappings->fid_render_manager_player_view_y);
}

// sub_180048720
void render_manager_set_player_view_y(events::JniContext* jni, jobject render_manager, float view_y) noexcept {
    jni->env->SetFloatField(render_manager, jni->mappings->fid_render_manager_player_view_y, view_y);
}

// sub_180048750
jobject render_manager_get_font_renderer(events::JniContext* jni, jobject render_manager) noexcept {
    return (jni->env->GetObjectField(render_manager, jni->mappings->fid_render_manager_font_renderer));
}

// sub_180048770
jobject as_score_player_team(events::JniContext* jni, jobject obj) noexcept {
    return jni->env->IsInstanceOf(obj, jni->mappings->cls_score_player_team) ? obj : nullptr;
}

// sub_1800487B0
jstring score_player_team_get_color_prefix(events::JniContext* jni, jobject team) noexcept {
    return static_cast<jstring>(jni->env->GetObjectField(team, jni->mappings->fid_score_player_team_name_prefix));
}

// sub_1800487D0
void entity_renderer_get_mouse_over(events::JniContext* jni, jobject entity_renderer, float partial_ticks) noexcept {
    jni->env->CallVoidMethod(entity_renderer, jni->mappings->mid_entity_renderer_get_mouse_over, partial_ticks);
}

// sub_180048810
void entity_renderer_render_hand(events::JniContext* jni, jobject entity_renderer, float partial_ticks, int32_t pass) noexcept {
    jni->env->CallVoidMethod(entity_renderer, jni->mappings->mid_entity_renderer_render_hand, partial_ticks, pass);
}

// sub_180048850
void entity_renderer_orient_camera(events::JniContext* jni, jobject entity_renderer, float partial_ticks) noexcept {
    jni->env->CallVoidMethod(entity_renderer, jni->mappings->mid_entity_renderer_orient_camera, partial_ticks);
}

// sub_180048890
float entity_renderer_get_fov_modifier(events::JniContext* jni, jobject entity_renderer, float partial_ticks) noexcept {
    return jni->env->CallFloatMethod(entity_renderer, jni->mappings->mid_entity_renderer_get_fov_modifier, partial_ticks, static_cast<jboolean>(JNI_TRUE));
}

// sub_1800488D0
void framebuffer_bind(events::JniContext* jni, jobject framebuffer, bool set_viewport) noexcept {
    jni->env->CallVoidMethod(framebuffer, jni->mappings->mid_framebuffer_bind, static_cast<jboolean>(set_viewport));
}

// sub_180048900
void render_global_set_entities_dirty(events::JniContext* jni, jobject render_global, bool dirty) noexcept {
    jni->env->SetBooleanField(render_global, jni->mappings->fid_render_global_entities_dirty, static_cast<jboolean>(dirty));
}

// sub_180048930
void active_render_info_update(events::JniContext* jni, jobject player, bool third_person_front) noexcept {
    jni->env->CallStaticVoidMethod(jni->mappings->cls_active_render_info, jni->mappings->smid_active_render_info_update, player, static_cast<jboolean>(third_person_front));
}

// sub_180048970
bool renderer_living_can_render_name(events::JniContext* jni, jobject renderer, jobject entity) noexcept {
    return jni->env->CallBooleanMethod(renderer, jni->mappings->mid_rle_can_render_name, entity) != JNI_FALSE;
}

// sub_1800489A0
void renderer_living_do_render_super(events::JniContext* jni, jobject renderer, jobject entity, double x, double y, double z, float entity_yaw, float partial_ticks) noexcept {
    jni->env->CallNonvirtualVoidMethod(renderer, jni->mappings->cls_renderer_living_entity, jni->mappings->mid_rle_do_render, entity, x, y, z, entity_yaw, partial_ticks);
}

// sub_180048A10
int32_t font_renderer_get_string_width(events::JniContext* jni, jobject font_renderer, jstring text) noexcept {
    return jni->env->CallIntMethod(font_renderer, jni->mappings->mid_font_renderer_get_string_width, text);
}

// sub_180048A40
int32_t font_renderer_draw_string(events::JniContext* jni, jobject font_renderer, jstring text, float x, float y, int32_t color, bool drop_shadow) noexcept {
    if (jni->mappings->version != 0) {
        return jni->env->CallIntMethod(font_renderer, jni->mappings->mid_font_renderer_draw_string, text, x, y, color, static_cast<jboolean>(drop_shadow));
    }
    const int32_t ix = static_cast<int32_t>(std::round(x));
    const int32_t iy = static_cast<int32_t>(std::round(y));
    return jni->env->CallIntMethod(font_renderer, jni->mappings->mid_font_renderer_draw_string, text, ix, iy, color, static_cast<jboolean>(drop_shadow));
}

// sub_180048B20
void gl_state_enable_cull(events::JniContext* jni) noexcept {
    jni->env->CallStaticVoidMethod(jni->mappings->cls_gl_state_manager, jni->mappings->smid_gl_enable_cull);
}

// sub_180048B50
void gl_state_disable_cull(events::JniContext* jni) noexcept {
    jni->env->CallStaticVoidMethod(jni->mappings->cls_gl_state_manager, jni->mappings->smid_gl_disable_cull);
}

// sub_180048B80
void gl_state_enable_lighting(events::JniContext* jni) noexcept {
    jni->env->CallStaticVoidMethod(jni->mappings->cls_gl_state_manager, jni->mappings->smid_gl_enable_lighting);
}

// sub_180048BB0
void gl_state_disable_lighting(events::JniContext* jni) noexcept {
    jni->env->CallStaticVoidMethod(jni->mappings->cls_gl_state_manager, jni->mappings->smid_gl_disable_lighting);
}

// sub_180048BE0
void gl_state_enable_depth(events::JniContext* jni) noexcept {
    jni->env->CallStaticVoidMethod(jni->mappings->cls_gl_state_manager, jni->mappings->smid_gl_enable_depth);
}

// sub_180048C10
void gl_state_disable_depth(events::JniContext* jni) noexcept {
    jni->env->CallStaticVoidMethod(jni->mappings->cls_gl_state_manager, jni->mappings->smid_gl_disable_depth);
}

// sub_180048C40
void gl_state_enable_blend(events::JniContext* jni) noexcept {
    jni->env->CallStaticVoidMethod(jni->mappings->cls_gl_state_manager, jni->mappings->smid_gl_enable_blend);
}

// sub_180048C70
void gl_state_disable_blend(events::JniContext* jni) noexcept {
    jni->env->CallStaticVoidMethod(jni->mappings->cls_gl_state_manager, jni->mappings->smid_gl_disable_blend);
}

// sub_180048CA0
void gl_state_enable_texture_2d(events::JniContext* jni) noexcept {
    jni->env->CallStaticVoidMethod(jni->mappings->cls_gl_state_manager, jni->mappings->smid_gl_enable_texture_2d);
}

// sub_180048CD0
void gl_state_disable_texture_2d(events::JniContext* jni) noexcept {
    jni->env->CallStaticVoidMethod(jni->mappings->cls_gl_state_manager, jni->mappings->smid_gl_disable_texture_2d);
}

// sub_180048D00
void gl_state_enable_alpha(events::JniContext* jni) noexcept {
    jni->env->CallStaticVoidMethod(jni->mappings->cls_gl_state_manager, jni->mappings->smid_gl_enable_alpha);
}

// sub_180048D30
void gl_state_disable_alpha(events::JniContext* jni) noexcept {
    jni->env->CallStaticVoidMethod(jni->mappings->cls_gl_state_manager, jni->mappings->smid_gl_disable_alpha);
}

// sub_180048D60
void gl_state_blend_func(events::JniContext* jni, int32_t src_factor, int32_t dst_factor) noexcept {
    jni->env->CallStaticVoidMethod(jni->mappings->cls_gl_state_manager, jni->mappings->smid_gl_blend_func, src_factor, dst_factor);
}

// sub_180048DA0
void gl_state_color(events::JniContext* jni, float r, float g, float b, float a) noexcept {
    jni->env->CallStaticVoidMethod(jni->mappings->cls_gl_state_manager, jni->mappings->smid_gl_color, r, g, b, a);
}

// sub_180048E10
jobject potion_get_by_id(events::JniContext* jni, uint32_t potion_id) noexcept {
    if (potion_id > 0xFFu) {
        return nullptr;
    }
    auto arr = static_cast<jobjectArray>(jni->env->GetStaticObjectField(jni->mappings->cls_potion, jni->mappings->sfid_potion_types));
    return jni->env->GetObjectArrayElement(arr, static_cast<jsize>(potion_id));
}

// sub_180048E70
void potion_effect_set_duration(events::JniContext* jni, jobject potion_effect, int32_t duration) noexcept {
    jni->env->SetIntField(potion_effect, jni->mappings->fid_potion_effect_duration, duration);
}

// sub_180048EA0
void lwjgl_display_update(events::JniContext* jni) noexcept {
    jni->env->CallStaticVoidMethod(jni->mappings->cls_lwjgl_display, jni->mappings->smid_display_update);
}

// sub_180048ED0
bool lwjgl_display_is_active(events::JniContext* jni) noexcept {
    return jni->env->CallStaticBooleanMethod(jni->mappings->cls_lwjgl_display, jni->mappings->smid_display_is_active) != JNI_FALSE;
}

// sub_180048F10
int32_t lwjgl_display_get_width(events::JniContext* jni) noexcept {
    return jni->env->GetStaticIntField(jni->mappings->cls_lwjgl_display, jni->mappings->sfid_display_width);
}

// sub_180048F40
int32_t lwjgl_display_get_height(events::JniContext* jni) noexcept {
    return jni->env->GetStaticIntField(jni->mappings->cls_lwjgl_display, jni->mappings->sfid_display_height);
}

// sub_180048F70
int32_t lwjgl_display_get_framebuffer_width(events::JniContext* jni) noexcept {
    return jni->env->GetStaticIntField(jni->mappings->cls_lwjgl_display, jni->mappings->sfid_display_framebuffer_width);
}

// sub_180048FA0
int32_t lwjgl_display_get_framebuffer_height(events::JniContext* jni) noexcept {
    return jni->env->GetStaticIntField(jni->mappings->cls_lwjgl_display, jni->mappings->sfid_display_framebuffer_height);
}

// sub_180048FD0
jobject render_item_new_1_7(events::JniContext* jni) noexcept {
    return jni->env->NewObject(jni->mappings->cls_render_item, jni->mappings->mid_render_item_init_1_7);
}

// sub_180049000
float render_item_get_z_level(events::JniContext* jni, jobject render_item) noexcept {
    return jni->env->GetFloatField(render_item, jni->mappings->fid_render_item_z_level);
}

// sub_180049020
void render_item_set_z_level(events::JniContext* jni, jobject render_item, float z_level) noexcept {
    jni->env->SetFloatField(render_item, jni->mappings->fid_render_item_z_level, z_level);
}

// sub_180049050
void render_item_into_gui(events::JniContext* jni, jobject render_item, jobject font_renderer, jobject texture_manager, jobject stack, int32_t x, int32_t y) noexcept {
    if (jni->mappings->version == 0) {
        jni->env->CallVoidMethod(render_item, jni->mappings->mid_render_item_into_gui, font_renderer, texture_manager, stack, x, y);
    } else {
        jni->env->CallVoidMethod(render_item, jni->mappings->mid_render_item_into_gui, stack, x, y);
    }
}

// sub_1800490C0
void render_item_overlay_into_gui(events::JniContext* jni, jobject render_item, jobject font_renderer, jobject texture_manager, jobject stack, int32_t x, int32_t y, jstring text) noexcept {
    if (jni->mappings->version == 0) {
        jni->env->CallVoidMethod(render_item, jni->mappings->mid_render_item_overlay_into_gui, font_renderer, texture_manager, stack, x, y, text);
    } else {
        jni->env->CallVoidMethod(render_item, jni->mappings->mid_render_item_overlay_into_gui, font_renderer, stack, x, y, text);
    }
}

// sub_180049140
void render_helper_enable_item_lighting(events::JniContext* jni) noexcept {
    jni->env->CallStaticVoidMethod(jni->mappings->cls_render_helper, jni->mappings->smid_render_helper_enable_item_lighting);
}

// sub_180049170
void render_helper_disable_item_lighting(events::JniContext* jni) noexcept {
    jni->env->CallStaticVoidMethod(jni->mappings->cls_render_helper, jni->mappings->smid_render_helper_disable_item_lighting);
}

// sub_1800491A0
jobject as_entity_item(events::JniContext* jni, jobject obj) noexcept {
    return jni->env->IsInstanceOf(obj, jni->mappings->cls_entity_item) ? obj : nullptr;
}

// sub_1800491E0
jobject entity_item_get_stack(events::JniContext* jni, jobject entity_item) noexcept {
    return (jni->env->CallObjectMethod(entity_item, jni->mappings->mid_entity_item_get_item_stack));
}

// sub_180049210
void entity_item_search_nearby(events::JniContext* jni, jobject entity_item) noexcept {
    jni->env->CallVoidMethod(entity_item, jni->mappings->mid_entity_item_search_nearby);
}

// sub_180049240
int64_t uuid_get_least_sig_bits(events::JniContext* jni, jobject uuid) noexcept {
    return jni->env->CallLongMethod(uuid, jni->mappings->mid_uuid_get_least_sig_bits);
}

// sub_180049270
int64_t uuid_get_most_sig_bits(events::JniContext* jni, jobject uuid) noexcept {
    return jni->env->CallLongMethod(uuid, jni->mappings->mid_uuid_get_most_sig_bits);
}

// sub_1800492A0
float controller_get_cur_block_damage(events::JniContext* jni, jobject controller) noexcept {
    return jni->env->GetFloatField(controller, jni->mappings->fid_controller_cur_block_damage_mp);
}

// sub_1800492C0
void controller_set_cur_block_damage(events::JniContext* jni, jobject controller, float damage) noexcept {
    jni->env->SetFloatField(controller, jni->mappings->fid_controller_cur_block_damage_mp, damage);
}

// sub_1800492E0
int32_t controller_get_block_hit_delay(events::JniContext* jni, jobject controller) noexcept {
    return jni->env->GetIntField(controller, jni->mappings->fid_controller_block_hit_delay);
}

// sub_180049300
void controller_set_block_hit_delay(events::JniContext* jni, jobject controller, int32_t delay) noexcept {
    jni->env->SetIntField(controller, jni->mappings->fid_controller_block_hit_delay, delay);
}

// sub_180049320
bool controller_is_hitting_block(events::JniContext* jni, jobject controller) noexcept {
    return jni->env->GetBooleanField(controller, jni->mappings->fid_controller_is_hitting_block) != JNI_FALSE;
}

// sub_180049340
void controller_set_hitting_block(events::JniContext* jni, jobject controller, bool hitting) noexcept {
    jni->env->SetBooleanField(controller, jni->mappings->fid_controller_is_hitting_block, static_cast<jboolean>(hitting));
}

// sub_180049360
void controller_update(events::JniContext* jni, jobject controller) noexcept {
    jni->env->CallVoidMethod(controller, jni->mappings->mid_controller_update_controller);
}

// sub_180049390
int32_t nbt_tag_list_count(events::JniContext* jni, jobject tag_list) noexcept {
    return jni->env->CallIntMethod(tag_list, jni->mappings->mid_nbt_tag_list_tag_count);
}

// sub_1800493C0
jobject nbt_tag_list_get_compound_at(events::JniContext* jni, jobject tag_list, int32_t index) noexcept {
    return jni->env->CallObjectMethod(tag_list, jni->mappings->mid_nbt_tag_list_get_compound_tag_at, index);
}

// sub_1800493F0
int16_t nbt_tag_compound_get_short(events::JniContext* jni, jobject compound, jstring key) noexcept {
    return jni->env->CallShortMethod(compound, jni->mappings->mid_nbt_tag_compound_get_short, key);
}

// sub_180049420
bool entity_arrow_is_in_ground(events::JniContext* jni, jobject arrow) noexcept {
    return jni->env->GetBooleanField(arrow, jni->mappings->fid_entity_arrow_in_ground) != JNI_FALSE;
}

// sub_180049440
jstring stat_collector_translate_to_local(events::JniContext* jni, jstring key) noexcept {
    return static_cast<jstring>(jni->env->CallStaticObjectMethod(jni->mappings->cls_stat_collector, jni->mappings->smid_stat_collector_translate_to_local, key));
}

} // namespace slinky::minecraft
