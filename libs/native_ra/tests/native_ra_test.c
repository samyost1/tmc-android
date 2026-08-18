#include "native_ra/native_ra.h"
#include "native_ra_internal.h"
#include "../src/native_ra_outbox_journal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdatomic.h>

#define FIXTURE_BYTES 32768u

typedef struct Mock {
    NRA_Context* context;
    NRA_RequestId ids[64];
    char posts[64][256];
    char user_agents[64][64];
    uint8_t* rom;
    uint8_t memory[32];
    size_t rom_size;
    int begins, cancels, shutdowns, stores, store_attempts, store_fail_attempt, deletes;
    uint8_t* secure_blob;
    size_t secure_size;
    int secure_gets, secure_sets, secure_deletes;
    char stored_keys[2][32];
    uint8_t stored_values[2][16];
    size_t stored_sizes[2];
    NRA_Result inline_result;
    uint64_t now_ms;
    int builds, releases, loads, unloads, resets, snapshot_reads;
    bool processable, validated, accept_game, validation_reader, allow_live_mode;
    bool gate_saw_game_loaded;
    bool inline_completion;
} Mock;

static const char login_ok[] = "{\"Success\":true,\"User\":\"user\",\"Token\":\"token\"}";
static const char patch[] = "{\"Success\":true,\"GameId\":1234,\"Title\":\"Synthetic\",\"ConsoleId\":5,"
    "\"ImageIconUrl\":\"http://server/Images/112233.png\",\"RichPresenceGameId\":1234,\"RichPresencePatch\":\"Display:\\nSynthetic presence\",\"Sets\":[{"
    "\"AchievementSetId\":1111,\"GameId\":1234,\"Title\":null,\"Type\":\"core\",\"ImageIconUrl\":\"http://server/Images/112233.png\","
    "\"Achievements\":[{\"ID\":5501,\"Title\":\"Read\",\"Description\":\"Read\",\"Flags\":3,\"Points\":5,"
    "\"MemAddr\":\"0xH0001=3_0xH0002=7\",\"Author\":\"test\",\"BadgeName\":\"00234\",\"Created\":1367266583,\"Modified\":1376929305},"
    "{\"ID\":5502,\"Title\":\"Read2\",\"Description\":\"Read2\",\"Flags\":3,\"Points\":2,"
    "\"MemAddr\":\"0xH0001=2_0x0002=9\",\"Author\":\"test\",\"BadgeName\":\"00235\",\"Created\":1376970283,\"Modified\":1376970283}],"
    "\"Leaderboards\":[]}]}";
static const char session_ok[] = "{\"Success\":true,\"Unlocks\":[],\"HardcoreUnlocks\":[]}";
static const char award_ok[] = "{\"Success\":true,\"Score\":1,\"SoftcoreScore\":1,\"AchievementID\":5501,\"AchievementsRemaining\":0}";

/* Pinned rcheevos test/rhash/data.c generate_generic_file pattern, generated at runtime. */
static uint8_t* generate_generic_file(size_t size) {
    uint8_t* image = calloc(size, 1);
    uint32_t seed = (uint32_t)(size ^ (size >> 8) ^ ((size - 1) * 25387));

    if (image != NULL) {
        for (size_t i = 0; i < size;) {
            int count;
            uint8_t value;
            switch (seed & 0xff) {
            case 0: count = (((seed >> 8) & 0x3f) & ~(size - i & 0x0f)); if (!count) count = 1; value = 0; break;
            case 1: count = ((seed >> 8) & 7) + 1; value = (uint8_t)(seed >> 16); break;
            case 2: count = ((seed >> 8) & 3) + 1; value = (uint8_t)(seed >> 16) ^ 0xff; break;
            case 3: count = ((seed >> 8) & 3) + 1; value = (uint8_t)(seed >> 16) ^ 0xa5; break;
            case 4: count = ((seed >> 8) & 3) + 1; value = (uint8_t)(seed >> 16) ^ 0xc3; break;
            case 5: count = ((seed >> 8) & 3) + 1; value = (uint8_t)(seed >> 16) ^ 0x96; break;
            case 6: case 7: count = ((seed >> 8) & 3) + 1; value = (uint8_t)(seed >> 16) ^ 0x78; break;
            default: count = 1; value = (uint8_t)((seed >> 8) ^ (seed >> 16)); break;
            }
            while (i < size && count--) image[i++] = value;
            seed = (seed * UINT32_C(0x41c64e6d) + UINT32_C(12345)) & UINT32_C(0x7fffffff);
        }
    }
    return image;
}

