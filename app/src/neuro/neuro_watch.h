// The profile's RAM watches as one DebugViews request (#967 phase 3c).  Neuro never touches core RAM: the UI thread
// states the covering range here, the emulation thread's copier fills the buffer after each frame, and the UI reads
// it back 4 times a second.  No App, no ImGui, no core: the unit test drives it with a fake core.
//
// UI thread only (DebugViews has its own mutex; the feed's state is not shared).
#pragma once

#include <cstdint>
#include <vector>

#include "debug/debug_views.h"

namespace vj {
namespace neuro {

// A watch span wider than this is served by a Kind::Dram request (the whole 2 MiB) instead of a clamped Memory one.
constexpr unsigned kMaxWatchSpan = 65536;
static_assert(kMaxWatchSpan == DebugViews::kMaxMemoryBytes, "a Memory request is clamped to kMaxMemoryBytes");

class WatchFeed {
public:
    static constexpr double kReadInterval = 0.25;   // seconds: a 2 MiB Dram copy is not made every UI frame

    // Keeps one request alive for [base, base + len) (Memory, or Dram when len > kMaxWatchSpan) and refreshes bytes().
    // want = false (Neuro off / not allowed / no content / no watches) releases the request.  A changed (kind, base,
    // len) releases the old id and sets a fresh one, so the old range's buffer can never be read as the new one.  The
    // buffer is read at most every kReadInterval of `now` (at once while bytes() is empty) and adopted only when it
    // is ok, current (read() fails across a content switch and before the first fill) and answers exactly this
    // request; anything else leaves bytes() empty, which the state text shows as "?".
    void update(DebugViews& v, bool want, uint32_t base, unsigned len, double now);
    void release(DebugViews& v);   // neuroShutdown; also what want = false does

    // What the state text reads: bytes() starts at address base() (0 for a Dram request).  Mutable so the caller can
    // lend the vector to GameState without a copy.
    std::vector<uint8_t>& bytes() { return bytes_; }
    const std::vector<uint8_t>& bytes() const { return bytes_; }
    uint32_t base() const { return base_; }
    DebugViews::Id id() const { return id_; }   // kNoId while released (tests)

private:
    DebugViews::Id id_ = DebugViews::kNoId;
    uint64_t key_ = 0;            // 0 = nothing requested; else (dram, base, len) packed
    ViewRequest req_;             // what was asked of id_
    std::vector<uint8_t> bytes_;
    uint32_t base_ = 0;
    uint64_t epoch_ = 0;          // the epoch of the buffer bytes_ came from
    double at_ = -1e9;            // when the buffer was last read
};

}  // namespace neuro
}  // namespace vj
