#include "tmc_ra_policy.h"

#include <assert.h>
#include <stdio.h>

static void AssertCapabilities(NRA_Mode mode, bool game_loaded, bool memory_fully_validated, bool expected) {
    assert(TmcRaPolicy_CanRestoreSaveState(mode, game_loaded, memory_fully_validated) == expected);
    assert(TmcRaPolicy_CanFastForward(mode, game_loaded, memory_fully_validated) == expected);
    assert(TmcRaPolicy_CanUsePracticeControls(mode, game_loaded, memory_fully_validated) == expected);
}

static void AssertDenied(NRA_Mode mode, bool game_loaded, bool memory_fully_validated) {
    assert(!TmcRaPolicy_CanAdmitMode(mode, game_loaded, memory_fully_validated));
    assert(!TmcRaPolicy_CanSubmit(mode, game_loaded, memory_fully_validated));
    AssertCapabilities(mode, game_loaded, memory_fully_validated, false);
}

int main(void) {
    const NRA_Mode invalid_modes[] = {(NRA_Mode)-1, (NRA_Mode)4, (NRA_Mode)255};
    size_t i;

    for (i = 0; i < 4; ++i) {
        bool game_loaded = (i & 1u) != 0;
        bool memory_fully_validated = (i & 2u) != 0;

        assert(!TmcRaPolicy_CanAdmitMode(NRA_MODE_SPECTATOR, game_loaded, memory_fully_validated));
        assert(!TmcRaPolicy_CanSubmit(NRA_MODE_SPECTATOR, game_loaded, memory_fully_validated));
        AssertCapabilities(NRA_MODE_SPECTATOR, game_loaded, memory_fully_validated, false);
    }

    AssertDenied(NRA_MODE_LIVE_CASUAL, false, false);
    AssertDenied(NRA_MODE_LIVE_CASUAL, false, true);
    AssertDenied(NRA_MODE_LIVE_CASUAL, true, false);
    assert(TmcRaPolicy_CanAdmitMode(NRA_MODE_LIVE_CASUAL, true, true));
    assert(TmcRaPolicy_CanSubmit(NRA_MODE_LIVE_CASUAL, true, true));
    AssertCapabilities(NRA_MODE_LIVE_CASUAL, true, true, true);

    for (i = 0; i < 4; ++i) {
        bool game_loaded = (i & 1u) != 0;
        bool memory_fully_validated = (i & 2u) != 0;

        AssertDenied(NRA_MODE_STRICT_UNAPPROVED, game_loaded, memory_fully_validated);
        AssertDenied(NRA_MODE_HARDCORE_APPROVED, game_loaded, memory_fully_validated);
    }

    for (i = 0; i < sizeof(invalid_modes) / sizeof(invalid_modes[0]); ++i)
        AssertDenied(invalid_modes[i], true, true);

    puts("tmc_ra_policy_test: ALL PASS");
    return 0;
}
