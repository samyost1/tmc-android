#ifndef TMC_RA_INPUT_REPLAY_H
#define TMC_RA_INPUT_REPLAY_H

/*
 * Shared host/native input replay format:
 *
 *   TMC_RA_INPUT_REPLAY_V1
 *   <frame-index> <pressed-GBA-mask>
 *
 * Frame indexes are decimal uint32 values and masks are decimal or 0x-prefixed
 * hexadecimal values. A mask bit is set when that GBA button is pressed:
 * bit 0=A, 1=B, 2=Select, 3=Start, 4=Right, 5=Left, 6=Up, 7=Down,
 * 8=R, and 9=L. Records must be strictly increasing. Missing frame indexes
 * produce a zero mask. The whole file is validated before the first frame is
 * consumed so malformed, out-of-order, or overflowing input fails closed.
 *
 * This is header-only so the native xmake graph does not need a production
 * target edit just to share the parser with the standalone mGBA tool.
 */

#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define TMC_RA_INPUT_REPLAY_MAGIC "TMC_RA_INPUT_REPLAY_V1"
#define TMC_RA_INPUT_REPLAY_GBA_MASK UINT16_C(0x03ff)
#define TMC_RA_INPUT_REPLAY_LINE_BYTES 256u

typedef enum {
    TMC_RA_INPUT_REPLAY_OK = 0,
    TMC_RA_INPUT_REPLAY_ERROR_FILE,
    TMC_RA_INPUT_REPLAY_ERROR_EMPTY,
    TMC_RA_INPUT_REPLAY_ERROR_HEADER,
    TMC_RA_INPUT_REPLAY_ERROR_LINE_TOO_LONG,
    TMC_RA_INPUT_REPLAY_ERROR_MALFORMED,
    TMC_RA_INPUT_REPLAY_ERROR_OUT_OF_ORDER,
    TMC_RA_INPUT_REPLAY_ERROR_OVERFLOW,
    TMC_RA_INPUT_REPLAY_ERROR_IO,
} TmcRaInputReplayError;

typedef struct {
    FILE* file;
    uint32_t next_frame;
    uint32_t record_frame;
    uint16_t record_mask;
    bool next_frame_valid;
    bool has_record;
    bool has_last_record;
    TmcRaInputReplayError error;
} TmcRaInputReplay;

static const char* TmcRaInputReplay_ErrorString(TmcRaInputReplayError error) {
    switch (error) {
        case TMC_RA_INPUT_REPLAY_OK:
            return "ok";
        case TMC_RA_INPUT_REPLAY_ERROR_FILE:
            return "file";
        case TMC_RA_INPUT_REPLAY_ERROR_EMPTY:
            return "empty";
        case TMC_RA_INPUT_REPLAY_ERROR_HEADER:
            return "header";
        case TMC_RA_INPUT_REPLAY_ERROR_LINE_TOO_LONG:
            return "line-too-long";
        case TMC_RA_INPUT_REPLAY_ERROR_MALFORMED:
            return "malformed";
        case TMC_RA_INPUT_REPLAY_ERROR_OUT_OF_ORDER:
            return "out-of-order";
        case TMC_RA_INPUT_REPLAY_ERROR_OVERFLOW:
            return "overflow";
        case TMC_RA_INPUT_REPLAY_ERROR_IO:
            return "io";
    }
    return "unknown";
}

static TmcRaInputReplayError TmcRaInputReplay_ReadLine(
    FILE* file, char line[TMC_RA_INPUT_REPLAY_LINE_BYTES], bool* eof) {
    if (fgets(line, (int)TMC_RA_INPUT_REPLAY_LINE_BYTES, file) == NULL) {
        if (ferror(file))
            return TMC_RA_INPUT_REPLAY_ERROR_IO;
        *eof = true;
        return TMC_RA_INPUT_REPLAY_OK;
    }
    *eof = false;
    if (strchr(line, '\n') == NULL && !feof(file))
        return TMC_RA_INPUT_REPLAY_ERROR_LINE_TOO_LONG;
    return TMC_RA_INPUT_REPLAY_OK;
}

static void TmcRaInputReplay_TrimLineEnd(char* line) {
    size_t length = strlen(line);
    while (length != 0 && (line[length - 1] == '\n' || line[length - 1] == '\r'))
        line[--length] = '\0';
}

static TmcRaInputReplayError TmcRaInputReplay_ReadHeader(FILE* file) {
    char line[TMC_RA_INPUT_REPLAY_LINE_BYTES];
    bool eof = false;
    TmcRaInputReplayError error = TmcRaInputReplay_ReadLine(file, line, &eof);

    if (error != TMC_RA_INPUT_REPLAY_OK)
        return error;
    if (eof)
        return TMC_RA_INPUT_REPLAY_ERROR_EMPTY;
    TmcRaInputReplay_TrimLineEnd(line);
    return strcmp(line, TMC_RA_INPUT_REPLAY_MAGIC) == 0
               ? TMC_RA_INPUT_REPLAY_OK
               : TMC_RA_INPUT_REPLAY_ERROR_HEADER;
}

