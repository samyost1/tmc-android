#include "tmc_ra_runtime.h"

#include "port_version.h"
#include "tmc_ra_memory.h"
#include "tmc_ra_policy.h"
#include "tmc_ra_ui_bridge.h"
#ifdef __ANDROID__
#include "tmc_ra_android.h"
#endif

#include <stdlib.h>
#include <string.h>
#include <time.h>

#define TMC_RA_USER_AGENT "SkyEmu/4.0"
#define TMC_RA_IDENTIFY_MAX_ATTEMPTS 2u

TmcRaRuntime gTmcRaRuntime;
static bool sAtexitRegistered;
#ifdef TMC_RA_RUNTIME_TEST
static bool sTestCasualReady;
#endif

static bool IsOwner(const TmcRaRuntime* runtime) {
    return runtime != NULL && runtime->owner_thread_set && pthread_equal(runtime->owner_thread, pthread_self());
}

uint64_t TmcRaRuntime_MonotonicMs(void) {
    struct timespec now;

    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        return 0;
    return (uint64_t)now.tv_sec * 1000u + (uint64_t)now.tv_nsec / 1000000u;
}

#ifndef __ANDROID__
static uint64_t Now(void* userdata) {
    (void)userdata;
    return TmcRaRuntime_MonotonicMs();
}
#endif

static void ShutdownAtExit(void) {
    TmcRaRuntime_Shutdown(&gTmcRaRuntime);
}

static void ResetIdentifyState(TmcRaRuntime* runtime) {
    if (runtime == NULL)
        return;
    runtime->identify_requested = false;
    runtime->identify_retry_pending = false;
    runtime->identify_attempts = 0;
}

static bool StartIdentify(TmcRaRuntime* runtime) {
    NRA_Result result;

    if (runtime == NULL || runtime->context == NULL ||
        runtime->identify_attempts >= TMC_RA_IDENTIFY_MAX_ATTEMPTS)
        return false;
    ++runtime->identify_attempts;
    result = nra_load_current_game(runtime->context);
    runtime->identify_requested = result == NRA_PENDING;
    runtime->identify_retry_pending = result != NRA_PENDING &&
        runtime->identify_attempts < TMC_RA_IDENTIFY_MAX_ATTEMPTS;
    return runtime->identify_requested;
}

typedef bool (*TmcRaPolicyPredicate)(NRA_Mode mode, bool game_loaded, bool memory_fully_validated);

static bool CanUsePolicy(const TmcRaRuntime* runtime, TmcRaPolicyPredicate predicate) {
    NRA_Mode mode = NRA_MODE_LIVE_CASUAL;
    bool game_loaded = false;
    bool memory_fully_validated = false;
    NRA_StatusSnapshot status;

    if (runtime == NULL || predicate == NULL)
        return false;
    memory_fully_validated = TmcRaAdapter_MemoryFullyValidated(&runtime->adapter);
    if (runtime->context != NULL && nra_copy_status_snapshot(runtime->context, &status)) {
        mode = status.mode;
        game_loaded = status.game_loaded;
    }
    return predicate(mode, game_loaded, memory_fully_validated);
}

static void DrainUiCommands(TmcRaRuntime* runtime) {
    TmcRaUiCommand command;

    if (runtime == NULL || runtime->context == NULL)
        return;
    while (TmcRaUiBridge_TakeCommand(&command)) {
        switch (command.kind) {
            case TMC_RA_UI_COMMAND_REQUEST_PASSWORD_LOGIN:
#ifdef __ANDROID__
                TmcRaAndroid_RequestPasswordLogin();
#endif
                break;
            case TMC_RA_UI_COMMAND_LOGOUT:
                nra_logout(runtime->context, true);
                ResetIdentifyState(runtime);
                runtime->logged_in_seen = false;
                runtime->game_loaded_seen = false;
                break;
            case TMC_RA_UI_COMMAND_REQUEST_MODE:
                {
                    NRA_StatusSnapshot status;
                    bool game_loaded = false;
                    bool memory_fully_validated = TmcRaAdapter_MemoryFullyValidated(&runtime->adapter);

                    if (nra_copy_status_snapshot(runtime->context, &status))
                        game_loaded = status.game_loaded;
                    if (TmcRaPolicy_CanAdmitMode(command.mode, game_loaded, memory_fully_validated))
                        (void)nra_request_mode(runtime->context, command.mode);
                }
                break;
            case TMC_RA_UI_COMMAND_NONE:
            default:
                break;
        }
    }
}

#ifdef TMC_RA_RUNTIME_TEST
void TmcRaRuntime_TestSetCasualReady(bool ready) {
    sTestCasualReady = ready;
}
#endif

bool TmcRaRuntime_Init(TmcRaRuntime* runtime, const NRA_PlatformVTable* platform, void* platform_userdata) {
    const NRA_CreateParams params = {
        .abi_version = NRA_ABI_VERSION,
        .client_name = "The Minish Cap PC Port",
        .client_version = TMC_PC_VERSION,
        .user_agent = TMC_RA_USER_AGENT,
        .platform = platform,
        .platform_userdata = platform_userdata,
        .game = TmcRaAdapter_VTable(),
        .game_userdata = runtime ? &runtime->adapter : NULL,
    };

    if (runtime == NULL || platform == NULL || runtime->context != NULL)
        return false;
    memset(runtime, 0, sizeof(*runtime));
    TmcRaUiBridge_Reset();
    TmcRaAdapter_Init(&runtime->adapter);
#ifdef TMC_RA_RUNTIME_TEST
    runtime->adapter.memory_fully_validated = sTestCasualReady;
#endif
    if (nra_create(&params, &runtime->context) != NRA_OK)
        return false;
    if (nra_request_mode(runtime->context, NRA_MODE_LIVE_CASUAL) != NRA_OK) {
        nra_destroy(runtime->context);
        runtime->context = NULL;
        return false;
    }
    runtime->owner_thread = pthread_self();
    runtime->owner_thread_set = true;
    return true;
}

