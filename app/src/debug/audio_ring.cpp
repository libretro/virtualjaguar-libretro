#include "audio_ring.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace vj {

void AudioRing::push(const int16_t* in, size_t frames) {
    if (!in || frames == 0) return;
    total_ += frames;
    if (frames >= cap_) {   // only the tail fits
        in += (frames - cap_) * 2;
        frames = cap_;
        head_ = 0;
    }
    for (size_t i = 0; i < frames; i++) {
        buf_[head_ * 2] = in[i * 2];
        buf_[head_ * 2 + 1] = in[i * 2 + 1];
        head_ = (head_ + 1) % cap_;
    }
    filled_ = std::min(cap_, filled_ + frames);
    if (frames == cap_) filled_ = cap_;
}

size_t AudioRing::latest(size_t n, std::vector<int16_t>& l, std::vector<int16_t>& r) const {
    n = std::min(n, filled_);
    l.resize(n);
    r.resize(n);
    size_t start = (head_ + cap_ - n) % cap_;
    for (size_t i = 0; i < n; i++) {
        size_t k = (start + i) % cap_;
        l[i] = buf_[k * 2];
        r[i] = buf_[k * 2 + 1];
    }
    return n;
}

AudioRing::Peak AudioRing::peak(size_t n) const {
    Peak p;
    n = std::min(n, filled_);
    size_t start = (head_ + cap_ - n) % cap_;
    for (size_t i = 0; i < n; i++) {
        size_t k = (start + i) % cap_;
        p.left = std::max(p.left, std::abs((int)buf_[k * 2]));
        p.right = std::max(p.right, std::abs((int)buf_[k * 2 + 1]));
    }
    return p;
}

float peakDb(int peak, float floorDb) {
    if (peak <= 0) return floorDb;
    float db = 20.0f * std::log10(std::min(peak, 32768) / 32768.0f);
    return std::max(db, floorDb);
}

void PeakMeter::update(int peak, float dt, float fall, float holdSeconds) {
    float in = peakLevel(peak);
    if (peak >= 32767) clipped = true;
    if (in >= level) level = in;
    else level = std::max(in, level - fall * dt);
    holdAge += dt;
    if (in >= hold || holdAge > holdSeconds) {
        hold = in >= hold ? in : std::max(in, level);
        holdAge = 0.0f;
    }
}

}  // namespace vj
