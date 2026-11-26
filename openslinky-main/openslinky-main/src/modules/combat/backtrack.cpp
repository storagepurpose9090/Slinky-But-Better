#include "modules/combat/backtrack.hpp"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <immintrin.h>

#include <glad/glad.h>

#include "config/settings.hpp"
#include "gui/widgets.hpp"
#include "minecraft/game_helpers.hpp"
#include "minecraft/mc_wrappers.hpp"
#include "modules/module_interop.hpp"
#include "util/math.hpp"

namespace slinky::modules {

namespace {

int32_t s_last_queued_pkt_id = 0;
int32_t s_last_released_pkt_id = 0;
std::atomic_flag s_lock = ATOMIC_FLAG_INIT;
int64_t s_max_delay_ms = 0;
int32_t s_target_entity_id = 0;
int32_t s_local_entity_id = 0;
bool s_active = false;
bool s_disable_on_hit = false;
math::Vec3d s_queued_pos_accum{};
bool s_has_absolute_teleport = false;
float s_remote_yaw = 0.0f;
bool s_has_remote_yaw = false;
int32_t s_last_teleport_pkt_id = 0;
bool s_render_indicator = false;
math::Vec3d s_prev_indicator_pos{};
math::Vec3d s_cur_indicator_pos{};
float s_prev_indicator_yaw = 0.0f;
float s_cur_indicator_yaw = 0.0f;
int32_t s_cooldown_ticks = 0;
math::Vec3d s_target_goal_pos{};
float s_target_goal_yaw = 0.0f;
int32_t s_indicator_interp_steps = 0;

void acquire_lock() noexcept {
    while (s_lock.test_and_set(std::memory_order_acquire)) {
        _mm_pause();
    }
}

void release_lock() noexcept {
    s_lock.clear(std::memory_order_release);
}

void on_incoming_packet(void* event) noexcept;
void on_render_world_3d(void* event) noexcept;
void on_post_living_update(void* event) noexcept;
void render_settings(gui::ClickGui* gui, gui::GuiRenderer* renderer) noexcept;
void visit_config(config::ConfigArchive* ar) noexcept;
int format_hud_tag(char* out_buf) noexcept;

struct BacktrackModule {
    events::Module base{};
    config::FloatSetting indicator_line_width{{}, "Line width", "%0.0f px", 1.0f, 0.0f, 1.0f, 4.0f, 1.0f, 1.0f};
    config::ColorSetting indicator_color{"Color", {0.99f, 0.99f, 0.99f}, {0.99f, 0.99f, 0.99f}};
    config::BoolSetting indicator_rotate{{}, "Apply head rotation", true, true};
    config::BoolSetting indicator_filled{{}, "Filled", true, true};
    config::FloatSetting max_hurt_time{{}, "Maximum hurt time", "%.0f ms", 1.0f, 50.0f, 1.0f, 10.0f, 10.0f, 10.0f};
    config::RangeSetting target_dist{{}, "Target distance", "%.1f blocks", "%.1f - %.1f blocks", 0.1f, 0.0f, 0.0f, 12.0f, {2.0f, 5.0f}, {2.0f, 5.0f}};
    config::FloatSetting delay{{}, "Maximum delay", "%.0f ms", 0.0f, 0.0f, 10.0f, 1000.0f, 100.0f, 100.0f};
    config::FloatSetting cooldown{{}, "Cooldown", "%.1f s", 1.0f, 0.05f, 0.0f, 100.0f, 5.0f, 5.0f};
    config::BoolSetting require_mouse_pressed{{}, "Mouse pressed", false, false};
    config::BoolSetting require_weapon{{}, "Holding weapon", false, false};
    config::BoolSetting disable_on_hit{{}, "Disable when hit", false, false};
    config::BoolSetting render_remote_pos{{}, "Enabled", false, false};

