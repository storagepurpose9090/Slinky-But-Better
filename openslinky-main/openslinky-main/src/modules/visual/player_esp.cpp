#include "modules/visual/player_esp.hpp"

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

constexpr size_t kMaxCachedPlayers = 100;

constexpr const char* kStyleOptions[2] = {"2D", "3D"};
constexpr const char* kColorModeOptions[2] = {"Automatic", "Manual"};

// Matches 8 corner offsets @ 0x18016D020
constexpr math::Vec3d kPlayerBoxCorners[8] = {
    {-0.4, 0.0, -0.4},
    {-0.4, 0.0,  0.4},
    { 0.4, 0.0, -0.4},
    { 0.4, 0.0,  0.4},
    {-0.4, 2.0, -0.4},
    {-0.4, 2.0,  0.4},
    { 0.4, 2.0, -0.4},
    { 0.4, 2.0,  0.4},
};

// 80-byte cached player entry (@ 0x180315010)
struct CachedPlayer {
    math::Vec3d prev_pos{};      // +0x00
    math::Vec3d pos{};           // +0x18
    math::Color3f color{};       // +0x30
    float dist_sq = 0.0f;        // +0x3C
    int32_t hurt_time = 0;       // +0x40
    float health_frac = 0.0f;    // +0x44
    float absorption_frac = 0.0f;// +0x48
    bool is_target = false;      // +0x4C
    uint8_t pad[3]{};            // +0x4D
};
static_assert(sizeof(CachedPlayer) == 0x50, "CachedPlayer must be 80 bytes");

config::ModeSetting s_mode{
    {}, "Style", 2, 0, kStyleOptions, 0, 0, false
};
config::FloatSetting s_range{
    {}, "Range", "%0.0f blocks", 0.0f, 0.0f, 5.0f, 300.0f, 150.0f, 0.0f
};
config::BoolSetting s_hide_friendlies{{}, "Hide friendlies", false, false};
config::ModeSetting s_color_mode{
    {}, "Colors", 2, 0, kColorModeOptions, 0, 0, false
};
config::ColorSetting s_color_enemies{"Enemy color", {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}};
config::ColorSetting s_color_friendlies{"Friendly color", {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 0.0f}};
config::BoolSetting s_override_target_color{{}, "Override target color", false, false};
config::ColorSetting s_color_target{"Target color", {1.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 0.0f}};
config::FloatSetting s_line_width{
    {}, "Line width", "%0.0f px", 1.0f, 0.0f, 1.0f, 4.0f, 1.0f, 0.0f
};
config::BoolSetting s_hurt_effect{{}, "Hurt effect", false, false};
config::BoolSetting s_filled{{}, "Filled", true, false};
config::BoolSetting s_show_2d_box{{}, "Show box", true, false};
config::BoolSetting s_show_health_bar{{}, "Show health bar", true, false};

size_t s_player_count = 0;
CachedPlayer s_players[kMaxCachedPlayers]{};

// Matches sub_180051240
int compare_players_by_dist(const void* a, const void* b) noexcept {
    const auto* pa = static_cast<const CachedPlayer*>(a);
    const auto* pb = static_cast<const CachedPlayer*>(b);
    return static_cast<int>(pb->dist_sq - pa->dist_sq);
}

// Matches sub_1800510D0
void draw_health_bar(const CachedPlayer& entry, float x, float y_top, float y_bottom) noexcept {
    float y_hp = math::lerpf(y_bottom, y_top, entry.health_frac);

    glColor3f(0.0f, 0.0f, 0.0f);
    glBegin(GL_LINES);
    glVertex2f(x, y_top);
    glVertex2f(x, y_hp);
    glEnd();

    if (entry.absorption_frac > 0.0f) {
        const float y_abs = math::lerpf(y_hp, y_bottom, entry.absorption_frac);
        glColor3f(1.0f, 0.67f, 0.0f);
        glBegin(GL_LINES);
        glVertex2f(x, y_hp);
        glVertex2f(x, y_abs);
        glEnd();
        y_hp = y_abs;
    }

    const float r = math::lerpf(1.0f, 0.0f, entry.health_frac);
    const float g = math::lerpf(0.0f, 1.0f, entry.health_frac);
    glColor3f(r, g, 0.0f);
    glBegin(GL_LINES);
    glVertex2f(x, y_hp);
    glVertex2f(x, y_bottom);
    glEnd();
}

