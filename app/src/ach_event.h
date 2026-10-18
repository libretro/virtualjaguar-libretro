// ach_event.h -- an rc_client event, copied (#967 phase 2).  The handler runs on whichever thread raised the event
// (the emulation thread inside rc_client_do_frame; the UI thread for completions pumped by AchHttp); it queues one of
// these and Achievements::pump() applies it on the UI thread.  Every string is owned.
#pragma once

#include "rc_client.h"

#include <cstdint>
#include <string>

namespace vj {

struct AchEvent {
    uint32_t type = 0;                 // RC_CLIENT_EVENT_*
    uint32_t id = 0;                   // achievement / leaderboard / tracker id
    uint32_t points = 0;
    bool hardcore = false;             // rc_client's hardcore flag when the event was raised
    std::string title, description, badgeUrl;
    std::string measured;              // achievement: measured_progress
    std::string display;               // leaderboard tracker
    std::string api, error;            // server error
    std::string submittedScore;        // scoreboard
    uint32_t newRank = 0, numEntries = 0;
    std::string subsetTitle;
};

AchEvent achEventFrom(const rc_client_event_t& e, bool hardcore);

}  // namespace vj
