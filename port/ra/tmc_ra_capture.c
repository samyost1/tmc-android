/*
 * One-shot local RA parity capture.
 *
 * TMC_RA_CAPTURE_AT_FRAME=N and TMC_RA_CAPTURE_STATE=S0..S14 write only below
 * artifacts/ra-local/<N>/. TMC_RA_CAPTURE_CHECKPOINT is accepted as an alias
 * for TMC_RA_CAPTURE_STATE.
 * The directory is ignored because snapshots can contain save and game data.
 * The two audit request functions are intentionally restricted to absolute
 * caller-owned paths so an Android callback cannot redirect capture output
 * through a relative or traversal path.
 */

#include "port_repro.h"
#include "port_rom.h"
#include "rc_hash.h"
#include "tmc_ra_memory.h"
#include "tmc_ra_runtime.h"

#include <errno.h>
#include <stdint.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#define TMC_RA_MKDIR(path) _mkdir(path)
#else
#include <sys/stat.h>
#define TMC_RA_MKDIR(path) mkdir(path, 0700)
#endif

#define TMC_RA_CAPTURE_ROOT "artifacts/ra-local"
#define TMC_RA_AUDIT_PATH_MAX 512
#define TMC_RA_OPERATOR_PATH_MAX 1024
#define TMC_RA_BUNDLE_PATH_MAX (TMC_RA_OPERATOR_PATH_MAX + 32)

static pthread_mutex_t sAuditMutex = PTHREAD_MUTEX_INITIALIZER;
static char sAuditPath[TMC_RA_AUDIT_PATH_MAX];
static char sSnapshotAuditPath[TMC_RA_AUDIT_PATH_MAX];
static char sOperatorState[sizeof("S14")];
static char sOperatorPath[TMC_RA_OPERATOR_PATH_MAX];
static bool sOperatorPending;

static const char* const kCheckpointIds[] = {
    "S0", "S1", "S2", "S3", "S4", "S5", "S6", "S7",
    "S8", "S9", "S10", "S11", "S12", "S13", "S14",
};

static const char* const kCheckpointNames[] = {
    "boot/title",
    "file select",
    "new game intro",
    "first controllable overworld",
    "room transition",
    "pause/items",
    "quest/status",
    "world map",
    "dungeon entry",
    "dungeon map/floor transition",
    "item pickup / progress flag change",
    "save and reload",
    "soft reset/relaunch",
    "authenticated achievement evaluation",
    "normal achievement trigger",
};

static int CheckpointIndex(const char* checkpoint) {
    size_t index;

    if (checkpoint == NULL)
        return -1;
    for (index = 0; index < sizeof(kCheckpointIds) / sizeof(kCheckpointIds[0]); ++index) {
        if (strcmp(checkpoint, kCheckpointIds[index]) == 0)
            return (int)index;
    }
    return -1;
}

bool Port_ReproRaCapture_CheckpointValid(const char* checkpoint) {
    return CheckpointIndex(checkpoint) >= 0;
}

static const char* CaptureCheckpointName(const char* checkpoint) {
    const int index = CheckpointIndex(checkpoint);
    return index >= 0 ? kCheckpointNames[index] : NULL;
}

static const char* CaptureCheckpoint(void) {
    const char* state = getenv("TMC_RA_CAPTURE_STATE");
    const char* checkpoint = getenv("TMC_RA_CAPTURE_CHECKPOINT");
    const bool has_state = state != NULL && *state != '\0';
    const bool has_checkpoint = checkpoint != NULL && *checkpoint != '\0';

    if (has_state && has_checkpoint && strcmp(state, checkpoint) != 0)
        return NULL;
    return has_state ? state : (has_checkpoint ? checkpoint : NULL);
}

static const char* RomMd5(void) {
    static char hash[33];
    static bool initialized;

    if (!initialized) {
        initialized = true;
        if (gRomData == NULL || gRomSize == 0 ||
            !rc_hash_generate_from_buffer(hash, RC_CONSOLE_GAMEBOY_ADVANCE, gRomData, gRomSize))
            snprintf(hash, sizeof(hash), "unknown");
    }
    return hash;
}

static int MakeDirectory(const char* path) {
    return TMC_RA_MKDIR(path) == 0 || errno == EEXIST;
}

static int WriteFile(const char* path, const void* data, size_t bytes) {
    FILE* file = fopen(path, "wb");
    int complete;

    if (file == NULL)
        return 0;
    complete = fwrite(data, 1, bytes, file) == bytes;
    return fclose(file) == 0 && complete;
}

