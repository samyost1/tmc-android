#include "native_ra_outbox_journal.h"

#include <string.h>

enum {
    JOURNAL_MAGIC_OFFSET = 0,
    JOURNAL_VERSION_OFFSET = 4,
    JOURNAL_HEADER_SIZE_OFFSET = 6,
    JOURNAL_TOTAL_SIZE_OFFSET = 8,
    JOURNAL_RECORD_COUNT_OFFSET = 12,
    JOURNAL_FLAGS_OFFSET = 16,
    JOURNAL_CRC_OFFSET = 20,

    RECORD_SIZE_OFFSET = 0,
    RECORD_SEQUENCE_OFFSET = 4,
    RECORD_GAME_ID_OFFSET = 12,
    RECORD_CONSOLE_ID_OFFSET = 16,
    RECORD_KIND_OFFSET = 20,
    RECORD_STATUS_OFFSET = 21,
    RECORD_FLAGS_OFFSET = 22,
    RECORD_ACCOUNT_SIZE_OFFSET = 24,
    RECORD_URL_SIZE_OFFSET = 26,
    RECORD_CONTENT_TYPE_SIZE_OFFSET = 28,
    RECORD_RESERVED_OFFSET = 30,
    RECORD_POST_SIZE_OFFSET = 32,
    RECORD_DEDUPE_KEY_OFFSET = 36,
    RECORD_PAYLOAD_OFFSET = NRA_OUTBOX_JOURNAL_RECORD_HEADER_SIZE
};

static const uint8_t JOURNAL_MAGIC[4] = {'N', 'R', 'A', 'J'};

static uint16_t read_le16(const uint8_t* source) {
    return (uint16_t)source[0] | (uint16_t)((uint16_t)source[1] << 8);
}

static uint32_t read_le32(const uint8_t* source) {
    return (uint32_t)source[0] | ((uint32_t)source[1] << 8) |
        ((uint32_t)source[2] << 16) | ((uint32_t)source[3] << 24);
}

static uint64_t read_le64(const uint8_t* source) {
    return (uint64_t)source[0] | ((uint64_t)source[1] << 8) |
        ((uint64_t)source[2] << 16) | ((uint64_t)source[3] << 24) |
        ((uint64_t)source[4] << 32) | ((uint64_t)source[5] << 40) |
        ((uint64_t)source[6] << 48) | ((uint64_t)source[7] << 56);
}

static void write_le16(uint8_t* destination, uint16_t value) {
    destination[0] = (uint8_t)value;
    destination[1] = (uint8_t)(value >> 8);
}

static void write_le32(uint8_t* destination, uint32_t value) {
    destination[0] = (uint8_t)value;
    destination[1] = (uint8_t)(value >> 8);
    destination[2] = (uint8_t)(value >> 16);
    destination[3] = (uint8_t)(value >> 24);
}

static void write_le64(uint8_t* destination, uint64_t value) {
    destination[0] = (uint8_t)value;
    destination[1] = (uint8_t)(value >> 8);
    destination[2] = (uint8_t)(value >> 16);
    destination[3] = (uint8_t)(value >> 24);
    destination[4] = (uint8_t)(value >> 32);
    destination[5] = (uint8_t)(value >> 40);
    destination[6] = (uint8_t)(value >> 48);
    destination[7] = (uint8_t)(value >> 56);
}

static bool size_add(size_t left, size_t right, size_t* result) {
    if (right > SIZE_MAX - left) return false;
    *result = left + right;
    return true;
}

static bool contains_nul(const uint8_t* bytes, size_t size) {
    size_t i;

    for (i = 0; i < size; ++i) {
        if (bytes[i] == 0) return true;
    }
    return false;
}

static uint32_t crc32_update(uint32_t crc, uint8_t byte) {
    uint32_t bit;

    crc ^= byte;
    for (bit = 0; bit < 8; ++bit) {
        if ((crc & 1u) != 0u)
            crc = (crc >> 1) ^ UINT32_C(0xedb88320);
        else
            crc >>= 1;
    }
    return crc;
}

static uint32_t journal_crc32(const uint8_t* buffer, size_t size) {
    uint32_t crc = UINT32_C(0xffffffff);
    size_t i;

    for (i = 0; i < size; ++i) {
        uint8_t byte = (i >= JOURNAL_CRC_OFFSET && i < JOURNAL_CRC_OFFSET + sizeof(uint32_t))
            ? 0
            : buffer[i];
        crc = crc32_update(crc, byte);
    }
    return ~crc;
}

