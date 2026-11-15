// The developer loop around the debugger (docs/app-debug-tools.md, "Homebrew workflow"), against the mock core and
// synthesized symbol tables -- no window, no core, no disk beyond one temp file:
//   SymbolStore   where the names come from, name <-> address, the listing's annotation
//   DebugSession  the watchpoint, breakpoints that remember their symbol and follow it across a rebuild (carry / rebind /
//                 armOnCore / adopt), the boot halt the app owns, and the windows that stay open across a reload
//   FileWatch     reload-on-rebuild: one report per rewrite, only once the file has stopped changing
//   dev_boot      the --break / --watch command line
#include "debug_mock.h"
#include "debug_session.h"
#include "dev_boot.h"
#include "file_watch.h"
#include "symbol_store.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

using namespace vj;

static int g_fail = 0, g_checks = 0;
#define CHECK(cond)                                                                \
    do {                                                                           \
        g_checks++;                                                                \
        if (!(cond)) {                                                             \
            g_fail++;                                                              \
            std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);   \
        }                                                                          \
    } while (0)

// ---- a synthesized .abs: the 36-byte header, no text or data, then 14-byte records (symbol_table.cpp, parseAbs) --------
struct Sym {
    const char* name;
    uint32_t value;
    uint16_t type;   // 0x8000 defined | 0x0200 text | 0x0400 data | 0x0100 bss
};
static const uint16_t kText = 0x8200, kData = 0x8400, kBss = 0x8100, kAbs = 0x8000;

static std::vector<unsigned char> absImage(const std::vector<Sym>& syms) {
    std::vector<unsigned char> f(36 + syms.size() * 14, 0);
    f[0] = 0x60;
    f[1] = 0x1B;
    const uint32_t bytes = (uint32_t)syms.size() * 14;
    f[14] = (unsigned char)(bytes >> 24), f[15] = (unsigned char)(bytes >> 16), f[16] = (unsigned char)(bytes >> 8), f[17] = (unsigned char)bytes;
    for (size_t i = 0; i < syms.size(); i++) {
        unsigned char* r = f.data() + 36 + i * 14;
        std::strncpy(reinterpret_cast<char*>(r), syms[i].name, 8);
        r[8] = (unsigned char)(syms[i].type >> 8), r[9] = (unsigned char)syms[i].type;
        r[10] = (unsigned char)(syms[i].value >> 24), r[11] = (unsigned char)(syms[i].value >> 16);
        r[12] = (unsigned char)(syms[i].value >> 8), r[13] = (unsigned char)syms[i].value;
    }
    return f;
}

// An in-memory "disk" for the store's reader.
struct Disk {
    std::map<std::string, std::vector<unsigned char>> files;
    int reads = 0;
    SymbolStore::Reader reader() {
        return [this](const std::string& path, std::vector<unsigned char>& out) {
            reads++;
            auto it = files.find(path);
            if (it == files.end()) return false;
            out = it->second;
            return true;
        };
    }
};

static const std::vector<Sym> kBuild1 = {
    {"_start", 0x802000, kText}, {"main", 0x802010, kText},   {"draw", 0x802100, kText},  {"alias", 0x802100, kText},
    {"score", 0x004000, kBss},   {"lives", 0x004004, kBss},   {"gpu_go", 0xF03000, kText}, {"SIZE", 0x000008, kAbs},
    {"G_CTRL", 0xF02114, kAbs},  {"ABCD", 0x802200, kText},   {"Twin", 0x802300, kText},  {"twin", 0x802310, kText},
};
// The same program rebuilt: main grew, so everything after it moved; `draw` is gone.
static const std::vector<Sym> kBuild2 = {
    {"_start", 0x802000, kText}, {"main", 0x802010, kText}, {"alias", 0x802140, kText}, {"score", 0x004010, kBss},
    {"lives", 0x004014, kBss},   {"gpu_go", 0xF03000, kText},
};