// Matches sub_1800501B0
void on_render_world_3d(void* raw_event) noexcept {
    if (s_player_count == 0 || s_mode.value != 1) {
        return;
    }

    const auto* ev = static_cast<const events::RenderWorld3DEvent*>(raw_event);
    events::GameContext* ctx = ev->ctx;

    glEnable(GL_BLEND);
    glLineWidth(s_line_width.value);

    jobject render_manager = minecraft::mc_get_render_manager(ctx->jni, ctx->minecraft);
    const float neg_view_y = -minecraft::render_manager_get_player_view_y(ctx->jni, render_manager);
    if (s_player_count == 0) {
        return;
    }

    const double partial = static_cast<double>(ev->partial_ticks);
    for (size_t i = 0; i < s_player_count; ++i) {
        const CachedPlayer& entry = s_players[i];
        const math::Vec3d interp_pos = math::vec3_lerp(entry.prev_pos, entry.pos, partial);
        const math::Vec3d rel_pos = math::vec3_sub(interp_pos, ev->render_pos);

        glPushMatrix();
        glTranslatef(
            static_cast<float>(rel_pos.x),
            static_cast<float>(rel_pos.y + 1.0),
            static_cast<float>(rel_pos.z));
        glScalef(0.4f, 1.0f, 0.4f);

        if (s_show_2d_box.value || entry.is_target) {
            minecraft::gl_bind_unit_cube_vertices();

            float r = entry.color.r;
            float g = entry.color.g;
            float b = entry.color.b;
            if (entry.hurt_time > 0 && s_hurt_effect.value) {
                const float hurt_t = std::fmin(10.0f, static_cast<float>(entry.hurt_time) - ev->partial_ticks) * 0.1f;
                r = math::lerpf(r, 1.0f, hurt_t);
                g = math::lerpf(g, 0.0f, hurt_t);
                b = math::lerpf(b, 0.0f, hurt_t);
            }

            if (s_filled.value) {
                glColor4f(r, g, b, 0.2f);
                minecraft::gl_draw_unit_cube_filled();
            }

            glColor3f(r, g, b);
            minecraft::gl_draw_unit_cube_outline();
            minecraft::gl_unbind_vertex_array();
        }

        if (s_show_health_bar.value) {
            glRotatef(neg_view_y, 0.0f, 1.0f, 0.0f);
            glColor4f(0.0f, 1.0f, 0.0f, 0.75f);
            draw_health_bar(entry, 2.0f, 1.0f, -1.0f);
        }

        glPopMatrix();
    }
}

