// WatchFeed (neuro_watch.h).
#include "neuro_watch.h"

#include <utility>

#include "debug/debug_session.h"   // DebugSession::kDramSize

namespace vj {
namespace neuro {

void WatchFeed::release(DebugViews& v) {
    if (id_ != DebugViews::kNoId) v.clear(id_);
    id_ = DebugViews::kNoId;
    key_ = 0;
    bytes_.clear();
    base_ = 0;
}

void WatchFeed::update(DebugViews& v, bool want, uint32_t base, unsigned len, double now) {
    if (!want || len == 0) {
        release(v);
        return;
    }
    const bool dram = len > kMaxWatchSpan;
    // A Dram request does not depend on the span, so a profile switch between two wide ranges keeps the request.
    const uint64_t key = dram ? (1ull << 63) : ((uint64_t)base << 32) | len;
    if (key != key_) {   // a fresh id for a new range: the old buffer can never be read as the new one
        release(v);
        key_ = key;
        req_ = ViewRequest();
        req_.kind = dram ? ViewRequest::Kind::Dram : ViewRequest::Kind::Memory;
        req_.addr = dram ? 0 : base;
        req_.len = dram ? 0 : len;
        id_ = v.set(DebugViews::kNoId, req_);
        base_ = dram ? 0 : base;
    }
    // A content switch makes the cached bytes the previous game's: gone at once, not at the next read.
    if (!bytes_.empty() && v.epoch() != epoch_) bytes_.clear();
    if (!bytes_.empty() && now - at_ < kReadInterval) return;
    ViewBuffer vb;
    if (!v.read(id_, vb)) {   // never filled, or filled in an older epoch
        bytes_.clear();
        return;
    }
    at_ = now;   // a copy was made: the next one waits, whether or not the buffer is adopted
    const size_t expect = dram ? (size_t)DebugSession::kDramSize : (size_t)req_.len;
    if (vb.ok && vb.req.kind == req_.kind && vb.req.addr == req_.addr && vb.req.len == req_.len && vb.bytes.size() == expect) {
        bytes_ = std::move(vb.bytes);
        epoch_ = vb.epoch;
    } else {
        bytes_.clear();
    }
}

}  // namespace neuro
}  // namespace vj
