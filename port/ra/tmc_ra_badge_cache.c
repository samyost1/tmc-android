#include "tmc_ra_badge_cache.h"

#include <pthread.h>
#include <stddef.h>
#include <string.h>

typedef struct {
    bool used;
    char url[NRA_UI_IMAGE_URL_MAX];
    TmcRaBadgeImage image;
} TmcRaBadgeCacheEntry;

static pthread_mutex_t sMutex = PTHREAD_MUTEX_INITIALIZER;
static TmcRaBadgeCacheEntry sEntries[TMC_RA_BADGE_CACHE_CAPACITY];
static unsigned sNextReplacement;

static uint32_t ArgbToNative(uint32_t argb) {
    const uint32_t r = (argb >> 16) & 0xffu;
    const uint32_t g = (argb >> 8) & 0xffu;
    const uint32_t b = argb & 0xffu;
    return (argb & 0xff000000u) | (b << 16) | (g << 8) | r;
}

static int FindEntry(const char* url) {
    unsigned i;

    for (i = 0; i < TMC_RA_BADGE_CACHE_CAPACITY; ++i) {
        if (sEntries[i].used && strcmp(sEntries[i].url, url) == 0)
            return (int)i;
    }
    return -1;
}

bool TmcRaBadgeCache_PutArgb(const char* url, const uint32_t* pixels, int width, int height) {
    TmcRaBadgeCacheEntry* entry;
    size_t pixel_count;
    size_t i;
    int index;

    if (url == NULL || pixels == NULL || url[0] == '\0' || strlen(url) >= NRA_UI_IMAGE_URL_MAX ||
        width < 1 || height < 1 || width > TMC_RA_BADGE_MAX_DIMENSION ||
        height > TMC_RA_BADGE_MAX_DIMENSION)
        return false;

    pixel_count = (size_t)width * (size_t)height;
    pthread_mutex_lock(&sMutex);
    index = FindEntry(url);
    if (index < 0) {
        index = (int)sNextReplacement;
        sNextReplacement = (sNextReplacement + 1u) % TMC_RA_BADGE_CACHE_CAPACITY;
    }
    entry = &sEntries[index];
    memset(entry, 0, sizeof(*entry));
    entry->used = true;
    memcpy(entry->url, url, strlen(url) + 1);
    entry->image.width = width;
    entry->image.height = height;
    for (i = 0; i < pixel_count; ++i)
        entry->image.pixels[i] = ArgbToNative(pixels[i]);
    pthread_mutex_unlock(&sMutex);
    return true;
}

bool TmcRaBadgeCache_Copy(const char* url, TmcRaBadgeImage* image) {
    int index;

    if (url == NULL || image == NULL || url[0] == '\0')
        return false;
    pthread_mutex_lock(&sMutex);
    index = FindEntry(url);
    if (index >= 0)
        *image = sEntries[index].image;
    pthread_mutex_unlock(&sMutex);
    return index >= 0;
}

void TmcRaBadgeCache_Reset(void) {
    pthread_mutex_lock(&sMutex);
    memset(sEntries, 0, sizeof(sEntries));
    sNextReplacement = 0;
    pthread_mutex_unlock(&sMutex);
}
