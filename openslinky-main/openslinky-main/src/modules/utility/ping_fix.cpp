#include "modules/utility/ping_fix.hpp"

#include "minecraft/mc_wrappers.hpp"

namespace slinky::modules {

namespace {

// Netty action 3 = bypass artificial delay queue and forward immediately
constexpr int32_t kNettyActionBypassQueue = 3;

// sub_18005FAD0
void on_incoming_packet(void* raw_event) noexcept {
    auto* event = static_cast<events::NettyPacketEvent*>(raw_event);
    if (mc::as_packet_s00_keep_alive(event->jni, event->packet) != nullptr) {
        event->action = kNettyActionBypassQueue;
    }
}

// sub_18005FB00
void on_outgoing_packet(void* raw_event) noexcept {
    auto* event = static_cast<events::NettyPacketEvent*>(raw_event);
    if (mc::as_packet_c00_keep_alive(event->jni, event->packet) != nullptr) {
        event->action = kNettyActionBypassQueue;
    }
}

events::Module make_module() noexcept {
    events::Module mod{};
    mod.name = "Ping Fix";
    mod.description = "Shows your real ping while using lag mods.";
    mod.category = events::ModuleCategory::Utility;
    mod.enabled = false;
    mod.flags = events::kModuleFlagNone;
    mod.keybind = 0;
    mod.hold_mode = false;
    mod.events[events::kEventIncomingPacket] = {on_incoming_packet, -1000, 0};
    mod.events[events::kEventOutgoingPacket] = {on_outgoing_packet, -1000, 0};
    return mod;
}

events::Module s_module = make_module();

} // namespace

events::Module* get_ping_fix_module() noexcept {
    return &s_module;
}

} // namespace slinky::modules
