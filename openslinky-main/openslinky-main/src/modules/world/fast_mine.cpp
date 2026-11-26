#include "modules/world/fast_mine.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>

#include "config/settings.hpp"
#include "events/events.hpp"
#include "gui/widgets.hpp"
#include "minecraft/game_helpers.hpp"
#include "minecraft/item_database.hpp"
#include "minecraft/mc_wrappers.hpp"
#include "util/containers.hpp"

namespace slinky::modules {

namespace {

void get_block_picker_options(const char* const** out_items, size_t* out_count) noexcept {
    mc::get_all_block_names(out_items, out_count);
}

void on_game_tick(void* event) noexcept;
void on_post_living_update(void* event) noexcept;
void render_settings(gui::ClickGui* gui, gui::GuiRenderer* renderer) noexcept;
void visit_config(config::ConfigArchive* archive) noexcept;
int format_hud_tag(char* out_buf_32) noexcept;

struct FastMineState {
    events::Module base{};

    // Runtime state (@ 0x1803215A0 .. 0x1803215BF)
    int32_t prev_block_hit_delay = 0;
    float pre_tick_block_damage = 0.0f;
    util::PodVector<mc::ItemIdEntry> ignored_blocks_list{};

    // Settings (@ 0x18030F030 .. 0x18030F127)
    config::FloatSetting break_delay{{}, "Break delay", "%.0f ms", 1.0f, 50.0f, 0.0f, 5.0f, 5.0f, 5.0f};
    config::BoolSetting decr_block_hit_delay{{}, "Always decrease break delay", false, false, {}};
    config::FloatSetting speed{{}, "Speed", "%.2fx", 0.01f, 0.0f, 1.0f, 2.0f, 1.0f, 1.0f};
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

    FastMineState() noexcept {
        ignored_blocks.ctx = &ignored_blocks_list;

        base.name = "Fast Mine";
        base.description = "Increases block mining speed.";
        base.category = events::ModuleCategory::Block;
        base.enabled = false;
        base.flags = events::kModuleFlagNone;
        base.keybind = 0;
        base.hold_mode = false;
        base.events[events::kEventGameTick] = {on_game_tick, 0, 0};
        base.events[events::kEventPostLivingUpdate] = {on_post_living_update, 0, 0};
        base.render_settings = render_settings;
        base.visit_config = visit_config;
        base.format_hud_tag = format_hud_tag;
    }
};

FastMineState g_fast_mine{};

// Caps blockHitDelay and records pre-tick block damage (sub_180062E90)
void on_game_tick(void* event) noexcept {
    events::GameContext* ctx = static_cast<events::ContextEvent*>(event)->ctx;
    events::JniContext* jni = ctx->jni;

    jobject controller = mc::mc_get_player_controller(jni, ctx->minecraft);
    if (controller == nullptr) {
        g_fast_mine.prev_block_hit_delay = 0;
        return;
    }

    if (ctx->in_game_focus) {
        jobject mop = mc::mc_get_object_mouse_over(jni, ctx->minecraft);
        if (mop != nullptr && mc::mop_get_type_ordinal(jni, mop) == 1) {
            const mc::Vec3i pos = mc::mop_get_block_pos_vec3i(jni, mop);
            jobject block = mc::world_get_block(jni, ctx->world, pos.x, pos.y, pos.z);
            if (block != nullptr) {
                const int32_t block_id = mc::block_get_id(jni, block);
                if (!mc::item_filter_list_contains_id(&g_fast_mine.ignored_blocks_list, block_id)) {
                    g_fast_mine.pre_tick_block_damage = mc::controller_get_cur_block_damage(jni, controller);

                    const int32_t hit_delay = mc::controller_get_block_hit_delay(jni, controller);
                    if (static_cast<float>(hit_delay) > g_fast_mine.break_delay.value) {
                        mc::controller_set_block_hit_delay(
                            jni, controller, static_cast<int32_t>(g_fast_mine.break_delay.value));
                    } else if (hit_delay > 0 &&
                               g_fast_mine.decr_block_hit_delay.value &&
                               hit_delay == g_fast_mine.prev_block_hit_delay) {
                        mc::controller_set_block_hit_delay(jni, controller, hit_delay - 1);
                    }

                    g_fast_mine.prev_block_hit_delay = hit_delay;
                    return;
                }
            }
        }
    }

    g_fast_mine.pre_tick_block_damage = -1.0f;
}

// Advances initial curBlockDamageMP to achieve exact ceil-quantized tick speedup (sub_180062FD0)
void on_post_living_update(void* event) noexcept {
    events::GameContext* ctx = static_cast<events::ContextEvent*>(event)->ctx;
    events::JniContext* jni = ctx->jni;

    jobject controller = mc::mc_get_player_controller(jni, ctx->minecraft);
    if (controller == nullptr) {
        return;
    }

    const float cur_damage = mc::controller_get_cur_block_damage(jni, controller);
    if (cur_damage != 0.0f && g_fast_mine.pre_tick_block_damage == 0.0f) {
        const int64_t vanilla_ticks = static_cast<int64_t>(std::ceil(1.0f / cur_damage));
        const float boosted_ticks_f = static_cast<float>(
            (1.0 / static_cast<double>(g_fast_mine.speed.value)) / static_cast<double>(cur_damage));
        const int64_t boosted_ticks = static_cast<int64_t>(std::ceil(boosted_ticks_f));
        const int32_t multiplier_ticks =
            static_cast<int32_t>(vanilla_ticks) - static_cast<int32_t>(boosted_ticks) + 1;
        mc::controller_set_cur_block_damage(
            jni, controller, static_cast<float>(multiplier_ticks) * cur_damage);
    }
}

// Renders Fast Mine settings in ClickGUI (sub_1800630C0)
void render_settings(gui::ClickGui* gui, gui::GuiRenderer* renderer) noexcept {
    gui::widgets::render_section_header(gui, renderer, "Main");
    g_fast_mine.speed.render(gui, renderer);
    g_fast_mine.break_delay.render(gui, renderer);
    g_fast_mine.decr_block_hit_delay.render(gui, renderer);
    g_fast_mine.ignored_blocks.render(gui, renderer);
}

// Serializes/deserializes Fast Mine config (sub_180063130)
void visit_config(config::ConfigArchive* archive) noexcept {
    g_fast_mine.speed.visit(archive, "speed");
    g_fast_mine.break_delay.visit(archive, "break_delay");
    g_fast_mine.decr_block_hit_delay.visit(archive, "decr_block_hit_delay");
    g_fast_mine.ignored_blocks.visit(archive, "ignored_blocks");
}

// Formats HUD overlay tag showing mining speed multiplier (sub_1800631A0)
int format_hud_tag(char* out_buf_32) noexcept {
    return std::snprintf(out_buf_32, 32, "%.2fx", static_cast<double>(g_fast_mine.speed.value));
}

} // namespace

events::Module* get_fast_mine_module() noexcept {
    return &g_fast_mine.base;
}

} // namespace slinky::modules
