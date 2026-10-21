// TV mode (#991): the in-game menu drawn headlessly -- a real ImGui context with no renderer, the Shell over a fake
// tv::Host that records what the menu asked of the app.  What this catches that the Shell's own logic tests cannot:
// an ImGui error (ID / stack / window misuse) on any page of the menu, the text it really draws (the netplay banner,
// the Load rows that must not exist under hardcore), and that the menu leaves the pointer and the keyboard to the app
// (no ImGui window while it is closed; no keyboard capture while it is open: F4 and the hold edges are gated on it).
#include "desktop_nav_popup.h"
#include "tv/tv_screen.h"
#include "tv/tv_shell.h"
#include "tv/tv_widgets.h"

#include "imgui.h"
#include "imgui_internal.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <map>
#include <set>
#include <string>
#include <utility>
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
    bool controlsShown = false;
    std::string lastToast;
    bool tv = false, tvOn = true;   // TV mode on right now; tvOn = tvAvailable() (the setting is not Off)
    std::string ra;                 // raStatus(): "" unless signed in
    std::string clockStr = "8:42 PM";
    // The library (phase 2): the four lists, each game's states, and what was launched.
    std::vector<GameRef> all, favs, recent, disc;
    std::map<std::string, std::vector<StateRef>> st;
    std::string launchedKey;
    std::string launchedDisc;
    std::pair<std::string, int> launchedState{"", -99};
    int launches = 0;
    std::set<std::string> favSet;   // the keys marked favorite

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
    void showControlsOverlay() override { controls++; controlsShown = true; }   // idempotent, like the app's
    void armReleaseGuard() override { guardArmed++; }
    void clearHolds() override { holdsCleared++; }
    void toast(const std::string& t) override { lastToast = t; }
    // A Switch-family pad (#1021): its EAST button is Confirm and SOUTH is Back, so the shell must ask the host, not the Xbox table.
    bool switchPad = false, desktopIntents = false;   // desktopIntents: WEST is Search, NORTH is AcceptLong (the desktop library's)
    Intent padIntent(uint32_t, int b) const override {
        if (desktopIntents && b == SDL_GAMEPAD_BUTTON_WEST) return Intent::Search;
        if (desktopIntents && b == SDL_GAMEPAD_BUTTON_NORTH) return Intent::AcceptLong;
        if (switchPad && b == SDL_GAMEPAD_BUTTON_EAST) return Intent::Accept;
        if (switchPad && b == SDL_GAMEPAD_BUTTON_SOUTH) return Intent::Back;
        return intentForButton(b);
    }
    bool keyboardLast = false;   // the keyboard was used last: it has no key for Menu / Alt / Aux (as AppHost::glyph)
    std::string glyph(Intent i) const override {
        switch (i) {
        case Intent::Menu: return keyboardLast ? "" : "Start";
        case Intent::Alt: return keyboardLast ? "" : "X";
        case Intent::Aux: return keyboardLast ? "" : "Y";
        case Intent::Accept: return "A";
        case Intent::Back: return "B";
        case Intent::Up:
        case Intent::Down: return "D-pad";
        default: return "";
        }
    }
    bool tvActive() const override { return tv; }
    bool tvAvailable() const override { return tvOn; }
    void setTvActive(bool on) override { tv = on && tvOn; }
    std::string clockText() const override { return clockStr; }
    std::string raStatus() const override { return ra; }
    bool desktopOpen = false, scanning = false;   // a desktop window covers the TV screens / a library scan is running
    bool desktopWindowOpen() const override { return desktopOpen; }
    bool libraryScanning() const override { return scanning; }
    std::vector<GameRef> games(List l) override {
        switch (l) {
        case List::All: return all;
        case List::Favorites: return favs;
        case List::Recent: return recent;
        case List::Disc: return disc;
        }
        return {};
    }
    Art art(const GameRef&) override { return {}; }
    std::vector<StateRef> states(const GameRef& g) override {
        auto it = st.find(g.key);
        return it == st.end() ? std::vector<StateRef>() : it->second;
    }
    Art stateThumb(const GameRef&, const StateRef&) override { return {}; }
    void launch(const GameRef& g) override { launchedKey = g.key; launches++; }
    void launchState(const GameRef& g, const StateRef& state) override { launchedState = {g.key, state.slot}; launchedDisc = state.disc; }
    bool isFavorite(const GameRef& g) override { return favSet.count(g.key) != 0; }
    void setFavorite(const GameRef& g, bool on) override {
        if (on) favSet.insert(g.key);
        else favSet.erase(g.key);
    }
};

// n games named "<prefix> 0" .. "<prefix> n-1", keyed by a path.
static std::vector<GameRef> mkGames(const char* prefix, int n, bool disc = false) {
    std::vector<GameRef> v;
    for (int i = 0; i < n; i++) {
        GameRef g;
        g.title = std::string(prefix) + " " + std::to_string(i);
        g.key = "/games/" + g.title + (disc ? ".cue" : ".j64");
        g.region = i % 2 ? "Europe" : "USA";
        g.disc = disc;
        g.index = i;
        v.push_back(g);
    }
    return v;
}

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
        shell.draw(Layer::Base, ImVec2(0, 0), io.DisplaySize);
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
    down(host, shell, 3);   // Controls, Enter TV mode, Reset
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
    down(host, shell, 6);
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

// Controls SHOWS the cheat sheet (an already visible one stays), closes the menu onto the game.
static void controlsRowShowsNeverHides() {
    FakeHost host;
    host.controlsShown = true;   // already up (auto-shown at boot)
    Shell shell(host);
    shell.openMenu();
    frames(host, shell, 2);
    down(host, shell, 3);   // Resume, Save state, Load state, Controls
    CHECK(std::string(focusedLabel(shell)) == "Controls");
    accept(host, shell);
    CHECK(host.controls == 1 && host.controlsShown && !shell.menuOpen());
}

// The mouse wheel walks the list: a mouse-only player reaches slots 6-9.
static void wheelReachesLastSlots() {
    FakeHost host;
    Shell shell(host);
    ImGuiIO& io = ImGui::GetIO();
    shell.openMenu();
    frames(host, shell, 2);
    down(host, shell);
    accept(host, shell);   // Save state -> the slot page
    CHECK(shell.menuPage() == Shell::Page::SaveSlots && shell.menuFocus() == 0);
    for (int i = 0; i < 9; i++) {
        io.AddMouseWheelEvent(0, -1.0f);   // down one notch
        frames(host, shell, 1);
    }
    CHECK(shell.menuFocus() == 9);
    io.AddMouseWheelEvent(0, 2.0f);        // up two notches
    frames(host, shell, 1);
    CHECK(shell.menuFocus() == 7);
    io.AddMouseWheelEvent(0, -0.5f);       // half a notch (a trackpad) is not a step yet
    frames(host, shell, 1);
    CHECK(shell.menuFocus() == 7);
}

// A narrow hint bar drops the others, never the way out.
static void hintBarKeepsBack() {
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(1920, 1080);
    Metrics m = metrics(1080);
    const std::vector<widgets::Hint> hints = {{"D-pad", "Move"}, {"A", "Select"}, {"B", "Resume"}};
    for (float w : {2000.0f, 600.0f, 160.0f}) {
        g_texts.clear();
        io.DeltaTime = 1.0f / 60.0f;
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("##hb", nullptr, ImGuiWindowFlags_NoDecoration);
        widgets::hintBar(ImGui::GetWindowDrawList(), m, ImVec2(10, 10), w, hints);
        ImGui::End();
        ImGui::Render();
        CHECK(drew("Resume"));
        if (w >= 2000.0f) CHECK(drew("Move") && drew("Select"));
        if (w <= 160.0f) CHECK(!drew("Move") && !drew("Select"));   // only the way out fits
    }
}


// ---- the Base layer: sections, chrome, Back, typing, the cursor (phase 2) ------------------------------------------

static ImGuiWindow* baseWindow() { return ImGui::FindWindowByName(kBaseWindowName); }

static bool keyEvent(Shell& shell, SDL_Scancode sc, SDL_Keycode k, bool down = true, SDL_Keymod mod = SDL_KMOD_NONE,
                     bool repeat = false) {
    SDL_Event e;
    SDL_zero(e);
    e.key.repeat = repeat;
    e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    e.key.scancode = sc;
    e.key.key = k;
    e.key.mod = mod;
    e.key.down = down;
    return shell.handleEvent(e, g_ms);
}
static void rb(FakeHost& h, Shell& s) { tapButton(h, s, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER); }
static void lb(FakeHost& h, Shell& s) { tapButton(h, s, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER); }

// The Base window exists only while TV mode is on with no game running.  Run before anything draws it: ImGui keeps a
// window it has seen for the life of the context.
static void baseOnlyWithoutGame() {
    {
        FakeHost host;
        host.tv = true;
        host.loaded = true;
        Shell shell(host);
        frames(host, shell, 5);
        CHECK(baseWindow() == nullptr && !shell.ownsInput());
    }
    {
        FakeHost host;
        host.tv = false;
        host.loaded = false;
        Shell shell(host);
        frames(host, shell, 5);
        CHECK(baseWindow() == nullptr && !shell.ownsInput());
    }
    FakeHost host;
    host.tv = true;
    host.loaded = false;
    Shell shell(host);
    frames(host, shell, 2);
    CHECK(shell.ownsInput() && baseWindow() && baseWindow()->WasActive);
}

static void tvShellWalksSections() {
    FakeHost host;
    host.tv = true;
    host.loaded = false;
    Shell shell(host);
    g_texts.clear();
    frames(host, shell, 1);
    CHECK(shell.screenId() == "home" && shell.section() == Section::Home);
    rb(host, shell);
    CHECK(shell.screenId() == "library");
    rb(host, shell);   // no wrap
    CHECK(shell.screenId() == "library");
    lb(host, shell);
    CHECK(shell.screenId() == "home");
    lb(host, shell);
    CHECK(shell.screenId() == "home");
    frames(host, shell, 120);
    CHECK(g_imguiErrors == 0 && drew("Home") && drew("Library") && drew("8:42 PM"));
    CHECK(drew("VIRTUAL JAGUAR"));
    rb(host, shell);
    frames(host, shell, 3);
    CHECK(drew("Library"));
}

static void escBackThenRoot() {
    FakeHost host;
    host.tv = true;
    host.loaded = false;
    Shell shell(host);
    frames(host, shell, 1);
    rb(host, shell);
    CHECK(shell.screenId() == "library" && !shell.backAtRoot());
    CHECK(keyEvent(shell, SDL_SCANCODE_ESCAPE, SDLK_ESCAPE));
    keyEvent(shell, SDL_SCANCODE_ESCAPE, SDLK_ESCAPE, false);
    frames(host, shell, 2);
    CHECK(shell.screenId() == "home" && shell.backAtRoot());
    CHECK(!keyEvent(shell, SDL_SCANCODE_ESCAPE, SDLK_ESCAPE));   // at the root Esc keeps its desktop meaning
    frames(host, shell, 2);
    CHECK(shell.screenId() == "home");
}


