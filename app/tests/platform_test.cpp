// Native-OS integration (#833), the parts with no OS in them: the menu model every native surface is
// drawn from, the command dispatcher, the recent-games conversion, the file-association table, the
// launcher-argument filter, and the command event round trip.  No window, no GL, no core.
#include "platform/commands.h"
#include "platform/platform.h"
#include "platform/viewports.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <vector>

using namespace vj::platform;

static int g_checks = 0, g_fail = 0;
#define CHECK(c) do { g_checks++; if (!(c)) { g_fail++; std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

// ---- helpers over the model ---------------------------------------------------------------------------
static const MenuItem* findTop(const MenuBar& bar, const char* title) {
    for (const MenuItem& m : bar.menus)
        if (m.title == title) return &m;
    return nullptr;
}

static const MenuItem* findCmd(const std::vector<MenuItem>& items, Cmd c, int arg = -1) {
    for (const MenuItem& m : items) {
        if (m.separator) continue;
        if (m.submenu) {
            if (const MenuItem* f = findCmd(m.sub, c, arg)) return f;
        } else if (m.cmd == c && (arg < 0 || m.arg == arg)) {
            return &m;
        }
    }
    return nullptr;
}

static const MenuItem* findCmd(const MenuBar& bar, Cmd c, int arg = -1) {
    if (const MenuItem* f = findCmd(bar.app, c, arg)) return f;
    return findCmd(bar.menus, c, arg);
}

static MenuState idle() {
    MenuState s;
    s.fileManager = "Finder";
    return s;
}

static MenuState maximal(bool signedIn) {
    MenuState s;
    s.gameLoaded = true;
    s.canLoadState = true;
    s.usedSlots = 0x5;
    s.stateSlot = 2;
    s.recents = {"/games/A.j64", "/other/A.j64", "/games/B.cue"};
    s.disc.available = true;
    s.disc.count = 3;
    s.disc.index = 1;
    s.disc.labels = {"One", "", "Three"};
    s.achAvailable = true;
    s.achSignedIn = signedIn;
    s.netplayConfigured = true;
    return s;
}

// ---- the model ----------------------------------------------------------------------------------------------
static void testIdleModel() {
    MenuBar bar = buildMenuBar(idle());
    const char* order[] = {"File", "Emulation", "View", "Netplay", "Window", "Help"};   // no Achievements without RA
    CHECK(bar.menus.size() == 6);
    for (size_t i = 0; i < bar.menus.size() && i < 6; i++) CHECK(bar.menus[i].title == order[i]);

    // Nothing to act on without a game.
    for (Cmd c : {Cmd::CloseGame, Cmd::RevealGame, Cmd::Pause, Cmd::Reset, Cmd::FastForward, Cmd::SaveState,
                  Cmd::LoadState, Cmd::StateManager, Cmd::Cheats}) {
        const MenuItem* m = findCmd(bar, c);
        CHECK(m && !m->enabled);
    }
    // ... but opening things works.
    for (Cmd c : {Cmd::Open, Cmd::BootBiosCd, Cmd::BootBiosCart, Cmd::BiosSettings, Cmd::NewWindow, Cmd::Library, Cmd::Settings, Cmd::Quit, Cmd::About,
                  Cmd::Fullscreen, Cmd::Mute, Cmd::Netplay}) {
        const MenuItem* m = findCmd(bar, c);
        CHECK(m && m->enabled);
    }
    const MenuItem* file = findTop(bar, "File");
    const MenuItem* recent = nullptr;
    for (const MenuItem& m : file->sub)
        if (m.title == "Open Recent") recent = &m;
    CHECK(recent && recent->submenu && !recent->enabled && recent->sub.empty());   // greyed, not missing
    CHECK(findCmd(bar, Cmd::RevealGame)->title == "Show in Finder");
    CHECK(findCmd(bar, Cmd::ShowDataFolder)->title == "Show Data Folder in Finder");
    CHECK(findCmd(bar, Cmd::DiscTray) == nullptr);   // no disc drive: no tray item
    CHECK(findCmd(bar, Cmd::NetplayDisconnect) == nullptr);
    CHECK(findCmd(bar, Cmd::AchSignIn) == nullptr);
}

