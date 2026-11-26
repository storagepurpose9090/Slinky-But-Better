#include "config/settings.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include "config/profile_manager.hpp"
#include "gui/widgets.hpp"

namespace slinky::config {

math::Color3f parse_json_rgb(const cJSON* obj, const math::Color3f& fallback) noexcept {
    if (!cJSON_IsObject(obj)) {
        return fallback;
    }
    const cJSON* r_item = cJSON_GetObjectItemCaseSensitive(obj, "r");
    const cJSON* g_item = cJSON_GetObjectItemCaseSensitive(obj, "g");
    const cJSON* b_item = cJSON_GetObjectItemCaseSensitive(obj, "b");
    if (!cJSON_IsNumber(r_item) || !cJSON_IsNumber(g_item) || !cJSON_IsNumber(b_item)) {
        return fallback;
    }
    const double r = cJSON_GetNumberValue(r_item);
    const double g = cJSON_GetNumberValue(g_item);
    const double b = cJSON_GetNumberValue(b_item);
    constexpr double kMax = 1.000000059604645;
    if (r < 0.0 || r > kMax || g < 0.0 || g > kMax || b < 0.0 || b > kMax) {
        return fallback;
    }
    return math::Color3f{
        static_cast<float>(r),
        static_cast<float>(g),
        static_cast<float>(b),
    };
}

void BoolSetting::visit(ConfigArchive* ar, const char* key) noexcept {
    if (ar->failed) {
        return;
    }
    switch (ar->action) {
        case ConfigAction::Load: {
            bool result = default_value;
            const cJSON* item = cJSON_GetObjectItemCaseSensitive(ar->json, key);
            if (cJSON_IsBool(item)) {
                result = cJSON_IsTrue(item) != 0;
            }
            value = result;
            break;
        }
        case ConfigAction::Save: {
            if (cJSON_AddBoolToObject(ar->json, key, value ? 1 : 0) == nullptr) {
                ar->failed = true;
            }
            break;
        }
        case ConfigAction::CaptureDefaults:
            default_value = value;
            break;
        case ConfigAction::ResetDefaults:
            value = default_value;
            break;
    }
}

void BoolSetting::render(gui::ClickGui* gui, gui::GuiRenderer* renderer) noexcept {
    if (gui::widgets::render_bool_toggle(gui, renderer, label, &anim, &value)) {
        mark_profile_dirty(true);
    }
}

void FloatSetting::visit(ConfigArchive* ar, const char* key) noexcept {
    if (ar->failed) {
        return;
    }
    switch (ar->action) {
        case ConfigAction::Load: {
            float result = default_value;
            const cJSON* item = cJSON_GetObjectItemCaseSensitive(ar->json, key);
            if (cJSON_IsNumber(item)) {
                const auto parsed = static_cast<float>(cJSON_GetNumberValue(item));
                result = std::clamp(parsed, min_val, max_val);
            }
            value = result;
            break;
        }
        case ConfigAction::Save: {
            if (cJSON_AddNumberToObject(ar->json, key, static_cast<double>(value)) == nullptr) {
                ar->failed = true;
            }
            break;
        }
        case ConfigAction::CaptureDefaults:
            default_value = value;
            break;
        case ConfigAction::ResetDefaults:
            value = default_value;
            break;
    }
}

void FloatSetting::render(gui::ClickGui* gui, gui::GuiRenderer* renderer) noexcept {
    if (gui::widgets::render_float_slider(
            gui, renderer, label, format, &anim, min_val, max_val, step, display_scale, &value)) {
        mark_profile_dirty(true);
    }
}

void RangeSetting::visit(ConfigArchive* ar, const char* key) noexcept {
    if (ar->failed) {
        return;
    }
    switch (ar->action) {
        case ConfigAction::Load: {
            math::Range2f result = default_value;
            const cJSON* obj = cJSON_GetObjectItemCaseSensitive(ar->json, key);
            if (cJSON_IsObject(obj)) {
                const cJSON* min_item = cJSON_GetObjectItemCaseSensitive(obj, "min");
                const cJSON* max_item = cJSON_GetObjectItemCaseSensitive(obj, "max");
                const auto raw_min = static_cast<float>(cJSON_GetNumberValue(min_item));
                const auto raw_max = static_cast<float>(cJSON_GetNumberValue(max_item));
                const float ordered_min = std::fmin(raw_max, raw_min);
                result.min = std::clamp(ordered_min, min_val, max_val);
                result.max = std::clamp(raw_max, min_val, max_val);
            }
            value = result;
            break;
        }
        case ConfigAction::Save: {
            cJSON* obj = cJSON_AddObjectToObject(ar->json, key);
            if (obj != nullptr) {
                if (cJSON_AddNumberToObject(obj, "min", static_cast<double>(value.min)) == nullptr) {
                    ar->failed = true;
                }
                if (cJSON_AddNumberToObject(obj, "max", static_cast<double>(value.max)) == nullptr) {
                    ar->failed = true;
                }
            } else {
                ar->failed = true;
            }
            break;
        }
        case ConfigAction::CaptureDefaults:
            default_value = value;
            break;
        case ConfigAction::ResetDefaults:
            value = default_value;
            break;
    }
}

void RangeSetting::render(gui::ClickGui* gui, gui::GuiRenderer* renderer) noexcept {
    if (gui::widgets::render_range_slider(
            gui, renderer, label, single_format, range_format, &anim,
            min_val, max_val, step, display_scale, &value.min, &value.max)) {
        mark_profile_dirty(true);
    }
}

void ModeSetting::visit(ConfigArchive* ar, const char* key) noexcept {
    if (ar->failed) {
        return;
    }
    switch (ar->action) {
        case ConfigAction::Load: {
            int32_t result = default_value;
            const cJSON* item = cJSON_GetObjectItemCaseSensitive(ar->json, key);
            if (cJSON_IsString(item) && item->valuestring != nullptr && options != nullptr) {
                for (uint32_t i = 0; i < option_count; ++i) {
                    if (std::strcmp(options[i], item->valuestring) == 0) {
                        result = static_cast<int32_t>(i);
                        break;
                    }
                }
            }
            value = result;
            break;
        }
        case ConfigAction::Save: {
            const uint32_t idx = (static_cast<uint32_t>(value) < option_count)
                ? static_cast<uint32_t>(value)
                : 0u;
            if (options == nullptr || cJSON_AddStringToObject(ar->json, key, options[idx]) == nullptr) {
                ar->failed = true;
            }
            break;
        }
        case ConfigAction::CaptureDefaults:
            default_value = value;
            break;
        case ConfigAction::ResetDefaults:
            value = default_value;
            break;
    }
}

void ModeSetting::render(gui::ClickGui* gui, gui::GuiRenderer* renderer) noexcept {
    if (gui::widgets::render_mode_dropdown(
            gui, renderer, label, &anim, option_count, options, &value, nullable_zero)) {
        mark_profile_dirty(true);
    }
}

void ColorSetting::visit(ConfigArchive* ar, const char* key) noexcept {
    if (ar->failed) {
        return;
    }
    switch (ar->action) {
        case ConfigAction::Load: {
            const cJSON* obj = cJSON_GetObjectItemCaseSensitive(ar->json, key);
            value = parse_json_rgb(obj, default_value);
            break;
        }
        case ConfigAction::Save: {
            cJSON* obj = cJSON_AddObjectToObject(ar->json, key);
            if (obj != nullptr) {
                if (cJSON_AddNumberToObject(obj, "r", static_cast<double>(value.r)) == nullptr) {
                    ar->failed = true;
                }
                if (cJSON_AddNumberToObject(obj, "g", static_cast<double>(value.g)) == nullptr) {
                    ar->failed = true;
                }
                if (cJSON_AddNumberToObject(obj, "b", static_cast<double>(value.b)) == nullptr) {
                    ar->failed = true;
                }
            } else {
                ar->failed = true;
            }
            break;
        }
        case ConfigAction::CaptureDefaults:
            default_value = value;
            break;
        case ConfigAction::ResetDefaults:
            value = default_value;
            break;
    }
}

void ColorSetting::render(gui::ClickGui* gui, gui::GuiRenderer* renderer) noexcept {
    if (gui::widgets::render_color_button(gui, renderer, label, &value)) {
        mark_profile_dirty(true);
    }
}

void DynamicListSetting::visit(ConfigArchive* ar, const char* key) noexcept {
    if (ar->failed) {
        return;
    }
    switch (ar->action) {
        case ConfigAction::Load: {
            if (clear_all != nullptr) {
                clear_all(ctx);
            }
            const cJSON* arr = cJSON_GetObjectItemCaseSensitive(ar->json, key);
            if (!cJSON_IsArray(arr) || add_item == nullptr) {
                break;
            }
            for (const cJSON* elem = arr->child; elem != nullptr; elem = elem->next) {
                if (item_kind == DynamicListItemKind::ColoredEntry) {
                    if (cJSON_IsObject(elem)) {
                        const cJSON* name_obj = cJSON_GetObjectItemCaseSensitive(elem, "name");
                        if (cJSON_IsString(name_obj) && name_obj->valuestring != nullptr) {
                            const cJSON* color_obj = cJSON_GetObjectItemCaseSensitive(elem, "color");
                            const math::Color3f white{1.0f, 1.0f, 1.0f};
                            math::Color3f parsed = parse_json_rgb(color_obj, white);
                            add_item(ctx, name_obj->valuestring, &parsed);
                        }
                    }
                } else if (item_kind == DynamicListItemKind::StringOnly) {
                    if (cJSON_IsString(elem) && elem->valuestring != nullptr) {
                        const math::Color3f zero{};
                        add_item(ctx, elem->valuestring, &zero);
                    }
                }
            }
            break;
        }
        case ConfigAction::Save: {
            cJSON* arr = cJSON_AddArrayToObject(ar->json, key);
            if (arr == nullptr) {
                ar->failed = true;
                return;
            }
            if (get_count == nullptr || get_name == nullptr) {
                break;
            }
            const size_t total = get_count(ctx);
            for (size_t i = 0; i < total; ++i) {
                cJSON* elem = nullptr;
                bool step_failed = false;
                if (item_kind == DynamicListItemKind::ColoredEntry) {
                    elem = cJSON_CreateObject();
                    if (elem == nullptr) {
                        step_failed = true;
                    } else {
                        const char* entry_name = get_name(ctx, i);
                        if (cJSON_AddStringToObject(elem, "name", entry_name) == nullptr) {
                            step_failed = true;
                        } else {
                            cJSON* col_obj = cJSON_AddObjectToObject(elem, "color");
                            math::Color3f* col_ptr = (get_color != nullptr) ? get_color(ctx, i) : nullptr;
                            const math::Color3f col = (col_ptr != nullptr) ? *col_ptr : math::Color3f{1.0f, 1.0f, 1.0f};
                            if (col_obj == nullptr ||
                                cJSON_AddNumberToObject(col_obj, "r", static_cast<double>(col.r)) == nullptr ||
                                cJSON_AddNumberToObject(col_obj, "g", static_cast<double>(col.g)) == nullptr ||
                                cJSON_AddNumberToObject(col_obj, "b", static_cast<double>(col.b)) == nullptr) {
                                step_failed = true;
                            }
                        }
                    }
                } else {
                    const char* entry_name = get_name(ctx, i);
                    elem = cJSON_CreateString(entry_name);
                    if (elem == nullptr) {
                        step_failed = true;
                    }
                }

                if (!step_failed && elem != nullptr) {
                    if (!cJSON_AddItemToArray(arr, elem)) {
                        cJSON_Delete(elem);
                        step_failed = true;
                    }
                } else if (elem != nullptr) {
                    cJSON_Delete(elem);
                }
                if (step_failed) {
                    ar->failed = true;
                }
            }
            break;
        }
        case ConfigAction::CaptureDefaults:
            break;
        case ConfigAction::ResetDefaults:
            if (clear_all != nullptr) {
                clear_all(ctx);
            }
            break;
    }
}

void DynamicListSetting::render(gui::ClickGui* gui, gui::GuiRenderer* renderer) noexcept {
    const char* const* items = nullptr;
    size_t item_count = 0;
    if (get_picker_options != nullptr) {
        get_picker_options(&items, &item_count);
    }
    if (gui::widgets::render_dynamic_list(
            gui, renderer, label, prompt_title, input_validator, item_kind, this, items, item_count)) {
        modified = true;
        mark_profile_dirty(true);
    }
}

bool deserialize_module_from_json(
    const cJSON* root,
    events::ModuleManager* mgr,
    events::Module* mod) noexcept
{
    cJSON* mod_obj = cJSON_GetObjectItemCaseSensitive(root, mod->name);
    if ((mod->flags & events::kModuleFlagNoToggleSwitch) == 0) {
        const cJSON* on_item = cJSON_GetObjectItemCaseSensitive(mod_obj, "__on");
        const bool enable = cJSON_IsTrue(on_item) != 0;
        mgr->request_set_enabled(mod, enable);
    }
    if ((mod->flags & events::kModuleFlagNoKeybind) == 0) {
        uint32_t bind = 0;
        const cJSON* bind_item = cJSON_GetObjectItemCaseSensitive(mod_obj, "__bind");
        if (cJSON_IsNumber(bind_item)) {
            const int parsed = static_cast<int>(bind_item->valuedouble + 0.5);
            if (parsed >= 1 && parsed <= 103) {
                bind = static_cast<uint32_t>(parsed);
            }
        }
        mgr->set_module_keybind(mod, bind);
        if ((mod->flags & events::kModuleFlagNoHoldMode) != 0) {
            mod->hold_mode = false;
        } else {
            const cJSON* hold_item = cJSON_GetObjectItemCaseSensitive(mod_obj, "__hold");
            mod->hold_mode = cJSON_IsTrue(hold_item) != 0;
        }
    }
    if (mod->visit_config == nullptr) {
        return true;
    }
    ConfigArchive ar{mod_obj, ConfigAction::Load, false};
    mod->visit_config(&ar);
    return !ar.failed;
}

bool serialize_module_to_json(cJSON* root, const events::Module* mod) noexcept {
    cJSON* mod_obj = cJSON_AddObjectToObject(root, mod->name);
    if (mod_obj == nullptr) {
        return false;
    }
    if ((mod->flags & events::kModuleFlagNoToggleSwitch) == 0 && mod->enabled) {
        if (cJSON_AddBoolToObject(mod_obj, "__on", 1) == nullptr) {
            return false;
        }
    }
    if ((mod->flags & events::kModuleFlagNoKeybind) == 0) {
        if (mod->keybind != 0) {
            if (cJSON_AddNumberToObject(mod_obj, "__bind", static_cast<double>(mod->keybind)) == nullptr) {
                return false;
            }
        }
        if ((mod->flags & events::kModuleFlagNoHoldMode) == 0 && mod->hold_mode) {
            if (cJSON_AddBoolToObject(mod_obj, "__hold", 1) == nullptr) {
                return false;
            }
        }
    }
    if (mod->visit_config == nullptr) {
        return true;
    }
    ConfigArchive ar{mod_obj, ConfigAction::Save, false};
    mod->visit_config(&ar);
    return !ar.failed;
}

} // namespace slinky::config