static NRA_OutboxJournalResult validate_record(const NRA_OutboxJournalRecord* record,
                                                bool require_pending) {
    if (record == NULL || record->sequence == 0 || record->game_id == 0 || record->console_id == 0 ||
        (record->kind != NRA_OUTBOX_ACHIEVEMENT && record->kind != NRA_OUTBOX_LEADERBOARD) ||
        (record->status != NRA_OUTBOX_JOURNAL_PENDING && record->status != NRA_OUTBOX_JOURNAL_CONFIRMED) ||
        (require_pending && record->status != NRA_OUTBOX_JOURNAL_PENDING))
        return require_pending && record != NULL && record->status != NRA_OUTBOX_JOURNAL_PENDING
            ? NRA_OUTBOX_JOURNAL_NOT_CONFIRMED
            : NRA_OUTBOX_JOURNAL_INVALID_ARGUMENT;
    if (record->account == NULL || record->account_size == 0 ||
        record->account_size > NRA_OUTBOX_JOURNAL_MAX_ACCOUNT_BYTES ||
        record->url == NULL || record->url_size == 0 ||
        record->url_size > NRA_OUTBOX_JOURNAL_MAX_URL_BYTES ||
        record->content_type == NULL || record->content_type_size == 0 ||
        record->content_type_size > NRA_OUTBOX_JOURNAL_MAX_CONTENT_TYPE_BYTES ||
        record->post == NULL || record->post_size == 0 ||
        record->post_size > NRA_OUTBOX_JOURNAL_MAX_POST_BYTES ||
        record->dedupe_key == NULL || record->dedupe_key_size != NRA_OUTBOX_JOURNAL_DEDUPE_KEY_SIZE)
        return (record != NULL && record->dedupe_key_size != NRA_OUTBOX_JOURNAL_DEDUPE_KEY_SIZE)
            ? NRA_OUTBOX_JOURNAL_INVALID_ARGUMENT
            : NRA_OUTBOX_JOURNAL_LIMIT;
    if (contains_nul(record->account, record->account_size) ||
        contains_nul(record->url, record->url_size) ||
        contains_nul(record->content_type, record->content_type_size) ||
        contains_nul(record->post, record->post_size))
        return NRA_OUTBOX_JOURNAL_MALFORMED;
    {
        size_t i;
        for (i = 0; i < record->dedupe_key_size; ++i) {
            if (record->dedupe_key[i] != 0) return NRA_OUTBOX_JOURNAL_OK;
        }
    }
    return NRA_OUTBOX_JOURNAL_MALFORMED;
}

static NRA_OutboxJournalResult record_encoded_size(const NRA_OutboxJournalRecord* record,
                                                    bool require_pending, size_t* size) {
    NRA_OutboxJournalResult result;
    size_t payload_size;

    if (size == NULL) return NRA_OUTBOX_JOURNAL_INVALID_ARGUMENT;
    *size = 0;
    result = validate_record(record, require_pending);
    if (result != NRA_OUTBOX_JOURNAL_OK) return result;
    if (!size_add(record->account_size, record->url_size, &payload_size) ||
        !size_add(payload_size, record->content_type_size, &payload_size) ||
        !size_add(payload_size, record->post_size, &payload_size) ||
        !size_add(NRA_OUTBOX_JOURNAL_RECORD_HEADER_SIZE, payload_size, size))
        return NRA_OUTBOX_JOURNAL_OVERFLOW;
    if (*size > UINT32_MAX) return NRA_OUTBOX_JOURNAL_OVERFLOW;
    return NRA_OUTBOX_JOURNAL_OK;
}

static bool same_dedupe_key(const NRA_OutboxJournalRecord* left,
                            const NRA_OutboxJournalRecord* right) {
    return left->dedupe_key_size == right->dedupe_key_size &&
        memcmp(left->dedupe_key, right->dedupe_key, left->dedupe_key_size) == 0;
}