static void testStateReflected() {
    MenuState s = maximal(false);
    s.paused = true;
    s.fastForward = true;
    s.fullscreen = true;
    s.mute = true;
    s.integerScale = true;
    s.libraryOpen = true;
    MenuBar bar = buildMenuBar(s);
    CHECK(findCmd(bar, Cmd::Pause)->title == "Resume" && findCmd(bar, Cmd::Pause)->enabled);
    CHECK(findCmd(bar, Cmd::FastForward)->checked);
    CHECK(findCmd(bar, Cmd::Fullscreen)->title == "Exit Full Screen");
    CHECK(findCmd(bar, Cmd::Mute)->checked);
    CHECK(findCmd(bar, Cmd::IntegerScale)->checked && !findCmd(bar, Cmd::SmoothFilter)->checked);
    CHECK(findCmd(bar, Cmd::Library)->checked);
    CHECK(findCmd(bar, Cmd::LoadState)->enabled);
    s.canLoadState = false;
    CHECK(!findCmd(buildMenuBar(s), Cmd::LoadState)->enabled);   // an empty slot cannot be loaded

    // slots: exactly one checked, "used" flagged
    int checked = 0;
    for (int n = 0; n < 10; n++) {
        const MenuItem* m = findCmd(bar, Cmd::SetSlot, n);
        CHECK(m != nullptr);
        if (m && m->checked) {
            checked++;
            CHECK(n == 2);
        }
        if (m) CHECK((m->title.find("(used)") != std::string::npos) == (n == 0 || n == 2));
    }
    CHECK(checked == 1);

    // discs: labels, blank label falls back, current one checked, indices are disc indices
    CHECK(findCmd(bar, Cmd::DiscSelect, 0)->title == "One");
    CHECK(findCmd(bar, Cmd::DiscSelect, 1)->title == "Disc 2" && findCmd(bar, Cmd::DiscSelect, 1)->checked);
    CHECK(findCmd(bar, Cmd::DiscSelect, 2)->title == "Three");
    CHECK(findCmd(bar, Cmd::DiscTray)->title == "Open Tray");
    s.disc.ejected = true;
    CHECK(findCmd(buildMenuBar(s), Cmd::DiscTray)->title == "Close Tray");
    s.disc.count = 1;   // a single disc: no Disc submenu
    CHECK(findCmd(buildMenuBar(s), Cmd::DiscSelect) == nullptr);

    // achievements
    CHECK(findCmd(bar, Cmd::AchSignIn) && !findCmd(bar, Cmd::AchSignOut));
    MenuBar in = buildMenuBar(maximal(true));
    CHECK(findCmd(in, Cmd::AchSignOut) && !findCmd(in, Cmd::AchSignIn));
    MenuState busy = maximal(false);
    busy.achBusy = true;
    CHECK(!findCmd(buildMenuBar(busy), Cmd::AchSignIn)->enabled);
    CHECK(findCmd(bar, Cmd::NetplayDisconnect) != nullptr);
}

