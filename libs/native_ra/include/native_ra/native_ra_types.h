#ifndef NATIVE_RA_TYPES_H
#define NATIVE_RA_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NRA_ABI_VERSION 4u

typedef enum NRA_Result {
    NRA_OK = 0,
    NRA_PENDING,
    NRA_DISABLED,
    NRA_UNSUPPORTED,
    NRA_INVALID_ARGUMENT,
    NRA_INVALID_STATE,
    NRA_IO_ERROR,
    NRA_NETWORK_ERROR,
    NRA_AUTH_ERROR,
    NRA_MEMORY_UNVERIFIED,
    NRA_INTERNAL_ERROR
} NRA_Result;

typedef enum NRA_Mode {
    NRA_MODE_SPECTATOR = 0,
    NRA_MODE_LIVE_CASUAL = 1,
    NRA_MODE_STRICT_UNAPPROVED = 2,
    NRA_MODE_HARDCORE_APPROVED = 3
} NRA_Mode;

typedef uint64_t NRA_RequestId;

typedef struct NRA_StatusSnapshot {
    NRA_Mode mode;
    bool client_created;
    bool game_loaded;
    bool request_pending;
    bool logged_in;
    bool load_pending;
} NRA_StatusSnapshot;

#define NRA_UI_SNAPSHOT_VERSION 2u
#define NRA_UI_ACCOUNT_NAME_MAX 64u
#define NRA_UI_GAME_TITLE_MAX 128u
#define NRA_UI_RICH_PRESENCE_MAX 256u
#define NRA_UI_TEXT_MAX 128u
#define NRA_UI_BADGE_KEY_MAX 16u
#define NRA_UI_IMAGE_URL_MAX 256u
#define NRA_UI_MEASURED_PROGRESS_MAX 32u
#define NRA_UI_ACHIEVEMENT_MAX 128u
#define NRA_UI_TOAST_MAX 8u

typedef enum NRA_UIConnection {
    NRA_UI_CONNECTION_UNKNOWN = 0,
    NRA_UI_CONNECTION_ONLINE,
    NRA_UI_CONNECTION_OFFLINE
} NRA_UIConnection;

typedef enum NRA_UIToastKind {
    NRA_UI_TOAST_NONE = 0,
    NRA_UI_TOAST_ACHIEVEMENT_UNLOCKED,
    NRA_UI_TOAST_LEADERBOARD_STARTED,
    NRA_UI_TOAST_LEADERBOARD_FAILED,
    NRA_UI_TOAST_LEADERBOARD_SUBMITTED,
    NRA_UI_TOAST_GAME_COMPLETED,
    NRA_UI_TOAST_SUBSET_COMPLETED,
    NRA_UI_TOAST_DISCONNECTED,
    NRA_UI_TOAST_RECONNECTED,
    NRA_UI_TOAST_ERROR
} NRA_UIToastKind;

typedef struct NRA_UIAchievement {
    uint32_t id;
    uint32_t points;
    bool unlocked;
    char title[NRA_UI_TEXT_MAX];
    char description[NRA_UI_TEXT_MAX];
    char badge_key[NRA_UI_BADGE_KEY_MAX];
    char badge_url[NRA_UI_IMAGE_URL_MAX];
    char measured_progress[NRA_UI_MEASURED_PROGRESS_MAX];
} NRA_UIAchievement;

typedef struct NRA_UIToast {
    uint64_t sequence;
    NRA_UIToastKind kind;
    uint32_t related_id;
    uint32_t points;
    char title[NRA_UI_TEXT_MAX];
    char description[NRA_UI_TEXT_MAX];
    char badge_key[NRA_UI_BADGE_KEY_MAX];
    char badge_url[NRA_UI_IMAGE_URL_MAX];
    char measured_progress[NRA_UI_MEASURED_PROGRESS_MAX];
} NRA_UIToast;

typedef struct NRA_UISnapshot {
    bool available;
    uint32_t version;
    uint64_t generation;
    NRA_Mode mode;
    bool logged_in;
    uint32_t account_score;
    char account_name[NRA_UI_ACCOUNT_NAME_MAX];
    bool game_loaded;
    uint32_t game_id;
    bool game_supported;
    char game_title[NRA_UI_GAME_TITLE_MAX];
    NRA_UIConnection connection;
    char rich_presence[NRA_UI_RICH_PRESENCE_MAX];
    uint32_t achievements_total;
    uint32_t achievements_unlocked;
    uint32_t achievement_points_total;
    uint32_t achievement_points_unlocked;
    uint32_t achievement_count;
    NRA_UIAchievement achievements[NRA_UI_ACHIEVEMENT_MAX];
    bool challenge_active;
    NRA_UIAchievement challenge;
    bool progress_active;
    NRA_UIAchievement progress;
    bool leaderboard_tracker_active;
    uint32_t leaderboard_tracker_id;
    char leaderboard_tracker_value[NRA_UI_TEXT_MAX];
    bool recent_leaderboard_result;
    uint32_t recent_leaderboard_id;
    uint32_t recent_leaderboard_rank;
    char recent_leaderboard_score[NRA_UI_TEXT_MAX];
    uint32_t toast_count;
    NRA_UIToast toasts[NRA_UI_TOAST_MAX];
    bool has_error;
    char error[NRA_UI_TEXT_MAX];
} NRA_UISnapshot;

typedef enum NRA_UICommandKind {
    NRA_UI_COMMAND_NONE = 0,
    NRA_UI_COMMAND_REQUEST_PASSWORD_LOGIN = 1
} NRA_UICommandKind;

typedef struct NRA_UICommand {
    NRA_UICommandKind kind;
} NRA_UICommand;

typedef struct NRA_CapabilityPolicy {
    bool spectator_mode;
    bool live_casual_mode;
    bool strict_mode;
} NRA_CapabilityPolicy;

#endif
