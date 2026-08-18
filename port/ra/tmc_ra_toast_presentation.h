#ifndef TMC_RA_TOAST_PRESENTATION_H
#define TMC_RA_TOAST_PRESENTATION_H

#include "native_ra/native_ra_types.h"

#include <stdbool.h>

typedef struct TmcRaToastPresentation {
    NRA_UIToast toast;
    uint64_t shown_sequence;
    double expiry;
} TmcRaToastPresentation;

void TmcRaToastPresentation_Reset(TmcRaToastPresentation* presentation);
void TmcRaToastPresentation_Update(TmcRaToastPresentation* presentation,
                                    const NRA_UISnapshot* snapshot, double now);
bool TmcRaToastPresentation_CopyVisible(const TmcRaToastPresentation* presentation, double now,
                                        NRA_UIToast* toast);

#endif /* TMC_RA_TOAST_PRESENTATION_H */
