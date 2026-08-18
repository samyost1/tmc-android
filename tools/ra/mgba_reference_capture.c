/*
 * Host-only mGBA reference capture for TMC RA parity.
 *
 * ROM and save paths are caller supplied and are never echoed. Output is
 * restricted to artifacts/ra-local/, which is ignored by Git because captures
 * can contain private save and game state.
 */

#include <CommonCrypto/CommonDigest.h>
#include <mgba/core/core.h>
#include <mgba/core/log.h>
#include <mgba/gba/core.h>

#include "tmc_ra_input_replay.h"
#include "tmc_ra_memory_manifest.generated.h"

#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define SNAPSHOT_BYTES 0x58000u
#define IWRAM_BYTES 0x08000u
#define EWRAM_BYTES 0x40000u
#define SAVE_OFFSET 0x48000u
#define SAVE_BYTES 0x10000u
#define EEPROM_BYTES 0x02000u
#define IWRAM_START 0x03000000u
#define EWRAM_START 0x02000000u
#define MAX_ROM_BYTES 0x1000000u
#define OUTPUT_ROOT "artifacts/ra-local/"
#define POST_EVALUATION_PHASE "post-evaluation"
#define REFERENCE_EMULATOR "mGBA 0.10.5"
#define METADATA_VALUE_MAX 128u

enum {
    PROVENANCE_INVALID = 0,
    PROVENANCE_RAW_IWRAM = 1,
    PROVENANCE_RAW_EWRAM = 2,
    PROVENANCE_SAVE = 3,
};

typedef struct {
    uint8_t snapshot[SNAPSHOT_BYTES];
    uint8_t provenance[SNAPSHOT_BYTES];
    uint8_t validity[SNAPSHOT_BYTES];
    size_t save_bytes;
    const char* save_block;
} Capture;

static void DiscardMgbalog(struct mLogger* logger, int category, enum mLogLevel level,
                           const char* format, va_list arguments) {
    (void)logger;
    (void)category;
    (void)level;
    (void)format;
    (void)arguments;
}

static struct mLogger sDiscardMgbalog = { .log = DiscardMgbalog };

static bool IsRange(uint32_t offset, size_t bytes, uint32_t limit) {
    return offset <= limit && bytes <= limit - offset;
}

static bool MakeDirectory(const char* path) {
    return mkdir(path, 0700) == 0 || errno == EEXIST;
}

static bool WriteFile(const char* path, const void* bytes, size_t size) {
    FILE* file = fopen(path, "wb");
    bool complete;

    if (file == NULL)
        return false;
    complete = fwrite(bytes, 1, size, file) == size;
    return fclose(file) == 0 && complete;
}

static void Hex(const uint8_t* bytes, size_t size, char* out) {
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < size; ++i) {
        out[i * 2] = hex[bytes[i] >> 4];
        out[i * 2 + 1] = hex[bytes[i] & 0x0f];
    }
    out[size * 2] = '\0';
}

static bool DigestFile(const char* path, size_t expected_bytes,
                       char md5_hex[CC_MD5_DIGEST_LENGTH * 2 + 1],
                       char sha1_hex[CC_SHA1_DIGEST_LENGTH * 2 + 1],
                       char sha256_hex[CC_SHA256_DIGEST_LENGTH * 2 + 1]) {
    FILE* file = fopen(path, "rb");
    uint8_t buffer[32768];
    uint8_t md5[CC_MD5_DIGEST_LENGTH], sha1[CC_SHA1_DIGEST_LENGTH];
    uint8_t sha256[CC_SHA256_DIGEST_LENGTH];
    CC_MD5_CTX md5_context;
    CC_SHA1_CTX sha1_context;
    CC_SHA256_CTX sha256_context;
    size_t bytes = 0;

    if (file == NULL)
        return false;
    CC_MD5_Init(&md5_context);
    CC_SHA1_Init(&sha1_context);
    CC_SHA256_Init(&sha256_context);
    for (;;) {
        size_t count = fread(buffer, 1, sizeof(buffer), file);
        bytes += count;
        CC_MD5_Update(&md5_context, buffer, (CC_LONG)count);
        CC_SHA1_Update(&sha1_context, buffer, (CC_LONG)count);
        CC_SHA256_Update(&sha256_context, buffer, (CC_LONG)count);
        if (count != sizeof(buffer)) {
            if (ferror(file)) {
                fclose(file);
                return false;
            }
            break;
        }
    }
    fclose(file);
    CC_MD5_Final(md5, &md5_context);
    CC_SHA1_Final(sha1, &sha1_context);
    CC_SHA256_Final(sha256, &sha256_context);
    Hex(md5, sizeof(md5), md5_hex);
    Hex(sha1, sizeof(sha1), sha1_hex);
    Hex(sha256, sizeof(sha256), sha256_hex);
    return bytes == expected_bytes;
}