static uint64_t now(void* userdata) { return ++((Mock*)userdata)->now_ms; }
static void begin(void* userdata, NRA_RequestId id, const NRA_HttpRequest* request) {
    Mock* mock = userdata;
    mock->ids[mock->begins] = id;
    snprintf(mock->posts[mock->begins], sizeof(mock->posts[0]), "%s", request->post_data ? request->post_data : "");
    snprintf(mock->user_agents[mock->begins++], sizeof(mock->user_agents[0]), "%s", request->user_agent);
    if (mock->inline_completion) {
        NRA_HttpCompletion completion = {.request_id = id, .http_status_code = 200,
            .body = (const uint8_t*)login_ok, .body_size = strlen(login_ok)};
        mock->inline_result = nra_enqueue_http_completion(mock->context, &completion);
    }
}
static void cancel(void* userdata, NRA_RequestId id) { (void)id; ++((Mock*)userdata)->cancels; }
static void shutdown_http(void* userdata) { ++((Mock*)userdata)->shutdowns; }
static NRA_Result store(void* userdata, const char* key, const uint8_t* value, size_t size) {
    Mock* mock = userdata;
    int attempt = mock->store_attempts++;
    if (mock->store_fail_attempt == attempt + 1) return NRA_IO_ERROR;
    if (mock->stores < 2 && size <= sizeof(mock->stored_values[0])) {
        snprintf(mock->stored_keys[mock->stores], sizeof(mock->stored_keys[0]), "%s", key);
        memcpy(mock->stored_values[mock->stores], value, size);
        mock->stored_sizes[mock->stores] = size;
    }
    ++mock->stores;
    return NRA_OK;
}
static NRA_Result erase(void* userdata, const char* key) { (void)key; ++((Mock*)userdata)->deletes; return NRA_OK; }
static NRA_Result secure_get(void* userdata, const char* key, uint8_t* value, size_t capacity, size_t* size) {
    Mock* mock = userdata;

    if (key == NULL || strcmp(key, "native_ra.outbox") != 0 || size == NULL) return NRA_INVALID_ARGUMENT;
    ++mock->secure_gets;
    *size = mock->secure_size;
    if (mock->secure_size != 0) {
        if (value == NULL || capacity < mock->secure_size) {
            *size = 0;
            return NRA_INVALID_ARGUMENT;
        }
        memcpy(value, mock->secure_blob, mock->secure_size);
    }
    return NRA_OK;
}
static NRA_Result secure_set(void* userdata, const char* key, const uint8_t* value, size_t size) {
    Mock* mock = userdata;
    uint8_t* replacement;

    if (key == NULL || strcmp(key, "native_ra.outbox") != 0 || value == NULL ||
        size == 0 || size > NRA_OUTBOX_JOURNAL_MAX_TOTAL_BYTES) return NRA_INVALID_ARGUMENT;
    replacement = malloc(size);
    if (replacement == NULL) return NRA_INTERNAL_ERROR;
    memcpy(replacement, value, size);
    if (mock->secure_blob != NULL) memset(mock->secure_blob, 0, mock->secure_size);
    free(mock->secure_blob);
    mock->secure_blob = replacement;
    mock->secure_size = size;
    ++mock->secure_sets;
    return NRA_OK;
}
static NRA_Result secure_delete(void* userdata, const char* key) {
    Mock* mock = userdata;

    if (key == NULL || strcmp(key, "native_ra.outbox") != 0) return NRA_INVALID_ARGUMENT;
    if (mock->secure_blob != NULL) memset(mock->secure_blob, 0, mock->secure_size);
    free(mock->secure_blob);
    mock->secure_blob = NULL;
    mock->secure_size = 0;
    ++mock->secure_deletes;
    return NRA_OK;
}
static int seed_mixed_outbox(Mock* mock) {
    static const uint8_t other_account[] = "other";
    static const uint8_t user_account[] = "user";
    static const uint8_t url[] = "https://retroachievements.org/dorequest.php";
    static const uint8_t content_type[] = "application/x-www-form-urlencoded";
    static const uint8_t other_post[] = "r=awardachievement&u=other&a=5501";
    static const uint8_t user_post_one[] = "r=awardachievement&u=user&a=5501";
    static const uint8_t user_post_two[] = "r=awardachievement&u=user&a=5502";
    static const uint8_t other_key[NRA_OUTBOX_JOURNAL_DEDUPE_KEY_SIZE] = {1};
    static const uint8_t user_key_one[NRA_OUTBOX_JOURNAL_DEDUPE_KEY_SIZE] = {2};
    static const uint8_t user_key_two[NRA_OUTBOX_JOURNAL_DEDUPE_KEY_SIZE] = {3};
    NRA_OutboxJournalRecord records[3] = {
        {.sequence = 1, .game_id = 1234, .console_id = 5, .kind = NRA_OUTBOX_ACHIEVEMENT,
            .status = NRA_OUTBOX_JOURNAL_PENDING, .account = other_account,
            .account_size = sizeof(other_account) - 1, .dedupe_key = other_key,
            .dedupe_key_size = sizeof(other_key), .url = url, .url_size = sizeof(url) - 1,
            .content_type = content_type, .content_type_size = sizeof(content_type) - 1,
            .post = other_post, .post_size = sizeof(other_post) - 1},
        {.sequence = 2, .game_id = 1234, .console_id = 5, .kind = NRA_OUTBOX_ACHIEVEMENT,
            .status = NRA_OUTBOX_JOURNAL_PENDING, .account = user_account,
            .account_size = sizeof(user_account) - 1, .dedupe_key = user_key_one,
            .dedupe_key_size = sizeof(user_key_one), .url = url, .url_size = sizeof(url) - 1,
            .content_type = content_type, .content_type_size = sizeof(content_type) - 1,
            .post = user_post_one, .post_size = sizeof(user_post_one) - 1},
        {.sequence = 3, .game_id = 1234, .console_id = 5, .kind = NRA_OUTBOX_ACHIEVEMENT,
            .status = NRA_OUTBOX_JOURNAL_PENDING, .account = user_account,
            .account_size = sizeof(user_account) - 1, .dedupe_key = user_key_two,
            .dedupe_key_size = sizeof(user_key_two), .url = url, .url_size = sizeof(url) - 1,
            .content_type = content_type, .content_type_size = sizeof(content_type) - 1,
            .post = user_post_two, .post_size = sizeof(user_post_two) - 1}
    };
    uint8_t* journal;
    size_t journal_size = 0;
    int result;

    journal = malloc(NRA_OUTBOX_JOURNAL_MAX_TOTAL_BYTES);
    if (journal == NULL) return 0;
    result = nra_outbox_journal_encode(journal, NRA_OUTBOX_JOURNAL_MAX_TOTAL_BYTES,
                                       records, sizeof(records) / sizeof(records[0]), &journal_size);
    if (result == NRA_OUTBOX_JOURNAL_OK)
        result = secure_set(mock, "native_ra.outbox", journal, journal_size);
    memset(journal, 0, NRA_OUTBOX_JOURNAL_MAX_TOTAL_BYTES);
    free(journal);
    return result == NRA_OK;
}
static NRA_Result rom(void* userdata, NRA_RomView* view) {
    Mock* mock = userdata;
    view->path_hint = "synthetic.gba";
    view->data = mock->rom;
    view->size = mock->rom_size;
    view->console_id = 5;
    return NRA_OK;
}
static NRA_Result memory(void* userdata, NRA_MemoryView* view) {
    Mock* mock = userdata;
    ++mock->builds;
    view->data = mock->memory;
    view->size = sizeof(mock->memory);
    view->fully_validated = mock->validated;
    return NRA_OK;
}
static bool read_memory_snapshot(void* userdata, uint32_t address, uint8_t* buffer, uint32_t count) {
    Mock* mock = userdata;
    ++mock->snapshot_reads;
    if (!mock->validation_reader || address > sizeof(mock->memory) || count > sizeof(mock->memory) - address)
        return false;
    memcpy(buffer, mock->memory + address, count);
    return true;
}
static void release(void* userdata, const NRA_MemoryView* view) { (void)view; ++((Mock*)userdata)->releases; }
static bool processable(void* userdata) { return ((Mock*)userdata)->processable; }
static bool accept_game(void* userdata, uint32_t id, uint32_t console_id) {
    Mock* mock = userdata;
    return mock->accept_game && id == 1234 && console_id == 5;
}
static void loaded(void* userdata, uint32_t id) { if (id == 1234) ++((Mock*)userdata)->loads; }
static void unloaded(void* userdata) { ++((Mock*)userdata)->unloads; }
static void reset(void* userdata, uint32_t reason) { if (reason) ++((Mock*)userdata)->resets; }
static bool admit_mode(void* userdata, NRA_Mode requested_mode, bool game_loaded) {
    Mock* mock = userdata;

    mock->gate_saw_game_loaded = game_loaded;
    return !game_loaded && (requested_mode == NRA_MODE_SPECTATOR ||
        (requested_mode == NRA_MODE_LIVE_CASUAL && mock->allow_live_mode));
}
static NRA_Result complete(Mock* mock, NRA_RequestId id, const char* body, int status, bool retryable) {
    NRA_HttpCompletion completion = {.request_id = id, .http_status_code = status, .body = (const uint8_t*)body,
        .body_size = body ? strlen(body) : 0, .retryable = retryable};
    return nra_enqueue_http_completion(mock->context, &completion);
}
static void drain(Mock* mock) { nra_idle(mock->context); nra_idle(mock->context); }
static int route(Mock* mock, const char* needle, const char* response) {
    int request = mock->begins - 1;
    return request >= 0 && strstr(mock->posts[request], needle) &&
        complete(mock, mock->ids[request], response, 200, false) == NRA_OK;
}
static int load(Mock* mock, bool session) {
    if (nra_load_current_game(mock->context) != NRA_PENDING) return 0;
    if (!strstr(mock->posts[mock->begins - 1], "r=achievementsets") ||
        strstr(mock->posts[mock->begins - 1], "r=gameid")) {
        return 0;
    }
    if (!route(mock, "r=achievementsets", patch)) return 0;
    nra_do_frame(mock->context);
    if (session && (!route(mock, "r=startsession", session_ok))) return 0;
    if (session) nra_do_frame(mock->context);
    return 1;
}

