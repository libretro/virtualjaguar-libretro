// AudioOut (audio.h) under SDL's dummy audio driver (#967): the mode-A hooks, and the fields the emulation thread and
// the UI thread both touch (the ThreadSanitizer job runs this).
//
//   vj_audio_out_test
#include "audio.h"

#include <SDL3/SDL.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <thread>
#include <vector>

namespace {

int g_fail = 0;
#define CHECK(c, ...)                                        \
    do {                                                     \
        if (!(c)) {                                          \
            std::printf("FAIL %s:%d: ", __FILE__, __LINE__); \
            std::printf(__VA_ARGS__);                        \
            std::printf("\n");                               \
            g_fail++;                                        \
        }                                                    \
    } while (0)

}  // namespace

int main() {
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    if (!SDL_Init(SDL_INIT_AUDIO)) {
        std::printf("SKIP no SDL audio: %s\n", SDL_GetError());
        return 77;
    }
    {
        vj::AudioOut a;
        CHECK(a.open(48043.6), "the dummy device opens");
        std::vector<int16_t> batch(800 * 2, 0);
        a.push(batch.data(), 800);
        CHECK(a.queuedMs() > 5.0f, "a frame's audio is queued (%.2f ms)", a.queuedMs());
        a.update(false);
        CHECK(a.ratio() == 1.0, "update(false): mode A leaves the playback ratio alone (%.6f)", a.ratio());
        a.update(true);
        CHECK(a.ratio() != 1.0, "update(true): the single-thread loop's controller still nudges it (%.6f)", a.ratio());
        a.clear();
        CHECK(a.ratio() == 1.0, "clear() resets the ratio");

        // Mode B (#1038): the stream plays at the base ratio, update(true) trims within +/-0.5% of it, update(false)
        // leaves it alone, and leaving mode B puts the stream itself (not only the cached value) back at 1.
        const double base = 60.0 / 60.05445;
        a.setBaseRatio(base);
        CHECK(a.ratio() == base && a.baseRatio() == base, "setBaseRatio applies the base (%.6f)", a.ratio());
        CHECK(SDL_GetAudioStreamFrequencyRatio(a.sdlStream()) == (float)base, "...to the SDL stream itself (%.6f)",
              (double)SDL_GetAudioStreamFrequencyRatio(a.sdlStream()));
        for (int i = 0; i < 400; i++) {
            if (a.queuedMs() < 5.0f) a.push(batch.data(), 800);
            a.update(true);
            const double r = a.ratio();
            CHECK(r >= base * 0.995 - 1e-12 && r <= base * 1.005 + 1e-12, "update(true) stays within 0.5%% of the base (%.6f)", r);
            if (r < base * 0.995 - 1e-12 || r > base * 1.005 + 1e-12) break;
        }
        CHECK(SDL_GetAudioStreamFrequencyRatio(a.sdlStream()) == (float)a.ratio(), "the stream follows the trimmed ratio");
        a.update(false);
        const double held = a.ratio();
        a.update(false);
        CHECK(a.ratio() == held, "update(false) leaves the ratio alone in mode B too");
        a.clear();
        CHECK(a.ratio() == base && SDL_GetAudioStreamFrequencyRatio(a.sdlStream()) == (float)base,
              "clear() goes back to the base, not to 1 (%.6f)", a.ratio());
        a.setBaseRatio(1.0);
        CHECK(a.ratio() == 1.0 && SDL_GetAudioStreamFrequencyRatio(a.sdlStream()) == 1.0f,
              "leaving mode B restores 1.0 on the stream (%.6f)", (double)SDL_GetAudioStreamFrequencyRatio(a.sdlStream()));
        a.update(false);
        CHECK(SDL_GetAudioStreamFrequencyRatio(a.sdlStream()) == 1.0f, "and mode A's update(false) keeps it there");

        // The controller's step is pure: base 1 is the old single-thread rule, any base scales it.
        CHECK(vj::AudioOut::nextRatio(1.0, 1.0, 64.0f, 64.0f) == 1.0, "on target: no nudge");
        double r = 1.0;
        for (int i = 0; i < 1000; i++) r = vj::AudioOut::nextRatio(r, base, 1000.0f, 64.0f);
        CHECK(std::fabs(r - base * 1.005) < 1e-9, "overfull: settles at base * 1.005 (%.6f)", r);
        for (int i = 0; i < 1000; i++) r = vj::AudioOut::nextRatio(r, base, 0.0f, 64.0f);
        CHECK(std::fabs(r - base * 0.995) < 1e-9, "empty: settles at base * 0.995 (%.6f)", r);

        a.setPaused(true);
        CHECK(a.paused(), "paused() reports the pause");
        bool active = true, underrun = false;
        unsigned occ = 0;
        a.bufferStatus(active, occ, underrun);
        CHECK(!active, "a paused stream is not an active audio clock");
        a.setPaused(false);

        // The emulation thread pushes and measures while the UI thread pauses, re-targets and reads the counters.
        std::atomic<bool> stop{false};
        std::thread emu([&] {
            while (!stop.load()) {
                a.push(batch.data(), 800);
                a.update(false);
                bool ac = false, ur = false;
                unsigned oc = 0;
                a.bufferStatus(ac, oc, ur);
                if (a.queuedMs() > 200.0f) a.clear();
            }
        });
        for (int i = 0; i < 2000; i++) {
            a.setPaused((i & 1) != 0);
            a.setTargetMs(32.0f + (float)(i % 64));
            (void)a.ratio();
            (void)a.underruns();
        }
        stop = true;
        emu.join();
        a.setPaused(false);
        CHECK(a.isOpen(), "still open after the cross-thread run");
        a.close();
    }
    SDL_Quit();
    std::printf(g_fail ? "audio_out_test: %d FAILED\n" : "audio_out_test: OK\n", g_fail);
    return g_fail ? 1 : 0;
}
