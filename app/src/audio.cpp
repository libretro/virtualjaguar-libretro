#include "audio.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace vj {

AudioOut::~AudioOut() { close(); }

bool AudioOut::open(double coreRate) {
    int rate = (int)std::lround(coreRate > 8000.0 ? coreRate : 48000.0);
    SDL_AudioSpec spec;
    SDL_zero(spec);
    spec.format = SDL_AUDIO_S16;
    spec.channels = 2;
    spec.freq = rate;

    if (stream_) {
        if (rate != rate_) {
            if (!SDL_SetAudioStreamFormat(stream_, &spec, nullptr)) {
                std::fprintf(stderr, "audio: cannot re-format stream: %s\n", SDL_GetError());
                return false;
            }
            rate_ = rate;
        }
        clear();
        return true;
    }

    SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, "512");
    stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (!stream_) {
        std::fprintf(stderr, "audio: cannot open device: %s\n", SDL_GetError());
        return false;
    }
    rate_ = rate;
    SDL_SetAudioStreamGain(stream_, volume_);
    if (!paused_) SDL_ResumeAudioStreamDevice(stream_);
    return true;
}

void AudioOut::close() {
    if (stream_) {
        SDL_DestroyAudioStream(stream_);
        stream_ = nullptr;
    }
}

void AudioOut::push(const int16_t* stereo, size_t frames) {
    if (!stream_ || paused_) return;
    SDL_PutAudioStreamData(stream_, stereo, (int)(frames * 2 * sizeof(int16_t)));
}

float AudioOut::queuedMs() const {
    if (!stream_) return 0.0f;
    int bytes = SDL_GetAudioStreamQueued(stream_);
    return 1000.0f * (float)bytes / (float)(rate_ * 4);
}

void AudioOut::update() {
    if (!stream_) return;
    float q = queuedMs();
    if (q <= 0.5f) underruns_++;
    // Relative error against the target, mapped to a +/-0.5% playback-speed
    // nudge.  Smoothed so one burst does not wobble the pitch.
    float err = std::max(-1.0f, std::min(1.0f, (q - targetMs_) / targetMs_));
    double want = 1.0 + 0.005 * (double)err;
    ratio_ += (want - ratio_) * 0.05;
    SDL_SetAudioStreamFrequencyRatio(stream_, (float)ratio_);
}

void AudioOut::setPaused(bool p) {
    paused_ = p;
    if (!stream_) return;
    if (p) SDL_PauseAudioStreamDevice(stream_);
    else SDL_ResumeAudioStreamDevice(stream_);
}

void AudioOut::setVolume(float v) {
    volume_ = std::max(0.0f, std::min(1.0f, v));
    if (stream_) SDL_SetAudioStreamGain(stream_, volume_);
}

void AudioOut::clear() {
    if (stream_) SDL_ClearAudioStream(stream_);
    ratio_ = 1.0;
}

void AudioOut::bufferStatus(bool& active, unsigned& occupancy, bool& underrun) const {
    active = stream_ != nullptr && !paused_;
    float cap = std::max(targetMs_ * 2.0f, 1.0f);
    float q = queuedMs();
    occupancy = (unsigned)std::max(0.0f, std::min(100.0f, 100.0f * q / cap));
    underrun = q < targetMs_ * 0.25f;
}

}  // namespace vj
