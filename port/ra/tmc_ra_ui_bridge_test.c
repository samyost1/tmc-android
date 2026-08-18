#include "tmc_ra_ui_bridge.h"

#include <stdio.h>
#include <string.h>

static int expect(bool condition, const char* message) {
    if (!condition)
        fprintf(stderr, "FAIL: %s\n", message);
    return condition ? 0 : 1;
}

int main(void) {
    NRA_UISnapshot source;
    NRA_UISnapshot copy;
    TmcRaUiCommand command;
    TmcRaUiCommand taken;
    int failures = 0;

    TmcRaUiBridge_Reset();
    memset(&copy, 0, sizeof(copy));
    failures += expect(!TmcRaUiBridge_Copy(&copy), "reset snapshot is unavailable");

    memset(&source, 0, sizeof(source));
    source.available = true;
    source.version = NRA_UI_SNAPSHOT_VERSION;
    source.generation = 42;
    source.mode = NRA_MODE_LIVE_CASUAL;
    source.logged_in = true;
    source.game_loaded = true;
    source.game_id = 1234;
    source.game_supported = true;
    snprintf(source.account_name, sizeof(source.account_name), "test-user");
    snprintf(source.game_title, sizeof(source.game_title), "The Minish Cap");
    snprintf(source.rich_presence, sizeof(source.rich_presence), "Testing P5");
    source.achievement_count = 1;
    source.achievements[0].id = 77;
    source.achievements[0].points = 5;
    source.achievements[0].unlocked = true;
    snprintf(source.achievements[0].title, sizeof(source.achievements[0].title), "First");
    snprintf(source.achievements[0].badge_key, sizeof(source.achievements[0].badge_key), "badge-77");
    source.toast_count = 1;
    source.toasts[0].sequence = 9;
    source.toasts[0].kind = NRA_UI_TOAST_ACHIEVEMENT_UNLOCKED;
    snprintf(source.toasts[0].title, sizeof(source.toasts[0].title), "First");

    TmcRaUiBridge_Publish(&source);
    failures += expect(TmcRaUiBridge_Copy(&copy), "published snapshot is available");
    failures += expect(copy.generation == 42 && copy.achievements[0].id == 77,
                       "achievement snapshot is copied by value");
    failures += expect(strcmp(copy.rich_presence, "Testing P5") == 0 &&
                           copy.toasts[0].sequence == 9,
                       "rich presence and toast snapshot are copied");

    TmcRaUiBridge_Reset();
    memset(&command, 0, sizeof(command));
    command.kind = TMC_RA_UI_COMMAND_REQUEST_PASSWORD_LOGIN;
    failures += expect(TmcRaUiBridge_EnqueueCommand(&command), "login command is accepted");
    failures += expect(!TmcRaUiBridge_EnqueueCommand(&command), "pending login is coalesced");
    command.kind = TMC_RA_UI_COMMAND_LOGOUT;
    failures += expect(TmcRaUiBridge_EnqueueCommand(&command), "logout command is accepted");
    command.kind = TMC_RA_UI_COMMAND_REQUEST_MODE;
    command.mode = NRA_MODE_LIVE_CASUAL;
    failures += expect(TmcRaUiBridge_EnqueueCommand(&command), "mode command is accepted");
    failures += expect(TmcRaUiBridge_TakeCommand(&taken) &&
                           taken.kind == TMC_RA_UI_COMMAND_REQUEST_PASSWORD_LOGIN,
                       "commands are FIFO");
    failures += expect(TmcRaUiBridge_EnqueueCommand(&taken), "login can be queued after it is consumed");
    failures += expect(TmcRaUiBridge_TakeCommand(&taken) &&
                           taken.kind == TMC_RA_UI_COMMAND_LOGOUT,
                       "logout follows login");
    failures += expect(TmcRaUiBridge_TakeCommand(&taken) &&
                           taken.kind == TMC_RA_UI_COMMAND_REQUEST_MODE &&
                           taken.mode == NRA_MODE_LIVE_CASUAL,
                       "mode command follows logout");
    failures += expect(TmcRaUiBridge_TakeCommand(&taken) &&
                           taken.kind == TMC_RA_UI_COMMAND_REQUEST_PASSWORD_LOGIN,
                       "consumed login clears coalescing");

    TmcRaUiBridge_Reset();
    memset(&command, 0, sizeof(command));
    command.kind = TMC_RA_UI_COMMAND_LOGOUT;
    for (unsigned i = 0; i < TMC_RA_UI_COMMAND_CAPACITY; i++)
        failures += expect(TmcRaUiBridge_EnqueueCommand(&command), "queue accepts capacity");
    failures += expect(!TmcRaUiBridge_EnqueueCommand(&command), "queue rejects overflow");

    if (failures != 0)
        return 1;
    puts("tmc_ra_ui_bridge_test: ALL PASS");
    return 0;
}
