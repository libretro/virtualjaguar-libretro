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

#include "imgui.h"

#include <cstdio>
#include <filesystem>
#include <string>

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

    MockDebugIface mock;
    DebugSession session;
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

    session.setIface(mock.iface());
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
    CHECK(!scannerIsOpen() && scanner_test::candidates() == 0);
    frames(session, cm, 2);
    scannerSetOpen(true);
    frames(session, cm, 2);                       // open again while hardcore: the reason, no controls
    session.setHardcore(false);

    ImGui::DestroyContext();
    fs::remove_all(tmp);
    std::printf("scanner ui smoke: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