// Matches sub_180050530
void on_render_world_2d(void* raw_event) noexcept {
    if (s_player_count == 0 || s_mode.value != 0) {
        return;
    }

    const auto* ev = static_cast<const events::RenderWorld2DProjEvent*>(raw_event);

    glEnable(GL_BLEND);
    glLineWidth(s_line_width.value);
    if (s_player_count == 0) {
        return;
    }

    const double partial = static_cast<double>(ev->partial_ticks);
    for (size_t i = 0; i < s_player_count; ++i) {
        const CachedPlayer& entry = s_players[i];
        const math::Vec3d interp_pos = math::vec3_lerp(entry.prev_pos, entry.pos, partial);
        const math::Vec3d rel_pos = math::vec3_sub(interp_pos, ev->render_pos);

        math::Vec2f screen_pt{};
        if (!minecraft::project_world_to_screen(ev, math::vec3_add(rel_pos, kPlayerBoxCorners[0]), &screen_pt)) {
            continue;
        }

        float min_x = screen_pt.x;
        float max_x = screen_pt.x;
        float min_y = screen_pt.y;
        float max_y = screen_pt.y;

        bool all_projected = true;
        for (size_t c = 1; c < 8; ++c) {
            if (!minecraft::project_world_to_screen(ev, math::vec3_add(rel_pos, kPlayerBoxCorners[c]), &screen_pt)) {
                all_projected = false;
                break;
            }
            if (screen_pt.x < min_x) {
                min_x = screen_pt.x;
            } else if (screen_pt.x > max_x) {
                max_x = screen_pt.x;
            }
            if (screen_pt.y < min_y) {
                min_y = screen_pt.y;
            } else if (screen_pt.y > max_y) {
                max_y = screen_pt.y;
            }
        }
        if (!all_projected) {
            continue;
        }

        const bool min_in_view = (min_x >= 0.0f && min_y >= 0.0f &&
                                  min_x <= ev->viewport_size.x && min_y <= ev->viewport_size.y);
        const bool max_in_view = (max_x >= 0.0f && max_y >= 0.0f &&
                                  max_x <= ev->viewport_size.x && max_y <= ev->viewport_size.y);
        if (!min_in_view && !max_in_view) {
            continue;
        }

        float r = entry.color.r;
        float g = entry.color.g;
        float b = entry.color.b;
        if (entry.hurt_time > 0 && s_hurt_effect.value) {
            const float hurt_t = std::fmin(10.0f, static_cast<float>(entry.hurt_time) - ev->partial_ticks) * 0.1f;
            r = math::lerpf(r, 1.0f, hurt_t);
            g = math::lerpf(g, 0.0f, hurt_t);
            b = math::lerpf(b, 0.0f, hurt_t);
        }

        if (s_show_2d_box.value || entry.is_target) {
            minecraft::gl_bind_unit_quad_vertices();
            glPushMatrix();
            glTranslatef(min_x, min_y, 0.0f);
            glScalef(max_x - min_x, max_y - min_y, 1.0f);

            if (s_filled.value) {
                glColor4f(r, g, b, 0.25f);
                minecraft::gl_draw_unit_quad_filled();
            }

            glColor3f(r, g, b);
            minecraft::gl_draw_unit_quad_outline();
            glPopMatrix();
            minecraft::gl_unbind_vertex_array();
        }

        if (s_show_health_bar.value) {
            const float bar_x = min_x - s_line_width.value - 1.0f - 4.0f;
            draw_health_bar(entry, bar_x, min_y, max_y);
        }
    }
}

// Matches sub_180050A80
void on_post_living_update(void* raw_event) noexcept {
    events::GameContext* ctx = static_cast<events::ContextEvent*>(raw_event)->ctx;
    if (ctx->world == nullptr) {
        return;
    }

    s_player_count = 0;
    const math::Vec3d local_pos = minecraft::entity_get_position(ctx->jni, ctx->local_player);
    jobject player_list = minecraft::world_get_player_entities(ctx->jni, ctx->world);

    const uint32_t exclude_mask = s_hide_friendlies.value
        ? minecraft::kPlayerTagExcludedMask
        : minecraft::kPlayerTagBot;

    for (int32_t i = 0; i < minecraft::java_list_size(ctx->jni, player_list); ++i) {
        jobject entity = minecraft::java_list_get(ctx->jni, player_list, i);
        if (ctx->jni->env->IsSameObject(ctx->local_player, entity)) {
            continue;
        }

        const uint16_t tags = minecraft::player_get_tag_flags(ctx->jni, entity);
        if ((tags & exclude_mask) != 0) {
            continue;
        }
        if (minecraft::entity_is_dead(ctx->jni, entity)) {
            continue;
        }

        const math::Vec3d pos = minecraft::entity_get_position(ctx->jni, entity);
        const double dist_sq = math::vec3_dist_sq(pos, local_pos);
        if (dist_sq > static_cast<double>(s_range.value * s_range.value)) {
            continue;
        }

        bool is_target = false;
        math::Color3f color{};
        if (s_override_target_color.value &&
            ctx->jni->env->IsSameObject(entity, minecraft::get_aimassist_target_entity())) {
            color = s_color_target.value;
            is_target = true;
        } else if (s_color_mode.value == 0) {
            const uint16_t code = minecraft::entity_get_team_color_code(ctx->jni, entity);
            color = minecraft::chat_color_code_to_rgb(code);
        } else if ((tags & (minecraft::kPlayerTagFriend | minecraft::kPlayerTagTeammate)) != 0) {
            color = s_color_friendlies.value;
        } else {
            color = s_color_enemies.value;
        }

        CachedPlayer& dst = s_players[s_player_count++];
        dst.pos = pos;
        dst.prev_pos = minecraft::entity_get_prev_position(ctx->jni, entity);
        dst.color = color;
        dst.dist_sq = static_cast<float>(dist_sq);
        dst.hurt_time = minecraft::living_get_hurt_time(ctx->jni, entity);
        dst.is_target = is_target;

        const float health = minecraft::living_get_health(ctx->jni, entity);
        const float absorption = std::fmax(0.0f, minecraft::player_get_absorption_amount(ctx->jni, entity));
        const float max_total = minecraft::living_get_max_health(ctx->jni, entity) + absorption;

        dst.health_frac = (max_total != 0.0f)
            ? math::clampf(0.0f, 1.0f, (health + absorption) / max_total)
            : 0.0f;
        dst.absorption_frac = (health != 0.0f)
            ? math::clampf(0.0f, 1.0f, absorption / health)
            : 0.0f;

        if (s_player_count == kMaxCachedPlayers) {
            break;
        }
    }

    std::qsort(s_players, s_player_count, sizeof(CachedPlayer), &compare_players_by_dist);
}

