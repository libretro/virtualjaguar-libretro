// The Audio scope with the emulation thread on (#967 phase 3a).  The window had no gate of its own: it was off
// because DebugSession::enabled() was, and a threaded run switched every tool off.  Now the tools are live (only Break /
// Step / breakpoints stay greyed), and the scope must open, tap the audio callback while another thread delivers
// blocks, and draw them, without a single call into the core.
#include "core_host.h"
#include "debug_session.h"
#include "debug_ui_fixture.h"
#include "scope_ui.h"

#include "imgui.h"

#include <atomic>
#include <cstdio>
#include <thread>

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

static void frame() {
    ImGui::GetIO().DeltaTime = 1.0f / 60.0f;
    ImGui::NewFrame();
    scopeToolDraw();
    ImGui::Render();
    acknowledgeTextures();
}

int main() {
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(1100, 760);
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    ImGui::LoadIniSettingsFromMemory("[Window][Audio scope]\nPos=8,8\nSize=600,400\n");

    CountingIface::reset();
    UiFixture fx;
    scopeToolInit(fx.s, CoreHost::instance());
    DebugTool* t = fx.s.findTool("scope");
    fx.s.setHaltAvailable(false);                          // the threaded 3a state
    CHECK(fx.s.enabled() && t && t->toggle && fx.s.toolAvailable(*t));
    const UiFixture::Mark m = fx.mark();
    t->toggle();                                           // open: the tap now fills the ring
    CHECK(t->isOpen());
    std::atomic<bool> stop{false};
    std::thread audio([&] {                                // what the emulation thread does through host.onAudio
        int16_t block[128 * 2] = {};
        for (int i = 0; i < 200 && !stop; i++) {
            block[0] = (int16_t)i;
            CoreHost::instance().onAudio(block, 128);
        }
    });
    for (int i = 0; i < 60; i++) frame();                  // the window draws while blocks arrive
    audio.join();
    frame();
    CHECK(scope_test::framesSeen() >= 128 * 200);
    CHECK(CountingIface::offCore() == 0);
    CHECK(fx.quietSince(m));                               // the scope never calls, waits on or posts to the core

    // Closed, the tap stops: later blocks add nothing.
    t->toggle();
    CHECK(!t->isOpen());
    const size_t before = scope_test::framesSeen();
    int16_t block[128 * 2] = {};
    CoreHost::instance().onAudio(block, 128);
    frame();
    CHECK(scope_test::framesSeen() == before);

    ImGui::DestroyContext();
    std::printf("scope gate: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
