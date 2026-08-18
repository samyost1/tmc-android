#ifndef TMC_RA_ANDROID_H
#define TMC_RA_ANDROID_H

#include "native_ra/native_ra.h"

#include <stdbool.h>

bool TmcRaAndroid_IsRegistered(void);
const NRA_PlatformVTable* TmcRaAndroid_Platform(void);
void* TmcRaAndroid_PlatformUserdata(void);
void TmcRaAndroid_RequestPasswordLogin(void);
void TmcRaAndroid_Drain(NRA_Context* context);

#endif
