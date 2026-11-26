#include "modules/visual/item_esp.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <glad/glad.h>

#include "config/settings.hpp"
#include "gui/widgets.hpp"
#include "minecraft/game_helpers.hpp"
#include "minecraft/item_database.hpp"
#include "minecraft/mc_wrappers.hpp"
#include "util/containers.hpp"
#include "util/math.hpp"

namespace slinky::modules {

namespace {

constexpr size_t kMaxCachedItems = 100;

// 24-byte allowed item entry (@ 0x180321590)
struct AllowedItemEntry {
    const char* name = nullptr; // +0x00
    math::Color3f color{};      // +0x08
    int32_t item_id = 0;        // +0x14
};
static_assert(sizeof(AllowedItemEntry) == 0x18, "AllowedItemEntry must be 24 bytes");

struct AllowedItemList {
    size_t count = 0;                // +0x00
    size_t capacity = 0;             // +0x08
    AllowedItemEntry* data = nullptr;// +0x10
};

// 88-byte cached dropped item entry (@ 0x18031F320)
struct CachedItemEntity {
    math::Vec3d prev_pos{};           // +0x00
    math::Vec3d pos{};                // +0x18
    math::Color3f color{};            // +0x30
    int32_t pad0 = 0;                 // +0x3C
    jstring count_label = nullptr;    // +0x40 (JNI GlobalRef)
    int32_t label_width = 0;          // +0x48
    int32_t item_id = 0;              // +0x4C
    uint8_t stack_count = 0;          // +0x50
    uint8_t pad1[7]{};                // +0x51..+0x57
};
static_assert(sizeof(CachedItemEntity) == 0x58, "CachedItemEntity must be 88 bytes");

size_t s_item_count = 0;                       // qword_18031F310
CachedItemEntity s_items[kMaxCachedItems]{};   // unk_18031F320
AllowedItemList s_allowed_item_list{};         // xmmword_180321580..0x321590

// Matches sub_180062C30
int compare_item_id_key(const void* key, const void* elem) noexcept {
    const int32_t id = *static_cast<const int32_t*>(key);
    const auto* entry = static_cast<const AllowedItemEntry*>(elem);
    return id - entry->item_id;
}

const AllowedItemEntry* find_allowed_item(int32_t item_id) noexcept {
    if (s_allowed_item_list.data == nullptr || s_allowed_item_list.count == 0) {
        return nullptr;
    }
    return static_cast<const AllowedItemEntry*>(std::bsearch(
        &item_id,
        s_allowed_item_list.data,
        s_allowed_item_list.count,
        sizeof(AllowedItemEntry),
        &compare_item_id_key));
}

// DynamicListSetting callbacks (0x62C40..0x62E60)
size_t validate_item_search_char(char* buffer, char ch) noexcept {
    const size_t len = std::strlen(buffer);
    if (len > 99) {
        return len;
    }
    const uint8_t upper = static_cast<uint8_t>((static_cast<uint8_t>(ch) & 0xDFu) - 'A');
    const uint8_t digit = static_cast<uint8_t>(static_cast<uint8_t>(ch) - '0');
    if (upper < 26u || digit < 10u || ch == ' ') {
        buffer[len] = ch;
    }
    return len;
}

size_t item_list_get_count(void* ctx) noexcept {
    return static_cast<const AllowedItemList*>(ctx)->count;
}

const char* item_list_get_name(void* ctx, size_t index) noexcept {
    return static_cast<const AllowedItemList*>(ctx)->data[index].name;
}

math::Color3f* item_list_get_color(void* ctx, size_t index) noexcept {
    return &static_cast<AllowedItemList*>(ctx)->data[index].color;
}

bool item_list_add_item(void* ctx, const char* name, const math::Color3f* color) noexcept {
    auto* list = static_cast<AllowedItemList*>(ctx);
    const int32_t id = minecraft::find_item_id_by_name(name);
    if (id == -1) {
        return false;
    }
    if (find_allowed_item(id) != nullptr) {
        return false;
    }

    size_t insert_idx = 0;
    while (insert_idx < list->count && id > list->data[insert_idx].item_id) {
        ++insert_idx;
    }

    const char* canonical_name = minecraft::find_item_name_by_id(id);
    const math::Color3f saved_color = *color;

    if (!util::ensure_array_capacity(
            reinterpret_cast<void**>(&list->data),
            sizeof(AllowedItemEntry),
            list->count,
            &list->capacity)) {
        return false;
    }

    std::memmove(
        &list->data[insert_idx + 1],
        &list->data[insert_idx],
        (list->count - insert_idx) * sizeof(AllowedItemEntry));

    list->data[insert_idx].name = canonical_name;
    list->data[insert_idx].color = saved_color;
    list->data[insert_idx].item_id = id;
    ++list->count;
    return true;
}

void item_list_remove_at(void* ctx, size_t index) noexcept {
    auto* list = static_cast<AllowedItemList*>(ctx);
    --list->count;
    std::memmove(
        &list->data[index],
        &list->data[index + 1],
        (list->count - index) * sizeof(AllowedItemEntry));
}

void item_list_clear_all(void* ctx) noexcept {
    auto* list = static_cast<AllowedItemList*>(ctx);
    list->count = 0;
    list->capacity = 0;
    std::free(list->data);
    list->data = nullptr;
}

void item_list_get_options(const char* const** out_items, size_t* out_count) noexcept {
    minecraft::get_all_item_names(out_items, out_count);
}

config::FloatSetting s_range{
    {}, "Range", "%0.0f blocks", 0.0f, 0.0f, 20.0f, 100.0f, 50.0f, 0.0f
};
config::BoolSetting s_show_count{{}, "Show count", false, false};
config::DynamicListSetting s_allowed_items{
    "Item types",
    "Search item",
    &validate_item_search_char,
    config::DynamicListItemKind::ColoredEntry,
    0,
    &s_allowed_item_list,
    &item_list_get_count,
    &item_list_get_name,
    &item_list_get_color,
    &item_list_add_item,
    &item_list_remove_at,
    &item_list_clear_all,
    &item_list_get_options,
    false
};
config::BoolSetting s_highlight_all{{}, "Highlight all items", false, false};
config::ColorSetting s_default_color{"Default color", {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}};

// Matches sub_1800621C0
void on_render_world_3d(void* raw_event) noexcept {
    if (s_item_count == 0) {
        return;
    }

    const auto* ev = static_cast<const events::RenderWorld3DEvent*>(raw_event);
    const double partial = static_cast<double>(ev->partial_ticks);

    glEnable(GL_BLEND);
    minecraft::gl_bind_unit_cube_vertices();

    for (size_t i = 0; i < s_item_count; ++i) {
        const CachedItemEntity& entry = s_items[i];
        const math::Vec3d interp = math::vec3_lerp(entry.prev_pos, entry.pos, partial);
        const math::Vec3d rel = math::vec3_sub(interp, ev->render_pos);

        glPushMatrix();
        glTranslatef(
            static_cast<float>(rel.x),
            static_cast<float>(rel.y + 0.3),
            static_cast<float>(rel.z));
        glScalef(0.3f, 0.3f, 0.3f);
        glColor4f(entry.color.r, entry.color.g, entry.color.b, 0.4f);
        minecraft::gl_draw_unit_cube_filled();
        glPopMatrix();
    }

    minecraft::gl_unbind_vertex_array();

    if (!s_show_count.value) {
        return;
    }

    events::GameContext* ctx = ev->ctx;
    jobject render_manager = minecraft::mc_get_render_manager(ctx->jni, ctx->minecraft);
    jobject font_renderer = minecraft::render_manager_get_font_renderer(ctx->jni, render_manager);
    const float view_x = minecraft::render_manager_get_player_view_x(ctx->jni, render_manager);
    const float neg_view_y = -minecraft::render_manager_get_player_view_y(ctx->jni, render_manager);

    if (ev->is_hand_pass) {
        minecraft::gl_state_enable_texture_2d(ctx->jni);
        minecraft::gl_state_color(ctx->jni, 1.0f, 1.0f, 1.0f, 1.0f);
    } else {
        glEnable(GL_TEXTURE_2D);
        glColor3f(1.0f, 1.0f, 1.0f);
    }

    for (size_t i = 0; i < s_item_count; ++i) {
        const CachedItemEntity& entry = s_items[i];
        if (entry.count_label == nullptr) {
            continue;
        }

        const math::Vec3d interp = math::vec3_lerp(entry.prev_pos, entry.pos, partial);
        const math::Vec3d rel = math::vec3_sub(interp, ev->render_pos);

        glPushMatrix();
        glTranslatef(
            static_cast<float>(rel.x),
            static_cast<float>(rel.y + 0.3),
            static_cast<float>(rel.z));
        glRotatef(neg_view_y, 0.0f, 1.0f, 0.0f);
        glRotatef(view_x, 1.0f, 0.0f, 0.0f);
        glScalef(-0.05f, -0.05f, -0.05f);

        const float text_x = static_cast<float>(static_cast<double>(-entry.label_width) * 0.5);
        minecraft::font_renderer_draw_string(
            ctx->jni,
            font_renderer,
            entry.count_label,
            text_x,
            -4.0f,
            static_cast<int32_t>(0xFFFFFFFFu),
            true);
        glPopMatrix();
    }

    if (ev->is_hand_pass) {
        minecraft::gl_state_disable_texture_2d(ctx->jni);
    } else {
        glDisable(GL_TEXTURE_2D);
    }
}

// Matches sub_180062640
void on_post_living_update(void* raw_event) noexcept {
    events::GameContext* ctx = static_cast<events::ContextEvent*>(raw_event)->ctx;
    JNIEnv* env = ctx->jni->env;

    for (size_t i = 0; i < s_item_count; ++i) {
        if (s_items[i].count_label != nullptr) {
            env->DeleteGlobalRef(s_items[i].count_label);
            s_items[i].count_label = nullptr;
        }
    }
    s_item_count = 0;

    if (ctx->world == nullptr) {
        return;
    }

    const math::Vec3d local_pos = minecraft::entity_get_position(ctx->jni, ctx->local_player);
    jobject entity_list = minecraft::world_get_loaded_entity_list(ctx->jni, ctx->world);

    for (int32_t i = 0; i < minecraft::java_list_size(ctx->jni, entity_list); ++i) {
        jobject raw_ent = minecraft::java_list_get(ctx->jni, entity_list, i);
        jobject item_ent = minecraft::as_entity_item(ctx->jni, raw_ent);
        if (item_ent == nullptr || minecraft::entity_is_dead(ctx->jni, item_ent)) {
            continue;
        }

        const math::Vec3d pos = minecraft::entity_get_position(ctx->jni, item_ent);
        if (math::vec3_dist_sq(pos, local_pos) > static_cast<double>(s_range.value * s_range.value)) {
            continue;
        }

        jobject stack = minecraft::entity_item_get_stack(ctx->jni, item_ent);
        if (stack == nullptr) {
            continue;
        }
        const int32_t stack_size = minecraft::item_stack_get_size(ctx->jni, stack);
        if (stack_size <= 0) {
            continue;
        }
        jobject item = minecraft::item_stack_get_item(ctx->jni, stack);
        if (item == nullptr) {
            continue;
        }

        const int32_t item_id = minecraft::item_get_id(ctx->jni, item);
        const AllowedItemEntry* matched = find_allowed_item(item_id);
        if (matched == nullptr && !s_highlight_all.value) {
            continue;
        }

        CachedItemEntity& dst = s_items[s_item_count++];
        dst.color = (matched != nullptr) ? matched->color : s_default_color.value;
        dst.item_id = item_id;
        dst.stack_count = static_cast<uint8_t>(stack_size);
        dst.pos = pos;
        dst.prev_pos = minecraft::entity_get_prev_position(ctx->jni, item_ent);
        dst.count_label = nullptr;
        dst.label_width = 0;

        if (s_item_count == kMaxCachedItems) {
            break;
        }
    }

    if (!s_show_count.value || s_item_count == 0) {
        return;
    }

    jobject render_manager = minecraft::mc_get_render_manager(ctx->jni, ctx->minecraft);
    jobject font_renderer = minecraft::render_manager_get_font_renderer(ctx->jni, render_manager);
    if (font_renderer == nullptr) {
        return;
    }

    char count_buf[8]{};
    for (size_t i = 0; i < s_item_count; ++i) {
        if (s_items[i].stack_count == 0) {
            continue;
        }

        size_t total = s_items[i].stack_count;
        for (size_t j = 0; j < s_item_count; ++j) {
            if (i == j || s_items[j].item_id != s_items[i].item_id) {
                continue;
            }
            if (math::vec3_dist_sq(s_items[i].pos, s_items[j].pos) < 0.0625) {
                total += s_items[j].stack_count;
                s_items[j].stack_count = 0;
            }
        }

        const uint32_t clamped = (total < 99u) ? static_cast<uint32_t>(total) : 99u;
        s_items[i].stack_count = static_cast<uint8_t>(clamped);
        if (total < 2u) {
            s_items[i].count_label = nullptr;
            continue;
        }

        std::snprintf(count_buf, 5, "%u", clamped);
        jstring local_lbl = env->NewStringUTF(count_buf);
        if (local_lbl != nullptr) {
            s_items[i].count_label = static_cast<jstring>(env->NewGlobalRef(local_lbl));
            if (s_items[i].count_label != nullptr) {
                s_items[i].label_width = minecraft::font_renderer_get_string_width(
                    ctx->jni, font_renderer, s_items[i].count_label);
            }
        } else {
            s_items[i].count_label = nullptr;
        }
    }
}

// Matches sub_180062AC0
void on_client_shutdown(void* /*raw_event*/) noexcept {
    s_allowed_item_list.count = 0;
    s_allowed_item_list.capacity = 0;
    std::free(s_allowed_item_list.data);
    s_allowed_item_list.data = nullptr;
}

void render_settings(gui::ClickGui* gui, gui::GuiRenderer* renderer) noexcept {
    gui::widgets::render_section_header(gui, renderer, "Main");
    s_range.render(gui, renderer);
    gui::widgets::render_section_header(gui, renderer, "Customization");
    s_show_count.render(gui, renderer);
    s_allowed_items.render(gui, renderer);
    s_highlight_all.render(gui, renderer);
    if (s_highlight_all.value) {
        s_default_color.render(gui, renderer);
    }
}

void visit_config(config::ConfigArchive* ar) noexcept {
    s_range.visit(ar, "range");
    s_show_count.visit(ar, "show_count");
    s_allowed_items.visit(ar, "allowed_items");
    s_highlight_all.visit(ar, "highlight_all");
    s_default_color.visit(ar, "default_color");
}

events::Module create_module() noexcept {
    events::Module mod{};
    mod.name = "Item ESP";
    mod.description = "Highlights dropped items.";
    mod.category = events::ModuleCategory::Visual;
    mod.events[events::kEventRenderWorld3D] = {&on_render_world_3d, -200};
    mod.events[events::kEventPostLivingUpdate] = {&on_post_living_update, 0};
    mod.events[events::kEventClientShutdown] = {&on_client_shutdown, 0};
    mod.render_settings = &render_settings;
    mod.visit_config = &visit_config;
    return mod;
}

} // namespace

events::Module* get_item_esp_module() noexcept {
    static events::Module s_module = create_module();
    return &s_module;
}

} // namespace slinky::modules
