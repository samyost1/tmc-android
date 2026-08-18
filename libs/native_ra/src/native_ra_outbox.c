#include "native_ra_outbox.h"

#include "rc_api_runtime.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

static bool has_only_shape(const char* post, const char* const* allowed, size_t allowed_count,
                           const char* const* required, size_t required_count) {
    const char* item = post;
    size_t found = 0;
    size_t segments = 0;

    while (*item) {
        const char* end = strchr(item, '&');
        const char* equal = strchr(item, '=');
        size_t name_size;
        size_t value_size;
        size_t i;
        bool known = false;

        if (end == NULL) end = item + strlen(item);
        if (equal == NULL || equal >= end) return false;
        name_size = (size_t)(equal - item);
        value_size = (size_t)(end - equal - 1);
        for (i = 0; i < allowed_count; ++i) {
            if (strlen(allowed[i]) == name_size && memcmp(item, allowed[i], name_size) == 0) {
                known = true;
                break;
            }
        }
        if (!known || ++segments > allowed_count) return false;
        for (i = 0; i < required_count; ++i) {
            if (strlen(required[i]) == name_size && memcmp(item, required[i], name_size) == 0) {
                if (value_size == 0) return false;
                /* A request is replayable only when every allowed parameter
                 * has the expected cardinality. Duplicate required fields
                 * must not pass shape validation. */
                if (found & (size_t)1 << i) return false;
                found |= (size_t)1 << i;
                break;
            }
        }
        item = *end ? end + 1 : end;
    }
    return found == ((size_t)1 << required_count) - 1;
}

NRA_OutboxKind nra_outbox_classify_request(const char* url, const char* post, const char* content_type) {
    static const char* const achievement_allowed[] = {"r", "u", "t", "a", "h", "m", "o", "v"};
    static const char* const achievement_required[] = {"r", "u", "t", "a", "h", "v"};
    static const char* const leaderboard_allowed[] = {"r", "u", "t", "i", "s", "m", "o", "v"};
    static const char* const leaderboard_required[] = {"r", "u", "t", "i", "s", "v"};

    if (url == NULL || post == NULL || content_type == NULL ||
        strcmp(url, "https://retroachievements.org/dorequest.php") != 0 ||
        strcmp(content_type, "application/x-www-form-urlencoded") != 0)
        return NRA_OUTBOX_NONE;
    if (strncmp(post, "r=awardachievement&", 19) == 0 &&
        has_only_shape(post, achievement_allowed, 8, achievement_required, 6))
        return NRA_OUTBOX_ACHIEVEMENT;
    if (strncmp(post, "r=submitlbentry&", 16) == 0 &&
        has_only_shape(post, leaderboard_allowed, 8, leaderboard_required, 6))
        return NRA_OUTBOX_LEADERBOARD;
    return NRA_OUTBOX_NONE;
}

bool nra_outbox_response_confirmed(NRA_OutboxKind kind, const char* body, size_t body_size) {
    rc_api_server_response_t server_response;

    if (body == NULL || body_size == 0 || body_size > 1024u * 1024u)
        return false;
    server_response.body = body;
    server_response.body_length = body_size;
    server_response.http_status_code = 200;
    if (kind == NRA_OUTBOX_ACHIEVEMENT) {
        rc_api_award_achievement_response_t response;
        const int result = rc_api_process_award_achievement_server_response(&response, &server_response);
        const bool confirmed = result == RC_OK && response.response.succeeded != 0;
        rc_api_destroy_award_achievement_response(&response);
        return confirmed;
    }
    if (kind == NRA_OUTBOX_LEADERBOARD) {
        rc_api_submit_lboard_entry_response_t response;
        const int result = rc_api_process_submit_lboard_entry_server_response(&response, &server_response);
        const bool confirmed = result == RC_OK && response.response.succeeded != 0;
        rc_api_destroy_submit_lboard_entry_response(&response);
        return confirmed;
    }
    return false;
}