static void testRecents() {
    // order and titles
    std::vector<RecentEntry> r = nativeRecentList({"/g/Tempest 2000.j64", "", "/g/Doom.jag", "/g/Doom.jag", "C:\\roms\\Skyhammer.cue"});
    CHECK(r.size() == 3);   // the empty path and the exact duplicate are dropped
    CHECK(r[0].title == "Tempest 2000" && r[1].title == "Doom" && r[2].title == "Skyhammer");
    // same name, two folders: say which is which
    r = nativeRecentList({"/games/Doom.jag", "/backup/Doom.jag", "/x/Other.j64"});
    CHECK(r[0].title == "Doom (games)" && r[1].title == "Doom (backup)" && r[2].title == "Other");
    // the cap
    std::vector<std::string> many;
    for (int i = 0; i < 25; i++) many.push_back("/g/G" + std::to_string(i) + ".j64");
    CHECK(nativeRecentList(many).size() == 10);
    CHECK(nativeRecentList(many, 3).size() == 3);

    // the model hands the app the index into ITS list
    MenuState s;
    s.recents = {"", "/g/A.j64", "/g/B.j64"};   // a blank slot in the middle of the app's list
    MenuBar bar = buildMenuBar(s);
    const MenuItem* a = findCmd(bar, Cmd::OpenRecent, 1);
    const MenuItem* b = findCmd(bar, Cmd::OpenRecent, 2);
    CHECK(a && a->title == "A" && b && b->title == "B");
    CHECK(findCmd(bar, Cmd::ClearRecents) != nullptr);
    // the Dock menu leads with the recent games; the tray nests them
    std::vector<MenuItem> dock = buildDockMenu(s);
    CHECK(dock.size() > 2 && dock[0].cmd == Cmd::OpenRecent && dock[0].title == "A");
    std::vector<MenuItem> tray = buildTrayMenu(s);
    bool nested = false;
    for (const MenuItem& m : tray)
        if (m.submenu && m.title == "Recent Games" && m.enabled && m.sub.size() == 2) nested = true;
    CHECK(nested);
}

// Every command must be reachable from some native surface, or the dispatcher has dead cases.
static void testCoverage() {
    std::vector<Cmd> seen;
    for (bool in : {false, true}) {
        MenuState s = maximal(in);
        MenuBar bar = buildMenuBar(s);
        collectCommands(bar.app, seen);
        collectCommands(bar.menus, seen);
        collectCommands(buildTrayMenu(s), seen);
        collectCommands(buildDockMenu(s), seen);
    }
    std::set<int> got;
    for (Cmd c : seen) got.insert((int)c);
    for (int c = (int)Cmd::None + 1; c < (int)Cmd::Count_; c++)
        if (!got.count(c)) {
            std::fprintf(stderr, "FAIL command %d is in no native menu\n", c);
            g_fail++;
        }
    CHECK(got.count((int)Cmd::ShowWindow) == 1);
    CHECK(got.count((int)Cmd::None) == 0);
}

// Shortcuts: macOS rules.  One thing per shortcut, always Cmd-modified, a single key, none of the system's.
static void collectKeys(const std::vector<MenuItem>& items, std::vector<std::string>& out) {
    for (const MenuItem& m : items) {
        if (m.separator) continue;
        if (m.submenu) collectKeys(m.sub, out);
        else if (!m.key.empty()) out.push_back(m.key);
    }
}

static void testShortcuts() {
    MenuBar bar = buildMenuBar(maximal(false));
    std::vector<std::string> keys;
    collectKeys(bar.app, keys);
    collectKeys(bar.menus, keys);
    CHECK(keys.size() >= 10);
    std::set<std::string> uniq(keys.begin(), keys.end());
    CHECK(uniq.size() == keys.size());
    // Window > Minimize (cmd+m) and Hide (cmd+h, alt+cmd+h) are the system's.
    const char* reserved[] = {"cmd+m", "cmd+h", "cmd+alt+h", "cmd+c", "cmd+v", "cmd+x", "cmd+a", "cmd+z", "cmd+t"};
    for (const std::string& k : keys) {
        CHECK(k.rfind("cmd+", 0) == 0);                    // every binding carries Cmd
        size_t plus = k.find_last_of('+');
        CHECK(plus != std::string::npos && plus + 1 < k.size() && k.size() - plus - 1 == 1);   // one key
        for (const char* r : reserved) CHECK(k != r);
        // Anything not Cmd-only-letter must still parse into known modifier names.
        std::string mods = k.substr(0, plus);
        size_t pos = 0;
        while (pos < mods.size()) {
            size_t n = mods.find('+', pos);
            std::string tok = mods.substr(pos, n == std::string::npos ? std::string::npos : n - pos);
            CHECK(tok == "cmd" || tok == "shift" || tok == "alt" || tok == "ctrl");
            if (n == std::string::npos) break;
            pos = n + 1;
        }
    }
    // The ones users expect.
    CHECK(findCmd(bar, Cmd::VSync) && findCmd(bar, Cmd::HomeLibrary));   // the in-window View menu's two extra items
    CHECK(findCmd(bar, Cmd::Open)->key == "cmd+o");
    CHECK(findCmd(bar, Cmd::Settings)->key == "cmd+,");
    CHECK(findCmd(bar, Cmd::Quit)->key == "cmd+q");
    CHECK(findCmd(bar, Cmd::Reset)->key == "cmd+r");
    CHECK(findCmd(bar, Cmd::Fullscreen)->key == "cmd+ctrl+f");
    // The tray and the Dock menu show no shortcuts.
    std::vector<std::string> tk;
    collectKeys(buildTrayMenu(maximal(false)), tk);
    collectKeys(buildDockMenu(maximal(false)), tk);
    CHECK(tk.empty());
}

