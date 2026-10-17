// SDL3 audio output with dynamic rate control.
//
// The core hands over one fixed batch of stereo pairs per field; the true
// rate is pairs * field rate (48043.6 Hz NTSC), so the SDL stream is opened
// at the rounded core rate and SDL resamples to the device.  A small playback
// ratio correction keeps the queued amount near a target so audio follows the
// video pacing instead of drifting into under/overruns.
//
// Threads (#967): with the emulation thread on, push(), update() and bufferStatus() run on the emulation thread while
// the UI thread pauses, re-targets, sets the volume and reads the counters.  The fields both touch are atomics, and
// every SDL call used here is safe from any thread (SDL3 headers).  open() and close() only run while the other thread
// waits: inside an EmuThread::call(), or before the thread starts and after it stops.
#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>

struct SDL_AudioStream;

namespace vj {

class AudioOut {
public:
    ~AudioOut();
    bool open(double coreRate);          // opens (or re-formats) the device stream
    void close();
    bool isOpen() const { return stream_ != nullptr; }

    void push(const int16_t* stereo, size_t frames);
    // Once per emulated frame, before the core runs: counts underruns and, when `adjustRatio` (the single-thread
    // loop), nudges the playback ratio toward the queue target.  Mode A (the emulation thread) passes false: there the
    // pacer follows the audio clock instead, and two controllers must not fight.
    void update(bool adjustRatio = true);
    void setPaused(bool p);
    bool paused() const { return paused_.load(); }
    void setVolume(float v);             // 0..1
    void setTargetMs(float ms) { targetMs_.store(ms); }
    void clear();
    // Occupancy for RETRO_ENVIRONMENT_SET_AUDIO_BUFFER_STATUS_CALLBACK.
    void bufferStatus(bool& active, unsigned& occupancy, bool& underrun) const;

    float queuedMs() const;
    double ratio() const { return ratio_.load(); }
    uint64_t underruns() const { return underruns_.load(); }

private:
    SDL_AudioStream* stream_ = nullptr;   // changed only by open() / close() (see above)
    int rate_ = 48000;
    std::atomic<float> targetMs_{64.0f};
    std::atomic<double> ratio_{1.0};
    float volume_ = 1.0f;                 // UI thread only
    std::atomic<bool> paused_{false};
    std::atomic<uint64_t> underruns_{0};
};

}  // namespace vj