static bool VerifyRomIdentity(const char* path,
                              char md5_hex[CC_MD5_DIGEST_LENGTH * 2 + 1],
                              char sha1_hex[CC_SHA1_DIGEST_LENGTH * 2 + 1]) {
    char sha256_hex[CC_SHA256_DIGEST_LENGTH * 2 + 1];

    if (!DigestFile(path, MAX_ROM_BYTES, md5_hex, sha1_hex, sha256_hex))
        return false;
    return strcmp(md5_hex, "a104896da0047abe8bee2a6e3f4c7290") == 0 &&
           strcmp(sha1_hex, "b4bd50e4131b027c334547b4524e2dbbd4227130") == 0;
}

static bool ReadSaveIdentity(const char* path, char sha256_hex[CC_SHA256_DIGEST_LENGTH * 2 + 1]) {
    char md5_hex[CC_MD5_DIGEST_LENGTH * 2 + 1];
    char sha1_hex[CC_SHA1_DIGEST_LENGTH * 2 + 1];

    return DigestFile(path, EEPROM_BYTES, md5_hex, sha1_hex, sha256_hex);
}

static bool IsMetadataValue(const char* value) {
    size_t length;

    if (value == NULL || *value == '\0')
        return false;
    length = strlen(value);
    if (length >= METADATA_VALUE_MAX)
        return false;
    for (size_t i = 0; i < length; ++i) {
        unsigned char character = (unsigned char)value[i];
        if (character < 0x20 || character == '"' || character == '\\')
            return false;
    }
    return true;
}

static void CopyBlock(Capture* capture, uint32_t destination_offset, uint32_t destination_start,
                      uint32_t destination_size, uint32_t source_start, const uint8_t* source,
                      size_t source_size, uint8_t provenance) {
    uint64_t source_end = (uint64_t)source_start + source_size;
    uint64_t destination_end = (uint64_t)destination_start + destination_size;
    uint64_t start = source_start > destination_start ? source_start : destination_start;
    uint64_t end = source_end < destination_end ? source_end : destination_end;
    size_t bytes;
    uint32_t offset;

    if (source == NULL || start >= end)
        return;
    bytes = (size_t)(end - start);
    offset = destination_offset + (uint32_t)(start - destination_start);
    if (!IsRange(offset, bytes, SNAPSHOT_BYTES) ||
        (size_t)(start - source_start) > source_size - bytes)
        return;
    memcpy(capture->snapshot + offset, source + (start - source_start), bytes);
    memset(capture->provenance + offset, provenance, bytes);
    memset(capture->validity + offset, 1, bytes);
}

static void CopySaveBlock(Capture* capture, const uint8_t* source, size_t source_size,
                          const char* block_name) {
    size_t bytes = source_size < SAVE_BYTES ? source_size : SAVE_BYTES;

    if (source == NULL || bytes == 0 || capture->save_bytes != 0)
        return;
    memcpy(capture->snapshot + SAVE_OFFSET, source, bytes);
    memset(capture->provenance + SAVE_OFFSET, PROVENANCE_SAVE, bytes);
    memset(capture->validity + SAVE_OFFSET, 1, bytes);
    capture->save_bytes = bytes;
    capture->save_block = block_name;
}

static bool CaptureMemory(struct mCore* core, Capture* capture) {
    const struct mCoreMemoryBlock* blocks = NULL;
    size_t count = core->listMemoryBlocks(core, &blocks);

    if (blocks == NULL)
        return false;
    for (size_t i = 0; i < count; ++i) {
        size_t size = 0;
        const uint8_t* bytes = core->getMemoryBlock(core, blocks[i].id, &size);
        if (bytes == NULL || size == 0)
            continue;
        CopyBlock(capture, 0, IWRAM_START, IWRAM_BYTES, blocks[i].start, bytes, size,
                  PROVENANCE_RAW_IWRAM);
        CopyBlock(capture, IWRAM_BYTES, EWRAM_START, EWRAM_BYTES, blocks[i].start, bytes, size,
                  PROVENANCE_RAW_EWRAM);
        if (blocks[i].internalName != NULL &&
            (strcmp(blocks[i].internalName, "eeprom") == 0 ||
             strcmp(blocks[i].internalName, "sram") == 0 ||
             strcmp(blocks[i].internalName, "flash") == 0))
            CopySaveBlock(capture, bytes, size, blocks[i].internalName);
    }
    return memchr(capture->validity, 0, IWRAM_BYTES) == NULL &&
           memchr(capture->validity + IWRAM_BYTES, 0, EWRAM_BYTES) == NULL;
}