// The desktop library's popups under the pad (#1021, design section 4): ImGui's own gamepad navigation, fed with intents.
// Gamepad nav alone must not make ImGui want the keyboard (F4 and the #1020 shortcut keep working), the intents must walk
// and activate rows and Back must close the popup, and with the flags off the same intents must do nothing.
struct PopupProbe {
    int a = 0, b = 0, c = 0;
    bool open = false;
};
static void popupFrame(PopupProbe& p, bool navOn, bool openNow) {
    ImGuiIO& io = ImGui::GetIO();
    io.DeltaTime = 1.0f / 60.0f;
    vj::deskSetPopupNav(navOn);
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(20, 20));
    ImGui::SetNextWindowSize(ImVec2(300, 200));
    ImGui::Begin("padpopup");
    if (openNow) ImGui::OpenPopup("##m");
    p.open = false;
    if (ImGui::BeginPopup("##m")) {
        p.open = true;
        if (ImGui::MenuItem("Row A")) p.a++;
        if (ImGui::MenuItem("Row B")) p.b++;
        if (ImGui::MenuItem("Row C")) p.c++;
        ImGui::EndPopup();
    }
    ImGui::End();
    ImGui::Render();
}
static void popupFrames(PopupProbe& p, bool navOn, int n) {
    for (int i = 0; i < n; i++) {
        popupFrame(p, navOn, false);
        CHECK(!ImGui::GetIO().WantCaptureKeyboard);   // gamepad nav is not keyboard capture, popup open or not
    }
}
static void padPopupNav() {
    ImGuiIO& io = ImGui::GetIO();
    const ImGuiConfigFlags savedFlags = io.ConfigFlags;
    io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;   // as the app has it (initImGui); main() turns it on for the worst case
    // Unmapped intents press nothing; the six that matter map to the gamepad keys.
    CHECK(vj::deskPopupKey(Intent::Alt) == ImGuiKey_None && vj::deskPopupKey(Intent::Aux) == ImGuiKey_None &&
          vj::deskPopupKey(Intent::SectionNext) == ImGuiKey_None && vj::deskPopupKey(Intent::AcceptLong) == ImGuiKey_None);
    CHECK(vj::deskPopupKey(Intent::Accept) == ImGuiKey_GamepadFaceDown && vj::deskPopupKey(Intent::Back) == ImGuiKey_GamepadFaceRight);
    CHECK(vj::deskPopupKey(Intent::Down) == ImGuiKey_GamepadDpadDown && vj::deskPopupKey(Intent::Up) == ImGuiKey_GamepadDpadUp);

    PopupProbe p;
    popupFrames(p, false, 2);
    popupFrame(p, false, true);   // the popup opens (as the pad's long press opens the tile menu)
    popupFrames(p, true, 3);      // ... and the nav goes on the frame after
    CHECK(p.open);
    CHECK(!io.WantCaptureKeyboard);
    // The first row is focused and its highlight is up before any direction is pressed (Accept acts on press: a player must
    // see which row an A would take).
    CHECK(ImGui::GetCurrentContext()->NavCursorVisible && ImGui::GetCurrentContext()->NavId != 0);

    // That popup was opened by the "mouse" here (nothing marked it the pad's): a foreign one, under which the library does not
    // own the pad.  Marked as the pad's, it is not.  (deskPadActive applies this predicate.)
    CHECK(vj::deskForeignPopupOpen(false) && !vj::deskForeignPopupOpen(true));

    // With the nav off the same intents do nothing at all.
    vj::deskFeedPopupIntents({Intent::Down, Intent::Accept});
    popupFrames(p, false, 4);
    CHECK(p.a == 0 && p.b == 0 && p.c == 0 && p.open);

    // On: the popup opens with its first row focused, so Down, Down lands on the third; Accept activates that row, once,
    // and (a MenuItem) closes the popup.
    vj::deskFeedPopupIntents({Intent::Down});
    popupFrames(p, true, 3);
    vj::deskFeedPopupIntents({Intent::Down});
    popupFrames(p, true, 3);
    vj::deskFeedPopupIntents({Intent::Accept});
    popupFrames(p, true, 4);
    CHECK(p.a == 0 && p.b == 0 && p.c == 1);
    CHECK(!p.open);
    // Reopened: the intents a popup does not use do nothing; Back closes it without activating anything.
    popupFrame(p, true, true);
    popupFrames(p, true, 3);
    CHECK(p.open);
    vj::deskFeedPopupIntents({Intent::Alt, Intent::Aux, Intent::SectionNext, Intent::PagePrev});
    popupFrames(p, true, 4);
    CHECK(p.open && p.a == 0 && p.b == 0 && p.c == 1);
    vj::deskFeedPopupIntents({Intent::Back});
    popupFrames(p, true, 4);
    CHECK(!p.open);
    CHECK(p.a == 0 && p.b == 0 && p.c == 1);
    CHECK(!vj::deskForeignPopupOpen(false));   // no popup open: nothing foreign
    CHECK(g_imguiErrors == 0);
    vj::deskSetPopupNav(false);
    popupFrames(p, false, 3);
    CHECK(!io.ConfigNavCursorVisibleAlways && !ImGui::GetCurrentContext()->NavCursorVisible);   // the highlight goes with the nav
    io.ConfigFlags = savedFlags;
}

static void baseNeverCapturesKeyboard() {
    FakeHost host;
    host.tv = true;
    host.loaded = false;
    Shell shell(host);
    frames(host, shell, 2);
    down(host, shell, 2);
    accept(host, shell);
    frames(host, shell, 3);
    CHECK(!ImGui::GetIO().WantCaptureKeyboard);
    // ... not even with a click held in the window's empty space
    ImGuiIO& io = ImGui::GetIO();
    io.AddMousePosEvent(900, 500);
    frames(host, shell, 2);
    io.AddMouseButtonEvent(0, true);
    frames(host, shell, 3);
    CHECK(!io.WantCaptureKeyboard);
    io.AddMouseButtonEvent(0, false);
    frames(host, shell, 2);
}

static void padHidesCursor() {
    FakeHost host;
    host.tv = true;
    host.loaded = false;
    Shell shell(host);
    frames(host, shell, 2);
    down(host, shell);
    frames(host, shell, 1);
    CHECK(ImGui::GetMouseCursor() == ImGuiMouseCursor_None);
    SDL_Event e;
    SDL_zero(e);
    e.type = SDL_EVENT_MOUSE_MOTION;
    e.motion.xrel = 5;
    CHECK(!shell.handleEvent(e, g_ms));
    frames(host, shell, 1);
    CHECK(ImGui::GetMouseCursor() != ImGuiMouseCursor_None);
}

static void baseReturnsAfterGame() {
    FakeHost host;
    host.tv = true;
    host.loaded = false;
    Shell shell(host);
    frames(host, shell, 1);
    rb(host, shell);
    CHECK(shell.screenId() == "library");
    host.loaded = true;
    frames(host, shell, 5);
    CHECK(!shell.ownsInput());
    host.loaded = false;
    frames(host, shell, 2);
    CHECK(shell.screenId() == "library" && shell.section() == Section::Library && shell.ownsInput());
}

static void raStatusHiddenWhenEmpty() {
    FakeHost host;
    host.tv = true;
    host.loaded = false;
    Shell shell(host);
    g_texts.clear();
    frames(host, shell, 3);
    bool any = false;
    for (const std::string& t : g_texts)
        if (t.rfind("RetroAchievements", 0) == 0) any = true;
    CHECK(!any);
    host.ra = "RetroAchievements: joe \xC2\xB7 Hardcore";
    g_texts.clear();
    frames(host, shell, 3);
    CHECK(drew("RetroAchievements: joe \xC2\xB7 Hardcore"));
}

// A screen that records what the shell hands it.
struct ProbeScreen : Screen {
    std::string name;
    bool isModal = false, takesLetters = true, takesBackspace = true, popInDraw = false;
    Shell* shell = nullptr;
    std::vector<Intent> intents;
    std::string typedLog;
    std::vector<std::string>* drawLog = nullptr;
    explicit ProbeScreen(std::string n) : name(std::move(n)) {}
    const char* id() const override { return name.c_str(); }
    void intent(Intent in) override { intents.push_back(in); }
    bool typed(char c) override {
        typedLog += c;
        return c == '\b' ? takesBackspace : takesLetters;
    }
    void draw(const DrawCtx& c) override {
        if (drawLog) drawLog->push_back(name);
        widgets::text(c.dl, c.m.body, c.pos, IM_COL32_WHITE, name);
        if (popInDraw && shell) shell->pop();
    }
    std::vector<widgets::Hint> hints() const override { return {{"A", "Go"}, {"B", "Out"}}; }
    bool modal() const override { return isModal; }
};

static ProbeScreen* pushProbe(Shell& shell, const char* name, bool modal = false) {
    auto p = std::make_unique<ProbeScreen>(name);
    p->isModal = modal;
    p->shell = &shell;
    ProbeScreen* raw = p.get();
    shell.push(std::move(p));
    return raw;
}

static void screenStackRoutesInput() {
    FakeHost host;
    host.tv = true;
    host.loaded = false;
    Shell shell(host);
    frames(host, shell, 2);
    ProbeScreen* a = pushProbe(shell, "probe-a");
    CHECK(shell.screenId() == "probe-a" && !shell.backAtRoot());
    down(host, shell);
    tapButton(host, shell, SDL_GAMEPAD_BUTTON_WEST);
    CHECK(a->intents.size() == 2 && a->intents[0] == Intent::Down && a->intents[1] == Intent::Alt);
    // Start is the screens' Menu while the Base layer owns the input.
    SDL_Event st;
    SDL_zero(st);
    st.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
    st.gbutton.button = SDL_GAMEPAD_BUTTON_START;
    st.gbutton.down = true;
    CHECK(shell.handleEvent(st, g_ms));
    frames(host, shell, 1);
    st.type = SDL_EVENT_GAMEPAD_BUTTON_UP;
    st.gbutton.down = false;
    shell.handleEvent(st, g_ms);
    frames(host, shell, 1);
    CHECK(a->intents.back() == Intent::Menu);
    // Typing: a letter or a digit goes to the screen, and is consumed when the screen uses it.
    CHECK(keyEvent(shell, SDL_SCANCODE_D, SDLK_D));
    CHECK(keyEvent(shell, SDL_SCANCODE_5, SDLK_5));
    CHECK(keyEvent(shell, SDL_SCANCODE_BACKSPACE, SDLK_BACKSPACE));
    CHECK(a->typedLog == "d5\b");
    frames(host, shell, 2);
    CHECK(shell.screenId() == "probe-a");   // a used Backspace is not also Back
    // A key chord is a hotkey, never typing.
    CHECK(!keyEvent(shell, SDL_SCANCODE_A, SDLK_A, true, SDL_KMOD_CTRL));
    CHECK(!keyEvent(shell, SDL_SCANCODE_F, SDLK_F, true, SDL_KMOD_GUI));
    CHECK(!keyEvent(shell, SDL_SCANCODE_F4, SDLK_F4));
    CHECK(a->typedLog == "d5\b");
    // A key the screen does not use is not consumed; an unused Backspace is Back.
    a->takesLetters = false;
    a->takesBackspace = false;
    CHECK(!keyEvent(shell, SDL_SCANCODE_Q, SDLK_Q));
    CHECK(keyEvent(shell, SDL_SCANCODE_BACKSPACE, SDLK_BACKSPACE));
    keyEvent(shell, SDL_SCANCODE_BACKSPACE, SDLK_BACKSPACE, false);
    frames(host, shell, 2);
    CHECK(shell.screenId() == "home");
    // The root is never popped.
    shell.pop();
    CHECK(shell.screenId() == "home");
}