static void testStateEquality() {
    MenuState a = maximal(false), b = maximal(false);
    CHECK(a == b);
    b.paused = true;
    CHECK(a != b);
    b = a;
    b.recents.push_back("/x.j64");
    CHECK(a != b);
    b = a;
    b.disc.labels[0] = "Changed";
    CHECK(a != b);
    b = a;
    b.fileManager = "Explorer";
    CHECK(a != b);
}

// ---- dispatch ---------------------------------------------------------------------------------------------------
struct Recorder : Actions {
    std::vector<std::string> log;
    void rec(const char* n, int v = -1) { log.push_back(v < 0 ? std::string(n) : std::string(n) + ":" + std::to_string(v)); }
    void open() override { rec("open"); }
    void openRecent(int i) override { rec("openRecent", i); }
    void clearRecents() override { rec("clearRecents"); }
    void bootBiosCd() override { rec("bootBiosCd"); }
    void bootBiosCart() override { rec("bootBiosCart"); }
    void showBiosSettings() override { rec("showBiosSettings"); }
    void newWindow() override { rec("newWindow"); }
    void toggleLibrary() override { rec("toggleLibrary"); }
    void closeGame() override { rec("closeGame"); }
    void revealGame() override { rec("revealGame"); }
    void quit() override { rec("quit"); }
    void togglePause() override { rec("togglePause"); }
    void reset() override { rec("reset"); }
    void toggleFastForward() override { rec("toggleFastForward"); }
    void saveState() override { rec("saveState"); }
    void loadState() override { rec("loadState"); }
    void setSlot(int n) override { rec("setSlot", n); }
    void showStateManager() override { rec("showStateManager"); }
    void showCheats() override { rec("showCheats"); }
    void toggleDiscTray() override { rec("toggleDiscTray"); }
    void selectDisc(int i) override { rec("selectDisc", i); }
    void toggleFullscreen() override { rec("toggleFullscreen"); }
    void toggleIntegerScale() override { rec("toggleIntegerScale"); }
    void toggleSmoothFilter() override { rec("toggleSmoothFilter"); }
    void toggleShader() override { rec("toggleShader"); }
    void toggleFps() override { rec("toggleFps"); }
    void toggleKeypadOverlay() override { rec("toggleKeypadOverlay"); }
    void toggleControlsOverlay() override { rec("toggleControlsOverlay"); }
    void toggleVsync() override { rec("toggleVsync"); }
    void toggleHomeLibrary() override { rec("toggleHomeLibrary"); }
    void showSettings() override { rec("showSettings"); }
    void toggleMute() override { rec("toggleMute"); }
    void showAbout() override { rec("showAbout"); }
    void showDataFolder() override { rec("showDataFolder"); }
    void openUrl(const char* url) override { log.push_back(std::string("openUrl:") + url); }
    void achSignIn() override { rec("achSignIn"); }
    void achSignOut() override { rec("achSignOut"); }
    void achToggleEnabled() override { rec("achToggleEnabled"); }
    void achToggleHardcore() override { rec("achToggleHardcore"); }
    void achToggleUnofficial() override { rec("achToggleUnofficial"); }
    void achShowList() override { rec("achShowList"); }
    void showNetplay() override { rec("showNetplay"); }
    void netplayDisconnect() override { rec("netplayDisconnect"); }
    void showWindow() override { rec("showWindow"); }
};

