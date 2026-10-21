// Dev-only stall probe (#1026): see stall_probe.h.
#include "stall_probe.h"

#ifdef __APPLE__

#include <CoreFoundation/CoreFoundation.h>
#include <SDL3/SDL.h>
#include <dlfcn.h>
#include <mach/mach.h>
#include <pthread.h>
#include <sys/qos.h>
#include <time.h>

#include <atomic>
#include <chrono>
#include <climits>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

namespace vj {

namespace {

constexpr int kDepth = 40;
constexpr size_t kRing = 512;            // run-loop transitions kept (the UI thread's own ring)
constexpr size_t kMaxStalls = 400;
constexpr size_t kMaxSamples = 48;       // per stall, one every kSampleUs
constexpr int kSampleUs = 500;
constexpr uintptr_t kAddrMask = 0x00007fffffffffffull;   // strips pointer-authentication bits from return addresses

struct Sample {
    uint64_t ns;
    int runState, cpuUsage, suspend;
    unsigned depth;
    uintptr_t pc[kDepth];   // [0] pc, [1] lr (arm64; 0 elsewhere), then the frame-pointer chain
};
struct Loop {
    uint64_t ns;
    uint32_t act;
    uint32_t last;   // 0 = the first observer to run for this transition, 1 = the last
};
struct Stall {
    size_t row;
    unsigned char kind;
    uint64_t enterNs, dueNs, leaveNs, cpuNs;
    std::vector<Loop> loop;
    std::vector<Sample> samples;
};

const char* actName(uint32_t a) {
    switch (a) {
    case kCFRunLoopEntry: return "entry";
    case kCFRunLoopBeforeTimers: return "before-timers";
    case kCFRunLoopBeforeSources: return "before-sources";
    case kCFRunLoopBeforeWaiting: return "before-waiting";
    case kCFRunLoopAfterWaiting: return "after-waiting";
    case kCFRunLoopExit: return "exit";
    default: return "?";
    }
}
const char* stateName(int s) {
    switch (s) {
    case TH_STATE_RUNNING: return "running";
    case TH_STATE_STOPPED: return "stopped";
    case TH_STATE_WAITING: return "waiting";
    case TH_STATE_UNINTERRUPTIBLE: return "uninterruptible";
    case TH_STATE_HALTED: return "halted";
    default: return "?";
    }
}
uint64_t threadCpuNs() { return clock_gettime_nsec_np(CLOCK_THREAD_CPUTIME_ID); }

}  // namespace

struct StallProbe::Impl {
    std::string prefix;
    thread_act_t main = MACH_PORT_NULL;
    uintptr_t stackLo = 0, stackHi = 0;
    std::thread watcher;
    std::mutex m;
    std::condition_variable cv;
    bool quit = false;
    std::atomic<uint64_t> gen{0};   // odd while the UI thread is inside a bracketed call
    uint64_t due = 0;               // (under m) the current call's due time
    std::vector<Sample> cur;        // (under m) the watcher's samples of the current call
    // UI thread only
    uint64_t enterNs = 0, cpu0 = 0, cpuAcc = 0, calls = 0;
    unsigned char kind = 0;
    size_t row = 0, ringAtEnter = 0;
    Loop ring[kRing];
    size_t ringN = 0;
    std::vector<Stall> stalls;
    CFRunLoopObserverRef first = nullptr, last = nullptr;

    static void observe(CFRunLoopObserverRef, CFRunLoopActivity act, void* info) {
        auto* c = static_cast<std::pair<Impl*, uint32_t>*>(info);
        Impl* p = c->first;
        p->ring[p->ringN % kRing] = Loop{SDL_GetTicksNS(), (uint32_t)act, c->second};
        p->ringN++;
    }
    std::pair<Impl*, uint32_t> tagFirst{this, 0}, tagLast{this, 1};

