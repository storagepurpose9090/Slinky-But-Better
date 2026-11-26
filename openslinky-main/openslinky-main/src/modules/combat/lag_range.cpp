#include "modules/combat/lag_range.hpp"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <immintrin.h>

#include <glad/glad.h>

#include "config/settings.hpp"
#include "gui/widgets.hpp"
#include "minecraft/game_helpers.hpp"
#include "minecraft/mc_wrappers.hpp"
#include "modules/module_interop.hpp"
#include "util/math.hpp"
#include "util/random.hpp"

namespace slinky::modules {

namespace {

int32_t s_last_packet_id = 0;
std::atomic_flag s_lock = ATOMIC_FLAG_INIT;
int64_t s_max_delay_ms = 0;
bool s_closer_at_remote = false;
bool s_pending_attack_flush = false;
bool s_watch_sprint_reset = false;
bool s_force_flush_until_c03 = false;
math::Vec3d s_target_server_pos{};
bool s_active = false;
math::Vec3d s_last_sent_pos{};
bool s_render_indicator = false;
math::Vec3d s_prev_indicator_pos{};
math::Vec3d s_cur_indicator_pos{};
bool s_just_attacked = false;
bool s_was_sprinting = false;
int32_t s_splash_potion_cooldown_ticks = 0;
random::DriftNoise s_range_drift{};

void acquire_lock() noexcept {
    while (s_lock.test_and_set(std::memory_order_acquire)) {
        _mm_pause();
    }
}

void release_lock() noexcept {
    s_lock.clear(std::memory_order_release);
}

void on_outgoing_packet(void* event) noexcept;
void on_render_world_3d(void* event) noexcept;
void on_post_attack_or_use_entity(void* event) noexcept;
void on_post_living_update(void* event) noexcept;
void render_settings(gui::ClickGui* gui, gui::GuiRenderer* renderer) noexcept;
void visit_config(config::ConfigArchive* ar) noexcept;
int format_hud_tag(char* out_buf) noexcept;

struct LagRangeModule {
    events::Module base{};
    config::FloatSetting indicator_line_width{{}, "Line width", "%0.0f px", 1.0f, 0.0f, 1.0f, 4.0f, 1.0f, 1.0f};
    config::ColorSetting indicator_color{"Color", {0.99f, 0.99f, 0.99f}, {0.99f, 0.99f, 0.99f}};
    config::BoolSetting indicator_filled{{}, "Filled", true, true};
    config::FloatSetting delay{{}, "Maximum delay", "%.0f ms", 0.0f, 0.0f, 10.0f, 1000.0f, 100.0f, 100.0f};
    config::BoolSetting require_weapon{{}, "Holding weapon", false, false};
    config::BoolSetting flush_splash_potion{{}, "Used splash potion", false, false};
    config::BoolSetting flush_sprint_reset{{}, "Sprint reset", false, false};
    config::BoolSetting render_remote_pos{{}, "Enabled", false, false};

