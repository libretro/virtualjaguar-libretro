// The debug views with the REAL core on the emulation thread (#967 phase 3a): what App does for the Tools windows,
// minus the pixels.  usage: vj_debug_views_threaded <rom-a.j64> <rom-b.j64>
//
// The rig is wired the way App wires it (wireHost + startEmuThread + emuRunOneFrame's tail): the core hands its
// interface over on the emulation thread (onCoreIface), the session reaches the core through the debug lane
// (coreCall / corePost), and every frame ends with fillViews().  The thread stays paused, so frames run only through
// advance(), and every read goes through waitRead(): the UI's per-frame loop (wakeViews() until the buffer arrives).
// A frame run after a fill would make the buffer and a later peek differ (yarc rewrites DRAM every frame), so no case
// runs a frame between a fill and its comparison.
#include "core_host.h"
#include "debug/debug_session.h"
#include "emu_thread.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <functional>
#include <string>
#include <thread>
#include <vector>

using namespace vj;
using Id = DebugViews::Id;

static int g_fail = 0, g_checks = 0;
#define CHECK(c, ...)                                        \
    do {                                                     \
        g_checks++;                                          \
        if (!(c)) {                                          \
            std::printf("FAIL %s:%d: ", __FILE__, __LINE__); \
            std::printf(__VA_ARGS__);                        \
            std::printf("\n");                               \
            g_fail++;                                        \
        }                                                    \
    } while (0)

namespace {

uint32_t fnv(const std::vector<uint8_t>& v) {
    uint32_t h = 2166136261u;
    for (uint8_t b : v) {
        h ^= b;
        h *= 16777619u;
    }
    return h;
}

ViewRequest memReq(unsigned addr, unsigned len) {
    ViewRequest r;
    r.kind = ViewRequest::Kind::Memory;
    r.addr = addr;
    r.len = len;
    return r;
}

ViewRequest dramReq() {
    ViewRequest r;
    r.kind = ViewRequest::Kind::Dram;
    return r;
}

ViewRequest regsReq(int target) {
    ViewRequest r;
    r.kind = ViewRequest::Kind::Regs;
    r.target = target;
    return r;
}

struct Rig : EmuThread::Client {
    CoreHost& host = CoreHost::instance();
    DebugSession dbg;
    EmuThread emu;
    bool threaded;

    explicit Rig(bool thr) : threaded(thr) {}

    void threadStarted() override { host.setOwner(std::this_thread::get_id()); }
    void threadStopping() override { host.setOwner(std::thread::id()); }
    bool canRun() override { return host.isLoaded(); }
    double framePeriod() override { return 1.0 / 600.0; }
    double audioQueuedMs() override { return -1.0; }
    void runFrame() override {   // App::emuRunOneFrame's tail
        host.runFrame();
        host.publishSnapshot();
        dbg.fillViews();
    }
    void afterCommands() override { host.publishSnapshot(); }

    void wire() {   // = App::wireHost + startEmuThread
        host.onDebugIface = [this](const vj_debug_iface* i) {
            dbg.onCoreIface(i);
            if (!host.threaded()) dbg.syncCore();
        };
        dbg.coreCall = [this](std::function<void()> f) { emu.debugCall(std::move(f)); };
        if (threaded) dbg.corePost = [this](std::function<void()> f) { emu.debugPost(std::move(f)); };
        host.post = [this](std::function<void()> f) { emu.post(std::move(f)); };
        emu.setPaused(true);   // frames only through advance()
    }
    void unwire() {
        host.onDebugIface = nullptr;
        host.post = nullptr;
    }

    bool start() { return emu.start(threaded ? EmuThread::Mode::Threaded : EmuThread::Mode::Inline, *this); }
    bool load(const std::string& rom) {
        std::string err;
        const bool ok = emu.call([&] { return host.loadContent(rom, err); });
        if (!ok) std::printf("load %s: %s\n", rom.c_str(), err.c_str());
        return ok;
    }

