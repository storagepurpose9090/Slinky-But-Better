#include "modules/visual/trajectories.hpp"

#include <cmath>
#include <glad/glad.h>

#include "config/settings.hpp"
#include "gui/widgets.hpp"
#include "minecraft/game_helpers.hpp"
#include "minecraft/mc_wrappers.hpp"
#include "util/containers.hpp"
#include "util/math.hpp"

namespace slinky::modules {

namespace {

constexpr int32_t kMaxTrajectorySteps = 100;

constexpr int32_t kItemBow        = 261;
constexpr int32_t kItemSnowball   = 332;
constexpr int32_t kItemEgg        = 344;
constexpr int32_t kItemEnderPearl = 368;

config::FloatSetting s_line_width{
    {}, "Line width", "%0.0f px", 1.0f, 0.0f, 1.0f, 4.0f, 1.0f, 0.0f
};
config::ColorSetting s_color_hit{"Hit color", {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 0.0f}};
config::ColorSetting s_color_miss{"Miss color", {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}};
config::BoolSetting s_render_throwables{{}, "Snowballs/eggs", true, false};
config::BoolSetting s_render_pearls{{}, "Enderpearls", true, false};
config::BoolSetting s_render_bow{{}, "Bow", true, false};
config::BoolSetting s_render_uncharged_bow{{}, "Bow (uncharged)", false, false};

math::Vec3d s_hit_entity_motion{};                     // xmmword_180321650
util::PodVector<jobject> s_candidate_entities{};       // xmmword_180321668
util::PodVector<math::AABB> s_candidate_aabbs{};       // xmmword_180321678
int32_t s_point_count = 0;                             // dword_180321688
bool s_is_hit = false;                                 // byte_18032168C
float s_points[kMaxTrajectorySteps][3]{};              // unk_180321690
bool s_has_landing_box = false;                        // byte_180321B40
math::AABB s_landing_box{};                            // xmmword_180321B48..0x321B77
float s_gravity = 0.0f;                                // dword_180321B78
float s_prev_speed = 0.0f;                             // dword_180321B7C
float s_cur_speed = 0.0f;                              // dword_180321B80

// Matches sub_180067500
void compute_initial_state(
    events::GameContext* ctx,
    math::Vec3d player_pos,
    math::Vec2f rotation,
    float partial_ticks,
    math::Vec3d* out_pos,
    math::Vec3d* out_motion) noexcept {
    constexpr double kDegToRad = 0.017453292519943295;
    const float yaw_rad = static_cast<float>(static_cast<double>(rotation.x) * kDegToRad);
    const float pitch_rad = static_cast<float>(static_cast<double>(rotation.y) * kDegToRad);

    const float cos_yaw = std::cos(yaw_rad);
    const float cos_pitch = std::cos(pitch_rad);
    const float sin_yaw = std::sin(yaw_rad);
    const float sin_pitch = std::sin(pitch_rad);

    const math::Vec3d dir{
        static_cast<double>(-sin_yaw * cos_pitch),
        static_cast<double>(-sin_pitch),
        static_cast<double>(cos_pitch * cos_yaw),
    };

    const float speed = math::lerpf(s_prev_speed, s_cur_speed, partial_ticks);
    *out_motion = math::vec3_scale(dir, static_cast<double>(speed));

    if (!minecraft::is_version_1_7_10(ctx->jni)) {
        player_pos = math::vec3_add_eye_height(player_pos);
        if (minecraft::entity_is_sneaking(ctx->jni, ctx->local_player)) {
            player_pos.y += -0.08;
        }
    }

    player_pos.x -= static_cast<double>(cos_yaw) * 0.16;
    player_pos.y += 0.1;
    player_pos.z -= static_cast<double>(sin_yaw) * 0.16;
    *out_pos = player_pos;
}

// Matches sub_1800663E0
void on_module_state_changed(void* raw_event) noexcept {
    const auto* ev = static_cast<const events::ModuleStateChangedEvent*>(raw_event);
    if (!ev->enabled && ev->module == get_trajectories_module()) {
        s_candidate_entities.clear_and_free();
        s_candidate_aabbs.clear_and_free();
    }
}

// Matches sub_180066440
void on_render_world_3d(void* raw_event) noexcept {
    if (s_point_count == 0) {
        return;
    }

    const auto* ev = static_cast<const events::RenderWorld3DEvent*>(raw_event);
    events::GameContext* ctx = ev->ctx;

    const math::Vec2f rotation = minecraft::entity_get_rotation(ctx->jni, ctx->local_player);
    const math::Vec3d pos = minecraft::entity_get_position(ctx->jni, ctx->local_player);
    const math::Vec3d prev_pos = minecraft::entity_get_prev_position(ctx->jni, ctx->local_player);
    const math::Vec3d interp_pos = math::vec3_lerp(
        prev_pos, pos, static_cast<double>(ev->partial_ticks));

    math::Vec3d sim_pos{};
    math::Vec3d sim_motion{};
    compute_initial_state(ctx, interp_pos, rotation, ev->partial_ticks, &sim_pos, &sim_motion);

    for (int32_t i = 0; i < s_point_count; ++i) {
        sim_pos = math::vec3_add(sim_pos, sim_motion);
        s_points[i][0] = static_cast<float>(sim_pos.x - ev->render_pos.x);
        s_points[i][1] = static_cast<float>(sim_pos.y - ev->render_pos.y);
        s_points[i][2] = static_cast<float>(sim_pos.z - ev->render_pos.z);

        sim_motion = math::vec3_scale(sim_motion, 0.99);
        sim_motion.y -= static_cast<double>(s_gravity);
    }

    glEnable(GL_BLEND);
    glLineWidth(s_line_width.value);

    const math::Color3f& color = s_is_hit ? s_color_hit.value : s_color_miss.value;
    glColor3f(color.r, color.g, color.b);

    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(3, GL_FLOAT, 0, s_points);
    glDrawArrays(GL_LINE_STRIP, 0, s_point_count);

    if (s_has_landing_box) {
        const math::Vec3d box_center = math::vec3_lerp(s_landing_box.min, s_landing_box.max, 0.5);
        const math::Vec3d half_extents = math::vec3_sub(s_landing_box.max, box_center);
        const math::Vec3d lag_offset = math::vec3_scale(
            s_hit_entity_motion, 1.0 - static_cast<double>(ev->partial_ticks));
        const math::Vec3d rel_center = math::vec3_sub(
            math::vec3_sub(box_center, ev->render_pos), lag_offset);

        glPushMatrix();
        glTranslatef(
            static_cast<float>(rel_center.x),
            static_cast<float>(rel_center.y),
            static_cast<float>(rel_center.z));
        glScalef(
            static_cast<float>(half_extents.x),
            static_cast<float>(half_extents.y),
            static_cast<float>(half_extents.z));
        minecraft::gl_bind_unit_cube_vertices();
        minecraft::gl_draw_unit_cube_outline();
        glPopMatrix();
    }

    minecraft::gl_unbind_vertex_array();
}

// Matches sub_180066910
void on_post_living_update(void* raw_event) noexcept {
    events::GameContext* ctx = static_cast<events::ContextEvent*>(raw_event)->ctx;
    if (!ctx->in_game_focus || minecraft::is_rotation_spoof_active()) {
        s_point_count = 0;
        return;
    }

    jobject settings = minecraft::mc_get_game_settings(ctx->jni, ctx->minecraft);
    if (minecraft::settings_get_third_person_view(ctx->jni, settings) != 0) {
        s_point_count = 0;
        return;
    }

    jobject held_item = minecraft::player_get_held_item(ctx->jni, ctx->local_player);
    if (held_item == nullptr) {
        s_point_count = 0;
        return;
    }

    const int32_t item_id = minecraft::item_get_id(ctx->jni, held_item);
    bool hit_on_block = false;
    bool hit_on_entity = false;

    if (item_id == kItemEnderPearl) {
        if (!s_render_pearls.value) {
            s_point_count = 0;
            return;
        }
        s_prev_speed = 1.5f;
        s_cur_speed = 1.5f;
        s_gravity = 0.03f;
        hit_on_block = true;
        hit_on_entity = false;
    } else if (item_id == kItemSnowball || item_id == kItemEgg) {
        if (!s_render_throwables.value) {
            s_point_count = 0;
            return;
        }
        s_prev_speed = 1.5f;
        s_cur_speed = 1.5f;
        s_gravity = 0.03f;
        hit_on_block = false;
        hit_on_entity = true;
    } else if (item_id == kItemBow) {
        if (!s_render_bow.value) {
            s_point_count = 0;
            return;
        }
        s_gravity = 0.05f;
        hit_on_block = false;
        hit_on_entity = true;

        if (minecraft::player_is_using_item(ctx->jni, ctx->local_player)) {
            const int32_t use_count = minecraft::player_get_item_in_use_count(ctx->jni, ctx->local_player);
            float charge = static_cast<float>(72000 - use_count) / 20.0f;
            charge = (charge * charge + charge * 2.0f) / 3.0f;
            if (static_cast<double>(charge) < 0.1) {
                s_cur_speed = 0.0f;
                s_point_count = 0;
                return;
            }

            s_cur_speed = static_cast<float>(static_cast<double>(std::fmin(1.0f, charge)) * 1.5 * 2.0);
            if (s_cur_speed == 0.0f) {
                s_point_count = 0;
                return;
            }

            float prev_charge = static_cast<float>(71999 - use_count) / 20.0f;
            prev_charge = (prev_charge * prev_charge + prev_charge * 2.0f) / 3.0f;
            s_prev_speed = (static_cast<double>(prev_charge) < 0.1)
                ? 0.0f
                : static_cast<float>(static_cast<double>(std::fmin(1.0f, prev_charge)) * 1.5 * 2.0);
        } else {
            if (!s_render_uncharged_bow.value) {
                s_point_count = 0;
                return;
            }
            s_prev_speed = 3.0f;
            s_cur_speed = 3.0f;
        }
    } else {
        s_point_count = 0;
        return;
    }

    const math::Vec2f rotation = minecraft::entity_get_rotation(ctx->jni, ctx->local_player);
    const math::Vec3d local_pos = minecraft::entity_get_position(ctx->jni, ctx->local_player);

    math::Vec3d sim_pos{};
    math::Vec3d sim_motion{};
    compute_initial_state(ctx, local_pos, rotation, 0.0f, &sim_pos, &sim_motion);

    s_candidate_entities.count = 0;
    s_candidate_aabbs.count = 0;

    jobject entity_list = minecraft::world_get_loaded_entity_list(ctx->jni, ctx->world);
    const math::AABB sweep_box = math::aabb_from_points(
        {sim_pos.x, -100.0, sim_pos.z},
        {sim_pos.x + sim_motion.x * 1000.0, sim_pos.y + 200.0, sim_pos.z + sim_motion.z * 1000.0});

    for (int32_t i = 0; i < minecraft::java_list_size(ctx->jni, entity_list); ++i) {
        jobject entity = minecraft::java_list_get(ctx->jni, entity_list, i);
        if (ctx->jni->env->IsSameObject(entity, ctx->local_player)) {
            continue;
        }
        if (!minecraft::entity_can_be_collided_with(ctx->jni, entity)) {
            continue;
        }

        jobject raw_bbox = minecraft::entity_get_bounding_box(ctx->jni, entity);
        const math::AABB expanded = math::aabb_expand(
            minecraft::aabb_to_aabb(ctx->jni, raw_bbox), 0.3);
        if (math::aabb_intersects(expanded, sweep_box)) {
            s_candidate_entities.push_back(entity);
            s_candidate_aabbs.push_back(expanded);
        }
    }

    for (int32_t step = 0; step < kMaxTrajectorySteps; ++step) {
        const math::Vec3d cur_pos = sim_pos;
        const math::Vec3d next_pos = math::vec3_add(cur_pos, sim_motion);
        sim_pos = next_pos;

        jobject start_vec = minecraft::vec3_new(ctx->jni, cur_pos);
        jobject end_vec = minecraft::vec3_new(ctx->jni, next_pos);
        jobject mop = minecraft::world_ray_trace_blocks(
            ctx->jni, ctx->world, start_vec, end_vec, false, true, false);

        math::Vec3d ray_delta = sim_motion;
        if (mop != nullptr) {
            s_point_count = step + 1;
            s_has_landing_box = true;
            s_is_hit = hit_on_block;

            const math::Vec3d hit_vec = minecraft::vec3_to_vec3d(
                ctx->jni, minecraft::mop_get_hit_vec(ctx->jni, mop));
            sim_pos = hit_vec;
            ray_delta = math::vec3_sub(hit_vec, cur_pos);

            const math::Vec3d block_pos = minecraft::mop_get_block_pos_vec3d(ctx->jni, mop);
            s_landing_box = math::make_aabb(
                block_pos, math::vec3_add(block_pos, {1.0, 1.0, 1.0}));
            s_hit_entity_motion = {0.0, 0.0, 0.0};
        }

        const math::Vec3d inv_dir = math::vec3_rcp(ray_delta);
        for (int k = 0; k < s_candidate_aabbs.count; ++k) {
            if (math::aabb_ray_intersects_rcp(s_candidate_aabbs.data[k], cur_pos, inv_dir)) {
                s_point_count = step + 1;
                s_has_landing_box = true;
                s_is_hit = hit_on_entity;
                s_landing_box = s_candidate_aabbs.data[k];

                jobject hit_entity = s_candidate_entities.data[k];
                s_hit_entity_motion = math::vec3_sub(
                    minecraft::entity_get_position(ctx->jni, hit_entity),
                    minecraft::entity_get_prev_position(ctx->jni, hit_entity));
                return;
            }
        }

        if (mop != nullptr) {
            return;
        }

        sim_motion = math::vec3_scale(sim_motion, 0.99);
        sim_motion.y -= static_cast<double>(s_gravity);
    }

    s_point_count = kMaxTrajectorySteps;
    s_has_landing_box = false;
    s_is_hit = false;
}

void render_settings(gui::ClickGui* gui, gui::GuiRenderer* renderer) noexcept {
    gui::widgets::render_section_header(gui, renderer, "Main");
    s_line_width.render(gui, renderer);
    s_color_hit.render(gui, renderer);
    s_color_miss.render(gui, renderer);
    gui::widgets::render_section_header(gui, renderer, "Render when holding");
    s_render_throwables.render(gui, renderer);
    s_render_pearls.render(gui, renderer);
    s_render_bow.render(gui, renderer);
    if (s_render_bow.value) {
        s_render_uncharged_bow.render(gui, renderer);
    }
}

void visit_config(config::ConfigArchive* ar) noexcept {
    s_line_width.visit(ar, "line_width");
    s_color_hit.visit(ar, "color_hit");
    s_color_miss.visit(ar, "color_miss");
    s_render_throwables.visit(ar, "render_throwables");
    s_render_pearls.visit(ar, "render_pearls");
    s_render_bow.visit(ar, "render_bow");
    s_render_uncharged_bow.visit(ar, "render_uncharged_bow");
}

events::Module create_module() noexcept {
    events::Module mod{};
    mod.name = "Trajectories";
    mod.description = "Predicts the trajectory of the item in your hand.";
    mod.category = events::ModuleCategory::Visual;
    mod.events[events::kEventModuleStateChanged] = {&on_module_state_changed, 0};
    mod.events[events::kEventRenderWorld3D] = {&on_render_world_3d, -1000};
    mod.events[events::kEventPostLivingUpdate] = {&on_post_living_update, 0};
    mod.render_settings = &render_settings;
    mod.visit_config = &visit_config;
    return mod;
}

} // namespace

events::Module* get_trajectories_module() noexcept {
    static events::Module s_module = create_module();
    return &s_module;
}

} // namespace slinky::modules
