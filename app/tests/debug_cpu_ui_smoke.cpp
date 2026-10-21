// Headless smoke test for the CPU debugger window (app/src/debug/cpu_ui.cpp): real ImGui frames (no renderer: the
// font atlas is acknowledged by hand) against the mock core behind a counting interface.  Pins #967 phase 3a: once
// its registers and listing have arrived the window draws from published view buffers only -- no core call, no
// wait, no post -- and nothing it does reaches the interface off the emulation side.
#include "cpu_ui.h"
#include "debug_mock.h"
#include "debug_session.h"
#include "debug_ui_fixture.h"

#include "imgui.h"

#include <cstdio>
#include <functional>
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

static void acknowledgeTextures() {
    for (ImTextureData* t : ImGui::GetPlatformIO().Textures) {
        if (t->Status == ImTextureStatus_WantCreate) t->SetTexID((ImTextureID)1);
        if (t->Status == ImTextureStatus_WantCreate || t->Status == ImTextureStatus_WantUpdates) t->SetStatus(ImTextureStatus_OK);
        else if (t->Status == ImTextureStatus_WantDestroy) t->SetStatus(ImTextureStatus_Destroyed);
    }
}

static UiFixture* g_fx = nullptr;

static void frame(DebugSession& s) {
    ImGui::GetIO().DeltaTime = 1.0f / 60.0f;
    ImGui::NewFrame();
    s.tick(0.0, false);
    drawCpuTools(s);
    ImGui::Render();
    acknowledgeTextures();
    g_fx->fill();   // the emulation thread's share of the frame
}

static void settle(DebugSession& s, int n = 3) {
    for (int i = 0; i < n; i++) frame(s);
}

int main() {
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(1100, 760);
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    ImGui::LoadIniSettingsFromMemory("[Window][CPU debugger]\nPos=8,8\nSize=900,560\n");

    CountingIface::reset();
    UiFixture fx;
    g_fx = &fx;
    DebugSession& s = fx.s;
    registerCpuTools(s);
    CHECK(s.findTool("cpu") && s.findTool("cpu")->toggle);
    s.findTool("cpu")->toggle();
    CHECK(cpuWindowOpen());

    // 1. The first frames: the registers and the listing arrive through the view buffers.
    settle(s, 8);
    CHECK(cpu_test::haveRegs() && cpu_test::rowCount() >= 4);
    CHECK(CountingIface::offCore() == 0);

    // 2. Idle frames are free: no core call, no wait, no post.
    const UiFixture::Mark quiet = fx.mark();
    for (int i = 0; i < 30; i++) frame(s);
    CHECK(fx.quietSince(quiet) && CountingIface::offCore() == 0);

    // 3. The other tab: new requests, answered after a fill.
    showCpuAt(s, VJ_DBG_TARGET_GPU, 0xF03000);
    settle(s, 8);
    CHECK(cpu_test::haveRegs() && cpu_test::rowCount() >= 4 && CountingIface::offCore() == 0);
    const UiFixture::Mark quiet2 = fx.mark();
    for (int i = 0; i < 30; i++) frame(s);
    CHECK(fx.quietSince(quiet2));

    // 4. The threaded 3a state: the windows live, Break / Step greyed.
    s.setHaltAvailable(false);
    settle(s, 4);
    CHECK(cpu_test::haveRegs() && CountingIface::offCore() == 0);

    // 4b. Every breakpoint-adding control (margin, context menu, "Add at cursor") runs toggleBp: with the halt unavailable
    //     it adds nothing, makes no core call and says why -- not "the table is full".
    {
        const UiFixture::Mark before = fx.mark();
        cpu_test::toggleBreakpoint(s, VJ_DBG_TARGET_GPU, 0xF03000);
        CHECK(s.breakpoints().empty() && fx.quietSince(before) && CountingIface::offCore() == 0);
        CHECK(cpu_test::status() == s.haltDisabledReason() && !s.haltDisabledReason().empty());
        CHECK(cpu_test::status().find("table is full") == std::string::npos && !cpu_test::statusIsError());
        settle(s, 2);                                // the greyed controls draw (and the tooltips) without a core call
        CHECK(CountingIface::offCore() == 0);
    }

    // 5. A new refresh serial re-reads (a changed register shows up), still on the emulation side only.
    s.setHaltAvailable(true);
    cpu_test::toggleBreakpoint(s, VJ_DBG_TARGET_GPU, 0xF03000);   // back on: the same control sets one
    CHECK(s.breakpoints().size() == 1 && cpu_test::status() == "Breakpoint set");
    s.setHaltAvailable(false);
    cpu_test::toggleBreakpoint(s, VJ_DBG_TARGET_GPU, 0xF03000);   // removing never needs the halt machine
    CHECK(s.breakpoints().empty() && cpu_test::status() == "Breakpoint removed");
    s.setHaltAvailable(true);
    s.requestRefresh();
    settle(s, 3);
    CHECK(cpu_test::haveRegs() && CountingIface::offCore() == 0);

    // 6. Closing the window drops its requests: the copier has nothing to serve.
    s.findTool("cpu")->toggle();
    CHECK(!cpuWindowOpen());
    settle(s, 2);
    CHECK(s.views().count() == 0);
    s.findTool("cpu")->toggle();
    settle(s, 8);
    CHECK(cpu_test::haveRegs() && cpu_test::rowCount() >= 4 && s.views().count() == 2);

    // 7. A deferred emulation thread (posts wait in a queue): the window keeps drawing, asks once, and fills in when
    //    the queue is drained.
    s.findTool("cpu")->toggle();   // closed: nothing is remembered as answered
    settle(s, 2);
    showCpuAt(s, VJ_DBG_TARGET_M68K, 0x1000);   // opens it on the 68000 tab
    CHECK(cpuWindowOpen());
    std::vector<std::function<void()>> queue;
    auto inlinePost = s.corePost;
    s.corePost = [&](std::function<void()> fn) { queue.push_back(std::move(fn)); };
    settle(s, 5);
    CHECK(queue.size() <= 1);   // one outstanding view fill, however many frames drew
    for (auto& fn : queue) {
        CountingIface::OnCore on;
        fn();
    }
    queue.clear();
    settle(s, 3);
    s.corePost = inlinePost;
    CHECK(cpu_test::haveRegs() && cpu_test::rowCount() >= 4 && CountingIface::offCore() == 0);

    // 8. A core going away mid-session does not crash the window.
    s.setIface(nullptr);
    settle(s, 3);
    CHECK(CountingIface::offCore() == 0);

    ImGui::DestroyContext();
    std::printf("debug_cpu_ui_smoke: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
