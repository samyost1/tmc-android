#ifndef NATIVE_RA_OUTBOX_H
#define NATIVE_RA_OUTBOX_H

#include <stdbool.h>
#include <stddef.h>

typedef enum NRA_OutboxKind {
    NRA_OUTBOX_NONE,
    NRA_OUTBOX_ACHIEVEMENT,
    NRA_OUTBOX_LEADERBOARD,
} NRA_OutboxKind;

NRA_OutboxKind nra_outbox_classify_request(const char* url, const char* post_data, const char* content_type);
bool nra_outbox_response_confirmed(NRA_OutboxKind kind, const char* body, size_t body_size);

#endif
