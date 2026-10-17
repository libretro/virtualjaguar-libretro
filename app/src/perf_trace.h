// Dev-only presentation trace (#967 evenness): VJ_PERF_TRACE=<prefix> with --perf-frames N writes two CSVs when the
// perf window ends, on one clock (SDL_GetTicksNS):
//
//   <prefix>-emu.csv  one row per emulated frame: when it was due on the pacing grid, when it really started, when its
//                     picture was handed over, the trim and audio queue it was paced with, and its frame serial;
//   <prefix>-ui.csv   one row per UI iteration: when the iteration took a frame, which serial it showed, and the swap
//                     around it (its return is the vblank proxy), plus the late latch's wait.
//
// Joining the two answers "where in the refresh period did each frame finish, and where did the UI take it", which the
// per-iteration VJ_PERF_CSV cannot: it counts frames run between takes, not frames shown.  Nothing is recorded unless
// the variable is set.  The emulation thread is the only writer of the emu rows (a fixed array, published with an
// atomic count), the UI thread the only writer of the UI rows and the only reader of both.
#pragma once

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace vj {

struct PerfTrace {
    struct Emu {
        uint64_t dueNs, startNs, endNs, serial;
        float trim, filtQMs, queuedMs;
        unsigned char audio, refill;
    };
    struct Ui {
        uint64_t takeNs, serial, swap0Ns, swap1Ns, wakeNs, waitEndNs, budgetNs, targetNs;
        unsigned char locked;
    };

    std::string prefix;
    bool enabled() const { return on_.load(std::memory_order_acquire); }   // (the emulation thread may already run)

    void init(unsigned frames) {
        const char* p = std::getenv("VJ_PERF_TRACE");
        if (!p || !*p || !frames) return;
        prefix = p;
        emu_.resize((size_t)(frames + 240) * 4);
        ui_.reserve(frames + 240);
        on_.store(true, std::memory_order_release);
    }

    // ---- emulation thread (or the UI thread inline) ----
    void emu(const Emu& e) {
        const size_t i = emuN_.load(std::memory_order_relaxed);
        if (!enabled() || i >= emu_.size()) return;
        emu_[i] = e;
        emuN_.store(i + 1, std::memory_order_release);
    }

    // ---- UI thread ----
    void ui(const Ui& u) {
        if (enabled() && ui_.size() < ui_.capacity()) ui_.push_back(u);
    }
    void write() const {
        if (!enabled()) return;
        const size_t n = emuN_.load(std::memory_order_acquire);
        if (FILE* f = std::fopen((prefix + "-emu.csv").c_str(), "w")) {
            std::fprintf(f, "due_ns,start_ns,end_ns,serial,trim,filt_q_ms,queued_ms,audio,refill\n");
            for (size_t i = 0; i < n; i++) {
                const Emu& e = emu_[i];
                std::fprintf(f, "%llu,%llu,%llu,%llu,%.6f,%.3f,%.3f,%d,%d\n", (unsigned long long)e.dueNs,
                             (unsigned long long)e.startNs, (unsigned long long)e.endNs, (unsigned long long)e.serial,
                             (double)e.trim, (double)e.filtQMs, (double)e.queuedMs, (int)e.audio, (int)e.refill);
            }
            std::fclose(f);
        }
        if (FILE* f = std::fopen((prefix + "-ui.csv").c_str(), "w")) {
            std::fprintf(f, "take_ns,serial,swap0_ns,swap1_ns,wake_ns,wait_end_ns,budget_ns,target_ns,locked\n");
            for (const Ui& u : ui_)
                std::fprintf(f, "%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%d\n", (unsigned long long)u.takeNs,
                             (unsigned long long)u.serial, (unsigned long long)u.swap0Ns, (unsigned long long)u.swap1Ns,
                             (unsigned long long)u.wakeNs, (unsigned long long)u.waitEndNs,
                             (unsigned long long)u.budgetNs, (unsigned long long)u.targetNs, (int)u.locked);
            std::fclose(f);
        }
    }

private:
    std::atomic<bool> on_{false};
    std::vector<Emu> emu_;
    std::atomic<size_t> emuN_{0};
    std::vector<Ui> ui_;
};

}  // namespace vj