static std::string one(Cmd c, int arg, int recents = 5, int discs = 3) {
    Recorder r;
    bool ok = dispatch(c, arg, r, recents, discs);
    if (!ok) return r.log.empty() ? "rejected" : "rejected+called";
    return r.log.size() == 1 ? r.log[0] : "calls:" + std::to_string(r.log.size());
}

static void testDispatch() {
    CHECK(one(Cmd::Open, 0) == "open");
    CHECK(one(Cmd::OpenRecent, 3) == "openRecent:3");
    CHECK(one(Cmd::OpenRecent, 5) == "rejected");        // stale item: the list shrank
    CHECK(one(Cmd::OpenRecent, -1) == "rejected");
    CHECK(one(Cmd::SetSlot, 9) == "setSlot:9");
    CHECK(one(Cmd::SetSlot, 10) == "rejected");
    CHECK(one(Cmd::SetSlot, -2) == "rejected");
    CHECK(one(Cmd::DiscSelect, 2) == "selectDisc:2");
    CHECK(one(Cmd::DiscSelect, 3) == "rejected");        // only 3 discs now
    CHECK(one(Cmd::DiscSelect, 0, 5, 0) == "rejected");  // disc went away
    CHECK(one(Cmd::None, 0) == "rejected");
    CHECK(one(Cmd::Count_, 0) == "rejected");
    CHECK(one(Cmd::Website, 0) == std::string("openUrl:") + kWebsiteUrl);
    CHECK(one(Cmd::ReportIssue, 0) == std::string("openUrl:") + kIssuesUrl);
    CHECK(one(Cmd::Quit, 0) == "quit");
    CHECK(one(Cmd::ShowWindow, 0) == "showWindow");

    // Every command with a valid argument runs exactly one action, and no two commands run the same one.
    std::set<std::string> names;
    for (int c = (int)Cmd::None + 1; c < (int)Cmd::Count_; c++) {
        std::string got = one((Cmd)c, 1);
        CHECK(got.rfind("rejected", 0) != 0 && got.rfind("calls:", 0) != 0);
        // Website / ReportIssue differ by URL; strip arguments for the uniqueness test.
        names.insert(got);
    }
    CHECK((int)names.size() == (int)Cmd::Count_ - 1);
}

// ---- associations, launcher arguments, the event round trip ------------------------------------------------------------
static void testAssociations() {
    CHECK(associations().size() == 8);
    for (const char* e : {"j64", "jag", "rom", "abs", "cof", "cue", "chd", "cdi"}) CHECK(isAssociatedExtension(e));
    CHECK(isAssociatedExtension(".J64") && isAssociatedExtension("CUE"));
    // The core also opens bin / prg, but they are too generic to claim in a file manager.
    for (const char* e : {"bin", "prg", "zip", "iso", "", "."}) CHECK(!isAssociatedExtension(e));
    int games = 0, discs = 0;
    for (const Assoc& a : associations()) {
        if (!std::strcmp(a.group, "game")) games++;
        else if (!std::strcmp(a.group, "disc")) discs++;
        else CHECK(false);
    }
    CHECK(games == 5 && discs == 3);
}

static void testLauncherArgs() {
    char a0[] = "virtualjaguar", a1[] = "-psn_0_123456", a2[] = "-NSDocumentRevisionsDebugMode", a3[] = "YES",
         a4[] = "game.j64", a5[] = "--fullscreen", a6[] = "-AppleLanguages";
    char* argv[] = {a0, a1, a2, a3, a4, a5, a6};
    const int argc = 7;
    CHECK(ignorableLauncherArgs(argc, argv, 0) == 0);   // the program name is not an argument
    CHECK(ignorableLauncherArgs(argc, argv, 1) == 1);   // -psn_: skip one
    CHECK(ignorableLauncherArgs(argc, argv, 2) == 2);   // flag + its value
    CHECK(ignorableLauncherArgs(argc, argv, 4) == 0);   // a game path is ours
    CHECK(ignorableLauncherArgs(argc, argv, 5) == 0);   // so is --fullscreen
    CHECK(ignorableLauncherArgs(argc, argv, 6) == 1);   // flag at the end with no value: skip just it
    CHECK(ignorableLauncherArgs(argc, argv, 7) == 0 && ignorableLauncherArgs(argc, argv, -1) == 0);
}

