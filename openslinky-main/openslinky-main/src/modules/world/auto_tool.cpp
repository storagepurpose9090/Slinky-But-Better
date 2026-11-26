#include "modules/world/auto_tool.hpp"

#include <cmath>
#include <cstdint>

#include "config/settings.hpp"
#include "events/events.hpp"
#include "gui/widgets.hpp"
#include "minecraft/game_helpers.hpp"
#include "minecraft/item_database.hpp"
#include "minecraft/mc_wrappers.hpp"
#include "util/containers.hpp"
#include "util/math.hpp"

namespace slinky::modules {

namespace {

void get_block_picker_options(const char* const** out_items, size_t* out_count) noexcept {
    mc::get_all_block_names(out_items, out_count);
}

void on_controller_tick(void* event) noexcept;
void on_client_shutdown(void* event) noexcept;
void render_settings(gui::ClickGui* gui, gui::GuiRenderer* renderer) noexcept;
void visit_config(config::ConfigArchive* archive) noexcept;

struct AutoToolState {
    events::Module base{};

    // Runtime state (@ 0x18030B3D8..0x18030B3DF & 0x180319720..0x180319738)
    int32_t saved_prev_slot = -1;
    int32_t selected_tool_slot = -1;
    int32_t activation_delay_ticks = 0;
    util::PodVector<mc::ItemIdEntry> allowed_blocks_list{};