static int queue_limits(Mock* mock, const NRA_CreateParams* params) {
    NRA_Context* context = NULL;
    NRA_HttpCompletion completion = {.body = (const uint8_t*)"{}", .body_size = 2};
    size_t i;

    if (nra_create(params, &context) != NRA_OK) return 0;
    pthread_mutex_lock(&context->mutex);
    for (i = 0; i < NRA_MAX_IN_FLIGHT_REQUESTS; ++i) {
        context->requests[i].used = true;
        context->requests[i].id = 100 + i;
    }
    pthread_mutex_unlock(&context->mutex);
    completion.request_id = 999;
    if (nra_enqueue_http_completion(context, &completion) != NRA_INVALID_STATE) goto done;
    for (i = 0; i < NRA_MAX_IN_FLIGHT_REQUESTS; ++i) {
        completion.request_id = 100 + i;
        if (nra_enqueue_http_completion(context, &completion) != NRA_OK) goto done;
    }
    completion.request_id = 100;
    if (nra_enqueue_http_completion(context, &completion) != NRA_DISABLED) goto done;
    if (nra_login_password(context, "user", "password") != NRA_INTERNAL_ERROR ||
        mock->begins != 0) goto done;
    nra_destroy(context);
    return 1;
done:
    nra_destroy(context);
    return 0;
}

static int completion_edges(Mock* mock, const NRA_CreateParams* params) {
    NRA_StatusSnapshot status;
    NRA_HttpCompletion completion;

    if (nra_create(params, &mock->context) != NRA_OK ||
        nra_login_password(mock->context, "user", "password") != NRA_PENDING) goto fail;
    completion = (NRA_HttpCompletion){.request_id = mock->ids[0], .body = (const uint8_t*)"x",
        .body_size = NRA_MAX_RESPONSE_BYTES + 1};
    if (nra_enqueue_http_completion(mock->context, &completion) != NRA_INVALID_ARGUMENT) goto fail;
    completion.request_id++;
    completion.body_size = 1;
    if (nra_enqueue_http_completion(mock->context, &completion) != NRA_INVALID_STATE) goto fail;
    completion.request_id = mock->ids[0];
    completion.body = (const uint8_t*)login_ok;
    completion.body_size = strlen(login_ok);
    if (nra_enqueue_http_completion(mock->context, &completion) != NRA_OK ||
        nra_enqueue_http_completion(mock->context, &completion) != NRA_INVALID_STATE) goto fail;
    drain(mock);
    if (!nra_copy_status_snapshot(mock->context, &status) || !status.logged_in || status.request_pending) goto fail;
    nra_destroy(mock->context);
    mock->context = NULL;
    return mock->shutdowns == 1;
fail:
    if (mock->context != NULL) nra_destroy(mock->context);
    mock->context = NULL;
    return 0;
}

static int malformed_and_retry(Mock* mock, const NRA_CreateParams* params) {
    NRA_StatusSnapshot status;

    if (nra_create(params, &mock->context) != NRA_OK ||
        nra_login_password(mock->context, "user", "password") != NRA_PENDING ||
        !route(mock, "r=login2", "{")) goto fail;
    drain(mock);
    if (!nra_copy_status_snapshot(mock->context, &status) || status.logged_in || status.request_pending ||
        nra_login_password(mock->context, "user", "password") != NRA_PENDING ||
        complete(mock, mock->ids[1], login_ok, 200, true) != NRA_OK) goto fail;
    drain(mock);
    if (!nra_copy_status_snapshot(mock->context, &status) || status.logged_in || status.request_pending) goto fail;
    nra_destroy(mock->context);
    mock->context = NULL;
    return mock->shutdowns == 1;
fail:
    if (mock->context != NULL) nra_destroy(mock->context);
    mock->context = NULL;
    return 0;
}

static int frame_drains_login_completion(const NRA_CreateParams* params) {
    Mock mock = {.inline_completion = true};
    NRA_CreateParams local_params = *params;
    NRA_StatusSnapshot status;

    local_params.platform_userdata = &mock;
    local_params.game_userdata = &mock;
    if (nra_create(&local_params, &mock.context) != NRA_OK ||
        nra_login_password(mock.context, "user", "password") != NRA_PENDING ||
        mock.inline_result != NRA_OK) goto fail;
    nra_do_frame(mock.context);
    if (!nra_copy_status_snapshot(mock.context, &status) || !status.logged_in || status.request_pending ||
        mock.stores != 2 || mock.builds != 0) goto fail;
    nra_destroy(mock.context);
    return mock.shutdowns == 1;
fail:
    if (mock.context != NULL) nra_destroy(mock.context);
    return 0;
}

static int credential_persistence_failure(const NRA_CreateParams* params) {
    Mock mock = {.store_fail_attempt = 2};
    NRA_CreateParams local_params = *params;
    NRA_UISnapshot ui;

    local_params.platform_userdata = &mock;
    local_params.game_userdata = &mock;
    if (nra_create(&local_params, &mock.context) != NRA_OK ||
        nra_login_password(mock.context, "user", "password") != NRA_PENDING ||
        !route(&mock, "r=login2", login_ok)) goto fail;
    drain(&mock);
    if (!nra_copy_ui_snapshot(mock.context, &ui) || !ui.logged_in || !ui.has_error ||
        strcmp(ui.error, "RetroAchievements credentials were not saved") ||
        mock.stores != 1 || mock.store_attempts != 2 || mock.deletes != 2) goto fail;
    nra_destroy(mock.context);
    return mock.shutdowns == 1;
fail:
    if (mock.context != NULL) nra_destroy(mock.context);
    return 0;
}

static int rejected_stored_token_clears_credentials(const NRA_CreateParams* params) {
    Mock mock = {0};
    NRA_CreateParams local_params = *params;
    NRA_StatusSnapshot status;

    local_params.platform_userdata = &mock;
    local_params.game_userdata = &mock;
    if (nra_create(&local_params, &mock.context) != NRA_OK ||
        nra_login_token(mock.context, "user", "token") != NRA_PENDING ||
        !route(&mock, "r=login2", "{}")) goto fail;
    drain(&mock);
    if (!nra_copy_status_snapshot(mock.context, &status) || status.logged_in ||
        status.request_pending || mock.stores != 0 || mock.deletes != 2) goto fail;
    nra_destroy(mock.context);
    return mock.shutdowns == 1;
fail:
    if (mock.context != NULL) nra_destroy(mock.context);
    return 0;
}

