#include "modules/combat/weapons.hpp"

#include <cstddef>
#include <cstdint>

#include "config/settings.hpp"
#include "gui/widgets.hpp"
#include "minecraft/game_helpers.hpp"
#include "minecraft/item_database.hpp"
#include "minecraft/mc_wrappers.hpp"
#include "modules/module_interop.hpp"
#include "util/containers.hpp"

namespace slinky::modules {

namespace {

constexpr uint32_t kWeaponEnchantSharpness  = 1u << 0; // Enchantment ID 16
constexpr uint32_t kWeaponEnchantKnockback  = 1u << 1; // Enchantment ID 19
constexpr uint32_t kWeaponEnchantFireAspect = 1u << 2; // Enchantment ID 20

// Item IDs relative to 258 (0..28 -> 258..286):
// Swords: 267 (iron), 268 (wood), 272 (stone), 276 (diamond), 283 (gold) -> 0x02044600 (33834496)
// Axes:   258 (iron), 271 (wood), 275 (stone), 279 (diamond), 286 (gold) -> 0x10222001 (270671873)
constexpr uint32_t kSwordItemBitmask = 33834496u;
constexpr uint32_t kAxeItemBitmask   = 270671873u;

util::PodVector<mc::ItemIdEntry> s_extra_items{};
int32_t s_cached_ticks_existed = 0;
bool s_cached_is_holding_weapon = false;
uint32_t s_cached_enchant_flags = 0;

void get_extra_item_picker_options(const char* const** out_items, size_t* out_count) noexcept {
    mc::get_all_item_names(out_items, out_count);
}

void on_client_shutdown(void* event) noexcept;
void render_settings(gui::ClickGui* gui, gui::GuiRenderer* renderer) noexcept;
void visit_config(config::ConfigArchive* ar) noexcept;

struct WeaponsModule {
    events::Module base{};
    config::BoolSetting fist{{}, "Fist", false, false};
    config::BoolSetting hotbar_slots[9]{
        {{}, "Slot 1", false, false},
        {{}, "Slot 2", false, false},
        {{}, "Slot 3", false, false},
        {{}, "Slot 4", false, false},
        {{}, "Slot 5", false, false},
        {{}, "Slot 6", false, false},
        {{}, "Slot 7", false, false},
        {{}, "Slot 8", false, false},
        {{}, "Slot 9", false, false},
    };
    config::BoolSetting enchant_sharpness{{}, "Sharpness", false, false};
    config::BoolSetting enchant_knockback{{}, "Knockback", false, false};
    config::BoolSetting enchant_fire_aspect{{}, "Fire aspect", false, false};
    config::BoolSetting axes{{}, "Axes", false, false};
    config::BoolSetting swords{{}, "Swords", true, true};
    config::DynamicListSetting extra_items{
        "Extra",
        "Search item",
        &mc::validate_item_name_input,
        config::DynamicListItemKind::StringOnly,
        0,
        &s_extra_items,
        &mc::item_filter_list_count,
        &mc::item_filter_list_get_name,
        nullptr,
        &mc::item_filter_list_add,
        &mc::item_filter_list_remove,
        &mc::item_filter_list_clear,
        &get_extra_item_picker_options,
        false,
    };

