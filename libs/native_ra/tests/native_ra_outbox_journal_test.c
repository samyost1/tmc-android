#include "../src/native_ra_outbox_journal.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #condition); \
            return 1; \
        } \
    } while (0)

static const uint8_t account[] = {'u', 's', 'e', 'r'};
static const uint8_t url[] = "https://retroachievements.org/dorequest.php";
static const uint8_t content_type[] = "application/x-www-form-urlencoded";
static const uint8_t post_one[] = "r=awardachievement&u=user&t=token&a=5&h=0&v=one";
static const uint8_t post_two[] = "r=submitlbentry&u=user&t=token&i=4&s=-3&v=two";
static const uint8_t dedupe_one[NRA_OUTBOX_JOURNAL_DEDUPE_KEY_SIZE] = {
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10,
    0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
    0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20
};
static const uint8_t dedupe_two[NRA_OUTBOX_JOURNAL_DEDUPE_KEY_SIZE] = {
    0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8,
    0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf, 0xb0,
    0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8,
    0xb9, 0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf, 0xc0
};

static NRA_OutboxJournalRecord make_record(uint64_t sequence, NRA_OutboxKind kind,
                                            NRA_OutboxJournalStatus status,
                                            const uint8_t* dedupe_key,
                                            const uint8_t* post, size_t post_size) {
    NRA_OutboxJournalRecord record = {0};

    record.sequence = sequence;
    record.game_id = 559;
    record.console_id = 5;
    record.kind = kind;
    record.status = status;
    record.account = account;
    record.account_size = sizeof(account);
    record.dedupe_key = dedupe_key;
    record.dedupe_key_size = NRA_OUTBOX_JOURNAL_DEDUPE_KEY_SIZE;
    record.url = url;
    record.url_size = sizeof(url) - 1;
    record.content_type = content_type;
    record.content_type_size = sizeof(content_type) - 1;
    record.post = post;
    record.post_size = post_size;
    return record;
}

static uint16_t test_read_le16(const uint8_t* source) {
    return (uint16_t)source[0] | (uint16_t)((uint16_t)source[1] << 8);
}

static uint32_t test_read_le32(const uint8_t* source) {
    return (uint32_t)source[0] | ((uint32_t)source[1] << 8) |
        ((uint32_t)source[2] << 16) | ((uint32_t)source[3] << 24);
}

static int round_trip_and_layout(void) {
    NRA_OutboxJournalRecord input[2];
    NRA_OutboxJournalRecord output[2];
    uint8_t buffer[1024];
    size_t size;
    size_t count;
    NRA_OutboxJournalResult result;

    input[0] = make_record(10, NRA_OUTBOX_ACHIEVEMENT, NRA_OUTBOX_JOURNAL_PENDING,
                           dedupe_one, post_one, sizeof(post_one) - 1);
    input[1] = make_record(11, NRA_OUTBOX_LEADERBOARD, NRA_OUTBOX_JOURNAL_CONFIRMED,
                           dedupe_two, post_two, sizeof(post_two) - 1);
    result = nra_outbox_journal_encoded_size(input, 2, &size);
    CHECK(result == NRA_OUTBOX_JOURNAL_OK);
    CHECK(size == NRA_OUTBOX_JOURNAL_HEADER_SIZE +
              (NRA_OUTBOX_JOURNAL_RECORD_HEADER_SIZE + sizeof(account) +
               (sizeof(url) - 1) + (sizeof(content_type) - 1) + (sizeof(post_one) - 1)) +
              (NRA_OUTBOX_JOURNAL_RECORD_HEADER_SIZE + sizeof(account) +
               (sizeof(url) - 1) + (sizeof(content_type) - 1) + (sizeof(post_two) - 1)));
    result = nra_outbox_journal_encode(buffer, sizeof(buffer), input, 2, &size);
    CHECK(result == NRA_OUTBOX_JOURNAL_OK);
    CHECK(test_read_le16(buffer + 4) == NRA_OUTBOX_JOURNAL_VERSION);
    CHECK(test_read_le16(buffer + 6) == NRA_OUTBOX_JOURNAL_HEADER_SIZE);
    CHECK(test_read_le32(buffer + 8) == size);
    CHECK(test_read_le32(buffer + 12) == 2);
    result = nra_outbox_journal_validate(buffer, size, &count);
    CHECK(result == NRA_OUTBOX_JOURNAL_OK && count == 2);
    result = nra_outbox_journal_decode(buffer, size, output, 2, &count);
    CHECK(result == NRA_OUTBOX_JOURNAL_OK && count == 2);
    CHECK(output[0].sequence == 10 && output[0].kind == NRA_OUTBOX_ACHIEVEMENT &&
          output[0].status == NRA_OUTBOX_JOURNAL_PENDING);
    CHECK(output[1].sequence == 11 && output[1].kind == NRA_OUTBOX_LEADERBOARD &&
          output[1].status == NRA_OUTBOX_JOURNAL_CONFIRMED);
    CHECK(output[0].account_size == sizeof(account) &&
          memcmp(output[0].account, account, sizeof(account)) == 0);
    CHECK(output[1].post_size == sizeof(post_two) - 1 &&
          memcmp(output[1].post, post_two, sizeof(post_two) - 1) == 0);
    CHECK(nra_outbox_journal_decode(buffer, size, output, 1, &count) ==
          NRA_OUTBOX_JOURNAL_BUFFER_TOO_SMALL && count == 0);
    return 0;
}

