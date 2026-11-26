#include "vendor/vendor_auth.hpp"

#include <cstdio>
#include <cstring>
#include <intrin.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <wincrypt.h>

// Isolated vendor HWID / host fingerprinting & SHA-256 hashing routines (TASK.md Req 3 & 4).
// Uses the exact ADVAPI32 WinCrypt imports (`CryptAcquireContextA`, `CryptCreateHash`,
// `CryptHashData`, `CryptGetHashParam`, `CryptDestroyHash`, `CryptReleaseContext`) present in
// `slinky_beta`. Never included or invoked by `libslinky.dll`.

namespace slinky::vendor {

namespace {

std::string query_cpu_brand_string(uint32_t* out_signature) {
    int info[4] = {0, 0, 0, 0};
    __cpuid(info, 1);
    if (out_signature) {
        *out_signature = static_cast<uint32_t>(info[0]);
    }

    int ext_max[4] = {0, 0, 0, 0};
    __cpuid(ext_max, static_cast<int>(0x80000000u));
    if (static_cast<uint32_t>(ext_max[0]) < 0x80000004u) {
        return "UnknownCPU";
    }

    char brand[49]{};
    for (uint32_t leaf = 0x80000002u; leaf <= 0x80000004u; ++leaf) {
        int leaf_regs[4] = {0, 0, 0, 0};
        __cpuid(leaf_regs, static_cast<int>(leaf));
        std::memcpy(brand + (leaf - 0x80000002u) * 16u, leaf_regs, 16u);
    }
    brand[48] = '\0';
    return std::string(brand);
}

std::string query_system_volume_serial() {
    DWORD serial = 0;
    if (!GetVolumeInformationA("C:\\", nullptr, 0, &serial, nullptr, nullptr, nullptr, 0)) {
        return "00000000";
    }
    char buf[16]{};
    std::snprintf(buf, sizeof(buf), "%08lX", static_cast<unsigned long>(serial));
    return std::string(buf);
}

std::string query_machine_guid() {
    HKEY key = nullptr;
    if (RegOpenKeyExA(
            HKEY_LOCAL_MACHINE,
            "SOFTWARE\\Microsoft\\Cryptography",
            0,
            KEY_READ | KEY_WOW64_64KEY,
            &key) != ERROR_SUCCESS) {
        return {};
    }
    char guid[128]{};
    DWORD size = static_cast<DWORD>(sizeof(guid) - 1);
    DWORD type = 0;
    const LSTATUS st = RegQueryValueExA(
        key,
        "MachineGuid",
        nullptr,
        &type,
        reinterpret_cast<LPBYTE>(guid),
        &size);
    RegCloseKey(key);
    if (st != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ)) {
        return {};
    }
    return std::string(guid);
}

std::string query_smbios_system_uuid() {
    constexpr DWORD kRsmbSignature = 0x52534D42u; // 'RSMB'
    const UINT table_size = GetSystemFirmwareTable(kRsmbSignature, 0, nullptr, 0);
    if (table_size < 16 || table_size > 0x40000u) {
        return {};
    }
    std::vector<uint8_t> buf(table_size);
    if (GetSystemFirmwareTable(kRsmbSignature, 0, buf.data(), table_size) != table_size) {
        return {};
    }

    // RawSMBIOSData header is 8 bytes; walk SMBIOS structures to Type 1 (System Information)
    size_t offset = 8;
    while (offset + 4 <= buf.size()) {
        const uint8_t type = buf[offset];
        const uint8_t len  = buf[offset + 1];
        if (len < 4 || offset + len > buf.size()) {
            break;
        }
        if (type == 1 && len >= 0x18 && offset + 0x18 <= buf.size()) {
            const uint8_t* u = &buf[offset + 8];
            char uuid_str[40]{};
            std::snprintf(
                uuid_str,
                sizeof(uuid_str),
                "%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X",
                u[3], u[2], u[1], u[0],
                u[5], u[4],
                u[7], u[6],
                u[8], u[9],
                u[10], u[11], u[12], u[13], u[14], u[15]);
            return std::string(uuid_str);
        }
        // Skip formatted area + double-NUL string table
        size_t str_end = offset + len;
        while (str_end + 1 < buf.size() && !(buf[str_end] == 0 && buf[str_end + 1] == 0)) {
            ++str_end;
        }
        offset = str_end + 2;
        if (type == 127) {
            break;
        }
    }
    return {};
}

std::string query_env_string_narrow(const char* var_name) {
    char buf[260]{};
    const DWORD n = GetEnvironmentVariableA(var_name, buf, static_cast<DWORD>(sizeof(buf)));
    if (n == 0 || n >= sizeof(buf)) {
        return {};
    }
    return std::string(buf, n);
}

} // namespace

bool compute_sha256_bytes(const void* data, size_t size, uint8_t out_digest[32]) noexcept {
    if (!out_digest || (!data && size > 0)) {
        return false;
    }
    HCRYPTPROV prov = 0;
    if (!CryptAcquireContextA(&prov, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
        return false;
    }

    HCRYPTHASH hash = 0;
    bool ok = false;
    if (CryptCreateHash(prov, CALG_SHA_256, 0, 0, &hash)) {
        if (CryptHashData(
                hash,
                static_cast<const BYTE*>(data),
                static_cast<DWORD>(size),
                0)) {
            DWORD digest_len = 32;
            if (CryptGetHashParam(hash, HP_HASHVAL, out_digest, &digest_len, 0) && digest_len == 32) {
                ok = true;
            }
        }
        CryptDestroyHash(hash);
    }
    CryptReleaseContext(prov, 0);
    return ok;
}

std::string compute_sha256_hex(const void* data, size_t size) {
    uint8_t digest[32]{};
    if (!compute_sha256_bytes(data, size, digest)) {
        return {};
    }
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.resize(64);
    for (size_t i = 0; i < 32; ++i) {
        out[i * 2 + 0] = kHex[(digest[i] >> 4) & 0x0Fu];
        out[i * 2 + 1] = kHex[digest[i] & 0x0Fu];
    }
    return out;
}

bool collect_host_fingerprint(HostFingerprint* out_fp) noexcept {
    if (!out_fp) {
        return false;
    }
    try {
        out_fp->cpu_brand          = query_cpu_brand_string(&out_fp->cpu_signature);
        out_fp->disk_volume_serial = query_system_volume_serial();
        out_fp->machine_guid       = query_machine_guid();
        out_fp->smbios_uuid        = query_smbios_system_uuid();
        out_fp->computer_name      = query_env_string_narrow("COMPUTERNAME");
        out_fp->user_name          = query_env_string_narrow("USERNAME");

        std::string canonical;
        canonical.reserve(256);
        canonical += out_fp->cpu_brand;
        canonical += '|';
        char sig_hex[16]{};
        std::snprintf(sig_hex, sizeof(sig_hex), "%08X", out_fp->cpu_signature);
        canonical += sig_hex;
        canonical += '|';
        canonical += out_fp->disk_volume_serial;
        canonical += '|';
        canonical += out_fp->machine_guid;
        canonical += '|';
        canonical += out_fp->smbios_uuid;
        canonical += '|';
        canonical += out_fp->computer_name;

        out_fp->combined_sha256_hex = compute_sha256_hex(canonical.data(), canonical.size());
        return !out_fp->combined_sha256_hex.empty();
    } catch (...) {
        return false;
    }
}

} // namespace slinky::vendor
