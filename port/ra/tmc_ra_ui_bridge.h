#ifndef TMC_RA_UI_BRIDGE_H
#define TMC_RA_UI_BRIDGE_H

#include "native_ra/native_ra.h"

#include <stdbool.h>

#define TMC_RA_UI_COMMAND_CAPACITY 8u

typedef enum TmcRaUiCommandKind {
    TMC_RA_UI_COMMAND_NONE = 0,
    TMC_RA_UI_COMMAND_REQUEST_PASSWORD_LOGIN,
    TMC_RA_UI_COMMAND_LOGOUT,
    TMC_RA_UI_COMMAND_REQUEST_MODE
} TmcRaUiCommandKind;

typedef struct TmcRaUiCommand {
    TmcRaUiCommandKind kind;
    NRA_Mode mode;
} TmcRaUiCommand;

/*
 * Owner-thread RA state is copied into a value-only presentation snapshot.
 * Render/JNI threads may copy that snapshot, but never receive NRA_Context
 * or rcheevos-owned pointers.
 */
void TmcRaUiBridge_Reset(void);
void TmcRaUiBridge_Publish(const NRA_UISnapshot* snapshot);
bool TmcRaUiBridge_PublishFromContext(NRA_Context* context);
bool TmcRaUiBridge_Copy(NRA_UISnapshot* snapshot);

/* Bounded FIFO consumed by the game/owner thread. */
bool TmcRaUiBridge_EnqueueCommand(const TmcRaUiCommand* command);
bool TmcRaUiBridge_TakeCommand(TmcRaUiCommand* command);

#endif /* TMC_RA_UI_BRIDGE_H */
