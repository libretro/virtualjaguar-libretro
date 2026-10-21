// The cheat scanner window drawn headlessly: a real ImGui context with no renderer, the mock core interface
// behind a DebugSession, and the window's state driven through scanner_test::*.  What this catches is what a
// unit test of the engine cannot: an ImGui assertion (ID / stack / table misuse) in any state of the window --
// disabled, idle, 2 million candidates, narrowed, editing a value, the Add-as-cheat popup, undone, forced
// closed -- and that the values the window writes and the cheats it adds land where they should.
#include "scanner_ui.h"

#include "cheats.h"
#include "core_host.h"
#include "debug_mock.h"
#include "debug_session.h"
#include "debug_ui_fixture.h"

#include "imgui.h"

#include <cstdio>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace fs = std::filesystem;
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

static void frames(DebugSession& s, CheatManager& cm, int n) {
    ImGuiIO& io = ImGui::GetIO();
    for (int i = 0; i < n; i++) {
        io.DeltaTime = 1.0f / 60.0f;
        ImGui::NewFrame();
        drawScanner(s, cm, CoreHost::instance());
        ImGui::Render();
    }
}

// A deferred emulation thread over the fixture's session (what a threaded App does): posts queue, drain() runs them.
struct DeferredCore {
    std::vector<std::function<void()>> q;
    explicit DeferredCore(UiFixture& f) {
        f.s.corePost = [this](std::function<void()> fn) { q.push_back(std::move(fn)); };
    }
    void drain() {
        std::vector<std::function<void()>> work = std::move(q);
        q.clear();
        for (auto& fn : work) {
            CountingIface::OnCore on;
            fn();
        }
    }
};

