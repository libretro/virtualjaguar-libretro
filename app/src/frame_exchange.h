// Finished frames from the emulation thread to the UI thread (#967): a triple buffer.
//
// The writer (emulation thread) fills writeBuffer() and publish()es it; it never waits.  The reader (UI thread)
// takeNewest()s whatever was published last -- older unshown frames are skipped -- and never waits either.  Each side
// owns one of the three slots at any time and the third is the hand-over, so a slot is never written while it is read
// (no tearing).  The reader's slot stays valid, unchanged, until its next takeNewest().
//
// clear() starts a new epoch (a content switch: the load call runs it on the emulation thread).  A frame published
// before it, or begun before it and published after, is never handed out, so the previous game's last picture cannot
// reappear.  Serials come from CoreHost and stay monotonic across loads, which VideoOut::upload relies on.
#pragma once

#include "frame.h"

#include <atomic>
#include <cstdint>

namespace vj {

class FrameExchange {
public:
    struct Slot {
        Frame frame;
        float aspect = 4.0f / 3.0f;   // CoreHost::aspect() when the frame was made
        uint64_t epoch = 0;           // stamped by writeBuffer()
    };

    // ---- writer (the emulation thread) ----
    Slot& writeBuffer() {
        Slot& s = slots_[write_];
        s.epoch = epoch_.load(std::memory_order_acquire);
        return s;
    }
    void publish() {
        const unsigned prev = ready_.exchange(write_ | kFresh, std::memory_order_acq_rel);
        write_ = prev & kIndex;
    }

    // ---- reader (the UI thread) ----
    // The newest frame published since the last call; null when nothing new (or only a stale frame) arrived.
    const Slot* takeNewest() {
        if (!(ready_.load(std::memory_order_acquire) & kFresh)) return nullptr;
        const unsigned prev = ready_.exchange(read_, std::memory_order_acq_rel);
        read_ = prev & kIndex;
        hasRead_ = true;
        return fresh(slots_[read_]) ? &slots_[read_] : nullptr;
    }
    // The frame the last takeNewest() returned (to draw again), or null.
    const Slot* current() const { return hasRead_ && fresh(slots_[read_]) ? &slots_[read_] : nullptr; }

    // ---- either thread ----
    void clear() { epoch_.fetch_add(1, std::memory_order_acq_rel); }
    uint64_t epoch() const { return epoch_.load(std::memory_order_acquire); }

private:
    static constexpr unsigned kIndex = 3, kFresh = 4;
    bool fresh(const Slot& s) const { return s.epoch == epoch_.load(std::memory_order_acquire); }

    Slot slots_[3];
    std::atomic<unsigned> ready_{1};    // the hand-over slot's index, plus kFresh until the reader takes it
    unsigned write_ = 0;                // writer-owned
    unsigned read_ = 2;                 // reader-owned
    bool hasRead_ = false;              // reader-owned
    std::atomic<uint64_t> epoch_{0};
};

}  // namespace vj