static int append_and_lifecycle(void) {
    NRA_OutboxJournalRecord first = make_record(1, NRA_OUTBOX_ACHIEVEMENT,
                                                 NRA_OUTBOX_JOURNAL_PENDING, dedupe_one,
                                                 post_one, sizeof(post_one) - 1);
    NRA_OutboxJournalRecord second = make_record(2, NRA_OUTBOX_LEADERBOARD,
                                                  NRA_OUTBOX_JOURNAL_PENDING, dedupe_two,
                                                  post_two, sizeof(post_two) - 1);
    NRA_OutboxJournalRecord duplicate = second;
    uint8_t buffer[1024] = {0};
    uint8_t snapshot[sizeof(buffer)];
    NRA_OutboxJournalRecord output[2];
    size_t size = 0;
    size_t count;
    size_t new_size;

    CHECK(nra_outbox_journal_append(buffer, sizeof(buffer), size, &first, &size) ==
          NRA_OUTBOX_JOURNAL_OK);
    CHECK(nra_outbox_journal_append(buffer, sizeof(buffer), size, &second, &size) ==
          NRA_OUTBOX_JOURNAL_OK);
    memcpy(snapshot, buffer, size);
    duplicate.sequence = 3;
    CHECK(nra_outbox_journal_append(buffer, sizeof(buffer), size, &duplicate, &new_size) ==
          NRA_OUTBOX_JOURNAL_DUPLICATE_DEDUPE);
    CHECK(new_size == 0 && memcmp(buffer, snapshot, size) == 0);
    duplicate = second;
    duplicate.sequence = 2;
    memcpy(snapshot, buffer, size);
    CHECK(nra_outbox_journal_append(buffer, sizeof(buffer), size, &duplicate, &new_size) ==
          NRA_OUTBOX_JOURNAL_DUPLICATE_SEQUENCE);
    CHECK(new_size == 0 && memcmp(buffer, snapshot, size) == 0);
    CHECK(nra_outbox_journal_mark_confirmed(buffer, size, 1) == NRA_OUTBOX_JOURNAL_OK);
    CHECK(nra_outbox_journal_mark_confirmed(buffer, size, 1) == NRA_OUTBOX_JOURNAL_OK);
    CHECK(nra_outbox_journal_remove_confirmed(buffer, size, 2, &new_size) ==
          NRA_OUTBOX_JOURNAL_NOT_CONFIRMED);
    CHECK(nra_outbox_journal_remove_confirmed(buffer, size, 1, &new_size) ==
          NRA_OUTBOX_JOURNAL_OK);
    size = new_size;
    CHECK(nra_outbox_journal_decode(buffer, size, output, 2, &count) ==
          NRA_OUTBOX_JOURNAL_OK && count == 1 && output[0].sequence == 2);
    CHECK(nra_outbox_journal_mark_confirmed(buffer, size, 999) ==
          NRA_OUTBOX_JOURNAL_NOT_FOUND);
    CHECK(nra_outbox_journal_mark_confirmed(buffer, size, 2) == NRA_OUTBOX_JOURNAL_OK);
    CHECK(nra_outbox_journal_remove_confirmed(buffer, size, 2, &new_size) ==
          NRA_OUTBOX_JOURNAL_OK);
    CHECK(new_size == NRA_OUTBOX_JOURNAL_HEADER_SIZE);
    size = new_size;
    CHECK(nra_outbox_journal_validate(buffer, size, &count) ==
          NRA_OUTBOX_JOURNAL_OK && count == 0);
    return 0;
}

