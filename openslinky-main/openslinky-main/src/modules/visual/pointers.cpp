#include "modules/visual/pointers.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <glad/glad.h>

#include "config/settings.hpp"
#include "gui/widgets.hpp"
#include "minecraft/game_helpers.hpp"
#include "minecraft/mc_wrappers.hpp"
#include "util/math.hpp"

namespace slinky::modules {

namespace {

constexpr size_t kMaxCachedPointers = 100;

constexpr const char* kStyleOptions[2] = {"2D", "3D"};
constexpr const char* kColorModeOptions[3] = {"Distance", "Name tag", "Manual"};

// 56-byte cached pointer entry (@ 0x18031DA90)
struct CachedPointer {
    double pos_x = 0.0;       // +0x00
    double pos_z = 0.0;       // +0x08
    double prev_pos_x = 0.0;  // +0x10
    double prev_pos_z = 0.0;  // +0x18
    math::Color3f color{};    // +0x20
    float prev_dist = 0.0f;   // +0x2C
    float dist = 0.0f;        // +0x30
    uint32_t pad = 0;         // +0x34
};
static_assert(sizeof(CachedPointer) == 0x38, "CachedPointer must be 56 bytes");

config::FloatSetting s_range{
    {}, "Range", "%0.0f blocks", 0.0f, 0.0f, 10.0f, 1000.0f, 100.0f, 0.0f
};
config::FloatSetting s_ignore_fov{
    {}, "Ignore within FOV", "%0.0f\xC2\xB0", 1.0f, 2.0f, 0.0f, 180.0f, 0.0f, 0.0f
};
config::BoolSetting s_hide_friendlies{{}, "Hide friendlies", false, false};
config::ModeSetting s_style{
    {}, "Style", 2, 0, kStyleOptions, 0, 0, false
};
config::ModeSetting s_color_mode{
    {}, "Colors", 3, 0, kColorModeOptions, 0, 0, false
};
config::RangeSetting s_near_far_range{
    {}, "Transition between", "%0.0f blocks", "%0.0f - %0.0f blocks",
    0.0f, 0.0f, 0.0f, 100.0f, {5.0f, 30.0f}, {0.0f, 0.0f}
};
config::ColorSetting s_color_near{"Near color", {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}};
config::ColorSetting s_color_far{"Far color", {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 0.0f}};
config::ColorSetting s_color_enemies{"Enemy color", {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}};
config::ColorSetting s_color_friendlies{"Friendly color", {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 0.0f}};
config::FloatSetting s_scale{
    {}, "Scale", "%.0f%%", 0.0f, 100.0f, 0.75f, 2.0f, 1.0f, 0.0f
};
config::FloatSetting s_radius{
    {}, "Radius", "%.0f", 1.0f, 0.0f, 5.0f, 20.0f, 10.0f, 0.0f
};
config::BoolSetting s_scale_radius{{}, "Distance-based radius", false, false};

size_t s_pointer_count = 0;
CachedPointer s_pointers[kMaxCachedPointers]{};

// Matches sub_18005F970
int compare_pointers_by_dist(const void* a, const void* b) noexcept {
    const auto* pa = static_cast<const CachedPointer*>(a);
    const auto* pb = static_cast<const CachedPointer*>(b);
    return static_cast<int>(pb->dist - pa->dist);
}

// Matches sub_18005EE80
void on_render_world_2d(void* raw_event) noexcept {
    if (s_pointer_count == 0) {
        return;
    }

    const auto* ev = static_cast<const events::RenderWorld2DProjEvent*>(raw_event);
    events::GameContext* ctx = ev->ctx;

    jobject render_manager = minecraft::mc_get_render_manager(ctx->jni, ctx->minecraft);
    const float view_x = minecraft::render_manager_get_player_view_x(ctx->jni, render_manager);
    const float view_y = minecraft::render_manager_get_player_view_y(ctx->jni, render_manager);

    minecraft::gl_bind_chevron_vertices();
    const math::Vec2f center = math::vec2_scale(ev->viewport_size, 0.5f);

    glPushMatrix();
    if (s_style.value != 0) {
        const float pitch = std::fmax(30.0f, view_x);
        glTranslatef(center.x, center.y, 0.0f);
        glRotatef(90.0f - pitch, 1.0f, 0.0f, 0.0f);
    } else {
        glTranslatef(center.x, center.y, 0.0f);
    }

    glLineWidth(1.0f);
    glEnable(GL_LINE_SMOOTH);

    float range_span = s_near_far_range.value.max - s_near_far_range.value.min;
    if (range_span == 0.0f) {
        range_span = 1.0e-5f;
    }

    for (size_t i = 0; i < s_pointer_count; ++i) {
        const CachedPointer& entry = s_pointers[i];
        const double lx = static_cast<double>(math::lerpf(
            static_cast<float>(entry.prev_pos_x),
            static_cast<float>(entry.pos_x),
            ev->partial_ticks));
        const double lz = static_cast<double>(math::lerpf(
            static_cast<float>(entry.prev_pos_z),
            static_cast<float>(entry.pos_z),
            ev->partial_ticks));

        const double angle_rad = std::atan2(lz - ev->render_pos.z, lx - ev->render_pos.x);
        const float angle_deg = math::wrap_degrees_180(
            static_cast<float>(angle_rad * 180.0 / math::kPi - 90.0) - view_y);

        glPushMatrix();
        glRotatef(angle_deg, 0.0f, 0.0f, 1.0f);

        const float scale = s_scale.value * 10.0f;
        glScalef(scale, scale, scale);

        const float interp_dist = math::lerpf(entry.prev_dist, entry.dist, ev->partial_ticks);
        const float t = math::clampf(
            0.0f, 1.0f, (interp_dist - s_near_far_range.value.min) / range_span);

        float radius = s_radius.value;
        if (s_scale_radius.value) {
            radius *= (t + 1.0f);
        }
        glTranslatef(0.0f, -radius, 0.0f);

        float r = entry.color.r;
        float g = entry.color.g;
        float b = entry.color.b;
        if (s_color_mode.value == 0) {
            r = math::lerpf(s_color_near.value.r, s_color_far.value.r, t);
            g = math::lerpf(s_color_near.value.g, s_color_far.value.g, t);
            b = math::lerpf(s_color_near.value.b, s_color_far.value.b, t);
        }

        glColor3f(r, g, b);
        minecraft::gl_draw_chevron_filled();
        glColor3f(0.0f, 0.0f, 0.0f);
        minecraft::gl_draw_chevron_outline();
        glPopMatrix();
    }

    glPopMatrix();
    minecraft::gl_unbind_vertex_array();
}

// Matches sub_18005F310
void on_post_living_update(void* raw_event) noexcept {
    events::GameContext* ctx = static_cast<events::ContextEvent*>(raw_event)->ctx;
    if (ctx->world == nullptr) {
        return;
    }

    s_pointer_count = 0;
    const math::Vec3d local_pos = minecraft::entity_get_position(ctx->jni, ctx->local_player);
    const math::Vec3d local_prev_pos = minecraft::entity_get_prev_position(ctx->jni, ctx->local_player);
    jobject render_manager = minecraft::mc_get_render_manager(ctx->jni, ctx->minecraft);
    const float view_y = minecraft::render_manager_get_player_view_y(ctx->jni, render_manager);

    jobject player_list = minecraft::world_get_player_entities(ctx->jni, ctx->world);
    const uint32_t exclude_mask = s_hide_friendlies.value
        ? minecraft::kPlayerTagExcludedMask
        : minecraft::kPlayerTagBot;

    for (int32_t i = 0; i < minecraft::java_list_size(ctx->jni, player_list); ++i) {
        jobject entity = minecraft::java_list_get(ctx->jni, player_list, i);
        if (ctx->jni->env->IsSameObject(ctx->local_player, entity)) {
            continue;
        }
        if (minecraft::entity_is_dead(ctx->jni, entity)) {
            continue;
        }

        const uint16_t tags = minecraft::player_get_tag_flags(ctx->jni, entity);
        if ((tags & exclude_mask) != 0) {
            continue;
        }

        const math::Vec3d pos = minecraft::entity_get_position(ctx->jni, entity);
        const double dist_sq = math::vec3_dist_sq(pos, local_pos);
        if (dist_sq > static_cast<double>(s_range.value * s_range.value)) {
            continue;
        }

        const double angle_rad = std::atan2(pos.z - local_pos.z, pos.x - local_pos.x);
        const float yaw_diff = std::fabs(math::wrap_degrees_180(
            static_cast<float>(angle_rad * 180.0 / math::kPi - 90.0) - view_y));
        if (s_ignore_fov.value > yaw_diff) {
            continue;
        }

        const math::Vec3d prev_pos = minecraft::entity_get_prev_position(ctx->jni, entity);
        CachedPointer& dst = s_pointers[s_pointer_count++];
        dst.pos_x = pos.x;
        dst.pos_z = pos.z;
        dst.prev_pos_x = prev_pos.x;
        dst.prev_pos_z = prev_pos.z;
        dst.dist = std::sqrt(static_cast<float>(dist_sq));
        dst.prev_dist = static_cast<float>(math::vec3_dist(local_prev_pos, prev_pos));

        if (s_color_mode.value == 1) {
            const uint16_t code = minecraft::entity_get_team_color_code(ctx->jni, entity);
            dst.color = minecraft::chat_color_code_to_rgb(code);
        } else if (s_color_mode.value == 2) {
            if ((tags & (minecraft::kPlayerTagFriend | minecraft::kPlayerTagTeammate)) != 0) {
                dst.color = s_color_friendlies.value;
            } else {
                dst.color = s_color_enemies.value;
            }
        }

        if (s_pointer_count == kMaxCachedPointers) {
            break;
        }
    }

    std::qsort(s_pointers, s_pointer_count, sizeof(CachedPointer), &compare_pointers_by_dist);
}

void render_settings(gui::ClickGui* gui, gui::GuiRenderer* renderer) noexcept {
    gui::widgets::render_section_header(gui, renderer, "Main");
    s_range.render(gui, renderer);
    s_ignore_fov.render(gui, renderer);
    s_hide_friendlies.render(gui, renderer);
    gui::widgets::render_section_header(gui, renderer, "Customization");
    s_style.render(gui, renderer);
    s_color_mode.render(gui, renderer);
    s_near_far_range.render(gui, renderer);
    if (s_color_mode.value == 0) {
        s_color_near.render(gui, renderer);
        s_color_far.render(gui, renderer);
    } else if (s_color_mode.value == 2) {
        s_color_enemies.render(gui, renderer);
        if (!s_hide_friendlies.value) {
            s_color_friendlies.render(gui, renderer);
        }
    }
    s_scale.render(gui, renderer);
    s_radius.render(gui, renderer);
    s_scale_radius.render(gui, renderer);
}

void visit_config(config::ConfigArchive* ar) noexcept {
    s_range.visit(ar, "range");
    s_ignore_fov.visit(ar, "ignore_fov");
    s_hide_friendlies.visit(ar, "hide_friendlies");
    s_style.visit(ar, "style");
    s_color_mode.visit(ar, "color_mode");
    s_near_far_range.visit(ar, "near_far_range");
    s_color_near.visit(ar, "color_near");
    s_color_far.visit(ar, "color_far");
    s_color_enemies.visit(ar, "color_enemies");
    s_color_friendlies.visit(ar, "color_friendlies");
    s_scale.visit(ar, "scale");
    s_radius.visit(ar, "radius");
    s_scale_radius.visit(ar, "scale_radius");
}

int format_hud_tag(char* out_buf) noexcept {
    return std::snprintf(out_buf, 32, "%s", kStyleOptions[s_style.value]);
}

events::Module create_module() noexcept {
    events::Module mod{};
    mod.name = "Pointers";
    mod.description = "Points to other players.";
    mod.category = events::ModuleCategory::Visual;
    mod.events[events::kEventRenderWorld2DProj] = {&on_render_world_2d, -200};
    mod.events[events::kEventPostLivingUpdate] = {&on_post_living_update, 0};
    mod.render_settings = &render_settings;
    mod.visit_config = &visit_config;
    mod.format_hud_tag = &format_hud_tag;
    return mod;
}

} // namespace

events::Module* get_pointers_module() noexcept {
    static events::Module s_module = create_module();
    return &s_module;
}

} // namespace slinky::modules
