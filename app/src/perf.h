// Dev-only frame-cost profiler (--perf-frames N).  Phase timers around the
// interactive loop; nothing is measured, allocated or branched on unless the
// flag is given (Perf::on is false and every call is one predictable branch).
#pragma once
#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#include <sys/resource.h>
#include <sys/time.h>
#define VJ_PERF_RUSAGE 1
#endif

namespace vj {

struct Perf {
    enum Phase { Events, Step, Upload, Ui, Present, Sleep, Retro, Shader, GpuTotal, kPhases };
    static const char* name(int p) {
        static const char* n[] = {"events + input", "emulation step", "gl texture upload", "ui build + draw submit",
                                  "swap", "sleep / pacing", "  of step: retro_run only", "  of ui: shader chain",
                                  "  gpu total (glFinish)"};
        return n[p];
    }

    bool on = false;
    bool gpu = false;            // glFinish after the shader chain and after the UI: true GPU cost
    unsigned warmup = 120;       // frames discarded
    unsigned forceW = 0, forceH = 0;   // run the shader chain at this output size
    unsigned target = 0;         // frames measured, then quit
    unsigned seen = 0;
    uint64_t freq = 1;
    uint64_t t0 = 0;             // start of the measured window
    uint64_t mark = 0;
    uint64_t frameStart = 0;
    double sum[kPhases] = {};
    double mx[kPhases] = {};
    std::vector<double> frameMs;
    std::vector<unsigned char> emuPer;   // emulated frames run in each measured loop iteration (VJ_PERF_CSV)
    std::vector<unsigned char> syncPer;  // the swap interval was 1 during that iteration
    unsigned emuThis = 0;
    bool curSync = true;                 // set by the loop; recorded per iteration
    double cpuStart = 0;
    uint64_t emuFrames = 0;      // emulated frames run inside the measured window (retro_run calls)
    std::string note;            // one line describing the setup (window size, GL, shader), printed first

    static double cpuSeconds() {
#ifdef VJ_PERF_RUSAGE
        struct rusage r;
        getrusage(RUSAGE_SELF, &r);
        return (double)r.ru_utime.tv_sec + r.ru_utime.tv_usec * 1e-6 + (double)r.ru_stime.tv_sec +
               r.ru_stime.tv_usec * 1e-6;
#else
        return -1.0;
#endif
    }

    void init(unsigned frames, bool gpuFinish) {
        on = frames > 0;
        target = frames;
        gpu = gpuFinish;
        freq = SDL_GetPerformanceFrequency();
        frameMs.reserve(frames);
    }
    static void finish() {
        typedef void (*Fn)();
        static Fn fn = (Fn)SDL_GL_GetProcAddress("glFinish");
        if (fn) fn();
    }
    uint64_t tick() const { return SDL_GetPerformanceCounter(); }
    // Call at the top of each loop iteration.  Returns true once the window is complete.
    bool frameBegin() {
        if (!on) return false;
        uint64_t n = tick();
        if (seen == warmup) {
            t0 = n;
            cpuStart = cpuSeconds();
        } else if (seen > warmup) {
            frameMs.push_back(1000.0 * (double)(n - frameStart) / (double)freq);
            emuPer.push_back((unsigned char)std::min(emuThis, 255u));
            syncPer.push_back(curSync ? 1 : 0);
        }
        emuThis = 0;
        frameStart = n;
        mark = n;
        return seen >= warmup + target;
    }
    // Attribute the time since the last mark to phase p.
    void lap(Phase p) {
        if (!on) return;
        uint64_t n = tick();
        add(p, 1000.0 * (double)(n - mark) / (double)freq);
        mark = n;
    }
    // Add an explicitly timed span (does not move the mark).
    void add(Phase p, double ms) {
        if (!on || seen < warmup) return;
        sum[p] += ms;
        mx[p] = std::max(mx[p], ms);
    }
    // One emulated frame (a retro_run call) ran.  A UI frame may run several (catching up) or none.
    void emulated() {
        if (on && seen >= warmup) emuFrames++;
        emuThis++;
    }
    void frameEnd() {
        if (on) seen++;
    }

    void report(const char* what) const {
        if (!on || frameMs.empty()) return;
        double wall = (double)(tick() - t0) / (double)freq;
        double cpu = cpuSeconds() - cpuStart;
        size_t n = frameMs.size();
        std::vector<double> s = frameMs;
        std::sort(s.begin(), s.end());
        double tot = 0;
        for (double v : s) tot += v;
        if (!note.empty()) std::printf("PERF %s\n", note.c_str());
        std::printf("PERF %s: %zu frames in %.2f s (loop %.1f fps)\n", what, n, wall, (double)n / wall);
        std::printf("PERF   emulated %llu frames in the window = %.1f emulated fps (real time is the core's own rate, ~60)\n",
                    (unsigned long long)emuFrames, (double)emuFrames / wall);
        for (int p = 0; p < kPhases; p++) {
            if (sum[p] == 0 && p >= Retro) continue;
            std::printf("PERF   %-28s avg %7.3f ms  max %7.3f ms\n", name(p), sum[p] / (double)n, mx[p]);
        }
        std::printf("PERF   %-28s avg %7.3f ms  p50 %6.3f  p99 %6.3f  max %6.3f\n", "loop iteration", tot / (double)n,
                    s[n / 2], s[std::min(n - 1, (size_t)((double)n * 0.99))], s.back());
        if (cpu >= 0) std::printf("PERF   process CPU: %.1f%% of one core (user+sys %.2f s / wall %.2f s)\n", 100.0 * cpu / wall, cpu, wall);
        // VJ_PERF_CSV=path: one row per measured loop iteration (iteration ms, emulated frames run in it, swap
        // interval 1?) -- the raw material for "how evenly were new images presented" (tests/pacing_trace.cpp
        // replays the same thing from a core cost trace).
        if (const char* csv = std::getenv("VJ_PERF_CSV")) {
            if (FILE* f = std::fopen(csv, "w")) {
                std::fprintf(f, "iter_ms,emu_frames,swap_sync\n");
                for (size_t i = 0; i < n && i < emuPer.size(); i++)
                    std::fprintf(f, "%.3f,%d,%d\n", frameMs[i], (int)emuPer[i], (int)syncPer[i]);
                std::fclose(f);
            }
        }
        std::fflush(stdout);
    }
};

}  // namespace vj