static NRA_OutboxJournalResult validate_record_list(const NRA_OutboxJournalRecord* records,
                                                     size_t record_count, size_t* encoded_size) {
    size_t total_size = NRA_OUTBOX_JOURNAL_HEADER_SIZE;
    size_t i;
    uint64_t previous_sequence = 0;

    if (encoded_size == NULL) return NRA_OUTBOX_JOURNAL_INVALID_ARGUMENT;
    *encoded_size = 0;
    if (record_count > NRA_OUTBOX_JOURNAL_MAX_RECORDS)
        return NRA_OUTBOX_JOURNAL_LIMIT;
    if (records == NULL && record_count != 0)
        return NRA_OUTBOX_JOURNAL_INVALID_ARGUMENT;
    for (i = 0; i < record_count; ++i) {
        size_t current_size;
        NRA_OutboxJournalResult result = record_encoded_size(&records[i], false, &current_size);
        if (result != NRA_OUTBOX_JOURNAL_OK) return result;
        if (records[i].sequence <= previous_sequence)
            return records[i].sequence == previous_sequence
                ? NRA_OUTBOX_JOURNAL_DUPLICATE_SEQUENCE
                : NRA_OUTBOX_JOURNAL_MALFORMED;
        previous_sequence = records[i].sequence;
        {
            size_t prior;
            for (prior = 0; prior < i; ++prior) {
                if (same_dedupe_key(&records[prior], &records[i]))
                    return NRA_OUTBOX_JOURNAL_DUPLICATE_DEDUPE;
            }
        }
        if (!size_add(total_size, current_size, &total_size))
            return NRA_OUTBOX_JOURNAL_OVERFLOW;
        if (total_size > NRA_OUTBOX_JOURNAL_MAX_TOTAL_BYTES)
            return NRA_OUTBOX_JOURNAL_LIMIT;
    }
    if (total_size > UINT32_MAX) return NRA_OUTBOX_JOURNAL_OVERFLOW;
    *encoded_size = total_size;
    return NRA_OUTBOX_JOURNAL_OK;
}

