#include "modules/world/fast_place.hpp"

#include <cstdint>
#include <cstdio>

#include "config/settings.hpp"
#include "events/events.hpp"
#include "gui/widgets.hpp"
#include "minecraft/game_helpers.hpp"
#include "minecraft/item_database.hpp"
#include "minecraft/mc_wrappers.hpp"
#include "util/containers.hpp"
#include "util/math.hpp"
#include "util/random.hpp"

namespace slinky::modules {

namespace {

void get_block_picker_options(const char* const** out_items, size_t* out_count) noexcept {
    mc::get_all_block_names(out_items, out_count);
}

void on_game_tick(void* event) noexcept;
void render_settings(gui::ClickGui* gui, gui::GuiRenderer* renderer) noexcept;
void visit_config(config::ConfigArchive* archive) noexcept;
int format_hud_tag(char* out_buf_32) noexcept;

struct FastPlaceState {
    events::Module base{};

    // Runtime state (@ 0x180316F50 .. 0x180316F80)
    float place_cooldown_ticks = 0.0f;
    bool was_use_key_down = false;
    int32_t activate_delay_ticks = 0;
    util::PodVector<mc::ItemIdEntry> ignored_blocks_list{};
    random::DriftNoise extra_click_gate{};

    // Settings (@ 0x1803074E0 .. 0x1803075B7)
    config::FloatSetting max_cps{{}, "Maximum CPS", "%0.1f CPS", 0.1f, 0.0f, 10.0f, 50.0f, 20.0f, 20.0f};
    config::FloatSetting activate_delay{{}, "Activate delay", "%0.0f ms", 1.0f, 50.0f, 0.0f, 10.0f, 0.0f, 0.0f};
    config::DynamicListSetting ignored_blocks{
        "Ignored blocks",
        "Search item",
        mc::validate_item_name_input,
        config::DynamicListItemKind::StringOnly,
        0,
        nullptr,
        mc::item_filter_list_count,
        mc::item_filter_list_get_name,
        nullptr,
        mc::item_filter_list_add,
        mc::item_filter_list_remove,
        mc::item_filter_list_clear,
        get_block_picker_options,
        false,
        {},
    };

    FastPlaceState() noexcept {
        ignored_blocks.ctx = &ignored_blocks_list;

        base.name = "Fast Place";
        base.description = "Allows placing blocks faster.";
        base.category = events::ModuleCategory::Block;
        base.enabled = false;
        base.flags = events::kModuleFlagNone;
        base.keybind = 0;
        base.hold_mode = false;
        base.events[events::kEventGameTick] = {on_game_tick, -1000, 0};
        base.render_settings = render_settings;
        base.visit_config = visit_config;
        base.format_hud_tag = format_hud_tag;
    }
};

FastPlaceState g_fast_place{};

// High-frequency humanized block placement on game tick (sub_180051260)
void on_game_tick(void* event) noexcept {
    if (g_fast_place.place_cooldown_ticks > 0.0f) {
        g_fast_place.place_cooldown_ticks -= 1.0f;
        if (g_fast_place.place_cooldown_ticks > 0.0f) {
            return;
        }
    }

    events::GameContext* ctx = static_cast<events::ContextEvent*>(event)->ctx;
    if (!ctx->in_game_focus) {
        return;
    }

    events::JniContext* jni = ctx->jni;
    jobject settings = mc::mc_get_game_settings(jni, ctx->minecraft);
    jobject use_key = mc::settings_get_key_use_item(jni, settings);
    const bool use_down = mc::key_binding_is_physically_down(jni, use_key);
    const bool was_down = g_fast_place.was_use_key_down;
    g_fast_place.was_use_key_down = use_down;

    if (!use_down) {
        g_fast_place.activate_delay_ticks = static_cast<int32_t>(g_fast_place.activate_delay.value);
        return;
    }
    if (g_fast_place.activate_delay_ticks > 0) {
        --g_fast_place.activate_delay_ticks;
        return;
    }

    if (mc::key_binding_get_press_time(jni, use_key) > 0 || g_fast_place.place_cooldown_ticks > 0.0f) {
        return;
    }

    bool skip_initial_subtick = !was_down;
    do {
        g_fast_place.place_cooldown_ticks = static_cast<float>(
            static_cast<double>(g_fast_place.place_cooldown_ticks) +
            20.0 / static_cast<double>(g_fast_place.max_cps.value));

        if (!skip_initial_subtick && ctx->in_game_focus) {
            jobject held_item = mc::player_get_held_item(jni, ctx->local_player);
            if (mc::is_block_item(jni, held_item)) {
                bool should_click = false;

                jobject mop = mc::mc_get_object_mouse_over(jni, ctx->minecraft);
                if (mop != nullptr && mc::mop_get_type_ordinal(jni, mop) == 1) {
                    jobject cur_item = mc::player_get_held_item(jni, ctx->local_player);
                    if (cur_item != nullptr && mc::item_get_id(jni, cur_item) == 65) {
                        // Ladder (ID 65) has non-blocking collision box
                        should_click = true;
                    } else {
                        const math::Vec3d offset_pos = mc::mop_get_offset_block_pos_vec3d(jni, mop);
                        const math::Vec3d offset_max = math::vec3_add(offset_pos, math::make_vec3(1.0, 1.0, 1.0));
                        const math::AABB block_box = math::make_aabb(offset_pos, offset_max);
                        const math::AABB player_box =
                            mc::aabb_to_aabb(jni, mc::entity_get_bounding_box(jni, ctx->local_player));
                        if (!math::aabb_intersects(block_box, player_box)) {
                            should_click = true;
                        }
                    }
                }

                if (!should_click) {
                    should_click = g_fast_place.extra_click_gate.sample_chance_timed({0.02f, 0.08f}, 20.0);
                }

                if (should_click) {
                    const int32_t item_id = mc::item_get_id(jni, held_item);
                    if (!mc::item_filter_list_contains_id(&g_fast_place.ignored_blocks_list, item_id)) {
                        mc::key_binding_increment_press_time(jni, use_key);
                    }
                }
            }
        }

        skip_initial_subtick = false;
    } while (g_fast_place.place_cooldown_ticks <= 0.0f);
}

// Renders Fast Place settings in ClickGUI (sub_1800515C0)
void render_settings(gui::ClickGui* gui, gui::GuiRenderer* renderer) noexcept {
    gui::widgets::render_section_header(gui, renderer, "Main");
    g_fast_place.max_cps.render(gui, renderer);
    g_fast_place.activate_delay.render(gui, renderer);
    g_fast_place.ignored_blocks.render(gui, renderer);
}

// Serializes/deserializes Fast Place config (sub_180051620)
void visit_config(config::ConfigArchive* archive) noexcept {
    g_fast_place.max_cps.visit(archive, "max_cps");
    g_fast_place.activate_delay.visit(archive, "activate_delay");
    g_fast_place.ignored_blocks.visit(archive, "ignored_blocks");
}

// Formats HUD overlay tag showing max CPS (sub_180051680)
int format_hud_tag(char* out_buf_32) noexcept {
    return std::snprintf(out_buf_32, 32, "%.1f", static_cast<double>(g_fast_place.max_cps.value));
}

} // namespace

events::Module* get_fast_place_module() noexcept {
    return &g_fast_place.base;
}

} // namespace slinky::modules