bool TmcRaRuntime_InitDefault(TmcRaRuntime* runtime) {
#ifdef __ANDROID__
    const NRA_PlatformVTable* active_platform = TmcRaAndroid_Platform();
    void* active_userdata = TmcRaAndroid_PlatformUserdata();
#else
    static const NRA_PlatformVTable platform = {.now_ms = Now};
    const NRA_PlatformVTable* active_platform = &platform;
    void* active_userdata = NULL;
#endif

    if (!TmcRaRuntime_Init(runtime, active_platform, active_userdata))
        return false;
    if (!sAtexitRegistered && atexit(ShutdownAtExit) == 0)
        sAtexitRegistered = true;
    return true;
}

bool TmcRaRuntime_Frame(TmcRaRuntime* runtime) {
    NRA_StatusSnapshot status;
    bool retry_now;

    if (!IsOwner(runtime) || runtime->context == NULL || runtime->shutdown_started)
        return false;
    runtime->frame_view_valid = false;
#ifdef __ANDROID__
    TmcRaAndroid_Drain(runtime->context);
#endif
    DrainUiCommands(runtime);
    TmcRaMemory_ResetAudit();
    TmcRaMemory_Publish();
    TmcRaMemory_ResetRequestedCoverage();
    nra_do_frame(runtime->context);
    runtime->frame_view = TmcRaMemory_CurrentFrameView();
    runtime->frame_view_valid = true;
    (void)TmcRaUiBridge_PublishFromContext(runtime->context);
    if (!nra_copy_status_snapshot(runtime->context, &status))
        return TmcRaAdapter_TakeResetRequest(&runtime->adapter);

    retry_now = runtime->identify_retry_pending;
    runtime->identify_retry_pending = false;
    if (!status.logged_in) {
        ResetIdentifyState(runtime);
        runtime->logged_in_seen = false;
        runtime->game_loaded_seen = false;
    } else {
        if (!runtime->logged_in_seen ||
            (runtime->game_loaded_seen && !status.game_loaded))
            ResetIdentifyState(runtime);
        runtime->logged_in_seen = true;
        runtime->game_loaded_seen = status.game_loaded;

        if (status.game_loaded) {
            runtime->identify_requested = false;
            runtime->identify_retry_pending = false;
        } else if (runtime->identify_requested && !status.load_pending) {
            runtime->identify_requested = false;
            if (runtime->identify_attempts < TMC_RA_IDENTIFY_MAX_ATTEMPTS)
                runtime->identify_retry_pending = true;
        }

        if (!status.game_loaded && !status.load_pending && !status.request_pending &&
            !runtime->identify_requested &&
            runtime->identify_attempts < TMC_RA_IDENTIFY_MAX_ATTEMPTS &&
            (runtime->identify_attempts == 0 || retry_now))
            (void)StartIdentify(runtime);
    }
    return TmcRaAdapter_TakeResetRequest(&runtime->adapter);
}

TmcRaFrameView TmcRaRuntime_FrameView(const TmcRaRuntime* runtime) {
    if (runtime == NULL || !runtime->frame_view_valid)
        return (TmcRaFrameView){0};
    return runtime->frame_view;
}

void TmcRaRuntime_Idle(TmcRaRuntime* runtime) {
    if (IsOwner(runtime) && runtime->context != NULL && !runtime->shutdown_started) {
#ifdef __ANDROID__
        TmcRaAndroid_Drain(runtime->context);
#endif
        DrainUiCommands(runtime);
        nra_idle(runtime->context);
        (void)TmcRaUiBridge_PublishFromContext(runtime->context);
    }
}

void TmcRaRuntime_ResetCompleted(TmcRaRuntime* runtime) {
    if (IsOwner(runtime) && runtime->context != NULL && !runtime->shutdown_started)
        nra_notify_reset_completed(runtime->context);
}

void TmcRaRuntime_Shutdown(TmcRaRuntime* runtime) {
    if (!IsOwner(runtime) || runtime->shutdown_started)
        return;
    runtime->shutdown_started = true;
    nra_unload_game(runtime->context);
    nra_destroy(runtime->context);
    runtime->context = NULL;
    runtime->frame_view_valid = false;
    ResetIdentifyState(runtime);
    runtime->logged_in_seen = false;
    runtime->game_loaded_seen = false;
    TmcRaUiBridge_Reset();
}

bool TmcRaRuntime_IsInitialized(const TmcRaRuntime* runtime) {
    return runtime != NULL && runtime->context != NULL && !runtime->shutdown_started;
}

bool TmcRaRuntime_CanRestoreSaveState(const TmcRaRuntime* runtime) {
    return CanUsePolicy(runtime, TmcRaPolicy_CanRestoreSaveState);
}

bool TmcRaRuntime_CanFastForward(const TmcRaRuntime* runtime) {
    return CanUsePolicy(runtime, TmcRaPolicy_CanFastForward);
}

bool TmcRaRuntime_CanUsePracticeControls(const TmcRaRuntime* runtime) {
    return CanUsePolicy(runtime, TmcRaPolicy_CanUsePracticeControls);
}
