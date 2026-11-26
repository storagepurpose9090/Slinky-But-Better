#include "modules/utility/chest_stealer.hpp"
#include "modules/module_interop.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

#include "config/settings.hpp"
#include "gui/widgets.hpp"
#include "minecraft/game_helpers.hpp"
#include "minecraft/item_database.hpp"
#include "minecraft/mc_wrappers.hpp"
#include "util/containers.hpp"
#include "util/math.hpp"
#include "util/random.hpp"

namespace slinky::modules {

// ============================================================================
// Shared ContainerSlotHumanizer (0x6DB20 & 0x6DB90)
// ============================================================================

// sub_18006DB20
void ContainerSlotHumanizer::reset(uint64_t slot_mask, uint64_t container_slot_count) noexcept {
    pending_slot_mask = slot_mask;
    float count_f = 0.0f;
    if (static_cast<int64_t>(container_slot_count) < 0) {
        const float half = static_cast<float>(
            static_cast<int32_t>((container_slot_count >> 1) | (container_slot_count & 1ULL)));
        count_f = half + half;
    } else {
        count_f = static_cast<float>(static_cast<int32_t>(container_slot_count));
    }
    current_cursor_slot = static_cast<int32_t>(random::uniform_float(0.0f, count_f));
    last_step_distance = random::uniform_float(0.0f, 3.1415927f);
}

// sub_18006DB90
int32_t ContainerSlotHumanizer::next_slot(float* inout_delay_sec) noexcept {
    if (pending_slot_mask == 0ULL) {
        return -1;
    }

    const math::Vec2f cur_pos{
        static_cast<float>(current_cursor_slot % 9),
        static_cast<float>(current_cursor_slot / 9),
    };

    float best_valid_angle = 0.0f;
    float best_valid_cost = std::numeric_limits<float>::max();
    int32_t best_valid_slot = -1;

    float second_valid_angle = 0.0f;
    float second_valid_cost = std::numeric_limits<float>::max();
    int32_t second_valid_slot = -1;

    float best_empty_angle = 0.0f;
    float best_empty_cost = std::numeric_limits<float>::max();
    int32_t best_empty_slot = -1;

    for (uint32_t slot = 0; slot < 64u; ++slot) {
        const math::Vec2f cand_pos{
            static_cast<float>(slot % 9u),
            static_cast<float>(slot / 9u),
        };
        const float dist = math::vec2_dist(cur_pos, cand_pos);
        const float angle = math::vec2_angle_to(cur_pos, cand_pos);
        const float angle_delta = std::fabs(math::wrap_degrees_180(angle - last_step_distance));
        const float cost = static_cast<float>(
            static_cast<double>(dist) * 1.2 + static_cast<double>(angle_delta) * 0.5);

        if (((pending_slot_mask >> slot) & 1ULL) != 0ULL) {
            if (cost <= best_valid_cost) {
                second_valid_angle = best_valid_angle;
                second_valid_cost = best_valid_cost;
                second_valid_slot = best_valid_slot;

                best_valid_angle = angle;
                best_valid_cost = cost;
                best_valid_slot = static_cast<int32_t>(slot);
            }
        } else if (cost <= best_empty_cost) {
            best_empty_cost = cost;
            best_empty_angle = angle;
            best_empty_slot = static_cast<int32_t>(slot);
        }
    }

    int32_t chosen_slot = best_empty_slot;
    float chosen_cost = best_empty_cost;
    float chosen_angle = best_empty_angle;

    const bool do_misclick =
        best_valid_cost > best_empty_cost &&
        allow_misclicks &&
        misclick_gate.sample_chance_timed({0.05f, 0.1f}, 20.0);

    if (!do_misclick) {
        if (second_valid_slot != -1 &&
            static_cast<double>(best_valid_cost) * 1.5 > static_cast<double>(second_valid_cost) &&
            skip_nearest_gate.sample_chance_timed({0.05f, 0.1f}, 10.0)) {
            chosen_slot = second_valid_slot;
            chosen_cost = second_valid_cost;
            chosen_angle = second_valid_angle;
        } else {
            chosen_slot = best_valid_slot;
            chosen_cost = best_valid_cost;
            chosen_angle = best_valid_angle;
        }
    }

    if (chosen_slot == -1) {
        return -1;
    }

    pending_slot_mask &= ~(1ULL << static_cast<uint32_t>(chosen_slot));
    current_cursor_slot = chosen_slot;
    last_step_distance = chosen_angle;

    float effective_speed = speed_drift.sample_timed({0.8f, 1.2f}, 30.0) * speed_cps;
    effective_speed = static_cast<float>(
        static_cast<double>(effective_speed) *
        (static_cast<double>(gaussian_jitter.next_standard_normal()) * 0.3 + 1.0));

    if (pause_spike_gate.sample_chance_timed({0.04f, 0.08f}, 30.0)) {
        effective_speed /= random::uniform_float(1.4f, 1.8f);
    }

    const float numer = distance_based_delay ? chosen_cost : 1.0f;
    *inout_delay_sec += numer / effective_speed;
    return chosen_slot;
}

namespace {

constexpr int32_t kBlockChest = 54;
constexpr int32_t kBlockEnderChest = 130;
constexpr int32_t kBlockTrappedChest = 146;
constexpr int32_t kMopTypeBlock = 1;

// Target stack quotas per consumable bucket 0..13 (0x180172738..0x18017274B)
constexpr uint8_t kConsumableBucketTargets[14] = {
    255, // [ 0] Solid blocks
    64,  // [ 1] TNT (46)
    64,  // [ 2] Ladders (65)
    64,  // [ 3] Food
    1,   // [ 4] Flint and Steel (259)
    64,  // [ 5] Arrows (262)
    64,  // [ 6] Snowballs / Eggs (332, 344)
    64,  // [ 7] Golden Apples (322)
    64,  // [ 8] Fireballs (385)
    5,   // [ 9] Mushroom Stew (282)
    1,   // [10] Lava Bucket (327)
    1,   // [11] Water Bucket (326)
    32,  // [12] Ender Pearls (368)
    128, // [13] Bottle o' Enchanting (384)
};

util::PodVector<mc::ItemIdEntry> s_extra_items{};

void extra_items_get_options(const char* const** out_items, size_t* out_count) noexcept {
    mc::get_all_item_names(out_items, out_count);
}

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

config::FloatSetting s_auto_close_delay{
    {},
    "Delay",
    "%.0f ms",
    0.0f,
    50.0f,
    0.0f,
    10.0f,
    2.0f,
    0.0f,
};

config::BoolSetting s_require_click_block{
    {},
    "Recently right-clicked chest",
    false,
    false,
    {},
};

config::BoolSetting s_require_vanilla_title{
    {},
    "Vanilla window title",
    false,
    false,
    {},
};

config::BoolSetting s_smart_speed{
    {},
    "Smart speed",
    true,
    false,
    {},
};

config::BoolSetting s_allow_misclick{
    {},
    "Allow misclicking",
    true,
    false,
    {},
};

config::BoolSetting s_auto_close{
    {},
    "Enabled",
    false,
    false,
    {},
};

config::DynamicListSetting s_extra_items_setting{
    "Extra items",
    "Search item",
    mc::validate_item_name_input,
    config::DynamicListItemKind::StringOnly,
    0,
    &s_extra_items,
    mc::item_filter_list_count,
    mc::item_filter_list_get_name,
    nullptr,
    mc::item_filter_list_add,
    mc::item_filter_list_remove,
    mc::item_filter_list_clear,
    extra_items_get_options,
    false,
    {},
};

int32_t s_recent_click_ticks = 0;
int32_t s_open_chest_ticks = 0;
ContainerSlotHumanizer s_humanizer{};
int32_t s_pending_click_slot = 0;
float s_tick_delay = 0.0f;
bool s_opened_via_chest_click = false;
int32_t s_state = 0;

// sub_18006A570
bool matches_translated_title(events::JniContext* jni, jstring title_str, const char* translation_key) noexcept {
    JNIEnv* env = jni->env;
    jstring key_jstr = env->NewStringUTF(translation_key);
    jstring localized = mc::stat_collector_translate_to_local(jni, key_jstr);
    const jsize len = env->GetStringLength(title_str);
    if (env->GetStringLength(localized) != len) {
        return false;
    }

    const jchar* title_chars = env->GetStringCritical(title_str, nullptr);
    const jchar* loc_chars = env->GetStringCritical(localized, nullptr);
    const bool equal = (std::memcmp(title_chars, loc_chars, sizeof(jchar) * static_cast<size_t>(len)) == 0);
    env->ReleaseStringCritical(title_str, title_chars);
    env->ReleaseStringCritical(localized, loc_chars);
    return equal;
}

// sub_18006A650
bool classify_consumable_bucket(int32_t item_id, int32_t* out_bucket) noexcept {
    int32_t bucket = 0;
    switch (item_id) {
        case 1:
        case 2:
        case 3:
        case 4:
        case 5:
        case 17:
        case 18:
        case 24:
        case 35:
        case 45:
        case 49:
        case 87:
        case 121:
        case 133:
        case 155:
        case 159:
        case 162:
            bucket = 0;
            break;

        case 46:  bucket = 1; break;
        case 65:  bucket = 2; break;

        case 260:
        case 297:
        case 319:
        case 320:
        case 350:
        case 363:
        case 364:
        case 366:
        case 391:
        case 396:
        case 424:
            bucket = 3;
            break;

        case 259: bucket = 4; break;
        case 262: bucket = 5; break;
        case 332:
        case 344:
            bucket = 6;
            break;
        case 322: bucket = 7; break;
        case 385: bucket = 8; break;
        case 282: bucket = 9; break;
        case 327: bucket = 10; break;
        case 326: bucket = 11; break;
        case 368: bucket = 12; break;
        case 384: bucket = 13; break;
        default:
            return false;
    }

    *out_bucket = bucket;
    return true;
}

// sub_180069D90
void on_post_attack_or_use(void* raw_event) noexcept {
    auto* event = static_cast<events::AttackOrUseEntityEvent*>(raw_event);
    if (event->is_attack) {
        return;
    }

    events::GameContext* ctx = event->ctx;
    events::JniContext* jni = ctx->jni;
    jobject mop = mc::mc_get_object_mouse_over(jni, ctx->minecraft);
    if (mop != nullptr && mc::mop_get_type_ordinal(jni, mop) == kMopTypeBlock) {
        const mc::Vec3i pos = mc::mop_get_block_pos_vec3i(jni, mop);
        jobject block = mc::world_get_block(jni, ctx->world, pos.x, pos.y, pos.z);
        if (block != nullptr) {
            const int32_t block_id = mc::block_get_id(jni, block);
            if (block_id == kBlockChest || block_id == kBlockTrappedChest || block_id == kBlockEnderChest) {
                s_recent_click_ticks = 20;
            }
        }
    }
}

// sub_180069E40
void on_gui_screen_tick(void* raw_event) noexcept {
    if (s_recent_click_ticks != 0) {
        --s_recent_click_ticks;
        s_opened_via_chest_click = true;
    }

    auto* event = static_cast<events::GuiScreenTickEvent*>(raw_event);
    events::GameContext* ctx = event->ctx;
    events::JniContext* jni = ctx->jni;
    jobject gui_screen = mc::mc_get_gui_container_screen(jni, ctx->minecraft);

    if (gui_screen == nullptr || !mc::is_gui_chest(jni, gui_screen)) {
        s_state = 0;
        s_open_chest_ticks = 0;
        if (s_recent_click_ticks == 0) {
            s_opened_via_chest_click = false;
        }
        return;
    }

    ++s_open_chest_ticks;
    jobject lower_inv = mc::gui_chest_get_lower_inventory(jni, gui_screen);

    if ((s_require_click_block.value && !s_opened_via_chest_click) ||
        (s_require_vanilla_title.value &&
         (lower_inv == nullptr ||
          [&]() noexcept {
              jobject inv_basic = mc::as_inventory_basic(jni, lower_inv);
              jstring title = mc::inventory_basic_get_title(jni, inv_basic);
              return !matches_translated_title(jni, title, "container.chest") &&
                     !matches_translated_title(jni, title, "container.chestDouble");
          }()))) {
        s_state = 0;
        return;
    }

    s_humanizer.speed_cps = s_speed.value;
    s_humanizer.distance_based_delay = s_smart_speed.value;
    s_humanizer.allow_misclicks = s_allow_misclick.value;

    if (s_tick_delay > 0.0f) {
        s_tick_delay -= 1.0f;
        return;
    }

    while (true) {
        switch (s_state) {
            case 0: {
                const int32_t raw_chest_size = mc::inventory_get_size(jni, lower_inv);
                s_tick_delay += s_delay_after_open.value;

                uint16_t best_equip_scores[11]{};
                uint8_t bucket_counts[14]{};

                jobject player_inv = mc::player_get_inventory(jni, ctx->local_player);
                for (int32_t slot = 0; slot < 40; ++slot) {
                    jobject stack = mc::inventory_get_stack_in_slot(jni, player_inv, slot);
                    if (stack == nullptr) {
                        continue;
                    }
                    jobject item = mc::item_stack_get_item(jni, stack);
                    if (item == nullptr) {
                        continue;
                    }
                    const int32_t count = mc::item_stack_get_size(jni, stack);
                    if (count <= 0) {
                        continue;
                    }
                    const int32_t item_id = mc::item_get_id(jni, item);
                    int32_t bucket = 0;
                    if (classify_consumable_bucket(item_id, &bucket)) {
                        const uint32_t sum = bucket_counts[bucket] + static_cast<uint32_t>(count);
                        bucket_counts[bucket] = static_cast<uint8_t>(sum >= 255u ? 255u : sum);
                    } else {
                        int32_t cat = 0;
                        const uint16_t score =
                            static_cast<uint16_t>(inv_manager_score_useful_stack(ctx, stack, &cat));
                        if (score != 0 && score > best_equip_scores[cat]) {
                            best_equip_scores[cat] = score;
                        }
                    }
                }

                for (size_t b = 0; b < 14; ++b) {
                    const uint8_t target = kConsumableBucketTargets[b];
                    const uint8_t have = bucket_counts[b];
                    bucket_counts[b] = (have >= target) ? 0u : static_cast<uint8_t>(target - have);
                }

                const int32_t chest_slots = std::min(raw_chest_size, 64);
                uint64_t loot_mask = 0;
                uint64_t upgraded_cat_mask = 0;
                uint8_t upgraded_cat_slot[11]{};

                for (int32_t slot = 0; slot < chest_slots; ++slot) {
                    jobject stack = mc::inventory_get_stack_in_slot(jni, lower_inv, slot);
                    if (stack == nullptr) {
                        continue;
                    }
                    jobject item = mc::item_stack_get_item(jni, stack);
                    if (item == nullptr) {
                        continue;
                    }
                    const int32_t count = mc::item_stack_get_size(jni, stack);
                    if (count <= 0) {
                        continue;
                    }
                    const int32_t item_id = mc::item_get_id(jni, item);

                    if (mc::item_filter_list_contains_id(&s_extra_items, item_id)) {
                        loot_mask |= (1ULL << slot);
                        continue;
                    }

                    int32_t bucket = 0;
                    if (classify_consumable_bucket(item_id, &bucket)) {
                        const uint32_t needed = bucket_counts[bucket];
                        if (needed != 0u) {
                            bucket_counts[bucket] =
                                (static_cast<uint32_t>(count) < needed)
                                    ? static_cast<uint8_t>(needed - static_cast<uint32_t>(count))
                                    : 0u;
                            loot_mask |= (1ULL << slot);
                        }
                    } else {
                        int32_t cat = 0;
                        const uint16_t score =
                            static_cast<uint16_t>(inv_manager_score_useful_stack(ctx, stack, &cat));
                        if (score != 0 && score > best_equip_scores[cat]) {
                            if (((upgraded_cat_mask >> cat) & 1ULL) != 0ULL) {
                                loot_mask &= ~(1ULL << upgraded_cat_slot[cat]);
                            }
                            loot_mask |= (1ULL << slot);
                            best_equip_scores[cat] = score;
                            upgraded_cat_mask |= (1ULL << cat);
                            upgraded_cat_slot[cat] = static_cast<uint8_t>(slot);
                        }
                    }
                }

                if (loot_mask == 0ULL && static_cast<uint32_t>(s_open_chest_ticks) < 5u) {
                    return;
                }

                s_humanizer.reset(loot_mask, static_cast<uint64_t>(chest_slots));
                s_state = 1;
                if (s_tick_delay > 0.0f) {
                    return;
                }
                continue;
            }

            case 1: {
                const int32_t next = s_humanizer.next_slot(&s_tick_delay);
                s_pending_click_slot = next;
                if (next != -1) {
                    s_state = 2;
                } else if (s_auto_close.value) {
                    s_tick_delay += s_auto_close_delay.value;
                    s_state = 3;
                } else {
                    s_state = 4;
                }

                if (s_tick_delay > 0.0f) {
                    return;
                }
                continue;
            }

            case 2: {
                mc::gui_container_handle_mouse_click(jni, gui_screen, s_pending_click_slot, 0, 1);
                s_state = 1;
                if (s_tick_delay > 0.0f) {
                    return;
                }
                continue;
            }

            case 3: {
                if (s_auto_close.value) {
                    mc::gui_screen_key_typed(jni, gui_screen, 0, 1);
                }
                s_state = 4;
                return;
            }

            case 4:
            default:
                return;
        }
    }
}

// sub_18006A390
void render_settings(gui::ClickGui* gui, gui::GuiRenderer* renderer) noexcept {
    gui::widgets::render_section_header(gui, renderer, "Main");
    s_delay_after_open.render(gui, renderer);
    s_speed.render(gui, renderer);
    s_smart_speed.render(gui, renderer);
    s_allow_misclick.render(gui, renderer);
    s_extra_items_setting.render(gui, renderer);

    gui::widgets::render_section_header(gui, renderer, "Close inventory when done");
    s_auto_close.render(gui, renderer);
    if (s_auto_close.value) {
        s_auto_close_delay.render(gui, renderer);
    }

    gui::widgets::render_section_header(gui, renderer, "Conditions");
    s_require_click_block.render(gui, renderer);
    s_require_vanilla_title.render(gui, renderer);
}

// sub_18006A490
void visit_config(config::ConfigArchive* archive) noexcept {
    s_delay_after_open.visit(archive, "delay_after_open");
    s_speed.visit(archive, "speed");
    s_smart_speed.visit(archive, "smart_speed");
    s_allow_misclick.visit(archive, "allow_misclick");
    s_extra_items_setting.visit(archive, "extra_items");
    s_auto_close.visit(archive, "auto_close");
    s_auto_close_delay.visit(archive, "auto_close_delay");
    s_require_click_block.visit(archive, "require_click_block");
    s_require_vanilla_title.visit(archive, "require_vanilla_title");
}

events::Module make_module() noexcept {
    events::Module mod{};
    mod.name = "Chest Stealer";
    mod.description = "Automatically loots chests for you.";
    mod.category = events::ModuleCategory::Utility;
    mod.enabled = false;
    mod.flags = events::kModuleFlagNone;
    mod.keybind = 0;
    mod.hold_mode = false;
    mod.events[events::kEventPostAttackOrUseEntity] = {on_post_attack_or_use, 0, 0};
    mod.events[events::kEventGuiScreenTick] = {on_gui_screen_tick, 0, 0};
    mod.render_settings = render_settings;
    mod.visit_config = visit_config;
    return mod;
}

events::Module s_module = make_module();

} // namespace

events::Module* get_chest_stealer_module() noexcept {
    return &s_module;
}

} // namespace slinky::modules