static bool WriteCapture(const char* directory, const Capture* capture, uint32_t frame,
                         const char* state, const char* checkpoint, const char* rom_md5,
                         const char* rom_sha1, const char* save_sha256) {
    char path[512];
    char metadata[512];
    int metadata_size;

    if (strncmp(directory, OUTPUT_ROOT, strlen(OUTPUT_ROOT)) != 0 ||
        strstr(directory, "..") != NULL || !MakeDirectory("artifacts") ||
        !MakeDirectory("artifacts/ra-local") || !MakeDirectory(directory))
        return false;
    if (snprintf(path, sizeof(path), "%s/snapshot.bin", directory) >= (int)sizeof(path) ||
        !WriteFile(path, capture->snapshot, sizeof(capture->snapshot)) ||
        snprintf(path, sizeof(path), "%s/provenance.bin", directory) >= (int)sizeof(path) ||
        !WriteFile(path, capture->provenance, sizeof(capture->provenance)) ||
        snprintf(path, sizeof(path), "%s/validity.bin", directory) >= (int)sizeof(path) ||
        !WriteFile(path, capture->validity, sizeof(capture->validity)))
        return false;
    metadata_size = snprintf(
        metadata, sizeof(metadata),
        "{\"format\":\"tmc-ra-capture-v1\",\"state\":\"%s\",\"checkpoint\":\"%s\","
        "\"phase\":\"" POST_EVALUATION_PHASE "\",\"frame\":%" PRIu32
        ",\"manifest_sha256\":\"%s\",\"snapshot_bytes\":%u,"
        "\"reference_emulator\":\"" REFERENCE_EMULATOR "\",\"rom_md5\":\"%s\","
        "\"rom_sha1\":\"%s\","
        "\"save_bytes\":%zu,\"save_block\":\"%s\",\"save_sha256\":\"%s\"}\n",
        state, checkpoint, frame, TMC_RA_MEMORY_MANIFEST_SHA256, SNAPSHOT_BYTES,
        rom_md5, rom_sha1, capture->save_bytes,
        capture->save_block == NULL ? "none" : capture->save_block, save_sha256);
    if (metadata_size < 0 || metadata_size >= (int)sizeof(metadata) ||
        snprintf(path, sizeof(path), "%s/metadata.json", directory) >= (int)sizeof(path))
        return false;
    return WriteFile(path, metadata, (size_t)metadata_size);
}

static int SelfTestReplayCase(const char* contents, TmcRaInputReplayError expected_error) {
    FILE* file = tmpfile();
    TmcRaInputReplay replay = {0};
    bool opened;

    if (file == NULL || fputs(contents, file) == EOF) {
        if (file != NULL)
            fclose(file);
        return 1;
    }
    opened = TmcRaInputReplay_Init(&replay, file);
    if (expected_error == TMC_RA_INPUT_REPLAY_OK) {
        if (!opened)
            return 1;
        TmcRaInputReplay_Close(&replay);
        return 0;
    }
    if (opened) {
        TmcRaInputReplay_Close(&replay);
        return 1;
    }
    return replay.error == expected_error ? 0 : 1;
}

