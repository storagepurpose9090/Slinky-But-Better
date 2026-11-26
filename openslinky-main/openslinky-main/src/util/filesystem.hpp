#pragma once

#include <cstddef>
#include <cstdint>

namespace slinky::fs {

struct WidePath {
    wchar_t* ptr = nullptr;
};

struct DirectoryEntry {
    const char* name = nullptr;
    int64_t age_seconds = -1;
};

using DirectoryVisitorFn = int (*)(const DirectoryEntry* entry, void* user_data);

// Initializes `out` with `%USERPROFILE%` - matches sub_18006E880
int init_user_profile_path(WidePath* out) noexcept;

// Appends `\<component>` to `path` - matches sub_18006E900
int append_path(WidePath* path, const char* component) noexcept;

// Pops the trailing `\component` from `path` in-place - matches sub_18006E9F0
void pop_path(WidePath* path) noexcept;

// Creates directory (succeeds if already exists) - matches sub_18006EA30
int create_directory(const WidePath* path) noexcept;

// Deletes file - matches sub_18006EA70
int delete_file(const WidePath* path) noexcept;

// Opens directory in Windows Explorer (`ShellExecuteW("explore")`) - matches sub_18006EAA0
int open_in_explorer(const WidePath* path) noexcept;

// Iterates non-directory files in `dir_path`, passing filename and modification age in seconds - matches sub_18006EAF0
int list_files(const WidePath* dir_path, void* user_data, DirectoryVisitorFn visitor) noexcept;

// Reads entire file as NUL-terminated heap buffer - matches sub_18006ED20
int read_text_file(const WidePath* path, char** out_text) noexcept;

// Reads exact `size` bytes into `out_buf` - matches sub_18006EE50
int read_exact_bytes(const WidePath* path, void* out_buf, size_t size) noexcept;

// Writes string `content` to `path` (`overwrite=true` -> CREATE_ALWAYS, `false` -> CREATE_NEW; returns 1012 if exists) - matches sub_18006EF30
int write_text_file(const WidePath* path, const char* content, bool overwrite) noexcept;

// Frees `path->ptr` and sets to nullptr - matches sub_18006F010
void free_path(WidePath* path) noexcept;

// Formats a message and displays a modal Win32 error dialog ("Slinky") - matches sub_18006E560
void show_error_dialog(const char* fmt, ...) noexcept;

} // namespace slinky::fs