static int WriteFileAtomic(const char* path, const void* data, size_t bytes) {
    char temporary[TMC_RA_BUNDLE_PATH_MAX + 8];

    if (snprintf(temporary, sizeof(temporary), "%s.tmp", path) >= (int)sizeof(temporary) ||
        !WriteFile(temporary, data, bytes))
        return 0;
    if (rename(temporary, path) == 0)
        return 1;
    (void)remove(temporary);
    return 0;
}

static bool ParseCaptureFrame(const char* value, uint32_t* frame) {
    char* end;
    unsigned long long parsed;

    if (value == NULL || *value < '0' || *value > '9')
        return false;
    errno = 0;
    parsed = strtoull(value, &end, 10);
    if (errno == ERANGE || end == value || *end != '\0' || parsed > UINT32_MAX)
        return false;
    *frame = (uint32_t)parsed;
    return true;
}

static bool IsSafeAbsolutePath(const char* path, size_t capacity, size_t* length) {
    size_t bytes;

    if (path == NULL || path[0] != '/' || strstr(path, "..") != NULL)
        return false;
    bytes = strnlen(path, capacity);
    if (bytes == 0 || bytes >= capacity)
        return false;
    if (length != NULL)
        *length = bytes;
    return true;
}

bool Port_ReproRaCapture_Request(const char* checkpoint, const char* output_path) {
    size_t path_bytes;

    if (!Port_ReproRaCapture_CheckpointValid(checkpoint) ||
        !IsSafeAbsolutePath(output_path, sizeof(sOperatorPath), &path_bytes))
        return false;
    if (path_bytes < 2)
        return false;
    pthread_mutex_lock(&sAuditMutex);
    if (sOperatorPending) {
        pthread_mutex_unlock(&sAuditMutex);
        return false;
    }
    memset(sOperatorState, 0, sizeof(sOperatorState));
    memcpy(sOperatorState, checkpoint, strlen(checkpoint));
    memcpy(sOperatorPath, output_path, path_bytes + 1);
    sOperatorPending = true;
    pthread_mutex_unlock(&sAuditMutex);
    return true;
}

bool Port_ReproRaRequestedAudit_Request(const char* output_path) {
    size_t bytes;

    if (!IsSafeAbsolutePath(output_path, sizeof(sAuditPath), &bytes))
        return false;
    pthread_mutex_lock(&sAuditMutex);
    memcpy(sAuditPath, output_path, bytes + 1);
    pthread_mutex_unlock(&sAuditMutex);
    return true;
}

bool Port_ReproRaSnapshotAudit_Request(const char* output_base) {
    size_t bytes;

    if (!IsSafeAbsolutePath(output_base, sizeof(sSnapshotAuditPath), &bytes))
        return false;
    pthread_mutex_lock(&sAuditMutex);
    memcpy(sSnapshotAuditPath, output_base, bytes + 1);
    pthread_mutex_unlock(&sAuditMutex);
    return true;
}

static int WriteMetadata(const char* path, unsigned int frame, TmcRaFrameView view,
                         const char* state, const char* checkpoint) {
    FILE* file = fopen(path, "w");
    int wrote;

    if (file == NULL)
        return 0;
    wrote = fprintf(file,
                    "{\"format\":\"tmc-ra-capture-v1\",\"state\":\"%s\","
                    "\"checkpoint\":\"%s\",\"phase\":\"post-evaluation\","
                    "\"frame\":%u,\"generation\":%u,"
                    "\"rom_md5\":\"%s\",\"manifest_sha256\":\"%s\","
                    "\"snapshot_bytes\":%u,\"requested_bytes\":%u,"
                    "\"requested_out_of_range_bytes\":%u,\"requested_ranges\":%u,"
                    "\"invalid_requested_bytes\":%u,\"invalid_requested_ranges\":%u}\n",
                    state != NULL ? state : "",
                    checkpoint != NULL ? checkpoint : "",
                    frame, view.snapshot.generation, RomMd5(),
                    TmcRaMemory_ManifestHash(), TMC_RA_SNAPSHOT_BYTES,
                    view.coverage.selected_bytes, view.coverage.out_of_range_bytes,
                    view.audit.requested_ranges, view.audit.invalid_requested_bytes,
                    view.audit.invalid_requested_ranges) > 0;
    return fclose(file) == 0 && wrote;
}

