#include "tmc_ra_badge_gate.h"

#include "tmc_ra_badge_cache.h"

bool TmcRaBadgeGate_PutArgb(TmcRaBadgeGate* gate, uint64_t generation, const char* url,
                            const uint32_t* pixels, int width, int height) {
    bool stored = false;

    if (gate == NULL || gate->lifecycle_mutex == NULL || gate->state_mutex == NULL ||
        gate->accept == NULL)
        return false;

    pthread_mutex_lock(gate->lifecycle_mutex);
    pthread_mutex_lock(gate->state_mutex);
    if (gate->accept(gate->state, generation)) {
        if (gate->before_store != NULL)
            gate->before_store(gate->hook_userdata);
        stored = TmcRaBadgeCache_PutArgb(url, pixels, width, height);
    }
    pthread_mutex_unlock(gate->state_mutex);
    pthread_mutex_unlock(gate->lifecycle_mutex);
    return stored;
}
