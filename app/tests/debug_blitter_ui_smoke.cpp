// Headless smoke test for the Blitter log window (app/src/debug/blitter_ui.cpp): real ImGui frames (no renderer: the
// font atlas is acknowledged by hand) against the mock core behind a counting interface.  Pins #967 phase 3a: the
// window switches the core's log on while it is open, its list and registers arrive through published view buffers,
// idle frames make no core call, no wait and no post, and closing it switches the log off again.
#include "blitter_ui.h"
#include "debug_mock.h"
#include "debug_session.h"
#include "debug_ui_fixture.h"

#include "imgui.h"

#include <cstdio>

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

static void frame() {
    ImGui::GetIO().DeltaTime = 1.0f / 60.0f;
    ImGui::NewFrame();
    g_fx->s.tick(0.0, false);
    blitterToolDraw();
    ImGui::Render();
    acknowledgeTextures();
    g_fx->fill();   // the emulation thread's share of the frame
}

static void settle(int n) {
    for (int i = 0; i < n; i++) frame();
}

int main() {
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(1100, 760);
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    ImGui::LoadIniSettingsFromMemory("[Window][Blitter log]\nPos=8,8\nSize=900,600\n");

    CountingIface::reset();
    UiFixture fx;
    g_fx = &fx;
    blitterToolInit(fx.s);
    CHECK(fx.s.findTool("blitter") && fx.s.findTool("blitter")->toggle);
    fx.s.findTool("blitter")->toggle();
    settle(6);
    CHECK(blitter_test::logOn() && fx.mock.blitLogOn && blitter_test::launchesSeen() == 5);
    CHECK(CountingIface::offCore() == 0);
    const UiFixture::Mark quiet = fx.mark();
    for (int i = 0; i < 30; i++) frame();
    CHECK(fx.quietSince(quiet) && CountingIface::offCore() == 0);

    // A new launch shows up after the next refresh, still on the emulation side only.
    fx.mock.addBlit(0x01800001u, 0x00010002);
    fx.s.requestRefresh();
    settle(4);
    CHECK(blitter_test::launchesSeen() == 6 && CountingIface::offCore() == 0);

    // Another core (the views' epoch moves): the list starts over, from that core's log, switched on again.
    fx.s.setIface(fx.counted.iface());
    settle(6);
    CHECK(blitter_test::logOn() && fx.mock.blitLogOn && blitter_test::launchesSeen() == 6 && CountingIface::offCore() == 0);

    fx.s.findTool("blitter")->toggle();
    settle(3);                                                       // closing switches the core's log off
    CHECK(!blitter_test::logOn() && !fx.mock.blitLogOn && fx.s.views().count() == 0);

    ImGui::DestroyContext();
    std::printf("debug_blitter_ui_smoke: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
