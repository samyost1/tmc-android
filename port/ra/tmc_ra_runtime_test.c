#define main tmc_ra_memory_test_main
#include "tmc_ra_memory_test.c"
#undef main

#include "tmc_ra_runtime.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

typedef struct {
    TmcRaRuntime* runtime;
    unsigned requests;
    unsigned failed_game_loads;
    unsigned shutdowns;
    uint32_t generation_on_game_request;
    char user_agent[32];
} RuntimeFixture;

static const char kLoginResponse[] = "{\"Success\":true,\"User\":\"user\",\"Token\":\"token\"}";

const char* Port_GetLoadedRomPath(void) {
    return "synthetic.gba";
}

static uint64_t Now(void* userdata) {
    return ((RuntimeFixture*)userdata)->requests;
}

static void Begin(void* userdata, NRA_RequestId id, const NRA_HttpRequest* request) {
    RuntimeFixture* fixture = userdata;
    NRA_HttpCompletion completion = {
        .request_id = id,
        .http_status_code = 200,
        .body = (const uint8_t*)kLoginResponse,
        .body_size = sizeof(kLoginResponse) - 1,
    };
    snprintf(fixture->user_agent, sizeof(fixture->user_agent), "%s",
             request != NULL && request->user_agent != NULL ? request->user_agent : "");
    ++fixture->requests;
    if (fixture->requests == 1)
        (void)nra_enqueue_http_completion(fixture->runtime->context, &completion);
    else if (fixture->requests >= 2 && fixture->failed_game_loads != 0) {
        completion.body = (const uint8_t*)"{}";
        completion.body_size = 2;
        --fixture->failed_game_loads;
        (void)nra_enqueue_http_completion(fixture->runtime->context, &completion);
    } else if (fixture->requests == 2)
        fixture->generation_on_game_request = TmcRaMemory_Current().generation;
}

static void ShutdownHttp(void* userdata) {
    ++((RuntimeFixture*)userdata)->shutdowns;
}

static void* ForeignLifecycle(void* userdata) {
    TmcRaRuntime* runtime = userdata;

    if (TmcRaRuntime_Frame(runtime))
        return (void*)1;
    TmcRaRuntime_Idle(runtime);
    TmcRaRuntime_ResetCompleted(runtime);
    TmcRaRuntime_Shutdown(runtime);
    return NULL;
}

