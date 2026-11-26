#include "config/profile_manager.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "cJSON.h"
#include "config/settings.hpp"
#include "util/containers.hpp"
#include "util/filesystem.hpp"

namespace slinky::config {

namespace {

char* g_active_profile_name = nullptr;
bool g_profile_dirty = false;

int compare_bucket_keycode(const void* lhs, const void* rhs) noexcept {
    const auto* a = static_cast<const KeybindBucket*>(lhs);
    const auto* b = static_cast<const KeybindBucket*>(rhs);
    if (a->key_code < b->key_code) return -1;
    if (a->key_code > b->key_code) return 1;
    return 0;
}

struct SavedProfileBind {
    char* name = nullptr;
    uint32_t keybind = 0;
    uint32_t pad = 0;
};
static_assert(sizeof(SavedProfileBind) == 16, "SavedProfileBind must be 16 bytes");

struct SavedBindList {
    size_t count = 0;
    size_t capacity = 0;
    SavedProfileBind* items = nullptr;
};

struct ProfileEnumContext {
    ProfileRegistry* registry = nullptr;
    SavedBindList* saved_binds = nullptr;
};

int compare_cstr_ptr(const void* lhs, const void* rhs) noexcept {
    const char* a = *static_cast<const char* const*>(lhs);
    const char* b = *static_cast<const char* const*>(rhs);
    return std::strcmp(a, b);
}

int compare_profile_entries(const void* lhs, const void* rhs) noexcept {
    const auto* a = static_cast<const ProfileEntry*>(lhs);
    const auto* b = static_cast<const ProfileEntry*>(rhs);
    if (a->age_seconds < b->age_seconds) return -1;
    if (a->age_seconds > b->age_seconds) return 1;
    return std::strcmp(a->name, b->name);
}

int on_enumerate_profile_file(const fs::DirectoryEntry* info, void* user_data) noexcept {
    auto* ctx = static_cast<ProfileEnumContext*>(user_data);
    const size_t name_len = std::strlen(info->name);
    auto* name_copy = static_cast<char*>(util::zero_alloc(name_len + 1, 1));
    if (name_copy == nullptr) {
        return 1005;
    }
    std::memcpy(name_copy, info->name, name_len);

    ProfileEntry entry{};
    entry.name = name_copy;
    entry.display_name_end = &name_copy[name_len < 15 ? name_len : 15];
    entry.age_seconds = info->age_seconds;

    if (ctx->saved_binds->items != nullptr && ctx->saved_binds->count > 0) {
        SavedProfileBind key{name_copy, 0, 0};
        auto* found = static_cast<const SavedProfileBind*>(std::bsearch(
            &key,
            ctx->saved_binds->items,
            ctx->saved_binds->count,
            sizeof(SavedProfileBind),
            compare_cstr_ptr));
        if (found != nullptr) {
            entry.keybind = found->keybind;
        }
    }

    if (info->age_seconds == -1) {
        std::snprintf(entry.modified_label, sizeof(entry.modified_label), "Unknown modification date.");
    } else if (info->age_seconds / 86400 == 1) {
        std::snprintf(entry.modified_label, sizeof(entry.modified_label), "Modified 1 day ago.");
    } else if (info->age_seconds / 86400 != 0) {
        std::snprintf(
            entry.modified_label,
            sizeof(entry.modified_label),
            "Modified %lld days ago.",
            static_cast<long long>(info->age_seconds / 86400));
    } else {
        std::snprintf(entry.modified_label, sizeof(entry.modified_label), "Modified today.");
    }

    ProfileRegistry* reg = ctx->registry;
    if (!util::ensure_array_capacity(
            reinterpret_cast<void**>(&reg->entries),
            sizeof(ProfileEntry),
            reg->count,
            &reg->capacity)) {
        std::free(name_copy);
        return 1005;
    }
    reg->entries[reg->count++] = entry;
    return 0;
}

bool deserialize_global_settings_json(
    const char* json_str,
    events::ModuleManager* mgr,
    ProfileRegistry* profiles) noexcept
{
    cJSON* root = cJSON_Parse(json_str);
    if (root == nullptr) {
        return false;
    }

    bool ok = true;
    for (size_t i = 0; i < mgr->module_count; ++i) {
        events::Module* mod = mgr->all_modules[i];
        if ((mod->flags & events::kModuleFlagGlobalSettings) != 0) {
            if (!deserialize_module_from_json(root, mgr, mod)) {
                ok = false;
                break;
            }
        }
    }

    if (ok && profiles != nullptr) {
        const cJSON* binds = cJSON_GetObjectItemCaseSensitive(root, "profile_binds");
        if (cJSON_IsArray(binds)) {
            for (const cJSON* item = binds->child; item != nullptr; item = item->next) {
                const cJSON* name_obj = cJSON_GetObjectItemCaseSensitive(item, "name");
                const char* name_str = cJSON_GetStringValue(name_obj);
                if (name_str == nullptr) {
                    continue;
                }
                ProfileEntry* entry = profiles->find_by_name(name_str);
                if (entry == nullptr) {
                    continue;
                }
                uint32_t bind = 0;
                const cJSON* bind_obj = cJSON_GetObjectItemCaseSensitive(item, "bind");
                if (cJSON_IsNumber(bind_obj)) {
                    const int parsed = static_cast<int>(bind_obj->valuedouble + 0.5);
                    if (parsed >= 1 && parsed <= 103) {
                        bind = static_cast<uint32_t>(parsed);
                    }
                }
                if (profiles->set_bind_fn != nullptr) {
                    profiles->set_bind_fn(entry, bind);
                } else {
                    entry->keybind = bind;
                }
            }
        }
    }

    cJSON_Delete(root);
    return ok;
}

char* serialize_global_settings_json(events::ModuleManager* mgr, ProfileRegistry* profiles) noexcept {
    cJSON* root = cJSON_CreateObject();
    if (root == nullptr) {
        return nullptr;
    }

    char* printed = nullptr;
    for (size_t i = 0; i < mgr->module_count; ++i) {
        const events::Module* mod = mgr->all_modules[i];
        if ((mod->flags & events::kModuleFlagGlobalSettings) != 0) {
            if (!serialize_module_to_json(root, mod)) {
                cJSON_Delete(root);
                return nullptr;
            }
        }
    }

    cJSON* binds_arr = cJSON_AddArrayToObject(root, "profile_binds");
    if (binds_arr != nullptr && profiles != nullptr) {
        size_t prof_count = 0;
        const ProfileEntry* entries = profiles->get_entries(&prof_count);
        bool ok = true;
        for (size_t i = 0; i < prof_count; ++i) {
            if (entries[i].keybind != 0) {
                cJSON* item = cJSON_CreateObject();
                if (item == nullptr) {
                    ok = false;
                    break;
                }
                if (cJSON_AddStringToObject(item, "name", entries[i].name) == nullptr ||
                    cJSON_AddNumberToObject(item, "bind", static_cast<double>(entries[i].keybind)) == nullptr ||
                    !cJSON_AddItemToArray(binds_arr, item)) {
                    cJSON_Delete(item);
                    ok = false;
                    break;
                }
            }
        }
        if (ok) {
            printed = cJSON_PrintUnformatted(root);
        }
    }

    cJSON_Delete(root);
    return printed;
}

bool deserialize_profile_json(const char* json_str, events::ModuleManager* mgr) noexcept {
    cJSON* root = cJSON_Parse(json_str);
    if (root == nullptr) {
        return false;
    }
    bool ok = true;
    for (size_t i = 0; i < mgr->module_count; ++i) {
        events::Module* mod = mgr->all_modules[i];
        if ((mod->flags & events::kModuleFlagGlobalSettings) == 0) {
            if (!deserialize_module_from_json(root, mgr, mod)) {
                ok = false;
                break;
            }
        }
    }
    cJSON_Delete(root);
    return ok;
}

char* serialize_profile_json(events::ModuleManager* mgr) noexcept {
    cJSON* root = cJSON_CreateObject();
    if (root == nullptr) {
        return nullptr;
    }
    for (size_t i = 0; i < mgr->module_count; ++i) {
        const events::Module* mod = mgr->all_modules[i];
        if ((mod->flags & events::kModuleFlagGlobalSettings) == 0) {
            if (!serialize_module_to_json(root, mod)) {
                cJSON_Delete(root);
                return nullptr;
            }
        }
    }
    char* printed = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return printed;
}

} // namespace

// ============================================================================
// KeybindMap
// ============================================================================

KeybindMap* KeybindMap::create(KeybindTriggerFn trigger_cb, KeybindEvictFn evict_cb, bool allow_multi) noexcept {
    auto* map = static_cast<KeybindMap*>(util::zero_alloc(1, sizeof(KeybindMap)));
    if (map != nullptr) {
        map->on_trigger = trigger_cb;
        map->on_evict = evict_cb;
        map->allow_multiple = allow_multi;
    }
    return map;
}

void KeybindMap::destroy(KeybindMap* map) noexcept {
    if (map == nullptr) {
        return;
    }
    for (size_t i = 0; i < map->count; ++i) {
        KeybindBucket& bucket = map->buckets[i];
        if (map->on_evict != nullptr) {
            for (size_t j = 0; j < bucket.count; ++j) {
                map->on_evict(bucket.targets[j]);
            }
        }
        std::free(bucket.targets);
        bucket.targets = nullptr;
        bucket.count = 0;
        bucket.capacity = 0;
    }
    std::free(map->buckets);
    std::free(map);
}

bool KeybindMap::bind(int32_t key_code, void* target) noexcept {
    if (key_code == 0) {
        return true;
    }
    KeybindBucket key{};
    key.key_code = key_code;
    KeybindBucket* bucket = nullptr;
    if (buckets != nullptr && count > 0) {
        bucket = static_cast<KeybindBucket*>(
            std::bsearch(&key, buckets, count, sizeof(KeybindBucket), compare_bucket_keycode));
    }
    if (bucket == nullptr) {
        size_t insert_idx = 0;
        while (insert_idx < count && buckets[insert_idx].key_code < key_code) {
            ++insert_idx;
        }
        if (!util::ensure_array_capacity(
                reinterpret_cast<void**>(&buckets), sizeof(KeybindBucket), count, &capacity)) {
            return false;
        }
        std::memmove(
            &buckets[insert_idx + 1],
            &buckets[insert_idx],
            (count - insert_idx) * sizeof(KeybindBucket));
        buckets[insert_idx] = KeybindBucket{};
        buckets[insert_idx].key_code = key_code;
        ++count;
        bucket = &buckets[insert_idx];
    }

    if (!allow_multiple) {
        if (bucket->count > 0) {
            if (on_evict != nullptr) {
                on_evict(bucket->targets[0]);
            }
            std::free(bucket->targets);
            bucket->targets = nullptr;
            bucket->count = 0;
            bucket->capacity = 0;
        }
    } else {
        for (size_t i = 0; i < bucket->count; ++i) {
            if (bucket->targets[i] == target) {
                return true;
            }
        }
    }

    if (!util::ensure_array_capacity(
            reinterpret_cast<void**>(&bucket->targets),
            sizeof(void*),
            bucket->count,
            &bucket->capacity)) {
        return false;
    }
    bucket->targets[bucket->count++] = target;
    return true;
}

void KeybindMap::unbind(int32_t key_code, void* target) noexcept {
    if (key_code == 0 || buckets == nullptr || count == 0) {
        return;
    }
    KeybindBucket key{};
    key.key_code = key_code;
    auto* bucket = static_cast<KeybindBucket*>(
        std::bsearch(&key, buckets, count, sizeof(KeybindBucket), compare_bucket_keycode));
    if (bucket == nullptr || bucket->count == 0) {
        return;
    }

    if (!allow_multiple) {
        if (on_evict != nullptr) {
            on_evict(bucket->targets[0]);
        }
    } else {
        size_t idx = 0;
        while (idx < bucket->count && bucket->targets[idx] != target) {
            ++idx;
        }
        if (idx == bucket->count) {
            return;
        }
        if (on_evict != nullptr) {
            on_evict(bucket->targets[idx]);
        }
        --bucket->count;
        std::memmove(
            &bucket->targets[idx],
            &bucket->targets[idx + 1],
            (bucket->count - idx) * sizeof(void*));
        if (bucket->count > 0) {
            return;
        }
    }

    std::free(bucket->targets);
    bucket->targets = nullptr;
    bucket->count = 0;
    bucket->capacity = 0;
    const size_t bucket_idx = static_cast<size_t>(bucket - buckets);
    --count;
    std::memmove(&buckets[bucket_idx], &buckets[bucket_idx + 1], (count - bucket_idx) * sizeof(KeybindBucket));
}

void* KeybindMap::find(int32_t key_code, void* target) const noexcept {
    if (key_code == 0 || buckets == nullptr || count == 0) {
        return nullptr;
    }
    KeybindBucket key{};
    key.key_code = key_code;
    const auto* bucket = static_cast<const KeybindBucket*>(
        std::bsearch(&key, buckets, count, sizeof(KeybindBucket), compare_bucket_keycode));
    if (bucket == nullptr || bucket->count == 0) {
        return nullptr;
    }
    if (!allow_multiple) {
        return bucket->targets[0];
    }
    for (size_t i = 0; i < bucket->count; ++i) {
        if (bucket->targets[i] == target) {
            return bucket->targets[i];
        }
    }
    return nullptr;
}

bool KeybindMap::dispatch(uint32_t key_code, bool pressed) const noexcept {
    if (buckets == nullptr || count == 0 || on_trigger == nullptr) {
        return false;
    }
    KeybindBucket key{};
    key.key_code = static_cast<int32_t>(key_code);
    const auto* bucket = static_cast<const KeybindBucket*>(
        std::bsearch(&key, buckets, count, sizeof(KeybindBucket), compare_bucket_keycode));
    if (bucket == nullptr || bucket->count == 0) {
        return false;
    }
    bool handled = false;
    for (size_t i = 0; i < bucket->count; ++i) {
        if (on_trigger(bucket->targets[i], key_code, pressed)) {
            handled = true;
        }
    }
    return handled;
}

void KeybindMap::release_all() const noexcept {
    if (on_trigger == nullptr) {
        return;
    }
    for (size_t i = 0; i < count; ++i) {
        const KeybindBucket& bucket = buckets[i];
        for (size_t j = 0; j < bucket.count; ++j) {
            on_trigger(bucket.targets[j], static_cast<uint32_t>(bucket.key_code), false);
        }
    }
}

// ============================================================================
// ProfileRegistry
// ============================================================================

ProfileRegistry* ProfileRegistry::create(SetProfileKeybindFn bind_cb) noexcept {
    auto* reg = static_cast<ProfileRegistry*>(util::zero_alloc(1, sizeof(ProfileRegistry)));
    if (reg != nullptr) {
        reg->set_bind_fn = bind_cb;
    }
    return reg;
}

void ProfileRegistry::destroy(ProfileRegistry* reg) noexcept {
    if (reg == nullptr) {
        return;
    }
    for (size_t i = 0; i < reg->count; ++i) {
        std::free(reg->entries[i].name);
    }
    std::free(reg->entries);
    std::free(reg);
}

int ProfileRegistry::refresh_from_disk() noexcept {
    SavedBindList saved{};
    for (size_t i = 0; i < count; ++i) {
        if (entries[i].keybind != 0) {
            const size_t len = std::strlen(entries[i].name);
            auto* dup = static_cast<char*>(util::zero_alloc(len + 1, 1));
            if (dup == nullptr) {
                for (size_t k = 0; k < saved.count; ++k) {
                    std::free(saved.items[k].name);
                }
                std::free(saved.items);
                return 1005;
            }
            std::memcpy(dup, entries[i].name, len);
            if (util::ensure_array_capacity(
                    reinterpret_cast<void**>(&saved.items),
                    sizeof(SavedProfileBind),
                    saved.count,
                    &saved.capacity)) {
                saved.items[saved.count++] = SavedProfileBind{dup, entries[i].keybind, 0};
            } else {
                std::free(dup);
            }
        }
    }

    if (saved.items != nullptr && saved.count > 1) {
        std::qsort(saved.items, saved.count, sizeof(SavedProfileBind), compare_cstr_ptr);
    }

    for (size_t i = 0; i < count; ++i) {
        std::free(entries[i].name);
    }
    std::free(entries);
    entries = nullptr;
    count = 0;
    capacity = 0;

    fs::WidePath path{};
    int err = fs::init_user_profile_path(&path);
    if (err == 0) {
        err = fs::append_path(&path, ".slinky");
        if (err == 0) err = fs::create_directory(&path);
        if (err == 0) err = fs::append_path(&path, "profiles");
        if (err == 0) err = fs::create_directory(&path);
        if (err == 0) {
            ProfileEnumContext enum_ctx{this, &saved};
            err = fs::list_files(&path, &enum_ctx, on_enumerate_profile_file);
        }
        fs::free_path(&path);
    }

    if (entries != nullptr && count > 1) {
        std::qsort(entries, count, sizeof(ProfileEntry), compare_profile_entries);
    }

    for (size_t k = 0; k < saved.count; ++k) {
        std::free(saved.items[k].name);
    }
    std::free(saved.items);
    return err;
}

const ProfileEntry* ProfileRegistry::get_entries(size_t* out_count) const noexcept {
    *out_count = count;
    return count > 0 ? entries : nullptr;
}

ProfileEntry* ProfileRegistry::find_by_name(const char* profile_name) noexcept {
    if (profile_name == nullptr || entries == nullptr) {
        return nullptr;
    }
    for (size_t i = 0; i < count; ++i) {
        if (std::strcmp(profile_name, entries[i].name) == 0) {
            return &entries[i];
        }
    }
    return nullptr;
}

// ============================================================================
// Global & Profile Persistence
// ============================================================================

void capture_all_module_defaults(events::ModuleManager* mgr) noexcept {
    for (size_t i = 0; i < mgr->module_count; ++i) {
        events::Module* mod = mgr->all_modules[i];
        if (mod->visit_config != nullptr) {
            ConfigArchive ar{nullptr, ConfigAction::CaptureDefaults, false};
            mod->visit_config(&ar);
        }
    }
}

int load_global_settings(events::ModuleManager* mgr, ProfileRegistry* profiles) noexcept {
    fs::WidePath path{};
    int err = fs::init_user_profile_path(&path);
    if (err != 0) {
        return err;
    }
    char* text = nullptr;
    err = fs::append_path(&path, ".slinky");
    if (err == 0) err = fs::create_directory(&path);
    if (err == 0) err = fs::append_path(&path, "settings");
    if (err == 0) err = fs::read_text_file(&path, &text);
    if (err == 0) {
        err = deserialize_global_settings_json(text, mgr, profiles) ? 0 : 5001;
        std::free(text);
    }
    fs::free_path(&path);
    return err;
}

int save_global_settings(events::ModuleManager* mgr, ProfileRegistry* profiles) noexcept {
    fs::WidePath path{};
    int err = fs::init_user_profile_path(&path);
    if (err != 0) {
        return err;
    }
    err = fs::append_path(&path, ".slinky");
    if (err == 0) err = fs::create_directory(&path);
    if (err == 0) err = fs::append_path(&path, "settings");
    if (err == 0) {
        char* json = serialize_global_settings_json(mgr, profiles);
        if (json != nullptr) {
            err = fs::write_text_file(&path, json, true);
            cJSON_free(json);
        } else {
            err = 5000;
        }
    }
    fs::free_path(&path);
    return err;
}

int load_or_reset_profile(const char* profile_name, events::ModuleManager* mgr) noexcept {
    if (profile_name != nullptr) {
        fs::WidePath path{};
        int err = fs::init_user_profile_path(&path);
        if (err != 0) {
            return err;
        }
        char* text = nullptr;
        err = fs::append_path(&path, ".slinky");
        if (err == 0) err = fs::create_directory(&path);
        if (err == 0) err = fs::append_path(&path, "profiles");
        if (err == 0) err = fs::create_directory(&path);
        if (err == 0) err = fs::append_path(&path, profile_name);
        if (err == 0) err = fs::read_text_file(&path, &text);
        if (err == 0) {
            if (deserialize_profile_json(text, mgr)) {
                set_active_profile_name(profile_name);
                g_profile_dirty = false;
                err = 0;
            } else {
                err = 5001;
            }
            std::free(text);
        }
        fs::free_path(&path);
        return err;
    }

    for (size_t i = 0; i < mgr->module_count; ++i) {
        events::Module* mod = mgr->all_modules[i];
        if ((mod->flags & events::kModuleFlagGlobalSettings) == 0) {
            mgr->set_module_keybind(mod, 0);
            mod->hold_mode = false;
            if ((mod->flags & events::kModuleFlagNoToggleSwitch) == 0) {
                mgr->request_set_enabled(mod, false);
            }
            if (mod->visit_config != nullptr) {
                ConfigArchive ar{nullptr, ConfigAction::ResetDefaults, false};
                mod->visit_config(&ar);
            }
        }
    }
    free_active_profile_name();
    g_profile_dirty = false;
    return 0;
}

int save_profile(const char* profile_name, bool allow_overwrite, events::ModuleManager* mgr) noexcept {
    fs::WidePath path{};
    int err = fs::init_user_profile_path(&path);
    if (err != 0) {
        return err;
    }
    err = fs::append_path(&path, ".slinky");
    if (err == 0) err = fs::create_directory(&path);
    if (err == 0) err = fs::append_path(&path, "profiles");
    if (err == 0) err = fs::append_path(&path, profile_name);
    if (err == 0) {
        char* json = serialize_profile_json(mgr);
        if (json != nullptr) {
            err = fs::write_text_file(&path, json, allow_overwrite);
            if (err == 0) {
                set_active_profile_name(profile_name);
                g_profile_dirty = false;
            }
            cJSON_free(json);
        } else {
            err = 5000;
        }
    }
    fs::free_path(&path);
    return err;
}

int delete_profile_file(const char* profile_name) noexcept {
    fs::WidePath path{};
    int err = fs::init_user_profile_path(&path);
    if (err != 0) {
        return err;
    }
    err = fs::append_path(&path, ".slinky");
    if (err == 0) err = fs::append_path(&path, "profiles");
    if (err == 0) err = fs::append_path(&path, profile_name);
    if (err == 0) err = fs::delete_file(&path);
    fs::free_path(&path);
    return err;
}

int open_profiles_directory() noexcept {
    fs::WidePath path{};
    int err = fs::init_user_profile_path(&path);
    if (err != 0) {
        return err;
    }
    err = fs::append_path(&path, ".slinky");
    if (err == 0) err = fs::append_path(&path, "profiles");
    if (err == 0) err = fs::open_in_explorer(&path);
    fs::free_path(&path);
    return err;
}

void set_active_profile_name(const char* name) noexcept {
    std::free(g_active_profile_name);
    if (name != nullptr) {
        const size_t len = std::strlen(name);
        g_active_profile_name = static_cast<char*>(std::malloc(len + 1));
        if (g_active_profile_name != nullptr) {
            std::memcpy(g_active_profile_name, name, len);
            g_active_profile_name[len] = '\0';
        }
    } else {
        g_active_profile_name = nullptr;
    }
}

const char* get_active_profile_name() noexcept {
    return g_active_profile_name;
}

void free_active_profile_name() noexcept {
    std::free(g_active_profile_name);
    g_active_profile_name = nullptr;
}

bool is_profile_dirty() noexcept {
    return g_profile_dirty;
}

void mark_profile_dirty(bool dirty) noexcept {
    g_profile_dirty = dirty;
}

} // namespace slinky::config
