#pragma once

#include <cstddef>
#include <cstdint>
#include "events/events.hpp"

namespace slinky {

namespace config {
struct ConfigArchive;
struct ToggleAnimState {
    float progress = 0.0f;
    uint32_t pad = 0;
    double last_update_time = 0.0;
};
static_assert(sizeof(ToggleAnimState) == 16, "ToggleAnimState must be 16 bytes");
}

namespace events {

enum class ModuleCategory : uint32_t {
    Combat  = 0,
    Move    = 1,
    Visual  = 2,
    Block   = 3,
    Utility = 4,
    Count   = 5
};

enum ModuleFlags : uint32_t {
    kModuleFlagNone             = 0,
    kModuleFlagNoKeybind        = 1u << 0, // 0x01: No keybind button or __bind serialization
    kModuleFlagNoToggleSwitch   = 1u << 1, // 0x02: No enable/disable toggle switch or __on serialization
    kModuleFlagHiddenFromHud    = 1u << 2, // 0x04: Excluded from Mod Overlay active modules list
    kModuleFlagNoHoldMode       = 1u << 3, // 0x08: Keybind is instant/toggle-only (no HOLD pill)
    kModuleFlagGlobalSettings   = 1u << 4, // 0x10: Saved in .slinky/settings instead of per-profile
};

struct EventSubscription {
    EventCallbackFn callback = nullptr; // +0x00
    int32_t priority = 0;               // +0x08 (higher runs earlier; qsort descending in sub_1800497A0)
    int32_t pad = 0;                    // +0x0C
};
static_assert(sizeof(EventSubscription) == 16, "EventSubscription must be 16 bytes");

using RenderModuleSettingsFn = void (*)(gui::ClickGui* gui, gui::GuiRenderer* renderer);
using VisitModuleConfigFn    = void (*)(config::ConfigArchive* archive);
using FormatModuleHudTagFn   = int (*)(char* out_buf_32);

// Uniform 0x220-byte (544-byte) Module base header shared by all 67 modules (off_18016BC90)
struct Module {
    const char* name = nullptr;                        // +0x000 (0)
    const char* description = nullptr;                 // +0x008 (8)
    ModuleCategory category = ModuleCategory::Combat;  // +0x010 (16)
    bool enabled = false;                              // +0x014 (20)
    uint8_t pad_enabled[3]{};                          // +0x015
    uint32_t flags = kModuleFlagNone;                  // +0x018 (24)
    int32_t keybind = 0;                               // +0x01C (28)
    bool hold_mode = false;                            // +0x020 (32)
    bool hold_active = false;                          // +0x021 (33)
    uint8_t pad_hold[6]{};                             // +0x022
    config::ToggleAnimState toggle_anim{};             // +0x028 (40..55)
    float card_height_anim = 0.0f;                     // +0x038 (56)
    uint32_t pad_card = 0;                             // +0x03C (60)
    bool settings_expanded = false;                    // +0x040 (64)
    uint8_t pad_expanded[7]{};                         // +0x041
    EventSubscription events[kEventCount]{};           // +0x048 (72..503, 27 * 16 = 432 bytes)
    RenderModuleSettingsFn render_settings = nullptr;  // +0x1F8 (504)
    VisitModuleConfigFn visit_config = nullptr;        // +0x200 (512)
    FormatModuleHudTagFn format_hud_tag = nullptr;     // +0x208 (520)
    uint64_t reserved_vfunc0 = 0;                      // +0x210 (528)
    uint64_t reserved_vfunc1 = 0;                      // +0x218 (536)
};
static_assert(sizeof(Module) == 0x220, "Module header must match exact 0x220-byte layout");

} // namespace events
} // namespace slinky
