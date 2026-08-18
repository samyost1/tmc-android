#ifndef NATIVE_RA_INTERNAL_H
#define NATIVE_RA_INTERNAL_H

#include "native_ra/native_ra.h"

#include "rc_client.h"
#include "native_ra_outbox.h"

#include <pthread.h>

#define NRA_MAX_IN_FLIGHT_REQUESTS 16u
#define NRA_MAX_COMPLETIONS 16u
#define NRA_MAX_RESPONSE_BYTES (1024u * 1024u)

typedef struct NRA_Request {
    NRA_RequestId id;
    rc_client_server_callback_t callback;
    void* callback_data;
    char* url;
    char* post_data;
    char* content_type;
    char* user_agent;
    NRA_OutboxKind outbox_kind;
    uint64_t outbox_sequence;
    bool outbox_replay;
    bool requires_memory;
    bool used;
} NRA_Request;

typedef struct NRA_Completion {
    NRA_RequestId id;
    int http_status_code;
    uint8_t* body;
    size_t body_size;
    bool retryable;
    bool requires_memory;
} NRA_Completion;

#ifndef NRA_ENABLE_STRICT_MODE
#define NRA_ENABLE_STRICT_MODE 0
#endif

#ifndef NRA_ENABLE_HARDCORE_APPROVED
#define NRA_ENABLE_HARDCORE_APPROVED 0
#endif

struct NRA_Context {
    NRA_PlatformVTable platform;
    void* platform_userdata;
    NRA_GameAdapterVTable game;
    void* game_userdata;
    rc_client_t* client;
    char* user_agent;
    NRA_Mode mode;
    bool game_loaded;
    bool load_pending;
    bool login_pending;
    bool persist_login_credentials;
    bool logged_in;
    bool reset_pending;
    bool destroying;
    bool destroyed;
    bool has_memory;
    NRA_MemoryView memory;
    uint8_t* outbox_blob;
    size_t outbox_size;
    uint64_t next_outbox_sequence;
    bool outbox_durable;
    bool outbox_loaded;
    bool outbox_persistence_failed;
    bool outbox_replay_in_flight;
    NRA_RequestId next_request_id;
    NRA_Request requests[NRA_MAX_IN_FLIGHT_REQUESTS];
    NRA_Completion completions[NRA_MAX_COMPLETIONS];
    size_t completion_head;
    size_t completion_count;
    pthread_mutex_t mutex;
    uint32_t unknown_event_count;
    uint64_t next_toast_sequence;
    bool achievement_list_dirty;
    NRA_UISnapshot ui;
};

void nra_handle_event(NRA_Context* context, const rc_client_event_t* event);

#endif