static int token_failure_unloads_game(const NRA_CreateParams* params) {
    Mock mock = {.processable = true, .validated = true, .accept_game = true};
    NRA_CreateParams local_params = *params;
    NRA_StatusSnapshot status;

    local_params.platform_userdata = &mock;
    local_params.game_userdata = &mock;
    mock.rom = generate_generic_file(FIXTURE_BYTES);
    mock.rom_size = FIXTURE_BYTES;
    if (mock.rom == NULL ||
        nra_create(&local_params, &mock.context) != NRA_OK ||
        nra_request_mode(mock.context, NRA_MODE_SPECTATOR) != NRA_OK ||
        nra_login_password(mock.context, "user", "password") != NRA_PENDING ||
        !route(&mock, "r=login2", login_ok)) goto fail;
    drain(&mock);
    if (!load(&mock, false) ||
        !nra_copy_status_snapshot(mock.context, &status) || !status.logged_in ||
        !status.game_loaded || status.load_pending ||
        rc_client_get_game_info(mock.context->client) == NULL) goto fail;
    if (nra_login_token(mock.context, "user", "token") != NRA_PENDING ||
        !route(&mock, "r=login2", "{}")) goto fail;
    drain(&mock);
    if (!nra_copy_status_snapshot(mock.context, &status) || status.logged_in ||
        status.game_loaded || status.load_pending || status.request_pending ||
        rc_client_get_game_info(mock.context->client) != NULL || mock.unloads != 1) goto fail;
    if (nra_login_token(mock.context, "user", "token") != NRA_PENDING ||
        !route(&mock, "r=login2", login_ok)) goto fail;
    drain(&mock);
    if (!nra_copy_status_snapshot(mock.context, &status) || !status.logged_in ||
        status.game_loaded || status.load_pending || status.request_pending ||
        !load(&mock, false) ||
        !nra_copy_status_snapshot(mock.context, &status) || !status.game_loaded ||
        !status.logged_in || status.load_pending || mock.loads != 2) goto fail;
    if (nra_login_password(mock.context, "user", "password") != NRA_PENDING ||
        !route(&mock, "r=login2", "{}")) goto fail;
    drain(&mock);
    if (!nra_copy_status_snapshot(mock.context, &status) || status.logged_in ||
        status.game_loaded || status.load_pending || status.request_pending ||
        rc_client_get_game_info(mock.context->client) != NULL || mock.unloads != 2) goto fail;
    if (nra_login_password(mock.context, "user", "password") != NRA_PENDING ||
        !route(&mock, "r=login2", login_ok)) goto fail;
    drain(&mock);
    if (!nra_copy_status_snapshot(mock.context, &status) || !status.logged_in ||
        status.game_loaded || status.load_pending || status.request_pending ||
        !load(&mock, false) ||
        !nra_copy_status_snapshot(mock.context, &status) || !status.game_loaded ||
        !status.logged_in || status.load_pending || mock.loads != 3) goto fail;
    nra_logout(mock.context, false);
    if (!nra_copy_status_snapshot(mock.context, &status) || status.logged_in ||
        status.game_loaded || status.load_pending ||
        rc_client_get_game_info(mock.context->client) != NULL || mock.unloads != 3) goto fail;
    nra_destroy(mock.context);
    free(mock.rom);
    return 1;
fail:
    if (mock.context != NULL) nra_destroy(mock.context);
    free(mock.rom);
    return 0;
}

typedef struct StressMock {
    NRA_Context* context;
    pthread_t worker;
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    NRA_RequestId id;
    int cancels;
    int shutdowns;
    int accepted;
    int rejected;
    int attempts;
    bool started;
    bool worker_created;
    bool release_worker;
    bool stop;
} StressMock;

static void* stress_worker(void* userdata) {
    StressMock* mock = userdata;
    NRA_HttpCompletion completion;
    int i;

    pthread_mutex_lock(&mock->mutex);
    mock->started = true;
    pthread_cond_signal(&mock->condition);
    while (!mock->release_worker) pthread_cond_wait(&mock->condition, &mock->mutex);
    pthread_mutex_unlock(&mock->mutex);
    completion = (NRA_HttpCompletion){.request_id = mock->id, .http_status_code = 200,
        .body = (const uint8_t*)login_ok, .body_size = strlen(login_ok)};
    for (i = 0; i < 128; ++i) {
        NRA_Result result;

        pthread_mutex_lock(&mock->mutex);
        if (mock->stop) {
            pthread_mutex_unlock(&mock->mutex);
            break;
        }
        pthread_mutex_unlock(&mock->mutex);
        result = nra_enqueue_http_completion(mock->context, &completion);
        pthread_mutex_lock(&mock->mutex);
        if (result == NRA_OK) ++mock->accepted;
        else if (result == NRA_INVALID_STATE || result == NRA_DISABLED) ++mock->rejected;
        ++mock->attempts;
        pthread_cond_signal(&mock->condition);
        pthread_mutex_unlock(&mock->mutex);
    }
    return NULL;
}

static void stress_begin(void* userdata, NRA_RequestId id, const NRA_HttpRequest* request) {
    StressMock* mock = userdata;
    int result;
    (void)request;

    pthread_mutex_lock(&mock->mutex);
    mock->id = id;
    pthread_mutex_unlock(&mock->mutex);
    result = pthread_create(&mock->worker, NULL, stress_worker, mock);
    pthread_mutex_lock(&mock->mutex);
    mock->worker_created = result == 0;
    if (!mock->worker_created) {
        mock->started = true;
        pthread_cond_signal(&mock->condition);
    }
    while (!mock->started) pthread_cond_wait(&mock->condition, &mock->mutex);
    pthread_mutex_unlock(&mock->mutex);
}

static void stress_cancel(void* userdata, NRA_RequestId id) {
    StressMock* mock = userdata;
    (void)id;
    pthread_mutex_lock(&mock->mutex);
    ++mock->cancels;
    pthread_mutex_unlock(&mock->mutex);
}

static void stress_shutdown(void* userdata) {
    StressMock* mock = userdata;

    pthread_mutex_lock(&mock->mutex);
    mock->stop = true;
    mock->release_worker = true;
    pthread_cond_signal(&mock->condition);
    pthread_mutex_unlock(&mock->mutex);
    if (mock->worker_created) (void)pthread_join(mock->worker, NULL);
    pthread_mutex_lock(&mock->mutex);
    ++mock->shutdowns;
    pthread_mutex_unlock(&mock->mutex);
}

static int pthread_completion_stress(const NRA_CreateParams* params) {
    size_t cycle;

    for (cycle = 0; cycle < 64; ++cycle) {
        StressMock mock = {.mutex = PTHREAD_MUTEX_INITIALIZER, .condition = PTHREAD_COND_INITIALIZER};
        NRA_CreateParams local_params = *params;
        NRA_StatusSnapshot status;
        size_t i;

        local_params.platform = &(NRA_PlatformVTable){.http_begin = stress_begin, .http_cancel = stress_cancel,
            .http_shutdown = stress_shutdown};
        local_params.platform_userdata = &mock;
        if (nra_create(&local_params, &mock.context) != NRA_OK ||
            nra_login_password(mock.context, "user", "password") != NRA_PENDING) goto fail;
        pthread_mutex_lock(&mock.mutex);
        if (!mock.worker_created) {
            pthread_mutex_unlock(&mock.mutex);
            goto fail;
        }
        mock.release_worker = true;
        pthread_cond_signal(&mock.condition);
        while (mock.attempts == 0) pthread_cond_wait(&mock.condition, &mock.mutex);
        pthread_mutex_unlock(&mock.mutex);
        for (i = 0; i < 16; ++i) {
            if (!nra_copy_status_snapshot(mock.context, &status)) goto fail;
            nra_idle(mock.context);
        }
        nra_logout(mock.context, false);
        nra_destroy(mock.context);
        pthread_mutex_lock(&mock.mutex);
        if (mock.accepted > 1 || mock.shutdowns != 1 || mock.cancels > 1 ||
            mock.accepted + mock.rejected == 0) {
            fprintf(stderr, "stress cycle=%zu accepted=%d rejected=%d cancels=%d shutdowns=%d\n",
                cycle, mock.accepted, mock.rejected, mock.cancels, mock.shutdowns);
            pthread_mutex_unlock(&mock.mutex);
            goto fail;
        }
        pthread_mutex_unlock(&mock.mutex);
        pthread_cond_destroy(&mock.condition);
        pthread_mutex_destroy(&mock.mutex);
        continue;
fail:
        if (mock.context != NULL) nra_destroy(mock.context);
        pthread_cond_destroy(&mock.condition);
        pthread_mutex_destroy(&mock.mutex);
        return 0;
    }
    return 1;
}

typedef struct SnapshotReader {
    NRA_Context* context;
    atomic_bool stop;
    int copies;
} SnapshotReader;

static void* snapshot_reader(void* userdata) {
    SnapshotReader* reader = userdata;
    NRA_UISnapshot snapshot;
    while (!atomic_load_explicit(&reader->stop, memory_order_relaxed)) {
        if (!nra_copy_ui_snapshot(reader->context, &snapshot) ||
            snapshot.version != NRA_UI_SNAPSHOT_VERSION || !snapshot.available)
            return NULL;
        ++reader->copies;
    }
    return NULL;
}