static NRA_OutboxJournalResult parsed_record(const uint8_t* buffer, size_t record_offset,
                                              NRA_OutboxJournalRecord* record, size_t* record_size) {
    size_t size;
    size_t payload_size;
    size_t account_size;
    size_t url_size;
    size_t content_type_size;
    size_t post_size;
    const uint8_t* payload;

    size = read_le32(buffer + record_offset + RECORD_SIZE_OFFSET);
    if (size < NRA_OUTBOX_JOURNAL_RECORD_HEADER_SIZE) return NRA_OUTBOX_JOURNAL_MALFORMED;
    account_size = read_le16(buffer + record_offset + RECORD_ACCOUNT_SIZE_OFFSET);
    url_size = read_le16(buffer + record_offset + RECORD_URL_SIZE_OFFSET);
    content_type_size = read_le16(buffer + record_offset + RECORD_CONTENT_TYPE_SIZE_OFFSET);
    post_size = read_le32(buffer + record_offset + RECORD_POST_SIZE_OFFSET);
    if (account_size == 0 || account_size > NRA_OUTBOX_JOURNAL_MAX_ACCOUNT_BYTES ||
        url_size == 0 || url_size > NRA_OUTBOX_JOURNAL_MAX_URL_BYTES ||
        content_type_size == 0 || content_type_size > NRA_OUTBOX_JOURNAL_MAX_CONTENT_TYPE_BYTES ||
        post_size == 0 || post_size > NRA_OUTBOX_JOURNAL_MAX_POST_BYTES)
        return NRA_OUTBOX_JOURNAL_LIMIT;
    if (!size_add(account_size, url_size, &payload_size) ||
        !size_add(payload_size, content_type_size, &payload_size) ||
        !size_add(payload_size, post_size, &payload_size) ||
        !size_add(NRA_OUTBOX_JOURNAL_RECORD_HEADER_SIZE, payload_size, &payload_size))
        return NRA_OUTBOX_JOURNAL_OVERFLOW;
    if (payload_size != size) return NRA_OUTBOX_JOURNAL_MALFORMED;
    if (read_le16(buffer + record_offset + RECORD_FLAGS_OFFSET) != 0 ||
        read_le16(buffer + record_offset + RECORD_RESERVED_OFFSET) != 0 ||
        read_le64(buffer + record_offset + RECORD_SEQUENCE_OFFSET) == 0 ||
        read_le32(buffer + record_offset + RECORD_GAME_ID_OFFSET) == 0 ||
        read_le32(buffer + record_offset + RECORD_CONSOLE_ID_OFFSET) == 0 ||
        (buffer[record_offset + RECORD_KIND_OFFSET] != NRA_OUTBOX_ACHIEVEMENT &&
         buffer[record_offset + RECORD_KIND_OFFSET] != NRA_OUTBOX_LEADERBOARD) ||
        (buffer[record_offset + RECORD_STATUS_OFFSET] != NRA_OUTBOX_JOURNAL_PENDING &&
         buffer[record_offset + RECORD_STATUS_OFFSET] != NRA_OUTBOX_JOURNAL_CONFIRMED))
        return NRA_OUTBOX_JOURNAL_MALFORMED;
    payload = buffer + record_offset + RECORD_PAYLOAD_OFFSET;
    if (contains_nul(payload, account_size) ||
        contains_nul(payload + account_size, url_size) ||
        contains_nul(payload + account_size + url_size, content_type_size) ||
        contains_nul(payload + account_size + url_size + content_type_size, post_size))
        return NRA_OUTBOX_JOURNAL_MALFORMED;
    {
        size_t i;
        const uint8_t* dedupe_key = buffer + record_offset + RECORD_DEDUPE_KEY_OFFSET;
        bool nonzero = false;
        for (i = 0; i < NRA_OUTBOX_JOURNAL_DEDUPE_KEY_SIZE; ++i) {
            if (dedupe_key[i] != 0) {
                nonzero = true;
                break;
            }
        }
        if (!nonzero) return NRA_OUTBOX_JOURNAL_MALFORMED;
    }
    if (record != NULL) {
        record->sequence = read_le64(buffer + record_offset + RECORD_SEQUENCE_OFFSET);
        record->game_id = read_le32(buffer + record_offset + RECORD_GAME_ID_OFFSET);
        record->console_id = read_le32(buffer + record_offset + RECORD_CONSOLE_ID_OFFSET);
        record->kind = (NRA_OutboxKind)buffer[record_offset + RECORD_KIND_OFFSET];
        record->status = (NRA_OutboxJournalStatus)buffer[record_offset + RECORD_STATUS_OFFSET];
        record->account = payload;
        record->account_size = account_size;
        record->url = payload + account_size;
        record->url_size = url_size;
        record->content_type = record->url + url_size;
        record->content_type_size = content_type_size;
        record->post = record->content_type + content_type_size;
        record->post_size = post_size;
        record->dedupe_key = buffer + record_offset + RECORD_DEDUPE_KEY_OFFSET;
        record->dedupe_key_size = NRA_OUTBOX_JOURNAL_DEDUPE_KEY_SIZE;
    }
    if (record_size != NULL) *record_size = size;
    return NRA_OUTBOX_JOURNAL_OK;
}

