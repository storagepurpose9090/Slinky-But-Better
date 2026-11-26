#include "curl/curl.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>

typedef struct CurlEasyHandle {
    char*               url;
    char*               user_agent;
    const void*         post_fields;
    size_t              post_fields_size;
    int                 is_post;
    int                 follow_location;
    int                 verify_peer;
    int                 verify_host;
    long                timeout_ms;
    long                connect_timeout_ms;
    struct curl_slist*  headers;
    curl_write_callback write_cb;
    void*               write_userdata;
    long                response_code;
} CurlEasyHandle;

static char* dup_cstr(const char* s) {
    if (!s) {
        return NULL;
    }
    size_t len = strlen(s);
    char* copy = (char*)malloc(len + 1);
    if (copy) {
        memcpy(copy, s, len + 1);
    }
    return copy;
}

static wchar_t* utf8_to_wide(const char* s) {
    if (!s) {
        return NULL;
    }
    int wlen = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    if (wlen <= 0) {
        return NULL;
    }
    wchar_t* out = (wchar_t*)calloc((size_t)wlen, sizeof(wchar_t));
    if (!out) {
        return NULL;
    }
    MultiByteToWideChar(CP_UTF8, 0, s, -1, out, wlen);
    return out;
}

CURLcode curl_global_init(long flags) {
    (void)flags;
    return CURLE_OK;
}

void curl_global_cleanup(void) {}

CURL* curl_easy_init(void) {
    CurlEasyHandle* h = (CurlEasyHandle*)calloc(1, sizeof(CurlEasyHandle));
    if (!h) {
        return NULL;
    }
    h->verify_peer        = 1;
    h->verify_host        = 2;
    h->timeout_ms         = 15000;
    h->connect_timeout_ms = 10000;
    return (CURL*)h;
}

CURLcode curl_easy_setopt(CURL* curl, CURLoption option, ...) {
    if (!curl) {
        return CURLE_FAILED_INIT;
    }
    CurlEasyHandle* h = (CurlEasyHandle*)curl;
    va_list ap;
    va_start(ap, option);

    CURLcode rc = CURLE_OK;
    switch (option) {
        case CURLOPT_URL: {
            const char* u = va_arg(ap, const char*);
            free(h->url);
            h->url = dup_cstr(u);
            if (u && !h->url) rc = CURLE_OUT_OF_MEMORY;
            break;
        }
        case CURLOPT_USERAGENT: {
            const char* ua = va_arg(ap, const char*);
            free(h->user_agent);
            h->user_agent = dup_cstr(ua);
            break;
        }
        case CURLOPT_POST:
            h->is_post = (int)va_arg(ap, long);
            break;
        case CURLOPT_POSTFIELDS:
            h->post_fields = va_arg(ap, const void*);
            h->is_post = 1;
            break;
        case CURLOPT_POSTFIELDSIZE:
            h->post_fields_size = (size_t)va_arg(ap, long);
            break;
        case CURLOPT_HTTPHEADER:
            h->headers = va_arg(ap, struct curl_slist*);
            break;
        case CURLOPT_WRITEFUNCTION:
            h->write_cb = va_arg(ap, curl_write_callback);
            break;
        case CURLOPT_WRITEDATA:
            h->write_userdata = va_arg(ap, void*);
            break;
        case CURLOPT_FOLLOWLOCATION:
            h->follow_location = (int)va_arg(ap, long);
            break;
        case CURLOPT_SSL_VERIFYPEER:
            h->verify_peer = (int)va_arg(ap, long);
            break;
        case CURLOPT_SSL_VERIFYHOST:
            h->verify_host = (int)va_arg(ap, long);
            break;
        case CURLOPT_NOSIGNAL:
            (void)va_arg(ap, long);
            break;
        case CURLOPT_TIMEOUT:
            h->timeout_ms = va_arg(ap, long) * 1000L;
            break;
        case CURLOPT_CONNECTTIMEOUT:
            h->connect_timeout_ms = va_arg(ap, long) * 1000L;
            break;
        case CURLOPT_TIMEOUT_MS:
            h->timeout_ms = va_arg(ap, long);
            break;
        case CURLOPT_CONNECTTIMEOUT_MS:
            h->connect_timeout_ms = va_arg(ap, long);
            break;
        default:
            break;
    }

    va_end(ap);
    return rc;
}