static int SelfTestReplay(void) {
    static const char valid[] =
        TMC_RA_INPUT_REPLAY_MAGIC "\n"
        "0 0x0001\n"
        "2 0x0200\n";
    static const char boundary[] =
        TMC_RA_INPUT_REPLAY_MAGIC "\n"
        "65535 0x0001\n"
        "65536 0x0200\n";
    static const char malformed[] =
        TMC_RA_INPUT_REPLAY_MAGIC "\n"
        "0 0x0001 extra\n";
    static const char out_of_order[] =
        TMC_RA_INPUT_REPLAY_MAGIC "\n"
        "1 0\n"
        "0 0\n";
    static const char overflow[] =
        TMC_RA_INPUT_REPLAY_MAGIC "\n"
        "4294967296 0\n";
    static const char mask_overflow[] =
        TMC_RA_INPUT_REPLAY_MAGIC "\n"
        "0 0x0400\n";
    FILE* file = tmpfile();
    TmcRaInputReplay replay = {0};
    uint16_t mask;
    unsigned int frame;

    if (file == NULL || fputs(valid, file) == EOF) {
        if (file != NULL)
            fclose(file);
        return 1;
    }
    if (!TmcRaInputReplay_Init(&replay, file) ||
        !TmcRaInputReplay_Next(&replay, &mask) || mask != 0x0001 ||
        !TmcRaInputReplay_Next(&replay, &mask) || mask != 0 ||
        !TmcRaInputReplay_Next(&replay, &mask) || mask != 0x0200) {
        if (replay.file != NULL)
            TmcRaInputReplay_Close(&replay);
        return 1;
    }
    TmcRaInputReplay_Close(&replay);

    file = tmpfile();
    if (file == NULL || fputs(boundary, file) == EOF ||
        !TmcRaInputReplay_Init(&replay, file))
        return 1;
    for (frame = 0; frame < 65535; ++frame) {
        if (!TmcRaInputReplay_Next(&replay, &mask) || mask != 0)
            return 1;
    }
    if (!TmcRaInputReplay_Next(&replay, &mask) || mask != 0x0001 ||
        !TmcRaInputReplay_Next(&replay, &mask) || mask != 0x0200 ||
        replay.next_frame != UINT32_C(65537) ||
        !TmcRaInputReplay_Next(&replay, &mask) || mask != 0)
        return 1;
    TmcRaInputReplay_Close(&replay);

    if (SelfTestReplayCase(malformed, TMC_RA_INPUT_REPLAY_ERROR_MALFORMED) != 0 ||
        SelfTestReplayCase(out_of_order, TMC_RA_INPUT_REPLAY_ERROR_OUT_OF_ORDER) != 0 ||
        SelfTestReplayCase(overflow, TMC_RA_INPUT_REPLAY_ERROR_OVERFLOW) != 0 ||
        SelfTestReplayCase(mask_overflow, TMC_RA_INPUT_REPLAY_ERROR_OVERFLOW) != 0)
        return 1;
    return 0;
}

static int SelfTest(void) {
    Capture capture = {0};
    uint8_t raw[] = {0x11, 0x22};
    uint8_t save[] = {0x33, 0x44};

    CopyBlock(&capture, 0, IWRAM_START, IWRAM_BYTES, IWRAM_START + 1, raw, sizeof(raw),
              PROVENANCE_RAW_IWRAM);
    CopySaveBlock(&capture, save, sizeof(save), "eeprom");
    if (capture.snapshot[1] != 0x11 || capture.snapshot[2] != 0x22 ||
        capture.provenance[1] != PROVENANCE_RAW_IWRAM || !capture.validity[1] ||
        capture.snapshot[SAVE_OFFSET] != 0x33 || capture.save_bytes != sizeof(save) ||
        strcmp(capture.save_block, "eeprom") != 0)
        return 1;
    if (SelfTestReplay() != 0)
        return 1;
    puts("mgba_reference_capture: ALL PASS");
    return 0;
}

static void Usage(const char* name) {
    fprintf(stderr, "usage: %s --rom FILE --save FILE --frames N "
                    "--state ID --checkpoint NAME "
                    "--output artifacts/ra-local/DIR [--input-replay FILE] | --self-test\n", name);
}