static int corruption_and_structure(void) {
    NRA_OutboxJournalRecord record = make_record(1, NRA_OUTBOX_ACHIEVEMENT,
                                                  NRA_OUTBOX_JOURNAL_PENDING, dedupe_one,
                                                  post_one, sizeof(post_one) - 1);
    NRA_OutboxJournalRecord records[2];
    uint8_t buffer[1024];
    uint8_t duplicate[1024];
    size_t size;
    size_t count;
    size_t second_offset;

    CHECK(nra_outbox_journal_encode(buffer, sizeof(buffer), &record, 1, &size) ==
          NRA_OUTBOX_JOURNAL_OK);
    buffer[size - 1] ^= 0x80;
    CHECK(nra_outbox_journal_validate(buffer, size, &count) ==
          NRA_OUTBOX_JOURNAL_CRC_MISMATCH);
    CHECK(nra_outbox_journal_validate(buffer, size - 1, &count) ==
          NRA_OUTBOX_JOURNAL_TRUNCATED);
    records[0] = record;
    records[1] = make_record(2, NRA_OUTBOX_LEADERBOARD, NRA_OUTBOX_JOURNAL_PENDING,
                             dedupe_two, post_two, sizeof(post_two) - 1);
    CHECK(nra_outbox_journal_encode(duplicate, sizeof(duplicate), records, 2, &size) ==
          NRA_OUTBOX_JOURNAL_OK);
    second_offset = NRA_OUTBOX_JOURNAL_HEADER_SIZE + NRA_OUTBOX_JOURNAL_RECORD_HEADER_SIZE +
        sizeof(account) + (sizeof(url) - 1) + (sizeof(content_type) - 1) +
        (sizeof(post_one) - 1);
    memset(duplicate + second_offset + 4, 0, sizeof(uint64_t));
    duplicate[second_offset + 4] = 1;
    CHECK(nra_outbox_journal_validate(duplicate, size, &count) ==
          NRA_OUTBOX_JOURNAL_DUPLICATE_SEQUENCE);
    CHECK(nra_outbox_journal_encode(duplicate, sizeof(duplicate), &record, 1, &size) ==
          NRA_OUTBOX_JOURNAL_OK);
    duplicate[24 + 4] = 0;
    duplicate[24 + 5] = 0;
    duplicate[24 + 6] = 0;
    duplicate[24 + 7] = 0;
    CHECK(nra_outbox_journal_validate(duplicate, size, &count) ==
          NRA_OUTBOX_JOURNAL_MALFORMED);
    duplicate[24 + 0] = 67;
    duplicate[24 + 1] = 0;
    duplicate[24 + 2] = 0;
    duplicate[24 + 3] = 0;
    CHECK(nra_outbox_journal_validate(duplicate, size, &count) ==
          NRA_OUTBOX_JOURNAL_MALFORMED);
    CHECK(nra_outbox_journal_validate(NULL, 0, &count) ==
          NRA_OUTBOX_JOURNAL_INVALID_ARGUMENT);
    return 0;
}

