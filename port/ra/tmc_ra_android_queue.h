#ifndef TMC_RA_ANDROID_QUEUE_H
#define TMC_RA_ANDROID_QUEUE_H

#include "native_ra/native_ra.h"

#include <pthread.h>

#define TMC_RA_ANDROID_QUEUE_CAPACITY 16u
#define TMC_RA_ANDROID_MAX_RESPONSE_BYTES (1024u * 1024u)
#define TMC_RA_ANDROID_MAX_SECURE_BLOB_BYTES (512u * 1024u)
#define TMC_RA_ANDROID_MAX_USERNAME_BYTES 256u
#define TMC_RA_ANDROID_MAX_PASSWORD_BYTES 256u
#define TMC_RA_ANDROID_MAX_TOKEN_BYTES 1024u

typedef struct TmcRaAndroidCompletion {
    NRA_HttpCompletion completion;
    uint64_t generation;
    uint8_t* body;
} TmcRaAndroidCompletion;

typedef struct TmcRaAndroidLogin {
    uint64_t generation;
    char username[TMC_RA_ANDROID_MAX_USERNAME_BYTES + 1];
    char password[TMC_RA_ANDROID_MAX_PASSWORD_BYTES + 1];
    char token[TMC_RA_ANDROID_MAX_TOKEN_BYTES + 1];
    bool token_login;
    bool pending;
} TmcRaAndroidLogin;

typedef struct TmcRaAndroidQueue {
    pthread_mutex_t mutex;
    TmcRaAndroidCompletion completions[TMC_RA_ANDROID_QUEUE_CAPACITY];
    TmcRaAndroidLogin login;
    uint64_t generation;
    uint32_t head;
    uint32_t count;
    bool closed;
} TmcRaAndroidQueue;

bool TmcRaAndroidQueue_Init(TmcRaAndroidQueue* queue);
void TmcRaAndroidQueue_Destroy(TmcRaAndroidQueue* queue);
uint64_t TmcRaAndroidQueue_Generation(TmcRaAndroidQueue* queue);
uint64_t TmcRaAndroidQueue_AdvanceGeneration(TmcRaAndroidQueue* queue);
void TmcRaAndroidQueue_Reopen(TmcRaAndroidQueue* queue);
void TmcRaAndroidQueue_Close(TmcRaAndroidQueue* queue);

bool TmcRaAndroidQueue_EnqueueCompletion(TmcRaAndroidQueue* queue, uint64_t generation,
                                         const NRA_HttpCompletion* completion);
bool TmcRaAndroidQueue_TakeCompletion(TmcRaAndroidQueue* queue, uint64_t generation,
                                      TmcRaAndroidCompletion* completion);
void TmcRaAndroidQueue_ReleaseCompletion(TmcRaAndroidCompletion* completion);

bool TmcRaAndroidQueue_EnqueuePassword(TmcRaAndroidQueue* queue, uint64_t generation,
                                       const uint8_t* username, size_t username_size,
                                       const uint8_t* password, size_t password_size);
bool TmcRaAndroidQueue_EnqueueToken(TmcRaAndroidQueue* queue, uint64_t generation,
                                    const uint8_t* username, size_t username_size,
                                    const uint8_t* token, size_t token_size);
bool TmcRaAndroidQueue_TakePassword(TmcRaAndroidQueue* queue, uint64_t generation,
                                    TmcRaAndroidLogin* login);
void TmcRaAndroidQueue_WipeLogin(TmcRaAndroidLogin* login);
void TmcRaAndroidQueue_Clear(TmcRaAndroidQueue* queue);

#endif