void render_settings(gui::ClickGui* gui, gui::GuiRenderer* renderer) noexcept {
    gui::widgets::render_section_header(gui, renderer, "Main");
    s_range.render(gui, renderer);
    s_hide_friendlies.render(gui, renderer);
    gui::widgets::render_section_header(gui, renderer, "Customization");
    s_mode.render(gui, renderer);
    s_color_mode.render(gui, renderer);
    if (s_color_mode.value == 1) {
        s_color_enemies.render(gui, renderer);
        if (!s_hide_friendlies.value) {
            s_color_friendlies.render(gui, renderer);
        }
    }
    s_override_target_color.render(gui, renderer);
    if (s_override_target_color.value) {
        s_color_target.render(gui, renderer);
    }
    s_line_width.render(gui, renderer);
    s_show_2d_box.render(gui, renderer);
    if (s_show_2d_box.value) {
        s_filled.render(gui, renderer);
        s_hurt_effect.render(gui, renderer);
    }
    s_show_health_bar.render(gui, renderer);
}

void visit_config(config::ConfigArchive* ar) noexcept {
    s_mode.visit(ar, "mode");
    s_range.visit(ar, "range");
    s_hide_friendlies.visit(ar, "hide_friendlies");
    s_color_mode.visit(ar, "color_mode");
    s_color_enemies.visit(ar, "color_enemies");
    s_color_friendlies.visit(ar, "color_friendlies");
    s_override_target_color.visit(ar, "override_target_color");
    s_color_target.visit(ar, "color_target");
    s_line_width.visit(ar, "line_width");
    s_hurt_effect.visit(ar, "hurt_effect");
    s_filled.visit(ar, "filled");
    s_show_2d_box.visit(ar, "show_2d_box");
    s_show_health_bar.visit(ar, "show_health_bar");
}

int format_hud_tag(char* out_buf) noexcept {
    return std::snprintf(out_buf, 32, "%s", kStyleOptions[s_mode.value]);
}

events::Module create_module() noexcept {
    events::Module mod{};
    mod.name = "Player ESP";
    mod.description = "Highlights other players.";
    mod.category = events::ModuleCategory::Visual;
    mod.events[events::kEventRenderWorld3D] = {&on_render_world_3d, -300};
    mod.events[events::kEventRenderWorld2DProj] = {&on_render_world_2d, 0};
    mod.events[events::kEventPostLivingUpdate] = {&on_post_living_update, 0};
    mod.render_settings = &render_settings;
    mod.visit_config = &visit_config;
    mod.format_hud_tag = &format_hud_tag;
    return mod;
}

} // namespace

events::Module* get_player_esp_module() noexcept {
    static events::Module s_module = create_module();
    return &s_module;
}

} // namespace slinky::modules
