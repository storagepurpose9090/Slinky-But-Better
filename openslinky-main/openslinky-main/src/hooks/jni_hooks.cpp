#include "hooks/jni_hooks.hpp"

#include <cstring>
#include <glad/glad.h>

#include "commands/command_manager.hpp"
#include "core/client.hpp"
#include "hooks/channel_handler.hpp"
#include "minecraft/game_helpers.hpp"
#include "minecraft/mc_wrappers.hpp"
#include "modules/module_interop.hpp"
#include "util/filesystem.hpp"
#include "util/input_win32.hpp"
#include "util/keycodes.hpp"
#include "util/math.hpp"
#include "util/time.hpp"

namespace slinky::hooks {

namespace {

// Populates a stack GameContext for the current hook invocation
void build_game_context(
    events::GameContext* out_ctx,
    events::JniContext* jni,
    jobject mc,
    jobject player,
    jobject world) noexcept
{
    core::ClientRuntime& rt = core::runtime();
    *out_ctx = events::GameContext{};
    out_ctx->profile_registry = rt.profile_registry;
    out_ctx->local_player = player;
    out_ctx->world = world;
    out_ctx->jni = jni;
    out_ctx->in_game_focus =
        modules::get_gui_module()->enabled ? false : minecraft::mc_has_ingame_focus(jni, mc);
    out_ctx->minecraft = mc;
    out_ctx->key_states = rt.key_states;
    out_ctx->module_manager = &rt.module_manager;
    out_ctx->target_tracker = &rt.target_tracker;
}

} // namespace

const bytecode::HookCallbacks& get_jni_hook_callbacks() noexcept {
    static const bytecode::HookCallbacks kCallbacks{
        &hook_is_display_active_pre_render, // [0]  0x29110
        &hook_update_display,               // [1]  0x29160
        &hook_invalidate_chunk,             // [2]  0x296A0
        &hook_should_clip_ledge,            // [3]  0x29770
        &hook_move_flying,                  // [4]  0x29800
        &hook_post_world_clear_1_8,         // [5]  0x29A10
        &hook_click_mouse,                  // [6]  0x29A20
        &hook_orient_camera,                // [7]  0x29B50
        &hook_get_mouse_over,               // [8]  0x29C10
        &hook_update_active_render_info,    // [9]  0x29E90
        &hook_set_render_x,                 // [10] 0x29F80
        &hook_set_render_y,                 // [11] 0x29FE0
        &hook_update_entities,              // [12] 0x2A040
        &hook_can_render_name,              // [13] 0x2A230
        &hook_right_click_mouse,            // [14] 0x2A260
        &hook_render_player,                // [15] 0x2A390
        &hook_send_packet_1_7,              // [16] 0x2A3F0
        &hook_send_packet_1_8,              // [17] 0x2A440
        &hook_post_world_clear_1_7,         // [18] 0x2A490
        &hook_unload_chunk,                 // [19] 0x2A4A0
        &hook_set_block_1_7,                // [20] 0x2A550
        &hook_set_block_1_8,                // [21] 0x2A630
        &hook_update_controller,            // [22] 0x2A700
    };
    return kCallbacks;
}

// Matches sub_180029060
jint JNICALL on_netty_packet(
    JNIEnv* env,
    jobject handler_this,
    jobject packet,
    jboolean outbound,
    jlong elapsed_ms,
    jint packet_id) noexcept
{
    (void)handler_this;
    core::ClientRuntime& rt = core::runtime();

    core::acquire_packet_lock();
    jint action = 0;
    if (!rt.unloading) {
        events::JniContext jni{rt.mappings, env};
        events::NettyPacketEvent ev{};
        ev.id = (outbound != JNI_FALSE) ? events::kEventOutgoingPacket : events::kEventIncomingPacket;
        ev.jni = &jni;
        ev.packet = packet;
        ev.elapsed_ms = static_cast<int64_t>(elapsed_ms);
        ev.packet_id = packet_id;
        ev.action = 0;
        rt.module_manager.dispatch(&ev);
        action = ev.action;
    }
    core::release_packet_lock();
    return action;
}

// Matches sub_180029110
jboolean JNICALL hook_is_display_active_pre_render(JNIEnv* env, jclass cls) noexcept {
    (void)cls;
    core::ClientRuntime& rt = core::runtime();
    events::JniContext jni{rt.mappings, env};

    minecraft::pre_living_update_spoof_rotation(&jni);
    if (modules::get_no_hurt_cam_module()->enabled) {
        modules::no_hurt_cam_apply(&jni);
    }
    return minecraft::lwjgl_display_is_active(&jni) ? JNI_TRUE : JNI_FALSE;
}

// Matches sub_180029160
void JNICALL hook_update_display(JNIEnv* env, jclass cls) noexcept {
    (void)cls;
    core::ClientRuntime& rt = core::runtime();

    input::ensure_wndproc_installed();

    events::JniContext jni{rt.mappings, env};
    jobject mc = minecraft::mc_get_instance(&jni);
    jobject player = minecraft::mc_get_player(&jni, mc);
    jobject world = minecraft::mc_get_world(&jni, mc);
    jobject timer = minecraft::mc_get_timer(&jni, mc);

    events::GameContext ctx{};
    build_game_context(&ctx, &jni, mc, player, world);

    if (!ctx.in_game_focus && rt.prev_in_game_focus) {
        if (rt.module_keybinds != nullptr) {
            rt.module_keybinds->release_all();
        }
        if (!minecraft::lwjgl_display_is_active(&jni) && rt.key_states != nullptr) {
            std::memset(rt.key_states, 0, input::KEY_COUNT);
        }
    }
    rt.prev_in_game_focus = ctx.in_game_focus;

    events::Module* gui_mod = modules::get_gui_module();
    if (gui_mod->keybind == 0) {
        rt.module_manager.set_module_keybind(gui_mod, input::KEY_RSHIFT);
    }

    bool overlay_just_disabled = false;
    if (rt.module_manager.has_pending_toggles()) {
        const bool was_overlay_on = modules::get_mod_overlay_module()->enabled;
        core::acquire_packet_lock();
        rt.module_manager.flush_pending_toggles(&ctx);
        core::release_packet_lock();
        overlay_just_disabled = was_overlay_on && !modules::get_mod_overlay_module()->enabled;
    }

    const double now = time::now_seconds();
    const auto fb_w = static_cast<uint32_t>(minecraft::lwjgl_display_get_framebuffer_width(&jni));
    const auto fb_h = static_cast<uint32_t>(minecraft::lwjgl_display_get_framebuffer_height(&jni));

    if (rt.last_gui_poll_time < 0.0) {
        rt.last_gui_poll_time = now;
    }

    if (rt.gui_renderer != nullptr && !rt.module_manager.has_no_subscribers(events::kEventGuiRender2D)) {
        rt.framebuffer_width = fb_w;
        rt.framebuffer_height = fb_h;

        const bool fb_recreated = rt.gui_renderer->update_framebuffer_size(fb_w, fb_h);
        if (fb_recreated || overlay_just_disabled) {
            rt.gui_warmup_frames = 10;
        } else if (rt.gui_warmup_frames > 0) {
            --rt.gui_warmup_frames;
        }

        rt.gui_renderer->set_scale(modules::gui_get_scale_factor());

        const double gui_dt = now - rt.last_gui_poll_time;
        if (gui_dt > (1.0 / 120.0)) {
            events::GuiFramePollEvent poll_ev{};
            poll_ev.id = events::kEventGuiFramePoll;
            poll_ev.ctx = &ctx;
            poll_ev.delta_time = static_cast<float>(gui_dt);
            rt.module_manager.dispatch(&poll_ev);

            rt.gui_visible = (poll_ev.render_flags & 1u) != 0;
            if (rt.gui_visible) {
                const bool dirty = (poll_ev.render_flags & 2u) != 0;
                if (dirty || rt.gui_needs_redraw || rt.gui_warmup_frames > 0) {
                    glPushAttrib(GL_ALL_ATTRIB_BITS);
                    rt.gui_renderer->begin_offscreen_frame(now);

                    events::GuiRender2DEvent render_ev{};
                    render_ev.id = events::kEventGuiRender2D;
                    render_ev.ctx = &ctx;
                    render_ev.renderer = rt.gui_renderer;
                    render_ev.delta_time = static_cast<float>(gui_dt);
                    rt.module_manager.dispatch(&render_ev);

                    rt.gui_renderer->end_offscreen_frame();
                    glPopAttrib();
                    rt.gui_needs_redraw = dirty;
                }
            }
            rt.last_gui_poll_time = now;
        }

        if (rt.gui_visible) {
            glPushAttrib(GL_ALL_ATTRIB_BITS);
            rt.gui_renderer->begin_frame(now);
            rt.gui_renderer->composite_offscreen(1.0f);
            rt.gui_renderer->end_frame();
            glPopAttrib();
        } else {
            rt.gui_needs_redraw = true;
        }
    }

    core::dispatch_frame_update(&ctx, timer, now);
    minecraft::lwjgl_display_update(&jni);

    if (modules::gui_is_unload_confirmed()) {
        config::save_global_settings(&rt.module_manager, rt.profile_registry);
        rt.module_manager.dispatch_shutdown_to_all(&ctx);

        core::acquire_packet_lock();
        rt.unloading = true;
        hooks::remove_channel_handler_from_pipeline(&jni, &rt.channel_handler);
        core::release_packet_lock();

        rt.module_manager.flush_pending_toggles(&ctx);
        minecraft::clear_target_tracker(&ctx, &rt.target_tracker);
        core::shutdown_client_subsystems(env, false);
        input::shutdown_win32_input();
        input::spawn_self_unload_thread(rt.dll_module);
    }
}

// Matches sub_1800296A0
void JNICALL hook_invalidate_chunk(
    JNIEnv* env,
    jclass cls,
    jobject world,
    jint min_x,
    jint min_y,
    jint min_z,
    jint max_x,
    jint max_y,
    jint max_z) noexcept
{
    (void)cls;
    core::ClientRuntime& rt = core::runtime();
    events::JniContext jni{rt.mappings, env};

    minecraft::world_mark_block_range_for_render_update(&jni, world, min_x, min_y, min_z, max_x, max_y, max_z);

    events::WorldBlockUpdateEvent ev{};
    ev.id = events::kEventWorldBlockUpdate;
    ev.jni = &jni;
    ev.world = world;
    ev.update_kind = 1;
    ev.x = math::block_to_chunk_coord(min_x);
    ev.y_or_cz = math::block_to_chunk_coord(min_z);
    rt.module_manager.dispatch(&ev);
}

// Matches sub_180029770
jboolean JNICALL hook_should_clip_ledge(JNIEnv* env, jclass cls, jobject entity) noexcept {
    (void)cls;
    core::ClientRuntime& rt = core::runtime();
    events::JniContext jni{rt.mappings, env};

    if (!modules::get_safe_walk_module()->enabled) {
        return minecraft::entity_is_sneaking(&jni, entity) ? JNI_TRUE : JNI_FALSE;
    }

    jobject mc = minecraft::mc_get_instance(&jni);
    jobject player = minecraft::mc_get_player(&jni, mc);
    if (player == nullptr || env->IsSameObject(player, entity) == JNI_FALSE) {
        return minecraft::entity_is_sneaking(&jni, entity) ? JNI_TRUE : JNI_FALSE;
    }

    if (modules::safe_walk_is_edge_sneaking()) {
        return JNI_TRUE;
    }
    return minecraft::entity_is_sneaking(&jni, entity) ? JNI_TRUE : JNI_FALSE;
}

// Matches sub_180029800
void JNICALL hook_move_flying(
    JNIEnv* env,
    jclass cls,
    jobject entity,
    jfloat strafe,
    jfloat forward,
    jfloat friction) noexcept
{
    (void)cls;
    core::ClientRuntime& rt = core::runtime();
    events::JniContext jni{rt.mappings, env};

    if (rt.module_manager.has_no_subscribers(events::kEventPreMoveWithHeading) &&
        rt.module_manager.has_no_subscribers(events::kEventPostMoveWithHeading)) {
        minecraft::entity_move_flying(&jni, entity, strafe, forward, friction);
        return;
    }

    jobject mc = minecraft::mc_get_instance(&jni);
    jobject player = minecraft::mc_get_player(&jni, mc);
    if (player == nullptr || env->IsSameObject(entity, player) == JNI_FALSE) {
        minecraft::entity_move_flying(&jni, entity, strafe, forward, friction);
        return;
    }

    jobject world = minecraft::mc_get_world(&jni, mc);
    events::GameContext ctx{};
    build_game_context(&ctx, &jni, mc, player, world);

    jobject settings = minecraft::mc_get_game_settings(&jni, mc);
    jobject jump_key = minecraft::settings_get_key_jump(&jni, settings);
    const bool is_jumping =
        (minecraft::living_get_jump_ticks(&jni, player) > 0) &&
        minecraft::key_binding_is_pressed(&jni, jump_key);

    events::MoveWithHeadingEvent ev{};
    ev.id = events::kEventPreMoveWithHeading;
    ev.ctx = &ctx;
    ev.move_strafe = strafe;
    ev.move_forward = forward;
    ev.friction = friction;
    ev.is_jumping = is_jumping;
    rt.module_manager.dispatch(&ev);

    minecraft::entity_move_flying(&jni, player, ev.move_strafe, ev.move_forward, ev.friction);

    ev.id = events::kEventPostMoveWithHeading;
    rt.module_manager.dispatch(&ev);
}

// Matches sub_180029A10
void JNICALL hook_post_world_clear_1_8(JNIEnv* env, jclass cls, jint mask) noexcept {
    (void)cls;
    render_world_gl_pass(env, mask, true);
}

// Matches sub_180029A20
void JNICALL hook_click_mouse(JNIEnv* env, jclass cls, jobject mc) noexcept {
    (void)cls;
    core::ClientRuntime& rt = core::runtime();
    events::JniContext jni{rt.mappings, env};

    jobject world = minecraft::mc_get_world(&jni, mc);
    jobject player = minecraft::mc_get_player(&jni, mc);
    events::GameContext ctx{};
    build_game_context(&ctx, &jni, mc, player, world);

    minecraft::evaluate_attack_slowdown(&ctx, &rt.target_tracker);

    events::AttackOrUseEntityEvent ev{};
    ev.id = events::kEventPreAttackOrUseEntity;
    ev.ctx = &ctx;
    ev.is_attack = true;
    ev.cancelled = false;
    rt.module_manager.dispatch(&ev);

    if (!ev.cancelled) {
        minecraft::mc_click_mouse(&jni, mc);
        ev.id = events::kEventPostAttackOrUseEntity;
        rt.module_manager.dispatch(&ev);
    }
}

// Matches sub_180029B50
void JNICALL hook_orient_camera(
    JNIEnv* env,
    jclass cls,
    jobject entity_renderer,
    jfloat partial_ticks) noexcept
{
    (void)cls;
    core::ClientRuntime& rt = core::runtime();
    events::JniContext jni{rt.mappings, env};

    if (minecraft::is_rotation_spoof_active()) {
        jobject mc = minecraft::mc_get_instance(&jni);
        jobject player = minecraft::mc_get_player(&jni, mc);
        jobject render_global = minecraft::mc_get_render_global(&jni, mc);
        minecraft::render_global_set_entities_dirty(&jni, render_global, true);
        minecraft::post_living_update_restore_rotation(&jni, player);
        minecraft::swap_render_spoof_rotations(&jni, player, true);
        minecraft::entity_renderer_orient_camera(&jni, entity_renderer, partial_ticks);
        minecraft::swap_render_spoof_rotations(&jni, player, false);
    } else {
        minecraft::entity_renderer_orient_camera(&jni, entity_renderer, partial_ticks);
    }
}

// Matches sub_180029C10 (see disasm_11_full.txt lines 73..220)
void JNICALL hook_get_mouse_over(
    JNIEnv* env,
    jclass cls,
    jobject entity_renderer,
    jfloat partial_ticks) noexcept
{
    (void)cls;
    core::ClientRuntime& rt = core::runtime();
    events::JniContext jni{rt.mappings, env};

    minecraft::entity_renderer_get_mouse_over(&jni, entity_renderer, partial_ticks);

    uint8_t expected_init = 0;
    if (rt.gl_initialized.compare_exchange_strong(expected_init, 1, std::memory_order_acq_rel)) {
        const int err = core::ensure_render_thread_initialized();
        if (err != 0) {
            fs::show_error_dialog("Initialization failed: %u", static_cast<unsigned int>(err));
            core::shutdown_client_subsystems(env, true);
            input::shutdown_win32_input();
            input::spawn_self_unload_thread(rt.dll_module);
            return;
        }
    }

    jobject mc = minecraft::mc_get_instance(&jni);
    if (hooks::ensure_channel_handler_installed(&jni, &rt.channel_handler) != 0) {
        env->ExceptionDescribe();
        env->ExceptionClear();
    }

    minecraft::update_mouse_sensitivity_step(&jni, mc);

    jobject player = minecraft::mc_get_player(&jni, mc);
    jobject world = minecraft::mc_get_world(&jni, mc);
    events::GameContext ctx{};
    build_game_context(&ctx, &jni, mc, player, world);

    minecraft::update_target_tracker(&ctx, &rt.target_tracker);

    events::ContextEvent tick_ev{};
    tick_ev.id = events::kEventGameTick;
    tick_ev.ctx = &ctx;
    rt.module_manager.dispatch(&tick_ev);

    if (minecraft::mc_has_ingame_focus(&jni, mc)) {
        events::ReachRaycastQueryEvent reach_ev{};
        reach_ev.id = events::kEventReachRaycastQuery;
        reach_ev.ctx = &ctx;
        reach_ev.override_raycast = false;
        reach_ev.reach_distance = 3.0;
        reach_ev.hitbox_expansion = 0.1;
        rt.module_manager.dispatch(&reach_ev);

        if (reach_ev.override_raycast) {
            compute_reach_raycast(&reach_ev);
        }
        minecraft::release_hotbar_block_selector();
    }
}

// Matches sub_180029E90
void JNICALL hook_update_active_render_info(
    JNIEnv* env,
    jclass cls,
    jobject player,
    jboolean third_person_front) noexcept
{
    (void)cls;
    core::ClientRuntime& rt = core::runtime();
    events::JniContext jni{rt.mappings, env};

    if (minecraft::is_rotation_spoof_active()) {
        jobject mc = minecraft::mc_get_instance(&jni);
        jobject local_player = minecraft::mc_get_player(&jni, mc);
        const math::Vec2f saved_rot = minecraft::entity_get_rotation(&jni, local_player);
        const math::Vec2f saved_prev_rot = minecraft::entity_get_prev_rotation(&jni, local_player);
        const math::Vec2f spoof_rot = minecraft::get_server_spoof_rotation();

        minecraft::entity_set_rotation(&jni, local_player, spoof_rot);
        minecraft::entity_set_prev_rotation(&jni, local_player, spoof_rot);
        minecraft::active_render_info_update(&jni, player, third_person_front != JNI_FALSE);
        minecraft::entity_set_rotation(&jni, local_player, saved_rot);
        minecraft::entity_set_prev_rotation(&jni, local_player, saved_prev_rot);
    } else {
        minecraft::active_render_info_update(&jni, player, third_person_front != JNI_FALSE);
    }
}

// Matches sub_180029F80
void JNICALL hook_set_render_x(
    JNIEnv* env,
    jclass cls,
    jobject render_manager,
    jfloat angle_x) noexcept
{
    (void)cls;
    core::ClientRuntime& rt = core::runtime();
    float view_x = angle_x;
    if (minecraft::is_rotation_spoof_active()) {
        view_x = minecraft::get_server_spoof_rotation().y;
    }
    events::JniContext jni{rt.mappings, env};
    minecraft::render_manager_set_player_view_x(&jni, render_manager, view_x);
}

// Matches sub_180029FE0
void JNICALL hook_set_render_y(
    JNIEnv* env,
    jclass cls,
    jobject render_manager,
    jfloat angle_y) noexcept
{
    (void)cls;
    core::ClientRuntime& rt = core::runtime();
    float view_y = angle_y;
    if (minecraft::is_rotation_spoof_active()) {
        view_y = minecraft::get_server_spoof_rotation().x;
    }
    events::JniContext jni{rt.mappings, env};
    minecraft::render_manager_set_player_view_y(&jni, render_manager, view_y);
}

// Matches sub_18002A040
void JNICALL hook_update_entities(JNIEnv* env, jclass cls, jobject world) noexcept {
    (void)cls;
    core::ClientRuntime& rt = core::runtime();
    events::JniContext jni{rt.mappings, env};

    jobject mc = minecraft::mc_get_instance(&jni);
    jobject player = minecraft::mc_get_player(&jni, mc);
    events::GameContext ctx{};
    build_game_context(&ctx, &jni, mc, player, world);

    events::ContextEvent ev{};
    ev.id = events::kEventPreLivingUpdate;
    ev.ctx = &ctx;
    rt.module_manager.dispatch(&ev);

    minecraft::apply_move_fix_if_active(&ctx);

    ev.id = events::kEventMidLivingUpdate;
    rt.module_manager.dispatch(&ev);

    minecraft::world_update_entities(&jni, world);

    ev.id = events::kEventPostLivingUpdate;
    rt.module_manager.dispatch(&ev);

    math::Vec2f target_rot = minecraft::is_rotation_spoof_active()
        ? minecraft::get_server_spoof_rotation()
        : minecraft::entity_get_rotation(&jni, player);

    bool spoof_active = false;
    bool move_fix = false;
    bool silent_restore = false;
    if (!rt.module_manager.has_no_subscribers(events::kEventRotationSpoofQuery)) {
        spoof_active = core::query_rotation_spoof(&move_fix, &silent_restore, &target_rot);
    }

    minecraft::update_rotation_spoof_state(&ctx, spoof_active, move_fix, silent_restore, target_rot);
    minecraft::apply_pending_aim_rotation(&jni, player, true);
}

// Matches sub_18002A230
jboolean JNICALL hook_can_render_name(
    JNIEnv* env,
    jclass cls,
    jobject renderer,
    jobject entity) noexcept
{
    (void)cls;
    core::ClientRuntime& rt = core::runtime();
    events::JniContext jni{rt.mappings, env};
    return modules::nametags_can_render_vanilla_name(&jni, renderer, entity) ? JNI_TRUE : JNI_FALSE;
}

// Matches sub_18002A260
void JNICALL hook_right_click_mouse(JNIEnv* env, jclass cls, jobject mc) noexcept {
    (void)cls;
    core::ClientRuntime& rt = core::runtime();
    events::JniContext jni{rt.mappings, env};

    jobject world = minecraft::mc_get_world(&jni, mc);
    jobject player = minecraft::mc_get_player(&jni, mc);
    events::GameContext ctx{};
    build_game_context(&ctx, &jni, mc, player, world);

    events::AttackOrUseEntityEvent ev{};
    ev.id = events::kEventPreAttackOrUseEntity;
    ev.ctx = &ctx;
    ev.is_attack = false;
    ev.cancelled = false;
    rt.module_manager.dispatch(&ev);

    minecraft::pre_place_record_held_block(&ctx);

    if (!ev.cancelled) {
        minecraft::mc_right_click_mouse(&jni, mc);
        ev.id = events::kEventPostAttackOrUseEntity;
        rt.module_manager.dispatch(&ev);
        minecraft::post_place_check_held_block_depleted(&ctx);
    }
}

// Matches sub_18002A390
void JNICALL hook_render_player(
    JNIEnv* env,
    jclass cls,
    jobject render_player,
    jobject entity,
    jdouble x,
    jdouble y,
    jdouble z,
    jfloat entity_yaw,
    jfloat partial_ticks) noexcept
{
    (void)cls;
    core::ClientRuntime& rt = core::runtime();
    events::JniContext jni{rt.mappings, env};
    modules::chams_render_living_model(&jni, render_player, entity, x, y, z, entity_yaw, partial_ticks);
}

// Matches sub_18002A3F0
void JNICALL hook_send_packet_1_7(
    JNIEnv* env,
    jclass cls,
    jobject net_mgr,
    jobject packet,
    jobject listeners) noexcept
{
    (void)cls;
    core::ClientRuntime& rt = core::runtime();
    events::JniContext jni{rt.mappings, env};
    if (!handle_client_outgoing_packet(&jni, net_mgr, packet)) {
        minecraft::network_manager_send_packet_1_7(&jni, net_mgr, packet, static_cast<jobjectArray>(listeners));
    }
}

// Matches sub_18002A440
void JNICALL hook_send_packet_1_8(
    JNIEnv* env,
    jclass cls,
    jobject net_mgr,
    jobject packet) noexcept
{
    (void)cls;
    core::ClientRuntime& rt = core::runtime();
    events::JniContext jni{rt.mappings, env};
    if (!handle_client_outgoing_packet(&jni, net_mgr, packet)) {
        minecraft::network_manager_send_packet_1_8(&jni, net_mgr, packet);
    }
}

// Matches sub_18002A490
void JNICALL hook_post_world_clear_1_7(JNIEnv* env, jclass cls, jint mask) noexcept {
    (void)cls;
    render_world_gl_pass(env, mask, false);
}

// Matches sub_18002A4A0
void JNICALL hook_unload_chunk(
    JNIEnv* env,
    jclass cls,
    jobject chunk_provider,
    jint chunk_x,
    jint chunk_z) noexcept
{
    (void)cls;
    core::ClientRuntime& rt = core::runtime();
    events::JniContext jni{rt.mappings, env};

    jobject mc = minecraft::mc_get_instance(&jni);
    jobject world = minecraft::mc_get_world(&jni, mc);
    minecraft::chunk_provider_unload_chunk(&jni, chunk_provider, chunk_x, chunk_z);

    events::WorldBlockUpdateEvent ev{};
    ev.id = events::kEventWorldBlockUpdate;
    ev.jni = &jni;
    ev.world = world;
    ev.update_kind = 0;
    ev.x = chunk_x;
    ev.y_or_cz = chunk_z;
    rt.module_manager.dispatch(&ev);
}

// Matches sub_18002A550
jboolean JNICALL hook_set_block_1_7(
    JNIEnv* env,
    jclass cls,
    jobject world,
    jint x,
    jint y,
    jint z,
    jobject block,
    jint metadata,
    jint flags) noexcept
{
    (void)cls;
    core::ClientRuntime& rt = core::runtime();
    events::JniContext jni{rt.mappings, env};

    events::WorldBlockUpdateEvent ev{};
    ev.id = events::kEventWorldBlockUpdate;
    ev.jni = &jni;
    ev.world = world;
    ev.update_kind = 2;
    ev.x = x;
    ev.y_or_cz = y;
    ev.z = z;
    ev.block_id = minecraft::block_get_id(&jni, block);
    rt.module_manager.dispatch(&ev);

    return minecraft::world_set_block_1_7(&jni, world, x, y, z, block, metadata, flags) ? JNI_TRUE : JNI_FALSE;
}

// Matches sub_18002A630
jboolean JNICALL hook_set_block_1_8(
    JNIEnv* env,
    jclass cls,
    jobject world,
    jobject block_pos,
    jobject block_state,
    jint flags) noexcept
{
    (void)cls;
    core::ClientRuntime& rt = core::runtime();
    events::JniContext jni{rt.mappings, env};

    const minecraft::Vec3i pos = minecraft::block_pos_to_vec3i(&jni, block_pos);
    jobject block = minecraft::iblock_state_get_block(&jni, block_state);
    const int32_t block_id = minecraft::block_get_id(&jni, block);

    events::WorldBlockUpdateEvent ev{};
    ev.id = events::kEventWorldBlockUpdate;
    ev.jni = &jni;
    ev.world = world;
    ev.update_kind = 2;
    ev.x = pos.x;
    ev.y_or_cz = pos.y;
    ev.z = pos.z;
    ev.block_id = block_id;
    rt.module_manager.dispatch(&ev);

    return minecraft::world_set_block_state_1_8(&jni, world, block_pos, block_state, flags) ? JNI_TRUE : JNI_FALSE;
}

// Matches sub_18002A700
void JNICALL hook_update_controller(
    JNIEnv* env,
    jclass cls,
    jobject player_controller) noexcept
{
    (void)cls;
    core::ClientRuntime& rt = core::runtime();
    events::JniContext jni{rt.mappings, env};

    minecraft::controller_update(&jni, player_controller);

    jobject mc = minecraft::mc_get_instance(&jni);
    jobject player = minecraft::mc_get_player(&jni, mc);
    jobject world = minecraft::mc_get_world(&jni, mc);
    events::GameContext ctx{};
    build_game_context(&ctx, &jni, mc, player, world);

    events::GuiScreenTickEvent ev{};
    ev.id = events::kEventGuiScreenTick;
    ev.ctx = &ctx;
    ev.forward_movement_keys = false;
    rt.module_manager.dispatch(&ev);

    if (ev.forward_movement_keys) {
        jobject gui_container = minecraft::mc_get_gui_container_screen(&jni, mc);
        jobject settings = minecraft::mc_get_game_settings(&jni, mc);
        jobject inv_key = minecraft::settings_get_key_inventory(&jni, settings);
        const int32_t inv_key_code = minecraft::key_binding_get_key_code(&jni, inv_key);

        while (minecraft::lwjgl_mouse_next(&jni)) {
        }

        while (minecraft::lwjgl_keyboard_next(&jni)) {
            if (gui_container != nullptr && minecraft::lwjgl_keyboard_get_event_key_state(&jni)) {
                const int32_t event_key = minecraft::lwjgl_keyboard_get_event_key(&jni);
                if (event_key == 1 || event_key == inv_key_code) {
                    minecraft::gui_screen_key_typed(&jni, gui_container, 0, event_key);
                }
            }
        }
    }
}

// Matches sub_18002A8B0
void render_world_gl_pass(JNIEnv* env, jint clear_mask, bool clear_gl_state_1_8) noexcept {
    core::ClientRuntime& rt = core::runtime();
    if (rt.gui_renderer == nullptr) {
        return;
    }

    if (clear_mask != GL_DEPTH_BUFFER_BIT) {
        glClear(static_cast<GLbitfield>(clear_mask));
        return;
    }

    events::JniContext jni{rt.mappings, env};
    jobject mc = minecraft::mc_get_instance(&jni);
    jobject player = minecraft::mc_get_player(&jni, mc);
    const bool spoof_was_active = minecraft::is_rotation_spoof_active();
    if (spoof_was_active) {
        minecraft::swap_render_spoof_rotations(&jni, player, true);
    }

    const bool no_3d = rt.module_manager.has_no_subscribers(events::kEventRenderWorld3D);
    const bool no_2d_proj = rt.module_manager.has_no_subscribers(events::kEventRenderWorld2DProj);

    jobject world = nullptr;
    jobject view_entity = nullptr;
    if (!no_3d || !no_2d_proj) {
        world = minecraft::mc_get_world(&jni, mc);
        view_entity = minecraft::mc_get_render_view_entity(&jni, mc);
    }

    if ((no_3d && no_2d_proj) || player == nullptr || world == nullptr || view_entity == nullptr) {
        if (spoof_was_active) {
            minecraft::swap_render_spoof_rotations(&jni, player, false);
        }
        glClear(static_cast<GLbitfield>(clear_mask));
        return;
    }

    events::GameContext ctx{};
    build_game_context(&ctx, &jni, mc, player, world);

    jobject timer = minecraft::mc_get_timer(&jni, mc);
    const float partial_ticks = minecraft::timer_get_render_partial_ticks(&jni, timer);
    const math::Vec3d cur_pos = minecraft::entity_get_position(&jni, view_entity);
    const math::Vec3d prev_pos = minecraft::entity_get_prev_position(&jni, view_entity);
    const math::Vec3d render_pos = math::vec3_lerp(prev_pos, cur_pos, static_cast<double>(partial_ticks));

    if (clear_gl_state_1_8) {
        minecraft::gl_state_disable_depth(&jni);
        minecraft::gl_state_disable_texture_2d(&jni);
        minecraft::gl_state_disable_lighting(&jni);
        minecraft::gl_state_enable_blend(&jni);
        minecraft::gl_state_blend_func(&jni, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_LINE_SMOOTH);

    if (!no_3d) {
        events::RenderWorld3DEvent ev3d{};
        ev3d.id = events::kEventRenderWorld3D;
        ev3d.render_pos = render_pos;
        ev3d.ctx = &ctx;
        ev3d.partial_ticks = partial_ticks;
        ev3d.is_hand_pass = clear_gl_state_1_8;
        rt.module_manager.dispatch(&ev3d);
    }

    if (!no_2d_proj) {
        glGetFloatv(GL_MODELVIEW_MATRIX, rt.gl_modelview);
        glGetFloatv(GL_PROJECTION_MATRIX, rt.gl_projection);

        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(
            0.0,
            static_cast<double>(rt.framebuffer_width),
            static_cast<double>(rt.framebuffer_height),
            0.0,
            -3000.0,
            3000.0);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();

        events::RenderWorld2DProjEvent ev2d{};
        ev2d.id = events::kEventRenderWorld2DProj;
        ev2d.render_pos = render_pos;
        ev2d.ctx = &ctx;
        ev2d.viewport_size = math::make_vec2(
            static_cast<float>(rt.framebuffer_width),
            static_cast<float>(rt.framebuffer_height));
        ev2d.modelview_matrix = rt.gl_modelview;
        ev2d.projection_matrix = rt.gl_projection;
        ev2d.partial_ticks = partial_ticks;
        ev2d.is_hand_pass = clear_gl_state_1_8;
        rt.module_manager.dispatch(&ev2d);
    }

    if (clear_gl_state_1_8) {
        minecraft::gl_state_enable_texture_2d(&jni);
        minecraft::gl_state_enable_depth(&jni);
        minecraft::gl_state_enable_cull(&jni);
        minecraft::gl_state_enable_alpha(&jni);
        minecraft::gl_state_color(&jni, 1.0f, 1.0f, 1.0f, 1.0f);
    }

    glEnable(GL_TEXTURE_2D);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glEnable(GL_ALPHA_TEST);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

    if (spoof_was_active) {
        minecraft::swap_render_spoof_rotations(&jni, player, false);
    }
    glClear(static_cast<GLbitfield>(clear_mask));
}

// Matches sub_18002AEE0
bool handle_client_outgoing_packet(
    events::JniContext* jni,
    jobject net_handler_or_mgr,
    jobject packet) noexcept
{
    core::ClientRuntime& rt = core::runtime();
    jobject mc = minecraft::mc_get_instance(jni);
    jobject player = minecraft::mc_get_player(jni, mc);
    jobject world = minecraft::mc_get_world(jni, mc);

    events::GameContext ctx{};
    build_game_context(&ctx, jni, mc, player, world);

    if (jobject chat_pkt = minecraft::as_packet_c01_chat_message(jni, packet); chat_pkt != nullptr) {
        if (commands::handle_outgoing_chat_packet(&ctx, chat_pkt)) {
            return true;
        }
    } else if (jobject tab_pkt = minecraft::as_packet_c14_tab_complete(jni, packet); tab_pkt != nullptr) {
        if (commands::should_cancel_tab_complete_packet(&ctx, tab_pkt)) {
            return true;
        }
    }

    events::MainThreadPacketEvent ev{};
    ev.id = events::kEventMainThreadPacket;
    ev.jni = jni;
    ev.net_handler = net_handler_or_mgr;
    ev.packet = packet;
    ev.cancelled = false;
    rt.module_manager.dispatch(&ev);
    return ev.cancelled;
}

// Matches sub_18002E7F0
void compute_reach_raycast(const events::ReachRaycastQueryEvent* query) noexcept {
    events::GameContext* ctx = query->ctx;
    events::JniContext* jni = ctx->jni;

    const double max_reach = query->reach_distance;
    double block_limit = max_reach + 1.5;

    bool ignore_blocks = modules::piercing_should_ignore_blocks(ctx);
    jobject block_mop = minecraft::living_ray_trace(jni, ctx->local_player, max_reach + 1.5, 1.0f);
    jobject eye_vec_obj = minecraft::living_get_position_eyes(jni, ctx->local_player);
    const math::Vec3d eye_pos = minecraft::vec3_to_vec3d(jni, eye_vec_obj);

    if (!ignore_blocks && block_mop != nullptr) {
        jobject block_hit_vec = minecraft::mop_get_hit_vec(jni, block_mop);
        const math::Vec3d block_hit_pos = minecraft::vec3_to_vec3d(jni, block_hit_vec);
        block_limit = math::vec3_dist(eye_pos, block_hit_pos);
    }

    const math::Vec3d look_vec = minecraft::entity_get_look_vector(jni, ctx->local_player);
    const math::Vec3d ray_end = math::vec3_add(eye_pos, math::vec3_scale(look_vec, block_limit));
    jobject ray_end_obj = minecraft::vec3_new(jni, ray_end);

    jobject entity_list = minecraft::world_get_loaded_entity_list(jni, ctx->world);
    const int32_t entity_count = minecraft::java_list_size(jni, entity_list);

    bool block_hit_only = false;
    if (block_mop != nullptr) {
        block_hit_only = (minecraft::mop_get_type_ordinal(jni, block_mop) == 1);
    }

    if (entity_count <= 0) {
        if (block_hit_only) {
            minecraft::mc_set_object_mouse_over(jni, ctx->minecraft, block_mop);
        }
        return;
    }

    jobject best_entity = nullptr;
    jobject best_hit_vec = nullptr;
    double best_dist = max_reach;
    bool best_inside_box = false;

    for (int32_t i = 0; i < entity_count; ++i) {
        jobject entity = minecraft::java_list_get(jni, entity_list, i);
        if (jni->env->IsSameObject(ctx->local_player, entity) != JNI_FALSE) {
            continue;
        }
        if (!minecraft::entity_can_be_collided_with(jni, entity)) {
            continue;
        }

        jobject raw_bbox_obj = minecraft::entity_get_bounding_box(jni, entity);
        const math::AABB expanded_box = math::aabb_expand(
            minecraft::aabb_to_aabb(jni, raw_bbox_obj),
            query->hitbox_expansion);
        jobject expanded_bbox_obj = minecraft::aabb_new(jni, expanded_box);
        jobject intercept_mop = minecraft::aabb_calculate_intercept(
            jni, expanded_bbox_obj, eye_vec_obj, ray_end_obj);

        if (math::aabb_contains_point(expanded_box, eye_pos)) {
            if (best_dist < 0.0) {
                best_inside_box = true;
            } else if (best_entity != nullptr &&
                       modules::piercing_compare_targets(ctx, best_entity, entity, true) == 1) {
                best_inside_box = true;
            } else {
                best_entity = entity;
                best_hit_vec = (intercept_mop != nullptr)
                    ? minecraft::mop_get_hit_vec(jni, intercept_mop)
                    : eye_vec_obj;
                best_dist = 0.0;
                best_inside_box = true;
            }
        } else if (intercept_mop != nullptr) {
            jobject cand_hit_vec = minecraft::mop_get_hit_vec(jni, intercept_mop);
            const math::Vec3d cand_hit_pos = minecraft::vec3_to_vec3d(jni, cand_hit_vec);
            const double cand_dist = math::vec3_dist(eye_pos, cand_hit_pos);

            const int32_t cmp = modules::piercing_compare_targets(ctx, best_entity, entity, best_inside_box);
            bool accept = false;
            if (cmp == 2) {
                accept = (best_dist == 0.0) ? false : (cand_dist < best_dist || best_entity == nullptr);
                if (best_dist != 0.0 && cand_dist < best_dist) {
                    accept = true;
                } else if (best_dist == max_reach && best_entity == nullptr && cand_dist < max_reach) {
                    accept = true;
                }
            } else if (cmp == 0 && cand_dist < max_reach) {
                accept = true;
            }

            if (accept) {
                best_entity = entity;
                best_hit_vec = cand_hit_vec;
                best_dist = cand_dist;
            }
        } else {
            continue;
        }

        if (best_entity != nullptr) {
            const bool ignored = modules::ghost_hand_should_ignore_entity(ctx, best_entity);
            block_hit_only = ignored && block_hit_only;
        }
    }

    if (block_hit_only) {
        minecraft::mc_set_object_mouse_over(jni, ctx->minecraft, block_mop);
        return;
    }

    if (best_entity != nullptr) {
        const bool within_limit = (best_dist <= block_limit);
        if (ignore_blocks || within_limit || block_mop == nullptr) {
            jobject target_bbox_obj = minecraft::entity_get_bounding_box(jni, best_entity);
            const math::AABB vanilla_box = math::aabb_expand(
                minecraft::aabb_to_aabb(jni, target_bbox_obj),
                0.10000000149011612);
            const math::Vec3d raw_hit_pos = minecraft::vec3_to_vec3d(jni, best_hit_vec);
            const math::Vec3d clamped_hit_pos = math::aabb_closest_point(vanilla_box, raw_hit_pos);
            jobject clamped_vec_obj = minecraft::vec3_new(jni, clamped_hit_pos);
            jobject entity_mop = minecraft::mop_new_entity_hit(jni, best_entity, clamped_vec_obj);
            minecraft::mc_set_object_mouse_over(jni, ctx->minecraft, entity_mop);
        }
    }
}

} // namespace slinky::hooks
