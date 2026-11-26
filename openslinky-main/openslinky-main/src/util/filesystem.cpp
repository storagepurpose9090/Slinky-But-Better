#include "util/filesystem.hpp"
#include "util/containers.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>

namespace slinky::fs {

int init_user_profile_path(WidePath* out) noexcept {
    const wchar_t* env = _wgetenv(L"USERPROFILE");
    if (env == nullptr || *env == L'\0') {
        return 1004;
    }
    const size_t len = std::wcslen(env);
    auto* buf = static_cast<wchar_t*>(util::zero_alloc(len + 1, sizeof(wchar_t)));
    if (buf == nullptr) {
        return 1005;
    }
    std::wcsncpy(buf, env, len);
    buf[len] = L'\0';
    out->ptr = buf;
    return 0;
}

int append_path(WidePath* path, const char* component) noexcept {
    if (path == nullptr || path->ptr == nullptr || component == nullptr) {
        return 1006;
    }
    const size_t base_len = std::wcslen(path->ptr);
    const size_t comp_len = std::strlen(component);
    const size_t cap = (base_len + comp_len + 2 > 261) ? (base_len + comp_len + 2) : 261;
    auto* next = static_cast<wchar_t*>(util::zero_alloc(cap, sizeof(wchar_t)));
    if (next == nullptr) {
        return 1005;
    }
    int wcomp_len = MultiByteToWideChar(CP_UTF8, 0, component, -1, nullptr, 0);
    if (wcomp_len <= 0) {
        std::free(next);
        return 1006;
    }
    std::wcsncpy(next, path->ptr, base_len);
    next[base_len] = L'\\';
    MultiByteToWideChar(CP_UTF8, 0, component, -1, next + base_len + 1, static_cast<int>(cap - base_len - 1));
    std::free(path->ptr);
    path->ptr = next;
    return 0;
}

void pop_path(WidePath* path) noexcept {
    if (path == nullptr || path->ptr == nullptr) {
        return;
    }
    size_t idx = std::wcslen(path->ptr);
    while (idx > 0 && path->ptr[idx] != L'\\') {
        --idx;
    }
    path->ptr[idx] = L'\0';
}

int create_directory(const WidePath* path) noexcept {
    if (CreateDirectoryW(path->ptr, nullptr)) {
        return 0;
    }
    return (GetLastError() == ERROR_ALREADY_EXISTS) ? 0 : 1014;
}

int delete_file(const WidePath* path) noexcept {
    return DeleteFileW(path->ptr) ? 0 : 1011;
}

int open_in_explorer(const WidePath* path) noexcept {
    const intptr_t res = reinterpret_cast<intptr_t>(
        ShellExecuteW(nullptr, L"explore", path->ptr, nullptr, nullptr, SW_SHOWNORMAL));
    return (res >= 33) ? 0 : 4000;
}

int list_files(const WidePath* dir_path, void* user_data, DirectoryVisitorFn visitor) noexcept {
    const size_t base_len = std::wcslen(dir_path->ptr);
    const size_t cap = (base_len + 3 > 262) ? (base_len + 3) : 262;
    auto* pattern = static_cast<wchar_t*>(util::zero_alloc(cap, sizeof(wchar_t)));
    if (pattern == nullptr) {
        return 1005;
    }
    if (std::swprintf(pattern, cap, L"%ls\\*", dir_path->ptr) < 0) {
        std::free(pattern);
        return 1006;
    }

    WIN32_FIND_DATAW find_data{};
    HANDLE handle = FindFirstFileW(pattern, &find_data);
    if (handle == INVALID_HANDLE_VALUE) {
        const int rc = (GetLastError() == ERROR_FILE_NOT_FOUND) ? 0 : 1013;
        std::free(pattern);
        return rc;
    }

    FILETIME now_ft{};
    GetSystemTimeAsFileTime(&now_ft);
    const uint64_t now_100ns = (static_cast<uint64_t>(now_ft.dwHighDateTime) << 32) | now_ft.dwLowDateTime;

    int status = 0;
    char utf8_name[MAX_PATH * 3]{};
    do {
        if ((find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
            const int conv = WideCharToMultiByte(
                CP_UTF8, 0, find_data.cFileName, -1, utf8_name, sizeof(utf8_name), nullptr, nullptr);
            if (conv <= 0) {
                status = 1006;
                goto done;
            }
            const uint64_t write_100ns =
                (static_cast<uint64_t>(find_data.ftLastWriteTime.dwHighDateTime) << 32) |
                find_data.ftLastWriteTime.dwLowDateTime;

            DirectoryEntry entry{};
            entry.name = utf8_name;
            entry.age_seconds = -1;
            if (write_100ns > 0 && write_100ns <= now_100ns) {
                entry.age_seconds = static_cast<int64_t>((now_100ns - write_100ns) / 10000000ull);
            }
            const int vrc = visitor(&entry, user_data);
            if (vrc != 0) {
                status = vrc;
                goto done;
            }
        }
    } while (FindNextFileW(handle, &find_data));

    status = (GetLastError() == ERROR_NO_MORE_FILES) ? 0 : 1015;

done:
    FindClose(handle);
    std::free(pattern);
    return status;
}

int read_text_file(const WidePath* path, char** out_text) noexcept {
    HANDLE file = CreateFileW(
        path->ptr, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return 1008;
    }

    const DWORD file_size = GetFileSize(file, nullptr);
    if (file_size == INVALID_FILE_SIZE) {
        CloseHandle(file);
        return 1009;
    }

    auto* buf = static_cast<char*>(std::malloc(static_cast<size_t>(file_size) + 1));
    if (buf == nullptr) {
        CloseHandle(file);
        return 1005;
    }

    size_t total_read = 0;
    while (total_read < file_size) {
        DWORD chunk = 0;
        if (!ReadFile(file, buf + total_read, static_cast<DWORD>(file_size - total_read), &chunk, nullptr) || chunk == 0) {
            std::free(buf);
            CloseHandle(file);
            return 1009;
        }
        total_read += chunk;
    }

    buf[file_size] = '\0';
    *out_text = buf;
    CloseHandle(file);
    return 0;
}

int read_exact_bytes(const WidePath* path, void* out_buf, size_t size) noexcept {
    HANDLE file = CreateFileW(
        path->ptr, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return 1008;
    }

    int rc = 0;
    size_t total_read = 0;
    auto* dst = static_cast<uint8_t*>(out_buf);
    while (total_read < size) {
        DWORD chunk = 0;
        if (!ReadFile(file, dst + total_read, static_cast<DWORD>(size - total_read), &chunk, nullptr)) {
            rc = 1009;
            break;
        }
        if (chunk == 0) {
            rc = 1018;
            break;
        }
        total_read += chunk;
    }

    CloseHandle(file);
    return rc;
}

int write_text_file(const WidePath* path, const char* content, bool overwrite) noexcept {
    const DWORD disposition = overwrite ? CREATE_ALWAYS : CREATE_NEW;
    HANDLE file = CreateFileW(
        path->ptr, GENERIC_WRITE, 0, nullptr, disposition, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return (GetLastError() == ERROR_FILE_EXISTS) ? 1012 : 1008;
    }

    const size_t total_len = std::strlen(content);
    size_t total_written = 0;
    int rc = 0;
    while (total_written < total_len) {
        DWORD written = 0;
        const BOOL ok = WriteFile(
            file, content + total_written, static_cast<DWORD>(total_len - total_written), &written, nullptr);
        total_written += written;
        if (!ok) {
            rc = 1010;
            break;
        }
    }

    CloseHandle(file);
    return rc;
}

void free_path(WidePath* path) noexcept {
    if (path != nullptr) {
        std::free(path->ptr);
        path->ptr = nullptr;
    }
}

void show_error_dialog(const char* fmt, ...) noexcept {
    if (fmt == nullptr) {
        return;
    }
    char buf[256]{};
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    MessageBoxA(nullptr, buf, "Slinky", MB_OK | MB_ICONERROR);
}

} // namespace slinky::fs