    // Settings (@ 0x18030B3E0 .. 0x18030B55F)
    config::FloatSetting activation_time{{}, "Activation time", "%.0f ms", 1.0f, 50.0f, 0.0f, 10.0f, 3.0f, 3.0f};
    config::BoolSetting allow_using_fists{{}, "Fists", true, true, {}};
    config::BoolSetting allow_using_sword{{}, "Sword", true, true, {}};
    config::BoolSetting allow_using_tools{{}, "Tool", true, true, {}};
    config::BoolSetting allow_using_other{{}, "Other", true, true, {}};
    config::BoolSetting require_sneak{{}, "Sneaking", false, false, {}};
    config::BoolSetting restrict_allowed_blocks{{}, "Restrict allowed blocks", false, false, {}};
    config::BoolSetting switch_back{{}, "Switch back when done", true, true, {}};
    config::DynamicListSetting allowed_blocks{
        "Allowed blocks",
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

    AutoToolState() noexcept {
        allowed_blocks.ctx = &allowed_blocks_list;

        base.name = "Auto Tool";
        base.description = "Selects the best tool when digging.";
        base.category = events::ModuleCategory::Block;
        base.enabled = false;
        base.flags = events::kModuleFlagNone;
        base.keybind = 0;
        base.hold_mode = false;
        base.events[events::kEventGuiScreenTick] = {on_controller_tick, 0, 0};
        base.events[events::kEventClientShutdown] = {on_client_shutdown, 0, 0};
        base.render_settings = render_settings;
        base.visit_config = visit_config;
        base.format_hud_tag = nullptr;
        base.reserved_vfunc0 = ~uint64_t{0};
    }
};

AutoToolState g_auto_tool{};

bool is_held_item_allowed(events::JniContext* jni, jobject held_item) noexcept {
    if (held_item == nullptr) {
        return g_auto_tool.allow_using_fists.value;
    }
    switch (mc::item_get_id(jni, held_item)) {
        // Shovels, pickaxes, axes (iron, wood, stone, diamond, gold) + shears (359)
        case 256:
        case 257:
        case 258:
        case 269:
        case 270:
        case 271:
        case 273:
        case 274:
        case 275:
        case 277:
        case 278:
        case 279:
        case 284:
        case 285:
        case 286:
        case 359:
            return g_auto_tool.allow_using_tools.value;

        // Swords (iron, wood, stone, diamond, gold)
        case 267:
        case 268:
        case 272:
        case 276:
        case 283:
            return g_auto_tool.allow_using_sword.value;

        default:
            return g_auto_tool.allow_using_other.value;
    }
}

// Evaluates hotbar tools while digging in PlayerControllerMP.updateController (sub_1800588F0)
void on_controller_tick(void* event) noexcept {
    events::GameContext* ctx = static_cast<events::GuiScreenTickEvent*>(event)->ctx;
    if (!ctx->in_game_focus) {
        return;
    }

    events::JniContext* jni = ctx->jni;
    jobject inv = mc::player_get_inventory(jni, ctx->local_player);
    const int32_t current_slot = mc::inv_player_get_current_item(jni, inv);

    const auto release_and_restore = [&]() noexcept {
        mc::release_hotbar_lock(&g_auto_tool.base);
        if (g_auto_tool.saved_prev_slot != -1) {
            mc::inv_player_set_current_item(jni, inv, g_auto_tool.saved_prev_slot);
            g_auto_tool.saved_prev_slot = -1;
        }
    };

    if (g_auto_tool.saved_prev_slot != -1 && g_auto_tool.selected_tool_slot != current_slot) {
        g_auto_tool.saved_prev_slot = -1;
        release_and_restore();
        return;
    }

    if (g_auto_tool.saved_prev_slot == -1) {
        jobject held_item = mc::player_get_held_item(jni, ctx->local_player);
        if (!is_held_item_allowed(jni, held_item)) {
            g_auto_tool.activation_delay_ticks = static_cast<int32_t>(g_auto_tool.activation_time.value);
            release_and_restore();
            return;
        }
    }

    jobject settings = mc::mc_get_game_settings(jni, ctx->minecraft);
    jobject block = nullptr;

    bool digging_valid = !mc::player_is_using_item(jni, ctx->local_player);
    if (digging_valid) {
        jobject attack_key = mc::settings_get_key_attack(jni, settings);
        digging_valid = mc::key_binding_is_physically_down(jni, attack_key);
    }
    if (digging_valid && g_auto_tool.require_sneak.value) {
        jobject sneak_key = mc::settings_get_key_sneak(jni, settings);
        digging_valid = mc::key_binding_is_pressed(jni, sneak_key);
    }
    if (digging_valid) {
        jobject mop = mc::mc_get_object_mouse_over(jni, ctx->minecraft);
        if (mop == nullptr || mc::mop_get_type_ordinal(jni, mop) != 1) {
            digging_valid = false;
        } else {
            const math::Vec3d pos = mc::mop_get_block_pos_vec3d(jni, mop);
            block = mc::world_get_block(
                jni,
                ctx->world,
                static_cast<int32_t>(pos.x),
                static_cast<int32_t>(pos.y),
                static_cast<int32_t>(pos.z));
            if (block == nullptr) {
                digging_valid = false;
            } else if (g_auto_tool.restrict_allowed_blocks.value) {
                const int32_t block_id = mc::block_get_id(jni, block);
                digging_valid = mc::item_filter_list_contains_id(&g_auto_tool.allowed_blocks_list, block_id);
            }
        }
    }

    if (!digging_valid) {
        g_auto_tool.activation_delay_ticks = static_cast<int32_t>(g_auto_tool.activation_time.value);
        release_and_restore();
        return;
    }

    if (g_auto_tool.activation_delay_ticks > 0) {
        --g_auto_tool.activation_delay_ticks;
        release_and_restore();
        return;
    }

    float best_strength = 1.0f;
    int32_t best_slot = current_slot;
    for (int32_t offset = 0; offset < 9; ++offset) {
        const int32_t slot = (current_slot + offset) % 9;
        jobject stack = mc::inventory_get_stack_in_slot(jni, inv, slot);
        if (stack != nullptr) {
            const float strength = mc::item_stack_get_str_vs_block(jni, stack, block);
            if (strength > best_strength) {
                best_strength = strength;
                best_slot = slot;
            }
        }
    }

    if (best_slot != current_slot) {
        if (mc::try_acquire_hotbar_lock(&g_auto_tool.base)) {
            mc::inv_player_set_current_item(jni, inv, best_slot);
            g_auto_tool.selected_tool_slot = best_slot;
            if (g_auto_tool.saved_prev_slot == -1 && g_auto_tool.switch_back.value) {
                g_auto_tool.saved_prev_slot = current_slot;
            }
        }
        if (best_slot == -1) {
            release_and_restore();
        }
    }
}

// Frees dynamic allowed_blocks list on unload (sub_180058C20)
void on_client_shutdown(void* /*event*/) noexcept {
    g_auto_tool.allowed_blocks_list.clear_and_free();
}

// Renders Auto Tool settings in ClickGUI (sub_180058C50)
void render_settings(gui::ClickGui* gui, gui::GuiRenderer* renderer) noexcept {
    gui::widgets::render_section_header(gui, renderer, "Main");
    g_auto_tool.activation_time.render(gui, renderer);
    g_auto_tool.switch_back.render(gui, renderer);

    gui::widgets::render_section_header(gui, renderer, "Allow while using");
    g_auto_tool.allow_using_sword.render(gui, renderer);
    g_auto_tool.allow_using_tools.render(gui, renderer);
    g_auto_tool.allow_using_fists.render(gui, renderer);
    g_auto_tool.allow_using_other.render(gui, renderer);

    gui::widgets::render_section_header(gui, renderer, "Conditions");
    g_auto_tool.require_sneak.render(gui, renderer);
    g_auto_tool.restrict_allowed_blocks.render(gui, renderer);
    if (g_auto_tool.restrict_allowed_blocks.value) {
        g_auto_tool.allowed_blocks.render(gui, renderer);
    }
}

// Serializes/deserializes Auto Tool config (sub_180058D50)
void visit_config(config::ConfigArchive* archive) noexcept {
    g_auto_tool.activation_time.visit(archive, "activation_time");
    g_auto_tool.switch_back.visit(archive, "switch_back");
    g_auto_tool.allow_using_sword.visit(archive, "allow_using_sword");
    g_auto_tool.allow_using_tools.visit(archive, "allow_using_tools");
    g_auto_tool.allow_using_fists.visit(archive, "allow_using_fists");
    g_auto_tool.allow_using_other.visit(archive, "allow_using_other");
    g_auto_tool.require_sneak.visit(archive, "require_sneak");
    g_auto_tool.restrict_allowed_blocks.visit(archive, "restrict_allowed_blocks");
    g_auto_tool.allowed_blocks.visit(archive, "allowed_blocks");
}

} // namespace

events::Module* get_auto_tool_module() noexcept {
    return &g_auto_tool.base;
}

} // namespace slinky::modules
