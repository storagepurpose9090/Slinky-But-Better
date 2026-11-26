#include "vendor/vendor_auth.hpp"

#include "curl/curl.h"

#include <cstring>

// Isolated vendor HTTPS transport wrapper around libcurl (`sub_180030E40`, TASK.md Req 3 & 4).
// Never included or invoked by `libslinky.dll`.

namespace slinky::vendor {

namespace {

size_t write_response_callback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    const size_t total = size * nmemb;
    auto* body = static_cast<std::vector<uint8_t>*>(userdata);
    if (!body || (!ptr && total > 0)) {
        return 0;
    }
    try {
        const auto* begin = reinterpret_cast<const uint8_t*>(ptr);
        body->insert(body->end(), begin, begin + total);
        return total;
    } catch (...) {
        return 0;
    }
}

} // namespace

// Matches `sub_180030E40` @ 0x180030E40
bool https_post_encrypted(
    const char* url,
    const uint8_t* payload,
    size_t payload_size,
    const char* user_agent,
    HttpResponse* out_response) noexcept {
    if (!url || !out_response || (!payload && payload_size > 0)) {
        return false;
    }

    out_response->http_status = 0;
    out_response->curl_code   = CURLE_FAILED_INIT;
    out_response->body.clear();

    CURL* curl = curl_easy_init();
    if (!curl) {
        return false;
    }

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/octet-stream");
    headers = curl_slist_append(headers, "Accept: application/octet-stream");

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(payload_size));
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, user_agent ? user_agent : "slinky");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &write_response_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out_response->body);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);

    const CURLcode code = curl_easy_perform(curl);
    out_response->curl_code = static_cast<int>(code);
    if (code == CURLE_OK) {
        long response_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
        out_response->http_status = response_code;
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return code == CURLE_OK;
}

} // namespace slinky::vendor
