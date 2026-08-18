#include "tmc_ra_toast_presentation.h"

#include <stdio.h>
#include <string.h>

int main(void) {
    TmcRaToastPresentation presentation;
    NRA_UISnapshot snapshot;
    NRA_UIToast visible;

    TmcRaToastPresentation_Reset(&presentation);
    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.toast_count = 1;
    snapshot.toasts[0].sequence = 7;
    snapshot.toasts[0].kind = NRA_UI_TOAST_ACHIEVEMENT_UNLOCKED;
    snprintf(snapshot.toasts[0].title, sizeof(snapshot.toasts[0].title), "First");

    TmcRaToastPresentation_Update(&presentation, &snapshot, 10.0);
    if (!TmcRaToastPresentation_CopyVisible(&presentation, 13.9, &visible) ||
        visible.sequence != 7 ||
        TmcRaToastPresentation_CopyVisible(&presentation, 14.0, &visible)) {
        fprintf(stderr, "toast expiry failed\n");
        return 1;
    }

    TmcRaToastPresentation_Update(&presentation, &snapshot, 11.0);
    if (!TmcRaToastPresentation_CopyVisible(&presentation, 13.9, &visible)) {
        fprintf(stderr, "unchanged toast unexpectedly re-armed\n");
        return 1;
    }

    snapshot.toasts[0].sequence = 8;
    TmcRaToastPresentation_Update(&presentation, &snapshot, 30.0);
    if (!TmcRaToastPresentation_CopyVisible(&presentation, 33.9, &visible) ||
        visible.sequence != 8) {
        fprintf(stderr, "new toast did not re-arm\n");
        return 1;
    }

    snapshot.toast_count = 2;
    snapshot.toasts[0].sequence = 9;
    snprintf(snapshot.toasts[0].title, sizeof(snapshot.toasts[0].title), "Second");
    snapshot.toasts[1].sequence = 10;
    snprintf(snapshot.toasts[1].title, sizeof(snapshot.toasts[1].title), "Third");
    TmcRaToastPresentation_Update(&presentation, &snapshot, 31.0);
    if (!TmcRaToastPresentation_CopyVisible(&presentation, 33.9, &visible) ||
        visible.sequence != 8) {
        fprintf(stderr, "active toast was replaced by burst\n");
        return 1;
    }
    TmcRaToastPresentation_Update(&presentation, &snapshot, 34.0);
    if (!TmcRaToastPresentation_CopyVisible(&presentation, 37.9, &visible) ||
        visible.sequence != 9) {
        fprintf(stderr, "first burst toast did not get an interval\n");
        return 1;
    }
    TmcRaToastPresentation_Update(&presentation, &snapshot, 38.0);
    if (!TmcRaToastPresentation_CopyVisible(&presentation, 41.9, &visible) ||
        visible.sequence != 10) {
        fprintf(stderr, "second burst toast did not get an interval\n");
        return 1;
    }

    puts("tmc_ra_toast_presentation_test: ALL PASS");
    return 0;
}