static NRA_OutboxJournalResult parse_journal(const uint8_t* buffer, size_t size,
                                              NRA_OutboxJournalRecord* records,
                                              size_t record_capacity, size_t* record_count) {
    size_t offset = NRA_OUTBOX_JOURNAL_HEADER_SIZE;
    size_t count;
    size_t i;
    uint64_t previous_sequence = 0;

    if (record_count == NULL) return NRA_OUTBOX_JOURNAL_INVALID_ARGUMENT;
    *record_count = 0;
    if (buffer == NULL) return NRA_OUTBOX_JOURNAL_INVALID_ARGUMENT;
    if (size < NRA_OUTBOX_JOURNAL_HEADER_SIZE) return NRA_OUTBOX_JOURNAL_TRUNCATED;
    if (size > NRA_OUTBOX_JOURNAL_MAX_TOTAL_BYTES) return NRA_OUTBOX_JOURNAL_LIMIT;
    if (memcmp(buffer + JOURNAL_MAGIC_OFFSET, JOURNAL_MAGIC, sizeof(JOURNAL_MAGIC)) != 0)
        return NRA_OUTBOX_JOURNAL_MALFORMED;
    if (read_le16(buffer + JOURNAL_VERSION_OFFSET) != NRA_OUTBOX_JOURNAL_VERSION)
        return NRA_OUTBOX_JOURNAL_UNSUPPORTED_VERSION;
    if (read_le16(buffer + JOURNAL_HEADER_SIZE_OFFSET) != NRA_OUTBOX_JOURNAL_HEADER_SIZE ||
        read_le32(buffer + JOURNAL_FLAGS_OFFSET) != 0)
        return NRA_OUTBOX_JOURNAL_MALFORMED;
    {
        size_t total_size = read_le32(buffer + JOURNAL_TOTAL_SIZE_OFFSET);
        if (total_size < NRA_OUTBOX_JOURNAL_HEADER_SIZE) return NRA_OUTBOX_JOURNAL_MALFORMED;
        if (total_size > NRA_OUTBOX_JOURNAL_MAX_TOTAL_BYTES) return NRA_OUTBOX_JOURNAL_LIMIT;
        if (total_size > size) return NRA_OUTBOX_JOURNAL_TRUNCATED;
        if (total_size < size) return NRA_OUTBOX_JOURNAL_MALFORMED;
    }
    count = read_le32(buffer + JOURNAL_RECORD_COUNT_OFFSET);
    if (count > NRA_OUTBOX_JOURNAL_MAX_RECORDS) return NRA_OUTBOX_JOURNAL_LIMIT;
    if (records != NULL && record_capacity < count)
        return NRA_OUTBOX_JOURNAL_BUFFER_TOO_SMALL;
    for (i = 0; i < count; ++i) {
        NRA_OutboxJournalRecord current;
        size_t current_size;
        size_t prior_offset = NRA_OUTBOX_JOURNAL_HEADER_SIZE;
        size_t prior;
        NRA_OutboxJournalResult result;

        if (offset > size || size - offset < NRA_OUTBOX_JOURNAL_RECORD_HEADER_SIZE)
            return NRA_OUTBOX_JOURNAL_TRUNCATED;
        if ((size_t)read_le32(buffer + offset + RECORD_SIZE_OFFSET) > size - offset)
            return NRA_OUTBOX_JOURNAL_TRUNCATED;
        result = parsed_record(buffer, offset, &current, &current_size);
        if (result != NRA_OUTBOX_JOURNAL_OK) return result;
        if (current.sequence <= previous_sequence)
            return current.sequence == previous_sequence
                ? NRA_OUTBOX_JOURNAL_DUPLICATE_SEQUENCE
                : NRA_OUTBOX_JOURNAL_MALFORMED;
        for (prior = 0; prior < i; ++prior) {
            NRA_OutboxJournalRecord previous;
            result = parsed_record(buffer, prior_offset, &previous, NULL);
            if (result != NRA_OUTBOX_JOURNAL_OK) return result;
            if (same_dedupe_key(&previous, &current))
                return NRA_OUTBOX_JOURNAL_DUPLICATE_DEDUPE;
            prior_offset += read_le32(buffer + prior_offset + RECORD_SIZE_OFFSET);
        }
        if (records != NULL) records[i] = current;
        previous_sequence = current.sequence;
        offset += current_size;
    }
    if (offset != size) return NRA_OUTBOX_JOURNAL_MALFORMED;
    if (journal_crc32(buffer, size) != read_le32(buffer + JOURNAL_CRC_OFFSET))
        return NRA_OUTBOX_JOURNAL_CRC_MISMATCH;
    *record_count = count;
    return NRA_OUTBOX_JOURNAL_OK;
}

static void write_journal_header(uint8_t* buffer, size_t total_size, size_t record_count) {
    memcpy(buffer + JOURNAL_MAGIC_OFFSET, JOURNAL_MAGIC, sizeof(JOURNAL_MAGIC));
    write_le16(buffer + JOURNAL_VERSION_OFFSET, NRA_OUTBOX_JOURNAL_VERSION);
    write_le16(buffer + JOURNAL_HEADER_SIZE_OFFSET, NRA_OUTBOX_JOURNAL_HEADER_SIZE);
    write_le32(buffer + JOURNAL_TOTAL_SIZE_OFFSET, (uint32_t)total_size);
    write_le32(buffer + JOURNAL_RECORD_COUNT_OFFSET, (uint32_t)record_count);
    write_le32(buffer + JOURNAL_FLAGS_OFFSET, 0);
    write_le32(buffer + JOURNAL_CRC_OFFSET, 0);
}