static int snapshot_contains(const NRA_UISnapshot* snapshot, const char* text) {
    const unsigned char* bytes = (const unsigned char*)snapshot;
    const size_t length = strlen(text);
    size_t i;
    for (i = 0; i + length <= sizeof(*snapshot); ++i) {
        if (memcmp(bytes + i, text, length) == 0)
            return 1;
    }
    return 0;
}

static int ui_snapshot_events(NRA_Context* context) {
    NRA_UISnapshot snapshot;
    char long_text[NRA_UI_TEXT_MAX + 40];
    rc_client_achievement_t achievement = {.title = "Achievement title", .description = "Achievement description",
        .badge_name = "1234567", .measured_progress = "1/10", .id = 77, .points = 5};
    rc_client_leaderboard_t leaderboard = {.title = "Leaderboard title", .description = "Leaderboard description", .id = 88};
    rc_client_leaderboard_tracker_t tracker = {.display = "00:12.34", .id = 99};
    rc_client_leaderboard_scoreboard_t scoreboard = {.leaderboard_id = 88, .new_rank = 3};
    rc_client_server_error_t error = {.error_message = "https://server.invalid/token=secret", .api = "award", .result = -1};
    rc_client_event_t event = {.achievement = &achievement, .leaderboard = &leaderboard,
        .leaderboard_tracker = &tracker, .leaderboard_scoreboard = &scoreboard, .server_error = &error};
    SnapshotReader reader = {.context = context};
    pthread_t thread;
    int i;

    memset(long_text, 'x', sizeof(long_text) - 1);
    long_text[sizeof(long_text) - 1] = '\0';
    achievement.title = long_text;
    event.type = RC_CLIENT_EVENT_ACHIEVEMENT_CHALLENGE_INDICATOR_SHOW;
    nra_handle_event(context, &event);
    if (!nra_copy_ui_snapshot(context, &snapshot) ||
        snapshot.challenge.title[NRA_UI_TEXT_MAX - 1] != '\0' ||
        strlen(snapshot.challenge.title) != NRA_UI_TEXT_MAX - 1) return 0;
    achievement.title = "Achievement title";
    snprintf(scoreboard.submitted_score, sizeof(scoreboard.submitted_score), "%s", "12345");
    event.type = RC_CLIENT_EVENT_ACHIEVEMENT_TRIGGERED; nra_handle_event(context, &event);
    if (!nra_copy_ui_snapshot(context, &snapshot) || snapshot.toast_count != 1 ||
        snapshot.toasts[0].sequence == 0) return 0;
    event.type = RC_CLIENT_EVENT_LEADERBOARD_STARTED; nra_handle_event(context, &event);
    event.type = RC_CLIENT_EVENT_LEADERBOARD_FAILED; nra_handle_event(context, &event);
    event.type = RC_CLIENT_EVENT_LEADERBOARD_SUBMITTED; nra_handle_event(context, &event);
    event.type = RC_CLIENT_EVENT_ACHIEVEMENT_CHALLENGE_INDICATOR_SHOW; nra_handle_event(context, &event);
    event.type = RC_CLIENT_EVENT_ACHIEVEMENT_PROGRESS_INDICATOR_SHOW; nra_handle_event(context, &event);
    achievement.measured_progress[0] = '9';
    event.type = RC_CLIENT_EVENT_ACHIEVEMENT_PROGRESS_INDICATOR_UPDATE; nra_handle_event(context, &event);
    event.type = RC_CLIENT_EVENT_LEADERBOARD_TRACKER_SHOW; nra_handle_event(context, &event);
    snprintf(tracker.display, sizeof(tracker.display), "%s", "00:13.00");
    event.type = RC_CLIENT_EVENT_LEADERBOARD_TRACKER_UPDATE; nra_handle_event(context, &event);
    event.type = RC_CLIENT_EVENT_LEADERBOARD_SCOREBOARD; nra_handle_event(context, &event);
    event.type = RC_CLIENT_EVENT_GAME_COMPLETED; nra_handle_event(context, &event);
    event.type = RC_CLIENT_EVENT_SUBSET_COMPLETED; nra_handle_event(context, &event);
    event.type = RC_CLIENT_EVENT_DISCONNECTED; nra_handle_event(context, &event);
    event.type = RC_CLIENT_EVENT_RECONNECTED; nra_handle_event(context, &event);
    event.type = RC_CLIENT_EVENT_SERVER_ERROR; nra_handle_event(context, &event);
    for (i = 0; i < 10; ++i) {
        achievement.id = (uint32_t)(100 + i);
        event.type = RC_CLIENT_EVENT_ACHIEVEMENT_TRIGGERED;
        nra_handle_event(context, &event);
    }
    memset(&achievement, 0xa5, sizeof(achievement));
    memset(&leaderboard, 0xa5, sizeof(leaderboard));
    memset(&tracker, 0xa5, sizeof(tracker));
    memset(&scoreboard, 0xa5, sizeof(scoreboard));
    if (!nra_copy_ui_snapshot(context, &snapshot) || !snapshot.challenge_active || !snapshot.progress_active ||
        !snapshot.leaderboard_tracker_active || snapshot.leaderboard_tracker_id != 99 ||
        strcmp(snapshot.leaderboard_tracker_value, "00:13.00") || !snapshot.recent_leaderboard_result ||
        snapshot.recent_leaderboard_rank != 3 || strcmp(snapshot.recent_leaderboard_score, "12345") ||
        snapshot.connection != NRA_UI_CONNECTION_ONLINE || !snapshot.has_error ||
        strcmp(snapshot.error, "RetroAchievements server request failed") || snapshot.toast_count != NRA_UI_TOAST_MAX ||
        snapshot.toasts[0].related_id != 102 || snapshot.progress.id != 77 ||
        strcmp(snapshot.progress.measured_progress, "9/10") ||
        snapshot_contains(&snapshot, "token=secret")) return 0;
    event.type = RC_CLIENT_EVENT_ACHIEVEMENT_CHALLENGE_INDICATOR_HIDE; nra_handle_event(context, &event);
    event.type = RC_CLIENT_EVENT_ACHIEVEMENT_PROGRESS_INDICATOR_HIDE; nra_handle_event(context, &event);
    event.type = RC_CLIENT_EVENT_LEADERBOARD_TRACKER_HIDE; nra_handle_event(context, &event);
    if (!nra_copy_ui_snapshot(context, &snapshot) || snapshot.challenge_active || snapshot.progress_active ||
        snapshot.leaderboard_tracker_active) return 0;
    {
        rc_client_achievement_t reset_achievement = {.id = 1};
        event.achievement = &reset_achievement;
        event.type = RC_CLIENT_EVENT_ACHIEVEMENT_PROGRESS_INDICATOR_SHOW; nra_handle_event(context, &event);
        event.type = RC_CLIENT_EVENT_RESET; nra_handle_event(context, &event);
        if (!nra_copy_ui_snapshot(context, &snapshot) || snapshot.progress_active) return 0;
    }
    pthread_mutex_lock(&context->mutex);
    if (!context->reset_pending) {
        pthread_mutex_unlock(&context->mutex);
        return 0;
    }
    context->reset_pending = false;
    pthread_mutex_unlock(&context->mutex);
    if (pthread_create(&thread, NULL, snapshot_reader, &reader) != 0) return 0;
    for (i = 0; i < 1000; ++i) (void)nra_copy_ui_snapshot(context, &snapshot);
    atomic_store_explicit(&reader.stop, true, memory_order_relaxed);
    (void)pthread_join(thread, NULL);
    return reader.copies > 0;
}

