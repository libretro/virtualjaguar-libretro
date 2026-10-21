// AudioOut (audio.h) under SDL's dummy audio driver (#967): the mode-A hooks, and the fields the emulation thread and
// the UI thread both touch (the ThreadSanitizer job runs this).
//
//   vj_audio_out_test
#include "audio.h"

#include <SDL3/SDL.h>

#include <atomic>
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
