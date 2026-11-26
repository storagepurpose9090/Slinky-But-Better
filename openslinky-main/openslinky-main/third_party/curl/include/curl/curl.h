#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void CURL;

struct curl_slist {
    char* data;
    struct curl_slist* next;
};

typedef enum {
    CURLE_OK = 0,
    CURLE_UNSUPPORTED_PROTOCOL = 1,
    CURLE_FAILED_INIT = 2,
    CURLE_URL_MALFORMAT = 3,
    CURLE_COULDNT_RESOLVE_HOST = 6,
    CURLE_COULDNT_CONNECT = 7,
    CURLE_HTTP_RETURNED_ERROR = 22,
    CURLE_WRITE_ERROR = 23,
    CURLE_OUT_OF_MEMORY = 27,
    CURLE_OPERATION_TIMEDOUT = 28,
    CURLE_SSL_CONNECT_ERROR = 35,
    CURLE_PEER_FAILED_VERIFICATION = 60,
    CURLE_GOT_NOTHING = 52,
    CURLE_SEND_ERROR = 55,
    CURLE_RECV_ERROR = 56
} CURLcode;

typedef enum {
    CURLOPT_WRITEDATA          = 10001,
    CURLOPT_URL                = 10002,
    CURLOPT_POSTFIELDS         = 10015,
    CURLOPT_USERAGENT          = 10018,
    CURLOPT_HTTPHEADER         = 10023,
    CURLOPT_WRITEFUNCTION      = 20011,
    CURLOPT_POST               = 47,
    CURLOPT_FOLLOWLOCATION     = 52,
    CURLOPT_POSTFIELDSIZE      = 60,
    CURLOPT_SSL_VERIFYPEER     = 64,
    CURLOPT_SSL_VERIFYHOST     = 81,
    CURLOPT_NOSIGNAL           = 99,
    CURLOPT_TIMEOUT            = 13,
    CURLOPT_CONNECTTIMEOUT     = 78,
    CURLOPT_TIMEOUT_MS         = 155,
    CURLOPT_CONNECTTIMEOUT_MS  = 156
} CURLoption;

typedef enum {
    CURLINFO_RESPONSE_CODE = 0x200002
} CURLINFO;

#define CURL_GLOBAL_DEFAULT 3L

typedef size_t (*curl_write_callback)(char* ptr, size_t size, size_t nmemb, void* userdata);

CURLcode curl_global_init(long flags);
void     curl_global_cleanup(void);

CURL*    curl_easy_init(void);
CURLcode curl_easy_setopt(CURL* curl, CURLoption option, ...);
CURLcode curl_easy_perform(CURL* curl);
CURLcode curl_easy_getinfo(CURL* curl, CURLINFO info, ...);
void     curl_easy_cleanup(CURL* curl);
const char* curl_easy_strerror(CURLcode errornum);

struct curl_slist* curl_slist_append(struct curl_slist* list, const char* string);
void               curl_slist_free_all(struct curl_slist* list);

#ifdef __cplusplus
}
#endif
