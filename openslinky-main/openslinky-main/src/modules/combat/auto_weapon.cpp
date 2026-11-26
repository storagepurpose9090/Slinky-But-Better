#include "modules/combat/auto_weapon.hpp"

#include <cstdint>

#include "config/settings.hpp"
#include "gui/widgets.hpp"
#include "minecraft/game_helpers.hpp"
#include "minecraft/mc_wrappers.hpp"
#include "modules/module_interop.hpp"

namespace slinky::modules {

namespace {

int32_t s_aim_ticks = 0;

void on_gui_screen_tick(void* event) noexcept;
void render_settings(gui::ClickGui* gui, gui::GuiRenderer* renderer) noexcept;
void visit_config(config::ConfigArchive* ar) noexcept;

struct AutoWeaponModule {
    events::Module base{};
    config::FloatSetting activation_time{{}, "Activation time", "%.0f ms", 1.0f, 50.0f, 0.0f, 5.0f, 2.0f, 2.0f};
    config::BoolSetting require_mouse_pressed{{}, "Mouse pressed", false, false};

    AutoWeaponModule() noexcept {
        base.name = "Auto Weapon";
        base.description = "Selects your weapon when aiming on players.";
        base.category = events::ModuleCategory::Combat;
        base.enabled = false;
        base.flags = events::kModuleFlagNone;
        base.keybind = 0;
        base.hold_mode = false;
        base.events[events::kEventGuiScreenTick] = {&on_gui_screen_tick, 0};
        base.render_settings = &render_settings;
        base.visit_config = &visit_config;
        base.format_hud_tag = nullptr;
    }
};

AutoWeaponModule s_mod{};

// sub_18006C170
void on_gui_screen_tick(void* event) noexcept {
    const auto* ev = static_cast<const events::GuiScreenTickEvent*>(event);
    events::GameContext* ctx = ev->ctx;

    jobject inv = nullptr;
    int32_t cur_slot = -1;
    int32_t weapon_slot = -1;
    jobject mop = nullptr;
    jobject hit_player = nullptr;

    if (!ctx->in_game_focus ||
        mc::player_is_using_item(ctx->jni, ctx->local_player) ||
        (s_mod.require_mouse_pressed.value && !mc::is_attack_key_down(ctx)) ||
        (mop = mc::mc_get_object_mouse_over(ctx->jni, ctx->minecraft)) == nullptr ||
        mc::mop_get_type_ordinal(ctx->jni, mop) != 2 ||
        (hit_player = mc::as_entity_player(ctx->jni, mc::mop_get_entity_hit(ctx->jni, mop))) == nullptr ||
        mc::player_has_tag_flags(ctx->jni, hit_player, mc::kPlayerTagExcludedMask) ||
        weapons_is_holding_weapon_simple(ctx->jni, ctx->local_player) ||
        (inv = mc::player_get_inventory(ctx->jni, ctx->local_player),
         cur_slot = mc::inv_player_get_current_item(ctx->jni, inv),
         weapon_slot = weapons_find_hotbar_weapon_slot(ctx, inv),
         weapon_slot == -1) ||
        cur_slot == weapon_slot)
    {
        s_aim_ticks = 0;
        return;
    }

    if (s_mod.activation_time.value <= static_cast<float>(s_aim_ticks)) {
        if (mc::try_acquire_hotbar_lock(&s_mod.base)) {
            mc::inv_player_set_current_item(ctx->jni, inv, weapon_slot);
            mc::release_hotbar_lock(&s_mod.base);
        }
    } else {
        ++s_aim_ticks;
    }
}

// sub_18006C2C0
void render_settings(gui::ClickGui* gui, gui::GuiRenderer* renderer) noexcept {
    gui::widgets::render_section_header(gui, renderer, "Main");
    s_mod.activation_time.render(gui, renderer);

    gui::widgets::render_section_header(gui, renderer, "Conditions");
    s_mod.require_mouse_pressed.render(gui, renderer);
}

// sub_18006C320
void visit_config(config::ConfigArchive* ar) noexcept {
    s_mod.activation_time.visit(ar, "activation_time");
    s_mod.require_mouse_pressed.visit(ar, "require_mouse_pressed");
}

} // namespace

events::Module* get_auto_weapon_module() noexcept {
    return &s_mod.base;
}

} // namespace slinky::modules
