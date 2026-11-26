#include "vendor/vendor_auth.hpp"
#include "vendor/anti_debug_tamper.hpp"
#include "vendor/crypto_aes.hpp"

#include "cJSON.h"

#include <cstdio>
#include <cstring>
#include <ctime>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <bcrypt.h>

// Isolated vendor authentication, licensing & telemetry client (`sub_180001290`, TASK.md Req 3 & 4).
// Uses the exact 128-bit AES key from `.rdata` 0x18015A0C4, `BCryptGenRandom` IV generation,
// `cJSON` payload serialization, and error format strings (`0x18015A0F8`, `0x18015A754`).
// Never included or invoked by `libslinky.dll`.

namespace slinky::vendor {

namespace {

// Exact 128-bit AES key extracted from `.rdata` @ 0x18015A0C4 (`lea rcx, [rip + 0x158db7]` at 0x180001306)
constexpr uint8_t kVendorAes128Key[kAes128KeySize] = {
    0xFB, 0xA9, 0x69, 0x98, 0xDE, 0xB6, 0x99, 0xF6,
    0xB0, 0x2A, 0x50, 0xC0, 0x0D, 0x5E, 0x71, 0x43,
};

constexpr const char* kVendorAuthEndpoint      = "https://api.slinky.gg/v1/client/authenticate";
constexpr const char* kVendorTelemetryEndpoint = "https://api.slinky.gg/v1/client/telemetry";
constexpr const char* kVendorClientName        = "slinky"; // 0x18015A0D5

bool generate_random_iv(uint8_t out_iv[kAesBlockSize]) noexcept {
    const NTSTATUS st = BCryptGenRandom(
        nullptr,
        out_iv,
        static_cast<ULONG>(kAesBlockSize),
        BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    return st >= 0;
}

std::vector<uint8_t> encrypt_iv_prefixed_cbc(const uint8_t* plaintext, size_t len) {
    uint8_t iv[kAesBlockSize]{};
    if (!generate_random_iv(iv)) {
        return {};
    }

    const size_t pad_len   = kAesBlockSize - (len % kAesBlockSize);
    const size_t total_len = len + pad_len;

    std::vector<uint8_t> packet(kAesBlockSize + total_len);
    std::memcpy(packet.data(), iv, kAesBlockSize);
    if (len > 0) {
        std::memcpy(packet.data() + kAesBlockSize, plaintext, len);
    }
    std::memset(packet.data() + kAesBlockSize + len, static_cast<int>(pad_len), pad_len);

    Aes128Context ctx{};
    aes128_init_ctx_iv(&ctx, kVendorAes128Key, iv);
    aes128_cbc_encrypt_buffer(&ctx, packet.data() + kAesBlockSize, total_len);
    return packet;
}

bool decrypt_iv_prefixed_cbc(const std::vector<uint8_t>& packet, std::string* out_plaintext) {
    if (!out_plaintext || packet.size() < 2 * kAesBlockSize || (packet.size() % kAesBlockSize) != 0) {
        return false;
    }

    const uint8_t* iv      = packet.data();
    const size_t cipher_sz = packet.size() - kAesBlockSize;
    std::vector<uint8_t> buf(packet.begin() + static_cast<ptrdiff_t>(kAesBlockSize), packet.end());

    Aes128Context ctx{};
    aes128_init_ctx_iv(&ctx, kVendorAes128Key, iv);
    aes128_cbc_decrypt_buffer(&ctx, buf.data(), cipher_sz);

    const uint8_t pad = buf.back();
    if (pad == 0 || pad > kAesBlockSize || pad > buf.size()) {
        return false;
    }
    for (size_t i = buf.size() - pad; i < buf.size(); ++i) {
        if (buf[i] != pad) {
            return false;
        }
    }

    out_plaintext->assign(reinterpret_cast<const char*>(buf.data()), buf.size() - pad);
    return true;
}

void send_session_telemetry(const char* license_key, const HostFingerprint& fp) noexcept {
    cJSON* root = cJSON_CreateObject();
    if (!root) {
        return;
    }
    cJSON_AddStringToObject(root, "client", kVendorClientName);
    cJSON_AddStringToObject(root, "license", license_key ? license_key : "");
    cJSON_AddStringToObject(root, "hwid", fp.combined_sha256_hex.c_str());
    cJSON_AddStringToObject(root, "cpu", fp.cpu_brand.c_str());
    cJSON_AddNumberToObject(root, "timestamp", static_cast<double>(std::time(nullptr)));

    char* json_text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json_text) {
        return;
    }

    const std::vector<uint8_t> encrypted = encrypt_iv_prefixed_cbc(
        reinterpret_cast<const uint8_t*>(json_text),
        std::strlen(json_text));
    cJSON_free(json_text);

    if (!encrypted.empty()) {
        HttpResponse ignored{};
        (void)https_post_encrypted(
            kVendorTelemetryEndpoint,
            encrypted.data(),
            encrypted.size(),
            kVendorClientName,
            &ignored);
    }
}

void format_and_show_auth_error(const char* detail) noexcept {
    char msg[256]{};
    // Exact format string @ 0x18015A0F8: "Authentication failed: %s"
    std::snprintf(msg, sizeof(msg), "Authentication failed: %s", detail ? detail : "unknown error");
    show_vendor_error_dialog(msg);
}

void format_and_show_api_status_error(unsigned int http_status) noexcept {
    char msg[128]{};
    // Exact format string @ 0x18015A754: "API error: status %u"
    std::snprintf(msg, sizeof(msg), "API error: status %u", http_status);
    show_vendor_error_dialog(msg);
}

} // namespace

// Matches `sub_180001290` @ 0x180001290
uint32_t authenticate_and_validate_license(const char ascii_license_key[kLicenseKeyLength + 1]) noexcept {
    if (!ascii_license_key || std::strlen(ascii_license_key) != kLicenseKeyLength) {
        show_vendor_error_dialog("Invalid license key");
        return kErrorInvalidLicenseKey;
    }

    if (is_debugger_or_tamper_active()) {
        return kErrorEnvironmentCheckFailed;
    }

    // Check for active debugger windows (`FindWindowW` @ 0x18001FF11 -> L"x64dbg" @ 0x18015A6FE)
    if (FindWindowW(nullptr, L"x64dbg") != nullptr) {
        return kErrorEnvironmentCheckFailed;
    }

    HostFingerprint fp{};
    if (!collect_host_fingerprint(&fp)) {
        format_and_show_auth_error("HWID initialization error");
        return kErrorHwidCollectionFailed;
    }

    cJSON* req = cJSON_CreateObject();
    if (!req) {
        return kErrorAllocationFailed;
    }
    cJSON_AddStringToObject(req, "client", kVendorClientName);
    cJSON_AddStringToObject(req, "license", ascii_license_key);
    cJSON_AddStringToObject(req, "hwid", fp.combined_sha256_hex.c_str());
    cJSON_AddStringToObject(req, "cpu", fp.cpu_brand.c_str());
    cJSON_AddStringToObject(req, "volume_serial", fp.disk_volume_serial.c_str());
    cJSON_AddStringToObject(req, "machine_guid", fp.machine_guid.c_str());
    cJSON_AddStringToObject(req, "smbios_uuid", fp.smbios_uuid.c_str());
    cJSON_AddStringToObject(req, "computer_name", fp.computer_name.c_str());
    cJSON_AddStringToObject(req, "user_name", fp.user_name.c_str());
    cJSON_AddNumberToObject(req, "timestamp", static_cast<double>(std::time(nullptr)));

    char* req_json = cJSON_PrintUnformatted(req);
    cJSON_Delete(req);
    if (!req_json) {
        return kErrorAllocationFailed;
    }

    std::vector<uint8_t> encrypted_req;
    try {
        encrypted_req = encrypt_iv_prefixed_cbc(
            reinterpret_cast<const uint8_t*>(req_json),
            std::strlen(req_json));
    } catch (...) {
        cJSON_free(req_json);
        return kErrorAllocationFailed;
    }
    cJSON_free(req_json);

    if (encrypted_req.empty()) {
        return kErrorPayloadDecryptionFailed;
    }

    HttpResponse http_resp{};
    if (!https_post_encrypted(
            kVendorAuthEndpoint,
            encrypted_req.data(),
            encrypted_req.size(),
            kVendorClientName,
            &http_resp)) {
        format_and_show_auth_error("network error");
        return kErrorNetworkRequestFailed;
    }

    if (http_resp.http_status != 200) {
        format_and_show_api_status_error(static_cast<unsigned int>(http_resp.http_status));
        return kErrorNetworkRequestFailed;
    }

    std::string decrypted_json;
    if (!decrypt_iv_prefixed_cbc(http_resp.body, &decrypted_json)) {
        format_and_show_auth_error("invalid server response");
        return kErrorPayloadDecryptionFailed;
    }

    cJSON* resp = cJSON_Parse(decrypted_json.c_str());
    if (!resp) {
        format_and_show_auth_error("malformed response");
        return kErrorPayloadDecryptionFailed;
    }

    const cJSON* status_item = cJSON_GetObjectItemCaseSensitive(resp, "status");
    const cJSON* msg_item    = cJSON_GetObjectItemCaseSensitive(resp, "message");
    const bool authorized    = cJSON_IsString(status_item) &&
                               status_item->valuestring != nullptr &&
                               std::strcmp(status_item->valuestring, "ok") == 0;
    if (!authorized) {
        const char* reason = (cJSON_IsString(msg_item) && msg_item->valuestring)
                                 ? msg_item->valuestring
                                 : "license rejected";
        format_and_show_auth_error(reason);
        cJSON_Delete(resp);
        return kErrorLicenseExpiredOrBanned;
    }

    cJSON_Delete(resp);
    send_session_telemetry(ascii_license_key, fp);
    return kStatusOk;
}

} // namespace slinky::vendor
