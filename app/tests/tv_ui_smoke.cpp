// TV mode (#991): the in-game menu drawn headlessly -- a real ImGui context with no renderer, the Shell over a fake
// tv::Host that records what the menu asked of the app.  What this catches that the Shell's own logic tests cannot:
// an ImGui error (ID / stack / window misuse) on any page of the menu, the text it really draws (the netplay banner,
// the Load rows that must not exist under hardcore), and that the menu leaves the pointer and the keyboard to the app
// (no ImGui window while it is closed; no keyboard capture while it is open: F4 and the hold edges are gated on it).
#include "tv/tv_shell.h"
#include "tv/tv_widgets.h"

#include "imgui.h"
#include "imgui_internal.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <string>
#include <vector>

using namespace vj::tv;

static int g_fail = 0, g_checks = 0, g_imguiErrors = 0;
#define CHECK(cond)                                                                \
    do {                                                                           \
        g_checks++;                                                                \
        if (!(cond)) {                                                             \
            g_fail++;                                                              \
            std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);   \
        }                                                                          \
    } while (0)

// What the menu asked of the app, and the app state it reads.
struct FakeHost : Host {
    bool loaded = true, paused = false, canLoad = true, netplay = false;
    std::string refusePause;   // non-empty: menuPause() refuses with this reason (hardcore rationing)
    bool pausedBeforeMenu = false, heldPause = false;
    double clock = 0;
    int savedSlot = -1, loadedSlot = -1, resets = 0, closed = 0, resumed = 0, guardArmed = 0, holdsCleared = 0, controls = 0;
    std::string lastToast;

    double now() const override { return clock; }
    bool gameLoaded() const override { return loaded; }
    std::string gameTitle() const override { return "Yet Another Retro Cart"; }
    bool menuPause(std::string& why) override {
        if (!refusePause.empty()) {
            why = refusePause;
            return false;
        }
        if (netplay) return true;   // the link keeps running
        pausedBeforeMenu = paused;
        paused = true;
        heldPause = true;
        return true;
    }
    void menuResume() override {
        resumed++;
        if (heldPause) paused = pausedBeforeMenu;
        heldPause = false;
    }
    bool netplayActive() const override { return netplay; }
    bool canLoadState() const override { return canLoad; }
    std::vector<SlotInfo> slots() override {
        std::vector<SlotInfo> v;
        for (int i = 0; i < 10; i++) v.push_back({i, i == 2 || i == 5, (i == 2 || i == 5) ? 1760000000000000000LL : 0, 0, 0, 0});
        return v;
    }
    bool saveState(int s) override { savedSlot = s; return true; }
    bool loadState(int s) override { loadedSlot = s; return true; }
    void reset() override { resets++; }
    void closeGame() override { closed++; loaded = false; }
    void toggleControlsOverlay() override { controls++; }
    void armReleaseGuard() override { guardArmed++; }
    void clearHolds() override { holdsCleared++; }
    void toast(const std::string& t) override { lastToast = t; }
    std::string glyph(Intent i) const override {
        switch (i) {
        case Intent::Accept: return "A";
        case Intent::Back: return "B";
        case Intent::Up:
        case Intent::Down: return "D-pad";
        default: return "";
        }
    }
};

static void onImGuiError(ImGuiContext*, void*, const char* msg) {
    g_imguiErrors++;
    std::fprintf(stderr, "imgui error: %s\n", msg);
}

static std::vector<std::string> g_texts;   // every string the TV widgets drew in the last frames()
static uint64_t g_ms = 1000;

static void frames(FakeHost& host, Shell& shell, int n) {
    ImGuiIO& io = ImGui::GetIO();
    for (int i = 0; i < n; i++) {
        io.DeltaTime = 1.0f / 60.0f;
        host.clock += 1.0 / 60.0;
        g_ms += 16;
        ImGui::NewFrame();
        shell.frame(g_ms);
        shell.draw(Layer::Base, ImVec2(0, 0), io.DisplaySize);   // phase 1: nothing
        shell.draw(Layer::Menu, ImVec2(0, 0), io.DisplaySize);
        ImGui::Render();
    }
}

static bool drew(const std::string& s) {
    for (const std::string& t : g_texts)
        if (t == s) return true;
    return false;
}