static void WriteRequestedAudit(TmcRaFrameView view, const char* state,
                                const char* checkpoint, unsigned int frame) {
    char path[TMC_RA_AUDIT_PATH_MAX];
    char metadata_path[TMC_RA_AUDIT_PATH_MAX + 8];
    char metadata_bytes[256];
    int metadata_size;

    pthread_mutex_lock(&sAuditMutex);
    if (sAuditPath[0] == '\0') {
        pthread_mutex_unlock(&sAuditMutex);
        return;
    }
    memcpy(path, sAuditPath, sizeof(path));
    sAuditPath[0] = '\0';
    pthread_mutex_unlock(&sAuditMutex);

    if (snprintf(metadata_path, sizeof(metadata_path), "%s.meta", path) >= (int)sizeof(metadata_path))
        return;
    metadata_size = snprintf(metadata_bytes, sizeof(metadata_bytes),
                             "state=%s\ncheckpoint=%s\nframe=%u\ngeneration=%u\n"
                             "selected_bytes=%u\nout_of_range_bytes=%u\n"
                             "requested_ranges=%u\ninvalid_requested_bytes=%u\n"
                             "invalid_requested_ranges=%u\n",
                             state != NULL ? state : "",
                             checkpoint != NULL ? checkpoint : "",
                             frame, view.snapshot.generation,
                             view.coverage.selected_bytes, view.coverage.out_of_range_bytes,
                             view.audit.requested_ranges, view.audit.invalid_requested_bytes,
                             view.audit.invalid_requested_ranges);
    if (metadata_size < 0 || metadata_size >= (int)sizeof(metadata_bytes) ||
        !WriteFileAtomic(metadata_path, metadata_bytes, (size_t)metadata_size) ||
        view.coverage.bitmap == NULL ||
        !WriteFileAtomic(path, view.coverage.bitmap, TMC_RA_SNAPSHOT_BYTES))
        return;
}

static void WriteSnapshotAudit(unsigned int frame, TmcRaFrameView view,
                               const char* state, const char* checkpoint) {
    char base[TMC_RA_AUDIT_PATH_MAX];
    char path[TMC_RA_AUDIT_PATH_MAX + 24];

    pthread_mutex_lock(&sAuditMutex);
    if (sSnapshotAuditPath[0] == '\0') {
        pthread_mutex_unlock(&sAuditMutex);
        return;
    }
    memcpy(base, sSnapshotAuditPath, sizeof(base));
    sSnapshotAuditPath[0] = '\0';
    pthread_mutex_unlock(&sAuditMutex);

    if (snprintf(path, sizeof(path), "%s.provenance.bin", base) >= (int)sizeof(path) ||
        view.snapshot.provenance == NULL ||
        !WriteFileAtomic(path, view.snapshot.provenance, TMC_RA_SNAPSHOT_BYTES) ||
        snprintf(path, sizeof(path), "%s.validity.bin", base) >= (int)sizeof(path) ||
        view.snapshot.validated == NULL ||
        !WriteFileAtomic(path, view.snapshot.validated, TMC_RA_SNAPSHOT_BYTES) ||
        snprintf(path, sizeof(path), "%s.metadata.json", base) >= (int)sizeof(path) ||
        !WriteMetadata(path, frame, view, state, checkpoint) ||
        snprintf(path, sizeof(path), "%s.snapshot.bin", base) >= (int)sizeof(path))
        return;
    (void)WriteFileAtomic(path, view.snapshot.bytes, TMC_RA_SNAPSHOT_BYTES);
}

static bool TakeOperatorRequest(char* checkpoint, size_t checkpoint_capacity,
                                char* output_path, size_t output_capacity) {
    bool pending;

    pthread_mutex_lock(&sAuditMutex);
    pending = sOperatorPending;
    if (pending) {
        if (checkpoint_capacity < sizeof(sOperatorState) ||
            output_capacity < sizeof(sOperatorPath)) {
            pending = false;
        } else {
            memcpy(checkpoint, sOperatorState, sizeof(sOperatorState));
            memcpy(output_path, sOperatorPath, sizeof(sOperatorPath));
        }
        memset(sOperatorState, 0, sizeof(sOperatorState));
        memset(sOperatorPath, 0, sizeof(sOperatorPath));
        sOperatorPending = false;
    }
    pthread_mutex_unlock(&sAuditMutex);
    return pending;
}

static bool BundlePath(char* path, size_t capacity, const char* directory, const char* name) {
    int written;

    written = snprintf(path, capacity, "%s/%s", directory, name);
    return written >= 0 && (size_t)written < capacity;
}

static bool WriteOperatorBundle(uint32_t frame, TmcRaFrameView view,
                                const char* checkpoint, const char* output_path) {
    char path[TMC_RA_BUNDLE_PATH_MAX];
    const char* checkpoint_name = CaptureCheckpointName(checkpoint);

    if (checkpoint_name == NULL || view.snapshot.bytes == NULL ||
        view.snapshot.provenance == NULL || view.snapshot.validated == NULL ||
        view.coverage.bitmap == NULL)
        return false;
    if (!BundlePath(path, sizeof(path), output_path, "snapshot.bin") ||
        !WriteFileAtomic(path, view.snapshot.bytes, TMC_RA_SNAPSHOT_BYTES) ||
        !BundlePath(path, sizeof(path), output_path, "provenance.bin") ||
        !WriteFileAtomic(path, view.snapshot.provenance, TMC_RA_SNAPSHOT_BYTES) ||
        !BundlePath(path, sizeof(path), output_path, "validity.bin") ||
        !WriteFileAtomic(path, view.snapshot.validated, TMC_RA_SNAPSHOT_BYTES) ||
        !BundlePath(path, sizeof(path), output_path, "requested.bin") ||
        !WriteFileAtomic(path, view.coverage.bitmap, TMC_RA_SNAPSHOT_BYTES) ||
        !BundlePath(path, sizeof(path), output_path, "metadata.json") ||
        !WriteMetadata(path, frame, view, checkpoint, checkpoint_name))
        return false;
    return true;
}