    // Watcher thread: the UI thread's scheduler state and stack, right now.  Between suspend and resume nothing may
    // allocate or lock (the UI thread may hold the malloc lock).
    void sample(Sample& s) {
        s.ns = SDL_GetTicksNS();
        s.depth = 0;
        thread_basic_info_data_t bi{};
        mach_msg_type_number_t n = THREAD_BASIC_INFO_COUNT;
        if (thread_info(main, THREAD_BASIC_INFO, (thread_info_t)&bi, &n) == KERN_SUCCESS) {
            s.runState = bi.run_state;
            s.cpuUsage = bi.cpu_usage;
            s.suspend = bi.suspend_count;
        } else {
            s.runState = -1;
            s.cpuUsage = s.suspend = 0;
        }
        if (thread_suspend(main) != KERN_SUCCESS) return;
        uintptr_t pc = 0, lr = 0, fp = 0;
#if defined(__arm64__)
        arm_thread_state64_t st{};
        mach_msg_type_number_t c = ARM_THREAD_STATE64_COUNT;
        if (thread_get_state(main, ARM_THREAD_STATE64, (thread_state_t)&st, &c) == KERN_SUCCESS) {
            pc = (uintptr_t)arm_thread_state64_get_pc(st);
            lr = (uintptr_t)arm_thread_state64_get_lr(st) & kAddrMask;
            fp = (uintptr_t)arm_thread_state64_get_fp(st);
        }
#elif defined(__x86_64__)
        x86_thread_state64_t st{};
        mach_msg_type_number_t c = x86_THREAD_STATE64_COUNT;
        if (thread_get_state(main, x86_THREAD_STATE64, (thread_state_t)&st, &c) == KERN_SUCCESS) {
            pc = (uintptr_t)st.__rip;
            fp = (uintptr_t)st.__rbp;
        }
#endif
        s.pc[s.depth++] = pc;
        s.pc[s.depth++] = lr;
        while (s.depth < (unsigned)kDepth && fp >= stackLo && fp + 16 <= stackHi && !(fp & 7)) {
            const uintptr_t* f = reinterpret_cast<const uintptr_t*>(fp);
            const uintptr_t next = f[0], ret = f[1] & kAddrMask;
            if (!ret) break;
            s.pc[s.depth++] = ret;
            if (next <= fp) break;
            fp = next;
        }
        thread_resume(main);
    }

    void watch() {
        pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
        std::unique_lock<std::mutex> lk(m);
        for (;;) {
            cv.wait(lk, [&] { return quit || (gen.load() & 1); });
            if (quit) return;
            const uint64_t g = gen.load();
            const uint64_t lateAt = due + kSampleNs, now = SDL_GetTicksNS();
            if (lateAt > now)
                cv.wait_for(lk, std::chrono::nanoseconds(lateAt - now), [&] { return quit || gen.load() != g; });
            if (quit) return;
            while (gen.load() == g && cur.size() < kMaxSamples) {
                Sample s;
                lk.unlock();
                sample(s);
                lk.lock();
                if (gen.load() != g) break;
                cur.push_back(s);
                cv.wait_for(lk, std::chrono::microseconds(kSampleUs), [&] { return quit || gen.load() != g; });
                if (quit) return;
            }
            cv.wait(lk, [&] { return quit || gen.load() != g; });
        }
    }