static int TmcRaInputReplay_Digit(char character) {
    if (character >= '0' && character <= '9')
        return character - '0';
    if (character >= 'a' && character <= 'f')
        return character - 'a' + 10;
    if (character >= 'A' && character <= 'F')
        return character - 'A' + 10;
    return -1;
}

static TmcRaInputReplayError TmcRaInputReplay_ParseUnsigned(
    const char* token, unsigned base, uint64_t maximum, uint64_t* value) {
    uint64_t parsed = 0;
    bool digit_seen = false;

    if (token == NULL || *token == '\0' || *token == '-' || *token == '+')
        return TMC_RA_INPUT_REPLAY_ERROR_MALFORMED;
    if (base == 0) {
        if (token[0] == '0' && (token[1] == 'x' || token[1] == 'X')) {
            base = 16;
            token += 2;
        } else {
            base = 10;
        }
    }
    if (*token == '\0')
        return TMC_RA_INPUT_REPLAY_ERROR_MALFORMED;
    while (*token != '\0') {
        int digit = TmcRaInputReplay_Digit(*token++);
        if (digit < 0 || (unsigned)digit >= base)
            return TMC_RA_INPUT_REPLAY_ERROR_MALFORMED;
        digit_seen = true;
        if (parsed > (maximum - (uint64_t)digit) / base)
            return TMC_RA_INPUT_REPLAY_ERROR_OVERFLOW;
        parsed = parsed * base + (uint64_t)digit;
    }
    if (!digit_seen)
        return TMC_RA_INPUT_REPLAY_ERROR_MALFORMED;
    *value = parsed;
    return TMC_RA_INPUT_REPLAY_OK;
}

/*
 * Returns 1 for a record, 0 for a blank line, and -1 for an invalid line.
 * Tokens are split in place so no unbounded string allocation is needed.
 */
static int TmcRaInputReplay_ParseRecord(
    char* line, uint32_t* frame, uint16_t* mask, TmcRaInputReplayError* error) {
    char* first;
    char* second;
    char* cursor;
    uint64_t parsed_frame;
    uint64_t parsed_mask;
    TmcRaInputReplayError parse_error;

    TmcRaInputReplay_TrimLineEnd(line);
    cursor = line;
    while (*cursor != '\0' && isspace((unsigned char)*cursor))
        ++cursor;
    if (*cursor == '\0')
        return 0;

    first = cursor;
    while (*cursor != '\0' && !isspace((unsigned char)*cursor))
        ++cursor;
    if (*cursor != '\0')
        *cursor++ = '\0';
    while (*cursor != '\0' && isspace((unsigned char)*cursor))
        ++cursor;
    if (*cursor == '\0') {
        *error = TMC_RA_INPUT_REPLAY_ERROR_MALFORMED;
        return -1;
    }

    second = cursor;
    while (*cursor != '\0' && !isspace((unsigned char)*cursor))
        ++cursor;
    if (*cursor != '\0')
        *cursor++ = '\0';
    while (*cursor != '\0' && isspace((unsigned char)*cursor))
        ++cursor;
    if (*cursor != '\0') {
        *error = TMC_RA_INPUT_REPLAY_ERROR_MALFORMED;
        return -1;
    }

    parse_error = TmcRaInputReplay_ParseUnsigned(first, 10, UINT32_MAX, &parsed_frame);
    if (parse_error != TMC_RA_INPUT_REPLAY_OK) {
        *error = parse_error;
        return -1;
    }
    parse_error = TmcRaInputReplay_ParseUnsigned(
        second, 0, TMC_RA_INPUT_REPLAY_GBA_MASK, &parsed_mask);
    if (parse_error != TMC_RA_INPUT_REPLAY_OK) {
        *error = parse_error;
        return -1;
    }
    *frame = (uint32_t)parsed_frame;
    *mask = (uint16_t)parsed_mask;
    return 1;
}

static TmcRaInputReplayError TmcRaInputReplay_Validate(FILE* file) {
    char line[TMC_RA_INPUT_REPLAY_LINE_BYTES];
    bool eof = false;
    bool has_last = false;
    uint32_t last_frame = 0;
    TmcRaInputReplayError error;

    error = TmcRaInputReplay_ReadHeader(file);
    if (error != TMC_RA_INPUT_REPLAY_OK)
        return error;
    for (;;) {
        int parsed;
        uint32_t frame;
        uint16_t mask;

        error = TmcRaInputReplay_ReadLine(file, line, &eof);
        if (error != TMC_RA_INPUT_REPLAY_OK)
            return error;
        if (eof)
            return TMC_RA_INPUT_REPLAY_OK;
        parsed = TmcRaInputReplay_ParseRecord(line, &frame, &mask, &error);
        (void)mask;
        if (parsed < 0)
            return error;
        if (parsed == 0)
            continue;
        if (has_last && frame <= last_frame)
            return TMC_RA_INPUT_REPLAY_ERROR_OUT_OF_ORDER;
        last_frame = frame;
        has_last = true;
    }
}

