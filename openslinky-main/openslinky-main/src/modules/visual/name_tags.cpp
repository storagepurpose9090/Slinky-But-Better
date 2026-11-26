#include "modules/visual/name_tags.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <glad/glad.h>

#include "config/settings.hpp"
#include "gui/widgets.hpp"
#include "minecraft/game_helpers.hpp"
#include "minecraft/mc_wrappers.hpp"
#include "modules/module_interop.hpp"
#include "util/containers.hpp"
#include "util/math.hpp"

namespace slinky::modules {

namespace {

constexpr size_t kMaxCachedNameTags = 100;
constexpr size_t kEquipmentSlotCount = 5;
constexpr int32_t kMaxParsedEnchants = 25;
constexpr int32_t kMaxDisplayedEnchants = 4;

constexpr const char* kHealthFormatOptions[2] = {
    "HP (0-20)",
    "Hearts (0.0-10.0)",
};

// Enchantment short display names indexed by enchantment ID 0..62 (@ 0x18016FDF0)
constexpr const char* kEnchantShortNames[63] = {
    "Pr", "FP", "FF", "BP", "PP", "Re", "AA", "Th",
    "DS", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    "Sh", "Sm", "BA", "Kn", "FA", "Lo", nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    "Ef", "ST", "Un", "Fo", nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    "Po", "Pu", "Fl", "In", nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, "LS", "Lu",
};

// Enchantment sort priority indexed by enchantment ID 0..62 (@ 0x180170040)
constexpr int32_t kEnchantSortOrder[63] = {
    0, 3, 1, 4, 3, 4, 3, 2, 2, 0, 0, 0, 0, 0, 0, 0,
    0, 4, 4, 1, 1, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 1, 2, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1,
};

struct EnchantEntry {
    int16_t id = 0;
    int16_t level = 0;
};

struct EquipmentSlotCache {
    bool has_item = false;
    uint8_t pad[3]{};
    int32_t enchant_count = 0;
    jstring* enchant_labels = nullptr;
};
static_assert(sizeof(EquipmentSlotCache) == 16, "EquipmentSlotCache must be 16 bytes");

// 160-byte cached player nametag entry (@ 0x180319B00)
struct CachedNameTag {
    math::Vec3d prev_pos{};                          // +0x00
    math::Vec3d pos{};                               // +0x18
    jobject entity_ref = nullptr;                    // +0x30 (JNI GlobalRef)
    jstring tag_text = nullptr;                      // +0x38 (JNI GlobalRef)
    int32_t tag_width = 0;                           // +0x40
    float prev_dist = 0.0f;                          // +0x44
    float dist = 0.0f;                               // +0x48
    int32_t equipped_count = 0;                      // +0x4C
    EquipmentSlotCache slots[kEquipmentSlotCount]{}; // +0x50..+0x9F
};
static_assert(sizeof(CachedNameTag) == 0xA0, "CachedNameTag must be 160 bytes");

config::FloatSetting s_range{
    {}, "Range", "%0.0f blocks", 0.0f, 0.0f, 20.0f, 300.0f, 150.0f, 0.0f
};
config::FloatSetting s_scale{
    {}, "Scale", "%0.0f %%", 0.0f, 100.0f, 0.5f, 3.0f, 2.0f, 0.0f
};
config::BoolSetting s_show_own_name_tag{{}, "Show own name tag", false, false};
config::BoolSetting s_use_display_name{{}, "Use display name format", true, false};
config::BoolSetting s_distance_scaling{{}, "Distance scaling", false, false};
config::BoolSetting s_background{{}, "Text background", true, false};
config::BoolSetting s_text_shadow{{}, "Text shadow", false, false};
config::BoolSetting s_show_health{{}, "Health", false, false};
config::ModeSetting s_health_fmt{
    {}, "Health format", 2, 0, kHealthFormatOptions, 0, 0, false
};
config::BoolSetting s_show_distance{{}, "Distance", false, false};
config::BoolSetting s_show_equipment{{}, "Equipment", false, false};
config::BoolSetting s_show_enchants{{}, "Enchantments", false, false};

size_t s_nametag_count = 0;                   // qword_180319AF0
CachedNameTag s_nametags[kMaxCachedNameTags]{}; // unk_180319B00
jobject s_render_item = nullptr;              // qword_18031D980

// Matches sub_18005EC00
void clear_cached_nametags(events::JniContext* jni) noexcept {
    if (s_nametag_count != 0 && jni != nullptr && jni->env != nullptr) {
        JNIEnv* env = jni->env;
        for (size_t i = 0; i < s_nametag_count; ++i) {
            CachedNameTag& entry = s_nametags[i];
            if (entry.tag_text != nullptr) {
                env->DeleteGlobalRef(entry.tag_text);
                entry.tag_text = nullptr;
            }
            if (entry.entity_ref != nullptr) {
                env->DeleteGlobalRef(entry.entity_ref);
                entry.entity_ref = nullptr;
            }
            for (size_t s = 0; s < kEquipmentSlotCount; ++s) {
                EquipmentSlotCache& slot = entry.slots[s];
                if (slot.enchant_labels != nullptr) {
                    for (int32_t k = 0; k < slot.enchant_count; ++k) {
                        if (slot.enchant_labels[k] != nullptr) {
                            env->DeleteGlobalRef(slot.enchant_labels[k]);
                        }
                    }
                    std::free(slot.enchant_labels);
                    slot.enchant_labels = nullptr;
                }
                slot.enchant_count = 0;
            }
        }
    }
    s_nametag_count = 0;
}

// Matches sub_18005EE40
int compare_nametags_by_dist(const void* a, const void* b) noexcept {
    const auto* na = static_cast<const CachedNameTag*>(a);
    const auto* nb = static_cast<const CachedNameTag*>(b);
    return static_cast<int>(nb->dist - na->dist);
}

// Matches sub_18005EE60
int compare_enchant_entries(const void* a, const void* b) noexcept {
    const auto* ea = static_cast<const EnchantEntry*>(a);
    const auto* eb = static_cast<const EnchantEntry*>(b);
    return kEnchantSortOrder[ea->id] - kEnchantSortOrder[eb->id];
}

// Matches sub_18005D5E0
void on_module_state_changed(void* raw_event) noexcept {
    const auto* ev = static_cast<const events::ModuleStateChangedEvent*>(raw_event);
    if (!ev->enabled && ev->module == get_name_tags_module()) {
        events::JniContext* jni = (ev->ctx != nullptr) ? ev->ctx->jni : nullptr;
        clear_cached_nametags(jni);
        if (s_render_item != nullptr && jni != nullptr && jni->env != nullptr) {
            jni->env->DeleteGlobalRef(s_render_item);
            s_render_item = nullptr;
        }
    }
}

// Matches sub_18005D650
void on_render_world_2d(void* raw_event) noexcept {
    if (s_nametag_count == 0 || s_render_item == nullptr) {
        return;
    }

    const auto* ev = static_cast<const events::RenderWorld2DProjEvent*>(raw_event);
    events::GameContext* ctx = ev->ctx;

    jobject render_manager = minecraft::mc_get_render_manager(ctx->jni, ctx->minecraft);
    jobject font_renderer = minecraft::render_manager_get_font_renderer(ctx->jni, render_manager);
    jobject texture_manager = minecraft::mc_get_texture_manager(ctx->jni, ctx->minecraft);

    const float saved_z_level = minecraft::render_item_get_z_level(ctx->jni, s_render_item);
    minecraft::render_item_set_z_level(ctx->jni, s_render_item, -200.0f);

    jobject entity_renderer = minecraft::mc_get_entity_renderer(ctx->jni, ctx->minecraft);
    const float fov = minecraft::entity_renderer_get_fov_modifier(
        ctx->jni, entity_renderer, ev->partial_ticks);

    if (s_nametag_count > 0) {
        const float fov_factor = 180.0f / fov;
        const double partial = static_cast<double>(ev->partial_ticks);

        for (size_t i = 0; i < s_nametag_count; ++i) {
            const CachedNameTag& entry = s_nametags[i];
            math::Vec3d interp_pos = math::vec3_lerp(entry.prev_pos, entry.pos, partial);
            interp_pos.y += 2.2;
            const math::Vec3d rel_pos = math::vec3_sub(interp_pos, ev->render_pos);

            math::Vec2f screen{};
            float inv_w_half = 0.0f;
            if (!minecraft::project_world_to_screen_depth(ev, rel_pos, &screen, &inv_w_half)) {
                continue;
            }

            glPushMatrix();
            glTranslatef(screen.x, screen.y, inv_w_half * 100.0f);

            const float raw_dist_scale = inv_w_half * 10.0f;
            const float dist_scale = s_distance_scaling.value
                ? std::fmax(raw_dist_scale, 1.0f)
                : raw_dist_scale;
            const float final_scale = s_scale.value * fov_factor * dist_scale;
            glScalef(final_scale, final_scale, final_scale);

            const float left_x = static_cast<float>(static_cast<double>(-entry.tag_width) * 0.5 - 2.0);
            if (ev->is_hand_pass) {
                minecraft::gl_state_disable_depth(ctx->jni);
            } else {
                glDisable(GL_DEPTH_TEST);
            }

            if (s_background.value) {
                if (ev->is_hand_pass) {
                    minecraft::gl_state_disable_texture_2d(ctx->jni);
                    minecraft::gl_state_color(ctx->jni, 0.0f, 0.0f, 0.0f, 0.5f);
                } else {
                    glDisable(GL_TEXTURE_2D);
                    glColor4f(0.0f, 0.0f, 0.0f, 0.5f);
                }

                const float right_x = static_cast<float>(static_cast<double>(entry.tag_width) * 0.5 + 2.0);
                glBegin(GL_TRIANGLE_STRIP);
                glVertex2f(left_x, 0.0f);
                glVertex2f(right_x, 0.0f);
                glVertex2f(left_x, -12.0f);
                glVertex2f(right_x, -12.0f);
                glEnd();
            }

            if (ev->is_hand_pass) {
                minecraft::gl_state_enable_texture_2d(ctx->jni);
                minecraft::gl_state_color(ctx->jni, 1.0f, 1.0f, 1.0f, 1.0f);
            } else {
                glEnable(GL_TEXTURE_2D);
                glColor3f(1.0f, 1.0f, 1.0f);
            }

            minecraft::font_renderer_draw_string(
                ctx->jni,
                font_renderer,
                entry.tag_text,
                left_x + 2.0f,
                -10.0f,
                static_cast<int32_t>(0xFFFFFFFFu),
                s_text_shadow.value);

            if (s_show_equipment.value) {
                if (ev->is_hand_pass) {
                    minecraft::gl_state_enable_depth(ctx->jni);
                } else {
                    glEnable(GL_DEPTH_TEST);
                }

                minecraft::render_helper_enable_item_lighting(ctx->jni);
                glScalef(1.0f, 1.0f, 0.001f);

                int32_t item_x = -(entry.equipped_count * 8);
                for (size_t s = 0; s < kEquipmentSlotCount; ++s) {
                    if (!entry.slots[s].has_item) {
                        continue;
                    }
                    jobject stack = minecraft::player_get_equipment_in_slot(
                        ctx->jni, entry.entity_ref, static_cast<int32_t>(s));
                    if (stack == nullptr) {
                        continue;
                    }
                    minecraft::render_item_into_gui(
                        ctx->jni, s_render_item, font_renderer, texture_manager, stack, item_x, -32);
                    minecraft::render_item_overlay_into_gui(
                        ctx->jni, s_render_item, font_renderer, texture_manager, stack, item_x, -32, nullptr);
                    item_x += 16;
                }

                minecraft::render_helper_disable_item_lighting(ctx->jni);
                if (ev->is_hand_pass) {
                    minecraft::gl_state_enable_blend(ctx->jni);
                    minecraft::gl_state_disable_alpha(ctx->jni);
                    minecraft::gl_state_blend_func(ctx->jni, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                } else {
                    glEnable(GL_BLEND);
                    glDisable(GL_ALPHA_TEST);
                    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                }

                glColor3f(1.0f, 1.0f, 1.0f);
                glScalef(0.5f, 0.5f, 0.5f);

                if (s_show_enchants.value) {
                    int32_t base_x = -(entry.equipped_count * 8);
                    for (size_t s = 0; s < kEquipmentSlotCount; ++s) {
                        const EquipmentSlotCache& slot = entry.slots[s];
                        if (!slot.has_item) {
                            continue;
                        }
                        if (slot.enchant_labels != nullptr && slot.enchant_count > 0) {
                            const float ench_x = static_cast<float>(base_x * 2);
                            int32_t ench_y = -64;
                            for (int32_t k = 0; k < slot.enchant_count; ++k) {
                                minecraft::font_renderer_draw_string(
                                    ctx->jni,
                                    font_renderer,
                                    slot.enchant_labels[k],
                                    ench_x,
                                    static_cast<float>(ench_y),
                                    static_cast<int32_t>(0xFFFFFFFFu),
                                    true);
                                ench_y += 8;
                            }
                        }
                        base_x += 16;
                    }
                }
            }

            glPopMatrix();
        }
    }

    if (ev->is_hand_pass) {
        minecraft::gl_state_disable_texture_2d(ctx->jni);
        minecraft::gl_state_disable_depth(ctx->jni);
    } else {
        glDisable(GL_TEXTURE_2D);
        glDisable(GL_DEPTH_TEST);
    }

    minecraft::render_item_set_z_level(ctx->jni, s_render_item, saved_z_level);
}

// Matches sub_18005DE00
void on_post_living_update(void* raw_event) noexcept {
    events::GameContext* ctx = static_cast<events::ContextEvent*>(raw_event)->ctx;
    clear_cached_nametags(ctx->jni);
    if (ctx->world == nullptr) {
        return;
    }

    JNIEnv* env = ctx->jni->env;
    if (s_render_item == nullptr) {
        jobject ri = minecraft::is_version_1_7_10(ctx->jni)
            ? minecraft::render_item_new_1_7(ctx->jni)
            : minecraft::mc_get_render_item(ctx->jni, ctx->minecraft);
        if (ri != nullptr) {
            s_render_item = env->NewGlobalRef(ri);
        }
    }

    const math::Vec3d local_pos = minecraft::entity_get_position(ctx->jni, ctx->local_player);
    const math::Vec3d local_prev_pos = minecraft::entity_get_prev_position(ctx->jni, ctx->local_player);
    jobject player_list = minecraft::world_get_player_entities(ctx->jni, ctx->world);

    jobject render_manager = minecraft::mc_get_render_manager(ctx->jni, ctx->minecraft);
    jobject font_renderer = minecraft::render_manager_get_font_renderer(ctx->jni, render_manager);
    if (font_renderer == nullptr) {
        return;
    }

    jstring id_key = env->NewStringUTF("id");
    jstring lvl_key = env->NewStringUTF("lvl");
    if (id_key == nullptr || lvl_key == nullptr) {
        return;
    }

    char text_buf[128]{};
    char ench_buf[8]{};
    EnchantEntry temp_enchants[kMaxParsedEnchants]{};

    for (int32_t i = 0; i < minecraft::java_list_size(ctx->jni, player_list); ++i) {
        jobject entity = minecraft::java_list_get(ctx->jni, player_list, i);
        const bool is_self = env->IsSameObject(entity, ctx->local_player) != JNI_FALSE;
        if (is_self) {
            if (!s_show_own_name_tag.value) {
                continue;
            }
            jobject settings = minecraft::mc_get_game_settings(ctx->jni, ctx->minecraft);
            if (minecraft::settings_get_third_person_view(ctx->jni, settings) == 0) {
                continue;
            }
        }

        const uint16_t tags = minecraft::player_get_tag_flags(ctx->jni, entity);
        if ((tags & minecraft::kPlayerTagBot) != 0) {
            continue;
        }
        if (minecraft::entity_is_dead(ctx->jni, entity)) {
            continue;
        }

        math::Vec3d pos = minecraft::entity_get_position(ctx->jni, entity);
        const double dist_sq = math::vec3_dist_sq(pos, local_pos);
        if (dist_sq > static_cast<double>(s_range.value * s_range.value)) {
            continue;
        }

        jstring base_jstr = nullptr;
        bool need_custom_string = true;
        if (s_use_display_name.value) {
            jobject comp = minecraft::entity_get_display_name(ctx->jni, entity);
            base_jstr = minecraft::chat_component_get_formatted_text(ctx->jni, comp);
            if (!s_show_health.value && !s_show_distance.value) {
                need_custom_string = false;
            }
        } else {
            base_jstr = minecraft::entity_get_name(ctx->jni, entity);
        }

        if (need_custom_string && base_jstr != nullptr) {
            const int32_t utf_len = env->GetStringUTFLength(base_jstr);
            const int32_t copy_len = std::min<int32_t>(utf_len, 64);
            const char* utf_chars = env->GetStringUTFChars(base_jstr, nullptr);

            util::StringBuilder sb{};
            sb.init(text_buf, sizeof(text_buf));
            bool ok = (utf_chars != nullptr);

            if (ok && s_show_health.value) {
                float hp = math::clampf(0.0f, 999.0f, minecraft::living_get_health(ctx->jni, entity));
                float abs_amt = math::clampf(0.0f, 99.0f, minecraft::player_get_absorption_amount(ctx->jni, entity));

                char hp_color = (hp < 14.0f) ? 'e' : 'a';
                if (hp < 8.0f) {
                    hp_color = 'c';
                }

                const int decimals = (s_health_fmt.value == 1) ? 1 : 0;
                if (s_health_fmt.value == 1) {
                    hp *= 0.5f;
                    abs_amt *= 0.5f;
                }

                ok = sb.append_fmt("\xC2\xA7%c%.*f", hp_color, decimals, static_cast<double>(hp));
                if (ok && abs_amt > 0.0f) {
                    ok = sb.append_fmt("\xC2\xA7" "6+%.*f", decimals, static_cast<double>(abs_amt));
                }
                if (ok) {
                    ok = sb.append_str("\xE2\x9D\xA4\xC2\xA7r ");
                }
            }

            if (ok) {
                if (s_use_display_name.value) {
                    ok = sb.append_str(utf_chars);
                } else {
                    const char team_col =
                        ((tags & (minecraft::kPlayerTagFriend | minecraft::kPlayerTagTeammate)) == 0)
                            ? 'c'
                            : 'a';
                    ok = sb.append_str("\xC2\xA7") && sb.append_char(team_col);
                    for (int32_t c = 0; ok && c < copy_len;) {
                        if (static_cast<uint8_t>(utf_chars[c]) == 0xC2u) {
                            c += 3;
                        } else {
                            ok = sb.append_char(utf_chars[c]);
                            ++c;
                        }
                    }
                }
            }

            if (ok && s_show_distance.value && !env->IsSameObject(ctx->local_player, entity)) {
                const double dist = std::sqrt(dist_sq);
                char dist_col = (dist < 19.999999046325684) ? 'e' : 'a';
                if (dist < 9.999999523162842) {
                    dist_col = 'c';
                }
                ok = sb.append_fmt("\xC2\xA7r \xC2\xA7%c%.0fm", dist_col, dist);
            }

            if (utf_chars != nullptr) {
                env->ReleaseStringUTFChars(base_jstr, utf_chars);
            }

            if (ok && sb.terminate()) {
                base_jstr = env->NewStringUTF(text_buf);
            }
        }

        if (base_jstr == nullptr) {
            break;
        }

        jstring tag_global = static_cast<jstring>(env->NewGlobalRef(base_jstr));
        if (tag_global == nullptr) {
            break;
        }

        jobject entity_global = env->NewGlobalRef(entity);
        if (entity_global == nullptr) {
            env->DeleteGlobalRef(tag_global);
            break;
        }

        math::Vec3d prev_pos = minecraft::entity_get_prev_position(ctx->jni, entity);
        if (is_self && minecraft::is_version_1_7_10(ctx->jni)) {
            pos = math::vec3_sub_eye_height(pos);
            prev_pos = math::vec3_sub_eye_height(prev_pos);
        }

        CachedNameTag& dst = s_nametags[s_nametag_count++];
        dst.prev_pos = prev_pos;
        dst.pos = pos;
        dst.entity_ref = entity_global;
        dst.tag_text = tag_global;
        dst.dist = std::sqrt(static_cast<float>(dist_sq));
        dst.prev_dist = static_cast<float>(math::vec3_dist(local_prev_pos, prev_pos));
        dst.tag_width = minecraft::font_renderer_get_string_width(ctx->jni, font_renderer, tag_global);
        dst.equipped_count = 0;

        for (size_t s = 0; s < kEquipmentSlotCount; ++s) {
            jobject stack = minecraft::player_get_equipment_in_slot(
                ctx->jni, entity, static_cast<int32_t>(s));
            dst.slots[s].has_item = (stack != nullptr);
            dst.slots[s].enchant_count = 0;
            dst.slots[s].enchant_labels = nullptr;
            if (stack != nullptr) {
                ++dst.equipped_count;
            }
        }

        if (s_show_enchants.value) {
            for (size_t s = 0; s < kEquipmentSlotCount; ++s) {
                jobject stack = minecraft::player_get_equipment_in_slot(
                    ctx->jni, entity, static_cast<int32_t>(s));
                if (stack == nullptr) {
                    continue;
                }
                jobject tag_list = minecraft::item_stack_get_enchantment_tag_list(ctx->jni, stack);
                if (tag_list == nullptr) {
                    continue;
                }

                const int32_t raw_count = minecraft::nbt_tag_list_count(ctx->jni, tag_list);
                const int32_t scan_limit = std::min<int32_t>(raw_count, kMaxParsedEnchants);
                int32_t valid_count = 0;

                for (int32_t e = 0; e < scan_limit; ++e) {
                    jobject compound = minecraft::nbt_tag_list_get_compound_at(ctx->jni, tag_list, e);
                    const int16_t ench_id = minecraft::nbt_tag_compound_get_short(ctx->jni, compound, id_key);
                    if (static_cast<uint16_t>(ench_id) > 62u || kEnchantShortNames[ench_id] == nullptr) {
                        continue;
                    }

                    int16_t ench_lvl = minecraft::nbt_tag_compound_get_short(ctx->jni, compound, lvl_key);
                    if (ench_lvl <= 0) {
                        ench_lvl = 0;
                    } else if (ench_lvl >= 99) {
                        ench_lvl = 99;
                    }

                    temp_enchants[valid_count++] = {ench_id, ench_lvl};
                }

                if (valid_count <= 0) {
                    continue;
                }

                std::qsort(temp_enchants, static_cast<size_t>(valid_count), sizeof(EnchantEntry), &compare_enchant_entries);
                const int32_t display_count = std::min<int32_t>(valid_count, kMaxDisplayedEnchants);
                auto* labels = static_cast<jstring*>(
                    util::zero_alloc(static_cast<size_t>(display_count), sizeof(jstring)));
                dst.slots[s].enchant_labels = labels;
                if (labels == nullptr) {
                    break;
                }

                bool alloc_failed = false;
                for (int32_t e = 0; e < display_count; ++e) {
                    const uint16_t eid = static_cast<uint16_t>(temp_enchants[e].id);
                    const char* short_name = (eid <= 62u) ? kEnchantShortNames[eid] : nullptr;
                    std::snprintf(ench_buf, 6, "%s %d", short_name, static_cast<int>(temp_enchants[e].level));
                    jstring local_lbl = env->NewStringUTF(ench_buf);
                    if (local_lbl == nullptr) {
                        alloc_failed = true;
                        break;
                    }
                    jstring global_lbl = static_cast<jstring>(env->NewGlobalRef(local_lbl));
                    if (global_lbl == nullptr) {
                        alloc_failed = true;
                        break;
                    }
                    labels[e] = global_lbl;
                    ++dst.slots[s].enchant_count;
                }
                if (alloc_failed) {
                    break;
                }
            }
        }

        if (s_nametag_count == kMaxCachedNameTags) {
            break;
        }
    }

    std::qsort(s_nametags, s_nametag_count, sizeof(CachedNameTag), &compare_nametags_by_dist);
}

void render_settings(gui::ClickGui* gui, gui::GuiRenderer* renderer) noexcept {
    gui::widgets::render_section_header(gui, renderer, "Main");
    s_range.render(gui, renderer);
    s_scale.render(gui, renderer);
    s_show_own_name_tag.render(gui, renderer);
    s_use_display_name.render(gui, renderer);
    s_distance_scaling.render(gui, renderer);
    s_background.render(gui, renderer);
    s_text_shadow.render(gui, renderer);
    gui::widgets::render_section_header(gui, renderer, "Extra info");
    s_show_health.render(gui, renderer);
    if (s_show_health.value) {
        s_health_fmt.render(gui, renderer);
    }
    s_show_distance.render(gui, renderer);
    s_show_equipment.render(gui, renderer);
    if (s_show_equipment.value) {
        s_show_enchants.render(gui, renderer);
    }
}

void visit_config(config::ConfigArchive* ar) noexcept {
    s_range.visit(ar, "range");
    s_scale.visit(ar, "scale");
    s_show_own_name_tag.visit(ar, "show_own_name_tag");
    s_use_display_name.visit(ar, "use_display_name");
    s_distance_scaling.visit(ar, "distance_scaling");
    s_background.visit(ar, "background");
    s_text_shadow.visit(ar, "text_shadow");
    s_show_health.visit(ar, "show_health");
    s_health_fmt.visit(ar, "health_fmt");
    s_show_distance.visit(ar, "show_distance");
    s_show_equipment.visit(ar, "show_equipment");
    s_show_enchants.visit(ar, "show_enchants");
}

events::Module create_module() noexcept {
    events::Module mod{};
    mod.name = "Name Tags";
    mod.description = "Makes player name tags easier to see.";
    mod.category = events::ModuleCategory::Visual;
    mod.events[events::kEventModuleStateChanged] = {&on_module_state_changed, 0};
    mod.events[events::kEventRenderWorld2DProj] = {&on_render_world_2d, -1000};
    mod.events[events::kEventPostLivingUpdate] = {&on_post_living_update, 0};
    mod.render_settings = &render_settings;
    mod.visit_config = &visit_config;
    return mod;
}

} // namespace

// Matches sub_18005D570
bool nametags_can_render_vanilla_name(
    events::JniContext* jni,
    jobject renderer,
    jobject entity) noexcept {
    if (get_name_tags_module()->enabled &&
        minecraft::as_entity_player(jni, entity) != nullptr &&
        !minecraft::player_has_tag_flags(jni, entity, minecraft::kPlayerTagBot)) {
        return false;
    }
    return minecraft::renderer_living_can_render_name(jni, renderer, entity);
}

events::Module* get_name_tags_module() noexcept {
    static events::Module s_module = create_module();
    return &s_module;
}

} // namespace slinky::modules