    void write() {
        const std::string path = prefix + "-stall.txt";
        FILE* f = std::fopen(path.c_str(), "w");
        if (!f) return;
        std::fprintf(f, "# stall probe (#1026): %zu stalls in %llu bracketed calls; a stall returns > %.1f ms past its due time\n",
                     stalls.size(), (unsigned long long)calls, (double)StallProbe::kStallNs / 1e6);
        for (const Stall& s : stalls) {
            std::fprintf(f, "\nSTALL row %zu kind %s enter %llu due +%.3f ms return +%.3f ms (over %.3f ms) cpu %.3f ms\n",
                         s.row, s.kind == 1 ? "wait-event" : "precise", (unsigned long long)s.enterNs,
                         (double)(s.dueNs - s.enterNs) / 1e6, (double)(s.leaveNs - s.enterNs) / 1e6,
                         (double)(s.leaveNs - s.dueNs) / 1e6, (double)s.cpuNs / 1e6);
            for (const Loop& l : s.loop)
                std::fprintf(f, "  loop +%8.3f %-15s %s\n", (double)((int64_t)(l.ns - s.enterNs)) / 1e6, actName(l.act),
                             l.last ? "(last observer)" : "(first observer)");
            for (const Sample& x : s.samples) {
                std::fprintf(f, "  sample +%8.3f %s cpu %d%% suspend %d\n", (double)((int64_t)(x.ns - s.enterNs)) / 1e6,
                             stateName(x.runState), x.cpuUsage * 100 / TH_USAGE_SCALE, x.suspend);
                for (unsigned i = 0; i < x.depth; i++) {
                    const uintptr_t a = x.pc[i];
                    if (!a) continue;
                    Dl_info di{};
                    const char* img = "?";
                    const char* sym = "?";
                    uintptr_t off = 0;
                    if (dladdr((const void*)(i < 2 ? a : a - 1), &di)) {
                        if (di.dli_fname) {
                            img = std::strrchr(di.dli_fname, '/');
                            img = img ? img + 1 : di.dli_fname;
                        }
                        if (di.dli_sname) {
                            sym = di.dli_sname;
                            off = a - (uintptr_t)di.dli_saddr;
                        }
                    }
                    std::fprintf(f, "      %s%s`%s+%lu\n", i == 1 ? "(lr) " : "", img, sym, (unsigned long)off);
                }
            }
        }
        std::fclose(f);
    }
};

static void logPolicy(const char* when) {
    task_category_policy_data_t pol{};
    mach_msg_type_number_t n = TASK_CATEGORY_POLICY_COUNT;
    boolean_t def = false;
    const int role = task_policy_get(mach_task_self(), TASK_CATEGORY_POLICY, (task_policy_t)&pol, &n, &def) == KERN_SUCCESS
                         ? (int)pol.role
                         : -99;
    std::fprintf(stderr, "[stall] %s: UI thread QoS 0x%x, task role %d%s\n", when, (unsigned)qos_class_self(), role,
                 def ? " (default)" : "");
}

bool StallProbe::start(const std::string& prefix) {
    if (on_ || !pthread_main_np()) return false;
    p_ = new Impl;
    p_->prefix = prefix;
    p_->main = mach_thread_self();
    pthread_t self = pthread_self();
    p_->stackHi = (uintptr_t)pthread_get_stackaddr_np(self);
    p_->stackLo = p_->stackHi - pthread_get_stacksize_np(self);
    CFRunLoopObserverContext a{0, &p_->tagFirst, nullptr, nullptr, nullptr};
    CFRunLoopObserverContext b{0, &p_->tagLast, nullptr, nullptr, nullptr};
    p_->first = CFRunLoopObserverCreate(nullptr, kCFRunLoopAllActivities, true, LONG_MIN, &Impl::observe, &a);
    p_->last = CFRunLoopObserverCreate(nullptr, kCFRunLoopAllActivities, true, LONG_MAX, &Impl::observe, &b);
    CFRunLoopAddObserver(CFRunLoopGetMain(), p_->first, kCFRunLoopCommonModes);
    CFRunLoopAddObserver(CFRunLoopGetMain(), p_->last, kCFRunLoopCommonModes);
    p_->watcher = std::thread([this] { p_->watch(); });
    on_ = true;
    logPolicy("start");
    return true;
}

void StallProbe::enter(uint64_t dueNs, unsigned char kind, size_t row) {
    if (!on_) return;
    p_->kind = kind;
    p_->row = row;
    p_->ringAtEnter = p_->ringN;
    p_->cpu0 = threadCpuNs();
    p_->enterNs = SDL_GetTicksNS();
    {
        std::lock_guard<std::mutex> lk(p_->m);
        p_->due = dueNs;
        p_->cur.clear();
        p_->gen.fetch_add(1);
    }
    p_->cv.notify_one();
}

void StallProbe::leave() {
    if (!on_ || !(p_->gen.load() & 1)) return;
    const uint64_t now = SDL_GetTicksNS();
    const uint64_t cpu = threadCpuNs() - p_->cpu0;
    p_->cpuAcc += cpu;
    p_->calls++;
    Stall s;
    uint64_t due;
    {
        std::lock_guard<std::mutex> lk(p_->m);
        due = p_->due;
        p_->gen.fetch_add(1);
        if (now > due + kStallNs) s.samples.swap(p_->cur);
    }
    p_->cv.notify_one();
    if (now <= due + kStallNs || p_->stalls.size() >= kMaxStalls) return;
    s.row = p_->row;
    s.kind = p_->kind;
    s.enterNs = p_->enterNs;
    s.dueNs = due;
    s.leaveNs = now;
    s.cpuNs = cpu;
    const size_t n = p_->ringN - p_->ringAtEnter;
    for (size_t i = n > kRing ? p_->ringN - kRing : p_->ringAtEnter; i < p_->ringN; i++) s.loop.push_back(p_->ring[i % kRing]);
    p_->stalls.push_back(std::move(s));
}

uint64_t StallProbe::takeCpuNs() {
    if (!on_) return 0;
    const uint64_t c = p_->cpuAcc;
    p_->cpuAcc = 0;
    return c;
}

void StallProbe::stop() {
    if (!on_) return;
    on_ = false;
    {
        std::lock_guard<std::mutex> lk(p_->m);
        p_->quit = true;
    }
    p_->cv.notify_one();
    p_->watcher.join();
    CFRunLoopRemoveObserver(CFRunLoopGetMain(), p_->first, kCFRunLoopCommonModes);
    CFRunLoopRemoveObserver(CFRunLoopGetMain(), p_->last, kCFRunLoopCommonModes);
    CFRelease(p_->first);
    CFRelease(p_->last);
    logPolicy("stop");
    std::fprintf(stderr, "[stall] %zu stalls in %llu calls -> %s-stall.txt\n", p_->stalls.size(),
                 (unsigned long long)p_->calls, p_->prefix.c_str());
    p_->write();
}

StallProbe::~StallProbe() {
    stop();
    delete p_;
}

}  // namespace vj

#else  // !__APPLE__

namespace vj {
bool StallProbe::start(const std::string&) { return false; }
void StallProbe::stop() {}
void StallProbe::enter(uint64_t, unsigned char, size_t) {}
void StallProbe::leave() {}
uint64_t StallProbe::takeCpuNs() { return 0; }
StallProbe::~StallProbe() {}
}  // namespace vj

#endif
