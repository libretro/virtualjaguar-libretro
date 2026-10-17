// The REAL core inline vs on the emulation thread (#967): the same per-frame input script must give the same frames
// and the same Jaguar joypad matrix, frame for frame -- with the script applied on the emulation side, and through
// InputSnapshot in lock-step with this (UI) thread.  A negative control proves the script reaches the core.  Then the
// plan's review-focus pins: a load while frames run, and the owner rule.
//
// usage: vj_emu_thread_e2e <rom.j64>
#include "core_host.h"
#include "emu_thread.h"
#include "frame_exchange.h"
#include "input_snapshot.h"
#include "remap.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

extern "C" {
extern uint8_t joypad0Buttons[];   // src/jerry/joystick.h: what the 68000 reads for port 1
}

using namespace vj;

static int g_fail = 0, g_checks = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        g_checks++;                                                              \
        if (!(cond)) {                                                           \
            g_fail++;                                                            \
            std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
        }                                                                        \
    } while (0)

namespace {

uint32_t fnv(const void* p, size_t n, uint32_t h = 2166136261u) {
    const unsigned char* b = (const unsigned char*)p;
    for (size_t i = 0; i < n; i++) {
        h ^= b[i];
        h *= 16777619u;
    }
    return h;
}

// What frame `i` holds on port 1: A for frames 60-119, keypad 5 + B for 180-239.  `press` false = nothing at all.
CoreInput script(unsigned i, bool press) {
    CoreInput c;
    c.beginFrame();
    if (!press) return c;
    ControlSet s;
    if (i >= 60 && i < 120) s.set(C_A);
    if (i >= 180 && i < 240) {
        s.set(C_K5);
        s.set(C_B);
    }
    c.addControls(0, s);
    return c;
}

enum class Feed { Script, Snapshot };   // the script on the emulation side, or through InputSnapshot

struct Rig : EmuThread::Client {
    CoreHost& host = CoreHost::instance();
    FrameExchange frames;
    InputSnapshot snap;
    CoreInput cur;
    Feed feed = Feed::Script;
    bool press = true;
    unsigned index = 0;              // emulated frames in this session (emulation thread)
    std::vector<uint32_t> record;    // per frame: pixels + joypad matrix, hashed (emulation thread)
    uint64_t lastSerial = 0;

    void threadStarted() override { host.setOwner(std::this_thread::get_id()); }
    void threadStopping() override { host.setOwner(std::thread::id()); }
    bool canRun() override { return host.isLoaded(); }
    double framePeriod() override { return 1.0 / 600.0; }   // free-running parts: ten times real speed
    double audioQueuedMs() override { return -1.0; }
    void runFrame() override {
        if (feed == Feed::Script) cur = script(index, press);
        host.runFrame();
        const Frame& f = host.frame();
        uint32_t h = fnv(f.pixels.data(), f.pixels.size() * sizeof(uint32_t));
        h = fnv(joypad0Buttons, 21, h);
        record.push_back(h);
        if (f.serial != lastSerial) {
            FrameExchange::Slot& s = frames.writeBuffer();
            s.frame = f;
            frames.publish();
            lastSerial = f.serial;
        }
        index++;
        host.publishSnapshot();
    }
    void afterCommands() override { host.publishSnapshot(); }

    void begin(Feed fd, bool pr) {
        feed = fd;
        press = pr;
        index = 0;
        record.clear();
        lastSerial = 0;
    }
};

// Loads the ROM, runs `n` frames, unloads; returns the per-frame record.
std::vector<uint32_t> session(Rig& rig, const std::string& rom, unsigned n, bool threaded, Feed feed, bool press) {
    rig.begin(feed, press);
    EmuThread emu;
    rig.host.post = [&emu](std::function<void()> fn) { emu.post(std::move(fn)); };
    emu.setPaused(true);   // frames only through advance()
    CHECK(emu.start(threaded ? EmuThread::Mode::Threaded : EmuThread::Mode::Inline, rig));
    std::string err;
    CHECK(emu.call([&] { return rig.host.loadContent(rom, err); }));
    if (feed == Feed::Snapshot) {
        for (unsigned i = 0; i < n; i++) {
            rig.snap.publish(script(i, press), 0);   // lock-step: input i is published, then frame i runs and takes it
            emu.advance(1);
        }
    } else {
        emu.advance(n);
    }
    emu.call([&] { rig.host.unloadContent(); });
    CHECK(emu.stop());
    rig.host.post = nullptr;
    return rig.record;   // the thread has been joined: its writes are visible
}

void reportDivergence(const char* what, const std::vector<uint32_t>& a, const std::vector<uint32_t>& b) {
    for (size_t i = 0; i < a.size() && i < b.size(); i++)
        if (a[i] != b[i]) {
            std::fprintf(stderr, "  %s: first divergence at frame %zu (%08x vs %08x)\n", what, i, a[i], b[i]);
            return;
        }
    if (a.size() != b.size()) std::fprintf(stderr, "  %s: %zu vs %zu frames\n", what, a.size(), b.size());
}

// Polls `cond` every few ms for up to 5 s (host load makes fixed sleeps flaky); true when it was reached.
template <class F>
bool waitUntil(F cond) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!cond()) {
        if (std::chrono::steady_clock::now() >= end) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(3));
    }
    return true;
}