static int durable_outbox_flow(const NRA_CreateParams* params) {
    Mock mock = {.processable = true, .validated = true, .accept_game = true};
    NRA_PlatformVTable platform = *params->platform;
    NRA_CreateParams local_params = *params;
    size_t record_count = 0;
    int begins_before;

    platform.secure_blob_get = secure_get;
    platform.secure_blob_set = secure_set;
    platform.secure_blob_delete = secure_delete;
    mock.rom = generate_generic_file(FIXTURE_BYTES);
    mock.rom_size = FIXTURE_BYTES;
    local_params.platform = &platform;
    local_params.platform_userdata = &mock;
    local_params.game_userdata = &mock;
    if (nra_create(&local_params, &mock.context) != NRA_OK ||
        nra_request_mode(mock.context, NRA_MODE_LIVE_CASUAL) != NRA_OK ||
        nra_login_password(mock.context, "user", "password") != NRA_PENDING ||
        !route(&mock, "r=login2", login_ok)) goto fail;
    drain(&mock);
    if (!load(&mock, true)) goto fail;
    nra_notify_reset_completed(mock.context);
    nra_do_frame(mock.context);
    mock.memory[1] = 3;
    mock.memory[2] = 7;
    begins_before = mock.begins;
    for (int frame = 0; frame < 4 && mock.begins == begins_before; ++frame) {
        nra_do_frame(mock.context);
        nra_idle(mock.context);
    }
    if (mock.begins != begins_before + 1 ||
        !strstr(mock.posts[mock.begins - 1], "r=awardachievement") ||
        mock.secure_size == 0 || mock.secure_sets == 0 ||
        nra_outbox_journal_validate(mock.secure_blob, mock.secure_size, &record_count) !=
            NRA_OUTBOX_JOURNAL_OK ||
        record_count != 1) goto fail;
    if (complete(&mock, mock.ids[mock.begins - 1], award_ok, 200, false) != NRA_OK) goto fail;
    drain(&mock);
    if (mock.secure_size != 0 || mock.secure_deletes == 0) goto fail;
    /* Let rcheevos raise completion events before the game is unloaded. */
    nra_do_frame(mock.context);
    nra_unload_game(mock.context);
    nra_destroy(mock.context);
    free(mock.rom);
    return 1;
fail:
    if (mock.context != NULL) {
        nra_unload_game(mock.context);
        nra_destroy(mock.context);
    }
    if (mock.secure_blob != NULL) {
        memset(mock.secure_blob, 0, mock.secure_size);
        free(mock.secure_blob);
    }
    free(mock.rom);
    return 0;
}

static int mode_admission_gate(const NRA_CreateParams* params) {
    Mock mock = {0};
    NRA_GameAdapterVTable game = *params->game;
    NRA_CreateParams local_params = *params;
    NRA_UISnapshot ui;

    game.admit_mode = admit_mode;
    local_params.game = &game;
    local_params.platform_userdata = &mock;
    local_params.game_userdata = &mock;
    if (nra_create(&local_params, &mock.context) != NRA_OK ||
        nra_request_mode(mock.context, NRA_MODE_LIVE_CASUAL) != NRA_MEMORY_UNVERIFIED ||
        mock.gate_saw_game_loaded ||
        nra_request_mode(mock.context, NRA_MODE_SPECTATOR) != NRA_OK ||
        !nra_copy_ui_snapshot(mock.context, &ui) || ui.mode != NRA_MODE_SPECTATOR) {
        if (mock.context != NULL) nra_destroy(mock.context);
        return 0;
    }
    nra_destroy(mock.context);
    return 1;
}

static int locked_mode_transition(const NRA_CreateParams* params) {
    Mock mock = {.processable = true, .validated = true, .accept_game = true, .allow_live_mode = true};
    NRA_GameAdapterVTable game = *params->game;
    NRA_CreateParams local_params = *params;
    NRA_StatusSnapshot status;

    game.admit_mode = admit_mode;
    local_params.game = &game;
    local_params.platform_userdata = &mock;
    local_params.game_userdata = &mock;
    mock.rom = generate_generic_file(FIXTURE_BYTES);
    mock.rom_size = FIXTURE_BYTES;
    if (mock.rom == NULL ||
        nra_create(&local_params, &mock.context) != NRA_OK ||
        nra_request_mode(mock.context, NRA_MODE_SPECTATOR) != NRA_OK ||
        nra_login_password(mock.context, "user", "password") != NRA_PENDING ||
        !route(&mock, "r=login2", login_ok)) goto fail;
    drain(&mock);
    if (nra_load_current_game(mock.context) != NRA_PENDING ||
        !nra_copy_status_snapshot(mock.context, &status) || !status.load_pending ||
        status.mode != NRA_MODE_SPECTATOR || !rc_client_get_spectator_mode_enabled(mock.context->client) ||
        nra_request_mode(mock.context, NRA_MODE_LIVE_CASUAL) != NRA_INVALID_STATE ||
        !nra_copy_status_snapshot(mock.context, &status) || !status.load_pending ||
        status.mode != NRA_MODE_SPECTATOR || !rc_client_get_spectator_mode_enabled(mock.context->client)) goto fail;
    nra_unload_game(mock.context);
    if (!load(&mock, false) ||
        !nra_copy_status_snapshot(mock.context, &status) || !status.game_loaded ||
        status.mode != NRA_MODE_SPECTATOR ||
        !rc_client_get_spectator_mode_enabled(mock.context->client)) goto fail;
    if (nra_request_mode(mock.context, NRA_MODE_LIVE_CASUAL) != NRA_INVALID_STATE ||
        !rc_client_get_spectator_mode_enabled(mock.context->client) ||
        !nra_copy_status_snapshot(mock.context, &status) ||
        !status.game_loaded || status.mode != NRA_MODE_SPECTATOR) goto fail;
    nra_unload_game(mock.context);
    if (nra_request_mode(mock.context, NRA_MODE_LIVE_CASUAL) != NRA_OK ||
        !nra_copy_status_snapshot(mock.context, &status) ||
        status.game_loaded || status.mode != NRA_MODE_LIVE_CASUAL ||
        nra_load_current_game(mock.context) != NRA_PENDING ||
        !nra_copy_status_snapshot(mock.context, &status) || !status.load_pending ||
        status.mode != NRA_MODE_LIVE_CASUAL || rc_client_get_spectator_mode_enabled(mock.context->client) ||
        nra_request_mode(mock.context, NRA_MODE_SPECTATOR) != NRA_INVALID_STATE ||
        !nra_copy_status_snapshot(mock.context, &status) || !status.load_pending ||
        status.mode != NRA_MODE_LIVE_CASUAL || rc_client_get_spectator_mode_enabled(mock.context->client)) goto fail;
    nra_unload_game(mock.context);
    if (nra_request_mode(mock.context, NRA_MODE_LIVE_CASUAL) != NRA_OK ||
        !nra_copy_status_snapshot(mock.context, &status) ||
        status.game_loaded || status.mode != NRA_MODE_LIVE_CASUAL ||
        !load(&mock, true) ||
        rc_client_get_spectator_mode_enabled(mock.context->client) ||
        !nra_copy_status_snapshot(mock.context, &status) ||
        !status.game_loaded || status.mode != NRA_MODE_LIVE_CASUAL) goto fail;
    nra_unload_game(mock.context);
    nra_destroy(mock.context);
    free(mock.rom);
    return 1;
fail:
    if (mock.context != NULL) nra_destroy(mock.context);
    free(mock.rom);
    return 0;
}