int main(int argc, char** argv) {
    const char* rom = NULL;
    const char* save = NULL;
    const char* output = NULL;
    const char* input_replay = NULL;
    const char* state = NULL;
    const char* checkpoint = NULL;
    char* end = NULL;
    char rom_sha1[CC_SHA1_DIGEST_LENGTH * 2 + 1];
    char rom_md5[CC_MD5_DIGEST_LENGTH * 2 + 1];
    char save_sha256[CC_SHA256_DIGEST_LENGTH * 2 + 1];
    unsigned long frames = 0;
    bool frames_set = false;
    bool state_set = false;
    bool checkpoint_set = false;
    struct mCore* core = NULL;
    color_t* video = NULL;
    unsigned width = 0, height = 0;
    Capture capture = {0};
    TmcRaInputReplay replay = {0};
    bool replay_set = false;
    int result = 1;

    if (argc == 2 && strcmp(argv[1], "--self-test") == 0)
        return SelfTest();
    mLogSetDefaultLogger(&sDiscardMgbalog);
    for (int i = 1; i < argc; ++i) {
        if (i + 1 < argc && strcmp(argv[i], "--rom") == 0)
            rom = argv[++i];
        else if (i + 1 < argc && strcmp(argv[i], "--save") == 0)
            save = argv[++i];
        else if (i + 1 < argc && strcmp(argv[i], "--frames") == 0) {
            frames = strtoul(argv[++i], &end, 10);
            if (*argv[i] == '\0' || *end != '\0' || frames > UINT32_MAX)
                return 2;
            frames_set = true;
        } else if (i + 1 < argc && strcmp(argv[i], "--output") == 0)
            output = argv[++i];
        else if (i + 1 < argc && strcmp(argv[i], "--input-replay") == 0)
            input_replay = argv[++i];
        else if (i + 1 < argc && strcmp(argv[i], "--state") == 0) {
            state = argv[++i];
            state_set = true;
        } else if (i + 1 < argc && strcmp(argv[i], "--checkpoint") == 0) {
            checkpoint = argv[++i];
            checkpoint_set = true;
        }
        else {
            Usage(argv[0]);
            return 2;
        }
    }
    if (rom == NULL || save == NULL || output == NULL || !frames_set || !state_set ||
        !checkpoint_set || !IsMetadataValue(state) || !IsMetadataValue(checkpoint)) {
        Usage(argv[0]);
        return 2;
    }
    if (!VerifyRomIdentity(rom, rom_md5, rom_sha1)) {
        fputs("error: ROM is missing, unreadable, or not the required public identity\n", stderr);
        return 1;
    }
    if (!ReadSaveIdentity(save, save_sha256)) {
        fputs("error: save is missing, unreadable, or not exactly 8192 bytes\n", stderr);
        return 1;
    }
    core = GBACoreCreate();
    if (core == NULL) {
        fputs("error: mGBA core initialization failed\n", stderr);
        return 1;
    }
    mCoreInitConfig(core, "tmc-ra-reference");
    mCoreConfigLoadDefaults(&core->config, &(struct mCoreOptions){0});
    if (!core->init(core) || !mCoreLoadFile(core, rom)) {
        fputs("error: mGBA could not initialize or load the verified ROM\n", stderr);
        goto cleanup;
    }
    core->desiredVideoDimensions(core, &width, &height);
    video = calloc((size_t)width * height, sizeof(*video));
    if (video == NULL) {
        fputs("error: mGBA video buffer initialization failed\n", stderr);
        goto cleanup;
    }
    core->setVideoBuffer(core, video, width);
    mCoreLoadForeignConfig(core, &core->config);
    if (!mCoreLoadSaveFile(core, save, true)) {
        fputs("error: mGBA could not load the explicit temporary save\n", stderr);
        goto cleanup;
    }
    if (input_replay != NULL) {
        if (!TmcRaInputReplay_Open(&replay, input_replay)) {
            fprintf(stderr, "error: input replay rejected (%s)\n",
                    TmcRaInputReplay_ErrorString(replay.error));
            goto cleanup;
        }
        replay_set = true;
    }
    core->reset(core);
    /*
     * The native port's first KEYINPUT poll happens after its initial game
     * tick. Keep that zero-key warm-up when replay is enabled so replay
     * record 0 drives native/reference frame 2 in the same way.
     */
    if (replay_set && frames != 0) {
        core->setKeys(core, 0);
        core->runFrame(core);
    }
    for (unsigned long frame = replay_set ? 1 : 0; frame < frames; ++frame) {
        uint16_t pressed_mask = 0;
        if (replay_set && !TmcRaInputReplay_Next(&replay, &pressed_mask)) {
            fprintf(stderr, "error: input replay failed while reading (%s)\n",
                    TmcRaInputReplay_ErrorString(replay.error));
            goto cleanup;
        }
        core->setKeys(core, replay_set ? pressed_mask : 0);
        core->runFrame(core);
    }
    if (!CaptureMemory(core, &capture) ||
        !WriteCapture(output, &capture, (uint32_t)frames, state, checkpoint, rom_md5,
                      rom_sha1, save_sha256)) {
        fputs("error: mGBA reference capture failed\n", stderr);
        goto cleanup;
    }
    printf("mGBA reference capture: state %s, checkpoint %s, frame %lu, save bytes %zu\n",
           state, checkpoint, frames, capture.save_bytes);
    result = 0;

cleanup:
    TmcRaInputReplay_Close(&replay);
    if (core != NULL) {
        core->unloadROM(core);
        core->deinit(core);
        mCoreConfigDeinit(&core->config);
    }
    free(video);
    return result;
}
