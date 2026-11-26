#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

// NOTE: Isolated vendor authentication, licensing, HWID, telemetry, and HTTP client declarations
// (Requirement 3 & 4 of TASK.md).
// Restored so readers can inspect how `slinky_beta` handled license validation, host fingerprinting,
// encrypted HTTPS requests (`libcurl`), and loader entrypoints (`slinky_init`).
// NEVER #include or call any symbol from `slinky::vendor` in the active client (`libslinky.dll`).

namespace slinky::vendor {

constexpr size_t kLicenseKeyLength = 32;

// Vendor status / error codes reconstructed from `StartAddress` (0x1800010F0) and `auth_client` (0x180001290)
enum VendorStatus : uint32_t {
    kStatusOk                       = 0,
    kErrorAllocationFailed          = 1005,
    kErrorEnvironmentCheckFailed    = 7000,
    kErrorHwidCollectionFailed      = 7001,
    kErrorNetworkRequestFailed      = 7002,
    kErrorInvalidLicenseKey         = 7003,
    kErrorPayloadDecryptionFailed   = 7004,
    kErrorLicenseExpiredOrBanned    = 7005,
    kErrorClientInitFailed          = 7006,
};

// Host hardware fingerprint components collected prior to authentication / telemetry
struct HostFingerprint {
    std::string cpu_brand;
    uint32_t    cpu_signature = 0;
    std::string disk_volume_serial;
    std::string machine_guid;
    std::string smbios_uuid;
    std::string computer_name;
    std::string user_name;
    std::string combined_sha256_hex;
};

// Collects host hardware identifiers and computes the SHA-256 HWID digest via WinCrypt (`ADVAPI32`)
[[nodiscard]] bool collect_host_fingerprint(HostFingerprint* out_fp) noexcept;
[[nodiscard]] bool compute_sha256_bytes(const void* data, size_t size, uint8_t out_digest[32]) noexcept;
[[nodiscard]] std::string compute_sha256_hex(const void* data, size_t size);

// HTTPS client wrapper around libcurl (`0x180030E40`) used by licensing & telemetry endpoints
struct HttpResponse {
    long                 http_status = 0;
    int                  curl_code   = 0;
    std::vector<uint8_t> body;
};

[[nodiscard]] bool https_post_encrypted(
    const char* url,
    const uint8_t* payload,
    size_t payload_size,
    const char* user_agent,
    HttpResponse* out_response) noexcept;

// Displays a vendor error dialog (`MessageBoxA` wrapper matching `sub_18006E560`)
void show_vendor_error_dialog(const char* message) noexcept;

// Spawns the 1000ms delayed `FreeLibraryAndExitThread` worker (`sub_18006DFB0` / `sub_18006DFF0`)
bool spawn_delayed_self_unload(HMODULE module_handle) noexcept;

// Full vendor license handshake + telemetry session (`sub_180001290`)
[[nodiscard]] uint32_t authenticate_and_validate_license(const char ascii_license_key[kLicenseKeyLength + 1]) noexcept;

// Original vendor DLL entrypoints (`slinky_init` @ `0x180001000`, `StartAddress` @ `0x1800010F0`, `DllMain` @ `0x180001220`)
uint32_t vendor_slinky_init_export(const wchar_t* wide_license_key) noexcept;
DWORD WINAPI vendor_license_worker_thread(LPVOID parameter) noexcept;
BOOL vendor_dll_main(HMODULE module_handle, DWORD reason) noexcept;

} // namespace slinky::vendor
