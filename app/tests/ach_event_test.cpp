// AchEvent (ach_event.h): an owned copy of an rc_client event, so the handler can queue it on whichever thread raised
// it and the UI thread can apply it later (#967 phase 2).  rc_client's strings are valid for the call only.
//
//   vj_ach_event_test
#include "ach_event.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace {
int g_fail = 0;
#define CHECK(c)                                                    \
    do {                                                            \
        if (!(c)) {                                                 \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); \
            g_fail++;                                               \
        }                                                           \
    } while (0)
}  // namespace

int main() {
    char title[32], desc[32], badge[64], measured[16];
    std::strcpy(title, "First steps");
    std::strcpy(desc, "Do a thing");
    std::strcpy(badge, "https://example.invalid/b.png");
    std::strcpy(measured, "3/10");
    rc_client_achievement_t a;
    std::memset(&a, 0, sizeof a);
    a.id = 1001;
    a.title = title;
    a.description = desc;
    a.badge_url = badge;
    a.points = 5;
    std::strcpy(a.measured_progress, measured);   // a char array in rc_client_achievement_t
    rc_client_event_t e;
    std::memset(&e, 0, sizeof e);
    e.type = RC_CLIENT_EVENT_ACHIEVEMENT_TRIGGERED;
    e.achievement = &a;
    vj::AchEvent c = vj::achEventFrom(e, true);
    std::memset(title, 'x', sizeof title - 1);   // the source goes away after the handler returns
    std::memset(desc, 'x', sizeof desc - 1);
    CHECK(c.type == RC_CLIENT_EVENT_ACHIEVEMENT_TRIGGERED);
    CHECK(c.id == 1001 && c.points == 5 && c.hardcore);
    CHECK(c.title == "First steps" && c.description == "Do a thing" && c.badgeUrl == "https://example.invalid/b.png");
    CHECK(c.measured == "3/10");

    rc_client_leaderboard_tracker_t t;
    std::memset(&t, 0, sizeof t);
    t.id = 7;
    std::strcpy(t.display, "00:12.3");
    std::memset(&e, 0, sizeof e);
    e.type = RC_CLIENT_EVENT_LEADERBOARD_TRACKER_UPDATE;
    e.leaderboard_tracker = &t;
    c = vj::achEventFrom(e, false);
    CHECK(c.id == 7 && c.display == "00:12.3" && !c.hardcore);

    rc_client_server_error_t se;
    std::memset(&se, 0, sizeof se);
    se.api = "awardachievement";
    se.error_message = "nope";
    std::memset(&e, 0, sizeof e);
    e.type = RC_CLIENT_EVENT_SERVER_ERROR;
    e.server_error = &se;
    c = vj::achEventFrom(e, false);
    CHECK(c.api == "awardachievement" && c.error == "nope");

    std::memset(&e, 0, sizeof e);   // every pointer null: no crash, empty strings
    e.type = RC_CLIENT_EVENT_LEADERBOARD_STARTED;
    c = vj::achEventFrom(e, false);
    CHECK(c.title.empty());

    // A content switch between queueing and draining: transient state of the old game is dropped, real results stay.
    CHECK(!vj::achKeepStaleEvent(RC_CLIENT_EVENT_LEADERBOARD_TRACKER_UPDATE));
    CHECK(!vj::achKeepStaleEvent(RC_CLIENT_EVENT_LEADERBOARD_TRACKER_SHOW));
    CHECK(!vj::achKeepStaleEvent(RC_CLIENT_EVENT_LEADERBOARD_TRACKER_HIDE));
    CHECK(!vj::achKeepStaleEvent(RC_CLIENT_EVENT_ACHIEVEMENT_CHALLENGE_INDICATOR_SHOW));
    CHECK(!vj::achKeepStaleEvent(RC_CLIENT_EVENT_ACHIEVEMENT_PROGRESS_INDICATOR_UPDATE));
    CHECK(!vj::achKeepStaleEvent(RC_CLIENT_EVENT_LEADERBOARD_STARTED));
    CHECK(!vj::achKeepStaleEvent(RC_CLIENT_EVENT_LEADERBOARD_FAILED));
    CHECK(!vj::achKeepStaleEvent(RC_CLIENT_EVENT_RESET));
    CHECK(vj::achKeepStaleEvent(RC_CLIENT_EVENT_ACHIEVEMENT_TRIGGERED));
    CHECK(vj::achKeepStaleEvent(RC_CLIENT_EVENT_SERVER_ERROR));
    CHECK(vj::achKeepStaleEvent(RC_CLIENT_EVENT_DISCONNECTED));
    CHECK(vj::achKeepStaleEvent(RC_CLIENT_EVENT_RECONNECTED));
    CHECK(vj::achKeepStaleEvent(RC_CLIENT_EVENT_LEADERBOARD_SUBMITTED));

    std::printf("ach_event_test: %s\n", g_fail ? "FAILED" : "OK");
    return g_fail ? 1 : 0;
}
