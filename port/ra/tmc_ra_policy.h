#ifndef TMC_RA_POLICY_H
#define TMC_RA_POLICY_H

#include "native_ra/native_ra_types.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool TmcRaPolicy_CanAdmitMode(NRA_Mode requested_mode, bool game_loaded, bool memory_fully_validated);

bool TmcRaPolicy_CanSubmit(NRA_Mode mode, bool game_loaded, bool memory_fully_validated);
bool TmcRaPolicy_CanRestoreSaveState(NRA_Mode mode, bool game_loaded, bool memory_fully_validated);
bool TmcRaPolicy_CanFastForward(NRA_Mode mode, bool game_loaded, bool memory_fully_validated);
bool TmcRaPolicy_CanUsePracticeControls(NRA_Mode mode, bool game_loaded, bool memory_fully_validated);

#ifdef __cplusplus
}
#endif

#endif
