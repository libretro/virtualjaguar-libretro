// TV mode (#991): the in-game menu drawn headlessly -- a real ImGui context with no renderer, the Shell over a fake
// tv::Host that records what the menu asked of the app.  What this catches that the Shell's own logic tests cannot:
// an ImGui error (ID / stack / window misuse) on any page of the menu, the text it really draws (the netplay banner,
// the Load rows that must not exist under hardcore), and that the menu leaves the pointer and the keyboard to the app
// (no ImGui window while it is closed; no keyboard capture while it is open: F4 and the hold edges are gated on it).
#include "tv/tv_screen.h"
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
    bool controlsShown = false;
    std::string lastToast;
    bool tv = false, tvOn = true;   // TV mode on right now; tvOn = tvAvailable() (the setting is not Off)
    std::string ra;                 // raStatus(): "" unless signed in
    std::string clockStr = "8:42 PM";

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
    std::string glyph(Intent i) const override {
        switch (i) {
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

static bool keyEvent(Shell& shell, SDL_Scancode sc, SDL_Keycode k, bool down = true, SDL_Keymod mod = SDL_KMOD_NONE) {
    SDL_Event e;
    SDL_zero(e);
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
    for (int i = 0; i < 3; i++) {
        io.DeltaTime = 1.0f / 60.0f;
        ImGui::NewFrame();
        shell.frame(g_ms);
        shell.draw(Layer::Base, ImVec2(0, 0), io.DisplaySize);
        ImGui::Begin("box");
        ImGui::SetKeyboardFocusHere();
        ImGui::InputText("##t", buf, sizeof buf);
        ImGui::End();
        ImGui::Render();
    }
    CHECK(io.WantTextInput);
    CHECK(!keyEvent(shell, SDL_SCANCODE_B, SDLK_B) && a->typedLog.empty());
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
    launchedArmsGuard();
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