static void write_record(uint8_t* buffer, const NRA_OutboxJournalRecord* record, size_t size) {
    size_t payload_offset = RECORD_PAYLOAD_OFFSET;

    write_le32(buffer + RECORD_SIZE_OFFSET, (uint32_t)size);
    write_le64(buffer + RECORD_SEQUENCE_OFFSET, record->sequence);
    write_le32(buffer + RECORD_GAME_ID_OFFSET, record->game_id);
    write_le32(buffer + RECORD_CONSOLE_ID_OFFSET, record->console_id);
    buffer[RECORD_KIND_OFFSET] = (uint8_t)record->kind;
    buffer[RECORD_STATUS_OFFSET] = (uint8_t)record->status;
    write_le16(buffer + RECORD_FLAGS_OFFSET, 0);
    write_le16(buffer + RECORD_ACCOUNT_SIZE_OFFSET, (uint16_t)record->account_size);
    write_le16(buffer + RECORD_URL_SIZE_OFFSET, (uint16_t)record->url_size);
    write_le16(buffer + RECORD_CONTENT_TYPE_SIZE_OFFSET, (uint16_t)record->content_type_size);
    write_le16(buffer + RECORD_RESERVED_OFFSET, 0);
    write_le32(buffer + RECORD_POST_SIZE_OFFSET, (uint32_t)record->post_size);
    memcpy(buffer + RECORD_DEDUPE_KEY_OFFSET, record->dedupe_key, record->dedupe_key_size);
    memcpy(buffer + payload_offset, record->account, record->account_size);
    payload_offset += record->account_size;
    memcpy(buffer + payload_offset, record->url, record->url_size);
    payload_offset += record->url_size;
    memcpy(buffer + payload_offset, record->content_type, record->content_type_size);
    payload_offset += record->content_type_size;
    memcpy(buffer + payload_offset, record->post, record->post_size);
}

static bool find_sequence(const uint8_t* buffer, uint64_t sequence,
                          size_t* offset, size_t* record_size, NRA_OutboxJournalStatus* status) {
    size_t current_offset = NRA_OUTBOX_JOURNAL_HEADER_SIZE;
    size_t count = read_le32(buffer + JOURNAL_RECORD_COUNT_OFFSET);
    size_t i;

    for (i = 0; i < count; ++i) {
        size_t current_size = read_le32(buffer + current_offset + RECORD_SIZE_OFFSET);
        if (read_le64(buffer + current_offset + RECORD_SEQUENCE_OFFSET) == sequence) {
            if (offset != NULL) *offset = current_offset;
            if (record_size != NULL) *record_size = current_size;
            if (status != NULL)
                *status = (NRA_OutboxJournalStatus)buffer[current_offset + RECORD_STATUS_OFFSET];
            return true;
        }
        current_offset += current_size;
    }
    return false;
}

static bool has_dedupe_key(const uint8_t* buffer, const uint8_t* dedupe_key,
                           size_t dedupe_key_size) {
    size_t current_offset = NRA_OUTBOX_JOURNAL_HEADER_SIZE;
    size_t count = read_le32(buffer + JOURNAL_RECORD_COUNT_OFFSET);
    size_t i;

    for (i = 0; i < count; ++i) {
        size_t current_size = read_le32(buffer + current_offset + RECORD_SIZE_OFFSET);
        if (dedupe_key_size == NRA_OUTBOX_JOURNAL_DEDUPE_KEY_SIZE &&
            memcmp(buffer + current_offset + RECORD_DEDUPE_KEY_OFFSET, dedupe_key,
                   NRA_OUTBOX_JOURNAL_DEDUPE_KEY_SIZE) == 0)
            return true;
        current_offset += current_size;
    }
    return false;
}

NRA_OutboxJournalResult nra_outbox_journal_encoded_size(const NRA_OutboxJournalRecord* records,
                                                         size_t record_count, size_t* encoded_size) {
    return validate_record_list(records, record_count, encoded_size);
}

