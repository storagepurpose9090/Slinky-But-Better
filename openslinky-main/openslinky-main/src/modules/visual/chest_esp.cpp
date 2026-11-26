#include "modules/visual/chest_esp.hpp"

#include <glad/glad.h>

#include "config/settings.hpp"
#include "gui/widgets.hpp"
#include "minecraft/game_helpers.hpp"
#include "minecraft/mc_wrappers.hpp"
#include "util/math.hpp"

namespace slinky::modules {

namespace {

constexpr int32_t kBlockChest        = 54;
constexpr int32_t kBlockEnderChest   = 130;
constexpr int32_t kBlockBeacon       = 138;
constexpr int32_t kBlockTrappedChest = 146;
constexpr size_t kMaxCachedChests    = 100;

struct CachedTileEntity {
    math::Vec3d pos{};
    int32_t block_id = 0;
    int32_t pad = 0;
};

config::FloatSetting s_range{
    {}, "Range", "%0.0f blocks", 0.0f, 0.0f, 10.0f, 100.0f, 20.0f, 0.0f
};
config::BoolSetting s_render_chest{{}, "Regular chests", true, false};
config::BoolSetting s_render_ender_chest{{}, "Ender chests", true, false};
config::BoolSetting s_render_trapped_chest{{}, "Trapped chests", true, false};
config::BoolSetting s_render_beacon{{}, "Beacons", true, false};
config::FloatSetting s_line_width{
    {}, "Line width", "%0.0f px", 1.0f, 0.0f, 1.0f, 4.0f, 1.0f, 0.0f
};
config::ColorSetting s_color_chest{"Regular chests", {1.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 0.0f}};
config::ColorSetting s_color_ender_chest{"Ender chests", {1.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}};
config::ColorSetting s_color_trapped_chest{"Trapped chests", {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}};
config::ColorSetting s_color_beacon{"Beacons", {0.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}};
config::BoolSetting s_filled{{}, "Filled", true, false};

size_t s_chest_count = 0;
CachedTileEntity s_chests[kMaxCachedChests]{};

// Matches sub_1800550F0
void on_game_tick(void* raw_event) noexcept {
    s_chest_count = 0;
    events::GameContext* ctx = static_cast<events::ContextEvent*>(raw_event)->ctx;
    if (ctx->world == nullptr) {
        return;
    }

    const math::Vec3d player_pos = minecraft::entity_get_position(ctx->jni, ctx->local_player);
    jobject tile_list = minecraft::world_get_loaded_tile_entity_list(ctx->jni, ctx->world);

    for (int32_t i = 0; i < minecraft::java_list_size(ctx->jni, tile_list); ++i) {
        jobject tile_entity = minecraft::java_list_get(ctx->jni, tile_list, i);
        jobject block = minecraft::tile_entity_get_block_type(ctx->jni, tile_entity);
        if (block == nullptr) {
            continue;
        }

        const int32_t block_id = minecraft::block_get_id(ctx->jni, block);
        if (block_id > 137) {
            if (block_id == kBlockBeacon) {
                if (!s_render_beacon.value) {
                    continue;
                }
            } else if (block_id != kBlockTrappedChest || !s_render_trapped_chest.value) {
                continue;
            }
        } else if (block_id == kBlockChest) {
            if (!s_render_chest.value) {
                continue;
            }
        } else if (block_id != kBlockEnderChest || !s_render_ender_chest.value) {
            continue;
        }

        const math::Vec3d te_pos = minecraft::tile_entity_get_position(ctx->jni, tile_entity);
        const float max_dist_sq = s_range.value * s_range.value;
        if (math::vec3_dist_sq(te_pos, player_pos) <= static_cast<double>(max_dist_sq)) {
            CachedTileEntity& dst = s_chests[s_chest_count++];
            dst.pos = te_pos;
            dst.block_id = block_id;
            if (s_chest_count == kMaxCachedChests) {
                return;
            }
        }
    }
}

// Matches sub_1800552C0
void on_render_world_3d(void* raw_event) noexcept {
    const auto* ev = static_cast<const events::RenderWorld3DEvent*>(raw_event);

    minecraft::gl_bind_unit_cube_vertices();
    glLineWidth(s_line_width.value);

    for (size_t i = 0; i < s_chest_count; ++i) {
        const CachedTileEntity& entry = s_chests[i];
        const math::Color3f* color = &s_color_chest.value;
        if (entry.block_id == kBlockEnderChest) {
            color = &s_color_ender_chest.value;
        } else if (entry.block_id == kBlockBeacon) {
            color = &s_color_beacon.value;
        } else if (entry.block_id == kBlockTrappedChest) {
            color = &s_color_trapped_chest.value;
        }

        const math::Vec3d center = math::vec3_add(
            math::vec3_sub(entry.pos, ev->render_pos),
            {0.5, 0.5, 0.5});

        glPushMatrix();
        glTranslatef(
            static_cast<float>(center.x),
            static_cast<float>(center.y),
            static_cast<float>(center.z));
        glScalef(0.5f, 0.5f, 0.5f);

        if (s_filled.value) {
            glColor4f(color->r, color->g, color->b, 0.4f);
            minecraft::gl_draw_unit_cube_filled();
        }

        glColor3f(color->r, color->g, color->b);
        minecraft::gl_draw_unit_cube_outline();
        glPopMatrix();
    }

    minecraft::gl_unbind_vertex_array();
}

void render_settings(gui::ClickGui* gui, gui::GuiRenderer* renderer) noexcept {
    gui::widgets::render_section_header(gui, renderer, "Main");
    s_range.render(gui, renderer);
    gui::widgets::render_section_header(gui, renderer, "Target");
    s_render_chest.render(gui, renderer);
    s_render_ender_chest.render(gui, renderer);
    s_render_trapped_chest.render(gui, renderer);
    s_render_beacon.render(gui, renderer);
    gui::widgets::render_section_header(gui, renderer, "Customization");
    s_line_width.render(gui, renderer);
    if (s_render_chest.value) {
        s_color_chest.render(gui, renderer);
    }
    if (s_render_ender_chest.value) {
        s_color_ender_chest.render(gui, renderer);
    }
    if (s_render_trapped_chest.value) {
        s_color_trapped_chest.render(gui, renderer);
    }
    if (s_render_beacon.value) {
        s_color_beacon.render(gui, renderer);
    }
    s_filled.render(gui, renderer);
}

void visit_config(config::ConfigArchive* ar) noexcept {
    s_range.visit(ar, "range");
    s_render_chest.visit(ar, "render_chest");
    s_render_ender_chest.visit(ar, "render_ender_chest");
    s_render_trapped_chest.visit(ar, "render_trapped_chest");
    s_render_beacon.visit(ar, "render_beacon");
    s_line_width.visit(ar, "line_width");
    s_color_chest.visit(ar, "color_chest");
    s_color_ender_chest.visit(ar, "color_ender_chest");
    s_color_trapped_chest.visit(ar, "color_trapped_chest");
    s_color_beacon.visit(ar, "color_beacon");
    s_filled.visit(ar, "filled");
}

events::Module create_module() noexcept {
    events::Module mod{};
    mod.name = "Chest ESP";
    mod.description = "Highlights chests.";
    mod.category = events::ModuleCategory::Visual;
    mod.events[events::kEventGameTick] = {&on_game_tick, 0};
    mod.events[events::kEventRenderWorld3D] = {&on_render_world_3d, 0};
    mod.render_settings = &render_settings;
    mod.visit_config = &visit_config;
    return mod;
}

} // namespace

events::Module* get_chest_esp_module() noexcept {
    static events::Module s_module = create_module();
    return &s_module;
}

} // namespace slinky::modules
