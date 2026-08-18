#ifndef TMC_RA_RUNTIME_H
#define TMC_RA_RUNTIME_H

#include "native_ra/native_ra.h"
#include "tmc_ra_adapter.h"
#include "tmc_ra_memory.h"

#include <stdbool.h>
#include <pthread.h>

typedef struct TmcRaRuntime {
    NRA_Context* context;
    TmcRaAdapter adapter;
    pthread_t owner_thread;
    bool owner_thread_set;
    bool identify_requested;
    bool identify_retry_pending;
    bool logged_in_seen;
    bool game_loaded_seen;
    unsigned identify_attempts;
    bool shutdown_started;
    TmcRaFrameView frame_view;
    bool frame_view_valid;
} TmcRaRuntime;

extern TmcRaRuntime gTmcRaRuntime;

uint64_t TmcRaRuntime_MonotonicMs(void);
bool TmcRaRuntime_Init(TmcRaRuntime* runtime, const NRA_PlatformVTable* platform, void* platform_userdata);
bool TmcRaRuntime_InitDefault(TmcRaRuntime* runtime);
bool TmcRaRuntime_Frame(TmcRaRuntime* runtime);
TmcRaFrameView TmcRaRuntime_FrameView(const TmcRaRuntime* runtime);
void TmcRaRuntime_Idle(TmcRaRuntime* runtime);
void TmcRaRuntime_ResetCompleted(TmcRaRuntime* runtime);
void TmcRaRuntime_Shutdown(TmcRaRuntime* runtime);
bool TmcRaRuntime_IsInitialized(const TmcRaRuntime* runtime);
bool TmcRaRuntime_CanRestoreSaveState(const TmcRaRuntime* runtime);
bool TmcRaRuntime_CanFastForward(const TmcRaRuntime* runtime);
bool TmcRaRuntime_CanUsePracticeControls(const TmcRaRuntime* runtime);

#ifdef TMC_RA_RUNTIME_TEST
/* Test-only admission fixture; production readiness remains adapter-owned. */
void TmcRaRuntime_TestSetCasualReady(bool ready);
#endif

#endif