// A text box on a desktop window above the TV screens owns the typing.
static void textBoxOwnsTyping() {
    FakeHost host;
    host.tv = true;
    host.loaded = false;
    Shell shell(host);
    frames(host, shell, 2);
    ProbeScreen* a = pushProbe(shell, "probe-a");
    ImGuiIO& io = ImGui::GetIO();
    char buf[16] = "";
    auto boxFrame = [&]() {
        io.DeltaTime = 1.0f / 60.0f;
        ImGui::NewFrame();
        shell.frame(g_ms);
        shell.draw(Layer::Base, ImVec2(0, 0), io.DisplaySize);
        ImGui::Begin("box");
        ImGui::SetKeyboardFocusHere();
        ImGui::InputText("##t", buf, sizeof buf);
        ImGui::End();
        ImGui::Render();
    };
    for (int i = 0; i < 3; i++) boxFrame();
    CHECK(io.WantTextInput);
    CHECK(!keyEvent(shell, SDL_SCANCODE_B, SDLK_B) && a->typedLog.empty());
    // ... and so do Enter, Tab, Backspace, an arrow and Esc: none is consumed, none navigates (a key-up still reaches the nav).
    const Section sec = shell.section();
    const std::string id = shell.screenId();
    const std::pair<SDL_Scancode, SDL_Keycode> keys[] = {{SDL_SCANCODE_RETURN, SDLK_RETURN}, {SDL_SCANCODE_TAB, SDLK_TAB},
                                                           {SDL_SCANCODE_BACKSPACE, SDLK_BACKSPACE}, {SDL_SCANCODE_LEFT, SDLK_LEFT},
                                                           {SDL_SCANCODE_ESCAPE, SDLK_ESCAPE}};
    a->intents.clear();
    for (const auto& k : keys) {
        CHECK(!keyEvent(shell, k.first, k.second));
        CHECK(!keyEvent(shell, k.first, k.second, false));
        boxFrame();
        boxFrame();
    }
    CHECK(a->typedLog.empty() && shell.section() == sec && shell.screenId() == id);
    frames(host, shell, 3);   // the box is gone: the keyboard is the TV screen's again
    CHECK(!io.WantTextInput);
    CHECK(keyEvent(shell, SDL_SCANCODE_B, SDLK_B) && a->typedLog == "b");
}

static void screenStackDrawsModalsOverScreen() {
    FakeHost host;
    host.tv = true;
    host.loaded = false;
    Shell shell(host);
    frames(host, shell, 2);
    std::vector<std::string> log;
    pushProbe(shell, "under")->drawLog = &log;
    pushProbe(shell, "over", true)->drawLog = &log;
    g_texts.clear();
    log.clear();
    frames(host, shell, 1);
    CHECK(log.size() == 2 && log[0] == "under" && log[1] == "over");
    // The hints are the top screen's, with the section hint ahead of the way out.
    CHECK(drew("Go") && drew("Out") && drew("Sections"));
    // The root below is not drawn while a non-modal screen covers it.
    CHECK(g_imguiErrors == 0);
}

static void screenMayPopItselfWhileDrawing() {
    FakeHost host;
    host.tv = true;
    host.loaded = false;
    Shell shell(host);
    frames(host, shell, 2);
    pushProbe(shell, "x")->popInDraw = true;
    pushProbe(shell, "y", true)->popInDraw = true;
    frames(host, shell, 3);
    CHECK(shell.screenId() == "home");
    CHECK(g_imguiErrors == 0);
}

// Start is the app's while a game runs (the shell owns nothing then).
static void startStaysAppsWithGame() {
    FakeHost host;
    host.tv = true;
    host.loaded = true;
    Shell shell(host);
    SDL_Event st;
    SDL_zero(st);
    st.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
    st.gbutton.button = SDL_GAMEPAD_BUTTON_START;
    CHECK(!shell.handleEvent(st, g_ms));
}

// Inputs released while a game had the pads never reach the nav: coming back to the Base layer must not find a button
// still "down" (the first A would be swallowed) or a direction still repeating.
static void navForgetsAcrossGame() {
    FakeHost host;
    host.tv = true;
    host.loaded = false;
    Shell shell(host);
    frames(host, shell, 2);
    ProbeScreen* a = pushProbe(shell, "probe-a");
    SDL_Event e;
    SDL_zero(e);
    e.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
    e.gbutton.down = true;
    e.gbutton.button = SDL_GAMEPAD_BUTTON_SOUTH;
    shell.handleEvent(e, g_ms);             // A goes down on the Base layer ...
    e.gbutton.button = SDL_GAMEPAD_BUTTON_DPAD_DOWN;
    shell.handleEvent(e, g_ms);             // ... and so does Down
    host.loaded = true;                     // ... a game starts under them
    frames(host, shell, 3);
    e.type = SDL_EVENT_GAMEPAD_BUTTON_UP;   // both are let go during the game: the shell never sees it
    e.gbutton.down = false;
    CHECK(!shell.handleEvent(e, g_ms));
    host.loaded = false;
    frames(host, shell, 90);                // back on the Base layer: no Down keeps repeating
    a->intents.clear();
    frames(host, shell, 60);
    CHECK(a->intents.empty());
    accept(host, shell);                    // and the first A is not swallowed
    CHECK(a->intents.size() == 1 && a->intents[0] == Intent::Accept);
}

// Text width needs a frame (the font atlas is built in NewFrame).
static float measure(float px, const char* t) {
    ImGui::GetIO().DeltaTime = 1.0f / 60.0f;
    ImGui::NewFrame();
    const float w = widgets::textWidth(px, t);
    ImGui::Render();
    return w;
}

// A left click on a section tab switches to it (the mouse works in TV mode, spec 3.1).
static void tabClickSwitchesSection() {
    FakeHost host;
    host.tv = true;
    host.loaded = false;
    Shell shell(host);
    frames(host, shell, 2);
    CHECK(shell.section() == Section::Home);
    // The Library tab at 1920x1080: safe margin 96, accent 22 + gap 16, the logo at 34, gap 56, the LB pill (24 + 2x10) and
    // its gap 20, the Home tab (30) and its gap 40.
    const float x = 96 + 38 + measure(34, "VIRTUAL JAGUAR") + 56 + measure(24, "LB") + 20 + 20 +
                    measure(30, "Home") + 40;
    ImGuiIO& io = ImGui::GetIO();
    io.AddMousePosEvent(x + 10, 54 + 28);
    frames(host, shell, 2);
    CHECK(shell.section() == Section::Home);   // hovering alone does nothing
    io.AddMouseButtonEvent(0, true);
    frames(host, shell, 2);
    io.AddMouseButtonEvent(0, false);
    frames(host, shell, 2);
    CHECK(shell.section() == Section::Library && shell.screenId() == "library");
    // ... and back: the Home tab
    io.AddMousePosEvent(96 + 38 + measure(34, "VIRTUAL JAGUAR") + 56 + measure(24, "LB") + 40 + 10, 54 + 28);
    frames(host, shell, 2);
    io.AddMouseButtonEvent(0, true);
    frames(host, shell, 2);
    io.AddMouseButtonEvent(0, false);
    frames(host, shell, 2);
    CHECK(shell.section() == Section::Home);
    io.AddMousePosEvent(0, 0);
    frames(host, shell, 2);
}

static void launchedArmsGuard() {
    FakeHost host;
    host.tv = true;
    host.loaded = false;
    Shell shell(host);
    shell.launched();
    CHECK(host.guardArmed == 1 && host.holdsCleared == 1);
    CHECK(&shell.host() == &host);
    shell.memory().put("home/x", 3);
    CHECK(shell.memory().get("home/x") == 3);
}


// ---- Home (phase 2, Task 8) ----------------------------------------------------------------------------------------

static void rightN(FakeHost& h, Shell& s, int n) { for (int i = 0; i < n; i++) tapButton(h, s, SDL_GAMEPAD_BUTTON_DPAD_RIGHT); }
static void leftN(FakeHost& h, Shell& s, int n) { for (int i = 0; i < n; i++) tapButton(h, s, SDL_GAMEPAD_BUTTON_DPAD_LEFT); }

// A TV host on the Home screen with some games, drawn once.
static void homeSetup(FakeHost& host, Shell& shell) {
    host.tv = true;
    host.loaded = false;
    ImGui::GetIO().DisplaySize = ImVec2(1920, 1080);
    frames(host, shell, 2);
}

static void homeHidesEmptyShelves() {
    FakeHost host;
    host.recent = mkGames("Recent", 3);
    host.disc = mkGames("Disc", 2, true);
    Shell shell(host);
    g_texts.clear();
    homeSetup(host, shell);
    frames(host, shell, 2);
    CHECK(drew("Continue playing") && !drew("Favorites"));   // (the next shelf shows once it is focused, see homeHidesShelfThatCannotFit)
    down(host, shell);
    frames(host, shell, 40);
    g_texts.clear();
    frames(host, shell, 2);
    CHECK(drew("Jaguar CD") && !drew("Favorites"));
}

// A shelf shows only when its whole title fits above the hint bar: the next shelf is half under the bar at the top, so
// its title and tiles are not drawn; the focused shelf's are.  At both 720p and 1080p, with all three shelves.
static void homeHidesShelfThatCannotFit() {
    for (float hh : {720.0f, 1080.0f}) {
        FakeHost host;
        host.recent = mkGames("Recent", 3);
        host.favs = mkGames("Fav", 3);
        host.disc = mkGames("Disc", 3, true);
        Shell shell(host);
        host.tv = true;
        host.loaded = false;
        ImGui::GetIO().DisplaySize = ImVec2(hh * 16 / 9, hh);
        frames(host, shell, 3);
        g_texts.clear();
        frames(host, shell, 2);
        CHECK(drew("Continue playing") && drew("Recent 0"));
        CHECK(!drew("Favorites") && !drew("Fav 0") && !drew("Jaguar CD") && !drew("Disc 0"));
        down(host, shell);                          // Favorites focused: its title shows, the shelf after it does not
        frames(host, shell, 40);
        g_texts.clear();
        frames(host, shell, 2);
        CHECK(drew("Favorites") && drew("Fav 0"));
        CHECK(!drew("Jaguar CD") && !drew("Disc 0"));
    }
    ImGui::GetIO().DisplaySize = ImVec2(1920, 1080);
}