static void testSymbolStore() {
    Disk disk;
    disk.files["/g/game.abs"] = absImage(kBuild1);
    disk.files["/g/cart.cof"] = absImage(kBuild2);       // (the parser goes by the magic, not the extension)
    disk.files["/g/other.abs"] = absImage({{"only", 0x5000, kData}});
    disk.files["/g/readme.txt"] = {'h', 'i', '!', '\n'};

    SymbolStore st;
    st.reader = disk.reader();
    CHECK(st.stale() && st.empty());
    CHECK(!st.ensure());                                 // no content: nothing read, and it says so
    CHECK(disk.reads == 0 && st.status().find("No game") != std::string::npos);

    // The content's own table; read once.
    st.setContent("/g/game.abs");
    const uint64_t gen0 = st.generation();
    CHECK(st.ensure() && st.symbols().size() == kBuild1.size());
    CHECK(st.sourcePath() == "/g/game.abs" && !st.statusIsError());
    CHECK(st.generation() == gen0 + 1);
    const int reads = disk.reads;
    CHECK(st.ensure() && disk.reads == reads);           // current: no second read
    st.setContent("/g/game.abs");
    CHECK(!st.stale());                                  // the same path is not a change
    st.invalidate();                                     // a rebuild: read again
    CHECK(st.ensure() && disk.reads == reads + 1 && st.generation() == gen0 + 2);

    // name -> address
    uint32_t v = 0;
    CHECK(st.find("main", v) && v == 0x802010);
    CHECK(st.find("MAIN", v) && v == 0x802010);          // the only spelling: case does not matter
    CHECK(!st.find("TWIN", v));                          // two symbols differ only by case: ambiguous
    CHECK(st.find("twin", v) && v == 0x802310);          // ...an exact spelling still finds its own
    CHECK(!st.find("nope", v) && !st.find("", v));
    CHECK(st.resolve("main", v) && v == 0x802010);
    CHECK(st.resolve("  main+$12 ", v) && v == 0x802022);
    CHECK(st.resolve("main+12", v) && v == 0x802022);    // the offset is hex with or without the $
    CHECK(st.resolve("$802000", v) && v == 0x802000);
    CHECK(st.resolve("0x802000", v) && v == 0x802000);
    CHECK(st.resolve("802000", v) && v == 0x802000);
    CHECK(st.resolve("ABCD", v) && v == 0x802200);       // also valid hex: the exact symbol wins
    CHECK(st.resolve("$ABCD", v) && v == 0xABCD);        // ...and a prefix says "number"
    CHECK(st.resolve("abcd", v) && v == 0xABCD);         // not that symbol's spelling: hex
    CHECK(st.resolve("$1F03000", v) && v == 0xF03000);   // masked to the 24-bit bus
    CHECK(!st.resolve("nope", v) && !st.resolve("", v) && !st.resolve("main+zz", v) && !st.resolve("$", v));

    // address -> name
    CHECK(st.at(0x802010) && st.at(0x802010)->name == "main");
    CHECK(st.at(0x802100) && st.at(0x802100)->name == "alias");   // two names at one address: the first by name
    CHECK(!st.at(0x802012));
    CHECK(!st.at(0x000008));                             // `SIZE equ 8` does not name the bus-error vector
    CHECK(st.at(0xF02114) && st.at(0xF02114)->name == "G_CTRL");   // a register equate is a place
    CHECK(st.describe(0x802010) == "main");
    CHECK(st.describe(0x802022) == "main+$12");
    CHECK(st.describe(0x004005) == "lives+$1");
    CHECK(st.describe(0x900000).empty());                // nothing within reach
    CHECK(st.describe(0xF02114) == "G_CTRL");            // exact equate
    CHECK(st.describe(0xF02118).empty());                // ...but an equate is never the base of an offset
    std::string name;
    uint32_t off = 0;
    CHECK(st.nearest(0x802100, 0x400, name, off) && name == "alias" && off == 0);
    CHECK(!st.nearest(0x802010 + 0x401 + 0x4000, 0x400, name, off));

    // the listing's annotation
    CHECK(st.annotate("jsr     $802100") == "jsr     $802100  ; alias");
    CHECK(st.annotate("move.w  #$1234, D0") == "move.w  #$1234, D0");                      // a short immediate is left alone
    CHECK(st.annotate("move.l  $00F02114, $4000.w") == "move.l  $00F02114, $4000.w  ; G_CTRL");
    CHECK(st.annotate("move.w  $004000, $004004") == "move.w  $004000, $004004  ; score, lives");
    CHECK(st.annotate("jmp     $802012") == "jmp     $802012");                             // not a label
    CHECK(st.annotate("nop") == "nop");

    // A cartridge image with no table of its own: the .cof of the same name beside it answers.
    st.setContent("/g/cart.j64");
    CHECK(st.ensure() && st.sourcePath() == "/g/cart.cof" && st.symbols().size() == kBuild2.size());
    // Content that carries none and has nothing beside it: empty, with advice, not an error.
    st.setContent("/g/readme.txt");
    CHECK(!st.ensure() && st.status().find("carries no symbols") != std::string::npos && !st.statusIsError());
    CHECK(st.annotate("jsr     $802100") == "jsr     $802100");

    // A picked file wins until the content changes; a pinned one (--symbols) survives that.
    st.setContent("/g/game.abs");
    st.setPicked("/g/other.abs");
    CHECK(st.ensure() && st.symbols().size() == 1 && st.sourcePath() == "/g/other.abs");
    st.setContent("/g/cart.j64");
    CHECK(st.ensure() && st.sourcePath() == "/g/cart.cof");
    st.setPicked("/g/other.abs", true);
    st.setContent("/g/game.abs");
    CHECK(st.ensure() && st.sourcePath() == "/g/other.abs");
    st.dropPicked();                                     // pinned: a forced close does not drop it
    CHECK(st.ensure() && st.sourcePath() == "/g/other.abs");
    st.setPicked("/g/missing.abs");
    CHECK(!st.ensure() && st.statusIsError());
    st.dropPicked();
    CHECK(st.ensure() && st.sourcePath() == "/g/game.abs");

    CHECK(symbolPathWithExtension("/a/b.j64", ".cof") == "/a/b.cof");
    CHECK(symbolPathWithExtension("/a.b/c", ".cof") == "/a.b/c.cof");
}

