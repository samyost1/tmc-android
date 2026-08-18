#include "tmc_ra_toast_presentation.h"

#include <string.h>

void TmcRaToastPresentation_Reset(TmcRaToastPresentation* presentation) {
    if (presentation != NULL)
        memset(presentation, 0, sizeof(*presentation));
}

void TmcRaToastPresentation_Update(TmcRaToastPresentation* presentation,
                                    const NRA_UISnapshot* snapshot, double now) {
    size_t i;

    if (presentation == NULL || snapshot == NULL)
        return;
    if (snapshot->toast_count == 0) {
        presentation->shown_sequence = 0;
        presentation->expiry = 0;
        memset(&presentation->toast, 0, sizeof(presentation->toast));
        return;
    }
    if (now < presentation->expiry)
        return;

    /*
     * The native snapshot retains a bounded, oldest-to-newest event list.
     * Promote the first event not shown yet so a burst gets one full
     * presentation interval per toast instead of losing all but the newest.
     */
    for (i = 0; i < snapshot->toast_count; ++i) {
        if (snapshot->toasts[i].sequence > presentation->shown_sequence) {
            presentation->toast = snapshot->toasts[i];
            presentation->shown_sequence = snapshot->toasts[i].sequence;
            presentation->expiry = now + 4.0;
            return;
        }
    }
    presentation->expiry = 0;
}

bool TmcRaToastPresentation_CopyVisible(const TmcRaToastPresentation* presentation, double now,
                                        NRA_UIToast* toast) {
    if (presentation == NULL || toast == NULL || now >= presentation->expiry)
        return false;
    *toast = presentation->toast;
    return true;
}
