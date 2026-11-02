// Everything the Blitter log window reads: the launch list and the live registers, both as published view
// buffers (DebugViews), never the core's interface.  The core's launch log is switched on and off, and restarted,
// through the session's marshalled calls (one wait each).  No ImGui, so it is tested against the mock core
// (app/tests/debug_blitter_feed_test.cpp); blitter_ui.cpp is the thin ImGui layer on top.
#pragma once

#include <cstdint>
#include <vector>

#include "blitter_log.h"
#include "debug_session.h"
#include "debug_views.h"

namespace vj {
namespace blit {

class LogFeed {
public:
    LogTracker tracker;

    // Switches the core's log on or off to match `want`: one callCore on a change, none otherwise.  A new core (the
    // views' epoch moved) forgets "on" without calling: its log starts off, and turning it on again is a fresh start.
    void setLog(DebugSession& s, bool want);
    // Once per draw while the window is shown: states the BlitLog{since = tracker.total()} and Memory{kRegBase,
    // kRegBytes} requests, wakes the copier, and takes the published answers.  `frozen` keeps requesting (the
    // numbering stays in step) but absorbs nothing, so unfreezing catches up with every launch still in the ring.
    void update(DebugSession& s, bool frozen);
    // Empties the list and restarts the core's numbering (log off, then on) in ONE wait, so the list and the core stay
    // in step.  The old request is retired (see gen_) so a buffer filled before the restart is never absorbed.
    void clear(DebugSession& s);
    // The window is closed, collapsed or unusable: retires both requests (the clear is posted behind any queued job)
    // and forgets what was adopted.  The tracker's rows stay; setLog(true) resets them.
    void release(DebugSession& s);

    bool logOn() const { return logOn_; }
    bool haveRegs() const { return haveRegs_; }
    const std::vector<uint8_t>& regImage() const { return regImage_; }

private:
    void syncEpoch(DebugSession& s);
    void request(DebugSession& s);

    DebugViews::Id logId_ = DebugViews::kNoId, regId_ = DebugViews::kNoId;
    bool logOn_ = false;
    bool epochSet_ = false;
    uint64_t epochSeen_ = 0;
    // BlitLog ignores `addr`, so the feed uses it as a restart generation: a turned-on or cleared log changes the
    // request even when `since` is 0 again, which empties the front buffer a pre-restart fill left behind.
    unsigned gen_ = 0;
    bool haveLog_ = false;
    uint64_t lastLogSerial_ = 0;
    unsigned lastSince_ = 0;
    uint64_t lastRegSerial_ = 0;
    bool haveRegs_ = false;
    std::vector<uint8_t> regImage_;
};

}  // namespace blit
}  // namespace vj
