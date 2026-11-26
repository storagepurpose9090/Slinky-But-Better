#pragma once

#include <cstddef>
#include <cstdint>
#include "cJSON.h"
#include "events/module.hpp"
#include "util/math.hpp"

namespace slinky::gui {
struct ClickGui;
struct GuiRenderer;
}

namespace slinky::config {

enum class ConfigAction : int32_t {
    Load            = 0, // Deserialize from cJSON object (clamped, fallback to default)
    Save            = 1, // Serialize current value into cJSON object
    CaptureDefaults = 2, // Copy initial value -> default_value (xmmword_18015AF70)
    ResetDefaults   = 3, // Copy default_value -> value (xmmword_18015AFC8)
};

// 16-byte visitor archive passed to Module::visit_config and every *Setting::visit
struct ConfigArchive {
    cJSON* json = nullptr;                         // +0x00
    ConfigAction action = ConfigAction::Load;      // +0x08
    bool failed = false;                           // +0x0C
    uint8_t pad[3]{};                              // +0x0D
};
static_assert(sizeof(ConfigArchive) == 16, "ConfigArchive must be 16 bytes");

struct SliderAnimState {
    bool dragging = false;       // +0x00
    uint8_t pad[3]{};
    float visual_t = 0.0f;       // +0x04
    double last_update_time = 0.0; // +0x08
};
static_assert(sizeof(SliderAnimState) == 16, "SliderAnimState must be 16 bytes");

struct RangeSliderAnimState {
    bool dragging_min = false;   // +0x00
    bool dragging_max = false;   // +0x01
    uint8_t pad[2]{};
    float visual_min_t = 0.0f;   // +0x04
    float visual_max_t = 0.0f;   // +0x08
    uint32_t pad2 = 0;           // +0x0C
    double last_update_time = 0.0; // +0x10
};
static_assert(sizeof(RangeSliderAnimState) == 24, "RangeSliderAnimState must be 24 bytes");

struct DropdownAnimState {
    uint64_t reserved = 0;         // +0x00
    double last_update_time = 0.0; // +0x08
    float current_height = 20.0f;  // +0x10
    bool expanded = false;         // +0x14
    uint8_t pad[3]{};
};
static_assert(sizeof(DropdownAnimState) == 24, "DropdownAnimState must be 24 bytes");

// 1. BoolSetting (32 bytes = 0x20) - matches sub_18002D630 & sub_18002E1A0
struct BoolSetting {
    ToggleAnimState anim{};          // +0x00
    const char* label = nullptr;     // +0x10
    bool value = false;              // +0x18
    bool default_value = false;      // +0x19
    uint8_t pad[6]{};

    void visit(ConfigArchive* ar, const char* key) noexcept;
    void render(gui::ClickGui* gui, gui::GuiRenderer* renderer) noexcept;
};
static_assert(sizeof(BoolSetting) == 0x20, "BoolSetting must be 32 bytes");

// 2. FloatSetting (56 bytes = 0x38) - matches sub_18002D6D0 & sub_18002E1E0
struct FloatSetting {
    SliderAnimState anim{};          // +0x00
    const char* label = nullptr;     // +0x10
    const char* format = nullptr;    // +0x18 (nullptr -> "%0.2f")
    float step = 0.0f;               // +0x20
    float display_scale = 0.0f;      // +0x24
    float min_val = 0.0f;            // +0x28
    float max_val = 1.0f;            // +0x2C
    float value = 0.0f;              // +0x30
    float default_value = 0.0f;      // +0x34

    void visit(ConfigArchive* ar, const char* key) noexcept;
    void render(gui::ClickGui* gui, gui::GuiRenderer* renderer) noexcept;
};
static_assert(sizeof(FloatSetting) == 0x38, "FloatSetting must be 56 bytes");

// 3. RangeSetting (80 bytes = 0x50) - matches sub_18002D7D0 & sub_18002E260
struct RangeSetting {
    RangeSliderAnimState anim{};          // +0x00
    const char* label = nullptr;          // +0x18
    const char* single_format = nullptr;  // +0x20 (nullptr -> "%0.2f")
    const char* range_format = nullptr;   // +0x28 (nullptr -> "%0.2f - %0.2f")
    float step = 0.0f;                    // +0x30
    float display_scale = 0.0f;           // +0x34
    float min_val = 0.0f;                 // +0x38
    float max_val = 1.0f;                 // +0x3C
    math::Range2f value{};                // +0x40
    math::Range2f default_value{};        // +0x48