// The hint bar lists only what the last-used input device has: no Menu / Alt / Aux hint after keyboard input (the
// chrome drops a hint whose glyph is "", Home's hero buttons too); the pad shows them.  The pure part of the rule;
// AppHost::glyph itself (the "" for Menu / Alt / Aux while the keyboard was last used) needs an App and is not covered here.
static void hintsListOnlyWhatWorks() {
    for (int keyboard = 0; keyboard < 2; keyboard++) {
        FakeHost host;
        host.keyboardLast = keyboard != 0;
        host.recent = mkGames("Recent", 3);
        host.all = host.recent;
        host.st[host.recent[0].key] = {{3, 1792152000}};
        Shell shell(host);
        homeSetup(host, shell);
        g_texts.clear();
        frames(host, shell, 2);
        CHECK(drew("Play") && drew("Sections"));                 // Accept and the Tab pair exist on every device
        CHECK(drew("Details") == !keyboard && drew("Resume state") == !keyboard && drew("Resume last state") == !keyboard);
        tapButton(host, shell, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);   // Library
        CHECK(shell.screenId() == "library");
        g_texts.clear();
        frames(host, shell, 2);
        CHECK(drew("Play") && drew("Sections"));
        CHECK(drew("Details") == !keyboard && drew("Jump to letter") == !keyboard);
    }
}