static DebugSession& prime(DebugSession& s, MockDebugIface& m, Disk& disk, const char* content) {
    s.symbols().reader = disk.reader();
    s.symbols().setContent(content);
    s.setIface(m.iface());
    s.setContentLoaded(true);
    return s;
}

static void testWatchpoint() {
    Disk disk;
    disk.files["/g/game.abs"] = absImage(kBuild1);
    DebugSession s;
    MockDebugIface m;
    prime(s, m, disk, "/g/game.abs");

    CHECK(s.watchSupported() && !s.watch().set);
    CHECK(!s.setWatch(0x4000, 2, 0));                              // no kind
    CHECK(!s.setWatch(0x4000, 0, VJ_DBG_WATCH_WRITE));             // no length
    CHECK(!s.setWatch(0xFFFFFF, 2, VJ_DBG_WATCH_WRITE));           // runs off the bus
    CHECK(!m.attached());                                          // a refused request does not even attach

    // Setting attaches (the core needs a host before it can halt) and remembers the symbol.
    CHECK(s.setWatch(0x4000, 2, VJ_DBG_WATCH_WRITE));
    CHECK(s.attached() && m.watchArmed && m.watchAddr == 0x4000 && m.watchLen == 2 && m.watchKind == VJ_DBG_WATCH_WRITE);
    CHECK(s.watch().set && s.watch().symbol == "score" && s.watch().symbolOffset == 0);
    // One slot: a second request replaces the first.
    CHECK(s.setWatch(0x4006, 4, VJ_DBG_WATCH_WRITE | VJ_DBG_WATCH_READ));
    CHECK(m.watchAddr == 0x4006 && m.watchLen == 4 && s.watch().symbol == "lives" && s.watch().symbolOffset == 2);
    CHECK(DebugSession::boundName(s.watch().symbol, s.watch().symbolOffset) == "lives+$2");

    // A watchpoint halt is ours; its "pc" is the watched address.
    int closed = 0;
    s.onHaltUi.push_back([&](int, unsigned pc, int reason) {
        CHECK(reason == VJ_DBG_STOP_WATCHPOINT && pc == 0x4006);
        s.resume();
    });
    s.core().runUiFrame = [&] {
        s.syncHalt();
        closed++;
    };
    CHECK(m.simulateHalt(VJ_DBG_TARGET_M68K, 0x4006, VJ_DBG_STOP_WATCHPOINT) == VJ_DBG_RESUME_CONTINUE);
    s.syncHalt();
    CHECK(closed >= 1 && !s.haltByGdb());
    s.onHaltUi.clear();

    // Without one of ours, the same halt is a gdb client's (it shares the slot).
    s.clearWatch();
    CHECK(!s.watch().set && !m.watchArmed && m.watchClears == 1);
    s.onHaltUi.push_back([&](int, unsigned, int) { s.resume(); });
    m.simulateHalt(VJ_DBG_TARGET_M68K, 0x5000, VJ_DBG_STOP_WATCHPOINT);
    s.syncHalt();
    CHECK(s.haltByGdb());
    s.onHaltUi.clear();

    // A gdb client holding the slot: refused, and the session keeps no phantom.
    m.watchHeldByGdb = true;
    CHECK(!s.setWatch(0x4000, 2, VJ_DBG_WATCH_WRITE) && !s.watch().set);
    m.watchHeldByGdb = false;

    // Detach clears it with the breakpoints; a later attach has nothing stale to replay.
    CHECK(s.setWatch(0x4000, 1, VJ_DBG_WATCH_READ) && s.addBreakpoint(VJ_DBG_TARGET_M68K, 0x802010));
    s.detach();
    CHECK(!s.watch().set && !m.watchArmed && s.breakpoints().empty());
    const int sets = m.watchSets;
    CHECK(s.attach() && m.watchSets == sets && !m.watchArmed);

    // "Remove all" breakpoints leaves the watchpoint alone.
    CHECK(s.setWatch(0x4000, 1, VJ_DBG_WATCH_READ) && s.addBreakpoint(VJ_DBG_TARGET_M68K, 0x802010));
    s.clearBreakpoints();
    CHECK(s.watch().set && m.watchArmed);

    // A core swap drops it (it is per game), like the list.
    MockDebugIface m2;
    s.setIface(m2.iface());
    CHECK(!s.watch().set);
}