int main(void) {
    RuntimeFixture fixture = {0};
    RuntimeFixture gate_fixture = {0};
    TmcRaRuntime runtime = {0};
    TmcRaRuntime gate_runtime = {0};
    pthread_t thread;
    void* result = NULL;
    uint8_t rom[] = { 0x12, 0x34, 0x56, 0x78 };
    uint32_t generation_before_frame;
    uint32_t generation;
    TmcRaFrameView frame_view;
    uint64_t monotonic_before;
    const struct timespec delay = {.tv_nsec = 20000000L};
    const NRA_PlatformVTable platform = {
        .now_ms = Now,
        .http_begin = Begin,
        .http_shutdown = ShutdownHttp,
    };

    if (tmc_ra_memory_test_main() != 0)
        return 1;
    monotonic_before = TmcRaRuntime_MonotonicMs();
    if (nanosleep(&delay, NULL) != 0 || TmcRaRuntime_MonotonicMs() < monotonic_before + 10)
        return 1;

    /* No parity admission means Casual initialization must fail closed. */
    TmcRaRuntime_TestSetCasualReady(false);
    if (TmcRaRuntime_Init(&gate_runtime, &platform, &gate_fixture) ||
        TmcRaRuntime_IsInitialized(&gate_runtime) ||
        gate_runtime.context != NULL ||
        gate_runtime.owner_thread_set ||
        gate_fixture.shutdowns != 1)
        return 1;

    TmcRaRuntime_TestSetCasualReady(true);
    gRomData = rom;
    gRomSize = sizeof(rom);
    fixture.runtime = &runtime;
    if (!TmcRaRuntime_Init(&runtime, &platform, &fixture) || !TmcRaRuntime_IsInitialized(&runtime))
        return 1;
    if (nra_login_password(runtime.context, "user", "password") != NRA_PENDING)
        return 1;
    generation_before_frame = TmcRaMemory_Current().generation;
    if (pthread_create(&thread, NULL, ForeignLifecycle, &runtime) != 0)
        return 1;
    pthread_join(thread, &result);
    if (result != NULL || !TmcRaRuntime_IsInitialized(&runtime) || fixture.requests != 1 ||
        strcmp(fixture.user_agent, "SkyEmu/4.0") != 0)
        return 1;
    TmcRaMemory_ResetRequestedCoverage();
    (void)TmcRaMemory_Read(0x0010, NULL);
    TmcRaMemory_TestResetCoverageResetCount();
    if (TmcRaRuntime_Frame(&runtime) || fixture.requests != 2
        || fixture.generation_on_game_request != generation_before_frame + 1 ||
        TmcRaMemory_TestCoverageResetCount() != 1) {
        fprintf(stderr, "runtime frame contract failed: reset_count=%u requests=%u generation=%u expected_generation=%u\n",
                TmcRaMemory_TestCoverageResetCount(), fixture.requests,
                fixture.generation_on_game_request, generation_before_frame + 1);
        return 1;
    }
    frame_view = TmcRaRuntime_FrameView(&runtime);
    if (frame_view.snapshot.generation != TmcRaMemory_Current().generation ||
        frame_view.coverage.selected_bytes != 0 ||
        frame_view.audit.requested_bytes != 0) {
        fprintf(stderr, "runtime frame view failed: view_generation=%u current_generation=%u coverage=%u audit_bytes=%u\n",
                frame_view.snapshot.generation, TmcRaMemory_Current().generation,
                frame_view.coverage.selected_bytes, frame_view.audit.requested_bytes);
        return 1;
    }
    generation = TmcRaMemory_Current().generation;
    if (TmcRaRuntime_Frame(&runtime) || TmcRaMemory_Current().generation != generation + 1 ||
        TmcRaMemory_TestCoverageResetCount() != 2) {
        fprintf(stderr, "second runtime frame contract failed: reset_count=%u current_generation=%u expected_generation=%u\n",
                TmcRaMemory_TestCoverageResetCount(), TmcRaMemory_Current().generation,
                generation + 1);
        return 1;
    }
    TmcRaRuntime_ResetCompleted(&runtime);
    TmcRaRuntime_Shutdown(&runtime);
    if (TmcRaRuntime_IsInitialized(&runtime) || fixture.shutdowns != 1)
        return 1;
    {
        RuntimeFixture retry_fixture = {.failed_game_loads = 2};
        TmcRaRuntime retry_runtime = {0};

        retry_fixture.runtime = &retry_runtime;
        if (!TmcRaRuntime_Init(&retry_runtime, &platform, &retry_fixture) ||
            nra_login_password(retry_runtime.context, "user", "password") != NRA_PENDING ||
            TmcRaRuntime_Frame(&retry_runtime) || retry_fixture.requests != 2 ||
            !retry_runtime.identify_requested ||
            TmcRaRuntime_Frame(&retry_runtime) || retry_fixture.requests != 2 ||
            retry_runtime.identify_requested || !retry_runtime.identify_retry_pending ||
            TmcRaRuntime_Frame(&retry_runtime) || retry_fixture.requests != 3 ||
            !retry_runtime.identify_requested ||
            TmcRaRuntime_Frame(&retry_runtime) || retry_fixture.requests != 3 ||
            retry_runtime.identify_requested || retry_runtime.identify_retry_pending ||
            retry_runtime.identify_attempts != 2 ||
            TmcRaRuntime_Frame(&retry_runtime) || retry_fixture.requests != 3) {
            TmcRaRuntime_Shutdown(&retry_runtime);
            return 1;
        }
        TmcRaRuntime_Shutdown(&retry_runtime);
    }
    if (!TmcRaRuntime_InitDefault(&runtime) || !TmcRaRuntime_IsInitialized(&runtime))
        return 1;
    TmcRaRuntime_Idle(&runtime);
    TmcRaRuntime_Shutdown(&runtime);
    if (TmcRaRuntime_IsInitialized(&runtime))
        return 1;
    if (!TmcRaRuntime_InitDefault(&gTmcRaRuntime) || !TmcRaRuntime_IsInitialized(&gTmcRaRuntime))
        return 1;
    puts("tmc_ra_runtime_test: ALL PASS");
    return 0;
}
