#ifndef NATIVE_RA_OUTBOX_JOURNAL_H
#define NATIVE_RA_OUTBOX_JOURNAL_H

#include "native_ra_outbox.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NRA_OUTBOX_JOURNAL_VERSION 1u
#define NRA_OUTBOX_JOURNAL_HEADER_SIZE 24u
#define NRA_OUTBOX_JOURNAL_RECORD_HEADER_SIZE 68u
#define NRA_OUTBOX_JOURNAL_MAX_TOTAL_BYTES (512u * 1024u)
#define NRA_OUTBOX_JOURNAL_MAX_RECORDS 1024u
#define NRA_OUTBOX_JOURNAL_MAX_ACCOUNT_BYTES 256u
#define NRA_OUTBOX_JOURNAL_MAX_URL_BYTES 2048u
#define NRA_OUTBOX_JOURNAL_MAX_CONTENT_TYPE_BYTES 128u
#define NRA_OUTBOX_JOURNAL_MAX_POST_BYTES (64u * 1024u)
#define NRA_OUTBOX_JOURNAL_DEDUPE_KEY_SIZE 32u

typedef enum NRA_OutboxJournalStatus {
    NRA_OUTBOX_JOURNAL_PENDING = 0,
    NRA_OUTBOX_JOURNAL_CONFIRMED = 1
} NRA_OutboxJournalStatus;

typedef struct NRA_OutboxJournalRecord {
    uint64_t sequence;
    uint32_t game_id;
    uint32_t console_id;
    NRA_OutboxKind kind;
    NRA_OutboxJournalStatus status;
    const uint8_t* account;
    size_t account_size;
    const uint8_t* dedupe_key;
    size_t dedupe_key_size;
    const uint8_t* url;
    size_t url_size;
    const uint8_t* content_type;
    size_t content_type_size;
    const uint8_t* post;
    size_t post_size;
} NRA_OutboxJournalRecord;

typedef enum NRA_OutboxJournalResult {
    NRA_OUTBOX_JOURNAL_OK = 0,
    NRA_OUTBOX_JOURNAL_INVALID_ARGUMENT,
    NRA_OUTBOX_JOURNAL_TRUNCATED,
    NRA_OUTBOX_JOURNAL_UNSUPPORTED_VERSION,
    NRA_OUTBOX_JOURNAL_MALFORMED,
    NRA_OUTBOX_JOURNAL_CRC_MISMATCH,
    NRA_OUTBOX_JOURNAL_DUPLICATE_SEQUENCE,
    NRA_OUTBOX_JOURNAL_DUPLICATE_DEDUPE,
    NRA_OUTBOX_JOURNAL_NOT_FOUND,
    NRA_OUTBOX_JOURNAL_BUFFER_TOO_SMALL,
    NRA_OUTBOX_JOURNAL_LIMIT,
    NRA_OUTBOX_JOURNAL_OVERFLOW,
    NRA_OUTBOX_JOURNAL_NOT_CONFIRMED
} NRA_OutboxJournalResult;

NRA_OutboxJournalResult nra_outbox_journal_encoded_size(const NRA_OutboxJournalRecord* records,
                                                         size_t record_count, size_t* encoded_size);

NRA_OutboxJournalResult nra_outbox_journal_encode(uint8_t* buffer, size_t capacity,
                                                   const NRA_OutboxJournalRecord* records,
                                                   size_t record_count, size_t* encoded_size);

NRA_OutboxJournalResult nra_outbox_journal_validate(const uint8_t* buffer, size_t size,
                                                     size_t* record_count);

NRA_OutboxJournalResult nra_outbox_journal_decode(const uint8_t* buffer, size_t size,
                                                   NRA_OutboxJournalRecord* records,
                                                   size_t record_capacity, size_t* record_count);

NRA_OutboxJournalResult nra_outbox_journal_append(uint8_t* buffer, size_t capacity, size_t size,
                                                   const NRA_OutboxJournalRecord* record,
                                                   size_t* new_size);

NRA_OutboxJournalResult nra_outbox_journal_mark_confirmed(uint8_t* buffer, size_t size,
                                                           uint64_t sequence);

NRA_OutboxJournalResult nra_outbox_journal_remove_confirmed(uint8_t* buffer, size_t size,
                                                             uint64_t sequence, size_t* new_size);

#ifdef __cplusplus
}
#endif

#endif