    LagRangeModule() noexcept {
        base.name = "Lag Range";
        base.description = "Uses lag to stay out of target range.";
        base.category = events::ModuleCategory::Combat;
        base.enabled = false;
        base.flags = events::kModuleFlagNone;
        base.keybind = 0;
        base.hold_mode = false;
        base.events[events::kEventOutgoingPacket] = {&on_outgoing_packet, -100};
        base.events[events::kEventRenderWorld3D] = {&on_render_world_3d, 0};
        base.events[events::kEventPostAttackOrUseEntity] = {&on_post_attack_or_use_entity, 0};
        base.events[events::kEventPostLivingUpdate] = {&on_post_living_update, 0};
        base.render_settings = &render_settings;
        base.visit_config = &visit_config;
        base.format_hud_tag = &format_hud_tag;
    }
};

LagRangeModule s_mod{};

// sub_18005B790
void on_outgoing_packet(void* event) noexcept {
    auto* ev = static_cast<events::NettyPacketEvent*>(event);
    acquire_lock();

    if (ev->packet_id > s_last_packet_id) {
        if (s_active) {
            jobject c03 = mc::as_packet_c03_player(ev->jni, ev->packet);
            if (c03 != nullptr && mc::packet_c03_is_moving(ev->jni, c03)) {
                const math::Vec3d pkt_pos = mc::packet_c03_get_position(ev->jni, c03);
                const double old_dist_sq = math::vec3_dist_sq(s_target_server_pos, s_last_sent_pos);
                const double new_dist_sq = math::vec3_dist_sq(s_target_server_pos, pkt_pos);
                s_closer_at_remote = old_dist_sq > new_dist_sq;
            }
        }
        s_last_packet_id = ev->packet_id;
    }

    if (s_active &&
        s_closer_at_remote &&
        !s_pending_attack_flush &&
        !s_watch_sprint_reset &&
        !s_force_flush_until_c03 &&
        ev->elapsed_ms < s_max_delay_ms) {
        ev->action = 1;
    } else if (ev->action == 0) {
        jobject c03 = mc::as_packet_c03_player(ev->jni, ev->packet);
        if (c03 != nullptr) {
            s_force_flush_until_c03 = false;
            if (mc::packet_c03_is_moving(ev->jni, c03)) {
                s_last_sent_pos = mc::packet_c03_get_position(ev->jni, c03);
            }
        } else {
            jobject c02 = mc::as_packet_c02_use_entity(ev->jni, ev->packet);
            if (c02 != nullptr && s_pending_attack_flush) {
                s_pending_attack_flush = false;
                s_force_flush_until_c03 = true;
            } else if (c02 == nullptr) {
                jobject c0b = mc::as_packet_c0b_entity_action(ev->jni, ev->packet);
                if (c0b != nullptr && s_watch_sprint_reset && mc::packet_c0b_get_action(ev->jni, c0b) == 4) {
                    s_watch_sprint_reset = false;
                    s_force_flush_until_c03 = true;
                }
            }
        }
    }

    release_lock();
}

// sub_18005B9E0
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

// sub_18005BB50
void on_post_attack_or_use_entity(void* event) noexcept {
    auto* ev = static_cast<events::AttackOrUseEntityEvent*>(event);
    if (ev->is_attack) {
        events::GameContext* ctx = ev->ctx;
        const int32_t ticks_existed = mc::entity_get_ticks_existed(ctx->jni, ctx->local_player);
        s_just_attacked = (ticks_existed == mc::living_get_last_attacker_time(ctx->jni, ctx->local_player));
    }
}

// sub_18005BBA0
void on_post_living_update(void* event) noexcept {
    const bool attacked = s_just_attacked;
    s_just_attacked = false;

    auto* ev = static_cast<events::ContextEvent*>(event);
    events::GameContext* ctx = ev->ctx;
    auto deactivate = [&]() noexcept {
        acquire_lock();
        s_force_flush_until_c03 = false;
        s_pending_attack_flush = false;
        s_watch_sprint_reset = false;
        s_active = false;
        release_lock();
        s_render_indicator = false;
    };

    if (!ctx->in_game_focus ||
        (s_mod.require_weapon.value && !weapons_is_holding_weapon_simple(ctx->jni, ctx->local_player))) {
        deactivate();
        return;
    }

    if (s_mod.flush_splash_potion.value) {
        jobject inv = mc::player_get_inventory(ctx->jni, ctx->local_player);
        const int32_t cur_slot = mc::inv_player_get_current_item(ctx->jni, inv);
        jobject stack = mc::inventory_get_stack_in_slot(ctx->jni, inv, cur_slot);
        if (stack != nullptr) {
            jobject held_item = mc::player_get_held_item(ctx->jni, ctx->local_player);
            if (held_item != nullptr &&
                mc::item_get_id(ctx->jni, held_item) == 373 &&
                (mc::item_stack_get_damage(ctx->jni, stack) & 0x4000) != 0) {
                s_splash_potion_cooldown_ticks = 15;
                deactivate();
                return;
            }
        }
        if (s_splash_potion_cooldown_ticks > 0) {
            --s_splash_potion_cooldown_ticks;
            deactivate();
            return;
        }
    }

    const bool sprinting = mc::entity_is_sprinting(ctx->jni, ctx->local_player);
    bool sprint_stopped = s_was_sprinting ? s_mod.flush_sprint_reset.value : false;
    if (sprinting) {
        sprint_stopped = false;
    }
    s_was_sprinting = sprinting;

    events::TargetTracker* tracker = ctx->target_tracker;
    if (tracker->target_entity == nullptr) {
        deactivate();
        return;
    }

    const float max_range = s_range_drift.sample_timed({10.0f, 15.0f}, 10.0);
    if (tracker->distance_sq > static_cast<double>(max_range * max_range)) {
        deactivate();
        return;
    }

    const bool target_vulnerable = mc::living_get_hurt_time(ctx->jni, tracker->target_entity) < 5;
    bool third_person = false;
    if (s_mod.render_remote_pos.value) {
        jobject settings = mc::mc_get_game_settings(ctx->jni, ctx->minecraft);
        third_person = mc::settings_get_third_person_view(ctx->jni, settings) != 0;
    }

    const bool flush_for_attack = target_vulnerable && attacked;
    const math::Vec3d target_srv_pos = mc::other_player_mp_get_target_pos(ctx->jni, tracker->target_entity);

    acquire_lock();
    s_max_delay_ms = static_cast<int64_t>(static_cast<uint32_t>(static_cast<int32_t>(s_mod.delay.value)));
    s_pending_attack_flush = flush_for_attack;
    s_watch_sprint_reset = sprint_stopped;
    s_active = true;
    s_target_server_pos = target_srv_pos;
    s_prev_indicator_pos = s_render_indicator ? s_cur_indicator_pos : s_last_sent_pos;
    s_cur_indicator_pos = s_last_sent_pos;
    s_render_indicator = third_person ? s_closer_at_remote : false;
    release_lock();
}

// sub_18005BED0
void render_settings(gui::ClickGui* gui, gui::GuiRenderer* renderer) noexcept {
    gui::widgets::render_section_header(gui, renderer, "Main");
    s_mod.delay.render(gui, renderer);

    gui::widgets::render_section_header(gui, renderer, "Flush if");
    s_mod.flush_sprint_reset.render(gui, renderer);
    s_mod.flush_splash_potion.render(gui, renderer);

    gui::widgets::render_section_header(gui, renderer, "Real position indicator");
    s_mod.render_remote_pos.render(gui, renderer);
    if (s_mod.render_remote_pos.value) {
        s_mod.indicator_color.render(gui, renderer);
        s_mod.indicator_line_width.render(gui, renderer);
        s_mod.indicator_filled.render(gui, renderer);
    }

    gui::widgets::render_section_header(gui, renderer, "Conditions");
    s_mod.require_weapon.render(gui, renderer);
}

// sub_18005BFD0
void visit_config(config::ConfigArchive* ar) noexcept {
    s_mod.delay.visit(ar, "delay");
    s_mod.flush_sprint_reset.visit(ar, "flush_sprint_reset");
    s_mod.flush_splash_potion.visit(ar, "flush_splash_potion");
    s_mod.render_remote_pos.visit(ar, "render_remote_pos");
    s_mod.indicator_color.visit(ar, "indicator_color");
    s_mod.indicator_line_width.visit(ar, "indicator_line_width");
    s_mod.indicator_filled.visit(ar, "indicator_filled");
    s_mod.require_weapon.visit(ar, "require_weapon");
}

// sub_18005C0A0
int format_hud_tag(char* out_buf) noexcept {
    return std::snprintf(out_buf, 32, "%.0fms", static_cast<double>(s_mod.delay.value));
}

} // namespace

events::Module* get_lag_range_module() noexcept {
    return &s_mod.base;
}

} // namespace slinky::modules
