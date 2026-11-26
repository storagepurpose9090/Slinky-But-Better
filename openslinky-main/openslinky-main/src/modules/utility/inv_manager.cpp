#include "modules/utility/inv_manager.hpp"
#include "modules/module_interop.hpp"

#include <algorithm>
#include <cmath>

#include "config/settings.hpp"
#include "gui/widgets.hpp"
#include "minecraft/game_helpers.hpp"
#include "minecraft/mc_wrappers.hpp"
#include "util/math.hpp"
#include "util/random.hpp"

namespace slinky::modules {

namespace {

// Armor protection points [material][piece] (byte_180172300)
// Materials: 0=Leather, 1=Gold, 2=Chain, 3=Iron, 4=Diamond
// Pieces:    0=Helmet,  1=Chestplate, 2=Leggings, 3=Boots
constexpr uint8_t kArmorProtectionPoints[5][4] = {
    {1, 3, 2, 1},
    {2, 5, 3, 1},
    {2, 5, 4, 1},
    {2, 6, 5, 2},
    {3, 8, 6, 3},
};

// Base ID offset for armor IDs 298..317 (dword_180172620)
constexpr int32_t kArmorIdBaseOffset[20] = {
    -298, -298, -298, -298,
    -302, -302, -302, -302,
    -306, -306, -306, -306,
    -310, -310, -310, -310,
    -314, -314, -314, -314,
};

// Material index for armor IDs 298..317 (qword_180172670)
constexpr uint32_t kArmorMaterialIndex[20] = {
    0, 0, 0, 0,
    2, 2, 2, 2,
    3, 3, 3, 3,
    4, 4, 4, 4,
    1, 1, 1, 1,
};

// Tool/sword material tier bonus weight (byte_18017235E)
// Index: 1=Wood/Shears(0), 2=Stone(1), 3=Iron(2), 4=Gold(0), 5=Diamond(3)
constexpr uint8_t kMaterialTierWeight[6] = {0, 0, 1, 2, 0, 3};

// Enchantment ID weight tables (0..62) from 0x180172320, 0x180172370, 0x1801723B0
constexpr uint8_t kWeaponEnchantWeights[63] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    50, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

constexpr uint8_t kBowEnchantWeights[63] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    100, 10, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

constexpr uint8_t kToolEnchantWeights[63] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

constexpr const char* kHotbarCategoryOptions[27] = {
    "None",
    "Axe",
    "Blocks",
    "Bow",
    "Chest",
    "Ender pearl",
    "Fireball",
    "Fishing rod",
    "Flint and steel",
    "Golden apple",
    "Healing potion",
    "Jump potion",
    "Ladders",
    "Lava bucket",
    "Pickaxe",
    "Shears",
    "Shovel",
    "Snowballs/eggs",
    "Soup",
    "Speed potion",
    "Stick",
    "Strength potion",
    "Sword",
    "TNT",
    "Water bucket",
    "Weapon",
    "Wool",
};

struct ToolClassification {
    int32_t category = 0;
    uint32_t tier = 0;
};

struct InvManagerState {
    uint32_t phase = 0;
    uint32_t pad0 = 0;
    uint64_t completed_mask = 0;
    uint32_t click_kind = 0;
    uint8_t secondary_slot = 0;
    uint8_t pad1[3]{};
    uint32_t resume_phase = 0;
    uint8_t sub_index = 0;
    uint8_t target_window_slot = 0;
    bool advance_sub_index = false;
};

config::FloatSetting s_delay_after_open{
    {},
    "Delay after open",
    "%.0f ms",
    0.0f,
    50.0f,
    0.0f,
    10.0f,
    3.0f,
    0.0f,
};

config::BoolSetting s_equip_armor{
    {},
    "Enabled",
    false,
    false,
    {},
};

config::BoolSetting s_sort_hotbar{
    {},
    "Enabled",
    false,
    false,
    {},
};

config::ModeSetting s_hotbar_slots[9] = {
    {{}, "Slot 1", 27, 0, kHotbarCategoryOptions, 0, 0, true, {}},
    {{}, "Slot 2", 27, 0, kHotbarCategoryOptions, 0, 0, true, {}},
    {{}, "Slot 3", 27, 0, kHotbarCategoryOptions, 0, 0, true, {}},
    {{}, "Slot 4", 27, 0, kHotbarCategoryOptions, 0, 0, true, {}},
    {{}, "Slot 5", 27, 0, kHotbarCategoryOptions, 0, 0, true, {}},
    {{}, "Slot 6", 27, 0, kHotbarCategoryOptions, 0, 0, true, {}},
    {{}, "Slot 7", 27, 0, kHotbarCategoryOptions, 0, 0, true, {}},
    {{}, "Slot 8", 27, 0, kHotbarCategoryOptions, 0, 0, true, {}},
    {{}, "Slot 9", 27, 0, kHotbarCategoryOptions, 0, 0, true, {}},
};

config::BoolSetting s_sort_hotbar_fallback{
    {},
    "Smart fallbacks",
    true,
    false,
    {},
};

config::BoolSetting s_randomize{
    {},
    "Randomize",
    true,
    false,
    {},
};

config::FloatSetting s_speed{
    {},
    "Speed",
    "%.1fx",
    0.0f,
    0.0f,
    1.0f,
    4.0f,
    2.0f,
    0.0f,
};

config::BoolSetting s_smart_speed{
    {},
    "Smart speed",
    true,
    false,
    {},
};

InvManagerState s_state{};
float s_tick_delay = 0.0f;
math::Vec2f s_cursor_grid_pos{};

// sub_1800695B0
ToolClassification classify_tool_or_weapon(int32_t item_id) noexcept {
    switch (item_id) {
        case 268: return {22, 1}; // Wooden Sword
        case 269: return {16, 1}; // Wooden Shovel
        case 270: return {14, 1}; // Wooden Pickaxe
        case 271: return {1, 1};  // Wooden Axe

        case 272: return {22, 2}; // Stone Sword
        case 273: return {16, 2}; // Stone Shovel
        case 274: return {14, 2}; // Stone Pickaxe
        case 275: return {1, 2};  // Stone Axe

        case 267: return {22, 3}; // Iron Sword
        case 256: return {16, 3}; // Iron Shovel
        case 257: return {14, 3}; // Iron Pickaxe
        case 258: return {1, 3};  // Iron Axe

        case 283: return {22, 4}; // Golden Sword
        case 284: return {16, 4}; // Golden Shovel
        case 285: return {14, 4}; // Golden Pickaxe
        case 286: return {1, 4};  // Golden Axe

        case 276: return {22, 5}; // Diamond Sword
        case 277: return {16, 5}; // Diamond Shovel
        case 278: return {14, 5}; // Diamond Pickaxe
        case 279: return {1, 5};  // Diamond Axe

        case 359: return {15, 1}; // Shears
        default:  return {0, 0};
    }
}

// sub_1800696C0
uint32_t score_enchantments(events::GameContext* ctx, jobject ench_tag_list, const uint8_t weights[63]) noexcept {
    if (ench_tag_list == nullptr) {
        return 0;
    }

    events::JniContext* jni = ctx->jni;
    JNIEnv* env = jni->env;
    jstring id_key = env->NewStringUTF("id");
    if (id_key == nullptr) {
        return 0;
    }
    jstring lvl_key = env->NewStringUTF("lvl");
    if (lvl_key == nullptr) {
        return 0;
    }

    const int32_t count = mc::nbt_tag_list_count(jni, ench_tag_list);
    if (count <= 0) {
        return 0;
    }

    uint32_t total = 0;
    for (int32_t i = 0; i < count; ++i) {
        jobject compound = mc::nbt_tag_list_get_compound_at(jni, ench_tag_list, i);
        const uint16_t ench_id = static_cast<uint16_t>(mc::nbt_tag_compound_get_short(jni, compound, id_key));
        if (ench_id <= 62u) {
            const uint16_t w = weights[ench_id];
            if (w != 0) {
                const int16_t lvl = mc::nbt_tag_compound_get_short(jni, compound, lvl_key);
                if (lvl >= 0) {
                    total += static_cast<uint32_t>(static_cast<int32_t>(lvl) * static_cast<int32_t>(w));
                }
            }
        }
    }
    return total;
}

// sub_180069510
uint32_t score_melee_weapon(
    events::GameContext* ctx,
    int32_t item_id,
    bool sword_only,
    jobject ench_tag_list) noexcept {
    const ToolClassification tc = classify_tool_or_weapon(item_id);
    if (sword_only && tc.category != 22) {
        return 0;
    }

    uint16_t type_rank = 0;
    switch (tc.category) {
        case 1:  type_rank = 3; break; // Axe
        case 14: type_rank = 2; break; // Pickaxe
        case 16: type_rank = 1; break; // Shovel
        case 22: type_rank = 4; break; // Sword
        default: type_rank = 0; break;
    }

    const uint32_t base = 40u * (static_cast<uint32_t>(kMaterialTierWeight[tc.tier]) + type_rank);
    return base + score_enchantments(ctx, ench_tag_list, kWeaponEnchantWeights);
}

// sub_1800687A0
uint16_t score_armor_piece(events::GameContext* ctx, jobject stack, uint32_t armor_piece_idx) noexcept {
    if (stack == nullptr) {
        return 0;
    }

    events::JniContext* jni = ctx->jni;
    jobject item = mc::item_stack_get_item(jni, stack);
    if (item == nullptr) {
        return 0;
    }

    const int32_t item_id = mc::item_get_id(jni, item);
    const int32_t rel_id = item_id - 298;
    if (static_cast<uint32_t>(rel_id) > 19u ||
        static_cast<uint32_t>(kArmorIdBaseOffset[rel_id] + item_id) != armor_piece_idx) {
        return 0;
    }

    const uint32_t mat_idx = kArmorMaterialIndex[rel_id];
    const uint8_t base_prot = kArmorProtectionPoints[mat_idx][armor_piece_idx];
    jobject ench_list = mc::item_stack_get_enchantment_tag_list(jni, stack);

    double epf_bonus = 0.0;
    if (ench_list != nullptr) {
        JNIEnv* env = jni->env;
        jstring id_key = env->NewStringUTF("id");
        if (id_key != nullptr) {
            jstring lvl_key = env->NewStringUTF("lvl");
            if (lvl_key != nullptr) {
                const int32_t count = mc::nbt_tag_list_count(jni, ench_list);
                if (count > 0) {
                    float minor_prot = 0.0f;
                    float main_prot = 0.0f;
                    for (int32_t i = 0; i < count; ++i) {
                        jobject compound = mc::nbt_tag_list_get_compound_at(jni, ench_list, i);
                        const uint16_t ench_id =
                            static_cast<uint16_t>(mc::nbt_tag_compound_get_short(jni, compound, id_key));
                        const int16_t lvl = mc::nbt_tag_compound_get_short(jni, compound, lvl_key);
                        if (lvl >= 0) {
                            if (static_cast<uint32_t>(ench_id) - 1u < 4u) {
                                minor_prot = static_cast<float>(static_cast<double>(lvl) * 0.1 + minor_prot);
                            } else if (ench_id == 0u) {
                                uint32_t epf = static_cast<uint32_t>(
                                    static_cast<int32_t>(static_cast<double>(lvl * lvl + 6) * 0.75 / 3.0));
                                if (epf >= 25u) {
                                    epf = 25u;
                                }
                                const int32_t avg_epf =
                                    static_cast<int32_t>(((epf + 1u) & 0x3Eu) + (epf >> 1));
                                main_prot = static_cast<float>(avg_epf) * 0.5f;
                            }
                        }
                    }
                    epf_bonus = static_cast<double>(minor_prot + main_prot);
                }
            }
        }
    }

    const float combined_reduction = static_cast<float>(
        1.0 - (1.0 - epf_bonus * 0.04) * (1.0 - static_cast<double>(base_prot) * 0.04));
    return static_cast<uint16_t>(static_cast<int32_t>(combined_reduction * 1000.0f));
}

// sub_180068A50
uint16_t score_item_for_hotbar_category(
    events::GameContext* ctx,
    int32_t category,
    uint32_t item_id,
    uint32_t damage,
    jobject ench_list,
    bool has_custom_name) noexcept {
    switch (category) {
        case 0:
        case 2: // Blocks
            switch (item_id) {
                case 1u:
                case 2u:
                case 3u:
                case 4u:
                case 5u:
                case 17u:
                case 18u:
                case 20u:
                case 24u:
                case 35u:
                case 45u:
                case 49u:
                case 78u:
                case 87u:
                case 95u:
                case 98u:
                case 121u:
                case 125u:
                case 133u:
                case 155u:
                case 159u:
                case 162u:
                    return 1;
                default:
                    return 0;
            }

        case 1:  // Axe
        case 14: // Pickaxe
        case 15: // Shears
        case 16: { // Shovel
            const ToolClassification tc = classify_tool_or_weapon(static_cast<int32_t>(item_id));
            if (tc.category != category) {
                return 0;
            }
            return static_cast<uint16_t>(
                100u * tc.tier + score_enchantments(ctx, ench_list, kToolEnchantWeights));
        }

        case 3: // Bow
            if (item_id != 261u) {
                return 0;
            }
            return static_cast<uint16_t>(
                static_cast<uint8_t>(score_enchantments(ctx, ench_list, kBowEnchantWeights)) + 1u);

        case 4: // Chest
            return (item_id == 54u) ? 1 : 0;

        case 5: // Ender pearl (deprioritize named items such as BedWars shop/lobby items)
            return (item_id == 368u) ? static_cast<uint16_t>(2 - static_cast<int>(has_custom_name)) : 0;

        case 6: // Fireball
            return (item_id == 385u) ? 1 : 0;

        case 7: // Fishing rod
            if (item_id != 346u) {
                return 0;
            }
            return static_cast<uint16_t>(
                static_cast<uint8_t>(score_melee_weapon(ctx, 346, false, ench_list)) + 1u);

        case 8: // Flint and steel
            return (item_id == 259u) ? 1 : 0;

        case 9: // Golden apple (Notch apple damage==1 scores 2, regular scores 1)
            return (item_id == 322u) ? static_cast<uint16_t>((damage == 1u) + 1) : 0;

        case 10: // Healing potion (Regeneration=1, Instant Health=5)
        case 11: // Jump potion (Leaping=11)
        case 19: // Speed potion (Swiftness=2)
        case 21: { // Strength potion (Strength=9)
            if (item_id != 373u) {
                return 0;
            }
            uint16_t base = 0;
            switch (damage & 0x0Fu) {
                case 1u:
                    if (category != 10) return 0;
                    base = 1;
                    break;
                case 2u:
                    if (category != 19) return 0;
                    base = 1;
                    break;
                case 5u:
                    if (category != 10) return 0;
                    base = 2;
                    break;
                case 9u:
                    if (category != 21) return 0;
                    base = 1;
                    break;
                case 11u:
                    if (category != 11) return 0;
                    base = 1;
                    break;
                default:
                    return 0;
            }
            return static_cast<uint16_t>(base + ((damage & 0x20u) != 0) + ((damage >> 6) & 1u));
        }

        case 12: // Ladders
            return (item_id == 65u) ? 1 : 0;

        case 13: // Lava bucket
            return (item_id == 327u) ? 1 : 0;

        case 17: // Snowballs/eggs
            return (item_id == 332u || item_id == 344u) ? 1 : 0;

        case 18: // Soup
            return (item_id == 413u || item_id == 282u) ? 1 : 0;

        case 20: // Stick
            return (item_id == 280u) ? 1 : 0;

        case 22: { // Sword
            const ToolClassification tc = classify_tool_or_weapon(static_cast<int32_t>(item_id));
            if (tc.category != 22) {
                return 0;
            }
            const uint32_t tier_bonus = 40u * kMaterialTierWeight[tc.tier];
            return static_cast<uint16_t>(
                tier_bonus + score_enchantments(ctx, ench_list, kWeaponEnchantWeights) + 160u);
        }

        case 23: // TNT
            return (item_id == 46u) ? 1 : 0;

        case 24: // Water bucket
            return (item_id == 326u) ? 1 : 0;

        case 25: // Weapon
            return static_cast<uint16_t>(
                score_melee_weapon(ctx, static_cast<int32_t>(item_id), false, ench_list));

        case 26: // Wool
            return (item_id == 35u) ? 1 : 0;

        default:
            return 0;
    }
}

// sub_180069CA0
uint32_t score_stack_with_fallback(events::GameContext* ctx, jobject stack, int32_t wanted_category) noexcept {
    events::JniContext* jni = ctx->jni;
    jobject item = mc::item_stack_get_item(jni, stack);
    if (item == nullptr) {
        return 0;
    }

    const uint32_t item_id = static_cast<uint32_t>(mc::item_get_id(jni, item));
    const uint32_t damage = static_cast<uint32_t>(mc::item_stack_get_damage(jni, stack));
    jobject ench_list = mc::item_stack_get_enchantment_tag_list(jni, stack);
    const bool has_name = mc::item_stack_has_display_name(jni, stack);

    const uint16_t primary =
        score_item_for_hotbar_category(ctx, wanted_category, item_id, damage, ench_list, has_name);
    if (!s_sort_hotbar_fallback.value) {
        return primary;
    }

    if (primary != 0) {
        return static_cast<uint32_t>(primary) << 16;
    }

    int32_t fallback_category = 0;
    if (wanted_category == 22) {
        fallback_category = 25; // Sword -> Weapon
    } else if (wanted_category == 17) {
        fallback_category = 7;  // Snowballs/eggs -> Fishing rod
    } else if (wanted_category == 7) {
        fallback_category = 17; // Fishing rod -> Snowballs/eggs
    } else {
        return 0;
    }

    return score_item_for_hotbar_category(ctx, fallback_category, item_id, damage, ench_list, has_name);
}

// sub_180069800
bool find_next_armor_action(events::GameContext* ctx, InvManagerState* state) noexcept {
    const uint32_t piece_idx = state->sub_index;
    events::JniContext* jni = ctx->jni;
    jobject inv = mc::player_get_inventory(jni, ctx->local_player);
    jobject equipped = mc::inventory_get_stack_in_slot(jni, inv, static_cast<int32_t>(39u - piece_idx));
    jobjectArray main_inv = mc::inv_player_get_main_inventory(jni, inv);

    uint16_t best_score = score_armor_piece(ctx, equipped, piece_idx);
    int32_t best_slot = -1;
    for (int32_t slot = 0; slot < 36; ++slot) {
        jobject cand = jni->env->GetObjectArrayElement(main_inv, static_cast<jsize>(slot));
        const uint16_t score = score_armor_piece(ctx, cand, piece_idx);
        if (score > best_score) {
            best_score = score;
            best_slot = slot;
        }
    }

    if (best_slot == -1) {
        return false;
    }

    if (equipped != nullptr) {
        if (best_slot > 8) {
            state->click_kind = 2;
            state->secondary_slot = static_cast<uint8_t>(piece_idx + 5u);
            state->target_window_slot = static_cast<uint8_t>(best_slot);
        } else {
            state->click_kind = 1;
            state->secondary_slot = static_cast<uint8_t>(best_slot);
            state->target_window_slot = static_cast<uint8_t>(piece_idx + 5u);
        }
    } else {
        state->click_kind = 0;
        state->secondary_slot = 0;
        state->target_window_slot =
            static_cast<uint8_t>((best_slot < 9) ? (best_slot + 36) : best_slot);
    }
    return true;
}

// sub_180069950
bool find_next_hotbar_action(events::GameContext* ctx, InvManagerState* state) noexcept {
    const uint32_t hotbar_slot = state->sub_index;
    const int32_t wanted_cat = s_hotbar_slots[hotbar_slot].value;
    if (wanted_cat == 0) {
        return false;
    }

    events::JniContext* jni = ctx->jni;
    jobject inv = mc::player_get_inventory(jni, ctx->local_player);
    jobjectArray main_inv = mc::inv_player_get_main_inventory(jni, inv);
    JNIEnv* env = jni->env;

    jobject cur_stack = env->GetObjectArrayElement(main_inv, static_cast<jsize>(hotbar_slot));
    uint64_t cur_id_damage = 0;
    int32_t max_stack = 0;
    int32_t cur_count = 0;
    uint32_t cur_unit_score = 0;

    if (cur_stack != nullptr) {
        const int32_t raw_size = mc::item_stack_get_size(jni, cur_stack);
        max_stack = mc::item_stack_get_max_stack_size(jni, cur_stack);
        jobject cur_item = mc::item_stack_get_item(jni, cur_stack);
        if (cur_item != nullptr) {
            const uint32_t id = static_cast<uint32_t>(mc::item_get_id(jni, cur_item));
            const uint32_t dmg = static_cast<uint32_t>(mc::item_stack_get_damage(jni, cur_stack));
            cur_id_damage = (static_cast<uint64_t>(id) << 32) | dmg;
        }
        cur_unit_score = score_stack_with_fallback(ctx, cur_stack, wanted_cat);
        cur_count = (raw_size > 0) ? raw_size : 0;
    }

    bool storage_has_empty_slot = false;
    for (int32_t slot = 9; slot <= 35; ++slot) {
        if (env->GetObjectArrayElement(main_inv, static_cast<jsize>(slot)) == nullptr) {
            storage_has_empty_slot = true;
            break;
        }
    }

    uint32_t best_total_score = cur_unit_score * static_cast<uint32_t>(cur_count);
    uint32_t best_slot = hotbar_slot;
    bool best_is_shift_merge = (cur_count != 0);

    for (uint32_t slot = 0; slot < 36; ++slot) {
        if (slot == hotbar_slot || ((state->completed_mask >> slot) & 1ULL) != 0ULL) {
            continue;
        }

        jobject cand_stack = env->GetObjectArrayElement(main_inv, static_cast<jsize>(slot));
        if (cand_stack == nullptr) {
            continue;
        }

        const int32_t cand_count = mc::item_stack_get_size(jni, cand_stack);
        jobject cand_item = mc::item_stack_get_item(jni, cand_stack);
        uint64_t cand_id_damage = 0;
        if (cand_item != nullptr) {
            const uint32_t id = static_cast<uint32_t>(mc::item_get_id(jni, cand_item));
            const uint32_t dmg = static_cast<uint32_t>(mc::item_stack_get_damage(jni, cand_stack));
            cand_id_damage = (static_cast<uint64_t>(id) << 32) | dmg;
        }

        uint32_t merge_score = 0;
        if (cur_count < max_stack && cand_count < max_stack && cand_id_damage == cur_id_damage &&
            mc::item_stack_are_tags_equal(jni, cur_stack, cand_stack)) {
            const int32_t merged_count = std::min(cur_count + cand_count, max_stack);
            merge_score = cur_unit_score * static_cast<uint32_t>(merged_count);
        }

        uint32_t cand_score =
            static_cast<uint32_t>(cand_count) * score_stack_with_fallback(ctx, cand_stack, wanted_cat);
        const uint32_t cand_minus_one = cand_score - 1u;
        if (cand_minus_one < merge_score) {
            cand_score = merge_score;
        }

        if (cand_score > best_total_score) {
            const bool is_merge = (cand_minus_one < merge_score);
            if (!is_merge || storage_has_empty_slot || slot >= 9u) {
                best_total_score = cand_score;
                best_slot = slot;
                best_is_shift_merge = is_merge;
            }
        }
    }

    if (best_slot == hotbar_slot) {
        if (best_total_score == 0) {
            return false;
        }
        state->completed_mask |= (1ULL << hotbar_slot);
        return false;
    }

    state->click_kind = best_is_shift_merge ? 0u : 1u;
    state->secondary_slot = best_is_shift_merge ? 0u : static_cast<uint8_t>(hotbar_slot);
    state->target_window_slot =
        static_cast<uint8_t>((best_slot < 9u) ? (best_slot + 36u) : best_slot);
    state->completed_mask |= (1ULL << hotbar_slot);
    return true;
}

float compute_step_delay(math::Vec2f target_pos) noexcept {
    float step = s_speed.max_val / s_speed.value;
    const float dist = math::vec2_dist(s_cursor_grid_pos, target_pos);
    if (s_smart_speed.value) {
        step *= std::fmax(dist * 0.5f, 1.0f);
    }
    if (s_randomize.value) {
        step *= random::uniform_float(1.0f, 1.25f);
    }
    return step;
}

// sub_180068D30
void on_gui_screen_tick(void* raw_event) noexcept {
    auto* event = static_cast<events::GuiScreenTickEvent*>(raw_event);
    events::GameContext* ctx = event->ctx;
    events::JniContext* jni = ctx->jni;
    jobject container_screen = mc::mc_get_gui_container_screen(jni, ctx->minecraft);

    if (container_screen == nullptr || !mc::is_gui_inventory(jni, container_screen)) {
        s_state = {};
        s_tick_delay = s_delay_after_open.value;
        if (s_randomize.value) {
            s_tick_delay *= random::uniform_float(1.0f, 1.5f);
        }
        s_cursor_grid_pos = mc::get_inventory_slot_grid_pos(13u);
        return;
    }

    uint32_t phase = s_state.phase;
    if (phase == 2u) {
        return;
    }

    if (s_tick_delay > 0.0f) {
        s_tick_delay -= 1.0f;
        if (s_tick_delay > 0.0f) {
            return;
        }
    }

    event->forward_movement_keys = true;

    const bool* const phase_enabled[2] = {&s_equip_armor.value, &s_sort_hotbar.value};
    constexpr uint8_t kPhaseLimits[2] = {4, 9};
    using PhaseFinderFn = bool (*)(events::GameContext*, InvManagerState*) noexcept;
    constexpr PhaseFinderFn kPhaseFinders[2] = {find_next_armor_action, find_next_hotbar_action};

    int32_t click_button = 0;
    int32_t click_mode = 0;

    while (true) {
        switch (phase) {
            case 0u:
            case 1u: {
                if (!*phase_enabled[phase]) {
                    s_state = {};
                    s_state.phase = ++phase;
                    continue;
                }

                const uint8_t limit = kPhaseLimits[phase];
                bool found = false;
                while (s_state.sub_index < limit) {
                    if (kPhaseFinders[phase](ctx, &s_state)) {
                        found = true;
                        break;
                    }
                    ++s_state.sub_index;
                }

                if (!found) {
                    s_state = {};
                    s_state.phase = ++phase;
                    continue;
                }

                const math::Vec2f next_pos = mc::get_inventory_slot_grid_pos(s_state.target_window_slot);
                s_tick_delay += compute_step_delay(next_pos);
                s_cursor_grid_pos = next_pos;
                s_state.phase = 3u;
                s_state.resume_phase = phase;
                s_state.advance_sub_index = (phase != 1u);
                return;
            }

            case 3u: {
                const uint32_t kind = s_state.click_kind;
                const uint8_t sec_slot = s_state.secondary_slot;
                if (kind == 2u) {
                    click_button = 0;
                    click_mode = 0;
                } else if (kind == 1u) {
                    click_button = sec_slot;
                    click_mode = 2;
                } else if (kind == 0u) {
                    click_button = 0;
                    click_mode = 1;
                }

                mc::gui_container_handle_mouse_click(
                    jni,
                    container_screen,
                    s_state.target_window_slot,
                    click_button,
                    click_mode);

                s_state.completed_mask |= (1ULL << s_state.sub_index);
                if (s_state.advance_sub_index) {
                    ++s_state.sub_index;
                }

                if (kind != 2u) {
                    phase = s_state.resume_phase;
                    s_state.phase = phase;
                    continue;
                }

                const math::Vec2f armor_pos = mc::get_inventory_slot_grid_pos(sec_slot);
                s_tick_delay += compute_step_delay(armor_pos);
                s_cursor_grid_pos = armor_pos;
                s_state.phase = 4u;
                s_state.target_window_slot = sec_slot;
                return;
            }

            case 4u: {
                mc::gui_container_handle_mouse_click(
                    jni,
                    container_screen,
                    s_state.target_window_slot,
                    0,
                    0);

                const math::Vec2f outside_pos = {s_cursor_grid_pos.x - 2.0f, s_cursor_grid_pos.y};
                s_tick_delay += compute_step_delay(outside_pos);
                s_cursor_grid_pos = outside_pos;
                s_state.phase = 5u;
                return;
            }

            case 5u: {
                mc::gui_container_handle_mouse_click(jni, container_screen, -999, 0, 0);
                phase = s_state.resume_phase;
                s_state.phase = phase;
                continue;
            }

            default:
                return;
        }
    }
}

// sub_180069200
void render_settings(gui::ClickGui* gui, gui::GuiRenderer* renderer) noexcept {
    gui::widgets::render_section_header(gui, renderer, "Main");
    s_delay_after_open.render(gui, renderer);
    s_speed.render(gui, renderer);
    s_smart_speed.render(gui, renderer);
    s_randomize.render(gui, renderer);

    gui::widgets::render_section_header(gui, renderer, "Equip armor");
    s_equip_armor.render(gui, renderer);

    gui::widgets::render_section_header(gui, renderer, "Sort hotbar");
    s_sort_hotbar.render(gui, renderer);
    if (s_sort_hotbar.value) {
        s_sort_hotbar_fallback.render(gui, renderer);
        for (auto& slot_setting : s_hotbar_slots) {
            slot_setting.render(gui, renderer);
        }
    }
}

// sub_180069380
void visit_config(config::ConfigArchive* archive) noexcept {
    s_delay_after_open.visit(archive, "delay_after_open");
    s_speed.visit(archive, "speed");
    s_smart_speed.visit(archive, "smart_speed");
    s_randomize.visit(archive, "randomize");
    s_equip_armor.visit(archive, "equip_armor");
    s_sort_hotbar.visit(archive, "sort_hotbar");
    s_sort_hotbar_fallback.visit(archive, "sort_hotbar_fallback");

    char key[2] = {'1', '\0'};
    for (int i = 0; i < 9; ++i) {
        key[0] = static_cast<char>('1' + i);
        s_hotbar_slots[i].visit(archive, key);
    }
}

events::Module make_module() noexcept {
    events::Module mod{};
    mod.name = "Inv Manager";
    mod.description = "Organises your inventory for you.";
    mod.category = events::ModuleCategory::Utility;
    mod.enabled = false;
    mod.flags = events::kModuleFlagNone;
    mod.keybind = 0;
    mod.hold_mode = false;
    mod.events[events::kEventGuiScreenTick] = {on_gui_screen_tick, 0, 0};
    mod.render_settings = render_settings;
    mod.visit_config = visit_config;
    return mod;
}

events::Module s_module = make_module();

} // namespace

// sub_180068650
int32_t inv_manager_score_useful_stack(
    events::GameContext* ctx,
    jobject stack,
    int32_t* out_category) noexcept {
    events::JniContext* jni = ctx->jni;
    jobject item = mc::item_stack_get_item(jni, stack);
    if (item == nullptr) {
        return 0;
    }

    const int32_t item_id = mc::item_get_id(jni, item);
    int32_t useful_cat = 0;
    int32_t hotbar_cat = 0;

    switch (item_id) {
        case 256:
        case 269:
        case 273:
        case 277:
        case 284:
            useful_cat = 3; // Shovel
            hotbar_cat = 16;
            break;

        case 257:
        case 270:
        case 274:
        case 278:
        case 285:
            useful_cat = 2; // Pickaxe
            hotbar_cat = 14;
            break;

        case 258:
        case 271:
        case 275:
        case 279:
        case 286:
            useful_cat = 1; // Axe
            hotbar_cat = 1;
            break;

        case 261:
            useful_cat = 6; // Bow
            hotbar_cat = 3;
            break;

        case 267:
        case 268:
        case 272:
        case 276:
        case 283:
            useful_cat = 0; // Sword
            hotbar_cat = 22;
            break;

        case 298:
        case 302:
        case 306:
        case 310:
        case 314:
            *out_category = 7; // Helmet
            return score_armor_piece(ctx, stack, 0u);

        case 299:
        case 303:
        case 307:
        case 311:
        case 315:
            *out_category = 8; // Chestplate
            return score_armor_piece(ctx, stack, 1u);

        case 300:
        case 304:
        case 308:
        case 312:
        case 316:
            *out_category = 9; // Leggings
            return score_armor_piece(ctx, stack, 2u);

        case 301:
        case 305:
        case 309:
        case 313:
        case 317:
            *out_category = 10; // Boots
            return score_armor_piece(ctx, stack, 3u);

        case 346:
            useful_cat = 5; // Fishing rod
            hotbar_cat = 7;
            break;

        case 359:
            useful_cat = 4; // Shears
            hotbar_cat = 15;
            break;

        default:
            return 0;
    }

    const uint32_t damage = static_cast<uint32_t>(mc::item_stack_get_damage(jni, stack));
    jobject ench_list = mc::item_stack_get_enchantment_tag_list(jni, stack);
    *out_category = useful_cat;
    return score_item_for_hotbar_category(
        ctx,
        hotbar_cat,
        static_cast<uint32_t>(item_id),
        damage,
        ench_list,
        false);
}

events::Module* get_inv_manager_module() noexcept {
    return &s_module;
}

} // namespace slinky::modules
