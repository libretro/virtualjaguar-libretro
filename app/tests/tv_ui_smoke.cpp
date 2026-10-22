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
        case Intent::Down:
        case Intent::Left:
        case Intent::Right: return "D-pad";
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
    bool hasGames() const override { return !all.empty(); }
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
    // TV Settings (phase 3): the rows the screen lists, every change it asked for, and the "More settings" presses.
    std::vector<SettingRow> rows;
    std::vector<std::pair<std::string, int>> set;
    int moreOpened = 0;
    std::vector<SettingRow> settingRows() override { return rows; }
    void setSetting(const std::string& id, int index) override {
        set.push_back({id, index});
        for (SettingRow& r : rows)
            if (r.id == id) r.index = index;
        if (id == "tv_mode" && index == 0) tv = false;   // Off: the app's configureTvMode() turns TV mode off
    }
    void openMoreSettings() override { moreOpened++; }
    // TV Achievements (phase 3): the header, the loaded game's rows, the RA games list, the play it asked for, and which
    // library games the list knows (GameRef::key -> list id).
    AchHeader hdr;
    std::vector<AchItem> items;
    std::vector<AchGame> raGames;
    uint32_t played = 0;
    std::map<std::string, uint32_t> achIds;
    AchHeader achHeader() override { return hdr; }
    std::vector<AchItem> achItems() override { return hdr.hasGame ? items : std::vector<AchItem>(); }
    std::vector<AchGame> achGames() override { return raGames; }
    Art achImage(const std::string&) override { Art a; a.loading = true; return a; }
    void achPlay(uint32_t id) override { played = id; }
    uint32_t achGameId(const GameRef& g) override {
        auto it = achIds.find(g.key);
        return it == achIds.end() ? 0 : it->second;
    }
    // Overlays (phase 3): the rows the screen lists and every id it asked to toggle (an enabled row flips, as the app's does).
    std::vector<ToggleRow> overlays;
    std::vector<std::string> toggled;
    std::vector<ToggleRow> overlayRows() override { return overlays; }
    void toggleOverlay(const std::string& id) override {
        toggled.push_back(id);
        for (ToggleRow& r : overlays)
            if (r.id == id && r.enabled) r.on = !r.on;
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
    down(host, shell, 12);  // runs off the end: focus never wraps
    CHECK(std::string(focusedLabel(shell)) == "Close game");
    up(host, shell, 12);
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
    down(host, shell, 6);   // Achievements, Overlays, Settings, Controls, Enter TV mode, Reset
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
    down(host, shell, 9);
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
    down(host, shell, 6);   // Resume, Save state, Load state, Achievements, Overlays, Settings, Controls
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
    rb(host, shell);
    CHECK(shell.screenId() == "achievements" && shell.section() == Section::Achievements);
    rb(host, shell);
    CHECK(shell.screenId() == "settings" && shell.section() == Section::Settings);
    rb(host, shell);   // no wrap
    CHECK(shell.screenId() == "settings");
    lb(host, shell);
    CHECK(shell.screenId() == "achievements");
    lb(host, shell);
    CHECK(shell.screenId() == "library");
    lb(host, shell);
    CHECK(shell.screenId() == "home");
    lb(host, shell);
    CHECK(shell.screenId() == "home");
    frames(host, shell, 120);
    CHECK(g_imguiErrors == 0 && drew("Home") && drew("Library") && drew("Achievements") && drew("Settings") && drew("8:42 PM"));
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

// "More settings" from TV mode (#991): the pad (deskSetPopupNav + our intents) and the keyboard (deskSetKeyboardNav) both drive a
// plain ImGui window.  A focused probe with two buttons: Down moves from the first to the second, Accept presses it; the keyboard
// is captured exactly while the window is live (F4 and Ctrl+Shift+T are gated on it), and both flags go with the window.
static void moreSettingsNavFlags() {
    ImGuiIO& io = ImGui::GetIO();
    const ImGuiConfigFlags savedFlags = io.ConfigFlags;
    io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;   // as the app has it (initImGui)
    int first = 0, second = 0;
    auto frame = [&](bool live, bool focus) {
        io.DeltaTime = 1.0f / 60.0f;
        vj::deskSetPopupNav(live);
        vj::deskSetKeyboardNav(live);
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(40, 40));
        ImGui::SetNextWindowSize(ImVec2(300, 200));
        if (focus) ImGui::SetNextWindowFocus();
        ImGui::Begin("moreprobe");
        if (ImGui::Button("First")) first++;
        if (ImGui::Button("Second")) second++;
        ImGui::End();
        ImGui::Render();
    };
    frame(false, true);
    frame(false, false);
    CHECK(!(io.ConfigFlags & ImGuiConfigFlags_NavEnableKeyboard) && !(io.ConfigFlags & ImGuiConfigFlags_NavEnableGamepad));
    CHECK(!io.WantCaptureKeyboard);
    for (int i = 0; i < 4; i++) frame(true, i == 0);
    CHECK((io.ConfigFlags & ImGuiConfigFlags_NavEnableKeyboard) && (io.ConfigFlags & ImGuiConfigFlags_NavEnableGamepad));
    CHECK(io.WantCaptureKeyboard);   // the keyboard is the window's while it is live
    CHECK(io.ConfigNavCursorVisibleAlways && ImGui::GetCurrentContext()->NavId != 0);
    vj::deskFeedPopupIntents({Intent::Down});
    for (int i = 0; i < 4; i++) frame(true, false);
    vj::deskFeedPopupIntents({Intent::Alt, Intent::Aux, Intent::SectionNext, Intent::Accept});   // only Accept means anything here
    for (int i = 0; i < 4; i++) frame(true, false);
    CHECK(first == 0 && second == 1);
    CHECK(g_imguiErrors == 0);
    // The window closes: both flags, the highlight and the capture go with it.
    for (int i = 0; i < 3; i++) frame(false, false);
    CHECK(!(io.ConfigFlags & ImGuiConfigFlags_NavEnableKeyboard) && !(io.ConfigFlags & ImGuiConfigFlags_NavEnableGamepad));
    CHECK(!io.ConfigNavCursorVisibleAlways && !ImGui::GetCurrentContext()->NavCursorVisible && !io.WantCaptureKeyboard);
    io.ConfigFlags = savedFlags;
}

// Review focus 12: a Back with a text box active or a combo open is ImGui's Cancel, and it leaves the item without closing anything else
// (the glue's tvMoreSettingsClose refuses while either is up, so a pad that could not leave a text box would be stuck in it).
static void moreSettingsBackLeavesItems() {
    ImGuiIO& io = ImGui::GetIO();
    const ImGuiConfigFlags savedFlags = io.ConfigFlags;
    char text[16] = "abc";
    int choice = 0;
    auto frame = [&](bool focus) {
        io.DeltaTime = 1.0f / 60.0f;
        vj::deskSetPopupNav(true);
        vj::deskSetKeyboardNav(true);
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(40, 40));
        ImGui::SetNextWindowSize(ImVec2(400, 240));
        if (focus) ImGui::SetNextWindowFocus();
        ImGui::Begin("moreitems");
        ImGui::InputText("##text", text, sizeof text);
        ImGui::Combo("##combo", &choice, "One\0Two\0Three\0");
        ImGui::End();
        ImGui::Render();
    };
    auto settle = [&](int n) { for (int i = 0; i < n; i++) frame(false); };
    auto popupOpen = [] { return ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel); };
    frame(true);
    settle(4);
    CHECK(!ImGui::IsAnyItemActive() && !popupOpen());
    vj::deskFeedPopupIntents({Intent::Accept});   // the text box (the first item) takes the focus for typing
    settle(4);
    CHECK(ImGui::IsAnyItemActive() && io.WantTextInput);
    vj::deskFeedPopupIntents({Intent::Back});
    settle(4);
    CHECK(!ImGui::IsAnyItemActive() && !io.WantTextInput);   // left, so the next Back may close the window
    vj::deskFeedPopupIntents({Intent::Down});
    settle(4);
    vj::deskFeedPopupIntents({Intent::Accept});   // the combo opens
    settle(4);
    CHECK(popupOpen());
    vj::deskFeedPopupIntents({Intent::Back});
    settle(4);
    CHECK(!popupOpen() && !ImGui::IsAnyItemActive() && choice == 0);
    CHECK(g_imguiErrors == 0);
    vj::deskSetPopupNav(false);
    vj::deskSetKeyboardNav(false);
    settle(3);
    io.ConfigFlags = savedFlags;
}

