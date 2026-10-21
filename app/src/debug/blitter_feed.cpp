// The Blitter log window's data (blitter_feed.h).
#include "blitter_feed.h"

namespace vj {
namespace blit {

// A new core: nothing adopted before belongs to it.  The old ids stay (the views dropped their buffers with the epoch).
void LogFeed::syncEpoch(DebugSession& s) {
    const uint64_t ep = s.views().epoch();
    if (!epochSet_) {
        epochSet_ = true;
        epochSeen_ = ep;
        return;
    }
    if (ep == epochSeen_) return;
    epochSeen_ = ep;
    logOn_ = false;   // that core's log is gone; the new one starts off
    tracker.reset();
    haveLog_ = false;
    lastLogSerial_ = lastSince_ = lastRegSerial_ = 0;
    haveRegs_ = false;
    regImage_.clear();
    gen_++;
}

void LogFeed::setLog(DebugSession& s, bool want) {
    syncEpoch(s);
    if (want && !logOn_) {
        s.blitLogEnable(true);
        logOn_ = true;
        tracker.reset();
        haveLog_ = false;
        lastLogSerial_ = lastSince_ = 0;
        gen_++;   // fresh numbering from 0: retire any request (and its buffer) of the previous run
    } else if (!want && logOn_) {
        s.blitLogEnable(false);
        logOn_ = false;
    }
}

void LogFeed::request(DebugSession& s) {
    ViewRequest l;
    l.kind = ViewRequest::Kind::BlitLog;
    l.since = tracker.total();
    l.addr = gen_;
    logId_ = s.views().set(logId_, l);
    ViewRequest r;
    r.kind = ViewRequest::Kind::Memory;
    r.addr = kRegBase;
    r.len = kRegBytes;
    regId_ = s.views().set(regId_, r);
}

void LogFeed::update(DebugSession& s, bool frozen) {
    syncEpoch(s);
    request(s);
    s.wakeViews();

    ViewBuffer b;
    // Only the answer to the request we are on: the tracker also checks `since`, this checks the restart generation.
    if (!frozen && s.views().read(logId_, b) && b.req.kind == ViewRequest::Kind::BlitLog && b.req.addr == gen_ &&
        !(haveLog_ && b.serial == lastLogSerial_ && b.req.since == lastSince_)) {
        haveLog_ = true;
        lastLogSerial_ = b.serial;
        lastSince_ = b.req.since;
        tracker.absorb(b);
    }

    // The registers: adopted only for the request, the size and a serial not seen yet.
    if (s.views().read(regId_, b) && b.req.kind == ViewRequest::Kind::Memory && b.req.addr == kRegBase &&
        b.req.len == kRegBytes && b.serial != lastRegSerial_) {
        lastRegSerial_ = b.serial;
        haveRegs_ = b.ok && b.bytes.size() == kRegBytes;
        if (haveRegs_) regImage_ = b.bytes;
    }
}

void LogFeed::clear(DebugSession& s) {
    syncEpoch(s);
    tracker.reset();
    haveLog_ = false;
    lastLogSerial_ = lastSince_ = 0;
    gen_++;
    if (logOn_) {
        // One wait for both calls (a call inside a call is the emulation side's own work).
        DebugSession* sp = &s;
        s.callCore([sp] {
            sp->blitLogEnable(false);
            sp->blitLogEnable(true);
        });
    }
    // Retire the old request now: a fill queued before the restart has finished (the wait was FIFO behind it), and
    // the next fill reads the new request.
    if (logId_ != DebugViews::kNoId) request(s);
}

void LogFeed::release(DebugSession& s) {
    if (logId_ != DebugViews::kNoId || regId_ != DebugViews::kNoId) {
        // Posted, not cleared here: the lane is FIFO, so this lands behind a fill still queued (inline it runs at once).
        DebugViews* v = &s.views();
        const DebugViews::Id a = logId_, c = regId_;
        s.postCore([v, a, c] {
            if (a != DebugViews::kNoId) v->clear(a);
            if (c != DebugViews::kNoId) v->clear(c);
        });
    }
    logId_ = regId_ = DebugViews::kNoId;
    haveLog_ = false;
    lastLogSerial_ = lastSince_ = lastRegSerial_ = 0;
    haveRegs_ = false;
    regImage_.clear();
}

}  // namespace blit
}  // namespace vj