NRA_OutboxJournalResult nra_outbox_journal_encode(uint8_t* buffer, size_t capacity,
                                                   const NRA_OutboxJournalRecord* records,
                                                   size_t record_count, size_t* encoded_size) {
    size_t size;
    size_t offset = NRA_OUTBOX_JOURNAL_HEADER_SIZE;
    size_t i;
    NRA_OutboxJournalResult result;

    if (encoded_size == NULL) return NRA_OUTBOX_JOURNAL_INVALID_ARGUMENT;
    *encoded_size = 0;
    result = nra_outbox_journal_encoded_size(records, record_count, &size);
    if (result != NRA_OUTBOX_JOURNAL_OK) return result;
    if (buffer == NULL) return NRA_OUTBOX_JOURNAL_INVALID_ARGUMENT;
    if (capacity < size) return NRA_OUTBOX_JOURNAL_BUFFER_TOO_SMALL;
    write_journal_header(buffer, size, record_count);
    for (i = 0; i < record_count; ++i) {
        size_t current_size;
        (void)record_encoded_size(&records[i], false, &current_size);
        write_record(buffer + offset, &records[i], current_size);
        offset += current_size;
    }
    write_le32(buffer + JOURNAL_CRC_OFFSET, journal_crc32(buffer, size));
    *encoded_size = size;
    return NRA_OUTBOX_JOURNAL_OK;
}

NRA_OutboxJournalResult nra_outbox_journal_validate(const uint8_t* buffer, size_t size,
                                                     size_t* record_count) {
    return parse_journal(buffer, size, NULL, 0, record_count);
}

NRA_OutboxJournalResult nra_outbox_journal_decode(const uint8_t* buffer, size_t size,
                                                   NRA_OutboxJournalRecord* records,
                                                   size_t record_capacity, size_t* record_count) {
    NRA_OutboxJournalResult result;
    size_t count;

    if (record_count == NULL) return NRA_OUTBOX_JOURNAL_INVALID_ARGUMENT;
    *record_count = 0;
    result = nra_outbox_journal_validate(buffer, size, &count);
    if (result != NRA_OUTBOX_JOURNAL_OK) return result;
    if (record_capacity < count) return NRA_OUTBOX_JOURNAL_BUFFER_TOO_SMALL;
    if (count != 0 && records == NULL) return NRA_OUTBOX_JOURNAL_INVALID_ARGUMENT;
    result = parse_journal(buffer, size, records, record_capacity, &count);
    if (result != NRA_OUTBOX_JOURNAL_OK) return result;
    *record_count = count;
    return NRA_OUTBOX_JOURNAL_OK;
}

NRA_OutboxJournalResult nra_outbox_journal_append(uint8_t* buffer, size_t capacity, size_t size,
                                                   const NRA_OutboxJournalRecord* record,
                                                   size_t* new_size) {
    size_t record_size;
    size_t total_size;
    size_t record_count;
    size_t tail_offset;
    uint64_t tail_sequence = 0;
    NRA_OutboxJournalResult result;

    if (new_size == NULL) return NRA_OUTBOX_JOURNAL_INVALID_ARGUMENT;
    *new_size = 0;
    if (buffer == NULL || record == NULL) return NRA_OUTBOX_JOURNAL_INVALID_ARGUMENT;
    if (size > NRA_OUTBOX_JOURNAL_MAX_TOTAL_BYTES) return NRA_OUTBOX_JOURNAL_LIMIT;
    if (size > capacity) return NRA_OUTBOX_JOURNAL_BUFFER_TOO_SMALL;
    result = record_encoded_size(record, true, &record_size);
    if (result != NRA_OUTBOX_JOURNAL_OK) return result;
    if (size == 0) {
        record_count = 0;
    } else {
        result = nra_outbox_journal_validate(buffer, size, &record_count);
        if (result != NRA_OUTBOX_JOURNAL_OK) return result;
        tail_offset = NRA_OUTBOX_JOURNAL_HEADER_SIZE;
        if (record_count != 0) {
            size_t i;
            for (i = 0; i < record_count; ++i) {
                tail_sequence = read_le64(buffer + tail_offset + RECORD_SEQUENCE_OFFSET);
                tail_offset += read_le32(buffer + tail_offset + RECORD_SIZE_OFFSET);
            }
        }
        if (record->sequence <= tail_sequence)
            return record->sequence == tail_sequence
                ? NRA_OUTBOX_JOURNAL_DUPLICATE_SEQUENCE
                : NRA_OUTBOX_JOURNAL_MALFORMED;
        if (has_dedupe_key(buffer, record->dedupe_key, record->dedupe_key_size))
            return NRA_OUTBOX_JOURNAL_DUPLICATE_DEDUPE;
    }
    if (record_count >= NRA_OUTBOX_JOURNAL_MAX_RECORDS) return NRA_OUTBOX_JOURNAL_LIMIT;
    if (!size_add(size == 0 ? NRA_OUTBOX_JOURNAL_HEADER_SIZE : size, record_size, &total_size))
        return NRA_OUTBOX_JOURNAL_OVERFLOW;
    if (total_size > NRA_OUTBOX_JOURNAL_MAX_TOTAL_BYTES) return NRA_OUTBOX_JOURNAL_LIMIT;
    if (capacity < total_size) return NRA_OUTBOX_JOURNAL_BUFFER_TOO_SMALL;
    if (size == 0) write_journal_header(buffer, total_size, 0);
    write_record(buffer + (size == 0 ? NRA_OUTBOX_JOURNAL_HEADER_SIZE : size), record, record_size);
    write_le32(buffer + JOURNAL_TOTAL_SIZE_OFFSET, (uint32_t)total_size);
    write_le32(buffer + JOURNAL_RECORD_COUNT_OFFSET, (uint32_t)(record_count + 1));
    write_le32(buffer + JOURNAL_CRC_OFFSET, journal_crc32(buffer, total_size));
    *new_size = total_size;
    return NRA_OUTBOX_JOURNAL_OK;
}

