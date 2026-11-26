#pragma once

#include <cstddef>
#include <cstdint>
#include <jni.h>

#include "events/events.hpp"
#include "minecraft/mappings.hpp"
#include "util/math.hpp"

namespace slinky {

namespace minecraft {

// Integer 3D block coordinate matching BlockPos / Vec3i (12 bytes, returned via sret)
struct Vec3i {
    int32_t x = 0;
    int32_t y = 0;
    int32_t z = 0;
};
static_assert(sizeof(Vec3i) == 12, "Vec3i must be 12 bytes");
using BlockPos = Vec3i;

// 6 EnumFacing unit normal offsets (@ 0x18016BC00): DOWN, UP, NORTH, SOUTH, WEST, EAST
extern const math::Vec3d kEnumFacingOffsets[6];

jobject mc_get_instance(events::JniContext* jni) noexcept; // sub_180044040
jobject mc_get_player(events::JniContext* jni, jobject mc) noexcept; // sub_180044070
jobject mc_get_world(events::JniContext* jni, jobject mc) noexcept; // sub_180044090
jobject mc_get_timer(events::JniContext* jni, jobject mc) noexcept; // sub_1800440B0
jobject mc_get_gui_container_screen(events::JniContext* jni, jobject mc) noexcept; // sub_1800440D0
jobject mc_get_game_settings(events::JniContext* jni, jobject mc) noexcept; // sub_180044130
jobject mc_get_net_handler(events::JniContext* jni, jobject mc) noexcept; // sub_180044150
jobject mc_get_render_manager(events::JniContext* jni, jobject mc) noexcept; // sub_180044180
jobject mc_get_render_global(events::JniContext* jni, jobject mc) noexcept; // sub_1800441B0
jobject mc_get_entity_renderer(events::JniContext* jni, jobject mc) noexcept; // sub_1800441D0
jobject mc_get_render_item(events::JniContext* jni, jobject mc) noexcept; // sub_1800441F0
jobject mc_get_texture_manager(events::JniContext* jni, jobject mc) noexcept; // sub_180044210
jobject mc_get_player_controller(events::JniContext* jni, jobject mc) noexcept; // sub_180044230
jobject mc_get_object_mouse_over(events::JniContext* jni, jobject mc) noexcept; // sub_180044250
void mc_set_object_mouse_over(events::JniContext* jni, jobject mc, jobject mop) noexcept; // sub_180044270
bool mc_has_ingame_focus(events::JniContext* jni, jobject mc) noexcept; // sub_1800442A0
int32_t mc_get_left_click_counter(events::JniContext* jni, jobject mc) noexcept; // sub_1800442D0
void mc_set_left_click_counter(events::JniContext* jni, jobject mc, int32_t count) noexcept; // sub_1800442F0
int32_t mc_get_right_click_delay_timer(events::JniContext* jni, jobject mc) noexcept; // sub_180044320
void mc_set_right_click_delay_timer(events::JniContext* jni, jobject mc, int32_t delay) noexcept; // sub_180044340
int32_t mc_get_display_width(events::JniContext* jni, jobject mc) noexcept; // sub_180044370
int32_t mc_get_display_height(events::JniContext* jni, jobject mc) noexcept; // sub_180044390
jobject mc_get_render_view_entity(events::JniContext* jni, jobject mc) noexcept; // sub_1800443B0
jobject mc_add_scheduled_task(events::JniContext* jni, jobject mc, jobject runnable) noexcept; // sub_1800443D0
void mc_click_mouse(events::JniContext* jni, jobject mc) noexcept; // sub_180044400
void mc_right_click_mouse(events::JniContext* jni, jobject mc) noexcept; // sub_180044430
bool is_version_1_7_10(events::JniContext* jni) noexcept; // sub_180044460
math::Vec3d entity_get_position(events::JniContext* jni, jobject entity) noexcept; // sub_180044470
math::Vec3d entity_get_prev_position(events::JniContext* jni, jobject entity) noexcept; // sub_180044510
math::Vec2f entity_get_rotation(events::JniContext* jni, jobject entity) noexcept; // sub_1800445B0
void entity_set_rotation(events::JniContext* jni, jobject entity, math::Vec2f rotation) noexcept; // sub_180044620
math::Vec2f entity_get_prev_rotation(events::JniContext* jni, jobject entity) noexcept; // sub_1800446B0
void entity_set_prev_rotation(events::JniContext* jni, jobject entity, math::Vec2f rotation) noexcept; // sub_180044720
void entity_set_angles(events::JniContext* jni, jobject entity, float yaw_delta, float pitch_delta) noexcept; // sub_1800447B0
math::Vec3d entity_get_look_vector(events::JniContext* jni, jobject entity) noexcept; // sub_1800447F0
double entity_get_motion_x(events::JniContext* jni, jobject entity) noexcept; // sub_180044930
double entity_get_motion_y(events::JniContext* jni, jobject entity) noexcept; // sub_180044950
double entity_get_motion_z(events::JniContext* jni, jobject entity) noexcept; // sub_180044970
math::Vec3d entity_get_motion(events::JniContext* jni, jobject entity) noexcept; // sub_180044990
void entity_set_motion(events::JniContext* jni, jobject entity, const math::Vec3d& motion) noexcept; // sub_180044A30
jobject entity_get_bounding_box(events::JniContext* jni, jobject entity) noexcept; // sub_180044AB0
bool entity_is_sprinting(events::JniContext* jni, jobject entity) noexcept; // sub_180044AD0
void entity_set_sprinting(events::JniContext* jni, jobject entity, bool sprinting) noexcept; // sub_180044B00
bool entity_is_sneaking(events::JniContext* jni, jobject entity) noexcept; // sub_180044B30
int32_t entity_get_id(events::JniContext* jni, jobject entity) noexcept; // sub_180044B60
jobject entity_get_unique_id(events::JniContext* jni, jobject entity) noexcept; // sub_180044B80
int32_t entity_get_ticks_existed(events::JniContext* jni, jobject entity) noexcept; // sub_180044BA0
bool entity_is_on_ground(events::JniContext* jni, jobject entity) noexcept; // sub_180044BC0
bool entity_is_dead(events::JniContext* jni, jobject entity) noexcept; // sub_180044BF0
float entity_get_fall_distance(events::JniContext* jni, jobject entity) noexcept; // sub_180044C20
jobject entity_get_display_name(events::JniContext* jni, jobject entity) noexcept; // sub_180044C40
jstring entity_get_name(events::JniContext* jni, jobject entity) noexcept; // sub_180044C70
int32_t entity_get_type_id(events::JniContext* jni, jobject entity) noexcept; // sub_180044CA0
void entity_move_flying(events::JniContext* jni, jobject entity, float strafe, float forward, float friction) noexcept; // sub_180044CE0
bool entity_can_be_collided_with(events::JniContext* jni, jobject entity) noexcept; // sub_180044D40
jobject entity_get_riding_entity(events::JniContext* jni, jobject entity) noexcept; // sub_180044D70
jobject as_entity_living_base(events::JniContext* jni, jobject obj) noexcept; // sub_180044D90
jobject living_get_position_eyes(events::JniContext* jni, jobject living) noexcept; // sub_180044DD0
int32_t living_get_last_attacker_time(events::JniContext* jni, jobject living) noexcept; // sub_180044E10
int32_t living_get_hurt_time(events::JniContext* jni, jobject living) noexcept; // sub_180044E30
void living_set_max_hurt_time(events::JniContext* jni, jobject living, int32_t ticks) noexcept; // sub_180044E50
float living_get_health(events::JniContext* jni, jobject living) noexcept; // sub_180044E80
float living_get_max_health(events::JniContext* jni, jobject living) noexcept; // sub_180044ED0
float living_get_move_forward(events::JniContext* jni, jobject living) noexcept; // sub_180044F20
float living_get_move_strafing(events::JniContext* jni, jobject living) noexcept; // sub_180044F40
jobject living_ray_trace(events::JniContext* jni, jobject living, double reach, float partial_ticks) noexcept; // sub_180044F60
float living_get_land_movement_factor(events::JniContext* jni, jobject living) noexcept; // sub_180044FA0
int32_t living_get_jump_ticks(events::JniContext* jni, jobject living) noexcept; // sub_180044FC0
void living_set_jump_ticks(events::JniContext* jni, jobject living, int32_t ticks) noexcept; // sub_180044FE0
void living_swing_item_super(events::JniContext* jni, jobject living) noexcept; // sub_180045010
jobject living_get_team(events::JniContext* jni, jobject living) noexcept; // sub_180045040
jobject living_get_active_potion_effect(events::JniContext* jni, jobject living, jobject potion) noexcept; // sub_180045070
void living_jump(events::JniContext* jni, jobject living) noexcept; // sub_1800450A0
math::Vec2f player_sp_get_render_arm_rotation(events::JniContext* jni, jobject player_sp) noexcept; // sub_1800450D0
void player_sp_set_render_arm_rotation(events::JniContext* jni, jobject player_sp, math::Vec2f rotation) noexcept; // sub_180045140
math::Vec2f player_sp_get_prev_render_arm_rotation(events::JniContext* jni, jobject player_sp) noexcept; // sub_1800451B0
void player_sp_set_prev_render_arm_rotation(events::JniContext* jni, jobject player_sp, math::Vec2f rotation) noexcept; // sub_180045220
void player_sp_add_chat_message(events::JniContext* jni, jobject player_sp, jobject chat_component) noexcept; // sub_180045290
void player_sp_set_time_in_portal(events::JniContext* jni, jobject player_sp, float value) noexcept; // sub_1800452C0
void player_sp_set_prev_time_in_portal(events::JniContext* jni, jobject player_sp, float value) noexcept; // sub_1800452F0
jobject as_entity_player_sp(events::JniContext* jni, jobject obj) noexcept; // sub_180045320
jobject as_entity_player(events::JniContext* jni, jobject obj) noexcept; // sub_180045360
jobject player_get_inventory(events::JniContext* jni, jobject player) noexcept; // sub_1800453A0
bool player_is_invisible_to_player(events::JniContext* jni, jobject player, jobject viewer) noexcept; // sub_1800453C0
bool player_is_using_item(events::JniContext* jni, jobject player) noexcept; // sub_1800453F0
int32_t player_get_item_in_use_count(events::JniContext* jni, jobject player) noexcept; // sub_180045420
float player_get_absorption_amount(events::JniContext* jni, jobject player) noexcept; // sub_180045440
void player_set_item_in_use_count(events::JniContext* jni, jobject player, int32_t count) noexcept; // sub_180045490
jobject player_get_equipment_in_slot(events::JniContext* jni, jobject player, int32_t slot) noexcept; // sub_1800454C0
jobject player_get_inventory_container(events::JniContext* jni, jobject player) noexcept; // sub_1800454F0
bool player_is_sleeping(events::JniContext* jni, jobject player) noexcept; // sub_180045510
float player_get_speed_on_ground(events::JniContext* jni, jobject player) noexcept; // sub_180045540
void player_set_speed_on_ground(events::JniContext* jni, jobject player, float speed) noexcept; // sub_180045560
jobject as_entity_other_player_mp(events::JniContext* jni, jobject obj) noexcept; // sub_180045590
math::Vec3d other_player_mp_get_target_pos(events::JniContext* jni, jobject other_player) noexcept; // sub_1800455D0
int32_t other_player_mp_get_pos_rot_increments(events::JniContext* jni, jobject other_player) noexcept; // sub_180045670
double other_player_mp_get_target_yaw(events::JniContext* jni, jobject other_player) noexcept; // sub_180045690
jobject block_get_from_item(events::JniContext* jni, jobject item) noexcept; // sub_1800456B0
int32_t block_get_id(events::JniContext* jni, jobject block) noexcept; // sub_1800456F0
jobject block_pos_new(events::JniContext* jni, int32_t x, int32_t y, int32_t z) noexcept; // sub_180045730
math::Vec3d block_pos_to_vec3d(events::JniContext* jni, jobject block_pos) noexcept; // sub_180045780
void block_pos_set_xyz(events::JniContext* jni, jobject block_pos, int32_t x, int32_t y, int32_t z) noexcept; // sub_180045830
Vec3i block_pos_to_vec3i(events::JniContext* jni, jobject block_pos) noexcept; // sub_1800458B0
jobject iblock_state_get_block(events::JniContext* jni, jobject block_state) noexcept; // sub_180045930
jobject tile_entity_get_block_type(events::JniContext* jni, jobject tile_entity) noexcept; // sub_180045960
math::Vec3d tile_entity_get_position(events::JniContext* jni, jobject tile_entity) noexcept; // sub_180045980
jobject world_get_player_entities(events::JniContext* jni, jobject world) noexcept; // sub_180045A70
jobject world_get_loaded_entity_list(events::JniContext* jni, jobject world) noexcept; // sub_180045A90
jobject world_get_loaded_tile_entity_list(events::JniContext* jni, jobject world) noexcept; // sub_180045AB0
jobject world_get_entity_by_id(events::JniContext* jni, jobject world, int32_t entity_id) noexcept; // sub_180045AD0
jobject world_get_block(events::JniContext* jni, jobject world, int32_t x, int32_t y, int32_t z) noexcept; // sub_180045B00
jobject world_get_block_mutable_pos(events::JniContext* jni, jobject world, jobject reusable_pos, int32_t x, int32_t y, int32_t z) noexcept; // sub_180045B90
bool world_set_block_1_7(events::JniContext* jni, jobject world, int32_t x, int32_t y, int32_t z, jobject block, int32_t meta, int32_t flags) noexcept; // sub_180045C30
bool world_set_block_state_1_8(events::JniContext* jni, jobject world, jobject pos, jobject state, int32_t flags) noexcept; // sub_180045C90
jobject world_ray_trace_blocks(events::JniContext* jni, jobject world, jobject start_vec, jobject end_vec, bool stop_on_liquid, bool ignore_block_without_bbox, bool return_last_uncollidable) noexcept; // sub_180045CD0
void world_update_entities(events::JniContext* jni, jobject world) noexcept; // sub_180045D30
jobject world_get_chunk_from_chunk_coords(events::JniContext* jni, jobject world, int32_t chunk_x, int32_t chunk_z) noexcept; // sub_180045D60
void world_mark_block_range_for_render_update(events::JniContext* jni, jobject world, int32_t x1, int32_t y1, int32_t z1, int32_t x2, int32_t y2, int32_t z2) noexcept; // sub_180045DA0
jobject world_get_chunk_provider_client(events::JniContext* jni, jobject world) noexcept; // sub_180045E00
jobjectArray chunk_get_storage_arrays(events::JniContext* jni, jobject chunk) noexcept; // sub_180045E60
int32_t chunk_get_x(events::JniContext* jni, jobject chunk) noexcept; // sub_180045E80
int32_t chunk_get_z(events::JniContext* jni, jobject chunk) noexcept; // sub_180045EA0
jobject extended_block_storage_get_data(events::JniContext* jni, jobject ebs) noexcept; // sub_180045EC0
void chunk_provider_unload_chunk(events::JniContext* jni, jobject provider, int32_t chunk_x, int32_t chunk_z) noexcept; // sub_180045EE0
jobject chunk_provider_get_chunk_listing(events::JniContext* jni, jobject provider) noexcept; // sub_180045F20
jobject mop_new_entity_hit(events::JniContext* jni, jobject entity, jobject hit_vec) noexcept; // sub_180045F40
jobject aabb_calculate_intercept(events::JniContext* jni, jobject aabb, jobject vec_a, jobject vec_b) noexcept; // sub_180045F80
int32_t mop_get_type_ordinal(events::JniContext* jni, jobject mop) noexcept; // sub_180045FC0
int32_t mop_get_side_hit(events::JniContext* jni, jobject mop) noexcept; // sub_180046000
jobject mop_get_hit_vec(events::JniContext* jni, jobject mop) noexcept; // sub_180046060
math::Vec3d mop_get_block_pos_vec3d(events::JniContext* jni, jobject mop) noexcept; // sub_180046080
Vec3i mop_get_block_pos_vec3i(events::JniContext* jni, jobject mop) noexcept; // sub_180046150
math::Vec3d mop_get_offset_block_pos_vec3d(events::JniContext* jni, jobject mop) noexcept; // sub_1800461F0
jobject mop_get_entity_hit(events::JniContext* jni, jobject mop) noexcept; // sub_1800462A0
int32_t java_list_size(events::JniContext* jni, jobject list) noexcept; // sub_1800462C0
jobject java_list_get(events::JniContext* jni, jobject list, int32_t index) noexcept; // sub_1800462F0
bool java_list_add(events::JniContext* jni, jobject list, jobject elem) noexcept; // sub_180046320
bool java_list_remove(events::JniContext* jni, jobject list, jobject elem) noexcept; // sub_180046350
bool java_map_contains_key(events::JniContext* jni, jobject map_obj, jobject key) noexcept; // sub_180046380
jobject java_map_get(events::JniContext* jni, jobject map_obj, jobject key) noexcept; // sub_1800463B0
float settings_get_mouse_sensitivity(events::JniContext* jni, jobject settings) noexcept; // sub_1800463E0
jobject settings_get_key_use_item(events::JniContext* jni, jobject settings) noexcept; // sub_180046400
jobject settings_get_key_drop(events::JniContext* jni, jobject settings) noexcept; // sub_180046450
jobject settings_get_key_sneak(events::JniContext* jni, jobject settings) noexcept; // sub_1800464A0
jobject settings_get_key_jump(events::JniContext* jni, jobject settings) noexcept; // sub_1800464F0
jobject settings_get_key_forward(events::JniContext* jni, jobject settings) noexcept; // sub_180046540
jobject settings_get_key_back(events::JniContext* jni, jobject settings) noexcept; // sub_180046590
jobject settings_get_key_left(events::JniContext* jni, jobject settings) noexcept; // sub_1800465E0
jobject settings_get_key_right(events::JniContext* jni, jobject settings) noexcept; // sub_180046630
jobject settings_get_key_sprint(events::JniContext* jni, jobject settings) noexcept; // sub_180046680
jobject settings_get_key_attack(events::JniContext* jni, jobject settings) noexcept; // sub_1800466E0
jobject settings_get_key_inventory(events::JniContext* jni, jobject settings) noexcept; // sub_180046730
int32_t settings_get_third_person_view(events::JniContext* jni, jobject settings) noexcept; // sub_180046780
void settings_set_third_person_view(events::JniContext* jni, jobject settings, int32_t mode) noexcept; // sub_1800467A0
int32_t key_binding_get_press_time(events::JniContext* jni, jobject key_binding) noexcept; // sub_1800467D0
void key_binding_increment_press_time(events::JniContext* jni, jobject key_binding) noexcept; // sub_1800467F0
void key_binding_set_press_time(events::JniContext* jni, jobject key_binding, int32_t press_time) noexcept; // sub_180046850
int32_t key_binding_get_key_code(events::JniContext* jni, jobject key_binding) noexcept; // sub_180046880
bool key_binding_is_pressed(events::JniContext* jni, jobject key_binding) noexcept; // sub_1800468A0
void key_binding_set_pressed(events::JniContext* jni, jobject key_binding, bool pressed) noexcept; // sub_1800468D0
bool key_binding_is_physically_down(events::JniContext* jni, jobject key_binding) noexcept; // sub_180046900
jobject vec3_new(events::JniContext* jni, const math::Vec3d& v) noexcept; // sub_180046960
math::Vec3d vec3_to_vec3d(events::JniContext* jni, jobject vec3_obj) noexcept; // sub_1800469B0
jobject aabb_new(events::JniContext* jni, const math::AABB& box) noexcept; // sub_180046A50
math::AABB aabb_to_aabb(events::JniContext* jni, jobject aabb_obj) noexcept; // sub_180046AB0
bool lwjgl_keyboard_is_key_down(events::JniContext* jni, int32_t key_code) noexcept; // sub_180046BD0
bool lwjgl_keyboard_next(events::JniContext* jni) noexcept; // sub_180046C10
bool lwjgl_keyboard_get_event_key_state(events::JniContext* jni) noexcept; // sub_180046C50
int32_t lwjgl_keyboard_get_event_key(events::JniContext* jni) noexcept; // sub_180046C90
bool lwjgl_mouse_is_grabbed(events::JniContext* jni) noexcept; // sub_180046CC0
void lwjgl_mouse_set_grabbed(events::JniContext* jni, bool grabbed) noexcept; // sub_180046D00
bool lwjgl_mouse_is_button_down(events::JniContext* jni, int32_t button) noexcept; // sub_180046D40
bool lwjgl_mouse_next(events::JniContext* jni) noexcept; // sub_180046D80
void lwjgl_mouse_set_cursor_position(events::JniContext* jni, int32_t x, int32_t y) noexcept; // sub_180046DC0
float timer_get_render_partial_ticks(events::JniContext* jni, jobject timer) noexcept; // sub_180046E00
float timer_get_speed(events::JniContext* jni, jobject timer) noexcept; // sub_180046E20
void timer_set_speed(events::JniContext* jni, jobject timer, float speed) noexcept; // sub_180046E40
float timer_get_elapsed_partial_ticks(events::JniContext* jni, jobject timer) noexcept; // sub_180046E70
void timer_set_elapsed_partial_ticks(events::JniContext* jni, jobject timer, float partial_ticks) noexcept; // sub_180046E90
int32_t timer_get_elapsed_ticks(events::JniContext* jni, jobject timer) noexcept; // sub_180046EC0
int32_t inv_player_get_current_item(events::JniContext* jni, jobject inv_player) noexcept; // sub_180046EE0
void inv_player_set_current_item(events::JniContext* jni, jobject inv_player, int32_t slot) noexcept; // sub_180046F00
jobjectArray inv_player_get_main_inventory(events::JniContext* jni, jobject inv_player) noexcept; // sub_180046F30
jobject inv_player_get_dragged_stack(events::JniContext* jni, jobject inv_player) noexcept; // sub_180046F50
void gui_screen_key_typed(events::JniContext* jni, jobject gui_screen, jchar typed_char, int32_t key_code) noexcept; // sub_180046F70
void gui_container_set_last_click_time(events::JniContext* jni, jobject gui_container, int64_t time_ms) noexcept; // sub_180046FB0
jobject gui_container_get_hovered_slot(events::JniContext* jni, jobject gui_container) noexcept; // sub_180046FE0
bool is_gui_inventory(events::JniContext* jni, jobject obj) noexcept; // sub_180047000
bool is_gui_chest(events::JniContext* jni, jobject obj) noexcept; // sub_180047030
void gui_container_handle_mouse_click(events::JniContext* jni, jobject gui_container, int32_t slot_id, int32_t mouse_button, int32_t click_mode) noexcept; // sub_180047060
jobject gui_chest_get_lower_inventory(events::JniContext* jni, jobject gui_chest) noexcept; // sub_1800470B0
int32_t item_get_id(events::JniContext* jni, jobject item) noexcept; // sub_1800470D0
jobject item_get_by_id(events::JniContext* jni, int32_t item_id) noexcept; // sub_180047110
jobject item_stack_new_from_block(events::JniContext* jni, jobject block) noexcept; // sub_180047150
jobject item_stack_get_item(events::JniContext* jni, jobject stack) noexcept; // sub_180047190
float item_stack_get_str_vs_block(events::JniContext* jni, jobject stack, jobject block) noexcept; // sub_1800471C0
int32_t item_stack_get_size(events::JniContext* jni, jobject stack) noexcept; // sub_1800471F0
void item_stack_set_size(events::JniContext* jni, jobject stack, int32_t size) noexcept; // sub_180047210
int32_t item_stack_get_damage(events::JniContext* jni, jobject stack) noexcept; // sub_180047240
jobject item_stack_get_enchantment_tag_list(events::JniContext* jni, jobject stack) noexcept; // sub_180047260
int32_t item_stack_get_max_stack_size(events::JniContext* jni, jobject stack) noexcept; // sub_180047290
bool item_stack_are_tags_equal(events::JniContext* jni, jobject stack_a, jobject stack_b) noexcept; // sub_1800472C0
bool item_stack_has_display_name(events::JniContext* jni, jobject stack) noexcept; // sub_180047300
jobject net_handler_get_player_info_map(events::JniContext* jni, jobject net_handler) noexcept; // sub_180047330
jobject net_handler_get_network_manager(events::JniContext* jni, jobject net_handler) noexcept; // sub_180047350
jobject network_manager_get_channel(events::JniContext* jni, jobject net_manager) noexcept; // sub_180047370
void network_manager_send_packet_1_7(events::JniContext* jni, jobject net_manager, jobject packet, jobjectArray listeners) noexcept; // sub_180047390
void network_manager_send_packet_1_8(events::JniContext* jni, jobject net_manager, jobject packet) noexcept; // sub_1800473D0
void network_manager_send_packet_no_event(events::JniContext* jni, jobject net_manager, jobject packet) noexcept; // sub_180047400
jobject network_player_info_get_display_name(events::JniContext* jni, jobject player_info) noexcept; // sub_180047490
jobject network_player_info_get_player_team(events::JniContext* jni, jobject player_info) noexcept; // sub_1800474B0
int32_t java_object_hash_code(events::JniContext* jni, jobject obj) noexcept; // sub_1800474E0
jstring chat_component_get_formatted_text(events::JniContext* jni, jobject chat_component) noexcept; // sub_180047510
jobject chat_component_text_new(events::JniContext* jni, jstring text) noexcept; // sub_180047540
jobject chat_serializer_json_to_component(events::JniContext* jni, jstring json) noexcept; // sub_180047580
jobject inventory_get_stack_in_slot(events::JniContext* jni, jobject inventory, int32_t slot) noexcept; // sub_1800475C0
int32_t inventory_get_size(events::JniContext* jni, jobject inventory) noexcept; // sub_1800475F0
jobject container_player_get_craft_matrix(events::JniContext* jni, jobject container_player) noexcept; // sub_180047620
int32_t slot_get_number(events::JniContext* jni, jobject slot_obj) noexcept; // sub_180047640
jobject slot_get_stack(events::JniContext* jni, jobject slot_obj) noexcept; // sub_180047660
jobject as_inventory_basic(events::JniContext* jni, jobject obj) noexcept; // sub_180047690
jstring inventory_basic_get_title(events::JniContext* jni, jobject inv_basic) noexcept; // sub_1800476D0
jobject netty_channel_get_pipeline(events::JniContext* jni, jobject channel) noexcept; // sub_1800476F0
jobject netty_pipeline_add_before(events::JniContext* jni, jobject pipeline, const char* base_name, const char* name, jobject handler) noexcept; // sub_180047720
jobject netty_pipeline_get_by_class(events::JniContext* jni, jobject pipeline, jclass handler_cls) noexcept; // sub_1800477A0
jobject netty_pipeline_remove_by_class(events::JniContext* jni, jobject pipeline, jclass handler_cls) noexcept; // sub_1800477D0
jobject as_packet_c03_player(events::JniContext* jni, jobject obj) noexcept; // sub_180047800
bool packet_c03_is_moving(events::JniContext* jni, jobject packet) noexcept; // sub_180047840
bool packet_c03_is_on_ground(events::JniContext* jni, jobject packet) noexcept; // sub_180047870
double packet_c03_get_x(events::JniContext* jni, jobject packet) noexcept; // sub_1800478A0
double packet_c03_get_y(events::JniContext* jni, jobject packet) noexcept; // sub_1800478C0
double packet_c03_get_z(events::JniContext* jni, jobject packet) noexcept; // sub_1800478E0
math::Vec3d packet_c03_get_position(events::JniContext* jni, jobject packet) noexcept; // sub_180047900
jobject as_packet_c07_digging(events::JniContext* jni, jobject obj) noexcept; // sub_1800479A0
int32_t packet_c07_get_status(events::JniContext* jni, jobject packet) noexcept; // sub_1800479E0
jobject as_packet_c08_block_placement(events::JniContext* jni, jobject obj) noexcept; // sub_180047A30
jobject packet_c08_get_stack(events::JniContext* jni, jobject packet) noexcept; // sub_180047A70
jobject as_packet_c02_use_entity(events::JniContext* jni, jobject obj) noexcept; // sub_180047A90
jobject as_packet_c0b_entity_action(events::JniContext* jni, jobject obj) noexcept; // sub_180047AD0
int32_t packet_c0b_get_action(events::JniContext* jni, jobject packet) noexcept; // sub_180047B10
jobject as_packet_s12_entity_velocity(events::JniContext* jni, jobject obj) noexcept; // sub_180047B60
int32_t packet_s12_get_entity_id(events::JniContext* jni, jobject packet) noexcept; // sub_180047BA0
int32_t packet_s12_get_motion_x(events::JniContext* jni, jobject packet) noexcept; // sub_180047BC0
void packet_s12_set_motion_x(events::JniContext* jni, jobject packet, int32_t motion_x) noexcept; // sub_180047BE0
int32_t packet_s12_get_motion_y(events::JniContext* jni, jobject packet) noexcept; // sub_180047C10
void packet_s12_set_motion_y(events::JniContext* jni, jobject packet, int32_t motion_y) noexcept; // sub_180047C30
int32_t packet_s12_get_motion_z(events::JniContext* jni, jobject packet) noexcept; // sub_180047C60
void packet_s12_set_motion_z(events::JniContext* jni, jobject packet, int32_t motion_z) noexcept; // sub_180047C80
jobject as_packet_s14_entity(events::JniContext* jni, jobject obj) noexcept; // sub_180047CB0
int32_t packet_s14_get_entity_id(events::JniContext* jni, jobject packet) noexcept; // sub_180047CF0
int8_t packet_s14_get_pos_x(events::JniContext* jni, jobject packet) noexcept; // sub_180047D10
int8_t packet_s14_get_pos_y(events::JniContext* jni, jobject packet) noexcept; // sub_180047D30
int8_t packet_s14_get_pos_z(events::JniContext* jni, jobject packet) noexcept; // sub_180047D50
int8_t packet_s14_get_yaw(events::JniContext* jni, jobject packet) noexcept; // sub_180047D70
int8_t packet_s14_get_pitch(events::JniContext* jni, jobject packet) noexcept; // sub_180047D90
bool packet_s14_has_rotation(events::JniContext* jni, jobject packet) noexcept; // sub_180047DB0
jobject as_packet_s18_entity_teleport(events::JniContext* jni, jobject obj) noexcept; // sub_180047DE0
int32_t packet_s18_get_entity_id(events::JniContext* jni, jobject packet) noexcept; // sub_180047E20
int32_t packet_s18_get_pos_x(events::JniContext* jni, jobject packet) noexcept; // sub_180047E40
int32_t packet_s18_get_pos_y(events::JniContext* jni, jobject packet) noexcept; // sub_180047E60
int32_t packet_s18_get_pos_z(events::JniContext* jni, jobject packet) noexcept; // sub_180047E80
int8_t packet_s18_get_yaw(events::JniContext* jni, jobject packet) noexcept; // sub_180047EA0
int8_t packet_s18_get_pitch(events::JniContext* jni, jobject packet) noexcept; // sub_180047EC0
jobject as_packet_s19_entity_status(events::JniContext* jni, jobject obj) noexcept; // sub_180047EE0
int32_t packet_s19_get_entity_id(events::JniContext* jni, jobject packet) noexcept; // sub_180047F20
jobject as_packet_s29_sound_effect(events::JniContext* jni, jobject obj) noexcept; // sub_180047F40
jobject as_packet_s06_update_health(events::JniContext* jni, jobject obj) noexcept; // sub_180047F80
jobject as_packet_s08_player_pos_look(events::JniContext* jni, jobject obj) noexcept; // sub_180047FC0
jobject as_packet_s27_explosion(events::JniContext* jni, jobject obj) noexcept; // sub_180048000
float packet_s27_get_motion_x(events::JniContext* jni, jobject packet) noexcept; // sub_180048040
void packet_s27_set_motion_x(events::JniContext* jni, jobject packet, float motion_x) noexcept; // sub_180048060
float packet_s27_get_motion_y(events::JniContext* jni, jobject packet) noexcept; // sub_180048090
void packet_s27_set_motion_y(events::JniContext* jni, jobject packet, float motion_y) noexcept; // sub_1800480B0
float packet_s27_get_motion_z(events::JniContext* jni, jobject packet) noexcept; // sub_1800480E0
void packet_s27_set_motion_z(events::JniContext* jni, jobject packet, float motion_z) noexcept; // sub_180048100
jobject as_packet_s00_keep_alive(events::JniContext* jni, jobject obj) noexcept; // sub_180048130
jobject as_packet_c00_keep_alive(events::JniContext* jni, jobject obj) noexcept; // sub_180048170
jobject as_packet_s3f_custom_payload(events::JniContext* jni, jobject obj) noexcept; // sub_1800481B0
jstring packet_s3f_get_channel(events::JniContext* jni, jobject packet) noexcept; // sub_1800481F0
jbyteArray packet_s3f_get_payload_bytes(events::JniContext* jni, jobject packet) noexcept; // sub_180048210
int32_t packet_buffer_readable_bytes(events::JniContext* jni, jobject buf) noexcept; // sub_1800482D0
int32_t packet_buffer_reader_index(events::JniContext* jni, jobject buf) noexcept; // sub_180048300
jobject packet_buffer_get_bytes(events::JniContext* jni, jobject buf, int32_t index, jbyteArray dst) noexcept; // sub_180048330
jobject as_packet_c0e_click_window(events::JniContext* jni, jobject obj) noexcept; // sub_180048370
jobject as_packet_c16_client_status(events::JniContext* jni, jobject obj) noexcept; // sub_1800483B0
jobject c16_get_open_inventory_state(events::JniContext* jni) noexcept; // sub_1800483F0
jobject packet_c16_new(events::JniContext* jni, jobject enum_state) noexcept; // sub_180048420
int32_t packet_c16_get_status_ordinal(events::JniContext* jni, jobject packet) noexcept; // sub_180048460
jobject as_packet_c0d_close_window(events::JniContext* jni, jobject obj) noexcept; // sub_1800484A0
jobject packet_c0d_new(events::JniContext* jni, int32_t window_id) noexcept; // sub_1800484E0
jobject as_packet_c01_chat_message(events::JniContext* jni, jobject obj) noexcept; // sub_180048520
jstring packet_chat_get_message(events::JniContext* jni, jobject chat_packet) noexcept; // sub_180048560
void packet_chat_set_message(events::JniContext* jni, jobject chat_packet, jstring message) noexcept; // sub_180048580
jobject as_packet_c14_tab_complete(events::JniContext* jni, jobject obj) noexcept; // sub_1800485B0
jstring packet_tab_complete_get_message(events::JniContext* jni, jobject tab_packet) noexcept; // sub_1800485F0
jobject as_packet_s2a_particles(events::JniContext* jni, jobject obj) noexcept; // sub_180048610
int32_t enum_get_ordinal(events::JniContext* jni, jobject enum_obj) noexcept; // sub_180048650
jobject render_manager_get_instance_1_7(events::JniContext* jni) noexcept; // sub_180048680
float render_manager_get_player_view_x(events::JniContext* jni, jobject render_manager) noexcept; // sub_1800486B0
void render_manager_set_player_view_x(events::JniContext* jni, jobject render_manager, float view_x) noexcept; // sub_1800486D0
float render_manager_get_player_view_y(events::JniContext* jni, jobject render_manager) noexcept; // sub_180048700
void render_manager_set_player_view_y(events::JniContext* jni, jobject render_manager, float view_y) noexcept; // sub_180048720
jobject render_manager_get_font_renderer(events::JniContext* jni, jobject render_manager) noexcept; // sub_180048750
jobject as_score_player_team(events::JniContext* jni, jobject obj) noexcept; // sub_180048770
jstring score_player_team_get_color_prefix(events::JniContext* jni, jobject team) noexcept; // sub_1800487B0
void entity_renderer_get_mouse_over(events::JniContext* jni, jobject entity_renderer, float partial_ticks) noexcept; // sub_1800487D0
void entity_renderer_render_hand(events::JniContext* jni, jobject entity_renderer, float partial_ticks, int32_t pass) noexcept; // sub_180048810
void entity_renderer_orient_camera(events::JniContext* jni, jobject entity_renderer, float partial_ticks) noexcept; // sub_180048850
float entity_renderer_get_fov_modifier(events::JniContext* jni, jobject entity_renderer, float partial_ticks) noexcept; // sub_180048890
void framebuffer_bind(events::JniContext* jni, jobject framebuffer, bool set_viewport) noexcept; // sub_1800488D0
void render_global_set_entities_dirty(events::JniContext* jni, jobject render_global, bool dirty) noexcept; // sub_180048900
void active_render_info_update(events::JniContext* jni, jobject player, bool third_person_front) noexcept; // sub_180048930
bool renderer_living_can_render_name(events::JniContext* jni, jobject renderer, jobject entity) noexcept; // sub_180048970
void renderer_living_do_render_super(events::JniContext* jni, jobject renderer, jobject entity, double x, double y, double z, float entity_yaw, float partial_ticks) noexcept; // sub_1800489A0
int32_t font_renderer_get_string_width(events::JniContext* jni, jobject font_renderer, jstring text) noexcept; // sub_180048A10
int32_t font_renderer_draw_string(events::JniContext* jni, jobject font_renderer, jstring text, float x, float y, int32_t color, bool drop_shadow) noexcept; // sub_180048A40
void gl_state_enable_cull(events::JniContext* jni) noexcept; // sub_180048B20
void gl_state_disable_cull(events::JniContext* jni) noexcept; // sub_180048B50
void gl_state_enable_lighting(events::JniContext* jni) noexcept; // sub_180048B80
void gl_state_disable_lighting(events::JniContext* jni) noexcept; // sub_180048BB0
void gl_state_enable_depth(events::JniContext* jni) noexcept; // sub_180048BE0
void gl_state_disable_depth(events::JniContext* jni) noexcept; // sub_180048C10
void gl_state_enable_blend(events::JniContext* jni) noexcept; // sub_180048C40
void gl_state_disable_blend(events::JniContext* jni) noexcept; // sub_180048C70
void gl_state_enable_texture_2d(events::JniContext* jni) noexcept; // sub_180048CA0
void gl_state_disable_texture_2d(events::JniContext* jni) noexcept; // sub_180048CD0
void gl_state_enable_alpha(events::JniContext* jni) noexcept; // sub_180048D00
void gl_state_disable_alpha(events::JniContext* jni) noexcept; // sub_180048D30
void gl_state_blend_func(events::JniContext* jni, int32_t src_factor, int32_t dst_factor) noexcept; // sub_180048D60
void gl_state_color(events::JniContext* jni, float r, float g, float b, float a) noexcept; // sub_180048DA0
jobject potion_get_by_id(events::JniContext* jni, uint32_t potion_id) noexcept; // sub_180048E10
void potion_effect_set_duration(events::JniContext* jni, jobject potion_effect, int32_t duration) noexcept; // sub_180048E70
void lwjgl_display_update(events::JniContext* jni) noexcept; // sub_180048EA0
bool lwjgl_display_is_active(events::JniContext* jni) noexcept; // sub_180048ED0
int32_t lwjgl_display_get_width(events::JniContext* jni) noexcept; // sub_180048F10
int32_t lwjgl_display_get_height(events::JniContext* jni) noexcept; // sub_180048F40
int32_t lwjgl_display_get_framebuffer_width(events::JniContext* jni) noexcept; // sub_180048F70
int32_t lwjgl_display_get_framebuffer_height(events::JniContext* jni) noexcept; // sub_180048FA0
jobject render_item_new_1_7(events::JniContext* jni) noexcept; // sub_180048FD0
float render_item_get_z_level(events::JniContext* jni, jobject render_item) noexcept; // sub_180049000
void render_item_set_z_level(events::JniContext* jni, jobject render_item, float z_level) noexcept; // sub_180049020
void render_item_into_gui(events::JniContext* jni, jobject render_item, jobject font_renderer, jobject texture_manager, jobject stack, int32_t x, int32_t y) noexcept; // sub_180049050
void render_item_overlay_into_gui(events::JniContext* jni, jobject render_item, jobject font_renderer, jobject texture_manager, jobject stack, int32_t x, int32_t y, jstring text) noexcept; // sub_1800490C0
void render_helper_enable_item_lighting(events::JniContext* jni) noexcept; // sub_180049140
void render_helper_disable_item_lighting(events::JniContext* jni) noexcept; // sub_180049170
jobject as_entity_item(events::JniContext* jni, jobject obj) noexcept; // sub_1800491A0
jobject entity_item_get_stack(events::JniContext* jni, jobject entity_item) noexcept; // sub_1800491E0
void entity_item_search_nearby(events::JniContext* jni, jobject entity_item) noexcept; // sub_180049210
int64_t uuid_get_least_sig_bits(events::JniContext* jni, jobject uuid) noexcept; // sub_180049240
int64_t uuid_get_most_sig_bits(events::JniContext* jni, jobject uuid) noexcept; // sub_180049270
float controller_get_cur_block_damage(events::JniContext* jni, jobject controller) noexcept; // sub_1800492A0
void controller_set_cur_block_damage(events::JniContext* jni, jobject controller, float damage) noexcept; // sub_1800492C0
int32_t controller_get_block_hit_delay(events::JniContext* jni, jobject controller) noexcept; // sub_1800492E0
void controller_set_block_hit_delay(events::JniContext* jni, jobject controller, int32_t delay) noexcept; // sub_180049300
bool controller_is_hitting_block(events::JniContext* jni, jobject controller) noexcept; // sub_180049320
void controller_set_hitting_block(events::JniContext* jni, jobject controller, bool hitting) noexcept; // sub_180049340
void controller_update(events::JniContext* jni, jobject controller) noexcept; // sub_180049360
int32_t nbt_tag_list_count(events::JniContext* jni, jobject tag_list) noexcept; // sub_180049390
jobject nbt_tag_list_get_compound_at(events::JniContext* jni, jobject tag_list, int32_t index) noexcept; // sub_1800493C0
int16_t nbt_tag_compound_get_short(events::JniContext* jni, jobject compound, jstring key) noexcept; // sub_1800493F0
bool entity_arrow_is_in_ground(events::JniContext* jni, jobject arrow) noexcept; // sub_180049420
jstring stat_collector_translate_to_local(events::JniContext* jni, jstring key) noexcept; // sub_180049440

} // namespace minecraft

namespace mc {
using namespace ::slinky::minecraft;
} // namespace mc

} // namespace slinky
