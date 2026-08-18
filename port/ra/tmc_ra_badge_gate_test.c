#include "tmc_ra_badge_gate.h"

#include "tmc_ra_badge_cache.h"

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    pthread_mutex_t lifecycle_mutex;
    pthread_mutex_t state_mutex;
    pthread_mutex_t hook_mutex;
    pthread_cond_t hook_condition;
    bool active;
    uint64_t generation;
    bool hook_entered;
    bool hook_release;
    bool detach_started;
    bool detach_done;
    bool put_result;
    TmcRaBadgeGate gate;
} Fixture;

static bool Accept(void* userdata, uint64_t generation) {
    Fixture* fixture = userdata;
    return fixture->active && fixture->generation == generation;
}

static void PauseBeforeStore(void* userdata) {
    Fixture* fixture = userdata;

    pthread_mutex_lock(&fixture->hook_mutex);
    fixture->hook_entered = true;
    pthread_cond_broadcast(&fixture->hook_condition);
    while (!fixture->hook_release)
        pthread_cond_wait(&fixture->hook_condition, &fixture->hook_mutex);
    pthread_mutex_unlock(&fixture->hook_mutex);
}

static void* PutBadge(void* userdata) {
    Fixture* fixture = userdata;
    const uint32_t pixel = 0xFF112233u;

    fixture->put_result = TmcRaBadgeGate_PutArgb(
        &fixture->gate, 7, "https://media.retroachievements.org/Badge/race.png", &pixel, 1, 1);
    return NULL;
}

static void* Detach(void* userdata) {
    Fixture* fixture = userdata;

    pthread_mutex_lock(&fixture->hook_mutex);
    fixture->detach_started = true;
    pthread_cond_broadcast(&fixture->hook_condition);
    pthread_mutex_unlock(&fixture->hook_mutex);

    pthread_mutex_lock(&fixture->lifecycle_mutex);
    pthread_mutex_lock(&fixture->state_mutex);
    fixture->active = false;
    ++fixture->generation;
    pthread_mutex_unlock(&fixture->state_mutex);
    TmcRaBadgeCache_Reset();
    pthread_mutex_unlock(&fixture->lifecycle_mutex);

    pthread_mutex_lock(&fixture->hook_mutex);
    fixture->detach_done = true;
    pthread_cond_broadcast(&fixture->hook_condition);
    pthread_mutex_unlock(&fixture->hook_mutex);
    return NULL;
}

static int WaitFor(bool* value, Fixture* fixture) {
    pthread_mutex_lock(&fixture->hook_mutex);
    while (!*value)
        pthread_cond_wait(&fixture->hook_condition, &fixture->hook_mutex);
    pthread_mutex_unlock(&fixture->hook_mutex);
    return 0;
}

int main(void) {
    Fixture fixture;
    pthread_t put_thread;
    pthread_t detach_thread;
    TmcRaBadgeImage image;
    int failures = 0;

    memset(&fixture, 0, sizeof(fixture));
    pthread_mutex_init(&fixture.lifecycle_mutex, NULL);
    pthread_mutex_init(&fixture.state_mutex, NULL);
    pthread_mutex_init(&fixture.hook_mutex, NULL);
    pthread_cond_init(&fixture.hook_condition, NULL);
    fixture.active = true;
    fixture.generation = 7;
    fixture.gate.lifecycle_mutex = &fixture.lifecycle_mutex;
    fixture.gate.state_mutex = &fixture.state_mutex;
    fixture.gate.state = &fixture;
    fixture.gate.accept = Accept;
    fixture.gate.before_store = PauseBeforeStore;
    fixture.gate.hook_userdata = &fixture;

    TmcRaBadgeCache_Reset();
    pthread_create(&put_thread, NULL, PutBadge, &fixture);
    WaitFor(&fixture.hook_entered, &fixture);
    pthread_create(&detach_thread, NULL, Detach, &fixture);
    WaitFor(&fixture.detach_started, &fixture);

    pthread_mutex_lock(&fixture.hook_mutex);
    failures += fixture.detach_done;
    fixture.hook_release = true;
    pthread_cond_broadcast(&fixture.hook_condition);
    pthread_mutex_unlock(&fixture.hook_mutex);

    pthread_join(put_thread, NULL);
    pthread_join(detach_thread, NULL);
    failures += !fixture.put_result;
    failures += !fixture.detach_done;
    failures += TmcRaBadgeCache_Copy("https://media.retroachievements.org/Badge/race.png", &image);
    failures += TmcRaBadgeGate_PutArgb(
        &fixture.gate, 7, "https://media.retroachievements.org/Badge/race.png",
        (const uint32_t[]){0xFFFFFFFFu}, 1, 1);

    pthread_cond_destroy(&fixture.hook_condition);
    pthread_mutex_destroy(&fixture.hook_mutex);
    pthread_mutex_destroy(&fixture.state_mutex);
    pthread_mutex_destroy(&fixture.lifecycle_mutex);

    if (failures != 0) {
        fprintf(stderr, "tmc_ra_badge_gate_test: %d failures\n", failures);
        return 1;
    }
    puts("tmc_ra_badge_gate_test: ALL PASS");
    return 0;
}
