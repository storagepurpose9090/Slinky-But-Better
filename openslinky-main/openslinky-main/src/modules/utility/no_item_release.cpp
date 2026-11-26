#include "modules/utility/no_item_release.hpp"

#include <cstdio>

#include "config/settings.hpp"
#include "gui/widgets.hpp"
#include "minecraft/mc_wrappers.hpp"

namespace slinky::modules {

namespace {

constexpr int32_t kItemBow = 261;
constexpr int32_t kItemFishingRod = 346;
constexpr int32_t kDiggingReleaseUseItem = 5;
constexpr int32_t kNettyActionDrop = 2;

// Bitmask over (item_id - 261) matching swords: 267 (Iron), 268 (Wood), 272 (Stone), 276 (Diamond), 283 (Gold)
constexpr uint32_t kSwordItemOffsetMask = 0x4088C0u;

constexpr const char* kModeOptions[3] = {
    "Sword",
    "Consumable",
    "All",
};

config::ModeSetting s_mode{
    {},
    "Mode",
    3,
    0,
    kModeOptions,
    0,
    0,
    false,
    {},
};

bool s_suppress_next_release = false;

bool is_sword_item_id(int32_t item_id) noexcept {
    const uint32_t offset = static_cast<uint32_t>(item_id - kItemBow);
    return offset <= 22u && ((kSwordItemOffsetMask >> offset) & 1u) != 0u;
}

// sub_180054C20
void on_outgoing_packet(void* raw_event) noexcept {
    auto* event = static_cast<events::NettyPacketEvent*>(raw_event);
    events::JniContext* jni = event->jni;

    jobject placement = mc::as_packet_c08_block_placement(jni, event->packet);
    if (placement != nullptr) {
        jobject stack = mc::packet_c08_get_stack(jni, placement);
        if (stack == nullptr) {
            s_suppress_next_release = false;
            return;
        }
        jobject item = mc::item_stack_get_item(jni, stack);
        if (item == nullptr) {
            s_suppress_next_release = false;
            return;
        }

        const int32_t item_id = mc::item_get_id(jni, item);
        if (is_sword_item_id(item_id)) {
            if (s_mode.value == 1) {
                s_suppress_next_release = false;
            } else {
                const int32_t check_id = mc::item_get_id(jni, item);
                s_suppress_next_release = (check_id != kItemFishingRod && check_id != kItemBow);
            }
            return;
        }

        if (item_id == kItemBow || item_id == kItemFishingRod) {
            s_suppress_next_release = false;
            return;
        }

        if (s_mode.value != 0) {
            const int32_t check_id = mc::item_get_id(jni, item);
            s_suppress_next_release = (check_id != kItemFishingRod && check_id != kItemBow);
        } else {
            s_suppress_next_release = false;
        }
        return;
    }

    jobject digging = mc::as_packet_c07_digging(jni, event->packet);
    if (digging != nullptr && s_suppress_next_release) {
        if (mc::packet_c07_get_status(jni, digging) == kDiggingReleaseUseItem) {
            event->action = kNettyActionDrop;
        }
        s_suppress_next_release = false;
    }
}

// sub_180054D20
void render_settings(gui::ClickGui* gui, gui::GuiRenderer* renderer) noexcept {
    gui::widgets::render_section_header(gui, renderer, "Main");
    s_mode.render(gui, renderer);
}

// sub_180054D60
void visit_config(config::ConfigArchive* archive) noexcept {
    s_mode.visit(archive, "mode");
}

// sub_180054D80
int format_hud_tag(char* out_buf_32) noexcept {
    return std::snprintf(out_buf_32, 32, "%s", kModeOptions[s_mode.value]);
}

events::Module make_module() noexcept {
    events::Module mod{};
    mod.name = "No Item Release";
    mod.description = "Hides item release from server.";
    mod.category = events::ModuleCategory::Utility;
    mod.enabled = false;
    mod.flags = events::kModuleFlagNone;
    mod.keybind = 0;
    mod.hold_mode = false;
    mod.events[events::kEventOutgoingPacket] = {on_outgoing_packet, 0, 0};
    mod.render_settings = render_settings;
    mod.visit_config = visit_config;
    mod.format_hud_tag = format_hud_tag;
    return mod;
}

events::Module s_module = make_module();

} // namespace

events::Module* get_no_item_release_module() noexcept {
    return &s_module;
}

} // namespace slinky::modules