void Port_ReproRaCapture_Tick(uint32_t frame, TmcRaFrameView view) {
    static bool frame_request_initialized;
    static bool frame_requested;
    static uint32_t requested_frame;
    static int captured;
    char directory[128];
    char path[160];
    char operator_state[sizeof(sOperatorState)];
    char operator_path[sizeof(sOperatorPath)];
    const char* value;
    const char* state;
    const char* checkpoint;

    if (!frame_request_initialized) {
        value = getenv("TMC_RA_CAPTURE_AT_FRAME");
        frame_request_initialized = true;
        if (value != NULL && *value != '\0') {
            frame_requested = ParseCaptureFrame(value, &requested_frame);
            if (!frame_requested)
                fprintf(stderr, "[ra-capture] invalid TMC_RA_CAPTURE_AT_FRAME\n");
        }
    }
    state = CaptureCheckpoint();
    checkpoint = CaptureCheckpointName(state);
    WriteRequestedAudit(view, state, checkpoint, frame);
    WriteSnapshotAudit(frame, view, state, checkpoint);
    if (TakeOperatorRequest(operator_state, sizeof(operator_state),
                            operator_path, sizeof(operator_path))) {
        if (WriteOperatorBundle(frame, view, operator_state, operator_path)) {
            fprintf(stderr, "[ra-capture] operator bundle captured state %s frame %u generation %u\n",
                    operator_state, frame, view.snapshot.generation);
        } else {
            fprintf(stderr, "[ra-capture] operator bundle rejected or incomplete\n");
        }
    }
    if (!frame_requested || captured || frame < requested_frame)
        return;
    if (!Port_ReproRaCapture_CheckpointValid(state) ||
        view.snapshot.bytes == NULL || view.snapshot.provenance == NULL ||
        view.snapshot.validated == NULL || view.coverage.bitmap == NULL) {
        fprintf(stderr, "[ra-capture] missing or invalid checkpoint/frame view\n");
        captured = 1;
        return;
    }

    captured = 1;
    if (!MakeDirectory("artifacts") || !MakeDirectory(TMC_RA_CAPTURE_ROOT) ||
        snprintf(directory, sizeof(directory), TMC_RA_CAPTURE_ROOT "/%u", frame) >= (int)sizeof(directory) ||
        !MakeDirectory(directory)) {
        fprintf(stderr, "[ra-capture] failed to create local artifact directory\n");
        TmcRaRuntime_Shutdown(&gTmcRaRuntime);
        exit(1);
    }

    if (snprintf(path, sizeof(path), "%s/snapshot.bin", directory) >= (int)sizeof(path) ||
        !WriteFile(path, view.snapshot.bytes, TMC_RA_SNAPSHOT_BYTES) ||
        snprintf(path, sizeof(path), "%s/provenance.bin", directory) >= (int)sizeof(path) ||
        !WriteFile(path, view.snapshot.provenance, TMC_RA_SNAPSHOT_BYTES) ||
        snprintf(path, sizeof(path), "%s/validity.bin", directory) >= (int)sizeof(path) ||
        !WriteFile(path, view.snapshot.validated, TMC_RA_SNAPSHOT_BYTES) ||
        snprintf(path, sizeof(path), "%s/requested.bin", directory) >= (int)sizeof(path) ||
        !WriteFile(path, view.coverage.bitmap, TMC_RA_SNAPSHOT_BYTES) ||
        snprintf(path, sizeof(path), "%s/metadata.json", directory) >= (int)sizeof(path) ||
        !WriteMetadata(path, frame, view, state, checkpoint)) {
        fprintf(stderr, "[ra-capture] failed to write local artifact\n");
        TmcRaRuntime_Shutdown(&gTmcRaRuntime);
        exit(1);
    }

    fprintf(stderr, "[ra-capture] captured state %s frame %u generation %u\n",
            state, frame, view.snapshot.generation);
    fflush(NULL);
    TmcRaRuntime_Shutdown(&gTmcRaRuntime);
    _Exit(0);
}