static void homeAcceptLaunchesFocused() {
    FakeHost host;
    host.recent = mkGames("Recent", 3);
    Shell shell(host);
    homeSetup(host, shell);
    tapButton(host, shell, SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
    accept(host, shell);
    CHECK(host.launchedKey == host.recent[1].key && host.launches == 1);
    CHECK(host.guardArmed == 1 && host.holdsCleared == 1);
}

// A pad whose profile swaps Confirm and Back (a Switch-family pad) drives TV mode through that profile (#1021).
static void switchPadAcceptsEast() {
    FakeHost host;
    host.switchPad = true;
    host.recent = mkGames("Recent", 3);
    Shell shell(host);
    homeSetup(host, shell);
    tapButton(host, shell, SDL_GAMEPAD_BUTTON_SOUTH);          // Back at the root: nothing happens
    CHECK(host.launches == 0 && shell.screenId() == "home");
    tapButton(host, shell, SDL_GAMEPAD_BUTTON_EAST);           // Confirm: launches the focused game
    CHECK(host.launchedKey == host.recent[0].key && host.launches == 1);
}

// Search and AcceptLong are not TV's: the shell neither feeds nor consumes a button mapped to one (the app keeps it).
static void desktopIntentsAreNotTvs() {
    FakeHost host;
    host.desktopIntents = true;
    host.recent = mkGames("Recent", 3);
    host.st[host.recent[0].key] = {{3, 200}};
    Shell shell(host);
    homeSetup(host, shell);
    SDL_Event e;
    SDL_zero(e);
    e.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
    e.gbutton.down = true;
    e.gbutton.button = SDL_GAMEPAD_BUTTON_WEST;                // would be Resume state on the Xbox table
    CHECK(!shell.handleEvent(e, g_ms));
    e.gbutton.button = SDL_GAMEPAD_BUTTON_NORTH;
    CHECK(!shell.handleEvent(e, g_ms));
    frames(host, shell, 2);
    CHECK(host.launchedState.first.empty() && host.launches == 0 && shell.screenId() == "home");
}

static void homeXResumesNewest() {
    FakeHost host;
    host.recent = mkGames("Recent", 3);
    host.st[host.recent[0].key] = {{3, 200}, {-1, 100}};
    Shell shell(host);
    homeSetup(host, shell);
    tapButton(host, shell, SDL_GAMEPAD_BUTTON_WEST);
    CHECK(host.launchedState.first == host.recent[0].key && host.launchedState.second == 3);
    CHECK(host.guardArmed == 1 && host.holdsCleared == 1 && host.launches == 0);
}

static void homeResumeHiddenWithoutStatesOrInHardcore() {
    FakeHost host;
    host.recent = mkGames("Recent", 3);
    Shell shell(host);
    homeSetup(host, shell);
    g_texts.clear();
    frames(host, shell, 2);
    CHECK(!drew("Resume last state"));                      // no states
    tapButton(host, shell, SDL_GAMEPAD_BUTTON_WEST);
    CHECK(host.launchedState.first.empty());
    host.st[host.recent[0].key] = {{-1, 100}};
    g_texts.clear();
    frames(host, shell, 2);
    CHECK(drew("Resume last state") && drew("Resume state"));
    host.canLoad = false;                                   // hardcore refuses state loads
    g_texts.clear();
    frames(host, shell, 2);
    CHECK(!drew("Resume last state") && !drew("Resume state"));
    tapButton(host, shell, SDL_GAMEPAD_BUTTON_WEST);
    CHECK(host.launchedState.first.empty() && host.guardArmed == 0);
}

static void homeFocusRememberedPerShelf() {
    FakeHost host;
    host.recent = mkGames("Recent", 5);
    host.favs = mkGames("Fav", 5);
    Shell shell(host);
    homeSetup(host, shell);
    down(host, shell);
    rightN(host, shell, 3);
    up(host, shell);
    down(host, shell);
    accept(host, shell);
    CHECK(host.launchedKey == host.favs[3].key);
    // ... and the first shelf kept its own place too.
    up(host, shell);
    accept(host, shell);
    CHECK(host.launchedKey == host.recent[0].key);
    // Leaving Home and coming back (a new screen) lands where it was.
    down(host, shell);
    rb(host, shell);
    lb(host, shell);
    accept(host, shell);
    CHECK(host.launchedKey == host.favs[3].key);
}

static void homeEdgesDoNothing() {
    FakeHost host;
    host.recent = mkGames("Recent", 2);
    Shell shell(host);
    homeSetup(host, shell);
    up(host, shell);
    leftN(host, shell, 3);
    accept(host, shell);
    CHECK(host.launchedKey == host.recent[0].key);
    down(host, shell);                                      // one shelf only: nothing below
    rightN(host, shell, 5);                                 // no wrap
    accept(host, shell);
    CHECK(host.launchedKey == host.recent[1].key);
}

static void homeMouseHoverFocusesAndClicks() {
    FakeHost host;
    host.recent = mkGames("Recent", 5);
    host.disc = mkGames("Disc", 3, true);
    Shell shell(host);
    homeSetup(host, shell);
    ImGuiIO& io = ImGui::GetIO();
    io.AddMousePosEvent(96 + 12 + 2 * 242 + 105, 622 + 147);   // tile 2 of the first shelf
    frames(host, shell, 2);
    accept(host, shell);                                       // hovering focused it: A launches it
    CHECK(host.launchedKey == host.recent[2].key);
    host.launchedKey.clear();
    io.AddMouseButtonEvent(0, true);
    frames(host, shell, 1);
    io.AddMouseButtonEvent(0, false);
    frames(host, shell, 1);
    CHECK(host.launchedKey == host.recent[2].key && host.guardArmed == 2);
    // A resting pointer does not pull the focus back from the pad.
    rightN(host, shell, 1);
    host.launchedKey.clear();
    accept(host, shell);
    CHECK(host.launchedKey == host.recent[3].key);
    // A right-click is Menu: Game details of the tile under the pointer, nothing launched.
    host.launchedKey.clear();
    io.AddMouseButtonEvent(1, true);
    frames(host, shell, 1);
    io.AddMouseButtonEvent(1, false);
    frames(host, shell, 1);
    CHECK(host.launchedKey.empty() && shell.screenId() == "details");
    io.AddMousePosEvent(0, 0);
    frames(host, shell, 2);
}

// Each wheel notch is one shelf; the focused tile of each shelf is kept.
static void homeWheelStepsShelves() {
    FakeHost host;
    host.recent = mkGames("Recent", 4);
    host.favs = mkGames("Fav", 4);
    Shell shell(host);
    homeSetup(host, shell);
    ImGuiIO& io = ImGui::GetIO();
    io.AddMousePosEvent(900, 300);   // over the Base window
    frames(host, shell, 2);
    rightN(host, shell, 2);
    io.AddMouseWheelEvent(0, -1.0f);   // down one notch
    frames(host, shell, 1);
    host.launchedKey.clear();
    accept(host, shell);
    CHECK(host.launchedKey == host.favs[0].key);
    io.AddMouseWheelEvent(0, 1.0f);    // up
    frames(host, shell, 1);
    accept(host, shell);
    CHECK(host.launchedKey == host.recent[2].key);
    io.AddMousePosEvent(0, 0);
    frames(host, shell, 2);
}

// A rescan rebuilt the lists under the screen (a favourite's drive was unplugged): focus stays inside them.
static void homeSurvivesRescan() {
    FakeHost host;
    host.recent = mkGames("Recent", 6);
    host.favs = mkGames("Fav", 2);
    Shell shell(host);
    homeSetup(host, shell);
    rightN(host, shell, 3);
    const int errors = g_imguiErrors;
    std::vector<GameRef> small = {host.recent[4], host.recent[1]};
    for (GameRef& g : small) g.index = -1;
    host.recent = small;
    host.favs.clear();
    frames(host, shell, 3);
    CHECK(g_imguiErrors == errors);
    accept(host, shell);
    CHECK(host.launchedKey == small[1].key || host.launchedKey == small[0].key);
    CHECK(host.launches == 1);
    host.recent.clear();                                    // ... and a library emptied altogether
    frames(host, shell, 3);
    accept(host, shell);
    CHECK(host.launches == 1 && g_imguiErrors == errors);
}

static void homeHeroShowsFocusedGame() {
    FakeHost host;
    host.recent = mkGames("Recent", 3);
    host.recent[1].region = "Europe";
    host.disc = mkGames("Disc", 1, true);
    host.st[host.recent[0].key] = {{3, 1792152000}, {-1, 100}};
    Shell shell(host);
    homeSetup(host, shell);
    g_texts.clear();
    frames(host, shell, 2);
    CHECK(drew("CONTINUE PLAYING") && drew("Recent 0") && drew("USA \xC2\xB7 Cartridge"));
    CHECK(drew("2 save states \xC2\xB7 newest " + widgets::formatDate(1792152000)));
    CHECK(drew("Play") && drew("Details"));
    tapButton(host, shell, SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
    g_texts.clear();
    frames(host, shell, 2);
    CHECK(drew("Europe \xC2\xB7 Cartridge") && drew("No save states") && !drew("Resume last state"));
    down(host, shell);
    g_texts.clear();
    frames(host, shell, 2);
    CHECK(drew("JAGUAR CD") && drew("USA \xC2\xB7 Jaguar CD"));
    host.disc[0].region.clear();
    g_texts.clear();
    frames(host, shell, 2);
    CHECK(drew("Jaguar CD") && !drew(" \xC2\xB7 Jaguar CD"));
}

static void homeEmptyLibraryCard() {
    FakeHost host;
    Shell shell(host);
    g_texts.clear();
    homeSetup(host, shell);
    frames(host, shell, 2);
    CHECK(drew("Your library is empty. Add game folders in desktop mode: View > TV mode turns TV mode off."));
    CHECK(!drew("Play") && !drew("Continue playing"));
    // Every direction and button is harmless.
    for (SDL_GamepadButton b : {SDL_GAMEPAD_BUTTON_DPAD_UP, SDL_GAMEPAD_BUTTON_DPAD_DOWN, SDL_GAMEPAD_BUTTON_DPAD_LEFT,
                                SDL_GAMEPAD_BUTTON_DPAD_RIGHT, SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_WEST,
                                SDL_GAMEPAD_BUTTON_NORTH, SDL_GAMEPAD_BUTTON_START})
        tapButton(host, shell, b);
    CHECK(host.launches == 0 && host.launchedState.first.empty());
}

static void formatDateIsEnglishDayMonthYear() {
#ifndef _WIN32
    setenv("TZ", "UTC", 1);
    tzset();
    CHECK(widgets::formatDate(1792152000) == "16 Oct 2026");
    CHECK(widgets::formatDate(0) == "1 Jan 1970");
#else
    const std::string d = widgets::formatDate(1792152000);
    CHECK(d.find("Oct 2026") != std::string::npos);
#endif
}

static void homeWalk120FramesNoAsserts() {
    ImGuiIO& io = ImGui::GetIO();
    FakeHost host;
    host.tv = true;
    host.loaded = false;
    host.recent = mkGames("Recent", 20);
    host.favs = mkGames("Favourite with a rather long title that cannot fit a tile", 12);
    host.disc = mkGames("Disc", 15, true);
    host.st[host.recent[0].key] = {{2, 1792152000}};
    host.ra = "RetroAchievements: joe \xC2\xB7 Hardcore";
    Shell shell(host);
    const int errors = g_imguiErrors;
    for (float hh : {720.0f, 1080.0f, 2160.0f}) {
        io.DisplaySize = ImVec2(hh * 16 / 9, hh);
        frames(host, shell, 3);
        for (SDL_GamepadButton b : {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, SDL_GAMEPAD_BUTTON_DPAD_DOWN, SDL_GAMEPAD_BUTTON_DPAD_LEFT,
                                    SDL_GAMEPAD_BUTTON_DPAD_UP}) {
            for (int i = 0; i < 10; i++) tapButton(host, shell, b);
            frames(host, shell, 120);
        }
        tapButton(host, shell, SDL_GAMEPAD_BUTTON_START);
        frames(host, shell, 120);
        back(host, shell);                                               // Game details, and out again
    }
    io.DisplaySize = ImVec2(1920, 1080);
    CHECK(g_imguiErrors == errors);
}

// ---- Library (phase 2, Task 9) -----------------------------------------------------------------------------------

// Six titles in sorted order: the fixture the Library tests walk.
static std::vector<GameRef> libraryFixture() {
    const char* titles[] = {"Alien vs Predator", "Atari Karts", "Battlemorph", "Blue Lightning", "Cybermorph", "Doom"};
    std::vector<GameRef> v;
    for (int i = 0; i < 6; i++) {
        GameRef g;
        g.title = titles[i];
        g.key = std::string("/games/") + titles[i] + ".j64";
        g.region = "USA";
        g.index = i;
        v.push_back(g);
    }
    return v;
}

// A host showing the Library (RB from Home), drawn once.
static void librarySetup(FakeHost& host, Shell& shell) {
    host.tv = true;
    host.loaded = false;
    ImGui::GetIO().DisplaySize = ImVec2(1920, 1080);
    frames(host, shell, 2);
    rb(host, shell);
    frames(host, shell, 2);
}

// A trigger pulled all the way and let go: LT / RT are PagePrev / PageNext.
static void pullTrigger(FakeHost& h, Shell& s, SDL_GamepadAxis axis) {
    SDL_Event e;
    SDL_zero(e);
    e.type = SDL_EVENT_GAMEPAD_AXIS_MOTION;
    e.gaxis.axis = (Uint8)axis;
    e.gaxis.value = 32767;
    s.handleEvent(e, g_ms);
    frames(h, s, 1);
    e.gaxis.value = 0;
    s.handleEvent(e, g_ms);
    frames(h, s, 1);
}

static void libraryRailGridHandoff() {
    FakeHost host;
    host.all = libraryFixture();
    Shell shell(host);
    librarySetup(host, shell);
    CHECK(shell.screenId() == "library");
    rightN(host, shell, 2);       // the grid has the focus on entry: index 2
    leftN(host, shell, 3);        // 1, 0, then off the left edge onto the rail (so the place left is 0, not 2)
    down(host, shell);            // Favorites (empty here)
    g_texts.clear();
    frames(host, shell, 2);
    CHECK(drew("Favorites \xC2\xB7 0 games"));
    up(host, shell);              // All games again
    rightN(host, shell, 1);       // back to the grid, at the remembered index
    accept(host, shell);
    CHECK(host.launchedKey == host.all[0].key && host.launches == 1);
    CHECK(host.guardArmed == 1 && host.holdsCleared == 1);
}

// The rail remembers where the grid was left: row 1, column 0 of a longer list.
static void libraryRailKeepsGridPlace() {
    FakeHost host;
    host.all = mkGames("Game", 12);
    host.favs = mkGames("Fav", 3);
    Shell shell(host);
    librarySetup(host, shell);
    down(host, shell);            // index 6
    leftN(host, shell, 1);        // the rail
    down(host, shell);            // Favorites: its own place (0)
    rightN(host, shell, 1);       // onto the grid
    rightN(host, shell, 1);       // Favorites index 1
    leftN(host, shell, 2);        // 0, then the rail again
    up(host, shell);
    rightN(host, shell, 1);
    accept(host, shell);
    CHECK(host.launchedKey == host.all[6].key);
    leftN(host, shell, 1);        // column 0: the rail
    down(host, shell);
    rightN(host, shell, 1);       // Favorites kept its own place: 0
    accept(host, shell);
    CHECK(host.launchedKey == host.favs[0].key);
}

static void libraryLtRtJumpInitial() {
    FakeHost host;
    host.all = libraryFixture();
    Shell shell(host);
    librarySetup(host, shell);
    pullTrigger(host, shell, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
    accept(host, shell);
    CHECK(host.launchedKey == host.all[2].key);   // Battlemorph
    pullTrigger(host, shell, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
    accept(host, shell);
    CHECK(host.launchedKey == host.all[4].key);   // Cybermorph
    pullTrigger(host, shell, SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
    accept(host, shell);
    CHECK(host.launchedKey == host.all[2].key);   // back to Battlemorph
}

static void libraryLetterPickerJumps() {
    FakeHost host;
    host.all = libraryFixture();
    Shell shell(host);
    librarySetup(host, shell);
    tapButton(host, shell, SDL_GAMEPAD_BUTTON_NORTH);
    CHECK(shell.screenId() == "letters");
    g_texts.clear();
    frames(host, shell, 2);
    CHECK(drew("Jump to letter") && drew("Z") && drew("9"));
    rightN(host, shell, 3);       // A -> D
    accept(host, shell);
    CHECK(shell.screenId() == "library");
    accept(host, shell);
    CHECK(host.launchedKey == host.all[5].key);   // Doom
    tapButton(host, shell, SDL_GAMEPAD_BUTTON_NORTH);
    CHECK(shell.screenId() == "letters");         // opens on the focused title's initial: D
    down(host, shell, 4);         // row 4: "YZ0123", column 3 is '1'
    leftN(host, shell, 2);        // 'Z': no title starts with it
    accept(host, shell);
    CHECK(shell.screenId() == "letters");
    back(host, shell);
    CHECK(shell.screenId() == "library");
}

static void libraryTypingFiltersAndBackspace() {
    FakeHost host;
    host.all = libraryFixture();
    Shell shell(host);
    librarySetup(host, shell);
    g_texts.clear();
    frames(host, shell, 2);
    CHECK(drew("All games \xC2\xB7 6 games") && !drew("Filter: d"));
    CHECK(keyEvent(shell, SDL_SCANCODE_D, SDLK_D));
    CHECK(keyEvent(shell, SDL_SCANCODE_O, SDLK_O));
    g_texts.clear();
    frames(host, shell, 2);
    CHECK(drew("All games \xC2\xB7 1 game") && drew("Filter: do"));
    accept(host, shell);
    CHECK(host.launchedKey == host.all[5].key);   // the one match, Doom
    CHECK(keyEvent(shell, SDL_SCANCODE_BACKSPACE, SDLK_BACKSPACE));
    CHECK(keyEvent(shell, SDL_SCANCODE_BACKSPACE, SDLK_BACKSPACE));
    g_texts.clear();
    frames(host, shell, 2);
    CHECK(drew("All games \xC2\xB7 6 games") && !drew("Filter: d"));
    CHECK(shell.screenId() == "library");
    // With nothing to delete Backspace is Back: to Home.
    keyEvent(shell, SDL_SCANCODE_BACKSPACE, SDLK_BACKSPACE);
    keyEvent(shell, SDL_SCANCODE_BACKSPACE, SDLK_BACKSPACE, false);
    frames(host, shell, 2);
    CHECK(shell.screenId() == "home");
}

// A filter is a case-insensitive substring; switching lists drops it; a non-matching filter is an empty grid.
static void libraryFilterSubstringAndListSwitch() {
    FakeHost host;
    host.all = libraryFixture();
    host.favs = {host.all[1], host.all[3]};
    Shell shell(host);
    librarySetup(host, shell);
    rightN(host, shell, 1);                       // Atari Karts: the place the filter must not lose
    keyEvent(shell, SDL_SCANCODE_M, SDLK_M);
    keyEvent(shell, SDL_SCANCODE_O, SDLK_O);
    keyEvent(shell, SDL_SCANCODE_R, SDLK_R);
    g_texts.clear();
    frames(host, shell, 2);
    CHECK(drew("All games \xC2\xB7 2 games"));    // Battlemorph, Cybermorph
    pullTrigger(host, shell, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
    accept(host, shell);
    CHECK(host.launchedKey == host.all[4].key);   // the jump works inside the filtered list
    keyEvent(shell, SDL_SCANCODE_Z, SDLK_Z);
    g_texts.clear();
    frames(host, shell, 2);
    CHECK(drew("All games \xC2\xB7 0 games") && drew("Filter: morz"));
    accept(host, shell);                          // nothing to launch
    CHECK(host.launches == 1);
    leftN(host, shell, 1);                        // an empty grid still lets focus back to the rail
    down(host, shell);
    g_texts.clear();
    frames(host, shell, 2);
    CHECK(drew("Favorites \xC2\xB7 2 games") && !drew("Filter: morz"));
    // The place in All games was kept while the filter was on, and comes back after the switch.
    up(host, shell);
    rightN(host, shell, 1);
    accept(host, shell);
    CHECK(host.launchedKey == host.all[1].key);
}

// Holding Backspace clears the filter and stops there: the repeats after it ran out are dropped, only a fresh press is Back.
static void libraryHeldBackspaceStopsAtEmptyFilter() {
    FakeHost host;
    host.all = libraryFixture();
    Shell shell(host);
    librarySetup(host, shell);
    keyEvent(shell, SDL_SCANCODE_D, SDLK_D);
    keyEvent(shell, SDL_SCANCODE_O, SDLK_O);
    CHECK(keyEvent(shell, SDL_SCANCODE_BACKSPACE, SDLK_BACKSPACE, true, SDL_KMOD_NONE, false));   // "d"
    CHECK(keyEvent(shell, SDL_SCANCODE_BACKSPACE, SDLK_BACKSPACE, true, SDL_KMOD_NONE, true));    // ""
    CHECK(keyEvent(shell, SDL_SCANCODE_BACKSPACE, SDLK_BACKSPACE, true, SDL_KMOD_NONE, true));    // dropped
    CHECK(keyEvent(shell, SDL_SCANCODE_BACKSPACE, SDLK_BACKSPACE, true, SDL_KMOD_NONE, true));
    g_texts.clear();
    frames(host, shell, 2);
    CHECK(shell.screenId() == "library" && drew("All games \xC2\xB7 6 games"));
    keyEvent(shell, SDL_SCANCODE_BACKSPACE, SDLK_BACKSPACE, false);
    CHECK(keyEvent(shell, SDL_SCANCODE_BACKSPACE, SDLK_BACKSPACE, true, SDL_KMOD_NONE, false));   // a fresh press: Back
    frames(host, shell, 2);
    CHECK(shell.screenId() == "home");
}

static void libraryTypingLeavesHotkeys() {
    FakeHost host;
    host.all = libraryFixture();
    Shell shell(host);
    librarySetup(host, shell);
    CHECK(!keyEvent(shell, SDL_SCANCODE_D, SDLK_D, true, SDL_KMOD_CTRL));
    CHECK(!keyEvent(shell, SDL_SCANCODE_F4, SDLK_F4));
    g_texts.clear();
    frames(host, shell, 2);
    CHECK(drew("All games \xC2\xB7 6 games"));   // neither typed anything
}

// The pointer: hovering a rail item selects it, hovering a tile focuses it, a click plays it, the wheel steps rows.
static void libraryMouse() {
    FakeHost host;
    for (int i = 0; i < 20; i++) {
        GameRef g;
        g.title = "Game " + std::string(1, (char)('A' + i));
        g.key = "/games/" + g.title + ".j64";
        g.index = i;
        host.all.push_back(g);
    }
    host.favs = {host.all[7], host.all[8]};
    Shell shell(host);
    librarySetup(host, shell);
    ImGuiIO& io = ImGui::GetIO();
    io.AddMousePosEvent(96 + 100, 150 + 62 + 27);          // the second rail item: Favorites
    frames(host, shell, 2);
    g_texts.clear();
    frames(host, shell, 2);
    CHECK(drew("Favorites \xC2\xB7 2 games"));
    io.AddMousePosEvent(96 + 100, 150 + 27);               // ... and the first again
    frames(host, shell, 2);
    io.AddMousePosEvent(504 + 225 + 96, 330);              // column 1 of the first row
    frames(host, shell, 2);
    accept(host, shell);
    CHECK(host.launchedKey == host.all[1].key);
    host.launchedKey.clear();
    io.AddMouseButtonEvent(0, true);
    frames(host, shell, 1);
    io.AddMouseButtonEvent(0, false);
    frames(host, shell, 1);
    CHECK(host.launchedKey == host.all[1].key && host.guardArmed == 2);
    io.AddMouseWheelEvent(0, -1.0f);                       // one notch down: one row (six titles)
    frames(host, shell, 1);
    host.launchedKey.clear();
    accept(host, shell);
    CHECK(host.launchedKey == host.all[7].key);
    io.AddMouseWheelEvent(0, 1.0f);
    frames(host, shell, 1);
    accept(host, shell);
    CHECK(host.launchedKey == host.all[1].key);
    io.AddMousePosEvent(0, 0);
    frames(host, shell, 2);
}

static void libraryFocusRememberedAcrossVisits() {
    FakeHost host;
    host.all = libraryFixture();
    Shell shell(host);
    librarySetup(host, shell);
    rightN(host, shell, 3);
    lb(host, shell);                                       // Home
    rb(host, shell);                                       // a fresh Library screen
    accept(host, shell);
    CHECK(host.launchedKey == host.all[3].key);
}

static void libraryWalk120FramesNoAsserts() {
    ImGuiIO& io = ImGui::GetIO();
    FakeHost host;
    host.tv = true;
    host.loaded = false;
    for (int i = 0; i < 70; i++) {
        GameRef g;
        g.title = std::string(1, (char)('A' + i % 26)) + " game number " + std::to_string(i) + " with a rather long title";
        g.key = "/games/" + std::to_string(i) + ".j64";
        g.index = i;
        g.disc = i % 5 == 0;
        g.region = i % 3 ? "Europe" : "";
        host.all.push_back(g);
    }
    host.favs = {host.all[3], host.all[9]};
    host.disc = mkGames("Disc", 8, true);
    host.st[host.all[0].key] = {{1, 1792152000}, {-1, 100}};
    Shell shell(host);
    const int errors = g_imguiErrors;
    for (float hh : {720.0f, 1080.0f, 2160.0f}) {
        io.DisplaySize = ImVec2(hh * 16 / 9, hh);
        frames(host, shell, 3);
        rb(host, shell);
        for (SDL_GamepadButton b : {SDL_GAMEPAD_BUTTON_DPAD_DOWN, SDL_GAMEPAD_BUTTON_DPAD_RIGHT, SDL_GAMEPAD_BUTTON_DPAD_UP,
                                    SDL_GAMEPAD_BUTTON_DPAD_LEFT}) {
            for (int i = 0; i < 10; i++) tapButton(host, shell, b);
            frames(host, shell, 120);
        }
        for (int i = 0; i < 4; i++) {                                    // the rail
            down(host, shell);
            frames(host, shell, 10);
        }
        up(host, shell, 3);
        rightN(host, shell, 1);
        tapButton(host, shell, SDL_GAMEPAD_BUTTON_NORTH);                // the picker
        rightN(host, shell, 5);
        down(host, shell, 5);
        frames(host, shell, 120);
        CHECK(shell.screenId() == "letters");
        back(host, shell);
        keyEvent(shell, SDL_SCANCODE_G, SDLK_G);                         // the filter
        keyEvent(shell, SDL_SCANCODE_A, SDLK_A);
        keyEvent(shell, SDL_SCANCODE_Z, SDLK_Z);
        frames(host, shell, 120);
        for (int i = 0; i < 3; i++) keyEvent(shell, SDL_SCANCODE_BACKSPACE, SDLK_BACKSPACE);
        frames(host, shell, 120);
        tapButton(host, shell, SDL_GAMEPAD_BUTTON_START);
        frames(host, shell, 30);
        back(host, shell);                                               // Game details, and out again
        io.AddMousePosEvent(io.DisplaySize.x * 0.6f, io.DisplaySize.y * 0.4f);
        io.AddMouseWheelEvent(0, -2.0f);
        frames(host, shell, 30);
        io.AddMousePosEvent(0, 0);
        frames(host, shell, 3);
        lb(host, shell);
    }
    io.DisplaySize = ImVec2(1920, 1080);
    CHECK(g_imguiErrors == errors);
}

// ---- Game details and the TV-mode row (phase 2, Task 10) --------------------------------------------------------------

static void rightClick(FakeHost& h, Shell& s) {
    ImGuiIO& io = ImGui::GetIO();
    io.AddMouseButtonEvent(1, true);
    frames(h, s, 1);
    io.AddMouseButtonEvent(1, false);
    frames(h, s, 1);
}

// Home with three recent games, on the second one's Game details.
static void detailsSetup(FakeHost& host, Shell& shell) {
    host.recent = mkGames("Recent", 3);
    host.all = host.recent;
    homeSetup(host, shell);
    rightN(host, shell, 1);
    tapButton(host, shell, SDL_GAMEPAD_BUTTON_START);
    frames(host, shell, 2);
}

static int resumeRowsDrawn() {
    int n = 0;
    for (const std::string& t : g_texts)
        if (t.rfind("Resume: ", 0) == 0) n++;
    return n;
}

static void detailsFromStartOnAnyTile() {
    {
        FakeHost host;
        host.recent = mkGames("Recent", 3);
        host.all = host.recent;
        Shell shell(host);
        homeSetup(host, shell);
        rightN(host, shell, 1);
        tapButton(host, shell, SDL_GAMEPAD_BUTTON_START);
        CHECK(shell.screenId() == "details");
        back(host, shell);
        CHECK(shell.screenId() == "home");
        accept(host, shell);                                   // the focus survived the round trip
        CHECK(host.launchedKey == host.recent[1].key);
    }
    {
        FakeHost host;
        host.all = libraryFixture();
        Shell shell(host);
        librarySetup(host, shell);
        ImGuiIO& io = ImGui::GetIO();
        io.AddMousePosEvent(504 + 225 + 96, 330);              // column 1 of the first row
        frames(host, shell, 2);
        rightClick(host, shell);
        CHECK(shell.screenId() == "details" && host.launchedKey.empty());
        back(host, shell);
        CHECK(shell.screenId() == "library");
        io.AddMousePosEvent(0, 0);
        frames(host, shell, 2);
        tapButton(host, shell, SDL_GAMEPAD_BUTTON_START);      // ... and so does Start on a pad-focused tile
        CHECK(shell.screenId() == "details");
    }
}

static void detailsResumeRowsMatchStates() {
    FakeHost host;
    host.recent = mkGames("Recent", 3);
    host.all = host.recent;
    host.st[host.recent[1].key] = {{3, 1792152000}, {-1, 1792000000}};
    Shell shell(host);
    homeSetup(host, shell);
    rightN(host, shell, 1);
    tapButton(host, shell, SDL_GAMEPAD_BUTTON_START);
    g_texts.clear();
    frames(host, shell, 1);
    CHECK(resumeRowsDrawn() == 2 && drew("Resume: Slot 3") && drew("Resume: Auto-save"));
    CHECK(drew("Play") && drew("Favorite: Off") && drew("Select") && drew("Back"));
    CHECK(drew("Recent 1") && drew("Europe \xC2\xB7 Cartridge"));
    CHECK(drew(widgets::formatDate(1792152000)));              // slot 3
    host.canLoad = false;                                      // hardcore refuses loads: no Resume rows at all
    g_texts.clear();
    frames(host, shell, 1);
    CHECK(resumeRowsDrawn() == 0 && drew("Play"));
}

static void detailsResumeLaunchesThatState() {
    FakeHost host;
    host.recent = mkGames("Recent", 3);
    host.all = host.recent;
    host.st[host.recent[1].key] = {{3, 1792152000}, {-1, 1792000000}};
    Shell shell(host);
    homeSetup(host, shell);
    rightN(host, shell, 1);
    tapButton(host, shell, SDL_GAMEPAD_BUTTON_START);
    down(host, shell, 2);                                      // Play, Resume: Slot 3, Resume: Auto-save
    accept(host, shell);
    CHECK(host.launchedState == std::make_pair(host.recent[1].key, -1) && host.guardArmed == 1 && host.launches == 0);
    CHECK(shell.screenId() == "details");                      // closing the game lands back here
}

static void detailsPlaylistSameSlotsLaunchCorrectDisc() {
    FakeHost host;
    host.recent = mkGames("Recent", 3);
    host.all = host.recent;
    host.st[host.recent[1].key] = {{3, 1792152000, "/set/Disc 1.cue"}, {3, 1792000000, "/set/Disc 2.cue"}};
    Shell shell(host);
    homeSetup(host, shell);
    rightN(host, shell, 1);
    tapButton(host, shell, SDL_GAMEPAD_BUTTON_START);
    down(host, shell, 2);                                      // Play, Disc 1 slot 3, Disc 2 slot 3
    accept(host, shell);
    CHECK(host.launchedState == std::make_pair(host.recent[1].key, 3) && host.guardArmed == 1 && host.launches == 0);
    CHECK(host.launchedDisc == "/set/Disc 2.cue");
    CHECK(shell.screenId() == "details");                      // closing the game lands back here
}

static void detailsFavoriteToggles() {
    FakeHost host;
    Shell shell(host);
    detailsSetup(host, shell);
    g_texts.clear();
    frames(host, shell, 1);
    CHECK(drew("Favorite: Off") && !drew("Favorite: On"));
    down(host, shell);                                         // Play, Favorite (no states)
    accept(host, shell);
    CHECK(host.favSet.count(host.recent[1].key) == 1);
    g_texts.clear();
    frames(host, shell, 1);
    CHECK(drew("Favorite: On") && !drew("Favorite: Off"));
    accept(host, shell);
    CHECK(host.favSet.empty() && host.launches == 0);
    CHECK(shell.screenId() == "details");
}

static void detailsPlayLaunches() {
    FakeHost host;
    Shell shell(host);
    detailsSetup(host, shell);
    accept(host, shell);
    CHECK(host.launchedKey == host.recent[1].key && host.guardArmed == 1);
}

// A rescan dropped the game: the title and Back are all that is left, and A does nothing.
static void detailsSurvivesGameVanishing() {
    FakeHost host;
    Shell shell(host);
    detailsSetup(host, shell);
    host.recent.clear();
    host.all.clear();
    g_texts.clear();
    frames(host, shell, 2);
    CHECK(drew("Recent 1") && !drew("Play") && !drew("Favorite: Off"));
    accept(host, shell);
    down(host, shell);
    accept(host, shell);
    CHECK(host.launches == 0 && host.favSet.empty() && host.guardArmed == 0);
    back(host, shell);
    CHECK(shell.screenId() == "home");
}

static void detailsMouse() {
    FakeHost host;
    Shell shell(host);
    detailsSetup(host, shell);
    ImGuiIO& io = ImGui::GetIO();
    io.AddMousePosEvent(96 + 266 + 56 + 300, 274 + 72 + 12 + 30);   // the Favorite row (second)
    frames(host, shell, 2);
    io.AddMouseButtonEvent(0, true);
    frames(host, shell, 1);
    io.AddMouseButtonEvent(0, false);
    frames(host, shell, 1);
    CHECK(host.favSet.count(host.recent[1].key) == 1 && host.launches == 0);
    io.AddMousePosEvent(0, 0);
    frames(host, shell, 2);
}

// Every size, with a full set of states (the list scrolls), through every direction and the pointer.
static void detailsWalk120FramesNoAsserts() {
    ImGuiIO& io = ImGui::GetIO();
    FakeHost host;
    host.tv = true;
    host.loaded = false;
    host.recent = mkGames("Recent", 3);
    host.all = host.recent;
    for (int i = 0; i < 10; i++) host.st[host.recent[0].key].push_back({i, 1792152000 - i * 86400});
    host.st[host.recent[0].key].push_back({-1, 1700000000});
    Shell shell(host);
    const int errors = g_imguiErrors;
    for (float hh : {720.0f, 1080.0f, 2160.0f}) {
        io.DisplaySize = ImVec2(hh * 16 / 9, hh);
        frames(host, shell, 3);
        tapButton(host, shell, SDL_GAMEPAD_BUTTON_START);
        CHECK(shell.screenId() == "details");
        for (int i = 0; i < 14; i++) {
            down(host, shell);
            frames(host, shell, 5);
        }
        io.AddMousePosEvent(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.6f);
        io.AddMouseWheelEvent(0, 3.0f);
        frames(host, shell, 20);
        up(host, shell, 14);
        frames(host, shell, 30);
        io.AddMousePosEvent(0, 0);
        frames(host, shell, 2);
        back(host, shell);
        CHECK(shell.screenId() == "home");
    }
    io.DisplaySize = ImVec2(1920, 1080);
    CHECK(g_imguiErrors == errors);
}

static void ingameTvToggleHiddenWhenOff() {
    FakeHost host;
    host.loaded = true;
    host.tvOn = false;                                         // the setting is Off: no way into TV mode
    Shell shell(host);
    shell.openMenu();
    g_texts.clear();
    frames(host, shell, 3);
    for (const Shell::Row& r : shell.menuRows()) CHECK(r.label.find("TV mode") == std::string::npos);
    CHECK(!drew("Enter TV mode") && !drew("Exit TV mode"));
    shell.closeMenu();

    host.tvOn = true;
    host.tv = false;
    shell.openMenu();
    g_texts.clear();
    frames(host, shell, 3);
    CHECK(drew("Enter TV mode") && !drew("Exit TV mode"));
    down(host, shell, 4);                                      // Resume, Save state, Load state, Controls, Enter TV mode
    CHECK(std::string(focusedLabel(shell)) == "Enter TV mode");
    accept(host, shell);
    CHECK(host.tv && shell.menuOpen() && shell.menuPage() == Shell::Page::Root);
    CHECK(std::string(focusedLabel(shell)) == "Exit TV mode");   // the focus stayed on the row, its label flipped
    g_texts.clear();
    frames(host, shell, 3);
    CHECK(drew("Exit TV mode") && !drew("Enter TV mode"));
    accept(host, shell);
    CHECK(!host.tv && shell.menuOpen() && std::string(focusedLabel(shell)) == "Enter TV mode");
}

// ---- the final-review fix wave (#991) --------------------------------------------------------------------------------

// Launching a game moves it to the front of Recent.  The player who comes back lands on the game they left on, not on
// whatever now sits at the same position (I3).
static void moveToFront(std::vector<GameRef>& v, size_t i) {
    GameRef g = v[i];
    v.erase(v.begin() + (long)i);
    v.insert(v.begin(), g);
}
static void gameRunsAndCloses(FakeHost& host, Shell& shell) {
    host.loaded = true;
    frames(host, shell, 4);
    host.loaded = false;
    frames(host, shell, 3);
}

static void homeReturnsToPlayedGame() {
    FakeHost host;
    host.recent = mkGames("Recent", 6);
    host.favs = mkGames("Fav", 4);
    Shell shell(host);
    homeSetup(host, shell);
    rightN(host, shell, 3);
    const std::string played = host.recent[3].key;
    accept(host, shell);
    CHECK(host.launchedKey == played);
    moveToFront(host.recent, 3);                       // what noteLaunched does
    CHECK(host.recent[0].key == played);
    gameRunsAndCloses(host, shell);
    host.launchedKey.clear();
    accept(host, shell);
    CHECK(host.launchedKey == played);                 // the same game (the old index 3 is now the game that was at 2)
    // A different shelf kept its own game across the same reorder.
    down(host, shell);
    rightN(host, shell, 2);
    const std::string fav = host.favs[2].key;
    moveToFront(host.recent, 4);
    gameRunsAndCloses(host, shell);
    accept(host, shell);
    CHECK(host.launchedKey == fav);
    // A game that vanished from the list falls back to the clamped index.
    up(host, shell);                                   // back on Recent, on `played` (index 0)
    rightN(host, shell, 5);                            // the last of six
    const std::string last = host.recent[5].key;
    host.recent.pop_back();                            // ... gone
    host.recent.pop_back();                            // and one more: the clamp lands on the new last game
    gameRunsAndCloses(host, shell);
    accept(host, shell);
    CHECK(host.launchedKey == host.recent.back().key && host.launchedKey != last);
}

static void libraryReturnsToPlayedGame() {
    FakeHost host;
    host.recent = mkGames("Recent", 8);
    Shell shell(host);
    librarySetup(host, shell);
    down(host, shell, 2);                              // the rail: All games -> Favorites -> Recently played
    accept(host, shell);                               // into the grid
    rightN(host, shell, 3);
    const std::string played = host.recent[3].key;
    accept(host, shell);
    CHECK(host.launchedKey == played);
    moveToFront(host.recent, 3);
    gameRunsAndCloses(host, shell);
    host.launchedKey.clear();
    accept(host, shell);
    CHECK(host.launchedKey == played);
    // Still so when the screen is built again (another section and back): the memory holds the game too.
    moveToFront(host.recent, 5);
    lb(host, shell);
    rb(host, shell);
    accept(host, shell);
    CHECK(host.launchedKey == host.recent[0].key || host.launchedKey == played);
    CHECK(host.launchedKey == played);
}

// Typing a filter still starts at the filtered list's first game, and clearing it returns to the remembered game.
static void libraryFilterIgnoresRememberedGame() {
    FakeHost host;
    host.all = libraryFixture();
    Shell shell(host);
    librarySetup(host, shell);
    rightN(host, shell, 4);                            // Cybermorph
    keyEvent(shell, SDL_SCANCODE_B, SDLK_B);
    keyEvent(shell, SDL_SCANCODE_L, SDLK_L);           // "bl": Blue Lightning only
    accept(host, shell);
    CHECK(host.launchedKey == host.all[3].key);
    keyEvent(shell, SDL_SCANCODE_BACKSPACE, SDLK_BACKSPACE);
    keyEvent(shell, SDL_SCANCODE_BACKSPACE, SDLK_BACKSPACE, false);
    keyEvent(shell, SDL_SCANCODE_BACKSPACE, SDLK_BACKSPACE);
    keyEvent(shell, SDL_SCANCODE_BACKSPACE, SDLK_BACKSPACE, false);
    frames(host, shell, 2);
    accept(host, shell);
    CHECK(host.launchedKey == host.all[4].key);        // Cybermorph again
}

// M1: a desktop text field over the Base layer keeps the keyboard captured.
static void baseLeavesTextFieldTheKeyboard() {
    FakeHost host;
    host.tv = true;
    host.loaded = false;
    Shell shell(host);
    frames(host, shell, 2);
    ImGuiIO& io = ImGui::GetIO();
    char buf[16] = "";
    auto boxFrame = [&]() {
        io.DeltaTime = 1.0f / 60.0f;
        ImGui::NewFrame();
        shell.frame(g_ms);
        shell.draw(Layer::Base, ImVec2(0, 0), io.DisplaySize);
        ImGui::Begin("box");
        ImGui::SetKeyboardFocusHere();
        ImGui::InputText("##t", buf, sizeof buf);
        ImGui::End();
        ImGui::Render();
    };
    for (int i = 0; i < 4; i++) boxFrame();
    CHECK(io.WantTextInput && io.WantCaptureKeyboard);   // (the Base window used to force this false)
    frames(host, shell, 4);                              // the box is gone: the Base layer never wants the keyboard again
    CHECK(!io.WantTextInput && !io.WantCaptureKeyboard);
}

// M2: an OS key repeat is never a fresh press.
static void keyRepeatNeverActsAsPress() {
    FakeHost host;
    host.recent = mkGames("Recent", 5);
    Shell shell(host);
    homeSetup(host, shell);
    // Enter held across a game: the repeat that arrives on the reset nav does not relaunch.
    gameRunsAndCloses(host, shell);
    CHECK(keyEvent(shell, SDL_SCANCODE_RETURN, SDLK_RETURN, true, SDL_KMOD_NONE, true));   // consumed ...
    frames(host, shell, 3);
    CHECK(host.launches == 0);                                                              // ... and not an Accept
    keyEvent(shell, SDL_SCANCODE_RETURN, SDLK_RETURN, false);
    CHECK(keyEvent(shell, SDL_SCANCODE_RETURN, SDLK_RETURN));                               // a real press does
    frames(host, shell, 2);
    keyEvent(shell, SDL_SCANCODE_RETURN, SDLK_RETURN, false);
    frames(host, shell, 2);
    CHECK(host.launches == 1);
    // A direction: the press moves once; OS repeats of it do not move it again (NavInput's own timer repeats it).
    CHECK(keyEvent(shell, SDL_SCANCODE_RIGHT, SDLK_RIGHT));
    frames(host, shell, 2);
    for (int i = 0; i < 3; i++) {
        CHECK(keyEvent(shell, SDL_SCANCODE_RIGHT, SDLK_RIGHT, true, SDL_KMOD_NONE, true));
        frames(host, shell, 1);
    }
    keyEvent(shell, SDL_SCANCODE_RIGHT, SDLK_RIGHT, false);
    frames(host, shell, 2);
    accept(host, shell);
    CHECK(host.launchedKey == host.recent[1].key);
    // A repeat of a key that is no nav key is left to the app, as before.
    CHECK(!keyEvent(shell, SDL_SCANCODE_F4, SDLK_F4, true, SDL_KMOD_NONE, true));
    // The in-game menu too: a repeat does not press Resume.
    FakeHost game;
    Shell menu(game);
    menu.openMenu();
    frames(game, menu, 2);
    CHECK(keyEvent(menu, SDL_SCANCODE_RETURN, SDLK_RETURN, true, SDL_KMOD_NONE, true));
    frames(game, menu, 3);
    CHECK(menu.menuOpen());
}

// M3: an empty grid lists no action that does nothing.
static void libraryEmptyGridHintsOnlyBack() {
    FakeHost host;
    Shell shell(host);
    librarySetup(host, shell);                          // no games at all
    g_texts.clear();
    frames(host, shell, 3);
    CHECK(shell.screenId() == "library");
    CHECK(!drew("Play") && !drew("Details") && !drew("Jump to letter") && !drew("A\xE2\x80\x93Z") && !drew("Open"));
    CHECK(drew("Home"));
    // A filter that matches nothing, with the focus on the grid (rail and grid both show nothing playable).
    FakeHost host2;
    host2.all = libraryFixture();
    Shell shell2(host2);
    librarySetup(host2, shell2);
    g_texts.clear();
    frames(host2, shell2, 2);
    CHECK(drew("Play") && drew("Details") && drew("Jump to letter"));   // the full set with games (the fake pad has no triggers: no A-Z hint)
    keyEvent(shell2, SDL_SCANCODE_Q, SDLK_Q);
    keyEvent(shell2, SDL_SCANCODE_Z, SDLK_Z);
    keyEvent(shell2, SDL_SCANCODE_Z, SDLK_Z);
    g_texts.clear();
    frames(host2, shell2, 3);
    CHECK(drew("No games match"));
    CHECK(!drew("Play") && !drew("Details") && !drew("Jump to letter") && !drew("A\xE2\x80\x93Z"));
}

// M7: an empty library that is still being scanned is not "empty".
static void homeSaysScanningWhileScanning() {
    const char* empty = "Your library is empty. Add game folders in desktop mode: View > TV mode turns TV mode off.";
    FakeHost host;
    host.scanning = true;
    Shell shell(host);
    g_texts.clear();
    homeSetup(host, shell);
    frames(host, shell, 2);
    CHECK(drew("Scanning your library...") && !drew(empty));
    host.scanning = false;                              // the scan found nothing
    g_texts.clear();
    frames(host, shell, 2);
    CHECK(!drew("Scanning your library...") && drew(empty));
    host.scanning = true;                               // a rescan with games already listed keeps showing them
    host.recent = mkGames("Recent", 2);
    g_texts.clear();
    frames(host, shell, 2);
    CHECK(drew("Continue playing") && !drew("Scanning your library..."));
}

// M11: a desktop window over the Base layer takes the pad from it.
static void desktopWindowTakesInputFromBase() {
    FakeHost host;
    host.recent = mkGames("Recent", 4);
    Shell shell(host);
    homeSetup(host, shell);
    CHECK(shell.ownsInput());
    host.desktopOpen = true;
    CHECK(!shell.ownsInput());
    SDL_Event e;
    SDL_zero(e);
    e.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
    e.gbutton.button = SDL_GAMEPAD_BUTTON_SOUTH;
    e.gbutton.down = true;
    CHECK(!shell.handleEvent(e, g_ms));                // the app (and ImGui) get the A
    e.type = SDL_EVENT_GAMEPAD_BUTTON_UP;
    e.gbutton.down = false;
    shell.handleEvent(e, g_ms);
    frames(host, shell, 3);
    accept(host, shell);
    rb(host, shell);
    CHECK(host.launches == 0 && shell.section() == Section::Home);
    CHECK(!keyEvent(shell, SDL_SCANCODE_RETURN, SDLK_RETURN));
    // ... and it still draws (the TV screens stay visible behind the window).
    CHECK(baseWindow() && baseWindow()->WasActive);
    // The window opens between a press and the frame that would act on it: that A is not a launch.
    host.desktopOpen = false;
    frames(host, shell, 2);
    e.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
    e.gbutton.down = true;
    CHECK(shell.handleEvent(e, g_ms));
    host.desktopOpen = true;
    frames(host, shell, 3);
    CHECK(host.launches == 0);
    e.type = SDL_EVENT_GAMEPAD_BUTTON_UP;
    e.gbutton.down = false;
    shell.handleEvent(e, g_ms);
    // Closed again: the screens have the input back.
    host.desktopOpen = false;
    frames(host, shell, 2);
    CHECK(shell.ownsInput());
    accept(host, shell);
    CHECK(host.launches == 1 && host.launchedKey == host.recent[0].key);
    // The in-game menu is not the Base layer: a window next to it does not take its input.
    FakeHost game;
    Shell menu(game);
    menu.openMenu();
    game.desktopOpen = true;
    CHECK(menu.ownsInput());
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
    baseOnlyWithoutGame();   // first: ImGui remembers a window it has seen
    menuOpensAndWalks();
    menuRefusedInHardcore();
    loadRowsHiddenInHardcore();
    backResumes();
    menuKeepsUserPause();
    closeGameAsksFirst();
    netplayBanner();
    hintBarShowsWhatWorks();
    pointerFreeWhilePlaying();
    controlsRowShowsNeverHides();
    wheelReachesLastSlots();
    hintBarKeepsBack();
    tvShellWalksSections();
    escBackThenRoot();
    baseNeverCapturesKeyboard();
    padHidesCursor();
    baseReturnsAfterGame();
    raStatusHiddenWhenEmpty();
    screenStackRoutesInput();
    textBoxOwnsTyping();
    screenStackDrawsModalsOverScreen();
    screenMayPopItselfWhileDrawing();
    startStaysAppsWithGame();
    navForgetsAcrossGame();
    tabClickSwitchesSection();
    launchedArmsGuard();
    homeHidesEmptyShelves();
    homeHidesShelfThatCannotFit();
    hintsListOnlyWhatWorks();
    homeAcceptLaunchesFocused();
    switchPadAcceptsEast();
    desktopIntentsAreNotTvs();
    homeXResumesNewest();
    homeResumeHiddenWithoutStatesOrInHardcore();
    homeFocusRememberedPerShelf();
    homeEdgesDoNothing();
    homeMouseHoverFocusesAndClicks();
    homeWheelStepsShelves();
    homeSurvivesRescan();
    homeHeroShowsFocusedGame();
    homeEmptyLibraryCard();
    formatDateIsEnglishDayMonthYear();
    homeWalk120FramesNoAsserts();
    libraryRailGridHandoff();
    libraryRailKeepsGridPlace();
    libraryLtRtJumpInitial();
    libraryLetterPickerJumps();
    libraryTypingFiltersAndBackspace();
    libraryFilterSubstringAndListSwitch();
    libraryHeldBackspaceStopsAtEmptyFilter();
    libraryTypingLeavesHotkeys();
    libraryMouse();
    libraryFocusRememberedAcrossVisits();
    libraryWalk120FramesNoAsserts();
    detailsFromStartOnAnyTile();
    detailsResumeRowsMatchStates();
    detailsResumeLaunchesThatState();
    detailsPlaylistSameSlotsLaunchCorrectDisc();
    detailsFavoriteToggles();
    detailsPlayLaunches();
    detailsSurvivesGameVanishing();
    detailsMouse();
    detailsWalk120FramesNoAsserts();
    ingameTvToggleHiddenWhenOff();
    homeReturnsToPlayedGame();
    libraryReturnsToPlayedGame();
    libraryFilterIgnoresRememberedGame();
    baseLeavesTextFieldTheKeyboard();
    keyRepeatNeverActsAsPress();
    libraryEmptyGridHintsOnlyBack();
    homeSaysScanningWhileScanning();
    desktopWindowTakesInputFromBase();
    padPopupNav();
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
    // The Base layer (chrome and the stub screens) at the same sizes, signed in to RetroAchievements (the longest top bar).
    {
        FakeHost host;
        host.tv = true;
        host.loaded = false;
        host.ra = "RetroAchievements: a_rather_long_user_name \xC2\xB7 Hardcore";
        Shell shell(host);
        for (float hh : {720.0f, 1080.0f, 2160.0f}) {
            io.DisplaySize = ImVec2(hh * 16 / 9, hh);
            frames(host, shell, 3);
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
