#include "ach_event.h"

namespace vj {

namespace {
std::string str(const char* s) { return s ? std::string(s) : std::string(); }
}  // namespace

bool achKeepStaleEvent(uint32_t type) {
    switch (type) {
    case RC_CLIENT_EVENT_LEADERBOARD_TRACKER_SHOW:
    case RC_CLIENT_EVENT_LEADERBOARD_TRACKER_UPDATE:
    case RC_CLIENT_EVENT_LEADERBOARD_TRACKER_HIDE:
    case RC_CLIENT_EVENT_ACHIEVEMENT_CHALLENGE_INDICATOR_SHOW:
    case RC_CLIENT_EVENT_ACHIEVEMENT_CHALLENGE_INDICATOR_HIDE:
    case RC_CLIENT_EVENT_ACHIEVEMENT_PROGRESS_INDICATOR_SHOW:
    case RC_CLIENT_EVENT_ACHIEVEMENT_PROGRESS_INDICATOR_UPDATE:
    case RC_CLIENT_EVENT_ACHIEVEMENT_PROGRESS_INDICATOR_HIDE:
    case RC_CLIENT_EVENT_LEADERBOARD_STARTED:
    case RC_CLIENT_EVENT_LEADERBOARD_FAILED:
    case RC_CLIENT_EVENT_RESET:
        return false;
    default:
        return true;   // ACHIEVEMENT_TRIGGERED, GAME/SUBSET_COMPLETED, SUBMITTED, SCOREBOARD, SERVER_ERROR, (RE)CONNECTED
    }
}

AchEvent achEventFrom(const rc_client_event_t& e, bool hardcore) {
    AchEvent o;
    o.type = e.type;
    o.hardcore = hardcore;
    if (e.achievement) {
        o.id = e.achievement->id;
        o.title = str(e.achievement->title);
        o.description = str(e.achievement->description);
        o.badgeUrl = str(e.achievement->badge_url);
        o.points = e.achievement->points;
        o.measured = e.achievement->measured_progress;   // a fixed char array, always terminated by rc_client
    }
    if (e.leaderboard) {
        o.id = e.leaderboard->id;
        o.title = str(e.leaderboard->title);
    }
    if (e.leaderboard_tracker) {
        o.id = e.leaderboard_tracker->id;
        o.display = e.leaderboard_tracker->display;
    }
    if (e.leaderboard_scoreboard) {
        o.submittedScore = e.leaderboard_scoreboard->submitted_score;
        o.newRank = e.leaderboard_scoreboard->new_rank;
        o.numEntries = e.leaderboard_scoreboard->num_entries;
    }
    if (e.server_error) {
        o.api = str(e.server_error->api);
        o.error = str(e.server_error->error_message);
    }
    if (e.subset) o.subsetTitle = str(e.subset->title);
    return o;
}

}  // namespace vj
