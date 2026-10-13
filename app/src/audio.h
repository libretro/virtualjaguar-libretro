// SDL3 audio output with dynamic rate control.
//
// The core hands over one fixed batch of stereo pairs per field; the true
// rate is pairs * field rate (48043.6 Hz NTSC), so the SDL stream is opened
// at the rounded core rate and SDL resamples to the device.  A small playback
// ratio correction keeps the queued amount near a target so audio follows the
// video pacing instead of drifting into under/overruns.
#pragma once
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
    // Once per emulated frame, before the core runs: adjusts the playback ratio.
    void update();
    void setPaused(bool p);
    void setVolume(float v);             // 0..1
    void setTargetMs(float ms) { targetMs_ = ms; }
    void clear();
    // Occupancy for RETRO_ENVIRONMENT_SET_AUDIO_BUFFER_STATUS_CALLBACK.
    void bufferStatus(bool& active, unsigned& occupancy, bool& underrun) const;

    float queuedMs() const;
    double ratio() const { return ratio_; }
    uint64_t underruns() const { return underruns_; }

private:
    SDL_AudioStream* stream_ = nullptr;
    int rate_ = 48000;
    float targetMs_ = 64.0f;
    double ratio_ = 1.0;
    float volume_ = 1.0f;
    bool paused_ = false;
    uint64_t underruns_ = 0;
};

}  // namespace vj
