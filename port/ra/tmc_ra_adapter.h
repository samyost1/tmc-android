#ifndef TMC_RA_ADAPTER_H
#define TMC_RA_ADAPTER_H

#include "native_ra/native_ra_game_adapter.h"

typedef enum TmcRaAdmission {
    TMC_RA_ADMISSION_UNKNOWN = 0,
    TMC_RA_ADMISSION_SUPPORTED,
    TMC_RA_ADMISSION_UNSUPPORTED,
} TmcRaAdmission;

typedef struct TmcRaAdapter {
    TmcRaAdmission admission;
    uint32_t identified_game_id;
    bool reset_requested;
    bool strict_mode;
    bool attestation_checked;
    bool memory_fully_validated;
} TmcRaAdapter;

void TmcRaAdapter_Init(TmcRaAdapter* adapter);
const NRA_GameAdapterVTable* TmcRaAdapter_VTable(void);
TmcRaAdmission TmcRaAdapter_Admission(const TmcRaAdapter* adapter);
bool TmcRaAdapter_TakeResetRequest(TmcRaAdapter* adapter);
bool TmcRaAdapter_StrictMode(const TmcRaAdapter* adapter);
bool TmcRaAdapter_MemoryFullyValidated(const TmcRaAdapter* adapter);

#endif
