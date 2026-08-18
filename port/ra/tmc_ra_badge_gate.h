#ifndef TMC_RA_BADGE_GATE_H
#define TMC_RA_BADGE_GATE_H

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

typedef bool (*TmcRaBadgeGateAccept)(void* state, uint64_t generation);
typedef void (*TmcRaBadgeGateHook)(void* userdata);

typedef struct TmcRaBadgeGate {
    pthread_mutex_t* lifecycle_mutex;
    pthread_mutex_t* state_mutex;
    void* state;
    TmcRaBadgeGateAccept accept;
    TmcRaBadgeGateHook before_store;
    void* hook_userdata;
} TmcRaBadgeGate;

bool TmcRaBadgeGate_PutArgb(TmcRaBadgeGate* gate, uint64_t generation, const char* url,
                            const uint32_t* pixels, int width, int height);

#endif /* TMC_RA_BADGE_GATE_H */