    WeaponsModule() noexcept {
        base.name = "Weapons";
        base.description = "Configure which items are considered weapons.";
        base.category = events::ModuleCategory::Combat;
        base.enabled = false;
        base.flags = events::kModuleFlagNoKeybind |
                     events::kModuleFlagNoToggleSwitch |
                     events::kModuleFlagHiddenFromHud;
        base.keybind = 0;
        base.hold_mode = false;
        base.events[events::kEventClientShutdown] = {&on_client_shutdown, 0};
        base.render_settings = &render_settings;
        base.visit_config = &visit_config;
        base.format_hud_tag = nullptr;
    }
};

WeaponsModule s_mod{};

// sub_1800596E0
uint32_t get_stack_enchant_flags(events::JniContext* jni, jobject stack) noexcept {
    jobject tag_list = mc::item_stack_get_enchantment_tag_list(jni, stack);
    uint32_t flags = 0;
    if (tag_list != nullptr) {
        JNIEnv* env = jni->env;
        jstring id_key = env->NewStringUTF("id");
        if (id_key != nullptr) {
            if (env->NewStringUTF("lvl") != nullptr) {
                const int32_t count = mc::nbt_tag_list_count(jni, tag_list);
                for (int32_t i = 0; i < count; ++i) {
                    jobject compound = mc::nbt_tag_list_get_compound_at(jni, tag_list, i);
                    const int16_t ench_id = mc::nbt_tag_compound_get_short(jni, compound, id_key);
                    switch (ench_id) {
                        case 16:
                            flags |= kWeaponEnchantSharpness;
                            break;
                        case 19:
                            flags |= kWeaponEnchantKnockback;
                            break;
                        case 20:
                            flags |= kWeaponEnchantFireAspect;
                            break;
                        default:
                            break;
                    }
                }
            }
        }
    }
    return flags;
}

// sub_1800597C0
bool is_configured_weapon_item(events::JniContext* jni, jobject item) noexcept {
    if (item == nullptr) {
        return s_mod.fist.value;
    }
    const int32_t item_id = mc::item_get_id(jni, item);
    const uint32_t rel = static_cast<uint32_t>(item_id - 258);
    if (rel <= 0x1Cu) {
        if (((kSwordItemBitmask >> rel) & 1u) != 0u) {
            return s_mod.swords.value;
        }
        if (((kAxeItemBitmask >> rel) & 1u) != 0u) {
            return s_mod.axes.value;
        }
    }
    if (item_id != 0) {
        return mc::item_filter_list_contains_id(&s_extra_items, item_id);
    }
    return s_mod.fist.value;
}

// sub_180059910
void on_client_shutdown(void* /*event*/) noexcept {
    s_extra_items.clear_and_free();
}

// sub_180059940
void render_settings(gui::ClickGui* gui, gui::GuiRenderer* renderer) noexcept {
    gui::widgets::render_section_header(gui, renderer, "Item types");
    s_mod.fist.render(gui, renderer);
    s_mod.swords.render(gui, renderer);
    s_mod.axes.render(gui, renderer);
    s_mod.extra_items.render(gui, renderer);

    gui::widgets::render_section_header(gui, renderer, "Any item with");
    s_mod.enchant_sharpness.render(gui, renderer);
    s_mod.enchant_knockback.render(gui, renderer);
    s_mod.enchant_fire_aspect.render(gui, renderer);

    gui::widgets::render_section_header(gui, renderer, "Any item in");
    for (auto& slot_setting : s_mod.hotbar_slots) {
        slot_setting.render(gui, renderer);
    }
}

// sub_180059AB0
void visit_config(config::ConfigArchive* ar) noexcept {
    s_mod.fist.visit(ar, "fist");
    s_mod.swords.visit(ar, "swords");
    s_mod.axes.visit(ar, "axes");
    s_mod.extra_items.visit(ar, "extra_items");
    s_mod.enchant_sharpness.visit(ar, "enchant_sharpness");
    s_mod.enchant_knockback.visit(ar, "enchant_knockback");
    s_mod.enchant_fire_aspect.visit(ar, "enchant_fire_aspect");

    char slot_key[2] = {'1', '\0'};
    for (int i = 0; i < 9; ++i) {
        slot_key[0] = static_cast<char>('1' + i);
        s_mod.hotbar_slots[i].visit(ar, slot_key);
    }
}

} // namespace

// sub_1800595D0
bool weapons_is_holding_weapon(
    events::JniContext* jni,
    jobject player,
    uint32_t* out_weapon_flags) noexcept
{
    const int32_t ticks = mc::entity_get_ticks_existed(jni, player);
    if (ticks != s_cached_ticks_existed) {
        s_cached_ticks_existed = ticks;
        s_cached_enchant_flags = 0;

        jobject inv = mc::player_get_inventory(jni, player);
        const int32_t slot = mc::inv_player_get_current_item(jni, inv);
        jobject stack = mc::inventory_get_stack_in_slot(jni, inv, slot);
        if (stack != nullptr) {
            const uint32_t ench_flags = get_stack_enchant_flags(jni, stack);
            s_cached_enchant_flags = ench_flags;

            bool holding = true;
            const bool slot_forced = (slot >= 0 && slot < 9) && s_mod.hotbar_slots[slot].value;
            if (!slot_forced &&
                ((ench_flags & kWeaponEnchantSharpness) == 0 || !s_mod.enchant_sharpness.value) &&
                ((ench_flags & kWeaponEnchantKnockback) == 0 || !s_mod.enchant_knockback.value) &&
                (ench_flags < kWeaponEnchantFireAspect || !s_mod.enchant_fire_aspect.value))
            {
                jobject item = mc::item_stack_get_item(jni, stack);
                holding = is_configured_weapon_item(jni, item);
            }
            s_cached_is_holding_weapon = holding;
        } else {
            s_cached_is_holding_weapon = s_mod.fist.value;
        }
    }

    if (out_weapon_flags != nullptr) {
        *out_weapon_flags = s_cached_enchant_flags;
    }
    return s_cached_is_holding_weapon;
}

// sub_180059830
bool weapons_is_holding_weapon_simple(events::JniContext* jni, jobject player) noexcept {
    return weapons_is_holding_weapon(jni, player, nullptr);
}

// sub_180059840
int32_t weapons_find_hotbar_weapon_slot(events::GameContext* ctx, jobject inventory) noexcept {
    for (int32_t slot = 0; slot < 9; ++slot) {
        if (s_mod.hotbar_slots[slot].value) {
            return slot;
        }
        jobject stack = mc::inventory_get_stack_in_slot(ctx->jni, inventory, slot);
        if (stack != nullptr) {
            const uint32_t ench_flags = get_stack_enchant_flags(ctx->jni, stack);
            if (((ench_flags & kWeaponEnchantSharpness) != 0 && s_mod.enchant_sharpness.value) ||
                ((ench_flags & kWeaponEnchantKnockback) != 0 && s_mod.enchant_knockback.value) ||
                (ench_flags >= kWeaponEnchantFireAspect && s_mod.enchant_fire_aspect.value))
            {
                return slot;
            }
            jobject item = mc::item_stack_get_item(ctx->jni, stack);
            if (is_configured_weapon_item(ctx->jni, item)) {
                return slot;
            }
        }
    }
    return -1;
}

events::Module* get_weapons_module() noexcept {
    return &s_mod.base;
}

} // namespace slinky::modules