// One pad button press and release, with a frame in between so the nav turns it into an intent.
static void tapButton(FakeHost& host, Shell& shell, SDL_GamepadButton b) {
    SDL_Event e;
    SDL_zero(e);
    e.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
    e.gbutton.button = (Uint8)b;
    e.gbutton.down = true;
    shell.handleEvent(e, g_ms);
    frames(host, shell, 1);
    e.type = SDL_EVENT_GAMEPAD_BUTTON_UP;
    e.gbutton.down = false;
    shell.handleEvent(e, g_ms);
    frames(host, shell, 1);
}
static void down(FakeHost& h, Shell& s, int n = 1) { for (int i = 0; i < n; i++) tapButton(h, s, SDL_GAMEPAD_BUTTON_DPAD_DOWN); }
static void up(FakeHost& h, Shell& s, int n = 1) { for (int i = 0; i < n; i++) tapButton(h, s, SDL_GAMEPAD_BUTTON_DPAD_UP); }
static void accept(FakeHost& h, Shell& s) { tapButton(h, s, SDL_GAMEPAD_BUTTON_SOUTH); }
static void back(FakeHost& h, Shell& s) { tapButton(h, s, SDL_GAMEPAD_BUTTON_EAST); }

static const char* focusedLabel(const Shell& s) { return s.menuRows()[(size_t)s.menuFocus()].label.c_str(); }

static ImGuiWindow* menuWindow() { return ImGui::FindWindowByName(kMenuWindowName); }
static bool menuWindowDrawn() { ImGuiWindow* w = menuWindow(); return w && w->WasActive; }

static void menuOpensAndWalks() {
    FakeHost host;
    Shell shell(host);
    shell.toggleMenu();
    CHECK(shell.menuOpen() && host.paused);
    frames(host, shell, 3);
    down(host, shell, 8);   // runs off the end: focus never wraps
    CHECK(std::string(focusedLabel(shell)) == "Close game");
    up(host, shell, 8);
    CHECK(std::string(focusedLabel(shell)) == "Resume");
    down(host, shell);      // Save state
    CHECK(std::string(focusedLabel(shell)) == "Save state");
    accept(host, shell);
    CHECK(shell.menuPage() == Shell::Page::SaveSlots);
    down(host, shell, 3);
    accept(host, shell);
    CHECK(host.savedSlot == 3);
    frames(host, shell, 120);
    CHECK(g_imguiErrors == 0);
    // Every page draws without an ImGui error: the Load slot page (empty slots greyed), both confirms.
    CHECK(shell.menuPage() == Shell::Page::Root && shell.menuOpen());   // a save returns to the root; the menu stays open
    down(host, shell);      // Load state is the row after Save state
    accept(host, shell);
    CHECK(shell.menuPage() == Shell::Page::LoadSlots);
    frames(host, shell, 10);
    back(host, shell);
    down(host, shell, 2);
    accept(host, shell);
    CHECK(shell.menuPage() == Shell::Page::ConfirmReset);
    frames(host, shell, 10);
    back(host, shell);
    CHECK(g_imguiErrors == 0);
}

static void menuRefusedInHardcore() {
    FakeHost host;
    host.refusePause = "Hardcore: wait 20 s";
    Shell shell(host);
    CHECK(!shell.openMenu() && host.lastToast == "Hardcore: wait 20 s" && !shell.menuOpen());
    frames(host, shell, 3);
    CHECK(!menuWindowDrawn());
}

static void loadRowsHiddenInHardcore() {
    FakeHost host;
    host.canLoad = false;
    Shell shell(host);
    shell.openMenu();
    g_texts.clear();
    for (int i = 0; i < 8; i++) {
        for (const Shell::Row& r : shell.menuRows()) CHECK(r.label.rfind("Load", 0) != 0);
        down(host, shell);
    }
    for (const std::string& t : g_texts) CHECK(t.rfind("Load", 0) != 0);
}

static void backResumes() {
    FakeHost host;
    Shell shell(host);
    shell.openMenu();
    frames(host, shell, 2);
    back(host, shell);
    CHECK(!shell.menuOpen() && host.resumed == 1 && host.guardArmed == 1 && host.holdsCleared == 1);
}

static void menuKeepsUserPause() {
    FakeHost host;
    host.paused = true;
    Shell shell(host);
    shell.openMenu();
    back(host, shell);
    CHECK(!shell.menuOpen() && host.paused);
}

static void closeGameAsksFirst() {
    FakeHost host;
    Shell shell(host);
    shell.openMenu();
    down(host, shell, 5);
    CHECK(std::string(focusedLabel(shell)) == "Close game");
    accept(host, shell);                                // -> the confirm, focus on No
    CHECK(shell.menuPage() == Shell::Page::ConfirmClose && std::string(focusedLabel(shell)) == "No");
    accept(host, shell);                                // No
    CHECK(host.closed == 0 && shell.menuPage() == Shell::Page::Root && std::string(focusedLabel(shell)) == "Close game");
    accept(host, shell);
    up(host, shell);                                    // Yes
    CHECK(std::string(focusedLabel(shell)) == "Yes");
    accept(host, shell);
    CHECK(host.closed == 1 && !shell.menuOpen());
}

