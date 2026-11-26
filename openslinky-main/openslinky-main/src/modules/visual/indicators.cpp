#include "modules/visual/indicators.hpp"

#include <bit>
#include <cmath>
#include <cstdio>
#include <glad/glad.h>

#include "config/settings.hpp"
#include "gui/widgets.hpp"
#include "minecraft/game_helpers.hpp"
#include "minecraft/mc_wrappers.hpp"
#include "util/math.hpp"
#include "util/time.hpp"

namespace slinky::modules {

namespace {

constexpr size_t kMaxIndicators = 10;

constexpr int32_t kEntityArrow      = 10;
constexpr int32_t kEntityFireball   = 12;
constexpr int32_t kEntityEnderPearl = 14;

// Projectile RGB color lookup indexed by (entity_type - 10) (@ 0x180170690..0x1801706CB)
constexpr float kProjectileRed[5]   = {1.0f, 0.0f, 1.0f, 0.0f, std::bit_cast<float>(0x3E428F5Cu)};
constexpr float kProjectileGreen[5] = {1.0f, 0.0f, 0.5f, 0.0f, std::bit_cast<float>(0x3F19999Au)};
constexpr float kProjectileBlue[5]  = {1.0f, 0.0f, 0.0f, 0.0f, std::bit_cast<float>(0x3F0A3D71u)};

// 56-byte cached projectile indicator (@ 0x18031F090)
struct CachedIndicator {
    double pos_x = 0.0;          // +0x00
    double pos_z = 0.0;          // +0x08
    double prev_pos_x = 0.0;     // +0x10
    double prev_pos_z = 0.0;     // +0x18
    int32_t entity_type = 0;     // +0x20
    int32_t pad0 = 0;            // +0x24
    jstring dist_label = nullptr;// +0x28 (JNI GlobalRef)
    int32_t label_width = 0;     // +0x30
    uint32_t label_color = 0;    // +0x34
};
static_assert(sizeof(CachedIndicator) == 0x38, "CachedIndicator must be 56 bytes");

config::FloatSetting s_scale{
    {}, "Scale", "%.0f%%", 0.0f, 50.0f, 2.0f, 6.0f, 2.0f, 0.0f
};
config::FloatSetting s_radius{
    {}, "Radius", "%.0f", 1.0f, 0.0f, 50.0f, 200.0f, 50.0f, 0.0f
};
config::BoolSetting s_show_arrow{{}, "Arrows", true, false};
config::BoolSetting s_show_fireball{{}, "Fireballs", true, false};
config::BoolSetting s_show_pearl{{}, "Ender pearls", true, false};
config::BoolSetting s_require_coming_closer{{}, "Coming closer", false, false};

size_t s_indicator_count = 0;
CachedIndicator s_indicators[kMaxIndicators]{};

void release_indicator_labels(JNIEnv* env) noexcept {
    if (env != nullptr) {
        for (size_t i = 0; i < s_indicator_count; ++i) {
            if (s_indicators[i].dist_label != nullptr) {
                env->DeleteGlobalRef(s_indicators[i].dist_label);
                s_indicators[i].dist_label = nullptr;
            }
        }
    }
    s_indicator_count = 0;
}

// Matches sub_18005FE40
void on_module_state_changed(void* raw_event) noexcept {
    const auto* ev = static_cast<const events::ModuleStateChangedEvent*>(raw_event);
    if (!ev->enabled && ev->module == get_indicators_module()) {
        JNIEnv* env = (ev->ctx != nullptr && ev->ctx->jni != nullptr) ? ev->ctx->jni->env : nullptr;
        release_indicator_labels(env);
    }
}

// Matches sub_18005FED0
void on_render_world_2d(void* raw_event) noexcept {
    if (s_indicator_count == 0) {
        return;
    }

    const auto* ev = static_cast<const events::RenderWorld2DProjEvent*>(raw_event);
    events::GameContext* ctx = ev->ctx;

    jobject render_manager = minecraft::mc_get_render_manager(ctx->jni, ctx->minecraft);
    jobject font_renderer = minecraft::render_manager_get_font_renderer(ctx->jni, render_manager);
    const float view_y = minecraft::render_manager_get_player_view_y(ctx->jni, render_manager);

    minecraft::gl_bind_chevron_vertices();
    const math::Vec2f center = math::vec2_scale(ev->viewport_size, 0.5f);

    glPushMatrix();
    glTranslatef(center.x, center.y, 0.0f);
    glLineWidth(1.0f);
    glEnable(GL_LINE_SMOOTH);

    const float pulse_alpha = static_cast<float>(std::sin(time::now_seconds() * 20.0) * 0.5 + 0.5);

    for (size_t i = 0; i < s_indicator_count; ++i) {
        const CachedIndicator& entry = s_indicators[i];
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
        glTranslatef(0.0f, -s_radius.value, 0.0f);
        glScalef(s_scale.value, s_scale.value, s_scale.value);

        if (entry.dist_label != nullptr) {
            glPushMatrix();
            glRotatef(-angle_deg, 0.0f, 0.0f, 1.0f);

            if (ev->is_hand_pass) {
                minecraft::gl_state_enable_texture_2d(ctx->jni);
                minecraft::gl_state_color(ctx->jni, 1.0f, 1.0f, 1.0f, 1.0f);
            } else {
                glEnable(GL_TEXTURE_2D);
            }

            const float text_x = static_cast<float>(static_cast<double>(-entry.label_width) * 0.5);
            minecraft::font_renderer_draw_string(
                ctx->jni,
                font_renderer,
                entry.dist_label,
                text_x,
                -5.0f,
                static_cast<int32_t>(entry.label_color),
                true);

            if (ev->is_hand_pass) {
                minecraft::gl_state_disable_texture_2d(ctx->jni);
            } else {
                glDisable(GL_TEXTURE_2D);
            }

            glPopMatrix();
        }

        glTranslatef(0.0f, -15.0f, 0.0f);
        glScalef(5.0f, 5.0f, 5.0f);

        const int32_t color_idx = entry.entity_type - kEntityArrow;
        const float r = kProjectileRed[color_idx];
        const float g = kProjectileGreen[color_idx];
        const float b = kProjectileBlue[color_idx];

        glColor4f(r, g, b, pulse_alpha);
        minecraft::gl_draw_chevron_filled();
        glColor4f(0.0f, 0.0f, 0.0f, pulse_alpha);
        minecraft::gl_draw_chevron_outline();
        glPopMatrix();
    }

    glPopMatrix();
    minecraft::gl_unbind_vertex_array();
}

// Matches sub_180060320
void on_post_living_update(void* raw_event) noexcept {
    events::GameContext* ctx = static_cast<events::ContextEvent*>(raw_event)->ctx;
    release_indicator_labels(ctx->jni->env);

    if (ctx->world == nullptr) {
        return;
    }

    jobject render_manager = minecraft::mc_get_render_manager(ctx->jni, ctx->minecraft);
    jobject font_renderer = minecraft::render_manager_get_font_renderer(ctx->jni, render_manager);
    if (font_renderer == nullptr) {
        return;
    }

    const math::Vec3d local_pos = minecraft::entity_get_position(ctx->jni, ctx->local_player);
    jobject entity_list = minecraft::world_get_loaded_entity_list(ctx->jni, ctx->world);

    char label_buf[16]{};
    for (int32_t i = 0; i < minecraft::java_list_size(ctx->jni, entity_list); ++i) {
        jobject entity = minecraft::java_list_get(ctx->jni, entity_list, i);
        const int32_t type_id = minecraft::entity_get_type_id(ctx->jni, entity);

        if (type_id == kEntityArrow) {
            if (!s_show_arrow.value || minecraft::entity_arrow_is_in_ground(ctx->jni, entity)) {
                continue;
            }
        } else if (type_id == kEntityFireball) {
            if (!s_show_fireball.value) {
                continue;
            }
        } else if (type_id == kEntityEnderPearl) {
            if (!s_show_pearl.value) {
                continue;
            }
        } else {
            continue;
        }

        const math::Vec3d pos = minecraft::entity_get_position(ctx->jni, entity);
        const math::Vec3d prev_pos = minecraft::entity_get_prev_position(ctx->jni, entity);
        const double dist_sq = math::vec3_dist_sq(pos, local_pos);

        if (s_require_coming_closer.value) {
            const double prev_dist_sq = math::vec3_dist_sq(prev_pos, local_pos);
            if (dist_sq > prev_dist_sq) {
                continue;
            }
        }

        const double dist = std::sqrt(dist_sq);
        CachedIndicator& dst = s_indicators[s_indicator_count++];
        dst.pos_x = pos.x;
        dst.pos_z = pos.z;
        dst.prev_pos_x = prev_pos.x;
        dst.prev_pos_z = prev_pos.z;
        dst.entity_type = type_id;
        dst.dist_label = nullptr;
        dst.label_width = 0;
        dst.label_color = 0xFFFFFFFFu;

        std::snprintf(label_buf, 10, "%.0fm", dist);
        jstring local_str = ctx->jni->env->NewStringUTF(label_buf);
        if (local_str != nullptr) {
            dst.dist_label = static_cast<jstring>(ctx->jni->env->NewGlobalRef(local_str));
            if (dst.dist_label != nullptr) {
                dst.label_width = minecraft::font_renderer_get_string_width(
                    ctx->jni, font_renderer, dst.dist_label);

                uint32_t color = 0xFFFF0000u;
                if (dist > 15.0) {
                    color = 0xFFFFFF00u;
                }
                if (dist > 30.0) {
                    color = 0xFFFFFFFFu;
                }
                dst.label_color = color;
            }
        }

        if (s_indicator_count == kMaxIndicators) {
            break;
        }
    }
}

void render_settings(gui::ClickGui* gui, gui::GuiRenderer* renderer) noexcept {
    gui::widgets::render_section_header(gui, renderer, "Main");
    s_scale.render(gui, renderer);
    s_radius.render(gui, renderer);
    gui::widgets::render_section_header(gui, renderer, "Warn for");
    s_show_arrow.render(gui, renderer);
    s_show_fireball.render(gui, renderer);
    s_show_pearl.render(gui, renderer);
    gui::widgets::render_section_header(gui, renderer, "Conditions");
    s_require_coming_closer.render(gui, renderer);
}

void visit_config(config::ConfigArchive* ar) noexcept {
    s_scale.visit(ar, "scale");
    s_radius.visit(ar, "radius");
    s_show_arrow.visit(ar, "show_arrow");
    s_show_fireball.visit(ar, "show_fireball");
    s_show_pearl.visit(ar, "show_pearl");
    s_require_coming_closer.visit(ar, "require_coming_closer");
}

events::Module create_module() noexcept {
    events::Module mod{};
    mod.name = "Indicators";
    mod.description = "Points to incoming projectiles.";
    mod.category = events::ModuleCategory::Visual;
    mod.events[events::kEventModuleStateChanged] = {&on_module_state_changed, 0};
    mod.events[events::kEventRenderWorld2DProj] = {&on_render_world_2d, -300};
    mod.events[events::kEventPostLivingUpdate] = {&on_post_living_update, 0};
    mod.render_settings = &render_settings;
    mod.visit_config = &visit_config;
    return mod;
}

} // namespace

events::Module* get_indicators_module() noexcept {
    static events::Module s_module = create_module();
    return &s_module;
}

} // namespace slinky::modules
