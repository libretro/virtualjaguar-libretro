// Headless smoke test for the Memory and Palette windows (app/src/debug/memory_ui.cpp, clut_ui.cpp): real
// ImGui frames (no renderer: the font atlas is acknowledged by hand) against the mock core interface, driven
// with synthetic key events.  Catches ImGui assertions (ID clashes, unbalanced Begin/End), and proves the
// whole path from a typed hex digit to a poke -- including that a refused write never reaches the interface.
#include "debug_mock.h"
#include "debug_session.h"
#include "memory_ui.h"
#include "memory_view.h"

#include "imgui.h"

#include <cstdio>
#include <string>

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

static void frame(DebugSession& s) {
    ImGui::GetIO().DeltaTime = 1.0f / 60.0f;
    ImGui::NewFrame();
    s.tick(0.0, false);
    drawMemoryTools(s, nullptr);
    ImGui::Render();
    acknowledgeTextures();
}

static void settle(DebugSession& s, int n = 3) {
    for (int i = 0; i < n; i++) frame(s);
}

static void key(DebugSession& s, ImGuiKey k) {
    ImGui::GetIO().AddKeyEvent(k, true);
    frame(s);
    ImGui::GetIO().AddKeyEvent(k, false);
    frame(s);
}

// Select `addr` in the Memory window, put focus on it, press Enter and type `digits` then Enter.
static void typeAt(DebugSession& s, unsigned addr, const char* digits) {
    showMemoryAt(s, addr);
    settle(s);
    ImGui::SetWindowFocus("Memory");
    settle(s, 2);
    key(s, ImGuiKey_Enter);
    settle(s, 2);
    ImGui::GetIO().AddInputCharactersUTF8(digits);
    frame(s);
    key(s, ImGuiKey_Enter);
    settle(s, 2);
}

int main() {
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(1100, 760);
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    ImGui::LoadIniSettingsFromMemory("[Window][Memory]\nPos=8,8\nSize=660,560\n[Window][Palette (CLUT)]\nPos=680,8\nSize=410,740\n");

    MockDebugIface mock;
    DebugSession s;
    s.setIface(mock.iface());
    s.setContentLoaded(true);
    registerMemoryTools(s);
    CHECK(s.findTool("memory") && s.findTool("memory")->toggle);
    CHECK(s.findTool("clut") && s.findTool("clut")->toggle);

    // Tools off: the windows render their reason, not the data.
    s.setHardcore(true);
    memoryWindowOpen() = true;
    clutWindowOpen() = true;
    settle(s);
    s.closeAllTools("test");   // what hardcore / a content change does
    CHECK(!memoryWindowOpen() && !clutWindowOpen());
    s.setHardcore(false);
    s.findTool("memory")->toggle();
    s.findTool("clut")->toggle();
    CHECK(memoryWindowOpen() && clutWindowOpen());
    CHECK(s.findTool("memory")->isOpen() && s.findTool("clut")->isOpen());
    settle(s, 5);

    // 1. RAM: select $1000, two cells right, Enter, "ab", Enter -> $1002 = $AB.
    showMemoryAt(s, 0x1000);
    settle(s);
    ImGui::SetWindowFocus("Memory");
    settle(s, 2);
    key(s, ImGuiKey_RightArrow);
    key(s, ImGuiKey_RightArrow);
    key(s, ImGuiKey_Enter);
    settle(s, 2);
    io.AddInputCharactersUTF8("ab");
    frame(s);
    key(s, ImGuiKey_Enter);
    settle(s, 2);
    CHECK(mock.dram()[0x1002] == 0xAB);
    CHECK(mock.dram()[0x1001] == 0x01 && mock.dram()[0x1003] == 0x03);

    // 2. ROM: the edit does not even start, nothing reaches the interface.
    unsigned acc = mock.pokesAccepted, ref = mock.pokesRefused;
    typeAt(s, 0x800000, "ff");
    CHECK(mock.cart()[0] == 0xC0);
    CHECK(mock.pokesAccepted == acc && mock.pokesRefused == ref);

    // 3. A register without "Allow register writes": refused in the app.
    CHECK(!s.allowRegisterWrites);
    typeAt(s, 0xF00010, "ff");
    CHECK(mock.regwin()[0x10] == 0);
    CHECK(mock.pokesAccepted == acc && mock.pokesRefused == ref);

    // 4. ...and with it on, the write lands.
    s.allowRegisterWrites = true;
    typeAt(s, 0xF00010, "ff");
    CHECK(mock.regwin()[0x10] == 0xFF);

    // 5. B_CMD stays refused even with register writes on, before the interface is asked.
    acc = mock.pokesAccepted;
    ref = mock.pokesRefused;
    typeAt(s, 0xF02238, "ff");
    CHECK(mock.regwin()[0x2238] == 0);
    CHECK(mock.pokesAccepted == acc && mock.pokesRefused == ref);

    // 6. The CLUT window renders with a selection (no input needed for the layout to run).
    showMemoryAt(s, memview::clutAddrA(5));
    settle(s, 5);

    // 7. A core going away mid-session does not crash the windows.
    s.setIface(nullptr);
    settle(s, 3);
    CHECK(!memoryWindowOpen() && !clutWindowOpen());

    ImGui::DestroyContext();
    std::printf("debug_memory_ui_smoke: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
