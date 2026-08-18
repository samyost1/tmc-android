#include "tmc_ra_badge_cache.h"

#include <stdio.h>
#include <string.h>

int main(void) {
    const uint32_t argb[] = {
        0xFF112233u, 0x80102030u,
        0x00000000u, 0xFFFFFFFFu,
    };
    TmcRaBadgeImage image;
    int failures = 0;

    TmcRaBadgeCache_Reset();
    failures += !TmcRaBadgeCache_PutArgb("https://media.retroachievements.org/Badge/test.png",
                                         argb, 2, 2);
    failures += !TmcRaBadgeCache_Copy("https://media.retroachievements.org/Badge/test.png", &image);
    failures += image.width != 2 || image.height != 2;
    failures += image.pixels[0] != 0xFF332211u;
    failures += image.pixels[1] != 0x80302010u;
    failures += image.pixels[2] != 0x00000000u;
    failures += image.pixels[3] != 0xFFFFFFFFu;
    failures += TmcRaBadgeCache_Copy("https://media.retroachievements.org/Badge/missing.png", &image);

    if (failures != 0) {
        fprintf(stderr, "tmc_ra_badge_cache_test: %d failures\n", failures);
        return 1;
    }
    puts("tmc_ra_badge_cache_test: ALL PASS");
    return 0;
}
