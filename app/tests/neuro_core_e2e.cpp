// Neuro's input program against the REAL core (#967 phase 3c): the replica (AgentPlayer, driven by the core's input
// poll through pollAgent) presses exactly N polls per step -- inline, on the emulation thread in lock-step, and on the
// emulation thread free-running at 600 Hz -- and the Jaguar joypad matrix the core reads shows it frame for frame.
// No sockets, no Agent: the program is published by this (UI) thread, as Agent::publishTo does.
//
// usage: vj_neuro_core_e2e <rom.j64>
#include "core_host.h"
#include "emu_thread.h"
#include "input_snapshot.h"
#include "neuro_actions.h"
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
using namespace vj::neuro;

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

ControlSet pad(Ctl c) {
    ControlSet s;
    s.set(c);
    return s;
}

AgentProgram prog(uint64_t gen, uint64_t lineage, uint64_t first, std::vector<AgentStep> steps, bool enabled = true) {
    AgentProgram p;
    p.gen = gen;
    p.lineage = lineage;
    p.firstFrame = first;
    p.enabled = enabled;
    p.steps = steps;
    return p;
}

// Three 3-frame B presses with a 3-frame gap: what the Tempest "fire" macro with times=3 queues.
AgentProgram shots(bool enabled = true) {
    return prog(1, 1, 0, {{pad(C_B), 3}, {ControlSet(), 3}, {pad(C_B), 3}, {ControlSet(), 3}, {pad(C_B), 3}}, enabled);
}

struct Rig : EmuThread::Client {
    CoreHost& host = CoreHost::instance();
    InputSnapshot snap;
    AgentPlayer player;
    CoreInput cur;
    std::vector<uint32_t> record;   // per frame: the joypad matrix, hashed (emulation thread)

    void threadStarted() override { host.setOwner(std::this_thread::get_id()); }
    void threadStopping() override { host.setOwner(std::thread::id()); }
    bool canRun() override { return host.isLoaded(); }
    double framePeriod() override { return 1.0 / 600.0; }   // free-running parts: ten times real speed
    double audioQueuedMs() override { return -1.0; }
    void runFrame() override {
        host.runFrame();
        record.push_back(fnv(joypad0Buttons, 21));
        host.publishSnapshot();
    }
    void afterCommands() override { host.publishSnapshot(); }
};

// The core's input callbacks as the app wires them, minus the UI's own controls: the snapshot's input, then the
// agent's presses.
struct Wiring {
    Rig& rig;
    explicit Wiring(Rig& r) : rig(r) {
        rig.host.inputPoll = [this] {
            rig.snap.take(rig.cur);
            const ControlSet a = pollAgent(rig.snap, rig.player);
            if (a.any()) rig.cur.addControls(0, a);
        };
        rig.host.inputState = [this](unsigned p, unsigned d, unsigned i, unsigned id) {
            return rig.cur.state(p, d, i, id);
        };
    }
    ~Wiring() {
        rig.host.inputPoll = nullptr;
        rig.host.inputState = nullptr;
    }
};

// Loads the ROM and runs 120 frames in lock-step; `program` (if any) is published before frame 60.
std::vector<uint32_t> lockStep(const std::string& rom, EmuThread::Mode mode, const AgentProgram* program) {
    Rig rig;
    Wiring wiring(rig);
    EmuThread emu;
    rig.host.post = [&emu](std::function<void()> fn) { emu.post(std::move(fn)); };
    emu.setPaused(true);   // frames only through advance()
    CHECK(emu.start(mode, rig));
    std::string err;
    CHECK(emu.call([&] { return rig.host.loadContent(rom, err); }));
    for (unsigned i = 0; i < 120; i++) {
        if (i == 60 && program) rig.snap.publishAgent(*program);
        emu.advance(1);
    }
    emu.call([&] { rig.host.unloadContent(); });
    CHECK(emu.stop());
    rig.host.post = nullptr;
    return rig.record;   // the thread has been joined: its writes are visible
}