static int durable_outbox_replay_scope(const NRA_CreateParams* params) {
    Mock mock = {.processable = true, .validated = true, .accept_game = true};
    NRA_PlatformVTable platform = *params->platform;
    NRA_CreateParams local_params = *params;
    NRA_OutboxJournalRecord record;
    size_t record_count = 0;
    size_t begins_before;

    platform.secure_blob_get = secure_get;
    platform.secure_blob_set = secure_set;
    platform.secure_blob_delete = secure_delete;
    mock.rom = generate_generic_file(FIXTURE_BYTES);
    mock.rom_size = FIXTURE_BYTES;
    local_params.platform = &platform;
    local_params.platform_userdata = &mock;
    local_params.game_userdata = &mock;
    if (!seed_mixed_outbox(&mock) ||
        nra_create(&local_params, &mock.context) != NRA_OK ||
        nra_request_mode(mock.context, NRA_MODE_LIVE_CASUAL) != NRA_OK ||
        nra_login_password(mock.context, "user", "password") != NRA_PENDING ||
        !route(&mock, "r=login2", login_ok)) goto fail;
    drain(&mock);
    begins_before = mock.begins;
    if (!load(&mock, true) || mock.begins != begins_before + 3 ||
        !strstr(mock.posts[mock.begins - 1], "u=user") ||
        strstr(mock.posts[mock.begins - 1], "u=other") ||
        !strstr(mock.posts[mock.begins - 1], "a=5501") ||
        strstr(mock.posts[mock.begins - 1], "a=5502")) goto fail;
    if (complete(&mock, mock.ids[mock.begins - 1], award_ok, 200, false) != NRA_OK) goto fail;
    drain(&mock);
    if (mock.begins != begins_before + 4 ||
        !strstr(mock.posts[mock.begins - 1], "u=user") ||
        !strstr(mock.posts[mock.begins - 1], "a=5502") ||
        strstr(mock.posts[mock.begins - 1], "a=5501")) goto fail;
    if (complete(&mock, mock.ids[mock.begins - 1], award_ok, 200, false) != NRA_OK) goto fail;
    drain(&mock);
    if (nra_outbox_journal_validate(mock.secure_blob, mock.secure_size, &record_count) !=
            NRA_OUTBOX_JOURNAL_OK ||
        record_count != 1 ||
        nra_outbox_journal_decode(mock.secure_blob, mock.secure_size, &record, 1, &record_count) !=
            NRA_OUTBOX_JOURNAL_OK ||
        record_count != 1 || record.sequence != 1 || record.account_size != 5 ||
        memcmp(record.account, "other", 5) != 0) goto fail;
    nra_do_frame(mock.context);
    nra_unload_game(mock.context);
    nra_destroy(mock.context);
    free(mock.rom);
    return 1;
fail:
    if (mock.context != NULL) {
        nra_unload_game(mock.context);
        nra_destroy(mock.context);
    }
    if (mock.secure_blob != NULL) {
        memset(mock.secure_blob, 0, mock.secure_size);
        free(mock.secure_blob);
    }
    free(mock.rom);
    return 0;
}

