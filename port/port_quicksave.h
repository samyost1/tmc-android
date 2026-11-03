#ifndef PORT_QUICKSAVE_H
#define PORT_QUICKSAVE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

int Port_QuickSave(void);
int Port_QuickLoad(void);
int Port_QuickSave_HasSnapshot(void);

int Port_QuickSave_SaveSlot(int slot);
int Port_QuickSave_LoadSlot(int slot);
int Port_QuickSave_HasSlot(int slot);
uint64_t Port_QuickSave_SlotTimestamp(int slot);

int Port_QuickSave_SavePractice(void);
int Port_QuickSave_LoadPractice(void);
int Port_QuickSave_HasPractice(void);

int Port_QuickSave_SaveAutoResume(void);
int Port_QuickSave_LoadAutoResume(void);
int Port_QuickSave_HasAutoResume(void);

void Port_QuickSave_AutoTick(void);
int Port_QuickSave_AutoEnabled(void);
void Port_QuickSave_SetAutoEnabled(int enabled);
uint32_t Port_QuickSave_AutoIntervalMs(void);
void Port_QuickSave_SetAutoIntervalMs(uint32_t ms);

#ifdef __cplusplus
}
#endif

#endif /* PORT_QUICKSAVE_H */