static void testBindAndCarry() {
    Disk disk;
    disk.files["/g/game.abs"] = absImage(kBuild1);
    DebugSession s;
    MockDebugIface m;
    prime(s, m, disk, "/g/game.abs");

    // A breakpoint remembers the label it is at, or just past; one far from any label remembers nothing.
    CHECK(s.addBreakpoint(VJ_DBG_TARGET_M68K, 0x802010));          // main
    CHECK(s.addBreakpoint(VJ_DBG_TARGET_M68K, 0x802104));          // alias+4 (two names there: the listing's)
    CHECK(s.addBreakpoint(VJ_DBG_TARGET_GPU, 0xF03000));           // gpu_go
    CHECK(s.addBreakpoint(VJ_DBG_TARGET_M68K, 0x900000));          // nowhere
    CHECK(s.addBreakpoint(VJ_DBG_TARGET_M68K, 0x802108));          // alias+8
    s.setBreakpointEnabled(VJ_DBG_TARGET_GPU, 0xF03000, false);
    CHECK(s.setWatch(0x4004, 2, VJ_DBG_WATCH_WRITE));              // lives
    const std::vector<DebugBreakpoint>& b = s.breakpoints();
    CHECK(b[0].symbol == "main" && b[0].symbolOffset == 0);
    CHECK(b[1].symbol == "alias" && b[1].symbolOffset == 4);
    CHECK(b[3].symbol.empty());
    CHECK(DebugSession::boundName(b[1].symbol, b[1].symbolOffset) == "alias+$4" && DebugSession::boundName("", 4).empty());

    // ---- the rebuild: the list is carried, moved to the new build's symbols, armed in the NEW core before its first
    //      frame, and adopted by the session.
    DebugCarry c = s.carry();
    CHECK(c.bps.size() == 5 && c.watch.set && !c.empty());
    disk.files["/g/game.abs"] = absImage(kBuild2);
    s.symbols().invalidate();
    CHECK(s.symbols().ensure());
    const int moved = s.rebind(c);
    CHECK(moved == 3);                                             // alias+4, alias+8 and the watchpoint; main stayed
    CHECK(c.bps[0].addr == 0x802010);
    CHECK(c.bps[1].addr == 0x802144 && c.bps[4].addr == 0x802148);
    CHECK(c.bps[2].addr == 0xF03000 && !c.bps[2].enabled);         // the disabled one stays disabled
    CHECK(c.bps[3].addr == 0x900000);                              // no symbol: where it was
    CHECK(c.watch.addr == 0x4014 && c.watch.len == 2);

    MockDebugIface m2;                                             // the reloaded program's core
    s.setKeepToolsOnNextCore(true);
    std::vector<std::string> closes;
    s.onForcedClose.push_back([&](const char* why) { closes.push_back(why); });
    s.onCoreIface(m2.iface());                                     // (emulation thread: the load)
    CHECK(s.armOnCore(c, true));                                   // ...and still inside it, before the first frame
    CHECK(m2.attached() && m2.breakpoints.size() == 4);            // the enabled four
    CHECK(m2.watchArmed && m2.watchAddr == 0x4014);
    CHECK(m2.haltNexts.size() == 1 && m2.haltNexts[0] == VJ_DBG_TARGET_M68K);   // break at start
    s.adopt(c, true);                                              // (UI thread, after the load)
    CHECK(closes.size() == 1 && closes[0] == "reload");            // the windows that survive a swap stay open
    CHECK(s.breakpoints().size() == 5 && s.attached() && s.watch().set && s.watch().addr == 0x4014);
    CHECK(s.hasBreakpoint(VJ_DBG_TARGET_M68K, 0x802144) && !s.hasBreakpoint(VJ_DBG_TARGET_M68K, 0x802104));
    s.syncCore();
    CHECK(s.breakpoints().size() == 5);                            // a later sync does not drop what was adopted

    // The boot halt is the app's own, not gdb's -- and the claim is spent by it.
    bool ours = false, saw = false;
    unsigned at = 0;
    s.onHaltUi.push_back([&](int target, unsigned pc, int reason) {
        saw = true;
        ours = !s.haltByGdb();
        at = pc;
        CHECK(target == VJ_DBG_TARGET_M68K && reason == VJ_DBG_STOP_USER);
        s.resume();
    });
    s.core().runUiFrame = [&] { s.syncHalt(); };
    m2.simulateHalt(VJ_DBG_TARGET_M68K, 0x802000, VJ_DBG_STOP_USER);
    s.syncHalt();
    CHECK(saw && ours && at == 0x802000);
    ours = true;
    m2.simulateHalt(VJ_DBG_TARGET_M68K, 0x802002, VJ_DBG_STOP_USER);   // nobody asked for this one
    s.syncHalt();
    CHECK(!ours && at == 0x802002);
    s.onHaltUi.clear();

    // The carried entries behave like any other: enable the disabled one, remove one.
    s.setBreakpointEnabled(VJ_DBG_TARGET_GPU, 0xF03000, true);
    CHECK(m2.breakpoints.size() == 5);
    s.removeBreakpoint(VJ_DBG_TARGET_M68K, 0x802010);
    CHECK(m2.breakpoints.size() == 4 && s.breakpoints().size() == 4);

    // A different game: the next swap says "no core" again (the flag was spent), and the list goes.
    MockDebugIface m3;
    closes.clear();
    s.setIface(m3.iface());
    CHECK(closes.size() == 1 && closes[0] == "no core" && s.breakpoints().empty() && !s.watch().set);

    // Two entries that land on one address collapse; a symbol that is gone keeps its address.
    DebugCarry d;
    DebugBreakpoint x;
    x.addr = 0x802020, x.symbol = "main", x.symbolOffset = 0;
    d.bps.push_back(x);
    x.addr = 0x802030;
    d.bps.push_back(x);
    x.addr = 0x802100, x.symbol = "draw";                          // not in build 2
    d.bps.push_back(x);
    CHECK(s.rebind(d) == 2 && d.bps.size() == 2 && d.bps[0].addr == 0x802010 && d.bps[1].addr == 0x802100);

    // A core that refuses the host: nothing armed; the session keeps the list for the next attach().
    MockDebugIface m4;
    m4.refuseAttach = true;
    s.onCoreIface(m4.iface());
    CHECK(!s.armOnCore(c, true) && m4.breakpoints.empty() && m4.haltNexts.empty());
    s.adopt(c, false);
    s.setContentLoaded(true);
    CHECK(!s.attached() && s.breakpoints().size() == 5);
    m4.refuseAttach = false;
    CHECK(s.attach() && m4.breakpoints.size() == 4 && m4.watchArmed);   // the table is rebuilt from the list

    // A Reset's boot halt: attaches if it must, and claims the halt.
    MockDebugIface m5;
    DebugSession r;
    r.setIface(m5.iface());
    r.setContentLoaded(true);
    r.armBootHaltOnCore();
    CHECK(m5.attached() && m5.haltNexts.size() == 1);
    CHECK(r.core().takeOwnedHalt() && !r.core().takeOwnedHalt());
}

