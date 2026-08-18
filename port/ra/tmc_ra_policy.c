#include "tmc_ra_policy.h"

static bool TmcRaPolicy_IsAdmitted(NRA_Mode mode, bool game_loaded, bool memory_fully_validated) {
    return mode == NRA_MODE_LIVE_CASUAL && game_loaded && memory_fully_validated;
}

bool TmcRaPolicy_CanAdmitMode(NRA_Mode requested_mode, bool game_loaded, bool memory_fully_validated) {
    return TmcRaPolicy_IsAdmitted(requested_mode, game_loaded, memory_fully_validated);
}

bool TmcRaPolicy_CanSubmit(NRA_Mode mode, bool game_loaded, bool memory_fully_validated) {
    return TmcRaPolicy_IsAdmitted(mode, game_loaded, memory_fully_validated);
}

bool TmcRaPolicy_CanRestoreSaveState(NRA_Mode mode, bool game_loaded, bool memory_fully_validated) {
    return TmcRaPolicy_IsAdmitted(mode, game_loaded, memory_fully_validated);
}

bool TmcRaPolicy_CanFastForward(NRA_Mode mode, bool game_loaded, bool memory_fully_validated) {
    return TmcRaPolicy_IsAdmitted(mode, game_loaded, memory_fully_validated);
}

bool TmcRaPolicy_CanUsePracticeControls(NRA_Mode mode, bool game_loaded, bool memory_fully_validated) {
    return TmcRaPolicy_IsAdmitted(mode, game_loaded, memory_fully_validated);
}
