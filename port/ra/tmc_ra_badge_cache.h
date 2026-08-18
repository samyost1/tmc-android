#ifndef TMC_RA_BADGE_CACHE_H
#define TMC_RA_BADGE_CACHE_H

#include "native_ra/native_ra_types.h"

#include <stdbool.h>
#include <stdint.h>

#define TMC_RA_BADGE_CACHE_CAPACITY 16u
#define TMC_RA_BADGE_MAX_DIMENSION 64

typedef struct TmcRaBadgeImage {
    int width;
    int height;
    uint32_t pixels[TMC_RA_BADGE_MAX_DIMENSION * TMC_RA_BADGE_MAX_DIMENSION];
} TmcRaBadgeImage;

/*
 * Java decodes the bounded PNG response into Android ARGB pixels. The cache
 * converts them once to the native RGBA8888 word layout used by the panel and
 * publishes a copy-safe image to the render thread.
 */
bool TmcRaBadgeCache_PutArgb(const char* url, const uint32_t* pixels, int width, int height);
bool TmcRaBadgeCache_Copy(const char* url, TmcRaBadgeImage* image);
void TmcRaBadgeCache_Reset(void);

#endif /* TMC_RA_BADGE_CACHE_H */