// Runs free (600 Hz) on the emulation thread: the program is published once 30 frames have run, and the run goes on
// to frame 200.
std::vector<uint32_t> freeRunning(const std::string& rom, const AgentProgram& program) {
    Rig rig;
    Wiring wiring(rig);
    EmuThread emu;
    rig.host.post = [&emu](std::function<void()> fn) { emu.post(std::move(fn)); };
    CHECK(emu.start(EmuThread::Mode::Threaded, rig));   // not paused
    std::string err;
    CHECK(emu.call([&] { return rig.host.loadContent(rom, err); }));
    auto waitFrames = [&](uint64_t n) {
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(60);   // host load / TSan
        while (emu.framesRun() < n && std::chrono::steady_clock::now() < end)
            std::this_thread::sleep_for(std::chrono::milliseconds(3));
        CHECK(emu.framesRun() >= n);
    };
    waitFrames(30);
    rig.snap.publishAgent(program);
    waitFrames(200);
    emu.call([&] { rig.host.unloadContent(); });
    CHECK(emu.stop());
    rig.host.post = nullptr;
    return rig.record;
}

std::string diffs(const std::vector<uint32_t>& a, const std::vector<uint32_t>& b) {
    std::string s;
    for (size_t i = 0; i < a.size() && i < b.size(); i++)
        if (a[i] != b[i]) s += (s.empty() ? "" : ",") + std::to_string(i);
    return s;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: vj_neuro_core_e2e <rom.j64>\n");
        return 2;
    }
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path tmp = fs::temp_directory_path(ec) / ("vj-neuro-core-e2e-" + std::to_string((long long)std::time(nullptr)));
    fs::create_directories(tmp / "saves", ec);
    fs::create_directories(tmp / "system", ec);
    CoreHost::instance().setDirectories((tmp / "system").string() + "/", (tmp / "saves").string() + "/");
    const std::string rom = argv[1];

    // 1: idle, no program.
    const std::vector<uint32_t> base = lockStep(rom, EmuThread::Mode::Inline, nullptr);
    CHECK(base.size() == 120);
    bool flat = true;
    for (uint32_t h : base) flat = flat && h == base[0];
    CHECK(flat);   // an idle pad reads the same matrix every frame

    // 2: nine polls, inline: exactly the frames of the three shots differ from idle.
    const AgentProgram p = shots();
    const std::vector<uint32_t> inl = lockStep(rom, EmuThread::Mode::Inline, &p);
    CHECK(inl.size() == 120);
    const std::string want = "60,61,62,66,67,68,72,73,74";
    CHECK(diffs(inl, base) == want);
    if (diffs(inl, base) != want) std::fprintf(stderr, "  inline: differs from idle at frames %s\n", diffs(inl, base).c_str());

    // 3: the same on the emulation thread.
    const std::vector<uint32_t> thr = lockStep(rom, EmuThread::Mode::Threaded, &p);
    CHECK(thr == inl);
    if (thr != inl) std::fprintf(stderr, "  threaded vs inline: differ at frames %s\n", diffs(thr, inl).c_str());

    // 4: free-running at 600 Hz: nine polls, however fast the frames come.
    const std::vector<uint32_t> fr = freeRunning(rom, p);
    unsigned pressed = 0;
    for (uint32_t h : fr) pressed += h != base[0];
    CHECK(fr.size() >= 200);
    CHECK(pressed == 9);
    if (pressed != 9) std::fprintf(stderr, "  free-running: %u pressed frames in %zu\n", pressed, fr.size());

    // 5: a disabled program (a menu is open, hardcore) presses nothing.
    const AgentProgram off = shots(false);
    const std::vector<uint32_t> dis = lockStep(rom, EmuThread::Mode::Threaded, &off);
    CHECK(dis == base);

    fs::remove_all(tmp, ec);   // our own temp dir only
    std::printf("neuro_core_e2e: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
