// The menu model, the command dispatcher, the recent-games conversion and the association table.
// No SDL, no OS: see commands.h.
#include "platform/commands.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <map>

namespace vj {
namespace platform {

#define ELL "\xE2\x80\xA6"   // U+2026 HORIZONTAL ELLIPSIS, as the macOS HIG wants for "opens a dialog"

bool MenuState::operator==(const MenuState& o) const {
    return gameLoaded == o.gameLoaded && paused == o.paused && fastForward == o.fastForward &&
           fullscreen == o.fullscreen && canLoadState == o.canLoadState && stateSlot == o.stateSlot &&
           usedSlots == o.usedSlots && mute == o.mute && integerScale == o.integerScale &&
           smoothFilter == o.smoothFilter && shader == o.shader && showFps == o.showFps &&
           keypadOverlay == o.keypadOverlay && controlsOverlay == o.controlsOverlay &&
           libraryOpen == o.libraryOpen && disc == o.disc && recents == o.recents &&
           achAvailable == o.achAvailable && achSignedIn == o.achSignedIn && achBusy == o.achBusy &&
           achEnabled == o.achEnabled && achHardcore == o.achHardcore && achUnofficial == o.achUnofficial &&
           netplayConfigured == o.netplayConfigured && fileManager == o.fileManager;
}

const char* pauseTitle(bool paused) { return paused ? "Resume" : "Pause"; }

namespace {

MenuItem item(Cmd c, const std::string& title, const char* key = "", bool enabled = true, bool checked = false,
              int arg = 0) {
    MenuItem m;
    m.cmd = c;
    m.arg = arg;
    m.title = title;
    m.key = key;
    m.enabled = enabled;
    m.checked = checked;
    return m;
}

MenuItem sep() {
    MenuItem m;
    m.separator = true;
    return m;
}

MenuItem submenu(const std::string& title, std::vector<MenuItem> sub, bool enabled = true) {
    MenuItem m;
    m.title = title;
    m.submenu = true;
    m.enabled = enabled;
    m.sub = std::move(sub);
    return m;
}

std::vector<MenuItem> recentItems(const MenuState& s) {
    std::vector<MenuItem> out;
    std::vector<RecentEntry> list = nativeRecentList(s.recents);
    // The app's recent list can hold paths the filter dropped (empties), so the index the app gets
    // must be the position in ITS list: find it by path.
    for (const RecentEntry& e : list) {
        int idx = 0;
        for (size_t i = 0; i < s.recents.size(); i++)
            if (s.recents[i] == e.path) { idx = (int)i; break; }
        out.push_back(item(Cmd::OpenRecent, e.title, "", true, false, idx));
    }
    return out;
}

}  // namespace

MenuBar buildMenuBar(const MenuState& s) {
    MenuBar bar;
    const std::string fm = s.fileManager.empty() ? std::string("File Manager") : s.fileManager;

    bar.app.push_back(item(Cmd::About, "About Virtual Jaguar"));
    bar.app.push_back(sep());
    bar.app.push_back(item(Cmd::Settings, "Settings" ELL, "cmd+,"));
    // (the macOS renderer puts Services / Hide / Hide Others / Show All here)
    bar.app.push_back(sep());
    bar.app.push_back(item(Cmd::Quit, "Quit Virtual Jaguar", "cmd+q"));

    // ---- File ----
    {
        std::vector<MenuItem> recent = recentItems(s);
        const bool haveRecent = !recent.empty();
        if (haveRecent) {
            recent.push_back(sep());
            recent.push_back(item(Cmd::ClearRecents, "Clear Menu"));
        }
        std::vector<MenuItem> f;
        f.push_back(item(Cmd::Open, "Open" ELL, "cmd+o"));
        f.push_back(submenu("Open Recent", std::move(recent), haveRecent));
        f.push_back(item(Cmd::BootNoCart, "Boot Without Cartridge"));
        f.push_back(sep());
        f.push_back(item(Cmd::NewWindow, "New Window", "cmd+n"));
        f.push_back(item(Cmd::Library, "Game Library", "cmd+l", true, s.libraryOpen));
        f.push_back(sep());
        f.push_back(item(Cmd::CloseGame, "Close Game", "cmd+w", s.gameLoaded));
        f.push_back(item(Cmd::RevealGame, "Show in " + fm, "", s.gameLoaded));
        bar.menus.push_back(submenu("File", std::move(f)));
    }

    // ---- Emulation ----
    {
        std::vector<MenuItem> slots;
        for (int n = 0; n < 10; n++)
            slots.push_back(item(Cmd::SetSlot, "Slot " + std::to_string(n) + ((s.usedSlots >> n) & 1u ? "  (used)" : ""),
                                 "", true, s.stateSlot == n, n));
        std::vector<MenuItem> e;
        e.push_back(item(Cmd::Pause, pauseTitle(s.paused), "cmd+p", s.gameLoaded));
        e.push_back(item(Cmd::Reset, "Reset", "cmd+r", s.gameLoaded));
        e.push_back(item(Cmd::FastForward, "Fast-Forward (hold Tab)", "", s.gameLoaded, s.fastForward));
        e.push_back(item(Cmd::Mute, "Mute", "", true, s.mute));
        e.push_back(sep());
        e.push_back(item(Cmd::SaveState, "Save State", "cmd+s", s.gameLoaded));
        e.push_back(item(Cmd::LoadState, "Load State", "cmd+alt+l", s.gameLoaded && s.canLoadState));
        e.push_back(submenu("State Slot", std::move(slots)));
        e.push_back(item(Cmd::StateManager, "State Manager" ELL, "", s.gameLoaded));
        e.push_back(item(Cmd::Cheats, "Cheats" ELL, "", s.gameLoaded));
        if (s.disc.available) {
            e.push_back(sep());
            e.push_back(item(Cmd::DiscTray, s.disc.ejected ? "Close Tray" : "Open Tray"));
            if (s.disc.count > 1) {
                std::vector<MenuItem> discs;
                for (unsigned i = 0; i < s.disc.count; i++) {
                    std::string label = i < s.disc.labels.size() && !s.disc.labels[i].empty()
                                            ? s.disc.labels[i]
                                            : "Disc " + std::to_string(i + 1);
                    discs.push_back(item(Cmd::DiscSelect, label, "", true, s.disc.index == i, (int)i));
                }
                e.push_back(submenu("Disc", std::move(discs)));
            }
        }
        bar.menus.push_back(submenu("Emulation", std::move(e)));
    }

    // ---- View ----
    {
        std::vector<MenuItem> v;
        v.push_back(item(Cmd::Fullscreen, s.fullscreen ? "Exit Full Screen" : "Enter Full Screen", "cmd+ctrl+f"));
        v.push_back(sep());
        v.push_back(item(Cmd::IntegerScale, "Integer Scaling", "", true, s.integerScale));
        v.push_back(item(Cmd::SmoothFilter, "Smooth Filter", "", true, s.smoothFilter));
        v.push_back(item(Cmd::Shader, "Shader", "", true, s.shader));
        v.push_back(sep());
        v.push_back(item(Cmd::ShowFps, "Show Frame Rate", "", true, s.showFps));
        v.push_back(item(Cmd::KeypadOverlay, "Keypad Overlay", "", true, s.keypadOverlay));
        v.push_back(item(Cmd::ControlsOverlay, "Controls Overlay", "", true, s.controlsOverlay));
        bar.menus.push_back(submenu("View", std::move(v)));
    }

    // ---- Achievements (only when the build has RetroAchievements) ----
    if (s.achAvailable) {
        std::vector<MenuItem> a;
        if (s.achSignedIn) a.push_back(item(Cmd::AchSignOut, "Sign Out"));
        else a.push_back(item(Cmd::AchSignIn, s.achBusy ? "Signing In..." : "Sign In" ELL, "", !s.achBusy));
        a.push_back(sep());
        a.push_back(item(Cmd::AchEnable, "Enable RetroAchievements", "", true, s.achEnabled));
        a.push_back(item(Cmd::AchHardcore, "Hardcore Mode", "", true, s.achHardcore));
        a.push_back(item(Cmd::AchUnofficial, "Include Unofficial Achievements", "", true, s.achUnofficial));
        a.push_back(sep());
        a.push_back(item(Cmd::AchList, "Achievement List" ELL, "", s.gameLoaded));
        bar.menus.push_back(submenu("Achievements", std::move(a)));
    }

    // ---- Netplay ----
    {
        std::vector<MenuItem> n;
        n.push_back(item(Cmd::Netplay, "Netplay Window" ELL));
        if (s.netplayConfigured) n.push_back(item(Cmd::NetplayDisconnect, "Disconnect"));
        bar.menus.push_back(submenu("Netplay", std::move(n)));
    }

    // ---- Window: the OS fills it (Minimize, Zoom, Bring All to Front, the window list) ----
    bar.menus.push_back(submenu("Window", {}));

    // ---- Help ----
    {
        std::vector<MenuItem> h;
        h.push_back(item(Cmd::Website, "Virtual Jaguar Website"));
        h.push_back(item(Cmd::ReportIssue, "Report an Issue" ELL));
        h.push_back(sep());
        h.push_back(item(Cmd::ShowDataFolder, "Show Data Folder in " + fm));
        bar.menus.push_back(submenu("Help", std::move(h)));
    }
    return bar;
}

std::vector<MenuItem> buildTrayMenu(const MenuState& s) {
    std::vector<MenuItem> t;
    t.push_back(item(Cmd::ShowWindow, "Show Virtual Jaguar"));
    t.push_back(sep());
    t.push_back(item(Cmd::Pause, pauseTitle(s.paused), "", s.gameLoaded));
    t.push_back(item(Cmd::Reset, "Reset", "", s.gameLoaded));
    t.push_back(sep());
    std::vector<MenuItem> recent = recentItems(s);
    const bool haveRecent = !recent.empty();
    t.push_back(submenu("Recent Games", std::move(recent), haveRecent));
    t.push_back(item(Cmd::Open, "Open" ELL));
    t.push_back(item(Cmd::Library, "Game Library"));
    t.push_back(sep());
    t.push_back(item(Cmd::Quit, "Quit"));
    return t;
}

std::vector<MenuItem> buildDockMenu(const MenuState& s) {
    std::vector<MenuItem> d = recentItems(s);
    if (!d.empty()) d.push_back(sep());
    d.push_back(item(Cmd::Library, "Game Library"));
    d.push_back(item(Cmd::Open, "Open" ELL));
    if (s.gameLoaded) {
        d.push_back(sep());
        d.push_back(item(Cmd::Pause, pauseTitle(s.paused)));
        d.push_back(item(Cmd::Reset, "Reset"));
    }
    return d;
}

void collectCommands(const std::vector<MenuItem>& items, std::vector<Cmd>& out) {
    for (const MenuItem& m : items) {
        if (m.separator) continue;
        if (m.submenu) collectCommands(m.sub, out);
        else out.push_back(m.cmd);
    }
}

bool dispatch(Cmd cmd, int arg, Actions& a, int recentCount, int discCount) {
    switch (cmd) {
    case Cmd::Open: a.open(); return true;
    case Cmd::OpenRecent:
        if (arg < 0 || arg >= recentCount) return false;
        a.openRecent(arg);
        return true;
    case Cmd::ClearRecents: a.clearRecents(); return true;
    case Cmd::BootNoCart: a.bootNoCart(); return true;
    case Cmd::NewWindow: a.newWindow(); return true;
    case Cmd::Library: a.toggleLibrary(); return true;
    case Cmd::CloseGame: a.closeGame(); return true;
    case Cmd::RevealGame: a.revealGame(); return true;
    case Cmd::Quit: a.quit(); return true;
    case Cmd::Pause: a.togglePause(); return true;
    case Cmd::Reset: a.reset(); return true;
    case Cmd::FastForward: a.toggleFastForward(); return true;
    case Cmd::SaveState: a.saveState(); return true;
    case Cmd::LoadState: a.loadState(); return true;
    case Cmd::SetSlot:
        if (arg < 0 || arg > 9) return false;
        a.setSlot(arg);
        return true;
    case Cmd::StateManager: a.showStateManager(); return true;
    case Cmd::Cheats: a.showCheats(); return true;
    case Cmd::DiscTray: a.toggleDiscTray(); return true;
    case Cmd::DiscSelect:
        if (arg < 0 || arg >= discCount) return false;
        a.selectDisc(arg);
        return true;
    case Cmd::Fullscreen: a.toggleFullscreen(); return true;
    case Cmd::IntegerScale: a.toggleIntegerScale(); return true;
    case Cmd::SmoothFilter: a.toggleSmoothFilter(); return true;
    case Cmd::Shader: a.toggleShader(); return true;
    case Cmd::ShowFps: a.toggleFps(); return true;
    case Cmd::KeypadOverlay: a.toggleKeypadOverlay(); return true;
    case Cmd::ControlsOverlay: a.toggleControlsOverlay(); return true;
    case Cmd::Settings: a.showSettings(); return true;
    case Cmd::Mute: a.toggleMute(); return true;
    case Cmd::About: a.showAbout(); return true;
    case Cmd::ShowDataFolder: a.showDataFolder(); return true;
    case Cmd::Website: a.openUrl(kWebsiteUrl); return true;
    case Cmd::ReportIssue: a.openUrl(kIssuesUrl); return true;
    case Cmd::AchSignIn: a.achSignIn(); return true;
    case Cmd::AchSignOut: a.achSignOut(); return true;
    case Cmd::AchEnable: a.achToggleEnabled(); return true;
    case Cmd::AchHardcore: a.achToggleHardcore(); return true;
    case Cmd::AchUnofficial: a.achToggleUnofficial(); return true;
    case Cmd::AchList: a.achShowList(); return true;
    case Cmd::Netplay: a.showNetplay(); return true;
    case Cmd::NetplayDisconnect: a.netplayDisconnect(); return true;
    case Cmd::ShowWindow: a.showWindow(); return true;
    case Cmd::None:
    case Cmd::Count_: break;
    }
    return false;
}

// ---- recents ----------------------------------------------------------------------------------------
namespace {

bool isSep(char c) { return c == '/' || c == '\\'; }

std::string stemOf(const std::string& p) {
    size_t s = p.size();
    while (s > 0 && !isSep(p[s - 1])) s--;
    std::string name = p.substr(s);
    size_t dot = name.rfind('.');
    return dot == std::string::npos || dot == 0 ? name : name.substr(0, dot);
}

std::string parentName(const std::string& p) {
    size_t end = p.size();
    while (end > 0 && !isSep(p[end - 1])) end--;
    if (end == 0) return "";
    end--;   // the separator
    size_t s = end;
    while (s > 0 && !isSep(p[s - 1])) s--;
    return p.substr(s, end - s);
}

}  // namespace

std::vector<RecentEntry> nativeRecentList(const std::vector<std::string>& recents, size_t max) {
    std::vector<RecentEntry> out;
    for (const std::string& p : recents) {
        if (out.size() >= max) break;
        if (p.empty()) continue;
        bool dup = false;
        for (const RecentEntry& e : out)
            if (e.path == p) { dup = true; break; }
        if (dup) continue;
        RecentEntry e;
        e.path = p;
        e.title = stemOf(p);
        if (e.title.empty()) e.title = p;
        out.push_back(std::move(e));
    }
    // Two different files with the same name (same game from two folders): say which is which.
    std::map<std::string, int> count;
    for (const RecentEntry& e : out) count[e.title]++;
    for (RecentEntry& e : out) {
        if (count[e.title] < 2) continue;
        std::string parent = parentName(e.path);
        if (!parent.empty()) e.title += " (" + parent + ")";
    }
    return out;
}

// ---- associations -----------------------------------------------------------------------------------------
const std::vector<Assoc>& associations() {
    static const std::vector<Assoc> list = {
#define X(ext, group) {#ext, #group},
        VJ_ASSOC_LIST(X)
#undef X
    };
    return list;
}

bool isAssociatedExtension(const std::string& extIn) {
    std::string e = extIn;
    if (!e.empty() && e[0] == '.') e.erase(0, 1);
    for (char& c : e) c = (char)std::tolower((unsigned char)c);
    for (const Assoc& a : associations())
        if (e == a.ext) return true;
    return false;
}

// ---- launcher arguments -------------------------------------------------------------------------------------
int ignorableLauncherArgs(int argc, char** argv, int i) {
    if (i < 0 || i >= argc || !argv[i]) return 0;
    const char* s = argv[i];
    if (!std::strncmp(s, "-psn_", 5)) return 1;   // Finder, older macOS: a process serial number
    static const char* const withValue[] = {"-NSDocumentRevisionsDebugMode", "-ApplePersistenceIgnoreState",
                                            "-AppleLanguages", "-AppleLocale", "-NSShowAllViews"};
    for (const char* w : withValue)
        if (!std::strcmp(s, w)) return i + 1 < argc ? 2 : 1;
    return 0;
}

}  // namespace platform
}  // namespace vj