// A load while frames free-run: served at a frame boundary (never refused as "halted"), the core's buffer is empty
// inside the load call (a thumbnail taken then would be blank, never the previous game's), and FrameExchange never
// hands out a frame from before the switch.
void loadWhileRunning(Rig& rig, const std::string& rom) {
    rig.begin(Feed::Script, false);
    EmuThread emu;
    rig.host.post = [&emu](std::function<void()> fn) { emu.post(std::move(fn)); };
    CHECK(emu.start(EmuThread::Mode::Threaded, rig));   // not paused: frames run at 600 Hz
    std::string err;
    CHECK(emu.call([&] { return rig.host.loadContent(rom, err); }));
    // Deadline poll, not a fixed sleep: under host load (or TSan) the thread may be slow to get going.
    waitUntil([&] { return emu.framesRun() >= 1; });
    CHECK(emu.framesRun() >= 1);
    uint64_t lastSeen = 0;
    while (const FrameExchange::Slot* s = rig.frames.takeNewest()) lastSeen = s->frame.serial;
    unsigned widthInsideLoad = 1;
    const bool loaded = emu.call([&] {
        rig.frames.clear();
        const bool ok = rig.host.loadContent(rom, err);
        widthInsideLoad = rig.host.frame().width;
        return ok;
    });
    CHECK(loaded);
    CHECK(widthInsideLoad == 0);
    const uint64_t epoch = rig.frames.epoch();
    int newFrames = 0;
    bool monotonic = true, sameEpoch = true;
    waitUntil([&] {
        if (const FrameExchange::Slot* s = rig.frames.takeNewest()) {
            newFrames++;
            if (s->epoch != epoch) sameEpoch = false;
            if (s->frame.serial <= lastSeen) monotonic = false;
            lastSeen = s->frame.serial;
        }
        return newFrames >= 1;
    });
    // A few more takes once frames are flowing, to check epoch and serial order over several frames.
    for (int i = 0; i < 25; i++) {
        if (const FrameExchange::Slot* s = rig.frames.takeNewest()) {
            newFrames++;
            if (s->epoch != epoch) sameEpoch = false;
            if (s->frame.serial <= lastSeen) monotonic = false;
            lastSeen = s->frame.serial;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    CHECK(newFrames > 0);
    CHECK(sameEpoch);
    CHECK(monotonic);   // VideoOut::upload skips a repeated serial: serials must keep rising across the load
    emu.call([&] { rig.host.unloadContent(); });
    CHECK(emu.stop());
    rig.host.post = nullptr;
}

// Off the emulation thread a core call is refused and counted; the emulation thread runs it.
void ownerRule(Rig& rig, const std::string& rom, const std::string& stateFile) {
    rig.begin(Feed::Script, false);
    EmuThread emu;
    rig.host.post = [&emu](std::function<void()> fn) { emu.post(std::move(fn)); };
    emu.setPaused(true);
    CHECK(emu.start(EmuThread::Mode::Threaded, rig));
    std::string err;
    CHECK(emu.call([&] { return rig.host.loadContent(rom, err); }));
    const unsigned v0 = rig.host.ownerViolations();
    const uint64_t f0 = rig.host.frameCount();
    rig.host.runFrame();
    CHECK(!rig.host.saveStateToFile(stateFile, err));
    CHECK(rig.host.frameCount() == f0);
    CHECK(rig.host.ownerViolations() >= v0 + 2);
    CHECK(emu.advance(2) && rig.host.frameCount() == f0 + 2);
    CHECK(emu.call([&] { return rig.host.saveStateToFile(stateFile, err); }));
    emu.call([&] { rig.host.unloadContent(); });
    CHECK(emu.stop());
    rig.host.post = nullptr;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: vj_emu_thread_e2e <rom.j64>\n");
        return 2;
    }
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path tmp = fs::temp_directory_path(ec) / ("vj-emu-thread-e2e-" + std::to_string((long long)std::time(nullptr)));
    fs::create_directories(tmp / "saves", ec);
    fs::create_directories(tmp / "system", ec);
    Rig rig;
    rig.host.setDirectories((tmp / "system").string() + "/", (tmp / "saves").string() + "/");
    rig.host.inputPoll = [&rig] {
        if (rig.feed == Feed::Snapshot) rig.snap.take(rig.cur);
    };
    rig.host.inputState = [&rig](unsigned p, unsigned d, unsigned i, unsigned id) { return rig.cur.state(p, d, i, id); };
    const std::string rom = argv[1];
    const unsigned N = 300;

    const std::vector<uint32_t> inl = session(rig, rom, N, false, Feed::Script, true);
    const std::vector<uint32_t> thr = session(rig, rom, N, true, Feed::Script, true);
    CHECK(inl.size() == N);
    CHECK(thr == inl);
    if (thr != inl) reportDivergence("threaded vs inline", thr, inl);

    const std::vector<uint32_t> viaSnapshot = session(rig, rom, N, true, Feed::Snapshot, true);
    CHECK(viaSnapshot == inl);
    if (viaSnapshot != inl) reportDivergence("InputSnapshot vs inline", viaSnapshot, inl);

    const std::vector<uint32_t> none = session(rig, rom, N, true, Feed::Script, false);
    CHECK(none.size() == N && none[59] == inl[59]);   // identical until the first press...
    bool differs = false;
    for (unsigned i = 60; i < 126 && i < none.size(); i++) differs = differs || none[i] != inl[i];
    CHECK(differs);                                   // ...then the script is visible in the record

    loadWhileRunning(rig, rom);
    ownerRule(rig, rom, (tmp / "owner.state").string());

    fs::remove_all(tmp, ec);   // our own temp dir only
    std::printf("emu_thread_e2e: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
