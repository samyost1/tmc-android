#ifndef NATIVE_RA_H
#define NATIVE_RA_H

#include "native_ra_game_adapter.h"
#include "native_ra_platform.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct NRA_Context NRA_Context;

typedef struct NRA_CreateParams {
    uint32_t abi_version;
    const char* client_name;
    const char* client_version;
    const char* user_agent;
    const NRA_PlatformVTable* platform;
    void* platform_userdata;
    const NRA_GameAdapterVTable* game;
    void* game_userdata;
} NRA_CreateParams;

/* Every API in this header must run on the context owner thread in P1. */
NRA_Result nra_create(const NRA_CreateParams* params, NRA_Context** context);
void nra_destroy(NRA_Context* context);

NRA_Result nra_login_password(NRA_Context* context, const char* username, const char* password);
NRA_Result nra_login_token(NRA_Context* context, const char* username, const char* token);
void nra_logout(NRA_Context* context, bool delete_persisted_credentials);

NRA_Result nra_load_current_game(NRA_Context* context);
void nra_unload_game(NRA_Context* context);
void nra_do_frame(NRA_Context* context);
void nra_idle(NRA_Context* context);
void nra_notify_reset_completed(NRA_Context* context);
NRA_Result nra_request_mode(NRA_Context* context, NRA_Mode mode);
bool nra_can_pause(NRA_Context* context, uint32_t* frames_remaining);

size_t nra_progress_size(NRA_Context* context);
NRA_Result nra_serialize_progress(NRA_Context* context, uint8_t* buffer, size_t capacity, size_t* size);
NRA_Result nra_deserialize_progress(NRA_Context* context, const uint8_t* buffer, size_t size);
NRA_Result nra_reset_progress(NRA_Context* context);

NRA_Result nra_enqueue_http_completion(NRA_Context* context, const NRA_HttpCompletion* completion);
bool nra_copy_status_snapshot(NRA_Context* context, NRA_StatusSnapshot* snapshot);
bool nra_copy_ui_snapshot(NRA_Context* context, NRA_UISnapshot* snapshot);

#ifdef __cplusplus
}
#endif

#endif