CURLcode curl_easy_perform(CURL* curl) {
    if (!curl) {
        return CURLE_FAILED_INIT;
    }
    CurlEasyHandle* h = (CurlEasyHandle*)curl;
    if (!h->url || !h->url[0]) {
        return CURLE_URL_MALFORMAT;
    }

    wchar_t* wurl = utf8_to_wide(h->url);
    if (!wurl) {
        return CURLE_OUT_OF_MEMORY;
    }

    wchar_t host_buf[256] = {0};
    wchar_t path_buf[1024] = {0};
    URL_COMPONENTS uc;
    memset(&uc, 0, sizeof(uc));
    uc.dwStructSize     = sizeof(uc);
    uc.lpszHostName     = host_buf;
    uc.dwHostNameLength = (DWORD)(sizeof(host_buf) / sizeof(host_buf[0]));
    uc.lpszUrlPath      = path_buf;
    uc.dwUrlPathLength  = (DWORD)(sizeof(path_buf) / sizeof(path_buf[0]));

    if (!WinHttpCrackUrl(wurl, 0, 0, &uc)) {
        free(wurl);
        return CURLE_URL_MALFORMAT;
    }
    free(wurl);

    wchar_t* wua = utf8_to_wide(h->user_agent ? h->user_agent : "slinky/1.0");
    HINTERNET session = WinHttpOpen(
        wua ? wua : L"slinky/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0);
    free(wua);
    if (!session) {
        return CURLE_FAILED_INIT;
    }

    WinHttpSetTimeouts(
        session,
        (int)h->connect_timeout_ms,
        (int)h->connect_timeout_ms,
        (int)h->timeout_ms,
        (int)h->timeout_ms);

    HINTERNET connect = WinHttpConnect(session, host_buf, uc.nPort, 0);
    if (!connect) {
        WinHttpCloseHandle(session);
        return CURLE_COULDNT_CONNECT;
    }

    const DWORD req_flags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    const wchar_t* verb = h->is_post ? L"POST" : L"GET";
    HINTERNET request = WinHttpOpenRequest(
        connect,
        verb,
        path_buf[0] ? path_buf : L"/",
        NULL,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        req_flags);
    if (!request) {
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return CURLE_COULDNT_CONNECT;
    }

    if ((req_flags & WINHTTP_FLAG_SECURE) && (!h->verify_peer || !h->verify_host)) {
        DWORD sec_flags = SECURITY_FLAG_IGNORE_UNKNOWN_CA |
                          SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
                          SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
                          SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
        WinHttpSetOption(request, WINHTTP_OPTION_SECURITY_FLAGS, &sec_flags, sizeof(sec_flags));
    }

    for (struct curl_slist* node = h->headers; node != NULL; node = node->next) {
        wchar_t* whdr = utf8_to_wide(node->data);
        if (whdr) {
            WinHttpAddRequestHeaders(request, whdr, (DWORD)-1L, WINHTTP_ADDREQ_FLAG_ADD);
            free(whdr);
        }
    }

    size_t body_len = h->post_fields_size;
    if (h->post_fields && body_len == 0) {
        body_len = strlen((const char*)h->post_fields);
    }

    BOOL sent = WinHttpSendRequest(
        request,
        WINHTTP_NO_ADDITIONAL_HEADERS,
        0,
        (LPVOID)h->post_fields,
        (DWORD)body_len,
        (DWORD)body_len,
        0);
    if (!sent || !WinHttpReceiveResponse(request, NULL)) {
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return CURLE_RECV_ERROR;
    }

    DWORD status_code = 0;
    DWORD status_sz   = sizeof(status_code);
    if (WinHttpQueryHeaders(
            request,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX,
            &status_code,
            &status_sz,
            WINHTTP_NO_HEADER_INDEX)) {
        h->response_code = (long)status_code;
    }

    CURLcode res = CURLE_OK;
    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(request, &avail)) {
            res = CURLE_RECV_ERROR;
            break;
        }
        if (avail == 0) {
            break;
        }
        char* chunk = (char*)malloc(avail);
        if (!chunk) {
            res = CURLE_OUT_OF_MEMORY;
            break;
        }
        DWORD read_n = 0;
        if (!WinHttpReadData(request, chunk, avail, &read_n)) {
            free(chunk);
            res = CURLE_RECV_ERROR;
            break;
        }
        if (read_n > 0 && h->write_cb) {
            size_t written = h->write_cb(chunk, 1, (size_t)read_n, h->write_userdata);
            if (written != (size_t)read_n) {
                free(chunk);
                res = CURLE_WRITE_ERROR;
                break;
            }
        }
        free(chunk);
    }

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return res;
}

CURLcode curl_easy_getinfo(CURL* curl, CURLINFO info, ...) {
    if (!curl) {
        return CURLE_FAILED_INIT;
    }
    CurlEasyHandle* h = (CurlEasyHandle*)curl;
    va_list ap;
    va_start(ap, info);
    if (info == CURLINFO_RESPONSE_CODE) {
        long* out_code = va_arg(ap, long*);
        if (out_code) {
            *out_code = h->response_code;
        }
    }
    va_end(ap);
    return CURLE_OK;
}

void curl_easy_cleanup(CURL* curl) {
    if (!curl) {
        return;
    }
    CurlEasyHandle* h = (CurlEasyHandle*)curl;
    free(h->url);
    free(h->user_agent);
    free(h);
}

const char* curl_easy_strerror(CURLcode errornum) {
    switch (errornum) {
        case CURLE_OK:                       return "No error";
        case CURLE_UNSUPPORTED_PROTOCOL:     return "Unsupported protocol";
        case CURLE_FAILED_INIT:              return "Failed initialization";
        case CURLE_URL_MALFORMAT:            return "URL using bad/illegal format";
        case CURLE_COULDNT_RESOLVE_HOST:     return "Couldn't resolve host name";
        case CURLE_COULDNT_CONNECT:          return "Couldn't connect to server";
        case CURLE_HTTP_RETURNED_ERROR:      return "HTTP response code said error";
        case CURLE_WRITE_ERROR:              return "Failed writing received data";
        case CURLE_OUT_OF_MEMORY:            return "Out of memory";
        case CURLE_OPERATION_TIMEDOUT:       return "Timeout was reached";
        case CURLE_SSL_CONNECT_ERROR:        return "SSL connect error";
        case CURLE_PEER_FAILED_VERIFICATION: return "SSL peer certificate was not OK";
        default:                             return "Network request error";
    }
}

struct curl_slist* curl_slist_append(struct curl_slist* list, const char* string) {
    struct curl_slist* node = (struct curl_slist*)calloc(1, sizeof(struct curl_slist));
    if (!node) {
        return NULL;
    }
    node->data = dup_cstr(string);
    if (string && !node->data) {
        free(node);
        return NULL;
    }
    if (!list) {
        return node;
    }
    struct curl_slist* tail = list;
    while (tail->next) {
        tail = tail->next;
    }
    tail->next = node;
    return list;
}

void curl_slist_free_all(struct curl_slist* list) {
    while (list) {
        struct curl_slist* next = list->next;
        free(list->data);
        free(list);
        list = next;
    }
}
