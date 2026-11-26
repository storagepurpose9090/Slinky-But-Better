#pragma once

#include <cstddef>
#include <cstdint>
#include "events/module_manager.hpp"

namespace slinky::config {

// Sorted keybind dispatch table (48 bytes = 0x30) - matches sub_18002F750 .. sub_18002FD50
using KeybindTriggerFn = bool (*)(void* target, uint32_t key_code, bool pressed);
using KeybindEvictFn   = void (*)(void* target);

struct KeybindBucket {
    int32_t key_code = 0;       // +0x00
    int32_t pad = 0;            // +0x04
    size_t count = 0;           // +0x08
    size_t capacity = 0;        // +0x10
    void** targets = nullptr;   // +0x18
};
static_assert(sizeof(KeybindBucket) == 32, "KeybindBucket must be 32 bytes");

struct KeybindMap {
    KeybindTriggerFn on_trigger = nullptr; // +0x00
    KeybindEvictFn on_evict = nullptr;     // +0x08
    size_t count = 0;                      // +0x10
    size_t capacity = 0;                   // +0x18
    KeybindBucket* buckets = nullptr;      // +0x20
    bool allow_multiple = false;           // +0x28
    uint8_t pad[7]{};

    static KeybindMap* create(KeybindTriggerFn trigger_cb, KeybindEvictFn evict_cb, bool allow_multi) noexcept;
    static void destroy(KeybindMap* map) noexcept;

    bool bind(int32_t key_code, void* target) noexcept;
    void unbind(int32_t key_code, void* target) noexcept;
    void* find(int32_t key_code, void* target = nullptr) const noexcept;
    bool dispatch(uint32_t key_code, bool pressed) const noexcept;
    void release_all() const noexcept;
};
static_assert(sizeof(KeybindMap) == 48, "KeybindMap must be 48 bytes");

// 64-byte profile metadata entry - matches sub_18002FF80 .. sub_1800301E0
struct ProfileEntry {
    char* name = nullptr;                 // +0x00 (0)
    const char* display_name_end = nullptr; // +0x08 (8) - &name[min(len, 15)]
    char modified_label[32]{};            // +0x10..+0x2F (16..47)
    int64_t age_seconds = -1;             // +0x30 (48)
    uint32_t keybind = 0;                 // +0x38 (56)
    uint32_t pad = 0;                     // +0x3C (60)
};
static_assert(sizeof(ProfileEntry) == 64, "ProfileEntry must be 64 bytes");

using SetProfileKeybindFn = void (*)(ProfileEntry* entry, uint32_t key_code);

// 32-byte profile list registry - matches sub_18002FD60 .. sub_180030240
struct ProfileRegistry {
    size_t count = 0;                     // +0x00 (0)
    size_t capacity = 0;                  // +0x08 (8)
    ProfileEntry* entries = nullptr;      // +0x10 (16)
    SetProfileKeybindFn set_bind_fn = nullptr; // +0x18 (24)

    static ProfileRegistry* create(SetProfileKeybindFn bind_cb) noexcept;
    static void destroy(ProfileRegistry* reg) noexcept;

    SetProfileKeybindFn bind_callback() const noexcept { return set_bind_fn; }
    int refresh_from_disk() noexcept;
    const ProfileEntry* get_entries(size_t* out_count) const noexcept;
    ProfileEntry* find_by_name(const char* profile_name) noexcept;
};
static_assert(sizeof(ProfileRegistry) == 32, "ProfileRegistry must be 32 bytes");

enum class ProfileModalAction : int32_t {
    LoadOverwrite = 0,
    SaveOverwrite = 1,
    OpenDirectory = 2,
    DeleteProfile = 3,
};

// Captures initial module setting values as defaults across all 67 modules - matches sub_18002D050
void capture_all_module_defaults(events::ModuleManager* mgr) noexcept;

// Global settings (.slinky/settings: GUI, Friends, profile_binds) - matches sub_18002CC40 .. sub_18002D040
int load_global_settings(events::ModuleManager* mgr, ProfileRegistry* profiles) noexcept;
int save_global_settings(events::ModuleManager* mgr, ProfileRegistry* profiles) noexcept;

// Per-profile I/O (.slinky/profiles/<name>, or nullptr to reset to defaults) - matches sub_18002D0C0 .. sub_18002D5A0
int load_or_reset_profile(const char* profile_name, events::ModuleManager* mgr) noexcept;
int save_profile(const char* profile_name, bool allow_overwrite, events::ModuleManager* mgr) noexcept;
int delete_profile_file(const char* profile_name) noexcept;
int open_profiles_directory() noexcept;

// Active profile name & dirty tracking - matches sub_18002D240, sub_18002D470..0x2D490, sub_18002D570
void set_active_profile_name(const char* name) noexcept;
const char* get_active_profile_name() noexcept;
void free_active_profile_name() noexcept;
bool is_profile_dirty() noexcept;
void mark_profile_dirty(bool dirty) noexcept;

} // namespace slinky::config