// A release for a content command (Reset while halted) disarms the watchpoint with the breakpoints and puts it back.
static void testReleaseKeepsWatch() {
    DebugSession s;
    MockDebugIface m;
    s.setIface(m.iface());
    s.setContentLoaded(true);
    CHECK(s.setWatch(0x4000, 2, VJ_DBG_WATCH_WRITE) && s.addBreakpoint(VJ_DBG_TARGET_M68K, 0x4000));
    bool released = false;
    s.core().runUiFrame = [&] {
        s.syncHalt();
        if (!released) {
            released = true;
            s.releaseHalt();                 // inline: the post runs now, inside the pump
            CHECK(!m.watchArmed && m.breakpoints.empty());   // disarmed so the frame can end
        }
    };
    CHECK(m.simulateHalt(VJ_DBG_TARGET_M68K, 0x4000, VJ_DBG_STOP_WATCHPOINT) == VJ_DBG_RESUME_CONTINUE);
    s.syncHalt();
    CHECK(released && s.watch().set && s.breakpoints().size() == 1);   // the session kept both
    s.rearmBreakpoints();
    CHECK(m.watchArmed && m.watchAddr == 0x4000 && m.breakpoints.size() == 1);
}

static void testFileWatch() {
    FileWatch w;
    FileStamp a;
    a.exists = true, a.size = 100, a.mtime = 1;
    CHECK(!w.active() && !w.poll(a));                    // nothing watched
    w.watch("/g/game.cof", a);
    CHECK(w.active() && w.path() == "/g/game.cof");
    for (int i = 0; i < 5; i++) CHECK(!w.poll(a));       // untouched
    CHECK(!w.settling());

    // A rebuild: deleted, written in pieces, then still.  One report, after it has held still.
    FileStamp gone;
    CHECK(!w.poll(gone) && w.settling());
    CHECK(!w.poll(gone) && !w.poll(gone) && !w.poll(gone));   // a missing file never loads, however long
    FileStamp part = a;
    part.size = 40, part.mtime = 2;
    CHECK(!w.poll(part));
    FileStamp full = a;
    full.size = 120, full.mtime = 3;
    CHECK(!w.poll(full));                                // changed again: the count restarts
    CHECK(!w.poll(full));                                // still for one poll
    CHECK(w.poll(full));                                 // ...and for kSettlePolls: load it
    CHECK(!w.settling());
    for (int i = 0; i < 5; i++) CHECK(!w.poll(full));    // once per rewrite

    // Touched without changing (same size, new time) is a change; an empty file is not loadable.
    FileStamp touched = full;
    touched.mtime = 4;
    CHECK(!w.poll(touched) && !w.poll(touched) && w.poll(touched));
    FileStamp empty = touched;
    empty.size = 0, empty.mtime = 5;
    CHECK(!w.poll(empty) && !w.poll(empty) && !w.poll(empty) && !w.poll(empty));
    // Changed and put back before it settled: nothing to do.
    w.watch("/g/game.cof", a);
    CHECK(!w.poll(part) && !w.poll(a) && !w.poll(a) && !w.poll(a) && !w.settling());

    w.clear();
    CHECK(!w.active() && !w.poll(full));

    // stampFile on a real file: there, then different after a rewrite, then gone.
    std::error_code ec;
    const std::filesystem::path p = std::filesystem::temp_directory_path(ec) / "vj_debug_dev_test_stamp.bin";
    const std::string path = p.u8string();
    {
        std::ofstream f(p, std::ios::binary);
        f << "one";
    }
    const FileStamp s1 = stampFile(path);
    CHECK(s1.exists && s1.size == 3);
    {
        std::ofstream f(p, std::ios::binary);
        f << "three";
    }
    const FileStamp s2 = stampFile(path);
    CHECK(s2.exists && s2.size == 5 && s2 != s1);
    std::filesystem::remove(p, ec);
    CHECK(!stampFile(path).exists && !stampFile("").exists);
}