    BacktrackModule() noexcept {
        base.name = "Backtrack";
        base.description = "Uses lag to make players easier to hit.";
        base.category = events::ModuleCategory::Combat;
        base.enabled = false;
        base.flags = events::kModuleFlagNone;
        base.keybind = 0;
        base.hold_mode = false;
        base.events[events::kEventIncomingPacket] = {&on_incoming_packet, 0};
        base.events[events::kEventRenderWorld3D] = {&on_render_world_3d, 0};
        base.events[events::kEventPostLivingUpdate] = {&on_post_living_update, 0};
        base.render_settings = &render_settings;
        base.visit_config = &visit_config;
        base.format_hud_tag = &format_hud_tag;
    }
};

BacktrackModule s_mod{};

// sub_180058140
void track_target_packet(events::NettyPacketEvent* ev, bool released) noexcept {
    if (released && s_last_teleport_pkt_id > ev->packet_id) {
        return;
    }

    jobject s14 = mc::as_packet_s14_entity(ev->jni, ev->packet);
    if (s14 != nullptr) {
        if (mc::packet_s14_get_entity_id(ev->jni, s14) == s_target_entity_id) {
            const math::Vec3d delta{
                static_cast<double>(mc::packet_s14_get_pos_x(ev->jni, s14)) * 0.03125,
                static_cast<double>(mc::packet_s14_get_pos_y(ev->jni, s14)) * 0.03125,
                static_cast<double>(mc::packet_s14_get_pos_z(ev->jni, s14)) * 0.03125};
            if (released) {
                s_queued_pos_accum = math::vec3_sub(s_queued_pos_accum, delta);
            } else {
                s_queued_pos_accum = math::vec3_add(s_queued_pos_accum, delta);
                if (mc::packet_s14_has_rotation(ev->jni, s14)) {
                    const int32_t raw_yaw = static_cast<int32_t>(mc::packet_s14_get_yaw(ev->jni, s14));
                    s_remote_yaw = (static_cast<float>(raw_yaw) * 360.0f) * 0.00390625f;
                    s_has_remote_yaw = true;
                }
            }
        }
        return;
    }

    jobject s18 = mc::as_packet_s18_entity_teleport(ev->jni, ev->packet);
    if (s18 != nullptr && mc::packet_s18_get_entity_id(ev->jni, s18) == s_target_entity_id) {
        const math::Vec3d tp_pos{
            static_cast<double>(mc::packet_s18_get_pos_x(ev->jni, s18)) * 0.03125,
            static_cast<double>(mc::packet_s18_get_pos_y(ev->jni, s18)) * 0.03125,
            static_cast<double>(mc::packet_s18_get_pos_z(ev->jni, s18)) * 0.03125};
        if (released) {
            s_queued_pos_accum = math::vec3_sub(s_queued_pos_accum, tp_pos);
            s_has_absolute_teleport = false;
        } else {
            s_queued_pos_accum = tp_pos;
            s_has_absolute_teleport = true;
            const int32_t raw_yaw = static_cast<int32_t>(mc::packet_s18_get_yaw(ev->jni, s18));
            s_remote_yaw = (static_cast<float>(raw_yaw) * 360.0f) * 0.00390625f;
            s_has_remote_yaw = true;
            s_last_teleport_pkt_id = ev->packet_id;
        }
    }
}

// sub_180058460
double compute_distance_advantage(
    const math::Vec3d& local_feet,
    const math::Vec3d& target_srv_pos,
    const math::Vec3d& real_pos) noexcept {
    return math::vec3_dist(local_feet, real_pos) - math::vec3_dist(local_feet, target_srv_pos);
}

// sub_1800584F0
void step_indicator_smoothing(const math::Vec3d& goal_pos, float goal_yaw) noexcept {
    const bool same_goal =
        (std::memcmp(&s_target_goal_pos, &goal_pos, sizeof(math::Vec3d)) == 0) &&
        (goal_yaw == s_target_goal_yaw);

    int32_t steps = s_indicator_interp_steps;
    if (!same_goal) {
        s_indicator_interp_steps = 3;
        steps = 3;
    }

    s_target_goal_pos = goal_pos;
    s_prev_indicator_pos = s_cur_indicator_pos;
    s_target_goal_yaw = goal_yaw;
    s_prev_indicator_yaw = s_cur_indicator_yaw;

    if (steps <= 0) {
        s_cur_indicator_pos = goal_pos;
        s_cur_indicator_yaw = goal_yaw;
    } else {
        const double inv_steps = 1.0 / static_cast<double>(steps);
        const math::Vec3d delta = math::vec3_scale(math::vec3_sub(goal_pos, s_cur_indicator_pos), inv_steps);
        s_cur_indicator_pos = math::vec3_add(s_cur_indicator_pos, delta);
        const float yaw_delta =
            static_cast<float>(static_cast<double>(math::wrap_degrees_180(s_target_goal_yaw - s_cur_indicator_yaw)) * inv_steps);
        s_cur_indicator_yaw += yaw_delta;
        --s_indicator_interp_steps;
    }
}

// sub_1800586D0
bool is_target_moving_away(
    events::GameContext* ctx,
    jobject target,
    const math::Vec3d& local_feet) noexcept {
    const math::Vec3d client_pos = mc::entity_get_position(ctx->jni, target);
    const math::Vec3d server_pos = mc::other_player_mp_get_target_pos(ctx->jni, target);
    return (math::vec3_dist(local_feet, server_pos) - math::vec3_dist(local_feet, client_pos)) > 0.01;
}

// sub_180057740
void on_incoming_packet(void* event) noexcept {
    auto* ev = static_cast<events::NettyPacketEvent*>(event);
    acquire_lock();

    if (s_active) {
        jobject s19 = mc::as_packet_s19_entity_status(ev->jni, ev->packet);
        if (s19 != nullptr) {
            if (mc::packet_s19_get_entity_id(ev->jni, s19) == s_target_entity_id) {
                ev->action = 3;
                release_lock();
                return;
            }
        } else if (mc::as_packet_s06_update_health(ev->jni, ev->packet) != nullptr ||
                   mc::as_packet_s29_sound_effect(ev->jni, ev->packet) != nullptr) {
            ev->action = 3;
            release_lock();
            return;
        }

        jobject s12 = mc::as_packet_s12_entity_velocity(ev->jni, ev->packet);
        if (s12 != nullptr) {
            if (s_disable_on_hit && mc::packet_s12_get_entity_id(ev->jni, s12) == s_local_entity_id) {
                s_active = false;
                release_lock();
                return;
            }
        } else if (mc::as_packet_s08_player_pos_look(ev->jni, ev->packet) != nullptr) {
            s_active = false;
            release_lock();
            return;
        }

        if (ev->elapsed_ms <= s_max_delay_ms) {
            ev->action = 1;
            if (ev->packet_id > s_last_queued_pkt_id) {
                s_last_queued_pkt_id = ev->packet_id;
                track_target_packet(ev, false);
            }
        } else if (ev->action == 0 && ev->packet_id > s_last_released_pkt_id) {
            track_target_packet(ev, true);
            s_last_released_pkt_id = ev->packet_id;
        }
    }

    release_lock();
}

// sub_180057880
void on_render_world_3d(void* event) noexcept {
    if (!s_render_indicator) {
        return;
    }

    auto* ev = static_cast<events::RenderWorld3DEvent*>(event);
    const math::Vec3d interp_pos =
        math::vec3_lerp(s_prev_indicator_pos, s_cur_indicator_pos, static_cast<double>(ev->partial_ticks));
    const math::Vec3d rel = math::vec3_sub(interp_pos, ev->render_pos);

    mc::gl_bind_unit_cube_vertices();
    glLineWidth(s_mod.indicator_line_width.value);
    glPushMatrix();
    glTranslatef(
        static_cast<float>(rel.x),
        static_cast<float>(rel.y + 1.0),
        static_cast<float>(rel.z));
    glScalef(0.3f, 1.0f, 0.3f);

    if (s_mod.indicator_rotate.value) {
        const float lerped_yaw = math::lerpf(s_prev_indicator_yaw, s_cur_indicator_yaw, ev->partial_ticks);
        glRotatef(180.0f - lerped_yaw, 0.0f, 1.0f, 0.0f);
    }

    const math::Color3f col = s_mod.indicator_color.value;
    if (s_mod.indicator_filled.value) {
        glColor4f(col.r, col.g, col.b, 0.4f);
        mc::gl_draw_unit_cube_filled();
    }

    glColor3f(col.r, col.g, col.b);
    mc::gl_draw_unit_cube_outline();
    glPopMatrix();
    mc::gl_unbind_vertex_array();
}

// sub_180057A50
void on_post_living_update(void* event) noexcept {
    auto* ev = static_cast<events::ContextEvent*>(event);
    events::GameContext* ctx = ev->ctx;

    if (s_cooldown_ticks > 0) {
        --s_cooldown_ticks;
        if (s_cooldown_ticks > 0) {
            return;
        }
    }

    events::TargetTracker* tracker = ctx->target_tracker;
    const double min_dist_sq = static_cast<double>(s_mod.target_dist.value.min * s_mod.target_dist.value.min);
    const double max_dist_sq = static_cast<double>(s_mod.target_dist.value.max * s_mod.target_dist.value.max);

    bool eligible = false;
    if (ctx->in_game_focus &&
        (!s_mod.require_mouse_pressed.value || mc::is_attack_key_down(ctx)) &&
        (!s_mod.require_weapon.value || weapons_is_holding_weapon_simple(ctx->jni, ctx->local_player))) {
        const int32_t last_atk = mc::living_get_last_attacker_time(ctx->jni, ctx->local_player);
        const int32_t ticks_existed = mc::entity_get_ticks_existed(ctx->jni, ctx->local_player);
        if ((ticks_existed - last_atk) <= 20 &&
            tracker->target_entity != nullptr &&
            static_cast<float>(mc::living_get_hurt_time(ctx->jni, tracker->target_entity)) <= s_mod.max_hurt_time.value &&
            min_dist_sq <= tracker->distance_sq &&
            tracker->distance_sq <= max_dist_sq) {
            eligible = true;
        }
    }

    if (!eligible) {
        acquire_lock();
        if (s_active) {
            s_cooldown_ticks = static_cast<int32_t>(s_mod.cooldown.value);
            s_active = false;
        }
        release_lock();
        s_render_indicator = false;
        return;
    }

    math::Vec3d local_feet = mc::entity_get_position(ctx->jni, ctx->local_player);
    if (mc::is_version_1_7_10(ctx->jni)) {
        local_feet = math::vec3_sub_eye_height(local_feet);
    }

    jobject target = tracker->target_entity;
    const math::Vec3d target_srv_pos = mc::other_player_mp_get_target_pos(ctx->jni, target);
    const math::Vec2f target_rot = mc::entity_get_rotation(ctx->jni, target);
    const int32_t target_id = mc::entity_get_id(ctx->jni, target);
    const int32_t local_id = mc::entity_get_id(ctx->jni, ctx->local_player);

    acquire_lock();
    const int32_t prev_target_id = s_target_entity_id;
    s_max_delay_ms = static_cast<int64_t>(static_cast<uint32_t>(static_cast<int32_t>(s_mod.delay.value)));
    s_target_entity_id = target_id;
    s_local_entity_id = local_id;
    s_disable_on_hit = s_mod.disable_on_hit.value;

    if (s_active) {
        const math::Vec3d real_pos =
            s_has_absolute_teleport ? s_queued_pos_accum : math::vec3_add(s_queued_pos_accum, target_srv_pos);
        if (compute_distance_advantage(local_feet, target_srv_pos, real_pos) >= -0.05 &&
            prev_target_id == target_id) {
            const float real_yaw = s_has_remote_yaw ? s_remote_yaw : target_rot.x;
            step_indicator_smoothing(real_pos, real_yaw);
        } else {
            s_queued_pos_accum = {0.0, 0.0, 0.0};
            s_has_absolute_teleport = false;
            s_has_remote_yaw = false;
            s_render_indicator = false;
            s_active = false;
            s_cooldown_ticks = static_cast<int32_t>(s_mod.cooldown.value);
        }
        release_lock();
    } else {
        if (is_target_moving_away(ctx, target, local_feet)) {
            s_active = true;
            s_queued_pos_accum = {0.0, 0.0, 0.0};
            s_has_absolute_teleport = false;
            s_has_remote_yaw = false;

            const math::Vec3d init_pos = mc::entity_get_position(ctx->jni, target);
            s_prev_indicator_pos = init_pos;
            s_cur_indicator_pos = init_pos;
            s_target_goal_pos = init_pos;
            s_prev_indicator_yaw = target_rot.x;
            s_cur_indicator_yaw = target_rot.x;
            s_target_goal_yaw = target_rot.x;
            s_render_indicator = s_mod.render_remote_pos.value;
        } else {
            s_render_indicator = false;
        }
        release_lock();
    }
}

// sub_180057EB0
void render_settings(gui::ClickGui* gui, gui::GuiRenderer* renderer) noexcept {
    gui::widgets::render_section_header(gui, renderer, "Main");
    s_mod.target_dist.render(gui, renderer);
    s_mod.delay.render(gui, renderer);
    s_mod.max_hurt_time.render(gui, renderer);
    s_mod.cooldown.render(gui, renderer);
    s_mod.disable_on_hit.render(gui, renderer);

    gui::widgets::render_section_header(gui, renderer, "Real position indicator");
    s_mod.render_remote_pos.render(gui, renderer);
    if (s_mod.render_remote_pos.value) {
        s_mod.indicator_color.render(gui, renderer);
        s_mod.indicator_line_width.render(gui, renderer);
        s_mod.indicator_filled.render(gui, renderer);
        s_mod.indicator_rotate.render(gui, renderer);
    }

    gui::widgets::render_section_header(gui, renderer, "Conditions");
    s_mod.require_mouse_pressed.render(gui, renderer);
    s_mod.require_weapon.render(gui, renderer);
}

// sub_180057FE0
void visit_config(config::ConfigArchive* ar) noexcept {
    s_mod.target_dist.visit(ar, "target_dist");
    s_mod.delay.visit(ar, "delay");
    s_mod.max_hurt_time.visit(ar, "max_hurt_time");
    s_mod.cooldown.visit(ar, "cooldown");
    s_mod.disable_on_hit.visit(ar, "disable_on_hit");
    s_mod.render_remote_pos.visit(ar, "render_remote_pos");
    s_mod.indicator_color.visit(ar, "indicator_color");
    s_mod.indicator_line_width.visit(ar, "indicator_line_width");
    s_mod.indicator_filled.visit(ar, "indicator_filled");
    s_mod.indicator_rotate.visit(ar, "indicator_rotate");
    s_mod.require_mouse_pressed.visit(ar, "require_mouse_pressed");
    s_mod.require_weapon.visit(ar, "require_weapon");
}

// sub_180058100
int format_hud_tag(char* out_buf) noexcept {
    return std::snprintf(out_buf, 32, "%.0fms", static_cast<double>(s_mod.delay.value));
}

} // namespace

events::Module* get_backtrack_module() noexcept {
    return &s_mod.base;
}

} // namespace slinky::modules
