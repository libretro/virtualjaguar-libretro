// Audio scope logic (pure): a ring of the most recent stereo frames the core produced, newest-window
// copies for drawing, and peak meter maths.  The app feeds it from the audio callback (CoreHost::onAudio:
// interleaved L,R int16, `frames` pairs); nothing here touches the core.
#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

namespace vj {

class AudioRing {
public:
    explicit AudioRing(size_t frames = 16384) : buf_(frames ? frames * 2 : 2, 0), cap_(frames ? frames : 1) {}

    // push, latest, peak, clear and counters may be called across the audio/UI threads.
    // Interleaved L,R samples, `frames` pairs.  Only the newest `capacity()` frames are kept.
    void push(const int16_t* interleaved, size_t frames);
    size_t capacity() const { return cap_; }
    size_t available() const {   // frames held now (<= capacity)
        std::lock_guard<std::mutex> lock(mu_);
        return filled_;
    }
    uint64_t totalFrames() const {   // frames ever pushed
        std::lock_guard<std::mutex> lock(mu_);
        return total_;
    }
    void clear() {
        std::lock_guard<std::mutex> lock(mu_);
        head_ = filled_ = 0;
        total_ = 0;
    }

    // The newest min(n, available()) frames, oldest first.  Returns how many.
    size_t latest(size_t n, std::vector<int16_t>& left, std::vector<int16_t>& right) const;

    struct Peak {
        int left = 0, right = 0;   // max |sample| over the window, 0..32768
    };
    Peak peak(size_t n) const;     // over the newest n frames

private:
    mutable std::mutex mu_;       // audio producer vs UI reads/clear; capacity is immutable
    std::vector<int16_t> buf_;     // interleaved
    size_t cap_;
    size_t head_ = 0;              // next write frame index
    size_t filled_ = 0;
    uint64_t total_ = 0;
};

// Level (0..1) of a peak, and dBFS (-inf clamps to floorDb).
inline float peakLevel(int peak) { return peak <= 0 ? 0.0f : (peak >= 32768 ? 1.0f : peak / 32768.0f); }
float peakDb(int peak, float floorDb = -96.0f);

// A peak meter with a held maximum and a fall rate, for the bars.
struct PeakMeter {
    float level = 0.0f;      // follows the input up at once, falls at fallPerSecond
    float hold = 0.0f;       // the highest level in the last holdSeconds
    float holdAge = 0.0f;
    bool clipped = false;    // a full-scale sample was seen (cleared by reset())
    void update(int peak, float dt, float fallPerSecond = 1.5f, float holdSeconds = 1.5f);
    void reset() { *this = PeakMeter(); }
};

}  // namespace vj