int main(void) {
    static const NRA_PlatformVTable platform = {.now_ms = now, .http_begin = begin, .http_cancel = cancel,
        .http_shutdown = shutdown_http, .secret_set = store, .secret_delete = erase};
    static const NRA_GameAdapterVTable game = {.get_rom = rom, .build_memory_snapshot = memory,
        .release_memory_snapshot = release, .request_full_reset = reset, .is_game_tick_processable = processable,
        .accept_identified_game = accept_game, .on_ra_game_loaded = loaded, .on_ra_game_unloaded = unloaded};
    NRA_CreateParams params = {.abi_version = NRA_ABI_VERSION, .client_name = "test", .client_version = "1",
        .user_agent = "native-ra-test/1", .platform = &platform, .game = &game};
    NRA_StatusSnapshot status;
    Mock mock = {.processable = true, .validated = true, .accept_game = true};

    {
        NRA_PlatformVTable invalid_platform = platform;
        NRA_GameAdapterVTable invalid_game = game;
        NRA_CreateParams invalid_params = params;

        invalid_platform.http_shutdown = NULL;
        invalid_params.platform = &invalid_platform;
        if (nra_create(&invalid_params, &mock.context) != NRA_INVALID_ARGUMENT) return 1;
        invalid_game.release_memory_snapshot = NULL;
        invalid_params.platform = &platform;
        invalid_params.game = &invalid_game;
        if (nra_create(&invalid_params, &mock.context) != NRA_INVALID_ARGUMENT) return 1;
    }
    mock.rom = generate_generic_file(FIXTURE_BYTES);
    mock.rom_size = FIXTURE_BYTES;
    mock.memory[1] = 3;
    mock.memory[2] = 7;
    params.platform_userdata = &mock;
    params.game_userdata = &mock;
    {
        Mock capacity = {0};
        Mock edges = {0};
        Mock malformed = {0};
        NRA_CreateParams capacity_params = params;
        NRA_CreateParams edges_params = params;
        NRA_CreateParams malformed_params = params;
        NRA_HttpCompletion invalid = {.request_id = 1, .body = (const uint8_t*)"x",
            .body_size = NRA_MAX_RESPONSE_BYTES + 1};
        capacity_params.platform_userdata = &capacity;
        capacity_params.game_userdata = &capacity;
        edges_params.platform_userdata = &edges;
        edges_params.game_userdata = &edges;
        malformed_params.platform_userdata = &malformed;
        malformed_params.game_userdata = &malformed;
        if (!queue_limits(&capacity, &capacity_params)) { fprintf(stderr, "queue limits failed\n"); return 1; }
        if (!completion_edges(&edges, &edges_params)) { fprintf(stderr, "completion edges failed\n"); return 1; }
        if (!malformed_and_retry(&malformed, &malformed_params)) { fprintf(stderr, "retry failed\n"); return 1; }
        if (!frame_drains_login_completion(&params)) { fprintf(stderr, "frame login failed\n"); return 1; }
        if (!credential_persistence_failure(&params)) { fprintf(stderr, "credential persistence failed\n"); return 1; }
        if (!rejected_stored_token_clears_credentials(&params)) { fprintf(stderr, "stored token clear failed\n"); return 1; }
        if (!token_failure_unloads_game(&params)) { fprintf(stderr, "token failure unload failed\n"); return 1; }
        if (!mode_admission_gate(&params)) { fprintf(stderr, "mode admission gate failed\n"); return 1; }
        if (!locked_mode_transition(&params)) { fprintf(stderr, "locked mode transition failed\n"); return 1; }
        if (!durable_outbox_flow(&params)) { fprintf(stderr, "durable outbox flow failed\n"); return 1; }
        if (!durable_outbox_replay_scope(&params)) { fprintf(stderr, "durable outbox scope failed\n"); return 1; }
        if (!pthread_completion_stress(&params)) { fprintf(stderr, "pthread stress failed\n"); return 1; }
        if (nra_enqueue_http_completion(NULL, &invalid) != NRA_INVALID_ARGUMENT) return 1;
    }
    if (mock.rom == NULL || nra_create(&params, &mock.context) != NRA_OK ||
        nra_request_mode(mock.context, NRA_MODE_SPECTATOR) != NRA_OK ||
        nra_login_password(mock.context, "user", "password") != NRA_PENDING ||
        nra_login_token(mock.context, "user", "token") != NRA_INVALID_STATE ||
        !route(&mock, "r=login2", login_ok) || !strstr(mock.posts[0], "p=password") ||
        strcmp(mock.user_agents[0], "native-ra-test/1")) {
        fprintf(stderr, "login request failed begins=%d\n", mock.begins);
        goto fail;
    }
    {
        NRA_GameAdapterVTable validating_game = game;
        NRA_CreateParams validating_params = params;
        Mock validating = {
            .processable = true,
            .validated = true,
            .accept_game = true,
            .validation_reader = true,
        };

        validating.rom = mock.rom;
        validating.rom_size = mock.rom_size;
        validating.memory[1] = 3;
        validating.memory[2] = 7;
        validating_game.read_memory_snapshot = read_memory_snapshot;
        validating_params.game = &validating_game;
        validating_params.platform_userdata = &validating;
        validating_params.game_userdata = &validating;
        if (nra_create(&validating_params, &validating.context) != NRA_OK) {
            fprintf(stderr, "validation reader create failed\n");
            goto fail;
        }
        if (nra_request_mode(validating.context, NRA_MODE_SPECTATOR) != NRA_OK ||
            nra_login_password(validating.context, "user", "password") != NRA_PENDING ||
            !route(&validating, "r=login2", login_ok)) {
            fprintf(stderr, "validation reader login failed begins=%d\n", validating.begins);
            if (validating.context) nra_destroy(validating.context);
            goto fail;
        }
        drain(&validating);
        if (!load(&validating, false)) {
            fprintf(stderr, "validation reader load failed begins=%d\n", validating.begins);
            nra_destroy(validating.context);
            goto fail;
        }
        if (validating.snapshot_reads == 0) {
            fprintf(stderr, "validation reader had no reads builds=%d\n", validating.builds);
            nra_destroy(validating.context);
            goto fail;
        }
        nra_destroy(validating.context);
    }
    drain(&mock);
    if (!nra_copy_status_snapshot(mock.context, &status) || !status.logged_in || status.request_pending ||
        mock.stores != 2 || strcmp(mock.stored_keys[0], "native_ra.username") ||
        strcmp(mock.stored_keys[1], "native_ra.token") || mock.stored_sizes[0] != 4 ||
        mock.stored_sizes[1] != 5 || memcmp(mock.stored_values[0], "user", 4) ||
        memcmp(mock.stored_values[1], "token", 5)) {
        fprintf(stderr, "login persistence callback failed\n");
        goto fail;
    }
    if (nra_login_token(mock.context, "user", "token") != NRA_PENDING ||
        !nra_copy_status_snapshot(mock.context, &status) || !status.request_pending) goto fail;
    nra_logout(mock.context, true);
    if (mock.cancels != 1 || mock.deletes != 2 ||
        complete(&mock, mock.ids[mock.begins - 1], login_ok, 200, false) != NRA_INVALID_STATE) goto fail;
    {
        NRA_UISnapshot ui;
        if (!nra_copy_ui_snapshot(mock.context, &ui) || ui.logged_in || ui.game_loaded ||
            ui.achievement_count || ui.toast_count) goto fail;
    }
    if (nra_login_token(mock.context, "user", "token") != NRA_PENDING ||
        !route(&mock, "r=login2", login_ok)) goto fail;
    drain(&mock);
    if (mock.stores != 2) goto fail;
    if (!load(&mock, false)) { fprintf(stderr, "load1 failed loads=%d begins=%d\n", mock.loads, mock.begins); goto fail; }
    if (mock.loads != 1) { fprintf(stderr, "load1 callback failed loads=%d\n", mock.loads); goto fail; }
    {
        NRA_UISnapshot ui;
        if (!nra_copy_ui_snapshot(mock.context, &ui) || !ui.game_loaded || ui.game_id != 1234 ||
            strcmp(ui.game_title, "Synthetic") || ui.achievement_count != 2 || ui.achievements_total != 2 ||
            ui.achievement_points_total != 7 || strcmp(ui.rich_presence, "Synthetic presence") ||
            !ui.logged_in || strcmp(ui.account_name, "user")) goto fail;
        if (!ui_snapshot_events(mock.context)) goto fail;
    }
    if (!nra_copy_status_snapshot(mock.context, &status) || !status.game_loaded || status.request_pending) { fprintf(stderr, "load1 status failed loaded=%d pending=%d\n", status.game_loaded, status.request_pending); goto fail; }
    {
        uint32_t frames_remaining = 0;
        nra_idle(mock.context);
        if (mock.builds != 1 || mock.releases != 1 ||
            !nra_can_pause(mock.context, &frames_remaining)) goto fail;
    }
    nra_unload_game(mock.context);
    if (mock.unloads != 1) { fprintf(stderr, "unload1 failed %d\n", mock.unloads); goto fail; }
    {
        NRA_UISnapshot ui;
        if (!nra_copy_ui_snapshot(mock.context, &ui) || ui.game_loaded || ui.achievement_count ||
            ui.progress_active || ui.toast_count) goto fail;
    }

    if (nra_request_mode(mock.context, NRA_MODE_LIVE_CASUAL) != NRA_OK || !load(&mock, true) || mock.loads != 2) { fprintf(stderr, "load2 failed loads=%d begins=%d\n", mock.loads, mock.begins); goto fail; }
    nra_do_frame(mock.context);
    if (mock.builds != 4 || mock.releases != 4) goto fail;
    mock.validated = false;
    nra_do_frame(mock.context);
    mock.validated = true;
    mock.processable = false;
    nra_do_frame(mock.context);
    mock.processable = true;
    if (mock.builds != 6 || mock.releases != 6) goto fail;
    nra_do_frame(mock.context);
    if (mock.builds != 7 || mock.releases != 7) goto fail;
    pthread_mutex_lock(&mock.context->mutex);
    mock.context->reset_pending = true;
    pthread_mutex_unlock(&mock.context->mutex);
    nra_idle(mock.context);
    if (mock.resets != 0) goto fail;
    nra_do_frame(mock.context);
    if (mock.resets != 1 || mock.builds != 8 || mock.releases != 8) goto fail;
    {
        NRA_UISnapshot before, after;
        if (!nra_copy_ui_snapshot(mock.context, &before)) goto fail;
        nra_do_frame(mock.context);
        if (!nra_copy_ui_snapshot(mock.context, &after) || after.generation != before.generation) goto fail;
    }
    {
        size_t progress_size = nra_progress_size(mock.context);
        uint8_t* progress = malloc(progress_size);
        if (progress_size == 0 || progress == NULL ||
            nra_serialize_progress(mock.context, progress, progress_size, &progress_size) != NRA_OK ||
            nra_deserialize_progress(mock.context, progress, progress_size) != NRA_OK ||
            nra_reset_progress(mock.context) != NRA_OK) {
            free(progress);
            goto fail;
        }
        free(progress);
    }
    nra_unload_game(mock.context);

    mock.accept_game = false;
    if (!load(&mock, true) || !nra_copy_status_snapshot(mock.context, &status) || status.game_loaded || mock.loads != 2) goto fail;
    mock.accept_game = true;
    if (nra_login_token(mock.context, "user", "token") != NRA_PENDING ||
        !nra_copy_status_snapshot(mock.context, &status) || !status.request_pending) goto fail;
    nra_logout(mock.context, true);
    if (mock.cancels != 2 || mock.deletes != 4 ||
        complete(&mock, mock.ids[mock.begins - 1], login_ok, 200, false) != NRA_INVALID_STATE) goto fail;
    nra_destroy(mock.context);
    mock.context = NULL;
    free(mock.rom);
    if (mock.shutdowns != 1) return 1;
    fprintf(stderr, "NATIVE RA P3 LIFECYCLE OK\n");
    return 0;
fail:
    if (mock.context != NULL) {
        nra_destroy(mock.context);
        mock.context = NULL;
    }
    free(mock.rom);
    fprintf(stderr, "native_ra P3 lifecycle test failed (begins=%d cancels=%d builds=%d releases=%d)\n",
        mock.begins, mock.cancels, mock.builds, mock.releases);
    return 1;
}
