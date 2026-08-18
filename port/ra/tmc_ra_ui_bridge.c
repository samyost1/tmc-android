#include "tmc_ra_ui_bridge.h"

#include <pthread.h>
#include <string.h>

static pthread_mutex_t sMutex = PTHREAD_MUTEX_INITIALIZER;
static NRA_UISnapshot sSnapshots[2];
static unsigned sActiveSnapshot;
static TmcRaUiCommand sCommands[TMC_RA_UI_COMMAND_CAPACITY];
static unsigned sCommandHead;
static unsigned sCommandCount;

void TmcRaUiBridge_Reset(void) {
    pthread_mutex_lock(&sMutex);
    memset(sSnapshots, 0, sizeof(sSnapshots));
    sActiveSnapshot = 0;
    memset(sCommands, 0, sizeof(sCommands));
    sCommandHead = 0;
    sCommandCount = 0;
    pthread_mutex_unlock(&sMutex);
}

void TmcRaUiBridge_Publish(const NRA_UISnapshot* snapshot) {
    unsigned next;

    if (snapshot == NULL)
        return;
    pthread_mutex_lock(&sMutex);
    next = sActiveSnapshot ^ 1u;
    sSnapshots[next] = *snapshot;
    sActiveSnapshot = next;
    pthread_mutex_unlock(&sMutex);
}

bool TmcRaUiBridge_PublishFromContext(NRA_Context* context) {
    NRA_UISnapshot snapshot;

    if (context == NULL || !nra_copy_ui_snapshot(context, &snapshot))
        return false;
    TmcRaUiBridge_Publish(&snapshot);
    return true;
}

bool TmcRaUiBridge_Copy(NRA_UISnapshot* snapshot) {
    if (snapshot == NULL)
        return false;
    pthread_mutex_lock(&sMutex);
    *snapshot = sSnapshots[sActiveSnapshot];
    pthread_mutex_unlock(&sMutex);
    return snapshot->available;
}

bool TmcRaUiBridge_EnqueueCommand(const TmcRaUiCommand* command) {
    unsigned tail;
    unsigned i;

    if (command == NULL || command->kind == TMC_RA_UI_COMMAND_NONE)
        return false;
    pthread_mutex_lock(&sMutex);
    if (command->kind == TMC_RA_UI_COMMAND_REQUEST_PASSWORD_LOGIN) {
        for (i = 0; i < sCommandCount; i++) {
            unsigned index = (sCommandHead + i) % TMC_RA_UI_COMMAND_CAPACITY;
            if (sCommands[index].kind == command->kind) {
                pthread_mutex_unlock(&sMutex);
                return false;
            }
        }
    }
    if (sCommandCount == TMC_RA_UI_COMMAND_CAPACITY) {
        pthread_mutex_unlock(&sMutex);
        return false;
    }
    tail = (sCommandHead + sCommandCount) % TMC_RA_UI_COMMAND_CAPACITY;
    sCommands[tail] = *command;
    ++sCommandCount;
    pthread_mutex_unlock(&sMutex);
    return true;
}

bool TmcRaUiBridge_TakeCommand(TmcRaUiCommand* command) {
    if (command == NULL)
        return false;
    pthread_mutex_lock(&sMutex);
    if (sCommandCount == 0) {
        pthread_mutex_unlock(&sMutex);
        return false;
    }
    *command = sCommands[sCommandHead];
    memset(&sCommands[sCommandHead], 0, sizeof(sCommands[sCommandHead]));
    sCommandHead = (sCommandHead + 1) % TMC_RA_UI_COMMAND_CAPACITY;
    --sCommandCount;
    pthread_mutex_unlock(&sMutex);
    return true;
}
