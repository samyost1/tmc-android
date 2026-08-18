#include "../src/native_ra_outbox.h"

#include <stdio.h>

int main(void) {
    const char* url = "https://retroachievements.org/dorequest.php";
    const char* type = "application/x-www-form-urlencoded";
    const char award_ok[] = "{\"Success\":true,\"Score\":1,\"SoftcoreScore\":1,\"AchievementID\":5,\"AchievementsRemaining\":0}";
    const char award_error[] = "{\"Success\":false,\"Error\":\"Nope\"}";
    const char leaderboard_ok[] = "{\"Success\":true,\"Response\":{\"Score\":1234,\"BestScore\":2345,"
        "\"TopEntries\":[],\"RankInfo\":{\"Rank\":5,\"NumEntries\":\"17\"}}}";
    if (nra_outbox_classify_request(url, "r=awardachievement&u=user&t=token&a=5&h=0&v=0123456789abcdef0123456789abcdef", type) != NRA_OUTBOX_ACHIEVEMENT ||
        nra_outbox_classify_request(url, "r=submitlbentry&u=user&t=token&i=4&s=-3&v=0123456789abcdef0123456789abcdef", type) != NRA_OUTBOX_LEADERBOARD ||
        nra_outbox_classify_request(url, "r=awardachievement&u=user&u=other&t=token&a=5&h=0&v=0123456789abcdef0123456789abcdef", type) != NRA_OUTBOX_NONE ||
        nra_outbox_classify_request(url, "r=login2&u=user&t=token", type) != NRA_OUTBOX_NONE ||
        nra_outbox_classify_request("https://example.invalid/dorequest.php", "r=awardachievement&u=user&t=token&a=5&h=0&v=0123456789abcdef0123456789abcdef", type) != NRA_OUTBOX_NONE ||
        !nra_outbox_response_confirmed(NRA_OUTBOX_ACHIEVEMENT, award_ok, sizeof(award_ok) - 1) ||
        nra_outbox_response_confirmed(NRA_OUTBOX_ACHIEVEMENT, award_error, sizeof(award_error) - 1) ||
        !nra_outbox_response_confirmed(NRA_OUTBOX_LEADERBOARD, leaderboard_ok, sizeof(leaderboard_ok) - 1) ||
        nra_outbox_response_confirmed(NRA_OUTBOX_ACHIEVEMENT, award_ok, 0)) {
        return 1;
    }
    puts("NATIVE RA OUTBOX CLASSIFIER OK");
    return 0;
}