static void testCommandLine() {
    BreakSpec b;
    std::string err;
    CHECK(parseBreakSpec("main", b, err) && b.target == -1 && b.where == "main");
    CHECK(parseBreakSpec("$802000", b, err) && b.target == -1 && b.where == "$802000");
    CHECK(parseBreakSpec("gpu:main+$12", b, err) && b.target == VJ_DBG_TARGET_GPU && b.where == "main+$12");
    CHECK(parseBreakSpec("DSP:F1B000", b, err) && b.target == VJ_DBG_TARGET_DSP && b.where == "F1B000");
    CHECK(parseBreakSpec("68k:_start", b, err) && b.target == VJ_DBG_TARGET_M68K);
    CHECK(!parseBreakSpec("z80:main", b, err) && err.find("processor") != std::string::npos);
    CHECK(!parseBreakSpec("", b, err) && !parseBreakSpec("gpu:", b, err));

    WatchSpec w;
    CHECK(parseWatchSpec("score", w, err) && w.where == "score" && w.len == 1 && w.kind == VJ_DBG_WATCH_WRITE);
    CHECK(parseWatchSpec("score:2", w, err) && w.len == 2 && w.kind == VJ_DBG_WATCH_WRITE);
    CHECK(parseWatchSpec("$4000:4:rw", w, err) && w.where == "$4000" && w.len == 4 && w.kind == (VJ_DBG_WATCH_WRITE | VJ_DBG_WATCH_READ));
    CHECK(parseWatchSpec("score:r", w, err) && w.len == 1 && w.kind == VJ_DBG_WATCH_READ);
    CHECK(parseWatchSpec("score:W:16", w, err) && w.len == 16 && w.kind == VJ_DBG_WATCH_WRITE);   // either order
    CHECK(!parseWatchSpec("", w, err) && !parseWatchSpec(":2", w, err));
    CHECK(!parseWatchSpec("score:0", w, err) && !parseWatchSpec("score:x", w, err) && !parseWatchSpec("score:2:3", w, err));
    CHECK(!parseWatchSpec("score:r:w", w, err) && !parseWatchSpec("score:2:w:r", w, err));
}

int main() {
    testSymbolStore();
    testWatchpoint();
    testBindAndCarry();
    testReleaseKeepsWatch();
    testFileWatch();
    testCommandLine();
    std::printf("debug_dev_unit: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
