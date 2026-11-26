#include "modules/utility/teams.hpp"

#include <cstring>

#include "config/settings.hpp"
#include "gui/widgets.hpp"
#include "minecraft/game_helpers.hpp"
#include "minecraft/mc_wrappers.hpp"
#include "util/containers.hpp"

namespace slinky::modules {

namespace {

extern events::Module s_module;

constexpr const char* kTeamColorNames[16] = {
    "Black",
    "Dark blue",
    "Dark green",
    "Dark aqua",
    "Dark red",
    "Dark purple",
    "Gold",
    "Light gray",
    "Dark gray",
    "Light blue",
    "Light green",
    "Light aqua",
    "Light red",
    "Light purple",
    "Yellow",
    "White",
};

util::PodVector<int8_t> s_team_colors{};

// sub_1800538E0
size_t validate_color_input(char* buffer, char ch) noexcept {
    const size_t len = std::strlen(buffer);
    const uint8_t uch = static_cast<uint8_t>(ch);
    if (len <= 0x63u &&
        (static_cast<uint8_t>(uch - 'a') < 26u ||
         static_cast<uint8_t>(uch - 'A') < 26u ||
         ch == ' ')) {
        buffer[len] = ch;
    }
    return len;
}

// sub_180053930
size_t color_list_get_count(void* /*ctx*/) noexcept {
    return static_cast<size_t>(static_cast<uint32_t>(s_team_colors.count));
}

// sub_180053940
const char* color_list_get_name(void* /*ctx*/, size_t index) noexcept {
    return kTeamColorNames[static_cast<int>(s_team_colors.data[index])];
}

// sub_180053980
bool color_list_add_item(void* /*ctx*/, const char* name, const math::Color3f* /*color*/) noexcept {
    int8_t color_idx = -1;
    for (int8_t i = 0; i < 16; ++i) {
        if (std::strcmp(name, kTeamColorNames[i]) == 0) {
            color_idx = i;
            break;
        }
    }
    if (color_idx < 0) {
        return false;
    }

    uint32_t lo = 0;
    uint32_t hi = static_cast<uint32_t>(s_team_colors.count);
    while (lo < hi) {
        const uint32_t mid = lo + ((hi - lo) >> 1);
        const int cmp = static_cast<int>(color_idx) - static_cast<int>(s_team_colors.data[mid]);
        if (cmp < 0) {
            hi = mid;
        } else if (cmp == 0) {
            return true;
        } else {
            lo = mid + 1;
        }
    }
    s_team_colors.insert_at(static_cast<int>(lo), color_idx);
    return true;
}

// sub_180053B60
void color_list_remove_at(void* /*ctx*/, size_t index) noexcept {
    s_team_colors.erase_at(static_cast<int>(index));
}

// sub_180053B80
void color_list_clear_all(void* /*ctx*/) noexcept {
    s_team_colors.count = 0;
}

// sub_180053B90
void color_list_get_options(const char* const** out_items, size_t* out_count) noexcept {
    *out_items = kTeamColorNames;
    *out_count = 16;
}

bool color_list_contains(int8_t color_idx) noexcept {
    uint32_t lo = 0;
    uint32_t hi = static_cast<uint32_t>(s_team_colors.count);
    while (lo < hi) {
        const uint32_t mid = lo + ((hi - lo) >> 1);
        const int cmp = static_cast<int>(color_idx) - static_cast<int>(s_team_colors.data[mid]);
        if (cmp < 0) {
            hi = mid;
        } else if (cmp == 0) {
            return true;
        } else {
            lo = mid + 1;
        }
    }
    return false;
}

config::BoolSetting s_match_self_color{
    {},
    "Match own nametag",
    true,
    false,
    {},
};

config::DynamicListSetting s_team_colors_setting{
    "Colors",
    "Search color",
    validate_color_input,
    config::DynamicListItemKind::StringOnly,
    0,
    &s_team_colors,
    color_list_get_count,
    color_list_get_name,
    nullptr,
    color_list_add_item,
    color_list_remove_at,
    color_list_clear_all,
    color_list_get_options,
    false,
    {},
};

uint16_t resolve_local_player_team_color(events::GameContext* ctx) noexcept {
    events::JniContext* jni = ctx->jni;
    if (!mc::is_version_1_7_10(jni)) {
        jobject net_handler = mc::mc_get_net_handler(jni, ctx->minecraft);
        jobject info_map = mc::net_handler_get_player_info_map(jni, net_handler);
        jobject uuid = mc::entity_get_unique_id(jni, ctx->local_player);
        jobject player_info = mc::java_map_get(jni, info_map, uuid);
        if (player_info != nullptr) {
            jobject display_name = mc::network_player_info_get_display_name(jni, player_info);
            if (display_name != nullptr) {
                jstring formatted = mc::chat_component_get_formatted_text(jni, display_name);
                const uint16_t code = mc::extract_name_color_code_jstring(jni, formatted);
                return (code != 0) ? code : static_cast<uint16_t>('r');
            }
            jobject team = mc::network_player_info_get_player_team(jni, player_info);
            if (team != nullptr) {
                return mc::score_player_team_get_color_code(jni, team);
            }
        }
    }
    return mc::entity_get_team_color_code(jni, ctx->local_player);
}

// sub_180053570
void on_game_tick(void* raw_event) noexcept {
    auto* event = static_cast<events::ContextEvent*>(raw_event);
    events::GameContext* ctx = event->ctx;
    if (ctx->local_player == nullptr) {
        return;
    }

    events::JniContext* jni = ctx->jni;
    const uint16_t self_color = resolve_local_player_team_color(ctx);
    jobject player_list = mc::world_get_player_entities(jni, ctx->world);

    for (int32_t i = 0; i < mc::java_list_size(jni, player_list); ++i) {
        jobject player = mc::java_list_get(jni, player_list, i);
        if (jni->env->IsSameObject(ctx->local_player, player)) {
            continue;
        }

        const uint16_t player_color = mc::entity_get_team_color_code(jni, player);
        bool is_teammate = (s_match_self_color.value && player_color == self_color);
        if (!is_teammate) {
            const int8_t offset = (static_cast<int8_t>(player_color) >= 'a') ? -87 : -48;
            const int8_t color_idx = static_cast<int8_t>(static_cast<int8_t>(player_color) + offset);
            is_teammate = color_list_contains(color_idx);
        }

        if (is_teammate) {
            mc::player_add_tag_flags(jni, player, mc::kPlayerTagTeammate);
        } else {
            mc::player_remove_tag_flags(jni, player, mc::kPlayerTagTeammate);
        }
    }
}

// sub_180053750
void on_module_state_changed(void* raw_event) noexcept {
    auto* event = static_cast<events::ModuleStateChangedEvent*>(raw_event);
    if (event->module == &s_module && !event->enabled) {
        events::GameContext* ctx = event->ctx;
        if (ctx->world != nullptr) {
            events::JniContext* jni = ctx->jni;
            jobject player_list = mc::world_get_player_entities(jni, ctx->world);
            for (int32_t i = 0; i < mc::java_list_size(jni, player_list); ++i) {
                jobject player = mc::java_list_get(jni, player_list, i);
                if (!jni->env->IsSameObject(ctx->local_player, player)) {
                    mc::player_remove_tag_flags(jni, player, mc::kPlayerTagTeammate);
                }
            }
        }
    }
}

// sub_180053810
void on_client_shutdown(void* /*raw_event*/) noexcept {
    s_team_colors.clear_and_free();
}

// sub_180053840
void render_settings(gui::ClickGui* gui, gui::GuiRenderer* renderer) noexcept {
    gui::widgets::render_section_header(gui, renderer, "Main");
    s_match_self_color.render(gui, renderer);
    s_team_colors_setting.render(gui, renderer);
}

// sub_180053890
void visit_config(config::ConfigArchive* archive) noexcept {
    s_match_self_color.visit(archive, "match_self_color");
    s_team_colors_setting.visit(archive, "team_colors");
}

events::Module make_module() noexcept {
    events::Module mod{};
    mod.name = "Teams";
    mod.description = "Ignores teammates for specific modules.";
    mod.category = events::ModuleCategory::Utility;
    mod.enabled = false;
    mod.flags = events::kModuleFlagNone;
    mod.keybind = 0;
    mod.hold_mode = false;
    mod.events[events::kEventGameTick] = {on_game_tick, 10000, 0};
    mod.events[events::kEventModuleStateChanged] = {on_module_state_changed, 0, 0};
    mod.events[events::kEventClientShutdown] = {on_client_shutdown, 0, 0};
    mod.render_settings = render_settings;
    mod.visit_config = visit_config;
    return mod;
}

events::Module s_module = make_module();

} // namespace

events::Module* get_teams_module() noexcept {
    return &s_module;
}

} // namespace slinky::modules