// Where nav starts in the "More settings" window: the first item is the "Reset all settings..." button, which is out of the pad's and
// keyboard's reach while the window is live (NoNav, settings_reset_ui.cpp), so the first A is a tab's and D-pad Right goes on to the next
// tab, never to the button.  (The real window is App-side and not linked here: the probe is the same shape -- the flagged button, then
// a tab bar whose first tab is empty, as Core options is before a game has run -- and what it observes is ImGui's nav with the flags.)
static void moreSettingsDefaultFocusSkipsReset() {
    ImGuiIO& io = ImGui::GetIO();
    const ImGuiConfigFlags savedFlags = io.ConfigFlags;
    ImGuiID resetId = 0, tab1 = 0, tab2 = 0;
    auto frame = [&](bool live, bool focus) {
        io.DeltaTime = 1.0f / 60.0f;
        vj::deskSetPopupNav(live);
        vj::deskSetKeyboardNav(live);
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(40, 40));
        ImGui::SetNextWindowSize(ImVec2(400, 240));
        if (focus) ImGui::SetNextWindowFocus();
        ImGui::Begin("moredefault");
        ImGui::PushItemFlag(ImGuiItemFlags_NoNavDefaultFocus, true);
        ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);
        ImGui::SmallButton("Reset all settings...");
        resetId = ImGui::GetItemID();
        ImGui::PopItemFlag();
        ImGui::PopItemFlag();
        if (ImGui::BeginTabBar("##tabs")) {
            tab1 = ImGui::GetID("Core options");   // a tab's ID is its label hashed in the tab bar's scope
            tab2 = ImGui::GetID("Video");
            // (ImGui's tabs never take default focus themselves, and the button is out of reach: the selected tab asks for it)
            if (ImGui::BeginTabItem("Core options")) { ImGui::SetItemDefaultFocus(); ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Video")) { ImGui::SetItemDefaultFocus(); ImGui::Button("Integer"); ImGui::EndTabItem(); }
            ImGui::EndTabBar();
        }
        ImGui::End();
        ImGui::Render();
    };
    for (int i = 0; i < 2; i++) frame(false, i == 0);
    for (int i = 0; i < 4; i++) frame(true, false);
    CHECK(resetId != 0 && tab1 != 0 && tab2 != 0);
    ImGuiContext& g = *ImGui::GetCurrentContext();
    CHECK(g.NavId != 0 && g.NavId != resetId && g.NavId == tab1);
    vj::deskFeedPopupIntents({Intent::Right});
    for (int i = 0; i < 4; i++) frame(true, false);
    CHECK(g.NavId == tab2);
    vj::deskFeedPopupIntents({Intent::Left, Intent::Left, Intent::Up, Intent::Up});
    for (int i = 0; i < 6; i++) frame(true, false);
    CHECK(g.NavId != resetId && g.NavId != 0);
    for (int i = 0; i < 3; i++) frame(false, false);
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

