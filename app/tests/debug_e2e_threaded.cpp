// The debugger halt on the emulation thread (#967 phase 3b), with the REAL core.  This (test) thread plays the UI: it
// issues what the Tools windows issue, and polls the way App::syncDebug polls (syncCore + syncHalt, then look).  Every
// UI-side operation is timed: none may block over 50 ms, because the window keeps drawing while the game is halted.
//
// The rig mirrors App (wireHost + startEmuThread + emuRunOneFrame's tail + haltWait): the policy under test is App's, not a
// test-only one.  Sections:
//   1 break / step / continue          2 a debug call while halted         3 a frame-lane option post waits for Continue
//   4 a save state that gives up when a halt begins                         5 detach-first before a content call
//   6 hardcore from the UI while halted                                      7 a halt during a debugCall from the UI
//   8 a posted lane detach inside a live halt                                9 stop() while halted, abortHalt alone
// then, each on a fresh rig: a gdb_wait boot halt with no client (10: Detach releases it; 11: stop() joins within 500 ms).
//
// usage: vj_debug_e2e_threaded <rom.j64>
#include "core_host.h"
#include "debug/debug_session.h"
#include "emu_thread.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

using namespace vj;
namespace fs = std::filesystem;

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

using Clock = std::chrono::steady_clock;
double since(Clock::time_point t0) { return std::chrono::duration<double>(Clock::now() - t0).count(); }
void sleepMs(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

struct Rig : EmuThread::Client {
    CoreHost& host = CoreHost::instance();
    EmuThread emu;
    DebugSession dbg;
    std::atomic<bool> quitting{false};
    std::atomic<int> haltsEmu{0}, resumesEmu{0};
    void threadStarted() override { host.setOwner(std::this_thread::get_id()); }
    void threadStopping() override { host.setOwner(std::thread::id()); }
    bool canRun() override { return host.isLoaded(); }
    double framePeriod() override { return 1.0 / 60.0; }
    double audioQueuedMs() override { return -1.0; }
    void runFrame() override {   // App::emuRunOneFrame's tail
        host.runFrame();
        dbg.core().frameEnded();
        host.publishSnapshot();
        dbg.fillViews();
    }
    void afterCommands() override { host.publishSnapshot(); }
    void wire() {   // what App::wireHost + startEmuThread do
        host.onDebugIface = [this](const vj_debug_iface* i) { dbg.onCoreIface(i); };
        host.post = [this](std::function<void()> fn) { emu.post(std::move(fn)); };
        dbg.coreCall = [this](std::function<void()> fn) { emu.debugCall([&] { fn(); }); };
        dbg.corePost = [this](std::function<void()> fn) { emu.debugPost(std::move(fn)); };
        dbg.core().notifyVerdict = [this] { emu.debugPost([] {}); };
        dbg.core().waitVerdict = [this] {   // App::haltWait
            emu.serveDebugLane(DebugCore::kPumpWait);
            if (dbg.views().pending(dbg.refreshSerial())) dbg.fillViews();
        };
        dbg.core().abortHalt = [this] { return quitting.load(); };
        dbg.core().onHalt.push_back([this](int, unsigned, int) { haltsEmu++; });
        dbg.core().onResume.push_back([this] { resumesEmu++; });
    }
    void unwire() {
        host.onDebugIface = nullptr;
        host.post = nullptr;
    }
    void sync() {   // App::syncDebug's first lines, once per UI frame
        dbg.syncCore();
        dbg.syncHalt();
    }
    // App::openContentOnCore: the load and the silent gdb host's attach are one emulation-thread task, so the boot halt
    // (gdb_wait) that the first frame raises finds the host.
    bool loadAttached(const std::string& rom, std::string& err) {
        const bool ok = emu.call([&] {
            const bool l = host.loadContent(rom, err);
            if (l && dbg.core().loaded() && !dbg.core().attached()) dbg.core().attach();
            return l;
        });
        sync();
        dbg.setContentLoaded(ok);
        return ok;
    }
    bool load(const std::string& rom, std::string& err) {
        const bool ok = emu.call([&] { return host.loadContent(rom, err); });
        sync();
        dbg.setContentLoaded(ok);
        return ok;
    }
};

// A free loopback TCP port (bind to 0, read it back, close), so parallel ctest runs do not collide on the gdb stub's port.
// Fallback when that fails: a pid-derived port.
std::string freePort() {
    int port = 0;
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    SOCKET fd = socket(AF_INET, SOCK_STREAM, 0);
    const bool bad = fd == INVALID_SOCKET;
#else
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    const bool bad = fd < 0;
#endif
    if (!bad) {
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        a.sin_port = 0;
        socklen_t n = sizeof a;
        if (bind(fd, (sockaddr*)&a, sizeof a) == 0 && getsockname(fd, (sockaddr*)&a, &n) == 0) port = ntohs(a.sin_port);
#ifdef _WIN32
        closesocket(fd);
#else
        close(fd);
#endif
    }
    if (port <= 0) port = 40000 + (int)(std::clock() % 20000);
    return std::to_string(port);
}

// Runs `fn` on this thread and checks it took under 50 ms.
template <class F>
void ui(const char* what, F fn) {
    const auto t0 = Clock::now();
    fn();
    const double s = since(t0);
    if (s > 0.05) {
        g_fail++;
        std::fprintf(stderr, "FAIL %s blocked the UI thread for %.0f ms\n", what, s * 1e3);
    }
}

// The UI's poll: sync, test, 2 ms, up to 5 s.
template <class F>
bool until(Rig& r, F cond) {
    const auto end = Clock::now() + std::chrono::seconds(5);
    for (;;) {
        r.sync();
        if (cond()) return true;
        if (Clock::now() >= end) return false;
        sleepMs(2);
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: vj_debug_e2e_threaded <rom.j64>\n");
        return 2;
    }
    const std::string rom = argv[1];
    std::error_code ec;
    fs::path tmp = fs::temp_directory_path(ec);
    if (ec) tmp = ".";
    tmp /= "vj-debug-e2e-thr-" + std::to_string((long long)std::time(nullptr)) + "-" + std::to_string((long long)clock());
    fs::create_directories(tmp / "system", ec);
    fs::create_directories(tmp / "saves", ec);
    const std::string t = tmp.string() + "/";

    Rig r;
    r.host.setDirectories(t + "system/", t + "saves/");
    r.wire();
    CHECK(r.emu.start(EmuThread::Mode::Threaded, r));
    std::string err;
    CHECK(r.load(rom, err));
    CHECK(until(r, [&] { return r.host.frameCount() >= 30; }));
    const std::string stateFile = t + "s4.state";

    // 1. Break / step / continue from the UI thread.
    ui("breakNow", [&] { r.dbg.breakNow(); });
    CHECK(until(r, [&] { return r.dbg.halted(); }));
    CHECK(r.dbg.haltReason() == VJ_DBG_STOP_USER && !r.dbg.haltByGdb());
    CHECK(until(r, [&] { return r.haltsEmu == 1; }));   // the core publishes the halt, THEN runs onHalt: poll the counter
    const uint64_t f = r.host.frameCount();
    sleepMs(100);
    CHECK(r.host.frameCount() == f);   // no frames while halted
    const unsigned pc1 = r.dbg.haltedPc();
    const uint64_t s1 = r.dbg.haltSerial();
    ui("step", [&] { r.dbg.step(); });
    CHECK(until(r, [&] { return r.dbg.haltSerial() != s1 && r.dbg.halted(); }));
    CHECK(r.dbg.haltReason() == VJ_DBG_STOP_STEP && !r.dbg.haltByGdb() && r.resumesEmu == 0);   // pc may equal pc1: not asserted
    ui("resume", [&] { r.dbg.resume(); });
    CHECK(until(r, [&] { return !r.dbg.halted(); }));
    CHECK(until(r, [&] { return r.resumesEmu == 1; }));   // likewise onResume runs just after the halt closes
    CHECK(until(r, [&] { return r.host.frameCount() > f; }));

    // 2. A debug call while halted answers within 50 ms (the pump serves the lane).
    ui("breakNow", [&] { r.dbg.breakNow(); });
    CHECK(until(r, [&] { return r.dbg.halted(); }));
    ui("read", [&] {
        std::vector<uint8_t> b;
        CHECK(r.dbg.read(0x7000, b, 4));
    });
    ui("poke", [&] { CHECK(r.dbg.pokeU(0x7000, 4, 0xCAFEF00D) == 4); });
    vj_debug_regs regs{};
    ui("regs", [&] { CHECK(r.dbg.getRegsNow(VJ_DBG_TARGET_M68K, regs)); });
    CHECK(regs.pc == r.dbg.haltedPc());
    unsigned v = 0;
    ui("readU", [&] { CHECK(r.dbg.readU(0x7000, 4, v) && v == 0xCAFEF00D); });

    // 3. Review Focus 5: a frame-lane option post made while halted applies only after Continue.  The UI-side getter
    // (uiUser_) answers the new value at once by design, so the core's own copy is read where it lives: on the emulation
    // thread, through the debug lane (the pump serves it while halted).
    const auto coreOption = [&] {
        std::string val;
        r.dbg.callCore([&] { val = r.host.optionValueFor("virtualjaguar_blitter_timing", "unset"); });
        return val;
    };
    CHECK(coreOption() == "unset");
    r.host.setOptionValue("virtualjaguar_blitter_timing", "enabled");
    sleepMs(60);
    CHECK(coreOption() == "unset");   // the post waits for a frame boundary; the halted frame has none
    const uint64_t f3 = r.host.frameCount();
    ui("resume", [&] { r.dbg.resume(); });
    CHECK(until(r, [&] { return !r.dbg.halted(); }));   // the mirror must see the resume before breakNow() is allowed again
    CHECK(until(r, [&] { return coreOption() == "enabled"; }));
    CHECK(r.host.frameCount() >= f3 + 1);
    r.host.resetOptionToDefault("virtualjaguar_blitter_timing");

    // 4. Review Focus 1: a save state requested just before a halt returns false within 300 ms, and the cancelled save never runs.
    ui("breakNow", [&] { r.dbg.breakNow(); });
    CHECK(until(r, [&] { return r.dbg.halted(); }));
    const uint64_t s4 = r.dbg.haltSerial();
    r.dbg.callCore([&] { r.dbg.core().haltNext(VJ_DBG_TARGET_M68K); });   // served by the pump: the next instruction after Continue re-halts
    std::atomic<bool> saved{false};
    double tCall = 0;
    std::string err4;
    std::thread caller([&] {
        const auto t0 = Clock::now();
        saved = r.emu.call([&] { return r.host.saveStateToFile(stateFile, err4); },
                           [&] {
                               const HaltState h = r.dbg.core().halt();
                               return h.halted && h.serial != s4;
                           });
        tCall = since(t0);
    });
    sleepMs(5);
    ui("resume", [&] { r.dbg.resume(); });
    caller.join();
    CHECK(!saved && err4.empty() && tCall < 0.3);
    CHECK(until(r, [&] { return r.dbg.halted() && r.dbg.haltSerial() != s4; }));
    CHECK(r.dbg.haltReason() == VJ_DBG_STOP_USER);
    ui("resume", [&] { r.dbg.resume(); });
    CHECK(until(r, [&] { return !r.dbg.halted(); }));
    // The cancelled task must never have run: a frame-lane task is held while the core is halted, so the proof is after the
    // halt ended and frames ran (a stale task would have been drained and written the file by now).
    const uint64_t f4 = r.host.frameCount();
    CHECK(until(r, [&] { return r.host.frameCount() > f4 + 3; }));
    CHECK(!fs::exists(stateFile) && err4.empty());
    const std::string stateFile2 = t + "s4b.state";   // a different file: the final save is its own proof
    CHECK(r.emu.call([&] { return r.host.saveStateToFile(stateFile2, err4); }, [&] { return r.dbg.core().halt().halted; }) &&
          fs::exists(stateFile2));   // no halt now: the save runs
    CHECK(!fs::exists(stateFile));

    // 5. Detach-first: a content call after detachAndResume() runs within one frame and nothing re-halts.
    CHECK(r.dbg.addBreakpoint(VJ_DBG_TARGET_M68K, pc1));
    CHECK(until(r, [&] { return r.dbg.halted(); }));   // pc1 is revisited: the inline e2e relies on the same
    CHECK(r.dbg.haltReason() == VJ_DBG_STOP_BREAKPOINT);
    const uint64_t s5 = r.dbg.haltSerial();
    ui("detach", [&] { r.dbg.detachAndResume(); });
    const auto t5 = Clock::now();
    CHECK(r.emu.call([&] { return r.host.loadContent(rom, err); },
                     [&] {
                         const HaltState h = r.dbg.core().halt();
                         return h.halted && h.serial != s5;
                     }));
    CHECK(since(t5) < 0.5);
    r.sync();
    CHECK(!r.dbg.halted() && !r.dbg.attached() && r.dbg.breakpoints().empty());   // syncCore forgets the list on the core swap
    CHECK(until(r, [&] { return r.host.frameCount() >= 10; }));

    // 6. Hardcore from the UI while halted.
    ui("breakNow", [&] { r.dbg.breakNow(); });
    CHECK(until(r, [&] { return r.dbg.halted(); }));
    std::string closed;
    r.dbg.onForcedClose.push_back([&](const char* w) { closed = w; });
    ui("hardcore", [&] { r.dbg.setHardcore(true); });
    CHECK(until(r, [&] { return !r.dbg.halted(); }));
    CHECK(!r.dbg.attached() && closed == "hardcore");
    const uint64_t f6 = r.host.frameCount();
    CHECK(until(r, [&] { return r.host.frameCount() > f6 + 5; }));
    r.dbg.setHardcore(false);
    CHECK(r.dbg.enabled());

    // 7. A halt during a debugCall issued from the UI.
    ui("breakNow+read", [&] {
        r.dbg.breakNow();
        std::vector<uint8_t> b;
        CHECK(r.dbg.read(0, b, 16));
    });
    CHECK(until(r, [&] { return r.dbg.halted(); }));
    ui("resume", [&] { r.dbg.resume(); });
    CHECK(until(r, [&] { return !r.dbg.halted(); }));

    // 8. A posted lane detach that runs inside a live halt (the B1 carry): the UI's mirror has not seen the halt yet, so
    // detach() takes the lane branch; the pump serves the post, and the post offers DETACH for the halt it finds.  Nothing
    // below calls sync() until the core is halted, which is what keeps the mirror stale.
    ui("breakNow", [&] { r.dbg.breakNow(); });
    {
        const auto end = Clock::now() + std::chrono::seconds(5);
        while (!r.dbg.core().halt().halted && Clock::now() < end) sleepMs(1);
    }
    CHECK(r.dbg.core().halt().halted);
    CHECK(!r.dbg.halted());   // the mirror lags: that is the window
    const uint64_t r8 = (uint64_t)r.resumesEmu.load();
    ui("detach (stale mirror)", [&] { r.dbg.detach(); });
    CHECK(until(r, [&] { return !r.dbg.core().halt().halted; }));   // the pump released
    CHECK(until(r, [&] { return !r.dbg.halted() && !r.dbg.attached(); }));
    CHECK(until(r, [&] { return (uint64_t)r.resumesEmu.load() == r8 + 1; }));
    const uint64_t f8 = r.host.frameCount();
    CHECK(until(r, [&] { return r.host.frameCount() > f8 + 3; }));   // the game runs on, nothing re-halts
    CHECK(!r.dbg.core().halt().halted);

    // The main rig ends here (not halted); sections 9-11 each use a fresh rig and thread.
    CHECK(r.emu.callFor(std::chrono::milliseconds(3000), [&] { r.host.unloadContent(); }));
    CHECK(r.emu.stop());
    r.unwire();

    // 9. Review Focus 6: stop() returns true within 500 ms while halted, with abortHalt alone (a user halt).
    {
        Rig q;
        q.host.setDirectories(t + "system/", t + "saves/");
        q.wire();
        CHECK(q.emu.start(EmuThread::Mode::Threaded, q));
        CHECK(q.load(rom, err));
        CHECK(until(q, [&] { return q.host.frameCount() >= 10; }));
        ui("breakNow", [&] { q.dbg.breakNow(); });
        CHECK(until(q, [&] { return q.dbg.halted(); }));
        q.quitting = true;
        q.emu.debugPost([] {});
        const auto t9 = Clock::now();
        CHECK(q.emu.callFor(std::chrono::milliseconds(3000), [&] { q.host.unloadContent(); }));
        CHECK(q.emu.stop());
        CHECK(since(t9) < 0.5);
        CHECK(!q.dbg.core().attached());
        q.unwire();
    }

    // 10. A threaded gdb_wait boot halt with no client: the silent host sees it (by gdb), the UI mirror shows it, no frame
    // runs, and Detach releases it -- the game then runs.
    {
        Rig q;
        q.host.setDirectories(t + "system/", t + "saves/");
        q.host.setOptionValue("virtualjaguar_gdb_stub", "enabled");
        q.host.setOptionValue("virtualjaguar_gdb_wait", "enabled");
        q.host.setOptionValue("virtualjaguar_gdb_port", freePort());
        q.wire();
        CHECK(q.emu.start(EmuThread::Mode::Threaded, q));
        CHECK(q.loadAttached(rom, err));
        CHECK(until(q, [&] { return q.dbg.halted(); }));
        CHECK(q.dbg.haltByGdb());
        CHECK(until(q, [&] { return q.haltsEmu == 1; }));
        const uint64_t fb = q.host.frameCount();
        sleepMs(100);
        CHECK(q.host.frameCount() == fb && q.dbg.halted());   // parked until someone resumes it
        ui("detach", [&] { q.dbg.detachAndResume(); });
        CHECK(until(q, [&] { return !q.dbg.halted(); }));
        CHECK(until(q, [&] { return q.host.frameCount() > fb + 5; }));
        CHECK(!q.dbg.core().halt().halted);
        q.quitting = true;
        CHECK(q.emu.callFor(std::chrono::milliseconds(3000), [&] { q.host.unloadContent(); }));
        CHECK(q.emu.stop());
        q.unwire();
        q.host.resetOptionToDefault("virtualjaguar_gdb_stub");
        q.host.resetOptionToDefault("virtualjaguar_gdb_wait");
        q.host.resetOptionToDefault("virtualjaguar_gdb_port");
    }

    // 11. The same boot halt, then quit: stop() joins within 500 ms (abortHalt alone), nothing left attached.
    {
        Rig q;
        q.host.setDirectories(t + "system/", t + "saves/");
        q.host.setOptionValue("virtualjaguar_gdb_stub", "enabled");
        q.host.setOptionValue("virtualjaguar_gdb_wait", "enabled");
        q.host.setOptionValue("virtualjaguar_gdb_port", freePort());
        q.wire();
        CHECK(q.emu.start(EmuThread::Mode::Threaded, q));
        CHECK(q.loadAttached(rom, err));
        CHECK(until(q, [&] { return q.dbg.halted(); }));
        q.quitting = true;
        q.emu.debugPost([] {});
        const auto t11 = Clock::now();
        CHECK(q.emu.callFor(std::chrono::milliseconds(3000), [&] { q.host.unloadContent(); }));
        CHECK(q.emu.stop());
        CHECK(since(t11) < 0.5);
        CHECK(!q.dbg.core().attached());
        q.unwire();
        q.host.resetOptionToDefault("virtualjaguar_gdb_stub");
        q.host.resetOptionToDefault("virtualjaguar_gdb_wait");
        q.host.resetOptionToDefault("virtualjaguar_gdb_port");
    }

    fs::remove_all(tmp, ec);
    std::printf("debug_e2e_threaded: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
