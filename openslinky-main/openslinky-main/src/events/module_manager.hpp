#pragma once

#include <cstddef>
#include <cstdint>
#include "events/module.hpp"

namespace slinky::events {

struct ModuleSpan {
    Module** data = nullptr;
    size_t count = 0;
};

using SetModuleKeybindFn = void (*)(Module* mod, uint32_t key_code);
using OnModuleDisabledCleanupFn = void (*)(Module* mod);

// Priority-sorted 27-event bus and 67-module manager (784 bytes = 0x310 @ 0x1803140E8)
// Matches sub_180049480 .. sub_180049EF0
class ModuleManager {
public:
    Module** all_modules = nullptr;                                         // +0x000 (0)
    size_t module_count = 0;                                                // +0x008 (8)
    ModuleSpan categories[static_cast<size_t>(ModuleCategory::Count)]{};    // +0x010..+0x05F (16..95)
    ModuleSpan subscribers[kEventCount]{};                                  // +0x060..+0x20F (96..527)
    EventCallbackFn* active_callbacks[kEventCount]{};                       // +0x210..+0x2E7 (528..743)
    Module** pending_enable = nullptr;                                      // +0x2E8 (744)
    size_t pending_enable_count = 0;                                        // +0x2F0 (752)
    Module** pending_disable = nullptr;                                     // +0x2F8 (760)
    size_t pending_disable_count = 0;                                       // +0x300 (768)
    SetModuleKeybindFn set_keybind_fn = nullptr;                            // +0x308 (776)

    // Initializes arrays from the 67-module static registry - matches sub_180049480
    int initialize(Module* const* registry, size_t count, SetModuleKeybindFn keybind_cb) noexcept;

    // Frees all dynamically allocated arrays - matches sub_1800497C0
    void shutdown() noexcept;

    // O(1) null-terminated active callback dispatch - matches sub_180049900
    void dispatch(void* event) const noexcept {
        const uint32_t event_id = *static_cast<const uint32_t*>(event);
        EventCallbackFn* list = active_callbacks[event_id];
        if (list == nullptr) {
            return;
        }
        for (EventCallbackFn fn = *list; fn != nullptr; fn = *++list) {
            fn(event);
        }
    }

    // Returns true if zero active subscribers exist for `event_id` - matches sub_180049950
    bool has_no_subscribers(uint32_t event_id) const noexcept {
        EventCallbackFn* list = active_callbacks[event_id];
        return list == nullptr || *list == nullptr;
    }

    // Queues a module enable/disable transition (fires PreModuleDisable if disabling) - matches sub_180049970
    bool request_set_enabled(Module* mod, bool enable) noexcept;

    // Returns true if any enable/disable transitions are queued - matches sub_180049A40
    bool has_pending_toggles() const noexcept {
        return pending_disable_count != 0 || pending_enable_count != 0;
    }

    // Applies queued enable/disable transitions, rebuilds active_callbacks, fires kEventModuleStateChanged - matches sub_180049A60
    void flush_pending_toggles(GameContext* ctx, OnModuleDisabledCleanupFn on_disabled_cleanup = nullptr) noexcept;

    // Updates module keybind via registered callback - matches sub_180049EE0
    void set_module_keybind(Module* mod, uint32_t key_code) noexcept {
        if (set_keybind_fn != nullptr) {
            set_keybind_fn(mod, key_code);
        } else {
            mod->keybind = static_cast<int32_t>(key_code);
        }
    }

    // Invokes kEventClientShutdown on every module that defines slot 26 and queues disable - matches sub_180029160
    void dispatch_shutdown_to_all(GameContext* ctx) noexcept;

private:
    void rebuild_event_callbacks(uint64_t dirty_event_mask) noexcept;
};
static_assert(sizeof(ModuleManager) == 0x310, "ModuleManager must match exact 0x310-byte layout");

} // namespace slinky::events