// A pad drove the Base layer and a desktop window then opened over it ("More settings"): the shell no longer owns the input, but the
// pointer is still the player's the moment the mouse moves.
static void padCursorReturnsOverDesktopWindow() {
    FakeHost host;
    host.tv = true;
    host.loaded = false;
    Shell shell(host);
    frames(host, shell, 2);
    down(host, shell);
    frames(host, shell, 1);
    CHECK(ImGui::GetMouseCursor() == ImGuiMouseCursor_None);
    host.desktopOpen = true;
    frames(host, shell, 1);
    CHECK(!shell.ownsInput());
    // The app hands the shell no events now (App::handleEvent asks ownsInput() first): the pointer's return is seen in the draw, as the
    // mouse position changes.
    frames(host, shell, 2);
    CHECK(ImGui::GetMouseCursor() == ImGuiMouseCursor_None);
    ImGui::GetIO().AddMousePosEvent(700, 400);
    frames(host, shell, 2);
    CHECK(ImGui::GetMouseCursor() != ImGuiMouseCursor_None);
    ImGui::GetIO().AddMousePosEvent(0, 0);
    frames(host, shell, 2);
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

// Finding B: games in the library but nothing on Home's shelves is not "empty": it points at the Library section (RB).
static void homeWithGamesButNoShelvesPointsAtLibrary() {
    const char* empty = "Your library is empty. Add game folders in desktop mode: View > TV mode turns TV mode off.";
    FakeHost host;
    host.all = mkGames("Game", 20);   // 20 cartridges, nothing played, no favorites, no discs
    Shell shell(host);
    g_texts.clear();
    homeSetup(host, shell);
    frames(host, shell, 2);
    CHECK(drew("Nothing to continue yet. Press RB for your Library.") && !drew(empty));
    CHECK(!drew("Play") && !drew("Continue playing"));
    host.scanning = true;                               // a rescan changes nothing for a library that has games
    g_texts.clear();
    frames(host, shell, 2);
    CHECK(drew("Nothing to continue yet. Press RB for your Library.") && !drew("Scanning your library..."));
    host.all.clear();                                   // and with none, the old wording is back (homeEmptyLibraryCard)
    host.scanning = false;
    g_texts.clear();
    frames(host, shell, 2);
    CHECK(drew(empty) && !drew("Nothing to continue yet. Press RB for your Library."));
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
    down(host, shell, 7);                                      // ... Settings, Controls, Enter TV mode
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

// ---- TV Settings (phase 3, Task 12) -----------------------------------------------------------------------------------

static SettingRow mkRow(const char* id, const char* label, int group, std::vector<std::string> choices, int index = 0) {
    SettingRow r;
    r.id = id;
    r.label = label;
    r.group = group;
    r.choices = std::move(choices);
    r.index = index;
    return r;
}

// Picture: Aspect ratio; Sound: Volume; Interface: Show FPS, then More settings -- no Speed row, so no Speed group.
static std::vector<SettingRow> settingsFixture() {
    std::vector<std::string> pct;
    for (int i = 0; i <= 10; i++) pct.push_back(std::to_string(i * 10) + " %");
    return {mkRow("aspect", "Aspect ratio", 0, {"Core default", "4:3", "16:9", "8:7 (square pixels)"}),
            mkRow("volume", "Volume", 2, pct, 5),
            mkRow("fps", "Show FPS", 3, {"Off", "On"}),
            mkRow("more", "More settings\xE2\x80\xA6", 3, {})};
}

// A TV host on the Settings section (reached through the shell, not by counting RB presses), drawn once.
static void settingsSetup(FakeHost& host, Shell& shell, std::vector<SettingRow> rows = settingsFixture()) {
    host.tv = true;
    host.loaded = false;
    host.rows = std::move(rows);
    ImGui::GetIO().DisplaySize = ImVec2(1920, 1080);
    frames(host, shell, 2);
    shell.setSection(Section::Settings);
    frames(host, shell, 2);
}

static void leftClick(FakeHost& h, Shell& s) {
    ImGuiIO& io = ImGui::GetIO();
    io.AddMouseButtonEvent(0, true);
    frames(h, s, 1);
    io.AddMouseButtonEvent(0, false);
    frames(h, s, 1);
}

static bool lastSet(const FakeHost& h, const char* id, int index) {
    return !h.set.empty() && h.set.back().first == id && h.set.back().second == index;
}

static void settingsRailAndList() {
    FakeHost host;
    Shell shell(host);
    settingsSetup(host, shell);
    g_texts.clear();
    frames(host, shell, 2);
    CHECK(shell.screenId() == "settings");
    CHECK(drew("Picture") && drew("Sound") && drew("Interface") && !drew("Speed"));
    CHECK(drew("Aspect ratio") && !drew("Show FPS"));   // the list is the focused group's: Picture
    down(host, shell, 2);                                // Picture, Sound, Interface
    g_texts.clear();
    frames(host, shell, 2);
    CHECK(drew("Show FPS") && drew("More settings\xE2\x80\xA6") && !drew("Aspect ratio"));
    rightN(host, shell, 1);                              // into the list
    CHECK(host.set.empty());
    rightN(host, shell, 1);
    CHECK(lastSet(host, "fps", 1));
    leftN(host, shell, 1);
    CHECK(lastSet(host, "fps", 0));
    accept(host, shell);
    CHECK(lastSet(host, "fps", 1));
    accept(host, shell);                                 // Accept wraps
    CHECK(lastSet(host, "fps", 0) && host.set.size() == 4);
    rightN(host, shell, 1);
    rightN(host, shell, 1);                              // Right stops at the last choice
    CHECK(host.set.size() == 5 && lastSet(host, "fps", 1));
    CHECK(g_imguiErrors == 0);
}

// Back leaves the list for the rail, and the rail for Home; Left at the first choice is the same step as Back.
static void settingsBackAndLeftEdge() {
    FakeHost host;
    Shell shell(host);
    settingsSetup(host, shell);
    down(host, shell, 2);
    rightN(host, shell, 1);                              // Interface's list, on Show FPS (Off)
    leftN(host, shell, 1);                               // Off cannot go further left: the rail
    CHECK(host.set.empty());
    rightN(host, shell, 1);                              // ... and into the list again
    rightN(host, shell, 1);
    CHECK(lastSet(host, "fps", 1));
    back(host, shell);                                   // list -> rail: still on the Settings screen
    CHECK(shell.screenId() == "settings" && shell.section() == Section::Settings);
    rightN(host, shell, 1);
    accept(host, shell);                                 // the list again, Accept steps (wrapping)
    CHECK(lastSet(host, "fps", 0));
    back(host, shell);
    back(host, shell);                                   // the rail: Back is Home
    CHECK(shell.section() == Section::Home && shell.screenId() == "home");
}

static void settingsHints() {
    FakeHost host;
    Shell shell(host);
    settingsSetup(host, shell);
    g_texts.clear();
    frames(host, shell, 2);
    CHECK(drew("Open") && !drew("Change") && !drew("Groups"));   // the rail
    down(host, shell, 2);
    rightN(host, shell, 1);
    g_texts.clear();
    frames(host, shell, 2);
    CHECK(drew("Change") && drew("Next") && drew("Groups") && !drew("Open"));
    down(host, shell);                                   // More settings...
    g_texts.clear();
    frames(host, shell, 2);
    CHECK(drew("Open") && drew("Groups") && !drew("Change") && !drew("Next"));
    // A read-only row changes nothing, so the bar does not offer it.
    FakeHost ro;
    Shell roShell(ro);
    SettingRow r = mkRow("fps", "Show FPS", 3, {"Off", "On"});
    r.readOnly = true;
    r.note = "Set for this session";
    settingsSetup(ro, roShell, {r});
    rightN(ro, roShell, 1);
    g_texts.clear();
    frames(ro, roShell, 2);
    CHECK(drew("Groups") && !drew("Change") && !drew("Next"));
}

// Review focus 13: turning TV mode Off from its own row leaves the Base layer cleanly -- the window stops being drawn, the
// pointer comes back, and the pad belongs to the app.
static void settingsTvModeOffLeavesCleanly() {
    FakeHost host;
    Shell shell(host);
    settingsSetup(host, shell, {mkRow("tv_mode", "TV mode", 3, {"Off", "Manual", "Auto"}, 1)});
    CHECK(shell.ownsInput() && baseWindow() && baseWindow()->WasActive);
    rightN(host, shell, 1);                              // the list (only Interface exists)
    accept(host, shell);                                 // Manual -> Auto
    CHECK(lastSet(host, "tv_mode", 2) && host.tv);
    accept(host, shell);                                 // Auto -> Off (Accept wraps)
    CHECK(lastSet(host, "tv_mode", 0) && !host.tv);
    frames(host, shell, 3);
    CHECK(!baseWindow() || !baseWindow()->WasActive);
    CHECK(ImGui::GetMouseCursor() != ImGuiMouseCursor_None);
    CHECK(!shell.ownsInput());
    SDL_Event e;
    SDL_zero(e);
    e.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
    e.gbutton.button = SDL_GAMEPAD_BUTTON_DPAD_DOWN;
    e.gbutton.down = true;
    CHECK(!shell.handleEvent(e, g_ms));
    e.type = SDL_EVENT_GAMEPAD_BUTTON_UP;
    e.gbutton.down = false;
    shell.handleEvent(e, g_ms);
    frames(host, shell, 2);
    CHECK(g_imguiErrors == 0);
}

// The Auto rows (#991 phase 4): two On / Off rows after the TV mode row, steppable like any other (Right moves without wrapping,
// Accept wraps over the two choices), each change handed to the host as (id, index).
static void settingsAutoRowsStep() {
    FakeHost host;
    Shell shell(host);
    settingsSetup(host, shell,
                  {mkRow("tv_mode", "TV mode", 3, {"Off", "Manual", "Auto"}, 2), mkRow("tv_auto_fs", "Auto: in fullscreen", 3, {"Off", "On"}, 0),
                   mkRow("tv_auto_pad", "Auto: when a controller is used", 3, {"Off", "On"}, 1)});
    CHECK(shell.focusLabel() == "Interface");            // on the rail: the group (the headless script's `expect tv-focus=`)
    rightN(host, shell, 1);                              // the list (only Interface exists)
    CHECK(shell.focusLabel() == "TV mode");
    down(host, shell, 1);                                // Auto: in fullscreen
    CHECK(shell.focusLabel() == "Auto: in fullscreen");
    rightN(host, shell, 1);
    CHECK(lastSet(host, "tv_auto_fs", 1));
    accept(host, shell);
    CHECK(lastSet(host, "tv_auto_fs", 0));               // Accept wraps over the two choices
    g_texts.clear();
    frames(host, shell, 2);
    CHECK(drew("Auto: in fullscreen") && drew("Auto: when a controller is used"));
}

static void settingsReadonlyRowIgnoresInput() {
    FakeHost host;
    Shell shell(host);
    SettingRow r = mkRow("resolution", "Internal resolution", 0, {"1x", "2x"}, 1);
    r.readOnly = true;
    r.note = "Set for this session by Netplay";
    settingsSetup(host, shell, {r});
    g_texts.clear();
    frames(host, shell, 2);
    CHECK(drew("Set for this session by Netplay") && drew("Internal resolution") && drew("2x"));
    rightN(host, shell, 1);                              // into the list
    rightN(host, shell, 1);
    leftN(host, shell, 1);                               // (back to the rail: nothing to step)
    accept(host, shell);                                 // (into the list again)
    accept(host, shell);
    rightN(host, shell, 1);
    CHECK(host.set.empty());
    // The pointer is as inert: a click on either half of the value.
    ImGuiIO& io = ImGui::GetIO();
    io.AddMousePosEvent(1810, 196 + 36);
    frames(host, shell, 2);
    leftClick(host, shell);
    io.AddMousePosEvent(1700, 196 + 36);
    frames(host, shell, 2);
    leftClick(host, shell);
    CHECK(host.set.empty());
    io.AddMousePosEvent(0, 0);
    frames(host, shell, 2);
}

static void settingsMoreOpensDesktop() {
    FakeHost host;
    Shell shell(host);
    settingsSetup(host, shell);
    down(host, shell, 2);
    rightN(host, shell, 1);
    down(host, shell);                                   // More settings...
    rightN(host, shell, 2);                              // nothing to step
    leftN(host, shell, 1);                               // ... and Left is the rail
    CHECK(host.moreOpened == 0 && host.set.empty());
    rightN(host, shell, 1);
    CHECK(host.moreOpened == 0);
    accept(host, shell);
    CHECK(host.moreOpened == 1 && host.set.empty());
    host.desktopOpen = true;                             // what the app does next
    frames(host, shell, 2);
    SDL_Event e;
    SDL_zero(e);
    e.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
    e.gbutton.button = SDL_GAMEPAD_BUTTON_DPAD_DOWN;
    e.gbutton.down = true;
    CHECK(!shell.handleEvent(e, g_ms));                  // the shell stepped aside
    e.type = SDL_EVENT_GAMEPAD_BUTTON_UP;
    e.gbutton.down = false;
    shell.handleEvent(e, g_ms);
    host.desktopOpen = false;
    frames(host, shell, 2);
    CHECK(shell.ownsInput());
}

// A row the build or the session does not have is not drawn at all; rows are fetched again every frame.
static void settingsHiddenRowsAbsent() {
    FakeHost host;
    Shell shell(host);
    settingsSetup(host, shell);
    g_texts.clear();
    frames(host, shell, 2);
    CHECK(!drew("Shader"));
    SettingRow shader = mkRow("shader", "Shader", 0, {"Off", "crt-royale", "lcd"}, 1);
    host.rows.insert(host.rows.begin(), shader);
    g_texts.clear();
    frames(host, shell, 2);
    CHECK(drew("Shader") && drew("crt-royale") && drew("Aspect ratio"));
    host.rows.erase(host.rows.begin());                  // the library went away
    g_texts.clear();
    frames(host, shell, 2);
    CHECK(!drew("Shader") && drew("Aspect ratio"));
    // A group whose rows all went has no rail item either.
    host.rows.erase(host.rows.begin());                  // Aspect ratio: Picture is empty
    g_texts.clear();
    frames(host, shell, 2);
    CHECK(!drew("Picture") && drew("Sound") && drew("Interface") && drew("Volume"));
    CHECK(g_imguiErrors == 0);
}

// A desktop window over the Base layer ("More settings") has the pads: the screens' hints (A Open, LB RB Sections, B ...) list buttons
// that do nothing there, so none is drawn.
static void baseHintsHiddenOverDesktopWindow() {
    FakeHost host;
    Shell shell(host);
    settingsSetup(host, shell);
    g_texts.clear();
    frames(host, shell, 2);
    CHECK(drew("Sections") && drew("Open"));
    host.desktopOpen = true;
    g_texts.clear();
    frames(host, shell, 3);
    CHECK(!drew("Sections") && !drew("Open") && !drew("Groups"));
    host.desktopOpen = false;
    g_texts.clear();
    frames(host, shell, 3);
    CHECK(drew("Sections"));
    CHECK(g_imguiErrors == 0);
}

// Speed before a game has run (approved change G): the group is in the rail with its one line, which nothing steps; on the Base screen
// and in the menu's groups level.
static std::vector<SettingRow> speedNoteRows() {
    SettingRow note = mkRow("speed_note", "Available once a game has run", 1, {});
    note.readOnly = true;
    std::vector<SettingRow> rows = settingsFixture();
    rows.insert(rows.begin() + 1, note);                 // after the Picture row, before Sound
    return rows;
}

static void settingsSpeedNoteBeforeAGame() {
    FakeHost host;
    Shell shell(host);
    settingsSetup(host, shell, speedNoteRows());
    g_texts.clear();
    frames(host, shell, 2);
    CHECK(drew("Speed"));                                // the rail item
    down(host, shell);                                   // Speed
    g_texts.clear();
    frames(host, shell, 2);
    CHECK(drew("Available once a game has run") && !drew("Aspect ratio"));
    rightN(host, shell, 1);                              // into the list (it is the only row) ...
    accept(host, shell);
    rightN(host, shell, 2);
    CHECK(host.set.empty() && host.moreOpened == 0);     // ... where nothing steps or opens
    CHECK(g_imguiErrors == 0);
}

static void settingsFocusRemembered() {
    FakeHost host;
    Shell shell(host);
    settingsSetup(host, shell, {mkRow("aspect", "Aspect ratio", 0, {"Core default", "4:3"}),
                                mkRow("fps", "Show FPS", 3, {"Off", "On"}), mkRow("vsync", "VSync", 3, {"Off", "On"}),
                                mkRow("theme", "Theme", 3, {"System", "Dark", "Light"}), mkRow("more", "More settings", 3, {})});
    down(host, shell);                                   // Interface
    rightN(host, shell, 1);
    down(host, shell, 2);                                // Theme
    lb(host, shell);
    lb(host, shell);
    CHECK(shell.screenId() == "library");
    rb(host, shell);
    rb(host, shell);
    CHECK(shell.screenId() == "settings");
    accept(host, shell);                                 // lands on the same row, in its list
    CHECK(lastSet(host, "theme", 1) && host.set.size() == 1);
    // The row is followed by what it is, not by where it was: a row inserted above it does not move the focus.
    host.rows.insert(host.rows.begin() + 1, mkRow("shader2", "Extra", 3, {"Off", "On"}));
    frames(host, shell, 2);
    accept(host, shell);
    CHECK(lastSet(host, "theme", 2));
}

static void settingsMouse() {
    FakeHost host;
    Shell shell(host);
    settingsSetup(host, shell, {mkRow("fps", "Show FPS", 3, {"Off", "On"}), mkRow("volume", "Volume", 2, {"0 %", "50 %", "100 %"}, 1),
                                mkRow("vsync", "VSync", 3, {"Off", "On"}, 1)});
    ImGuiIO& io = ImGui::GetIO();
    // Interface is the second group: the rail item hovers into view.  1920x1080: the rail at x 96, 54 high, 62 apart; the
    // list from x 504 to 1824, rows 72 high, 86 apart from y 196.
    io.AddMousePosEvent(96 + 100, 150 + 62 + 27);
    frames(host, shell, 2);
    g_texts.clear();
    frames(host, shell, 2);
    CHECK(drew("Show FPS") && !drew("Volume"));
    io.AddMousePosEvent(1810, 196 + 36);    // right of the value's middle (the short value sits at the far right) of Show FPS
    frames(host, shell, 2);
    leftClick(host, shell);
    CHECK(lastSet(host, "fps", 1));
    io.AddMousePosEvent(1700, 196 + 36);    // ... left of it, still right of the row's middle
    frames(host, shell, 2);
    leftClick(host, shell);
    CHECK(lastSet(host, "fps", 0));
    io.AddMousePosEvent(504 + 200, 196 + 36);            // the label: Accept, one step on
    frames(host, shell, 2);
    leftClick(host, shell);
    CHECK(lastSet(host, "fps", 1) && host.set.size() == 3);
    leftClick(host, shell);                              // wraps
    CHECK(lastSet(host, "fps", 0));
    // The wheel moves the focus: one notch is one row (the pointer rests where it is, so it does not pull the focus back).
    io.AddMouseWheelEvent(0, -1.0f);
    frames(host, shell, 2);
    accept(host, shell);
    CHECK(lastSet(host, "vsync", 0));
    io.AddMouseWheelEvent(0, 1.0f);
    frames(host, shell, 2);
    accept(host, shell);
    CHECK(lastSet(host, "fps", 1));
    io.AddMousePosEvent(0, 0);
    frames(host, shell, 2);
}

// The value is right-aligned, so a short one ("50 %") has its chevrons far right of the row's middle: a click on each
// chevron of the focused (grown) row steps that way -- the right one lies past the unscaled card's edge.
static void settingsClickOnChevrons() {
    FakeHost host;
    Shell shell(host);
    settingsSetup(host, shell, {mkRow("volume", "Volume", 2, {"0 %", "50 %", "100 %"}, 1)});
    ImGuiIO& io = ImGui::GetIO();
    io.AddMousePosEvent(504 + 200, 196 + 36);            // focus the row and let it finish growing
    frames(host, shell, 30);
    // 1920x1080, the focused row (scale 1.08) spans 504 + 660 -+ 713; see SettingsScreen::drawRow.
    const float k = 1.08f, inner = 504 + 660 + 1320 * k * 0.5f - 24 * (0.6f + 0.4f * k);
    const float chevW = 9 * k, gap = 24 * k, valueRight = inner - chevW - gap;
    const float leftChev = valueRight - measure(30 * k, "50 %") - gap - chevW * 0.5f, rightChev = inner - chevW * 0.5f;
    CHECK(leftChev > 504 + 1320 * 0.75f && rightChev > 504 + 1320);   // a click map by quarters would miss both
    io.AddMousePosEvent(leftChev, 196 + 36);
    frames(host, shell, 2);
    leftClick(host, shell);
    CHECK(lastSet(host, "volume", 0) && host.set.size() == 1);
    io.AddMousePosEvent(rightChev, 196 + 36);
    frames(host, shell, 2);
    leftClick(host, shell);
    CHECK(lastSet(host, "volume", 1) && host.set.size() == 2);
    leftClick(host, shell);
    CHECK(lastSet(host, "volume", 2) && host.set.size() == 3);
    io.AddMousePosEvent(0, 0);
    frames(host, shell, 2);
}

// Every size, long labels and notes, a full fixture (the list scrolls), every direction and the pointer.
static void settingsWalk120FramesNoAsserts() {
    ImGuiIO& io = ImGui::GetIO();
    FakeHost host;
    host.tv = true;
    host.loaded = false;
    std::vector<std::string> pct;
    for (int i = 0; i <= 10; i++) pct.push_back(std::to_string(i * 10) + " %");
    host.rows = settingsFixture();
    SettingRow ro = mkRow("resolution", "Internal resolution with a rather long label", 0, {"1x (native)", "2x"}, 1);
    ro.readOnly = true;
    ro.note = "Set for this session by the Netplay window (or --option); disconnect to change it here";
    host.rows.push_back(ro);
    host.rows.push_back(mkRow("risc_clock", "RISC overclock", 1, {"0.5x", "1x (stock)", "137% (custom)"}, 2));
    host.rows.push_back(mkRow("idle_skip", "Idle-loop skip", 1, {"disabled", "enabled"}, 1));
    for (int i = 0; i < 12; i++) host.rows.push_back(mkRow("extra", "An extra setting", 3, {"Off", "On"}));
    host.rows.push_back(mkRow("theme", "Theme", 3, {"System", "Dark", "Light", "Custom: A very long theme name that does not fit"}, 3));
    Shell shell(host);
    const int errors = g_imguiErrors;
    shell.setSection(Section::Settings);
    for (float hh : {720.0f, 1080.0f, 2160.0f}) {
        io.DisplaySize = ImVec2(hh * 16 / 9, hh);
        frames(host, shell, 3);
        for (int round = 0; round < 3; round++) {
            for (SDL_GamepadButton b : {SDL_GAMEPAD_BUTTON_DPAD_DOWN, SDL_GAMEPAD_BUTTON_DPAD_RIGHT, SDL_GAMEPAD_BUTTON_DPAD_DOWN,
                                        SDL_GAMEPAD_BUTTON_DPAD_UP, SDL_GAMEPAD_BUTTON_DPAD_LEFT}) {
                for (int i = 0; i < 12; i++) tapButton(host, shell, b);
                frames(host, shell, 40);
            }
            for (int i = 0; i < 6; i++) accept(host, shell);
            back(host, shell);
        }
        io.AddMousePosEvent(io.DisplaySize.x * 0.6f, io.DisplaySize.y * 0.4f);
        io.AddMouseWheelEvent(0, -3.0f);
        frames(host, shell, 20);
        leftClick(host, shell);
        io.AddMouseWheelEvent(0, 3.0f);
        frames(host, shell, 20);
        io.AddMousePosEvent(0, 0);
        frames(host, shell, 3);
    }
    io.DisplaySize = ImVec2(1920, 1080);
    CHECK(g_imguiErrors == errors);
}

// ---- TV Achievements (phase 3, Task 14) -----------------------------------------------------------------------------------

static AchItem mkItem(uint32_t id, const char* title, const char* status, bool unlocked, unsigned points = 10) {
    AchItem a;
    a.id = id;
    a.title = title;
    a.status = status;
    a.unlocked = unlocked;
    a.points = points;
    return a;
}

static AchItem mkTracker(uint32_t id, const char* text) {
    AchItem a;
    a.kind = AchItem::Tracker;
    a.id = id;
    a.title = text;
    return a;
}

static AchGame mkAchGame(uint32_t id, const char* title, unsigned total, unsigned got, bool progress, bool inLibrary) {
    AchGame g;
    g.id = id;
    g.title = title;
    g.achievements = total;
    g.unlocked = got;
    g.haveProgress = progress;
    g.inLibrary = inLibrary;
    return g;
}

// Three games of the RA list: only "Beta" is in the library.
static std::vector<AchGame> raFixture() {
    return {mkAchGame(10, "Alpha", 12, 3, true, false), mkAchGame(11, "Beta", 8, 0, false, true), mkAchGame(12, "Gamma", 5, 5, true, false)};
}

// A signed-in TV host on the Achievements section (reached through the shell, not by counting RB presses), drawn once.
static void achSetup(FakeHost& host, Shell& shell) {
    host.tv = true;
    host.loaded = false;
    host.hdr.signedIn = true;
    ImGui::GetIO().DisplaySize = ImVec2(1920, 1080);
    frames(host, shell, 2);
    shell.setSection(Section::Achievements);
    frames(host, shell, 2);
}

// How many times one frame draws `s` (the top bar's tab and a hint of the same word are two).
static int countText(FakeHost& host, Shell& shell, const std::string& s) {
    g_texts.clear();
    frames(host, shell, 1);
    int n = 0;
    for (const std::string& t : g_texts)
        if (t == s) n++;
    return n;
}

// The loaded game of the rows tests: a tracker between two achievements.
static void achLoadedGame(FakeHost& host) {
    host.hdr.hasGame = true;
    host.hdr.title = "Cybermorph";
    host.hdr.mode = "Softcore (hardcore is paused: virtualjaguar_pal = enabled)";
    host.hdr.unlocked = 1;
    host.hdr.total = 2;
    host.items = {mkItem(1, "First Blood", "Unlocked in hardcore  2026-10-16 21:04", true, 5), mkTracker(900, "Lap 2: 1:12.4"),
                  mkItem(2, "Ring Master", "Locked  Progress: 5/10", false, 25)};
    host.items[0].description = "Defeat the first drone";
}

static void achNotSignedInLine() {
    FakeHost host;
    Shell shell(host);
    host.tv = true;
    host.loaded = false;
    host.hdr.signedIn = false;
    host.raGames = raFixture();   // a list the signed-out player must not see
    frames(host, shell, 2);
    shell.setSection(Section::Achievements);
    g_texts.clear();
    frames(host, shell, 3);
    CHECK(shell.screenId() == "achievements");
    CHECK(drew("Sign in from Settings > Achievements in desktop mode"));
    CHECK(!drew("Beta \xC2\xB7 8 achievements") && !drew("RetroAchievements is not available in this build"));
    accept(host, shell);
    down(host, shell);
    CHECK(host.played == 0 && host.guardArmed == 0);
    back(host, shell);
    CHECK(shell.screenId() == "home");
}

static void achNotAvailableLine() {
    FakeHost host;
    Shell shell(host);
    achSetup(host, shell);
    host.hdr.available = false;
    host.hdr.signedIn = false;   // no rc_client: nobody can be signed in
    g_texts.clear();
    frames(host, shell, 3);
    CHECK(drew("RetroAchievements is not available in this build"));
    CHECK(!drew("Sign in from Settings > Achievements in desktop mode"));
}

static void achRowsForLoadedGame() {
    FakeHost host;
    Shell shell(host);
    achLoadedGame(host);
    achSetup(host, shell);
    g_texts.clear();
    frames(host, shell, 3);
    CHECK(drew("First Blood") && drew("Ring Master") && drew("Lap 2: 1:12.4"));   // 3 rows
    CHECK(drew("Unlocked in hardcore  2026-10-16 21:04") && drew("Locked  Progress: 5/10"));
    CHECK(drew(host.hdr.mode) && drew("Cybermorph") && drew("1 of 2 achievements"));
    CHECK(drew("5 pts") && drew("25 pts") && !drew("Sign in from Settings > Achievements in desktop mode"));
    CHECK(drew("Defeat the first drone"));   // the description, a muted line between the title and the status
    // The focus lives on achievements: Down from the first jumps over the tracker, Up comes back, the ends hold.
    CHECK(shell.memory().get("achievements/rows") == 0);
    down(host, shell);
    CHECK(shell.memory().get("achievements/rows") == 2);
    down(host, shell);
    CHECK(shell.memory().get("achievements/rows") == 2);
    up(host, shell);
    CHECK(shell.memory().get("achievements/rows") == 0);
    up(host, shell);
    CHECK(shell.memory().get("achievements/rows") == 0);
    accept(host, shell);   // nothing to do on a row
    CHECK(host.played == 0 && host.guardArmed == 0 && shell.screenId() == "achievements");
    // Hints: just the way out.
    g_texts.clear();
    frames(host, shell, 2);
    CHECK(countText(host, shell, "Home") == 2 && !drew("Play"));   // the tab and the Back hint
    // Only trackers: nothing to focus, nothing breaks.
    host.items = {mkTracker(900, "Lap 2: 1:12.4")};
    down(host, shell);
    up(host, shell);
    accept(host, shell);
    g_texts.clear();
    frames(host, shell, 3);
    CHECK(drew("Lap 2: 1:12.4"));
    // A game with no achievements says so.
    host.items.clear();
    g_texts.clear();
    frames(host, shell, 3);
    CHECK(drew("This game has no achievements yet."));
    CHECK(g_imguiErrors == 0);
}

// Trackers listed first (as the app lists them): the first achievement has the focus from the start, and Up stays there.
static void achTrackersFirstFocusAchievement() {
    FakeHost host;
    Shell shell(host);
    host.hdr.hasGame = true;
    host.hdr.title = "Cybermorph";
    host.hdr.mode = "Hardcore";
    host.items = {mkTracker(900, "Lap 2: 1:12.4"), mkTracker(901, "Best: 1:09.0"), mkItem(1, "First Blood", "Locked", false),
                  mkItem(2, "Ring Master", "Locked", false)};
    achSetup(host, shell);
    CHECK(shell.memory().get("achievements/rows") == 2);
    up(host, shell);
    CHECK(shell.memory().get("achievements/rows") == 2);
    down(host, shell);
    CHECK(shell.memory().get("achievements/rows") == 3);
    // Away and back: the same achievement, even though the list changed under it.
    shell.setSection(Section::Library);
    frames(host, shell, 2);
    host.items.insert(host.items.begin() + 2, mkItem(7, "Newcomer", "Locked", false));
    shell.setSection(Section::Achievements);
    frames(host, shell, 2);
    CHECK(shell.memory().get("achievements/rows") == 4);
}

static void achGamesListWithoutGame() {
    FakeHost host;
    Shell shell(host);
    host.raGames = raFixture();
    achSetup(host, shell);
    g_texts.clear();
    frames(host, shell, 3);
    CHECK(drew("Alpha \xC2\xB7 3/12") && drew("Beta \xC2\xB7 8 achievements") && drew("Gamma \xC2\xB7 5/5"));
    CHECK(!drew("Sign in from Settings > Achievements in desktop mode") && !drew("RetroAchievements is not available in this build"));
    CHECK(shell.memory().get("achievements/games") == 0);
    accept(host, shell);   // Alpha is not in the library
    CHECK(host.played == 0 && host.guardArmed == 0);
    down(host, shell);
    CHECK(shell.memory().get("achievements/games") == 1);
    accept(host, shell);   // Beta is
    CHECK(host.played == 11 && host.guardArmed == 1 && host.holdsCleared == 1);
    host.played = 0;
    down(host, shell);
    accept(host, shell);   // Gamma is not
    CHECK(host.played == 0 && host.guardArmed == 1);
    down(host, shell);     // the end holds
    CHECK(shell.memory().get("achievements/games") == 2);
    up(host, shell, 5);
    CHECK(shell.memory().get("achievements/games") == 0);
    CHECK(g_imguiErrors == 0);
}

static void achGamesHintsAndEmpty() {
    FakeHost host;
    Shell shell(host);
    host.raGames = raFixture();
    achSetup(host, shell);
    g_texts.clear();
    frames(host, shell, 2);
    CHECK(!drew("Play") && countText(host, shell, "Home") == 2);   // Alpha: not in the library, nothing to play
    down(host, shell);
    g_texts.clear();
    frames(host, shell, 2);
    CHECK(drew("Play") && countText(host, shell, "Home") == 2);    // Beta
    host.raGames.clear();                   // no list (yet)
    g_texts.clear();
    frames(host, shell, 3);
    CHECK(!drew("Play") && countText(host, shell, "Home") == 2 && drew("No Jaguar games to show yet."));
    accept(host, shell);
    down(host, shell);
    up(host, shell);
    CHECK(host.played == 0);
}

// The list follows the game, not its place: a rescan or a progress update that reorders it keeps the focus on the same game.
static void achGamesFocusFollowsGame() {
    FakeHost host;
    Shell shell(host);
    host.raGames = raFixture();
    achSetup(host, shell);
    down(host, shell, 2);   // Gamma
    CHECK(shell.memory().get("achievements/games") == 2 && shell.memory().getItem("achievements/games") == "12");
    host.raGames.insert(host.raGames.begin(), mkAchGame(9, "Aardvark", 3, 0, false, false));
    frames(host, shell, 2);
    CHECK(shell.memory().get("achievements/games") == 3);
    shell.setSection(Section::Settings);
    frames(host, shell, 2);
    shell.setSection(Section::Achievements);
    frames(host, shell, 2);
    CHECK(shell.memory().get("achievements/games") == 3);
}

static void achGamesMouse() {
    FakeHost host;
    Shell shell(host);
    host.raGames = raFixture();
    achSetup(host, shell);
    ImGuiIO& io = ImGui::GetIO();
    // 1920x1080: the list from x 504 to 1824, rows 72 high, 86 apart from y 196.
    io.AddMousePosEvent(900, 196 + 86 + 36);   // Beta
    frames(host, shell, 2);
    CHECK(shell.memory().get("achievements/games") == 1 && host.played == 0);   // hovering focuses, it does not play
    leftClick(host, shell);
    CHECK(host.played == 11 && host.guardArmed == 1);
    host.played = 0;
    io.AddMousePosEvent(900, 196 + 36);        // Alpha: focused by the pointer, a click plays nothing
    frames(host, shell, 2);
    leftClick(host, shell);
    CHECK(shell.memory().get("achievements/games") == 0 && host.played == 0 && host.guardArmed == 1);
    io.AddMouseWheelEvent(0, -1.0f);
    frames(host, shell, 2);
    CHECK(shell.memory().get("achievements/games") == 1);
    io.AddMousePosEvent(0, 0);
    frames(host, shell, 2);
}

static void achFocusGameOnOpen() {
    FakeHost host;
    Shell shell(host);
    host.raGames = raFixture();
    host.raGames.push_back(mkAchGame(77, "Tempest 2000", 20, 4, true, true));
    achSetup(host, shell);
    shell.openSection(Section::Achievements, makeAchievementsScreen(host, shell, 77));
    CHECK(shell.section() == Section::Achievements && shell.screenId() == "achievements");
    frames(host, shell, 2);
    CHECK(shell.memory().get("achievements/games") == 3 && shell.memory().getItem("achievements/games") == "77");
    down(host, shell);
    CHECK(shell.memory().get("achievements/games") == 3);   // the last row
    up(host, shell);
    CHECK(shell.memory().get("achievements/games") == 2);
    // A game the list does not have: the focus stays where it was remembered.
    shell.openSection(Section::Achievements, makeAchievementsScreen(host, shell, 4242));
    frames(host, shell, 2);
    CHECK(shell.memory().get("achievements/games") == 2);
}

// The list arrives after the screen opens (the cache loads on the first draw, a fetch later): the focus request waits for it.
static void achFocusGameWaitsForList() {
    FakeHost host;
    Shell shell(host);
    achSetup(host, shell);
    shell.openSection(Section::Achievements, makeAchievementsScreen(host, shell, 11));
    frames(host, shell, 2);
    host.raGames = raFixture();
    frames(host, shell, 2);
    CHECK(shell.memory().get("achievements/games") == 1);
}

static void homeYOpensAchievements() {
    for (int keyboard = 0; keyboard < 2; keyboard++) {
        FakeHost host;
        host.keyboardLast = keyboard != 0;
        host.recent = mkGames("Recent", 3);
        host.all = host.recent;
        Shell shell(host);
        homeSetup(host, shell);
        g_texts.clear();
        frames(host, shell, 2);
        CHECK(countText(host, shell, "Achievements") == (keyboard ? 1 : 2));   // the tab, and with a pad the Y hint
        tapButton(host, shell, SDL_GAMEPAD_BUTTON_NORTH);
        CHECK(shell.screenId() == "achievements" && shell.section() == Section::Achievements);
        CHECK(host.launches == 0);
        back(host, shell);
        CHECK(shell.screenId() == "home" && shell.section() == Section::Home);
    }
    FakeHost host;   // an empty library has no tiles, but Y still goes
    Shell shell(host);
    homeSetup(host, shell);
    frames(host, shell, 2);
    tapButton(host, shell, SDL_GAMEPAD_BUTTON_NORTH);
    CHECK(shell.screenId() == "achievements");
}

static void detailsAchievementsRowWhenKnown() {
    {
        FakeHost host;
        host.recent = mkGames("Recent", 3);
        host.all = host.recent;
        host.achIds[host.recent[0].key] = 77;
        host.hdr.signedIn = true;
        host.raGames = raFixture();
        host.raGames.push_back(mkAchGame(77, "Recent 0", 9, 1, true, true));
        Shell shell(host);
        homeSetup(host, shell);
        tapButton(host, shell, SDL_GAMEPAD_BUTTON_START);
        CHECK(shell.screenId() == "details");
        g_texts.clear();
        frames(host, shell, 2);
        CHECK(countText(host, shell, "Achievements") == 2);   // the tab and the row
        down(host, shell, 2);                    // Play, Favorite, Achievements: no save states
        accept(host, shell);
        CHECK(shell.screenId() == "achievements" && shell.section() == Section::Achievements);
        frames(host, shell, 2);
        CHECK(shell.memory().get("achievements/games") == 3 && shell.memory().getItem("achievements/games") == "77");
        CHECK(host.launches == 0 && host.played == 0);
        back(host, shell);   // the Details screen is gone with the section's stack: Back is Home
        CHECK(shell.screenId() == "home");
    }
    {
        FakeHost host;   // a game the RA list does not know: no row
        host.recent = mkGames("Recent", 3);
        host.all = host.recent;
        host.achIds[host.recent[1].key] = 77;
        Shell shell(host);
        homeSetup(host, shell);
        tapButton(host, shell, SDL_GAMEPAD_BUTTON_START);
        CHECK(shell.screenId() == "details");
        g_texts.clear();
        frames(host, shell, 2);
        CHECK(countText(host, shell, "Achievements") == 1);   // the tab only
        down(host, shell, 5);                    // the end is Favorite
        accept(host, shell);
        CHECK(shell.screenId() == "details" && host.favSet.count(host.recent[0].key) == 1);
    }
}

// Both branches at every size: a long list, long titles and statuses, every direction and the pointer.
static void achWalk120FramesNoAsserts() {
    ImGuiIO& io = ImGui::GetIO();
    for (int branch = 0; branch < 2; branch++) {
        FakeHost host;
        host.tv = true;
        host.loaded = false;
        host.hdr.signedIn = true;
        if (branch == 0) {
            host.hdr.hasGame = true;
            host.hdr.title = "A game with a title that is far too long to fit beside its icon on a small screen";
            host.hdr.mode = "Softcore (hardcore is paused: virtualjaguar_risc_clock_scale = a very long value)";
            host.hdr.unlocked = 14;
            host.hdr.total = 30;
            host.items.push_back(mkTracker(900, "Lap 2: 1:12.4"));
            for (uint32_t i = 1; i <= 30; i++)
                host.items.push_back(mkItem(i, "An achievement whose title runs on and on and on and on past the badge",
                                            "Unlocked in hardcore  2026-10-16 21:04  12.3% of players  [Missable]  [not supported by this version]",
                                            i % 3 == 0, i * 5));
            host.items.insert(host.items.begin() + 10, mkTracker(901, "Best: 1:09.0"));
        } else {
            for (uint32_t i = 1; i <= 40; i++)
                host.raGames.push_back(mkAchGame(i, "A Jaguar game with a rather long title that does not fit in the row", 20 + i, i % 5, i % 2 == 0, i % 3 == 0));
        }
        Shell shell(host);
        const int errors = g_imguiErrors;
        shell.setSection(Section::Achievements);
        for (float hh : {720.0f, 1080.0f, 2160.0f}) {
            io.DisplaySize = ImVec2(hh * 16 / 9, hh);
            frames(host, shell, 3);
            for (int round = 0; round < 3; round++) {
                for (SDL_GamepadButton b : {SDL_GAMEPAD_BUTTON_DPAD_DOWN, SDL_GAMEPAD_BUTTON_DPAD_RIGHT, SDL_GAMEPAD_BUTTON_DPAD_DOWN,
                                            SDL_GAMEPAD_BUTTON_DPAD_UP, SDL_GAMEPAD_BUTTON_DPAD_LEFT}) {
                    for (int i = 0; i < 14; i++) tapButton(host, shell, b);
                    frames(host, shell, 30);
                }
                for (int i = 0; i < 4; i++) accept(host, shell);
            }
            io.AddMousePosEvent(io.DisplaySize.x * 0.6f, io.DisplaySize.y * 0.5f);
            io.AddMouseWheelEvent(0, -3.0f);
            frames(host, shell, 20);
            leftClick(host, shell);
            io.AddMouseWheelEvent(0, 3.0f);
            frames(host, shell, 20);
            io.AddMousePosEvent(0, 0);
            frames(host, shell, 3);
            back(host, shell);   // Home
            shell.setSection(Section::Achievements);
        }
        io.DisplaySize = ImVec2(1920, 1080);
        CHECK(g_imguiErrors == errors);
    }
    // The two lines and the empty list too.
    for (int line = 0; line < 3; line++) {
        FakeHost host;
        host.tv = true;
        host.loaded = false;
        host.hdr.signedIn = line == 2;
        host.hdr.available = line != 0;
        Shell shell(host);
        const int errors = g_imguiErrors;
        shell.setSection(Section::Achievements);
        for (float hh : {720.0f, 1080.0f, 2160.0f}) {
            io.DisplaySize = ImVec2(hh * 16 / 9, hh);
            frames(host, shell, 5);
            down(host, shell);
            accept(host, shell);
        }
        io.DisplaySize = ImVec2(1920, 1080);
        CHECK(g_imguiErrors == errors);
    }
}


// ---- Overlays, and Achievements / Overlays / Settings inside the in-game menu (phase 3, Task 15) ---------------------------

static std::vector<ToggleRow> overlayFixture() {
    ToggleRow fps, keypad, controls;
    fps.id = "fps";
    fps.label = "Show FPS";
    keypad.id = "keypad";
    keypad.label = "Keypad overlay";
    controls.id = "controls";
    controls.label = "Controls overlay";
    return {fps, keypad, controls};
}

static const char* const kToolsOff = "Debug windows are off (hardcore, the emulation thread, or no game)";

// The menu open on its root with the overlays fixture, drawn once.
static void menuSetup(FakeHost& host, Shell& shell) {
    host.overlays = overlayFixture();
    ImGui::GetIO().DisplaySize = ImVec2(1920, 1080);
    shell.openMenu();
    frames(host, shell, 2);
}

// Down to the root row labelled `label` (never past the end: focus does not wrap).
static void menuTo(FakeHost& host, Shell& shell, const char* label) {
    for (int i = 0; i < 12 && std::string(focusedLabel(shell)) != label; i++) down(host, shell);
}

static void ingameRowsFullOrder() {
    FakeHost host;
    host.canLoad = true;
    host.tvOn = true;
    Shell shell(host);
    shell.openMenu();
    std::vector<std::string> labels;
    for (const Shell::Row& r : shell.menuRows()) labels.push_back(r.label);
    const std::vector<std::string> want = {"Resume", "Save state", "Load state", "Achievements", "Overlays",
                                           "Settings", "Controls", "Enter TV mode", "Reset", "Close game"};
    CHECK(labels == want);
    // Hardcore (no Load row) and TV mode Off drop only their own rows; the three screens stay.
    FakeHost hard;
    hard.canLoad = false;
    hard.tvOn = false;
    Shell hardShell(hard);
    hardShell.openMenu();
    labels.clear();
    for (const Shell::Row& r : hardShell.menuRows()) labels.push_back(r.label);
    CHECK((labels == std::vector<std::string>{"Resume", "Save state", "Achievements", "Overlays", "Settings", "Controls", "Reset", "Close game"}));
}

static void overlaysToggleFps() {
    FakeHost host;
    Shell shell(host);
    menuSetup(host, shell);
    menuTo(host, shell, "Overlays");
    accept(host, shell);
    CHECK(shell.menuHasScreen() && shell.menuOpen() && shell.menuPage() == Shell::Page::Root);
    g_texts.clear();
    frames(host, shell, 2);
    CHECK(drew("Show FPS") && drew("Keypad overlay") && drew("Controls overlay") && drew("Off") && !drew("On"));
    accept(host, shell);                                 // the first row: Show FPS
    CHECK((host.toggled == std::vector<std::string>{"fps"}) && shell.menuOpen() && shell.menuHasScreen());
    g_texts.clear();
    frames(host, shell, 2);
    CHECK(drew("On"));                                   // the rows are fetched again every draw
    CHECK(host.resumed == 0 && host.paused);             // the menu still holds the pause
    CHECK(g_imguiErrors == 0);
}

static void overlaysKeypadAndControlsCloseTheMenu() {
    for (int row = 1; row <= 2; row++) {
        FakeHost host;
        Shell shell(host);
        menuSetup(host, shell);
        menuTo(host, shell, "Overlays");
        accept(host, shell);
        down(host, shell, row);
        accept(host, shell);
        CHECK((host.toggled == std::vector<std::string>{row == 1 ? "keypad" : "controls"}));
        CHECK(!shell.menuOpen() && host.resumed == 1 && !shell.menuHasScreen());
        shell.openMenu();                                // the next menu starts on the root, with no screen in the slot
        CHECK(!shell.menuHasScreen() && shell.menuPage() == Shell::Page::Root);
    }
}

static void overlaysToolRowsFromRegistry() {
    FakeHost host;
    Shell shell(host);
    menuSetup(host, shell);
    ToggleRow mem, cpu;
    mem.id = "tool:memory";
    mem.label = "Memory viewer";
    mem.on = true;
    cpu.id = "tool:cpu";
    cpu.label = "CPU debugger";
    cpu.enabled = false;
    cpu.note = kToolsOff;
    host.overlays = {mem, cpu};
    menuTo(host, shell, "Overlays");
    accept(host, shell);
    g_texts.clear();
    frames(host, shell, 2);
    CHECK(drew("Memory viewer") && drew("CPU debugger") && drew(kToolsOff) && drew("On"));
    CHECK(drew("Toggle"));                               // the focused row can be toggled
    accept(host, shell);                                 // Memory viewer: toggled in place, the menu stays
    CHECK((host.toggled == std::vector<std::string>{"tool:memory"}) && shell.menuOpen());
    down(host, shell);
    g_texts.clear();
    frames(host, shell, 2);
    CHECK(!drew("Toggle") && drew("Menu"));              // a disabled row offers nothing
    accept(host, shell);
    CHECK(host.toggled.size() == 1);                     // ... and Accept on it toggles nothing
}

static void overlaysHintsAndBack() {
    FakeHost host;
    Shell shell(host);
    menuSetup(host, shell);
    menuTo(host, shell, "Overlays");
    accept(host, shell);
    g_texts.clear();
    frames(host, shell, 2);
    CHECK(drew("Toggle") && drew("Menu") && !drew("Home"));
    back(host, shell);                                   // Back leaves the screen for the menu, on its row
    CHECK(!shell.menuHasScreen() && shell.menuOpen() && std::string(focusedLabel(shell)) == "Overlays");
    CHECK(host.toggled.empty() && host.resumed == 0);
}

// The Settings groups are a first list level in the menu: Accept opens a group's rows, Back returns to the groups, Back on the
// groups leaves for the menu row.
static void ingameSettingsSlotLevels() {
    FakeHost host;
    host.rows = settingsFixture();
    Shell shell(host);
    menuSetup(host, shell);
    menuTo(host, shell, "Settings");
    accept(host, shell);
    CHECK(shell.menuHasScreen() && shell.menuScreenId() == "settings");
    g_texts.clear();
    frames(host, shell, 2);
    CHECK(drew("Picture") && drew("Sound") && drew("Interface") && !drew("Aspect ratio"));   // the groups, not their rows
    CHECK(drew("Open") && drew("Menu"));
    accept(host, shell);                                 // Picture
    g_texts.clear();
    frames(host, shell, 2);
    CHECK(drew("Aspect ratio") && !drew("Sound") && drew("Groups"));
    rightN(host, shell, 1);                              // a row changes in place
    CHECK(lastSet(host, "aspect", 1));
    back(host, shell);                                   // rows -> groups: still the screen
    CHECK(shell.menuHasScreen() && shell.menuOpen());
    g_texts.clear();
    frames(host, shell, 2);
    CHECK(drew("Sound") && !drew("Aspect ratio"));
    back(host, shell);                                   // groups -> the menu, on Settings
    CHECK(!shell.menuHasScreen() && shell.menuOpen() && std::string(focusedLabel(shell)) == "Settings");
    // Down on the groups moves between them, and the rows shown are the chosen group's.
    accept(host, shell);
    down(host, shell);                                   // Sound
    accept(host, shell);
    g_texts.clear();
    frames(host, shell, 2);
    CHECK(drew("Volume") && !drew("Aspect ratio"));
    CHECK(g_imguiErrors == 0);
}

// "More settings..." in the menu's Settings slot: Accept on it asks the app for the window, and the menu stays open underneath -- it
// holds the pause and keeps the slot -- with the shell still the one that has the input (the app's glue takes the pads before it).
static void menuMoreSettingsKeepsMenu() {
    FakeHost host;
    host.loaded = true;
    host.rows = settingsFixture();
    Shell shell(host);
    menuSetup(host, shell);
    menuTo(host, shell, "Settings");
    accept(host, shell);                                 // the groups: Picture, Sound, Interface
    down(host, shell, 2);
    accept(host, shell);                                 // Interface: Show FPS, More settings...
    down(host, shell);
    CHECK(host.moreOpened == 0);
    accept(host, shell);
    CHECK(host.moreOpened == 1);
    host.desktopOpen = true;                             // what the app does next
    frames(host, shell, 3);
    CHECK(shell.menuOpen() && shell.menuHasScreen() && shell.menuScreenId() == "settings");
    CHECK(host.paused && host.resumed == 0);             // still holding the pause
    CHECK(shell.ownsInput());                            // (the glue, not the shell, steps aside)
    g_texts.clear();                                     // the window has the pads: the menu's hints (Open, Groups) list nothing that works
    frames(host, shell, 3);
    CHECK(!drew("Groups") && !drew("Open") && !drew("Menu"));
    host.desktopOpen = false;                            // the window closed: the menu is where it was
    g_texts.clear();
    frames(host, shell, 3);
    CHECK(drew("Groups"));
    CHECK(shell.menuOpen() && shell.menuScreenId() == "settings" && host.paused);
    back(host, shell);                                   // rows -> groups
    back(host, shell);                                   // groups -> the menu, on Settings
    CHECK(!shell.menuHasScreen() && std::string(focusedLabel(shell)) == "Settings");
    CHECK(g_imguiErrors == 0);
}

// ... and in the in-game menu's slot the groups level lists it too.
static void ingameSpeedNoteBeforeAGame() {
    FakeHost mh;
    mh.rows = speedNoteRows();
    Shell ms(mh);
    menuSetup(mh, ms);
    menuTo(mh, ms, "Settings");
    accept(mh, ms);
    g_texts.clear();
    frames(mh, ms, 2);
    CHECK(drew("Picture") && drew("Speed") && drew("Sound"));
    down(mh, ms);
    accept(mh, ms);
    g_texts.clear();
    frames(mh, ms, 2);
    CHECK(drew("Available once a game has run"));
    accept(mh, ms);
    rightN(mh, ms, 2);
    CHECK(mh.set.empty());
    CHECK(g_imguiErrors == 0);
}

static void ingameSlotBackReturnsToRow() {
    FakeHost host;
    host.rows = settingsFixture();
    Shell shell(host);
    menuSetup(host, shell);
    menuTo(host, shell, "Settings");
    accept(host, shell);
    CHECK(shell.menuHasScreen());
    back(host, shell);
    CHECK(!shell.menuHasScreen() && shell.menuOpen());
    CHECK(std::string(focusedLabel(shell)) == "Settings");
    back(host, shell);                                   // Back on the root is Resume, as before
    CHECK(!shell.menuOpen() && host.resumed == 1);
}

static void ingameAchievementsSlotDraws() {
    FakeHost host;
    host.hdr.signedIn = true;
    host.hdr.hasGame = true;
    host.hdr.title = "Cybermorph";
    host.hdr.mode = "Hardcore";
    host.hdr.total = 2;
    host.items = {mkItem(1, "First Blood", "Locked", false, 5), mkItem(2, "Ring Master", "Locked", false, 25)};
    Shell shell(host);
    menuSetup(host, shell);
    menuTo(host, shell, "Achievements");
    accept(host, shell);
    CHECK(shell.menuHasScreen() && shell.menuScreenId() == "achievements");
    g_texts.clear();
    frames(host, shell, 3);
    CHECK(drew("First Blood") && drew("Ring Master") && drew("Cybermorph") && drew("0 of 2 achievements"));
    CHECK(drew("Menu") && !drew("Home") && !drew("Play"));
    down(host, shell);                                   // the list takes the intents
    CHECK(shell.memory().get("achievements/rows") == 1);
    back(host, shell);
    CHECK(!shell.menuHasScreen() && std::string(focusedLabel(shell)) == "Achievements");
    CHECK(host.played == 0);
}

// A game the RetroAchievements list does not track leaves the in-menu screen on the Jaguar list: nothing on it starts another game
// from under the menu.
static void ingameAchievementsGamesListIsReadOnly() {
    FakeHost host;
    host.hdr.signedIn = true;
    host.raGames = raFixture();
    Shell shell(host);
    menuSetup(host, shell);
    menuTo(host, shell, "Achievements");
    accept(host, shell);
    g_texts.clear();
    frames(host, shell, 3);
    CHECK(drew("Beta \xC2\xB7 8 achievements") && !drew("Play"));
    down(host, shell);
    accept(host, shell);                                 // Beta is in the library: on the Base layer this plays it
    CHECK(host.played == 0 && host.guardArmed == 0 && shell.menuOpen() && shell.menuHasScreen());
}

static void ingameSlotLeavesMenuModelAlone() {
    FakeHost host;
    Shell shell(host);
    menuSetup(host, shell);
    menuTo(host, shell, "Overlays");
    const int focus = shell.menuFocus();
    accept(host, shell);
    CHECK(shell.menuPage() == Shell::Page::Root && shell.menuFocus() == focus);   // the slot sits beside the page model
    down(host, shell, 2);
    up(host, shell);
    CHECK(shell.menuFocus() == focus);                   // the intents went to the screen, not to the rows
    host.canLoad = false;                                // the root would rebuild without its Load row ...
    frames(host, shell, 3);
    CHECK(shell.menuHasScreen());                        // ... but not while the slot has the panel
    back(host, shell);
    CHECK(!shell.menuHasScreen());
    for (const Shell::Row& r : shell.menuRows()) CHECK(r.label != "Load state");
    CHECK(std::string(focusedLabel(shell)) == "Overlays");   // the focus came back to the row (by kind), not to its old place
}

// Every slot at three sizes, every direction, the pointer and the wheel: no ImGui error.
static void menuWalkWithSlotsNoAsserts() {
    ImGuiIO& io = ImGui::GetIO();
    const int errors = g_imguiErrors;
    for (const char* row : {"Achievements", "Overlays", "Settings"}) {
        FakeHost host;
        host.rows = settingsFixture();
        for (int i = 0; i < 12; i++) host.rows.push_back(mkRow("extra", "An extra setting", 0, {"Off", "On"}));
        host.hdr.signedIn = true;
        host.hdr.hasGame = true;
        host.hdr.title = "A game with a title that is far too long to fit beside its icon on a small screen";
        host.hdr.mode = "Softcore (hardcore is paused: virtualjaguar_risc_clock_scale = a very long value)";
        for (uint32_t i = 1; i <= 20; i++) {
            host.items.push_back(mkItem(i, "An achievement whose title runs on and on and on and on past the badge",
                                        "Unlocked in hardcore  2026-10-16 21:04  [Missable]", i % 3 == 0, i * 5));
            host.items.back().description = "A description that is long enough to be cut by the end of the row on a small screen, really";
        }
        host.overlays = overlayFixture();
        for (int i = 0; i < 10; i++) {
            ToggleRow t;
            t.id = "tool:" + std::to_string(i);
            t.label = "A tool window " + std::to_string(i);
            t.enabled = i % 2 == 0;
            t.note = t.enabled ? "" : kToolsOff;
            host.overlays.push_back(t);
        }
        Shell shell(host);
        for (float hh : {720.0f, 1080.0f, 2160.0f}) {
            io.DisplaySize = ImVec2(hh * 16 / 9, hh);
            shell.openMenu();
            frames(host, shell, 3);
            menuTo(host, shell, row);
            accept(host, shell);
            CHECK(shell.menuHasScreen());
            for (int round = 0; round < 2; round++) {
                for (SDL_GamepadButton b : {SDL_GAMEPAD_BUTTON_DPAD_DOWN, SDL_GAMEPAD_BUTTON_DPAD_RIGHT, SDL_GAMEPAD_BUTTON_DPAD_DOWN,
                                            SDL_GAMEPAD_BUTTON_DPAD_UP, SDL_GAMEPAD_BUTTON_DPAD_LEFT}) {
                    for (int i = 0; i < 10; i++) {
                        tapButton(host, shell, b);
                        if (!shell.menuHasScreen()) break;
                    }
                    frames(host, shell, 20);
                }
                if (!shell.menuHasScreen()) break;
            }
            if (shell.menuHasScreen()) {
                io.AddMousePosEvent(io.DisplaySize.x * 0.6f, io.DisplaySize.y * 0.4f);
                io.AddMouseWheelEvent(0, -3.0f);
                frames(host, shell, 20);
                io.AddMouseWheelEvent(0, 3.0f);
                frames(host, shell, 20);
                io.AddMousePosEvent(0, 0);
                frames(host, shell, 3);
            }
            if (!shell.menuOpen()) continue;             // an overlay row closed it: the next size opens it again
            shell.closeMenu();
        }
        if (shell.menuOpen()) shell.closeMenu();
    }
    io.DisplaySize = ImVec2(1920, 1080);
    CHECK(g_imguiErrors == errors);
}

// The four tabs and both shoulder pills fit the top bar at 1280x720 with room left for the clock (the chrome's own sums:
// the safe margin, the accent, the logo, the LB pill, each label at body size 40 apart, the RB pill).
static void tabBarFitsAt720p() {
    const float u = 720.0f / 1080.0f;
    float x = 96 * u + (22 + 16) * u + measure(34 * u, "VIRTUAL JAGUAR") + 56 * u;
    x += measure(24 * u, "LB") + 20 * u + 20 * u;
    const char* labels[] = {"Home", "Library", "Achievements", "Settings"};
    for (int i = 0; i < 4; i++) x += measure(30 * u, labels[i]) + (i < 3 ? 40 : 20) * u;
    x += measure(24 * u, "RB") + 20 * u;
    const float clockLeft = 1280 - 96 * u - measure(26 * u, "12:00 PM");
    CHECK(x + 24 * u < clockLeft);
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
    padCursorReturnsOverDesktopWindow();
    baseHintsHiddenOverDesktopWindow();
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
    homeWithGamesButNoShelvesPointsAtLibrary();
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
    moreSettingsNavFlags();
    moreSettingsBackLeavesItems();
    moreSettingsDefaultFocusSkipsReset();
    settingsRailAndList();
    settingsBackAndLeftEdge();
    settingsHints();
    settingsTvModeOffLeavesCleanly();
    settingsAutoRowsStep();
    settingsReadonlyRowIgnoresInput();
    settingsMoreOpensDesktop();
    settingsHiddenRowsAbsent();
    settingsSpeedNoteBeforeAGame();
    settingsFocusRemembered();
    settingsMouse();
    settingsClickOnChevrons();
    settingsWalk120FramesNoAsserts();
    achNotSignedInLine();
    achNotAvailableLine();
    achRowsForLoadedGame();
    achTrackersFirstFocusAchievement();
    achGamesListWithoutGame();
    achGamesHintsAndEmpty();
    achGamesFocusFollowsGame();
    achGamesMouse();
    achFocusGameOnOpen();
    achFocusGameWaitsForList();
    homeYOpensAchievements();
    detailsAchievementsRowWhenKnown();
    achWalk120FramesNoAsserts();
    ingameRowsFullOrder();
    overlaysToggleFps();
    overlaysKeypadAndControlsCloseTheMenu();
    overlaysToolRowsFromRegistry();
    overlaysHintsAndBack();
    ingameSettingsSlotLevels();
    ingameSpeedNoteBeforeAGame();
    menuMoreSettingsKeepsMenu();
    ingameSlotBackReturnsToRow();
    ingameAchievementsSlotDraws();
    ingameAchievementsGamesListIsReadOnly();
    ingameSlotLeavesMenuModelAlone();
    menuWalkWithSlotsNoAsserts();
    tabBarFitsAt720p();
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
    // The three screens of the menu's slot at the same sizes, inside the display (the rows with a long reason, a tall header and a
    // description included).
    for (const char* row : {"Achievements", "Overlays", "Settings"}) {
        FakeHost host;
        host.rows = settingsFixture();
        host.hdr.signedIn = true;
        host.hdr.hasGame = true;
        host.hdr.title = "A game with a title that is far too long to fit beside its icon on a small screen";
        host.hdr.mode = "Softcore (hardcore is paused: virtualjaguar_risc_clock_scale = a very long value)";
        host.items = {mkTracker(900, "Lap 2: 1:12.4"), mkItem(1, "First Blood", "Unlocked in hardcore  2026-10-16 21:04", true, 5)};
        host.items[1].description = "Defeat the first drone";
        host.overlays = overlayFixture();
        ToggleRow t;
        t.id = "tool:cpu";
        t.label = "CPU debugger";
        t.enabled = false;
        t.note = kToolsOff;
        host.overlays.push_back(t);
        Shell shell(host);
        for (float hh : {720.0f, 1080.0f, 2160.0f}) {
            io.DisplaySize = ImVec2(hh * 16 / 9, hh);
            shell.openMenu();
            frames(host, shell, 3);
            menuTo(host, shell, row);
            accept(host, shell);
            frames(host, shell, 30);
            CHECK(shell.menuHasScreen());
            ImDrawData* dd = ImGui::GetDrawData();
            bool inside = dd->CmdListsCount > 0;
            for (int l = 0; l < dd->CmdListsCount; l++)
                for (const ImDrawVert& v : dd->CmdLists[l]->VtxBuffer)
                    if (v.pos.x < -0.5f || v.pos.y < -0.5f || v.pos.x > io.DisplaySize.x + 0.5f || v.pos.y > io.DisplaySize.y + 0.5f) inside = false;
            CHECK(inside);
            shell.closeMenu();
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
    // The Settings screen (rows with values, chevrons and a note) inside the display at the same sizes, the list focused.
    {
        FakeHost host;
        host.tv = true;
        host.loaded = false;
        host.rows = settingsFixture();
        SettingRow ro = mkRow("resolution", "Internal resolution", 0, {"1x (native)", "2x"}, 1);
        ro.readOnly = true;
        ro.note = "Set for this session by Netplay or --option";
        host.rows.push_back(ro);
        Shell shell(host);
        shell.setSection(Section::Settings);
        for (float hh : {720.0f, 1080.0f, 2160.0f}) {
            io.DisplaySize = ImVec2(hh * 16 / 9, hh);
            frames(host, shell, 3);
            rightN(host, shell, 1);
            frames(host, shell, 30);
            ImDrawData* dd = ImGui::GetDrawData();
            bool inside = dd->CmdListsCount > 0;
            for (int l = 0; l < dd->CmdListsCount; l++)
                for (const ImDrawVert& v : dd->CmdLists[l]->VtxBuffer)
                    if (v.pos.x < -0.5f || v.pos.y < -0.5f || v.pos.x > io.DisplaySize.x + 0.5f || v.pos.y > io.DisplaySize.y + 0.5f) inside = false;
            CHECK(inside);
            back(host, shell);
        }
        CHECK(g_imguiErrors == 0);
    }
    // The Achievements screen, a game's rows and the games list, inside the display at the same sizes (the header, the long
    // title and the mode chip included).
    for (int branch = 0; branch < 2; branch++) {
        FakeHost host;
        host.tv = true;
        host.loaded = false;
        host.hdr.signedIn = true;
        if (branch == 0) {
            host.hdr.hasGame = true;
            host.hdr.title = "A game with a title that is far too long to fit beside its icon on a small screen";
            host.hdr.mode = "Softcore (hardcore is paused: virtualjaguar_risc_clock_scale = a very long value)";
            host.items = {mkTracker(900, "Lap 2: 1:12.4"), mkItem(1, "First Blood", "Unlocked in hardcore  2026-10-16 21:04", true, 5),
                          mkItem(2, "An achievement whose title runs on and on and on and on past the badge and the points", "Locked  Progress: 5/10", false, 250)};
        } else {
            host.raGames = raFixture();
            host.raGames.push_back(mkAchGame(13, "A Jaguar game with a rather long title that does not fit in the row", 20, 4, true, true));
        }
        Shell shell(host);
        shell.setSection(Section::Achievements);
        for (float hh : {720.0f, 1080.0f, 2160.0f}) {
            io.DisplaySize = ImVec2(hh * 16 / 9, hh);
            frames(host, shell, 30);
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
