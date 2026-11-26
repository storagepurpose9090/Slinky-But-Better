#include "vendor/vendor_auth.hpp"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <cwchar>

// NOTE: Isolated vendor entrypoint & license key loader routines (Requirement 3 & 4 of TASK.md).
// Restored from `slinky_init` (0x180001000), `StartAddress` (0x1800010F0), `sub_180001220` (0x180001220),
// `sub_180056760` (0x180056760), `sub_18006DFB0` (0x18006DFB0), `sub_18006DFF0` (0x18006DFF0),
// and `sub_18006E560` (0x18006E560).
// NEVER #include or call any symbol from `slinky::vendor` in the active client (`libslinky.dll`).

namespace slinky::vendor {

namespace {

HMODULE              g_vendor_module_handle = nullptr; // lpParameter @ 0x1803140C0
std::atomic<uint8_t> g_vendor_initialized{0};          // byte_1803140B8
std::atomic<uint8_t> g_vendor_running{1};              // byte_1803191B0

// Environment variable checked by `sub_180001220` when injected without calling `slinky_init` directly
constexpr const wchar_t* kLicenseEnvVarName = L"SLINKY_LICENSE";

// Matches `sub_180056760`: clears the active flag to trigger graceful self-unload on re-entry
void request_vendor_client_unload() noexcept {
    g_vendor_running.store(0, std::memory_order_release);
}

// Matches the 2x-unrolled `ch % 127` wide-to-ASCII conversion loop in `0x18000101B` and `0x18000111D`
void decode_wide_license_key(const wchar_t* wide_key, char out_ascii[kLicenseKeyLength + 1]) noexcept {
    std::memset(out_ascii, 0, kLicenseKeyLength + 1);
    if (!wide_key) {
        return;
    }
    for (size_t i = 0; i < kLicenseKeyLength; ++i) {
        const uint16_t ch = static_cast<uint16_t>(wide_key[i]);
        if (ch == 0) {
            break;
        }
        // Compiler strength-reduced `ch % 127u` in original binary:
        // q = (((517u * ch) >> 16) + ((uint16_t)(ch - ((517u * ch) >> 16)) >> 1)) >> 6;
        // out_ascii[i] = static_cast<char>(ch - 127u * q);
        out_ascii[i] = static_cast<char>(ch % 127u);
    }
}

// Matches `sub_18006DFF0` @ 0x18006DFF0
DWORD WINAPI delayed_unload_thread_proc(LPVOID parameter) noexcept {
    Sleep(1000);
    FreeLibraryAndExitThread(static_cast<HMODULE>(parameter), 0);
}

} // namespace

// Matches `sub_18006DFB0` @ 0x18006DFB0
bool spawn_delayed_self_unload(HMODULE module_handle) noexcept {
    HANDLE thread = CreateThread(
        nullptr,
        0,
        &delayed_unload_thread_proc,
        module_handle,
        0,
        nullptr);
    if (!thread) {
        return false;
    }
    return CloseHandle(thread) != FALSE;
}

// Matches `sub_18006E560` @ 0x18006E560
void show_vendor_error_dialog(const char* message) noexcept {
    MessageBoxA(
        nullptr,
        message ? message : "Unknown error",
        "Slinky",
        MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
}

// Matches exported `slinky_init` @ 0x180001000
uint32_t vendor_slinky_init_export(const wchar_t* wide_license_key) noexcept {
    char ascii_key[kLicenseKeyLength + 1]{};
    decode_wide_license_key(wide_license_key, ascii_key);

    uint8_t expected = 0;
    if (!g_vendor_initialized.compare_exchange_strong(expected, 1, std::memory_order_acq_rel)) {
        request_vendor_client_unload();
        return kStatusOk;
    }

    const uint32_t status = authenticate_and_validate_license(ascii_key);
    if (status != kStatusOk) {
        spawn_delayed_self_unload(g_vendor_module_handle);
        return status;
    }
    return kStatusOk;
}

// Matches `StartAddress` @ 0x1800010F0
DWORD WINAPI vendor_license_worker_thread(LPVOID parameter) noexcept {
    const auto* wide_key = static_cast<const wchar_t*>(parameter);
    if (!wide_key || std::wcslen(wide_key) != kLicenseKeyLength) {
        show_vendor_error_dialog("Invalid license key");
        return kErrorInvalidLicenseKey;
    }

    char ascii_key[kLicenseKeyLength + 1]{};
    decode_wide_license_key(wide_key, ascii_key);

    uint32_t status = kStatusOk;
    uint8_t expected = 0;
    if (!g_vendor_initialized.compare_exchange_strong(expected, 1, std::memory_order_acq_rel)) {
        request_vendor_client_unload();
    } else {
        const uint32_t auth_result = authenticate_and_validate_license(ascii_key);
        if (auth_result != kStatusOk) {
            spawn_delayed_self_unload(g_vendor_module_handle);
            status = auth_result;
        }
    }

    std::fprintf(stderr, "status: %lu\n", static_cast<unsigned long>(status));
    return status;
}

// Matches `sub_180001220` @ 0x180001220
BOOL vendor_dll_main(HMODULE module_handle, DWORD reason) noexcept {
    if (reason == DLL_PROCESS_ATTACH) {
        g_vendor_module_handle = module_handle;
        wchar_t* env_key = _wgetenv(kLicenseEnvVarName);
        if (env_key != nullptr) {
            HANDLE thread = CreateThread(
                nullptr,
                0,
                &vendor_license_worker_thread,
                env_key,
                0,
                nullptr);
            if (thread != nullptr) {
                CloseHandle(thread);
            }
        }
    }
    return TRUE;
}

} // namespace slinky::vendor