    // The UI's per-frame loop: wakeViews() every 2 ms until the id's buffer is readable at the current refresh serial,
    // up to 500 ms.  (A buffer from an older serial is the previous fill, not the one asked for.)
    bool waitRead(Id id, ViewBuffer& b) {
        const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
        for (;;) {
            dbg.wakeViews();
            if (dbg.views().read(id, b) && b.serial == dbg.refreshSerial()) return true;
            if (std::chrono::steady_clock::now() >= end) return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }

    // A peek of the live core on the emulation thread (the debug lane).
    std::vector<uint8_t> peek(unsigned addr, unsigned n) {
        std::vector<uint8_t> v(n);
        const int rc = emu.debugCall([&] { return dbg.core().peek(addr, v.data(), n); });
        if (rc != (int)n) v.clear();
        return v;
    }
};

void cases(Rig& r, const std::string& romA, const std::string& romB) {
    DebugSession& dbg = r.dbg;
    EmuThread& emu = r.emu;
    ViewBuffer b;

    CHECK(r.load(romA), "rom A loads");
    dbg.setContentLoaded(true);
    dbg.syncCore();
    CHECK(dbg.enabled(), "the tools are enabled with the interface handed over");

    // 1. The three request kinds answer what a debugCall peek says, at the same frame.
    {
        emu.advance(3);
        const Id mid = dbg.views().set(DebugViews::kNoId, memReq(0x1000, 256));
        const Id did = dbg.views().set(DebugViews::kNoId, dramReq());
        const Id rid = dbg.views().set(DebugViews::kNoId, regsReq(VJ_DBG_TARGET_M68K));
        dbg.requestRefresh();
        const unsigned frame = emu.debugCall([&] { return dbg.core().frameNumber(); });
        ViewBuffer m, d, g;
        const bool okM = r.waitRead(mid, m), okD = r.waitRead(did, d), okG = r.waitRead(rid, g);
        CHECK(okM && okD && okG && m.ok && d.ok && g.ok, "memory, dram and regs requests equal a debugCall peek at the same frame");
        CHECK(m.frame == frame && d.frame == frame && g.frame == frame, "all three buffers carry the frame %u", frame);
        CHECK(m.bytes == r.peek(0x1000, 256), "the Memory buffer equals the peek");
        const std::vector<uint8_t> dram = r.peek(0, DebugSession::kDramSize);
        CHECK(d.bytes.size() == DebugSession::kDramSize && fnv(d.bytes) == fnv(dram) && d.bytes == dram,
              "the Dram buffer equals a peek of 2 MiB");
        vj_debug_regs want{};
        const bool gotRegs = emu.debugCall([&] { return dbg.core().getRegs(VJ_DBG_TARGET_M68K, want); });
        CHECK(gotRegs && g.regs.pc == want.pc, "regs.pc equals getRegs(M68K).pc (%06X vs %06X)", g.regs.pc, want.pc);
        dbg.views().clear(mid);
        dbg.views().clear(did);
        dbg.views().clear(rid);
    }

    // 2. A poke through the session (marshalled through debugCall) shows in the next fill.
    {
        CHECK(dbg.pokeU(0x1F0000, 4, 0xCAFEF00D) == 4, "the poke is accepted");
        const Id id = dbg.views().set(DebugViews::kNoId, memReq(0x1F0000, 4));
        const bool ok = r.waitRead(id, b);
        CHECK(ok && b.bytes.size() == 4 && b.bytes[0] == 0xCA && b.bytes[1] == 0xFE && b.bytes[2] == 0xF0 && b.bytes[3] == 0x0D,
              "a poke through the session shows in the next fill");
        dbg.views().clear(id);
    }

    // 3. A new request is readable while paused, with no frame run.
    {
        const uint64_t frames = emu.framesRun();
        const Id id = dbg.views().set(DebugViews::kNoId, memReq(0x2000, 64));
        bool ok = r.waitRead(id, b);
        CHECK(ok, "readable within 500 ms with no frame run");
        CHECK(emu.framesRun() == frames, "a new request is readable while paused, with no advance()");
        CHECK(ok && b.req.addr == 0x2000 && b.bytes == r.peek(0x2000, 64), "the first range is the peek");
        const uint64_t serial = b.serial;
        dbg.views().set(id, memReq(0x3000, 64));   // a scroll while paused
        ok = r.waitRead(id, b);
        CHECK(ok && b.req.addr == 0x3000 && b.serial == serial && b.bytes == r.peek(0x3000, 64),
              "the scrolled range is the new one, at the same serial");
        CHECK(emu.framesRun() == frames, "the scroll ran no frame either");
        dbg.views().clear(id);
    }

    // 4. After a reload the Memory window's first read is the new ROM's bytes.
    {
        const Id id = dbg.views().set(DebugViews::kNoId, memReq(0x800000, 65536));
        emu.advance(2);
        CHECK(r.waitRead(id, b) && b.bytes.size() == 65536, "the cartridge window of ROM A is read");
        const std::vector<uint8_t> romAbytes = b.bytes;
        CHECK(r.load(romB), "rom B loads");   // onCoreIface fires on the emulation thread twice: null, then the new interface
        dbg.syncCore();
        ViewBuffer now;
        CHECK(!dbg.views().read(id, now) || now.bytes != romAbytes, "never the previous game's bytes");
        const bool ok = r.waitRead(id, b);   // the epoch bump made the old buffer pending at the same serial
        CHECK(ok && b.bytes == r.peek(0x800000, 65536), "after a reload the Memory window's first read is the new ROM's bytes");
        CHECK(b.bytes != romAbytes, "the two ROMs differ in their first 64 KiB (pick different ROMs if this fails)");
        dbg.views().clear(id);
    }

    // 6. No halt is possible while the halt is gated.
    {
        dbg.setHaltAvailable(false);
        dbg.breakNow();
        emu.advance(5);
        CHECK(!dbg.attached() && !dbg.halted(), "no halt is possible while it is gated");
    }

    // 7. Hardcore closes the tools and detaches, threaded.
    {
        dbg.setHaltAvailable(true);
        CHECK(dbg.attach() && dbg.attached(), "the core is attached (so the hardcore detach has something to do)");
        dbg.setHardcore(true);
        CHECK(!dbg.enabled(), "hardcore closes the tools");
        emu.debugCall([] {});   // the detach was posted to the debug lane: this runs behind it
        CHECK(!dbg.attached(), "hardcore detaches the core on the emulation thread");
        dbg.setHardcore(false);
        CHECK(dbg.enabled(), "the tools come back with hardcore off");
        dbg.setHaltAvailable(false);
    }

    // 8. A fill posted just before the thread stops is not lost: the stop drains the debug lane, and the next
    //    wakeViews() posts again (the thread is gone, so it runs where it is called).
    {
        const Id id = dbg.views().set(DebugViews::kNoId, memReq(0x4000, 16));
        dbg.wakeViews();
        CHECK(r.emu.stop(), "the thread stops with a wake post queued");
        CHECK(r.waitRead(id, b) && b.bytes.size() == 16, "a wake post queued at stop does not wedge the next one");
    }
}

// Inline and threaded give the same Dram view after the same 60 frames.  A stopped EmuThread is not restartable: each
// call builds a fresh rig.
uint32_t dramHash(bool threaded, const std::string& rom) {
    Rig r(threaded);
    r.wire();
    CHECK(r.start(), "the %s rig starts", threaded ? "threaded" : "inline");
    CHECK(r.load(rom), "the rom loads");
    r.dbg.setContentLoaded(true);
    r.dbg.syncCore();
    const Id id = r.dbg.views().set(DebugViews::kNoId, dramReq());
    r.emu.advance(60);
    r.dbg.requestRefresh();
    ViewBuffer b;
    const bool ok = r.waitRead(id, b);
    CHECK(ok && b.ok && b.bytes.size() == DebugSession::kDramSize, "the %s Dram view arrives", threaded ? "threaded" : "inline");
    const uint32_t h = ok ? fnv(b.bytes) : 0;
    r.emu.call([&] { r.host.unloadContent(); });
    CHECK(r.emu.stop(), "the %s rig stops", threaded ? "threaded" : "inline");
    CHECK(r.host.ownerViolations() == 0, "no owner violation in the %s rig", threaded ? "threaded" : "inline");
    r.unwire();
    return h;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: vj_debug_views_threaded <rom-a.j64> <rom-b.j64>\n");
        return 2;
    }
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path tmp = fs::temp_directory_path(ec);
    if (ec) tmp = ".";
    tmp /= "vj-debug-views-threaded-" + std::to_string((long long)std::time(nullptr)) + "-" + std::to_string((long long)clock());
    fs::create_directories(tmp / "system", ec);
    fs::create_directories(tmp / "saves", ec);
    const std::string t = tmp.string() + "/";
    CoreHost& host = CoreHost::instance();
    host.setDirectories(t + "system/", t + "saves/");
    const std::string romA = argv[1], romB = argv[2];

    {
        Rig r(true);
        r.wire();
        CHECK(r.start(), "the emulation thread starts");
        cases(r, romA, romB);
        // The thread has stopped (case 8): the core belongs to this thread again, as in App's shutdown.
        r.host.unloadContent();
        CHECK(r.host.ownerViolations() == 0, "no owner violation in the shared rig");
        r.unwire();
    }

    const uint32_t inl = dramHash(false, romA);
    const uint32_t thr = dramHash(true, romA);
    CHECK(inl != 0 && inl == thr, "inline and threaded give the same Dram view (%08x vs %08x)", inl, thr);

    std::error_code ec2;
    fs::remove_all(tmp, ec2);
    std::printf("debug_views_threaded: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
