#ifndef NATIVE_RA_PLATFORM_H
#define NATIVE_RA_PLATFORM_H

#include "native_ra_types.h"

typedef struct NRA_HttpRequest {
    const char* url;
    const char* post_data;
    const char* content_type;
    const char* user_agent;
    uint32_t connect_timeout_ms;
    uint32_t read_timeout_ms;
    uint32_t max_response_bytes;
} NRA_HttpRequest;

typedef struct NRA_HttpCompletion {
    NRA_RequestId request_id;
    int http_status_code;
    const uint8_t* body;
    size_t body_size;
    bool retryable;
} NRA_HttpCompletion;

typedef struct NRA_PlatformVTable {
    uint64_t (*now_ms)(void* userdata);
    void (*http_begin)(void* userdata, NRA_RequestId request_id, const NRA_HttpRequest* request);
    void (*http_cancel)(void* userdata, NRA_RequestId request_id);
    /* Wait for all HTTP callbacks; none may reference the context after return. */
    void (*http_shutdown)(void* userdata);
    NRA_Result (*secret_get)(void* userdata, const char* key, uint8_t* value, size_t capacity, size_t* size);
    NRA_Result (*secret_set)(void* userdata, const char* key, const uint8_t* value, size_t size);
    NRA_Result (*secret_delete)(void* userdata, const char* key);
    /* Opaque encrypted persistence for durable request journals. This is not
     * credential storage: platforms must keep it in a separate record/key. */
    NRA_Result (*secure_blob_get)(void* userdata, const char* key, uint8_t* value, size_t capacity, size_t* size);
    NRA_Result (*secure_blob_set)(void* userdata, const char* key, const uint8_t* value, size_t size);
    NRA_Result (*secure_blob_delete)(void* userdata, const char* key);
    void (*post_ui_command)(void* userdata, const NRA_UICommand* command);
    void (*log_redacted)(void* userdata, int level, const char* message);
} NRA_PlatformVTable;

#endif