int main() {
    fs::path tmp = fs::temp_directory_path() / "vj-scan-ui-smoke";
    fs::remove_all(tmp);
    fs::create_directories(tmp);

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(1280, 800);
    unsigned char* px = nullptr;
    int w = 0, h = 0;
    io.Fonts->GetTexDataAsRGBA32(&px, &w, &h);

    CountingIface::reset();
    UiFixture fx;
    MockDebugIface& mock = fx.mock;
    DebugSession& session = fx.s;
    session.setContentLoaded(false);              // the fixture starts with a core and a game; the first sections want neither
    session.setIface(nullptr);
    CheatManager cm;
    cm.setStateDir(tmp.string() + "/");
    GameIdentity id;
    id.key = "crc-5ca00002";
    cm.gameLoaded(id);

    registerScannerTool(session);
    DebugTool* tool = session.findTool("scanner");
    CHECK(tool && tool->toggle && tool->isOpen);
    CHECK(!tool->isOpen());
    frames(session, cm, 2);                       // closed: draws nothing
    tool->toggle();
    CHECK(tool->isOpen() && scannerIsOpen());
    frames(session, cm, 3);                       // open, but no core interface: shows why it is off

    session.setIface(fx.counted.iface());
    session.setContentLoaded(true);
    CHECK(session.enabled());
    frames(session, cm, 3);                       // idle

    // 16-bit aligned, Unknown initial value: every even address is a candidate.
    scanner_test::setOptions(1, false, true);
    scanner_test::setMode(0, 0);
    scanner_test::scan(session, CoreHost::instance());
    CHECK(scanner_test::candidates() == MockDebugIface::kDramSize / 2);
    frames(session, cm, 3);
    CHECK(scanner_test::shownRows() == 1000);

    // Three words change; Changed keeps those.
    mock.dram()[0x100] ^= 0xFF;
    mock.dram()[0x2001] ^= 0x01;
    mock.dram()[0x5000] ^= 0x10;
    scanner_test::setMode(0, 0);                  // next list: Changed
    scanner_test::scan(session, CoreHost::instance());
    CHECK(scanner_test::candidates() == 3);
    frames(session, cm, 3);
    CHECK(scanner_test::shownRows() == 3);

    // #967 3a: the scan pass reads RAM as of now (one callCore); the live column comes from the Dram request, and idle
    // frames reach the core not at all.
    const UiFixture::Mark quiet = fx.mark();
    const unsigned reads = scanner_test::liveReads();
    frames(session, cm, 40);                                  // the 10 Hz live gate passes several times at 1/60 s a frame
    CHECK(fx.quietSince(quiet) && CountingIface::offCore() == 0);
    CHECK(scanner_test::liveReads() == reads);                // ...and the 2 MB buffer is not copied out again
    CHECK(session.views().count() == 1);                      // one request, the whole of main RAM
    int64_t live = 0;
    CHECK(scanner_test::liveValue(0, live));                  // row 0 is the word at $000100
    mock.dram()[0x101] = 0x7E;                                // RAM moves behind the window
    session.requestRefresh();                                 // the next fill carries it; the gate re-reads within 0.1 s
    frames(session, cm, 10);
    int64_t after = 0;
    CHECK(scanner_test::liveValue(0, after) && after == (int64_t)((mock.dram()[0x100] << 8) | 0x7E) && after != live);
    CHECK(scanner_test::liveReads() == reads + 1 && CountingIface::offCore() == 0);

    // Edit the first row's value (word at $000100) and commit with Enter.
    ImGui::SetWindowFocus("Cheat scanner");       // what a click on the window does: keys are routed to it
    frames(session, cm, 1);
    scanner_test::beginEdit(0, "4660");           // $1234
    frames(session, cm, 6);                       // a few frames: ImGui routes a key to the active field one frame late
    io.AddKeyEvent(ImGuiKey_Enter, true);
    frames(session, cm, 4);
    io.AddKeyEvent(ImGuiKey_Enter, false);
    frames(session, cm, 2);
    CHECK(scanner_test::message().find("Wrote") == 0);
    CHECK(mock.dram()[0x100] == 0x12 && mock.dram()[0x101] == 0x34);

    // The Add-as-cheat popup, then undo back to the 2M set, new search, close.
    scanner_test::openAdd(1, "Test cheat");
    frames(session, cm, 3);
    scanner_test::undo();
    frames(session, cm, 2);
    CHECK(scanner_test::candidates() == MockDebugIface::kDramSize / 2);
    scanner_test::newSearch();
    frames(session, cm, 2);
    CHECK(scanner_test::candidates() == 0);

    // A value search with a range, then a bad value (the message path).
    scanner_test::setOptions(0, true, false);
    scanner_test::setMode(5, 0);                  // Range
    scanner_test::setValues("-5", "5");
    scanner_test::scan(session, CoreHost::instance());
    CHECK(scanner_test::candidates() > 0);
    frames(session, cm, 2);
    scanner_test::newSearch();
    scanner_test::setValues("nope", "");
    scanner_test::scan(session, CoreHost::instance());
    CHECK(scanner_test::candidates() == 0);
    frames(session, cm, 2);

    // Hardcore switches the tools off: the window closes itself and forgets the search.
    scanner_test::setValues("", "");
    scanner_test::setMode(0, 0);
    scanner_test::scan(session, CoreHost::instance());
    CHECK(scanner_test::candidates() > 0);
    session.setHardcore(true);
    CHECK(!scannerIsOpen() && scanner_test::candidates() == 0 && session.views().count() == 0);
    frames(session, cm, 2);
    scannerSetOpen(true);
    frames(session, cm, 2);                       // open again while hardcore: the reason, no controls, no request
    CHECK(session.views().count() == 0);
    session.setHardcore(false);

    // Threaded: the emulation thread runs a post only at its next boundary.  The window waits for the fill without
    // copying anything, copies the buffer once per publish, and drops what it holds with the request.
    DeferredCore d(fx);
    const unsigned base = scanner_test::liveReads();
    scanner_test::setOptions(1, false, true);
    scanner_test::setMode(0, 0);
    scanner_test::setValues("", "");
    scanner_test::scan(session, CoreHost::instance());        // Unknown initial value, a callCore (the fixture runs it at once)
    CHECK(scanner_test::candidates() == MockDebugIface::kDramSize / 2);
    CountingIface::reset();
    frames(session, cm, 1);                                   // states the request and queues ONE fill
    int64_t v = -1;
    CHECK(d.q.size() == 1 && session.views().count() == 1);
    CHECK(scanner_test::liveValue(0, v) && v == 0 && scanner_test::liveReads() == base + 0);   // nothing to show yet
    frames(session, cm, 6);                                   // still outstanding: not queued again, nothing copied
    CHECK(d.q.size() == 1 && scanner_test::liveReads() == base + 0);
    d.drain();
    frames(session, cm, 1);
    CHECK(scanner_test::liveReads() == base + 1 && scanner_test::liveValue(0, v) && v == 1);   // word at 0 = $0001
    const UiFixture::Mark idle = fx.mark();
    frames(session, cm, 30);
    CHECK(fx.quietSince(idle) && d.q.empty() && scanner_test::liveReads() == base + 1);

    // One publish, one copy: the refresh is waited for, not re-read every frame.
    mock.dram()[1] = 0x55;
    session.requestRefresh();
    frames(session, cm, 4);
    CHECK(d.q.size() == 1 && scanner_test::liveReads() == base + 1 && scanner_test::liveValue(0, v) && v == 1);
    d.drain();
    frames(session, cm, 10);
    CHECK(scanner_test::liveReads() == base + 2 && scanner_test::liveValue(0, v) && v == 0x55);

    // Another core / content (the epoch moves, the serial does not): the buffer held is dropped, so the refill at the
    // SAME serial is adopted; until then the last values stay on screen.
    mock.dram()[1] = 0x66;
    session.views().bumpEpoch();
    frames(session, cm, 4);
    CHECK(d.q.size() == 1 && scanner_test::liveReads() == base + 2 && scanner_test::liveValue(0, v) && v == 0x55);
    d.drain();
    frames(session, cm, 10);
    CHECK(scanner_test::liveReads() == base + 3 && scanner_test::liveValue(0, v) && v == 0x66);

    // Closed with a fill still queued: the request goes behind it, never in front.
    mock.dram()[1] = 0x77;
    session.requestRefresh();
    frames(session, cm, 1);
    CHECK(d.q.size() == 1);
    scannerSetOpen(false);
    frames(session, cm, 1);
    CHECK(d.q.size() == 2 && session.views().count() == 1);   // the clear is posted behind the fill, not made here
    d.drain();
    CHECK(session.views().count() == 0);
    frames(session, cm, 5);                                   // a closed window asks for nothing
    CHECK(d.q.empty() && session.views().count() == 0);
    scannerSetOpen(true);                                     // reopened: a new request, answered from scratch
    frames(session, cm, 1);
    CHECK(d.q.size() == 1 && session.views().count() == 1);
    d.drain();
    frames(session, cm, 10);
    CHECK(scanner_test::liveReads() == base + 4 && scanner_test::liveValue(0, v) && v == 0x77);
    CHECK(CountingIface::offCore() == 0);

    ImGui::DestroyContext();
    fs::remove_all(tmp);
    std::printf("scanner ui smoke: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
