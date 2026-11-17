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
#include <cstring>
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

    // 4. More frames, still on the emulation side only.
    settle(s, 4);
    CHECK(cpu_test::haveRegs() && CountingIface::offCore() == 0);

    // 5. A breakpoint is set and removed through the same control; a new refresh serial re-reads (a changed register
    //    shows up), still on the emulation side only.
    cpu_test::toggleBreakpoint(s, VJ_DBG_TARGET_GPU, 0xF03000);
    CHECK(s.breakpoints().size() == 1 && cpu_test::status() == "Breakpoint set");
    cpu_test::toggleBreakpoint(s, VJ_DBG_TARGET_GPU, 0xF03000);
    CHECK(s.breakpoints().empty() && cpu_test::status() == "Breakpoint removed");
    s.requestRefresh();
    settle(s, 3);
    CHECK(cpu_test::haveRegs() && CountingIface::offCore() == 0);

    // 5b. Names (the session's symbols): the Go to box takes one, with or without an offset, and the watchpoint row sets
    //     the session's watchpoint by name.  Drawing the labels costs no core call.
    {
        // A two-symbol .abs served from memory (36-byte header, no text or data, 14-byte records).
        static std::vector<unsigned char> image(36 + 2 * 14, 0);
        image[0] = 0x60, image[1] = 0x1B, image[17] = 28;
        auto record = [](size_t i, const char* name, uint16_t type, uint32_t value) {
            unsigned char* r = image.data() + 36 + i * 14;
            std::strncpy(reinterpret_cast<char*>(r), name, 8);
            r[8] = (unsigned char)(type >> 8), r[9] = (unsigned char)type;
            r[10] = (unsigned char)(value >> 24), r[11] = (unsigned char)(value >> 16), r[12] = (unsigned char)(value >> 8), r[13] = (unsigned char)value;
        };
        record(0, "gpu_loop", 0x8200, 0xF03010);   // text
        record(1, "counter", 0x8100, 0x004000);    // bss
        s.symbols().reader = [](const std::string&, std::vector<unsigned char>& out) {
            out = image;
            return true;
        };
        s.symbols().setContent("/mem/game.abs");
        settle(s, 2);   // the window reads them on its next draw
        CHECK(s.symbols().symbols().size() == 2);
        CHECK(cpu_test::gotoText(s, "gpu_loop") && cpu_test::listingTop() == 0xF03010);
        CHECK(cpu_test::gotoText(s, "gpu_loop+4") && cpu_test::listingTop() == 0xF03014);
        CHECK(cpu_test::gotoText(s, "$F03000") && cpu_test::listingTop() == 0xF03000);
        CHECK(!cpu_test::gotoText(s, "no_such_label") && cpu_test::listingTop() == 0xF03000);
        settle(s, 6);   // the listing at $F03000 draws gpu_loop's label a few rows down
        CHECK(cpu_test::haveRegs() && cpu_test::rowCount() >= 4 && CountingIface::offCore() == 0);
        const UiFixture::Mark named = fx.mark();
        for (int i = 0; i < 30; i++) frame(s);
        CHECK(fx.quietSince(named));   // labels and annotations: no core call, no post

        cpu_test::setWatch(s, "counter", 1, 0);   // 2 bytes, writes
        CHECK(s.watch().set && s.watch().addr == 0x4000 && s.watch().len == 2 && s.watch().kind == VJ_DBG_WATCH_WRITE);
        CHECK(s.watch().symbol == "counter" && cpu_test::status() == "Watchpoint set" && !cpu_test::statusIsError());
        cpu_test::setWatch(s, "nowhere", 0, 0);
        CHECK(cpu_test::statusIsError() && s.watch().addr == 0x4000);   // refused: the one already set stays
        settle(s, 3);   // the row draws with the watchpoint's line
        cpu_test::toggleBreakpoint(s, VJ_DBG_TARGET_GPU, 0xF03014);
        CHECK(s.breakpoints().size() == 1 && s.breakpoints()[0].symbol == "gpu_loop" && s.breakpoints()[0].symbolOffset == 4);
        settle(s, 3);   // ...and the breakpoint list with the name it is tied to
        cpu_test::toggleBreakpoint(s, VJ_DBG_TARGET_GPU, 0xF03014);
        s.clearWatch();
        CHECK(!s.watch().set && s.breakpoints().empty() && CountingIface::offCore() == 0);
        s.symbols().setContent("");
        settle(s, 2);
    }

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