static TmcRaInputReplayError TmcRaInputReplay_LoadNextRecord(TmcRaInputReplay* replay) {
    char line[TMC_RA_INPUT_REPLAY_LINE_BYTES];
    bool eof = false;
    TmcRaInputReplayError error;

    for (;;) {
        int parsed;
        uint32_t frame;
        uint16_t mask;

        error = TmcRaInputReplay_ReadLine(replay->file, line, &eof);
        if (error != TMC_RA_INPUT_REPLAY_OK)
            return error;
        if (eof) {
            replay->has_record = false;
            return TMC_RA_INPUT_REPLAY_OK;
        }
        parsed = TmcRaInputReplay_ParseRecord(line, &frame, &mask, &error);
        if (parsed < 0)
            return error;
        if (parsed == 0)
            continue;
        if (replay->has_last_record && frame <= replay->record_frame)
            return TMC_RA_INPUT_REPLAY_ERROR_OUT_OF_ORDER;
        replay->record_frame = frame;
        replay->record_mask = mask;
        replay->has_last_record = true;
        replay->has_record = true;
        return TMC_RA_INPUT_REPLAY_OK;
    }
}

static bool TmcRaInputReplay_Init(TmcRaInputReplay* replay, FILE* file) {
    TmcRaInputReplayError error;

    if (replay == NULL)
        return false;
    memset(replay, 0, sizeof(*replay));
    replay->error = TMC_RA_INPUT_REPLAY_ERROR_FILE;
    if (file == NULL)
        return false;
    replay->file = file;
    clearerr(file);
    if (fseek(file, 0, SEEK_SET) != 0)
        goto fail;
    error = TmcRaInputReplay_Validate(file);
    if (error != TMC_RA_INPUT_REPLAY_OK) {
        replay->error = error;
        goto fail;
    }
    clearerr(file);
    if (fseek(file, 0, SEEK_SET) != 0)
        goto fail;
    error = TmcRaInputReplay_ReadHeader(file);
    if (error != TMC_RA_INPUT_REPLAY_OK) {
        replay->error = error;
        goto fail;
    }
    replay->next_frame_valid = true;
    replay->error = TmcRaInputReplay_LoadNextRecord(replay);
    if (replay->error != TMC_RA_INPUT_REPLAY_OK)
        goto fail;
    return true;

fail:
    fclose(file);
    replay->file = NULL;
    return false;
}

static bool TmcRaInputReplay_Open(TmcRaInputReplay* replay, const char* path) {
    FILE* file;

    if (replay == NULL)
        return false;
    memset(replay, 0, sizeof(*replay));
    replay->error = TMC_RA_INPUT_REPLAY_ERROR_FILE;
    if (path == NULL || *path == '\0')
        return false;
    file = fopen(path, "rb");
    if (file == NULL)
        return false;
    return TmcRaInputReplay_Init(replay, file);
}

static void TmcRaInputReplay_Close(TmcRaInputReplay* replay) {
    if (replay == NULL)
        return;
    if (replay->file != NULL)
        fclose(replay->file);
    replay->file = NULL;
    replay->has_record = false;
    replay->next_frame_valid = false;
}

static bool TmcRaInputReplay_Next(TmcRaInputReplay* replay, uint16_t* pressed_mask) {
    uint32_t frame;
    TmcRaInputReplayError error;

    if (replay == NULL || replay->file == NULL || replay->error != TMC_RA_INPUT_REPLAY_OK ||
        pressed_mask == NULL)
        return false;
    if (!replay->next_frame_valid) {
        replay->error = TMC_RA_INPUT_REPLAY_ERROR_OVERFLOW;
        return false;
    }
    frame = replay->next_frame;
    *pressed_mask = 0;
    if (replay->has_record && replay->record_frame < frame) {
        replay->error = TMC_RA_INPUT_REPLAY_ERROR_OUT_OF_ORDER;
        return false;
    }
    if (replay->has_record && replay->record_frame == frame) {
        *pressed_mask = replay->record_mask;
        error = TmcRaInputReplay_LoadNextRecord(replay);
        if (error != TMC_RA_INPUT_REPLAY_OK) {
            replay->error = error;
            return false;
        }
    }
    if (frame == UINT32_MAX)
        replay->next_frame_valid = false;
    else
        replay->next_frame++;
    return true;
}

#endif
