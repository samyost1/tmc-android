#include "port_repro.h"
#include "rc_hash.h"
#include "tmc_ra_memory.h"
#include "tmc_ra_runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

TmcRaRuntime gTmcRaRuntime;

void TmcRaRuntime_Shutdown(TmcRaRuntime* runtime) {
    (void)runtime;
}

int rc_hash_generate_from_buffer(char hash[33], uint32_t console_id,
                                 const uint8_t* buffer, size_t buffer_size) {
    (void)console_id;
    (void)buffer;
    (void)buffer_size;
    memcpy(hash, "0123456789abcdef0123456789abcdef", 33);
    return 1;
}

extern int tmc_ra_memory_fixture_main(void);

static int ReadMetadata(const char* path, char* output, size_t capacity) {
    FILE* file = fopen(path, "rb");
    size_t bytes;

    if (file == NULL)
        return 0;
    bytes = fread(output, 1, capacity - 1, file);
    output[bytes] = '\0';
    return fclose(file) == 0 && bytes != 0;
}

static int Contains(const char* text, const char* needle) {
    return strstr(text, needle) != NULL;
}

static int HasSize(const char* path, size_t expected) {
    struct stat info;
    return stat(path, &info) == 0 && info.st_size == (off_t)expected;
}

int main(void) {
    char temporary[128];
    char metadata_path[512];
    char requested_audit_path[512];
    char snapshot_audit_base[512];
    char operator_root[512];
    char operator_state_dir[512];
    char operator_dir[512];
    char operator_failure_dir[512];
    char metadata[1024];
    char expected_generation[64];
    TmcRaFrameView view;
    pid_t child;
    int status;
    int result = 1;
    bool valid = false;

    if (tmc_ra_memory_fixture_main() != 0 ||
        !Port_ReproRaCapture_CheckpointValid("S0") ||
        !Port_ReproRaCapture_CheckpointValid("S14") ||
        Port_ReproRaCapture_CheckpointValid("S15") ||
        Port_ReproRaCapture_CheckpointValid("S1/../S2"))
        return 1;

    TmcRaMemory_ResetAudit();
    TmcRaMemory_ResetRequestedCoverage();
    TmcRaMemory_Publish();
    (void)TmcRaMemory_Read(0x0010, &valid);
    view = TmcRaMemory_CurrentFrameView();
    snprintf(expected_generation, sizeof(expected_generation),
             "\"generation\":%u", view.snapshot.generation);
    snprintf(temporary, sizeof(temporary), "/tmp/tmc-ra-capture-test-%ld", (long)getpid());
    if (mkdir(temporary, 0700) != 0)
        return 1;
    snprintf(requested_audit_path, sizeof(requested_audit_path),
             "%s/requested-audit.bin", temporary);
    snprintf(snapshot_audit_base, sizeof(snapshot_audit_base),
             "%s/snapshot-audit", temporary);
    snprintf(operator_root, sizeof(operator_root), "%s/ra-captures", temporary);
    snprintf(operator_state_dir, sizeof(operator_state_dir), "%s/S4", operator_root);
    snprintf(operator_dir, sizeof(operator_dir), "%s/native", operator_state_dir);
    snprintf(operator_failure_dir, sizeof(operator_failure_dir), "%s/failure", operator_root);
    if (mkdir(operator_root, 0700) != 0 ||
        mkdir(operator_state_dir, 0700) != 0 ||
        mkdir(operator_dir, 0700) != 0 ||
        Port_ReproRaCapture_Request("S15", operator_dir) ||
        Port_ReproRaCapture_Request("S4", "/") ||
        Port_ReproRaCapture_Request("S4", "relative/path") ||
        Port_ReproRaCapture_Request("S4", "/tmp/../outside"))
        goto cleanup;

    child = fork();
    if (child == 0) {
        if (chdir(temporary) != 0 ||
            !Port_ReproRaRequestedAudit_Request(requested_audit_path) ||
            !Port_ReproRaSnapshotAudit_Request(snapshot_audit_base)) {
            _Exit(2);
        }
        unsetenv("TMC_RA_CAPTURE_AT_FRAME");
        setenv("TMC_RA_CAPTURE_STATE", "S3", 1);
        Port_ReproRaCapture_Tick(0, view);
        _Exit(0);
    }
    if (child < 0 || waitpid(child, &status, 0) != child ||
        !WIFEXITED(status) || WEXITSTATUS(status) != 0)
        goto cleanup;

    snprintf(metadata_path, sizeof(metadata_path), "%s.meta", requested_audit_path);
    if (!HasSize(requested_audit_path, TMC_RA_SNAPSHOT_BYTES) ||
        !ReadMetadata(metadata_path, metadata, sizeof(metadata)) ||
        !Contains(metadata, "state=S3\n") ||
        !Contains(metadata, "checkpoint=first controllable overworld\n"))
        goto cleanup;
    snprintf(metadata_path, sizeof(metadata_path), "%s.snapshot.bin", snapshot_audit_base);
    if (!HasSize(metadata_path, TMC_RA_SNAPSHOT_BYTES))
        goto cleanup;
    snprintf(metadata_path, sizeof(metadata_path), "%s.provenance.bin", snapshot_audit_base);
    if (!HasSize(metadata_path, TMC_RA_SNAPSHOT_BYTES))
        goto cleanup;
    snprintf(metadata_path, sizeof(metadata_path), "%s.validity.bin", snapshot_audit_base);
    if (!HasSize(metadata_path, TMC_RA_SNAPSHOT_BYTES))
        goto cleanup;
    snprintf(metadata_path, sizeof(metadata_path), "%s.metadata.json", snapshot_audit_base);
    if (!ReadMetadata(metadata_path, metadata, sizeof(metadata)) ||
        !Contains(metadata, "\"state\":\"S3\"") ||
        !Contains(metadata, "\"checkpoint\":\"first controllable overworld\"") ||
        !Contains(metadata, "\"phase\":\"post-evaluation\""))
        goto cleanup;

    child = fork();
    if (child == 0) {
        char child_metadata_path[512];

        unsetenv("TMC_RA_CAPTURE_AT_FRAME");
        unsetenv("TMC_RA_CAPTURE_STATE");
        if (!Port_ReproRaCapture_Request("S4", operator_dir) ||
            Port_ReproRaCapture_Request("S4", operator_dir)) {
            _Exit(2);
        }
        Port_ReproRaCapture_Tick(0, view);
        Port_ReproRaCapture_Tick(1, view);
        snprintf(child_metadata_path, sizeof(child_metadata_path),
                 "%s/metadata.json", operator_dir);
        if (!ReadMetadata(child_metadata_path, metadata, sizeof(metadata)) ||
            !Contains(metadata, "\"state\":\"S4\"") ||
            !Contains(metadata, "\"checkpoint\":\"room transition\"") ||
            !Contains(metadata, "\"phase\":\"post-evaluation\"") ||
            !Contains(metadata, "\"frame\":0") ||
            !Contains(metadata, expected_generation))
            _Exit(3);
        _Exit(0);
    }
    if (child < 0 || waitpid(child, &status, 0) != child ||
        !WIFEXITED(status) || WEXITSTATUS(status) != 0)
        goto cleanup;
    snprintf(metadata_path, sizeof(metadata_path), "%s/snapshot.bin", operator_dir);
    if (!HasSize(metadata_path, TMC_RA_SNAPSHOT_BYTES))
        goto cleanup;
    snprintf(metadata_path, sizeof(metadata_path), "%s/provenance.bin", operator_dir);
    if (!HasSize(metadata_path, TMC_RA_SNAPSHOT_BYTES))
        goto cleanup;
    snprintf(metadata_path, sizeof(metadata_path), "%s/validity.bin", operator_dir);
    if (!HasSize(metadata_path, TMC_RA_SNAPSHOT_BYTES))
        goto cleanup;
    snprintf(metadata_path, sizeof(metadata_path), "%s/requested.bin", operator_dir);
    if (!HasSize(metadata_path, TMC_RA_SNAPSHOT_BYTES))
        goto cleanup;
    snprintf(metadata_path, sizeof(metadata_path), "%s/metadata.json", operator_dir);
    if (!ReadMetadata(metadata_path, metadata, sizeof(metadata)) ||
        !Contains(metadata, "\"state\":\"S4\"") ||
        !Contains(metadata, "\"checkpoint\":\"room transition\"") ||
        !Contains(metadata, "\"phase\":\"post-evaluation\"") ||
        !Contains(metadata, "\"frame\":0") ||
        !Contains(metadata, expected_generation))
        goto cleanup;

    child = fork();
    if (child == 0) {
        char failure_metadata_path[512];

        unsetenv("TMC_RA_CAPTURE_AT_FRAME");
        unsetenv("TMC_RA_CAPTURE_STATE");
        if (!Port_ReproRaCapture_Request("S5", operator_failure_dir) ||
            Port_ReproRaCapture_Request("S5", operator_failure_dir)) {
            _Exit(2);
        }
        Port_ReproRaCapture_Tick(2, view);
        if (mkdir(operator_failure_dir, 0700) != 0)
            _Exit(3);
        Port_ReproRaCapture_Tick(3, view);
        snprintf(failure_metadata_path, sizeof(failure_metadata_path),
                 "%s/metadata.json", operator_failure_dir);
        if (access(failure_metadata_path, F_OK) == 0)
            _Exit(4);
        _Exit(0);
    }
    if (child < 0 || waitpid(child, &status, 0) != child ||
        !WIFEXITED(status) || WEXITSTATUS(status) != 0)
        goto cleanup;

    child = fork();
    if (child == 0) {
        if (chdir(temporary) != 0)
            _Exit(2);
        setenv("TMC_RA_CAPTURE_AT_FRAME", "0", 1);
        setenv("TMC_RA_CAPTURE_STATE", "S3", 1);
        Port_ReproRaCapture_Tick(0, view);
        _Exit(3);
    }
    if (child < 0 || waitpid(child, &status, 0) != child ||
        !WIFEXITED(status) || WEXITSTATUS(status) != 0)
        goto cleanup;

    snprintf(metadata_path, sizeof(metadata_path),
             "%s/artifacts/ra-local/0/metadata.json", temporary);
    if (!ReadMetadata(metadata_path, metadata, sizeof(metadata)) ||
        !Contains(metadata, "\"state\":\"S3\"") ||
        !Contains(metadata, "\"checkpoint\":\"first controllable overworld\"") ||
        !Contains(metadata, "\"phase\":\"post-evaluation\"") ||
        !Contains(metadata, "\"frame\":0") ||
        !Contains(metadata, expected_generation) ||
        !Contains(metadata, "\"rom_md5\":\"0123456789abcdef0123456789abcdef\"") ||
        !Contains(metadata, "\"manifest_sha256\":\"") ||
        !Contains(metadata, "\"requested_bytes\":1") ||
        !Contains(metadata, "\"requested_ranges\":1"))
        goto cleanup;

    child = fork();
    if (child == 0) {
        if (chdir(temporary) != 0)
            _Exit(2);
        setenv("TMC_RA_CAPTURE_AT_FRAME", "65536tail", 1);
        setenv("TMC_RA_CAPTURE_STATE", "S3", 1);
        Port_ReproRaCapture_Tick(UINT32_C(65537), view);
        _Exit(0);
    }
    if (child < 0 || waitpid(child, &status, 0) != child ||
        !WIFEXITED(status) || WEXITSTATUS(status) != 0)
        goto cleanup;
    snprintf(metadata_path, sizeof(metadata_path),
             "%s/artifacts/ra-local/65537", temporary);
    if (access(metadata_path, F_OK) == 0)
        goto cleanup;

    child = fork();
    if (child == 0) {
        if (chdir(temporary) != 0)
            _Exit(2);
        setenv("TMC_RA_CAPTURE_AT_FRAME", "65536", 1);
        setenv("TMC_RA_CAPTURE_STATE", "S3", 1);
        Port_ReproRaCapture_Tick(UINT32_C(65536), view);
        _Exit(3);
    }
    if (child < 0 || waitpid(child, &status, 0) != child ||
        !WIFEXITED(status) || WEXITSTATUS(status) != 0)
        goto cleanup;

    snprintf(metadata_path, sizeof(metadata_path),
             "%s/artifacts/ra-local/65536/metadata.json", temporary);
    if (!ReadMetadata(metadata_path, metadata, sizeof(metadata)) ||
        !Contains(metadata, "\"state\":\"S3\"") ||
        !Contains(metadata, "\"checkpoint\":\"first controllable overworld\"") ||
        !Contains(metadata, "\"phase\":\"post-evaluation\"") ||
        !Contains(metadata, "\"frame\":65536"))
        goto cleanup;

    result = 0;

cleanup:
    {
        char path[512];
        snprintf(path, sizeof(path), "%s/artifacts/ra-local/0/requested.bin", temporary);
        (void)remove(path);
        snprintf(path, sizeof(path), "%s/artifacts/ra-local/0/validity.bin", temporary);
        (void)remove(path);
        snprintf(path, sizeof(path), "%s/artifacts/ra-local/0/provenance.bin", temporary);
        (void)remove(path);
        snprintf(path, sizeof(path), "%s/artifacts/ra-local/0/snapshot.bin", temporary);
        (void)remove(path);
        snprintf(path, sizeof(path), "%s/artifacts/ra-local/0/metadata.json", temporary);
        (void)remove(path);
        snprintf(path, sizeof(path), "%s/artifacts/ra-local/0", temporary);
        (void)rmdir(path);
        snprintf(path, sizeof(path), "%s/artifacts/ra-local/65536/requested.bin", temporary);
        (void)remove(path);
        snprintf(path, sizeof(path), "%s/artifacts/ra-local/65536/validity.bin", temporary);
        (void)remove(path);
        snprintf(path, sizeof(path), "%s/artifacts/ra-local/65536/provenance.bin", temporary);
        (void)remove(path);
        snprintf(path, sizeof(path), "%s/artifacts/ra-local/65536/snapshot.bin", temporary);
        (void)remove(path);
        snprintf(path, sizeof(path), "%s/artifacts/ra-local/65536/metadata.json", temporary);
        (void)remove(path);
        snprintf(path, sizeof(path), "%s/artifacts/ra-local/65536", temporary);
        (void)rmdir(path);
        snprintf(path, sizeof(path), "%s/artifacts/ra-local", temporary);
        (void)rmdir(path);
        snprintf(path, sizeof(path), "%s/artifacts", temporary);
        (void)rmdir(path);
        snprintf(path, sizeof(path), "%s/metadata.json", operator_dir);
        (void)remove(path);
        snprintf(path, sizeof(path), "%s/requested.bin", operator_dir);
        (void)remove(path);
        snprintf(path, sizeof(path), "%s/validity.bin", operator_dir);
        (void)remove(path);
        snprintf(path, sizeof(path), "%s/provenance.bin", operator_dir);
        (void)remove(path);
        snprintf(path, sizeof(path), "%s/snapshot.bin", operator_dir);
        (void)remove(path);
        (void)rmdir(operator_dir);
        (void)rmdir(operator_state_dir);
        (void)rmdir(operator_failure_dir);
        (void)rmdir(operator_root);
        snprintf(path, sizeof(path), "%s.meta", requested_audit_path);
        (void)remove(path);
        (void)remove(requested_audit_path);
        snprintf(path, sizeof(path), "%s.provenance.bin", snapshot_audit_base);
        (void)remove(path);
        snprintf(path, sizeof(path), "%s.validity.bin", snapshot_audit_base);
        (void)remove(path);
        snprintf(path, sizeof(path), "%s.metadata.json", snapshot_audit_base);
        (void)remove(path);
        snprintf(path, sizeof(path), "%s.snapshot.bin", snapshot_audit_base);
        (void)remove(path);
    }
    (void)rmdir(temporary);
    if (result == 0)
        puts("tmc_ra_capture_contract_test: ALL PASS");
    return result;
}