NRA_OutboxJournalResult nra_outbox_journal_mark_confirmed(uint8_t* buffer, size_t size,
                                                           uint64_t sequence) {
    size_t record_offset;
    NRA_OutboxJournalStatus status;
    NRA_OutboxJournalResult result;

    if (buffer == NULL || sequence == 0) return NRA_OUTBOX_JOURNAL_INVALID_ARGUMENT;
    result = nra_outbox_journal_validate(buffer, size, &(size_t){0});
    if (result != NRA_OUTBOX_JOURNAL_OK) return result;
    if (!find_sequence(buffer, sequence, &record_offset, NULL, &status))
        return NRA_OUTBOX_JOURNAL_NOT_FOUND;
    if (status == NRA_OUTBOX_JOURNAL_CONFIRMED) return NRA_OUTBOX_JOURNAL_OK;
    buffer[record_offset + RECORD_STATUS_OFFSET] = NRA_OUTBOX_JOURNAL_CONFIRMED;
    write_le32(buffer + JOURNAL_CRC_OFFSET, 0);
    write_le32(buffer + JOURNAL_CRC_OFFSET, journal_crc32(buffer, size));
    return NRA_OUTBOX_JOURNAL_OK;
}

NRA_OutboxJournalResult nra_outbox_journal_remove_confirmed(uint8_t* buffer, size_t size,
                                                             uint64_t sequence, size_t* new_size) {
    size_t record_offset;
    size_t record_size;
    size_t record_count;
    size_t compacted_size;
    NRA_OutboxJournalStatus status;
    NRA_OutboxJournalResult result;

    if (new_size == NULL) return NRA_OUTBOX_JOURNAL_INVALID_ARGUMENT;
    *new_size = 0;
    if (buffer == NULL || sequence == 0) return NRA_OUTBOX_JOURNAL_INVALID_ARGUMENT;
    result = nra_outbox_journal_validate(buffer, size, &record_count);
    if (result != NRA_OUTBOX_JOURNAL_OK) return result;
    if (!find_sequence(buffer, sequence, &record_offset, &record_size, &status))
        return NRA_OUTBOX_JOURNAL_NOT_FOUND;
    if (status != NRA_OUTBOX_JOURNAL_CONFIRMED)
        return NRA_OUTBOX_JOURNAL_NOT_CONFIRMED;
    compacted_size = size - record_size;
    memmove(buffer + record_offset, buffer + record_offset + record_size,
            size - record_offset - record_size);
    record_count -= 1;
    write_le32(buffer + JOURNAL_TOTAL_SIZE_OFFSET, (uint32_t)compacted_size);
    write_le32(buffer + JOURNAL_RECORD_COUNT_OFFSET, (uint32_t)record_count);
    write_le32(buffer + JOURNAL_CRC_OFFSET, 0);
    write_le32(buffer + JOURNAL_CRC_OFFSET, journal_crc32(buffer, compacted_size));
    *new_size = compacted_size;
    return NRA_OUTBOX_JOURNAL_OK;
}