static void testEventRoundTrip() {
    if (!SDL_Init(SDL_INIT_EVENTS)) {
        std::fprintf(stderr, "SKIP event round trip: %s\n", SDL_GetError());
        return;
    }
    SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);
    postCommand(Cmd::OpenRecent, 7);
    postCommand(Cmd::Pause);
    SDL_Event e;
    Cmd c = Cmd::None;
    int arg = -1;
    SDL_PumpEvents();
    CHECK(SDL_PollEvent(&e) && decodeCommandEvent(e, c, arg) && c == Cmd::OpenRecent && arg == 7);
    CHECK(SDL_PollEvent(&e) && decodeCommandEvent(e, c, arg) && c == Cmd::Pause && arg == 0);
    CHECK(!SDL_PollEvent(&e));
    // Other user events (the app's own "open this path") are not commands.
    SDL_zero(e);
    e.type = SDL_EVENT_USER;
    e.user.code = 0x56414A47;
    CHECK(!decodeCommandEvent(e, c, arg));
    // ... and a forged one with an out-of-range command is refused.
    e.user.code = kCommandEventCode;
    e.user.data1 = (void*)(intptr_t)99999;
    CHECK(!decodeCommandEvent(e, c, arg));
    e.user.data1 = (void*)(intptr_t)0;
    CHECK(!decodeCommandEvent(e, c, arg));
    SDL_Quit();
}

// ---- viewports gating (#833) --------------------------------------------------------------------------
static void testViewports() {
    namespace vp = vj::viewports;
    // On a real desktop driver the setting decides.
    CHECK(vp::decide(true, "cocoa", false).active);
    CHECK(vp::decide(true, "windows", false).active);
    CHECK(vp::decide(true, "x11", false).active);          // X11 / XWayland can place windows
    CHECK(vp::decide(true, "x11", false).why == vp::Why::Active);
    CHECK(!vp::decide(false, "cocoa", false).active);
    CHECK(vp::decide(false, "cocoa", false).why == vp::Why::UserOff);
    // Wayland cannot position secondary windows: forced off with a reason, even when asked for.
    CHECK(!vp::decide(true, "wayland", false).active);
    CHECK(vp::decide(true, "wayland", false).why == vp::Why::Wayland);
    CHECK(vp::explain(vp::Why::Wayland)[0] != 0);
    CHECK(vp::driverIsWayland("wayland") && !vp::driverIsWayland("x11") && !vp::driverIsWayland(nullptr));
    // Fullscreen keeps everything inside.
    CHECK(!vp::decide(true, "cocoa", true).active);
    CHECK(vp::decide(true, "cocoa", true).why == vp::Why::Fullscreen);
    // Headless runs (dummy video, or no driver at all) never get viewports.
    CHECK(!vp::decide(true, "dummy", false).active);
    CHECK(!vp::decide(true, "offscreen", false).active);
    CHECK(!vp::decide(true, nullptr, false).active);
    CHECK(vp::decide(true, "dummy", false).why == vp::Why::NoWindowSystem);
    // Nothing to say when it is simply on or off.
    CHECK(vp::explain(vp::Why::Active)[0] == 0 && vp::explain(vp::Why::UserOff)[0] == 0);
#if defined(__APPLE__) || defined(_WIN32)
    CHECK(vp::defaultEnabled());
#else
    CHECK(!vp::defaultEnabled());
#endif
}

int main() {
    testIdleModel();
    testStateReflected();
    testRecents();
    testCoverage();
    testShortcuts();
    testStateEquality();
    testDispatch();
    testAssociations();
    testLauncherArgs();
    testEventRoundTrip();
    testViewports();
    std::printf("platform_test: %d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
