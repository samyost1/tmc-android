#include "tmc_ra_android_queue.h"

#include <stdio.h>
#include <string.h>

static bool AllZero(const void* data, size_t size) {
    const uint8_t* bytes = data;

    while (size-- != 0) {
        if (*bytes++ != 0)
            return false;
    }
    return true;
}

int main(void) {
    TmcRaAndroidQueue queue;
    TmcRaAndroidCompletion taken;
    TmcRaAndroidLogin login;
    const uint8_t user[] = "user";
    const uint8_t password[] = "password";
    const uint8_t token_user[] = "token-user";
    const uint8_t token_value[] = "token-value";
    const uint8_t embedded_nul[] = {'u', 0, 'r'};
    uint8_t max_user[TMC_RA_ANDROID_MAX_USERNAME_BYTES];
    uint8_t max_password[TMC_RA_ANDROID_MAX_PASSWORD_BYTES];
    uint8_t max_token[TMC_RA_ANDROID_MAX_TOKEN_BYTES];
    uint8_t oversized_user[TMC_RA_ANDROID_MAX_USERNAME_BYTES + 1];
    uint8_t oversized_password[TMC_RA_ANDROID_MAX_PASSWORD_BYTES + 1];
    uint8_t oversized_token[TMC_RA_ANDROID_MAX_TOKEN_BYTES + 1];
    const uint8_t body[] = {1, 2, 3};
    const NRA_HttpCompletion completion = {
        .request_id = 7,
        .http_status_code = 200,
        .body = body,
        .body_size = sizeof(body),
    };
    const uint64_t first_generation = 1;
    uint64_t second_generation;
    unsigned index;

    memset(max_user, 'u', sizeof(max_user));
    memset(max_password, 'p', sizeof(max_password));
    memset(max_token, 't', sizeof(max_token));
    memset(oversized_user, 'u', sizeof(oversized_user));
    memset(oversized_password, 'p', sizeof(oversized_password));
    memset(oversized_token, 't', sizeof(oversized_token));
    if (!TmcRaAndroidQueue_Init(&queue) || TmcRaAndroidQueue_Generation(&queue) != first_generation)
        return 1;
    if (TmcRaAndroidQueue_EnqueuePassword(&queue, first_generation, NULL, 1,
                                          password, sizeof(password) - 1)
        || TmcRaAndroidQueue_EnqueuePassword(&queue, first_generation, user, 0,
                                             password, sizeof(password) - 1)
        || TmcRaAndroidQueue_EnqueuePassword(&queue, first_generation, oversized_user, sizeof(oversized_user),
                                             password, sizeof(password) - 1)
        || TmcRaAndroidQueue_EnqueuePassword(&queue, first_generation, user, sizeof(user) - 1,
                                             oversized_password, sizeof(oversized_password))
        || TmcRaAndroidQueue_EnqueuePassword(&queue, first_generation, embedded_nul, sizeof(embedded_nul),
                                             password, sizeof(password) - 1))
        return 1;
    if (!TmcRaAndroidQueue_EnqueuePassword(&queue, first_generation, max_user, sizeof(max_user),
                                           max_password, sizeof(max_password))
        || !TmcRaAndroidQueue_TakePassword(&queue, first_generation, &login)
        || memcmp(login.username, max_user, sizeof(max_user)) != 0
        || login.username[sizeof(max_user)] != '\0'
        || memcmp(login.password, max_password, sizeof(max_password)) != 0
        || login.password[sizeof(max_password)] != '\0'
        || !AllZero(&queue.login, sizeof(queue.login)))
        return 1;
    TmcRaAndroidQueue_WipeLogin(&login);
    if (!AllZero(&login, sizeof(login)))
        return 1;
    if (!TmcRaAndroidQueue_EnqueueToken(&queue, first_generation, user, sizeof(user) - 1,
                                        max_token, sizeof(max_token))
        || !TmcRaAndroidQueue_TakePassword(&queue, first_generation, &login)
        || !login.token_login
        || memcmp(login.token, max_token, sizeof(max_token)) != 0
        || login.token[sizeof(max_token)] != '\0'
        || !AllZero(&queue.login, sizeof(queue.login)))
        return 1;
    TmcRaAndroidQueue_WipeLogin(&login);
    if (!AllZero(&login, sizeof(login))
        || TmcRaAndroidQueue_EnqueueToken(&queue, first_generation, oversized_user, sizeof(oversized_user),
                                          token_value, sizeof(token_value) - 1))
        return 1;
    if (TmcRaAndroidQueue_EnqueueToken(&queue, first_generation, user, sizeof(user) - 1,
                                       oversized_token, sizeof(oversized_token)))
        return 1;
    TmcRaAndroidQueue_Clear(&queue);
    for (index = 0; index < TMC_RA_ANDROID_QUEUE_CAPACITY; ++index) {
        NRA_HttpCompletion item = completion;
        item.request_id += index;
        if (!TmcRaAndroidQueue_EnqueueCompletion(&queue, first_generation, &item))
            return 1;
    }
    if (TmcRaAndroidQueue_EnqueueCompletion(&queue, first_generation, &completion))
        return 1;
    if (!TmcRaAndroidQueue_TakeCompletion(&queue, first_generation, &taken)
        || taken.completion.request_id != completion.request_id
        || taken.completion.body_size != sizeof(body)
        || memcmp(taken.completion.body, body, sizeof(body)) != 0)
        return 1;
    TmcRaAndroidQueue_ReleaseCompletion(&taken);
    if (!TmcRaAndroidQueue_EnqueuePassword(&queue, first_generation, user, sizeof(user) - 1,
                                           password, sizeof(password) - 1)
        || TmcRaAndroidQueue_EnqueueToken(&queue, first_generation, token_user, sizeof(token_user) - 1,
                                          token_value, sizeof(token_value) - 1))
        return 1;
    second_generation = TmcRaAndroidQueue_AdvanceGeneration(&queue);
    if (second_generation == first_generation || TmcRaAndroidQueue_TakePassword(&queue, first_generation, &login)
        || TmcRaAndroidQueue_EnqueueCompletion(&queue, first_generation, &completion)
        || !AllZero(&queue.login, sizeof(queue.login)))
        return 1;
    if (!TmcRaAndroidQueue_EnqueuePassword(&queue, second_generation, user, sizeof(user) - 1,
                                           password, sizeof(password) - 1)
        || TmcRaAndroidQueue_EnqueuePassword(&queue, second_generation, token_user, sizeof(token_user) - 1,
                                             token_value, sizeof(token_value) - 1)
        || !TmcRaAndroidQueue_TakePassword(&queue, second_generation, &login)
        || login.token_login || strcmp(login.username, "user") != 0
        || strcmp(login.password, "password") != 0
        || !AllZero(&queue.login, sizeof(queue.login)))
        return 1;
    TmcRaAndroidQueue_WipeLogin(&login);
    if (!AllZero(&login, sizeof(login))
        || !TmcRaAndroidQueue_EnqueueToken(&queue, second_generation, token_user, sizeof(token_user) - 1,
                                           token_value, sizeof(token_value) - 1)
        || !TmcRaAndroidQueue_TakePassword(&queue, second_generation, &login)
        || !login.token_login || strcmp(login.username, "token-user") != 0
        || strcmp(login.token, "token-value") != 0
        || !AllZero(&queue.login, sizeof(queue.login)))
        return 1;
    TmcRaAndroidQueue_WipeLogin(&login);
    if (!AllZero(&login, sizeof(login)))
        return 1;
    TmcRaAndroidQueue_Close(&queue);
    if (TmcRaAndroidQueue_EnqueuePassword(&queue, second_generation, user, sizeof(user) - 1,
                                          password, sizeof(password) - 1)
        || TmcRaAndroidQueue_EnqueueCompletion(&queue, second_generation, &completion))
        return 1;
    TmcRaAndroidQueue_Reopen(&queue);
    if (TmcRaAndroidQueue_Generation(&queue) == second_generation
        || TmcRaAndroidQueue_EnqueueToken(&queue, second_generation, token_user, sizeof(token_user) - 1,
                                          token_value, sizeof(token_value) - 1))
        return 1;
    second_generation = TmcRaAndroidQueue_Generation(&queue);
    if (!TmcRaAndroidQueue_EnqueueToken(&queue, second_generation, token_user, sizeof(token_user) - 1,
                                        token_value, sizeof(token_value) - 1)
        || !TmcRaAndroidQueue_TakePassword(&queue, second_generation, &login)
        || !login.token_login || strcmp(login.username, "token-user") != 0
        || strcmp(login.token, "token-value") != 0)
        return 1;
    TmcRaAndroidQueue_WipeLogin(&login);
    TmcRaAndroidQueue_Close(&queue);
    TmcRaAndroidQueue_Destroy(&queue);
    puts("tmc_ra_android_queue_test: ALL PASS");
    return 0;
}