static int duplicate_and_overflow(void) {
    NRA_OutboxJournalRecord input[2];
    NRA_OutboxJournalRecord record = make_record(1, NRA_OUTBOX_ACHIEVEMENT,
                                                  NRA_OUTBOX_JOURNAL_PENDING, dedupe_one,
                                                  post_one, sizeof(post_one) - 1);
    uint8_t binary_dedupe[NRA_OUTBOX_JOURNAL_DEDUPE_KEY_SIZE] = {0};
    uint8_t buffer[1024];
    uint8_t snapshot[sizeof(buffer)];
    uint8_t too_large_post[NRA_OUTBOX_JOURNAL_MAX_POST_BYTES + 1] = {0};
    size_t size;
    size_t count;
    size_t encoded_size = 123;

    input[0] = record;
    input[1] = record;
    input[1].sequence = 2;
    CHECK(nra_outbox_journal_encoded_size(input, 2, &encoded_size) ==
          NRA_OUTBOX_JOURNAL_DUPLICATE_DEDUPE && encoded_size == 0);
    CHECK(nra_outbox_journal_encoded_size(NULL, SIZE_MAX, &encoded_size) ==
          NRA_OUTBOX_JOURNAL_LIMIT && encoded_size == 0);
    record.post = too_large_post;
    record.post_size = sizeof(too_large_post);
    CHECK(nra_outbox_journal_encoded_size(&record, 1, &encoded_size) ==
          NRA_OUTBOX_JOURNAL_LIMIT && encoded_size == 0);
    binary_dedupe[31] = 1;
    record = make_record(1, NRA_OUTBOX_ACHIEVEMENT, NRA_OUTBOX_JOURNAL_PENDING,
                         binary_dedupe, post_one, sizeof(post_one) - 1);
    CHECK(nra_outbox_journal_encode(buffer, sizeof(buffer), &record, 1, &encoded_size) ==
          NRA_OUTBOX_JOURNAL_OK);
    CHECK(nra_outbox_journal_validate(buffer, encoded_size, &count) ==
          NRA_OUTBOX_JOURNAL_OK && count == 1);
    record = make_record(1, NRA_OUTBOX_ACHIEVEMENT, NRA_OUTBOX_JOURNAL_PENDING,
                         dedupe_one, post_one, sizeof(post_one) - 1);
    CHECK(nra_outbox_journal_encode(buffer, sizeof(buffer), &record, 1, &size) ==
          NRA_OUTBOX_JOURNAL_OK);
    memcpy(snapshot, buffer, size);
    CHECK(nra_outbox_journal_append(buffer, sizeof(buffer), NRA_OUTBOX_JOURNAL_MAX_TOTAL_BYTES + 1,
                                    &record, &count) == NRA_OUTBOX_JOURNAL_LIMIT);
    CHECK(count == 0 && memcmp(buffer, snapshot, size) == 0);
    CHECK(nra_outbox_journal_encode(buffer, sizeof(buffer), &record, 1, &encoded_size) ==
          NRA_OUTBOX_JOURNAL_OK);
    CHECK(nra_outbox_journal_validate(buffer, encoded_size, &count) ==
          NRA_OUTBOX_JOURNAL_OK && count == 1);
    return 0;
}

static int exact_total_limit(void) {
    static uint8_t post[NRA_OUTBOX_JOURNAL_MAX_POST_BYTES];
    static uint8_t exact_buffer[NRA_OUTBOX_JOURNAL_MAX_TOTAL_BYTES];
    static uint8_t keys[8][NRA_OUTBOX_JOURNAL_DEDUPE_KEY_SIZE];
    NRA_OutboxJournalRecord records[8];
    size_t size;
    size_t fixed_record_overhead;
    size_t full_record_size;
    size_t final_post_size;
    size_t i;

    memset(post, 'x', sizeof(post));
    fixed_record_overhead = NRA_OUTBOX_JOURNAL_RECORD_HEADER_SIZE + sizeof(account) +
        (sizeof(url) - 1) + (sizeof(content_type) - 1);
    full_record_size = fixed_record_overhead + NRA_OUTBOX_JOURNAL_MAX_POST_BYTES;
    final_post_size = NRA_OUTBOX_JOURNAL_MAX_TOTAL_BYTES -
        NRA_OUTBOX_JOURNAL_HEADER_SIZE - (7 * full_record_size) - fixed_record_overhead;
    for (i = 0; i < 8; ++i) {
        size_t post_size = i < 7 ? NRA_OUTBOX_JOURNAL_MAX_POST_BYTES : final_post_size;
        size_t key_index = i + 1;
        memset(keys[i], (int)key_index, sizeof(keys[i]));
        records[i] = make_record((uint64_t)key_index, NRA_OUTBOX_ACHIEVEMENT,
                                 NRA_OUTBOX_JOURNAL_PENDING, keys[i], post, post_size);
    }
    CHECK(nra_outbox_journal_encoded_size(records, 8, &size) == NRA_OUTBOX_JOURNAL_OK);
    CHECK(size == NRA_OUTBOX_JOURNAL_MAX_TOTAL_BYTES);
    CHECK(nra_outbox_journal_encode(exact_buffer, sizeof(exact_buffer), records, 8, &size) ==
          NRA_OUTBOX_JOURNAL_OK);
    CHECK(nra_outbox_journal_validate(exact_buffer, size, &i) ==
          NRA_OUTBOX_JOURNAL_OK && i == 8);
    records[7].post_size += 1;
    CHECK(nra_outbox_journal_encoded_size(records, 8, &size) ==
          NRA_OUTBOX_JOURNAL_LIMIT && size == 0);
    return 0;
}

int main(void) {
    if (round_trip_and_layout() != 0 ||
        append_and_lifecycle() != 0 ||
        corruption_and_structure() != 0 ||
        duplicate_and_overflow() != 0 ||
        exact_total_limit() != 0)
        return 1;
    puts("NATIVE RA OUTBOX JOURNAL OK");
    return 0;
}
