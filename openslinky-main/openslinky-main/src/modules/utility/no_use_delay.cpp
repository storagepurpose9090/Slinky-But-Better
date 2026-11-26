#include "modules/utility/no_use_delay.hpp"

#include "config/settings.hpp"
#include "gui/widgets.hpp"
#include "minecraft/mc_wrappers.hpp"

namespace slinky::modules {

namespace {

config::BoolSetting s_allow_desync{
    {},
    "Allow desync abuse",
    false,
    false,
    {},
};

int32_t s_remaining_use_ticks = 0;
int32_t s_last_hotbar_slot = 0;

// sub_18005FCE0
void on_post_living_update(void* raw_event) noexcept {
    auto* event = static_cast<events::ContextEvent*>(raw_event);
    events::GameContext* ctx = event->ctx;

    if (ctx->in_game_focus) {
        events::JniContext* jni = ctx->jni;
        jobject settings = mc::mc_get_game_settings(jni, ctx->minecraft);
        jobject use_key = mc::settings_get_key_use_item(jni, settings);
        const bool was_pressed = mc::key_binding_is_pressed(jni, use_key);
        jobject inv = mc::player_get_inventory(jni, ctx->local_player);
        const int32_t current_slot = mc::inv_player_get_current_item(jni, inv);

        if (current_slot != s_last_hotbar_slot) {
            s_remaining_use_ticks = 0;
            s_last_hotbar_slot = current_slot;
            const bool physically_down = mc::key_binding_is_physically_down(jni, use_key);
            mc::key_binding_set_pressed(jni, use_key, physically_down);
        }

        if (was_pressed) {
            const int32_t in_use_count = mc::player_get_item_in_use_count(jni, ctx->local_player);
            const int32_t prev_ticks = s_remaining_use_ticks;
            if (prev_ticks <= 0) {
                s_remaining_use_ticks = in_use_count;
            } else {
                --s_remaining_use_ticks;
                if (prev_ticks == 1 || (s_allow_desync.value && in_use_count == 0)) {
                    mc::key_binding_set_pressed(jni, use_key, false);
                }
            }
        }
    } else {
        s_remaining_use_ticks = 0;
    }
}

// sub_18005FDE0
void render_settings(gui::ClickGui* gui, gui::GuiRenderer* renderer) noexcept {
    gui::widgets::render_section_header(gui, renderer, "Main");
    s_allow_desync.render(gui, renderer);
}

// sub_18005FE20
void visit_config(config::ConfigArchive* archive) noexcept {
    s_allow_desync.visit(archive, "allow_desync");
}

events::Module make_module() noexcept {
    events::Module mod{};
    mod.name = "No Use Delay";
    mod.description = "Removes vanilla delay when consuming items.";
    mod.category = events::ModuleCategory::Utility;
    mod.enabled = false;
    mod.flags = events::kModuleFlagNone;
    mod.keybind = 0;
    mod.hold_mode = false;
    mod.events[events::kEventPostLivingUpdate] = {on_post_living_update, 0, 0};
    mod.render_settings = render_settings;
    mod.visit_config = visit_config;
    return mod;
}

events::Module s_module = make_module();

} // namespace

events::Module* get_no_use_delay_module() noexcept {
    return &s_module;
}

} // namespace slinky::modules