    void visit(ConfigArchive* ar, const char* key) noexcept;
    void render(gui::ClickGui* gui, gui::GuiRenderer* renderer) noexcept;
};
static_assert(sizeof(RangeSetting) == 0x50, "RangeSetting must be 80 bytes");

// 4. ModeSetting (64 bytes = 0x40) - matches sub_18002D970 & sub_18002E2F0
struct ModeSetting {
    DropdownAnimState anim{};             // +0x00
    const char* label = nullptr;          // +0x18
    uint32_t option_count = 0;            // +0x20
    uint32_t pad0 = 0;                    // +0x24
    const char* const* options = nullptr; // +0x28
    int32_t value = 0;                    // +0x30
    int32_t default_value = 0;            // +0x34
    bool nullable_zero = false;           // +0x38
    uint8_t pad1[7]{};

    void visit(ConfigArchive* ar, const char* key) noexcept;
    void render(gui::ClickGui* gui, gui::GuiRenderer* renderer) noexcept;
};
static_assert(sizeof(ModeSetting) == 0x40, "ModeSetting must be 64 bytes");

// 5. ColorSetting (32 bytes = 0x20) - matches sub_18002DA60 & sub_18002E350
struct ColorSetting {
    const char* label = nullptr;          // +0x00
    math::Color3f value{};                // +0x08..+0x13
    math::Color3f default_value{};        // +0x14..+0x1F

    void visit(ConfigArchive* ar, const char* key) noexcept;
    void render(gui::ClickGui* gui, gui::GuiRenderer* renderer) noexcept;
};
static_assert(sizeof(ColorSetting) == 0x20, "ColorSetting must be 32 bytes");

// 6. DynamicListSetting (104 bytes = 0x68) - matches sub_18002DCB0 & sub_18002E390
enum class DynamicListItemKind : int32_t {
    StringOnly   = 0, // Array of JSON strings (e.g. Friends, Item ESP filter)
    ColoredEntry = 1, // Array of {"name": ..., "color": {"r","g","b"}} (e.g. Block ESP Search)
};

using ListInputValidatorFn = size_t (*)(char* buffer, char ch);
using ListGetCountFn       = size_t (*)(void* ctx);
using ListGetItemNameFn    = const char* (*)(void* ctx, size_t index);
using ListGetItemColorFn   = math::Color3f* (*)(void* ctx, size_t index);
using ListAddItemFn        = bool (*)(void* ctx, const char* name, const math::Color3f* color);
using ListRemoveItemFn     = void (*)(void* ctx, size_t index);
using ListClearFn          = void (*)(void* ctx);
using ListGetOptionsFn     = void (*)(const char* const** out_items, size_t* out_count);

struct DynamicListSetting {
    const char* label = nullptr;                  // +0x00
    const char* prompt_title = nullptr;           // +0x08
    ListInputValidatorFn input_validator = nullptr; // +0x10
    DynamicListItemKind item_kind = DynamicListItemKind::StringOnly; // +0x18
    uint32_t pad0 = 0;                            // +0x1C
    void* ctx = nullptr;                          // +0x20
    ListGetCountFn get_count = nullptr;           // +0x28
    ListGetItemNameFn get_name = nullptr;         // +0x30
    ListGetItemColorFn get_color = nullptr;       // +0x38
    ListAddItemFn add_item = nullptr;             // +0x40
    ListRemoveItemFn remove_at = nullptr;         // +0x48
    ListClearFn clear_all = nullptr;              // +0x50
    ListGetOptionsFn get_picker_options = nullptr;// +0x58
    bool modified = false;                        // +0x60
    uint8_t pad1[7]{};

    void visit(ConfigArchive* ar, const char* key) noexcept;
    void render(gui::ClickGui* gui, gui::GuiRenderer* renderer) noexcept;
};
static_assert(sizeof(DynamicListSetting) == 0x68, "DynamicListSetting must be 104 bytes");

// Parses {"r": ..., "g": ..., "b": ...} in [0, 1] or returns fallback - matches sub_18002DB90
math::Color3f parse_json_rgb(const cJSON* obj, const math::Color3f& fallback) noexcept;

// Deserializes/serializes a single Module into a root cJSON object - matches sub_18002DFD0 & sub_18002E0D0
bool deserialize_module_from_json(const cJSON* root, events::ModuleManager* mgr, events::Module* mod) noexcept;
bool serialize_module_to_json(cJSON* root, const events::Module* mod) noexcept;

} // namespace slinky::config