static void netplayBanner() {
    FakeHost host;
    host.netplay = true;
    Shell shell(host);
    shell.openMenu();
    g_texts.clear();
    frames(host, shell, 3);
    CHECK(host.paused == false);
    CHECK(drew("Netplay: the game keeps running"));
    FakeHost solo;
    Shell shell2(solo);
    shell2.openMenu();
    g_texts.clear();
    frames(solo, shell2, 3);
    CHECK(!drew("Netplay: the game keeps running"));
}

static void hintBarShowsWhatWorks() {
    FakeHost host;
    Shell shell(host);
    shell.openMenu();
    frames(host, shell, 3);
    down(host, shell, 2);                               // Load state
    accept(host, shell);                                // Load page, first filled slot (2)
    CHECK(shell.menuPage() == Shell::Page::LoadSlots);
    g_texts.clear();
    frames(host, shell, 2);
    CHECK(drew("Load"));                                // A loads the slot under focus
    up(host, shell);                                    // slot 1 is empty: nothing to load
    g_texts.clear();
    frames(host, shell, 2);
    CHECK(!drew("Load"));
    CHECK(drew("Back"));
}

static void pointerFreeWhilePlaying() {
    FakeHost host;
    Shell shell(host);
    ImGuiIO& io = ImGui::GetIO();
    io.MousePos = ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
    frames(host, shell, 3);
    CHECK(!io.WantCaptureMouse && !menuWindowDrawn());  // the light gun / mouse keep the picture
    shell.openMenu();
    frames(host, shell, 3);
    CHECK(menuWindowDrawn());
    CHECK(!io.WantCaptureKeyboard);                     // F4 and the hold edges are gated on it
    // A click on a row (the second, Save state, at 1920x1080) picks it -- and holding the button does not make ImGui
    // capture the keyboard either (a click in a window's empty space activates its move ID).
    io.MousePos = ImVec2(1100, 248);
    io.AddMousePosEvent(1100, 248);
    frames(host, shell, 3);
    CHECK(std::string(focusedLabel(shell)) == "Save state");
    io.AddMouseButtonEvent(0, true);
    frames(host, shell, 3);
    CHECK(shell.menuPage() == Shell::Page::SaveSlots);
    CHECK(!io.WantCaptureKeyboard);
    CHECK(io.WantCaptureMouse);
    io.AddMouseButtonEvent(0, false);
    frames(host, shell, 2);
    shell.closeMenu();
    frames(host, shell, 3);
    CHECK(!menuWindowDrawn() && !io.WantCaptureMouse && !io.WantCaptureKeyboard);
}

int main() {
    ImGuiContext* ctx = ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(1920, 1080);
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;   // the worst case: the app turns it off, but a window that took nav input would then capture the keyboard
    io.ConfigErrorRecoveryEnableAssert = false;         // count ImGui's recoverable errors instead of aborting
    ctx->ErrorCallback = onImGuiError;
    unsigned char* px = nullptr;
    int w = 0, h = 0;
    io.Fonts->GetTexDataAsRGBA32(&px, &w, &h);

    widgets::setTextProbe(&g_texts);
    menuOpensAndWalks();
    menuRefusedInHardcore();
    loadRowsHiddenInHardcore();
    backResumes();
    menuKeepsUserPause();
    closeGameAsksFirst();
    netplayBanner();
    hintBarShowsWhatWorks();
    pointerFreeWhilePlaying();
    // The menu at the other sizes the app runs at.
    {
        FakeHost host;
        Shell shell(host);
        shell.openMenu();
        for (float hh : {720.0f, 1080.0f, 2160.0f}) {
            io.DisplaySize = ImVec2(hh * 16 / 9, hh);
            frames(host, shell, 3);
            // Everything the menu drew lies inside the display (the 5% safe area holds its text; only the scrim is edge to edge).
            ImDrawData* dd = ImGui::GetDrawData();
            bool inside = dd->CmdListsCount > 0;
            for (int l = 0; l < dd->CmdListsCount; l++)
                for (const ImDrawVert& v : dd->CmdLists[l]->VtxBuffer)
                    if (v.pos.x < -0.5f || v.pos.y < -0.5f || v.pos.x > io.DisplaySize.x + 0.5f || v.pos.y > io.DisplaySize.y + 0.5f) inside = false;
            CHECK(inside);
        }
        CHECK(g_imguiErrors == 0);
    }
    widgets::setTextProbe(nullptr);

    ImGui::DestroyContext();
    std::printf("tv ui smoke: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
