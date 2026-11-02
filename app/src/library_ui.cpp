// The "Game library" window: toolbar, folder list, and the box art grid.
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <utility>

#include "app.h"
#include "desktop_nav_ui.h"
#include "imgui.h"
#include "library_service.h"
#include "ui_util.h"
#include "window_chrome.h"

namespace vj {

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

uint32_t fnv(const std::string& s) {
    uint32_t h = 2166136261u;
    for (unsigned char c : s) {
        h ^= c;
        h *= 16777619u;
    }
    return h;
}

enum Filter { kAll, kUnverified };

// The search box and the "Not verified" filter, on whatever list is being shown.
bool matches(const lib::Entry& e, int filter, const std::string& needle) {
    if (filter == kUnverified && e.match == lib::Match::Crc) return false;
    if (needle.empty()) return true;
    return lower(e.title).find(needle) != std::string::npos || lower(e.file.path).find(needle) != std::string::npos;
}

bool passes(const lib::Entry& e, int section, int filter, const std::string& needle) {
    return (int)e.section == section && matches(e, filter, needle);
}

// "2026-10-13 19:31" in local time, from seconds since the epoch.
std::string stamp(int64_t seconds) {
    SDL_DateTime dt;
    if (seconds <= 0 || !SDL_TimeToDateTime((SDL_Time)seconds * 1000000000LL, &dt, true)) return std::string();
    char b[32];
    std::snprintf(b, sizeof b, "%04d-%02d-%02d %02d:%02d", dt.year, dt.month, dt.day, dt.hour, dt.minute);
    return b;
}

#ifdef __APPLE__
const char* const kRevealLabel = "Show in Finder";
#elif defined(_WIN32)
const char* const kRevealLabel = "Show in File Explorer";
#else
const char* const kRevealLabel = "Show in file manager";
#endif

// A five-point star centred in the box, filled (a favorite) or an outline (a hint on a hovered tile).
void drawStar(ImDrawList* dl, ImVec2 c, float r, bool filled, bool hot) {
    ImVec2 v[10];
    for (int i = 0; i < 10; i++) {
        float a = -1.5707963f + (float)i * 0.62831853f;
        float rr = (i & 1) ? r * 0.42f : r;
        v[i] = ImVec2(c.x + std::cos(a) * rr, c.y + std::sin(a) * rr);
    }
    // The star sits on its own dark disc over the art, so these are the same in both themes.
    const theme::Palette& p = ui::pal();
    const ImU32 fill = ui::u32(hot ? p.starFillHot : p.starFill);
    if (filled) {
        for (int i = 0; i < 10; i++) dl->AddTriangleFilled(c, v[i], v[(i + 1) % 10], fill);
        dl->AddPolyline(v, 10, ui::u32(p.starEdge), ImDrawFlags_Closed, 1.2f);
    } else {
        dl->AddPolyline(v, 10, hot ? ui::u32(p.starFillHot) : ui::u32(p.starOutline), ImDrawFlags_Closed, 1.6f);
    }
}

// ---- controller navigation (#1021) ------------------------------------------------------------------------------------
// The grid publishes its lists to app.deskPad.nav each frame, draws the focus ring on the focused tile, scrolls to it and
// executes the commands the pad queued (desktop_nav_app.h).  The rest -- the focus model, the ring, the hint -- is in
// desktop_nav.cpp / desktop_nav_ui.cpp.

// A command that needs a tile's ID scope (its menu, a BIOS tile's popup) waits here until that tile is drawn; the grid
// clips its rows, so a tile scrolled out of sight is submitted only after the scroll the command asked for.
struct TileCmd {
    bool active = false;
    std::string key;
    int age = 0;      // frames it has waited
    int frame = 0;    // ImGui frame it was queued in: the library not being drawn does not age it, the frame count does
} g_tileCmd;

// What the pad changed that the draw pass must not do midway (the tab the lists were built for, the window).
struct DeskDefer {
    int tabStep = 0;
    bool back = false;
} g_defer;

bool g_searchActive = false;   // the search box has keyboard focus (drawToolbar)
uint32_t g_searchSeq = 0;      // deskPad.seq when it got it

struct StateRef {
    const lib::Entry* owner;   // null: the game is not in the library, its states cannot be started
    int slot;
    std::string key;
};
// This frame's lists, so a command (which names an item by key) finds the entry.
struct DeskFrame {
    std::vector<const lib::Entry*> fav, recent, grid;
    std::vector<StateRef> states;
    bool scroll = false;   // the focus moved (or a command needs its tile drawn): bring it into view
} g_df;

const std::vector<const lib::Entry*>& entriesOf(DeskRegion r) {
    static const std::vector<const lib::Entry*> none;   // the States tab's shots are g_df.states, not entries
    return r == DeskRegion::FavStrip ? g_df.fav : r == DeskRegion::RecentStrip ? g_df.recent : r == DeskRegion::Grid ? g_df.grid : none;
}

const lib::Entry* findEntry(DeskRegion r, const std::string& key) {
    for (const lib::Entry* e : entriesOf(r))
        if (e->file.path == key) return e;
    return nullptr;
}

// A click and the pad's Accept start a game the same way.  The pad also arms the release guard (the confirm press that
// started it must not reach the game) and skips the resume prompt, which needs a mouse (as TV mode's launch does).
void activateEntry(App& app, LibraryService& svc, const lib::Entry& e, bool fromPad) {
    if (fromPad) app.input.armReleaseGuard();
    svc.launch(e, fromPad);
}

void queueTileCmd(const std::string& key) {
    g_tileCmd.active = true;
    g_tileCmd.key = key;
    g_tileCmd.age = 0;
    g_tileCmd.frame = ImGui::GetFrameCount();
    g_df.scroll = true;
}

// Called inside a tile's ID scope: opens `popupId` there when the pad asked for it, under the tile (above it when the
// screen has no room below) and marks the popup as the pad's.  True when it did.
bool padOpensPopup(App& app, const lib::Entry& e, bool focus, ImVec2 p0, ImVec2 size, const char* popupId) {
    if (!focus || !g_tileCmd.active || g_tileCmd.key != e.file.path) return false;
    g_tileCmd.active = false;
    if (ImGui::GetFrameCount() - g_tileCmd.frame > 8) return false;   // stale: queued before the library went away
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const bool below = p0.y + size.y + ImGui::GetFontSize() * 14.0f < vp->WorkPos.y + vp->WorkSize.y;
    ImGui::SetNextWindowPos(below ? ImVec2(p0.x, p0.y + size.y) : p0, ImGuiCond_Always, ImVec2(0.0f, below ? 0.0f : 1.0f));
    ImGui::OpenPopup(popupId);
    app.deskPad.padPopup = true;
    return true;
}

// The pad's queued commands, once the frame's lists are published.  Any pad press while the search box has focus leaves the
// box (pads do not type).  A direction has already moved the focus by now; a Back / Accept / menu / favorite command of that
// press is dropped and its release is forgotten (in.reset), so it neither acts nor closes the window.  Tab switches still run.
void runDeskCommands(App& app, LibraryService& svc) {
    DesktopPad& d = app.deskPad;
    if (g_searchActive && d.active && d.seq != g_searchSeq) {
        deskLeaveTextInput();
        g_searchActive = false;
        d.in.reset();   // the matching release must not fire a tap on the grid
        std::vector<DeskCmd> keep;
        for (const DeskCmd& c : d.pending)
            if (c.kind == DeskCmd::TabPrev || c.kind == DeskCmd::TabNext) keep.push_back(c);
        d.pending.swap(keep);
    }
    if (d.pending.empty()) return;
    std::vector<DeskCmd> cmds;
    cmds.swap(d.pending);
    if (!d.active) return;
    const DeskRegion region = d.nav.region();
    for (const DeskCmd& c : cmds) {
        switch (c.kind) {
        case DeskCmd::Activate:
            if (region == DeskRegion::States) {
                for (const StateRef& s : g_df.states)
                    if (s.key == c.key && s.owner) {
                        app.input.armReleaseGuard();
                        svc.launchState(*s.owner, s.slot);
                        break;
                    }
            } else if (const lib::Entry* e = findEntry(region, c.key)) {
                if (e->section == lib::Section::Bios) queueTileCmd(c.key);   // its popup, as a click opens it
                else activateEntry(app, svc, *e, true);
            }
            break;
        case DeskCmd::OpenMenu:
            if (region != DeskRegion::States && findEntry(region, c.key)) queueTileCmd(c.key);
            break;
        case DeskCmd::ToggleFavorite:
            if (const lib::Entry* e = findEntry(region, c.key))
                if (e->section != lib::Section::Bios) svc.setFavorite(*e, !svc.isFavorite(*e));
            break;
        case DeskCmd::TabPrev: g_defer.tabStep--; break;
        case DeskCmd::TabNext: g_defer.tabStep++; break;
        case DeskCmd::FocusSearch: svc.focusSearch = true; break;
        case DeskCmd::Back: g_defer.back = true; break;
        }
    }
}

// After the grid is drawn (also when it returned early): the commands nothing drew for, then what waited for the end.
void finishDeskFrame(App& app, LibraryService& svc) {
    DesktopPad& d = app.deskPad;
    if (!d.active) g_tileCmd.active = false;   // a menu queued before the pad lost the library must not pop up later
    if (g_tileCmd.active && ++g_tileCmd.age >= 4) g_tileCmd.active = false;   // its tile never came: give up
    if (g_defer.tabStep != 0) {
        const std::vector<int> tabs = lib::visibleTabs(svc.favoritesShow, svc.recentsShow);
        int at = 0;
        for (size_t i = 0; i < tabs.size(); i++)
            if (tabs[i] == svc.activeTab()) at = (int)i;
        const int to = std::min(std::max(at + g_defer.tabStep, 0), (int)tabs.size() - 1);   // no wrap, as the focus has none
        if (to != at) {
            svc.section = tabs[(size_t)to];
            svc.saveSettings();
        }
    }
    if (g_defer.back && !app.homeVisible()) {   // the home library is not a window: nothing to close
        svc.show = false;
        if (app.host.isLoaded()) app.input.armReleaseGuard();   // the Back press must not reach the paused game
    }
    g_defer = DeskDefer();
}

}  // namespace

// Favorites and recently played: where each one shows.  Used by the toolbar popup and by Settings.
void drawLibraryListOptions(LibraryService& svc) {
    bool changed = false;
    auto row = [&](const char* name, const char* id, int& v) {
        ImGui::TextUnformatted(name);
        ImGui::SetNextItemWidth(ui::em(24.0f));
        if (ImGui::BeginCombo(id, lib::showAsLabel(v))) {
            for (int m = 0; m < lib::kShowAsCount; m++) {
                if (ImGui::Selectable(lib::showAsLabel(m), v == m)) {
                    v = m;
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }
    };
    row("Favorites", "##favshow", svc.favoritesShow);
    row("Recently played", "##recshow", svc.recentsShow);
    if (changed) svc.saveSettings();
    ui::hint("A tab lists both cartridges and CDs.  In the Cartridges and Jaguar CD tabs a strip at the top shows "
             "the games of that media only.  Right-click a game, or click the star on its tile, to favorite it; "
             "games you boot are added to Recently played (the last %zu).",
             lib::kMaxRecentGames);
    if (svc.instance > 1)
        ui::hint("This is copy %d of the app.  The lists are shared by all copies and every change is saved at once; "
                 "these display options follow the other settings and are not saved by extra copies.",
                 svc.instance);
    if (ImGui::Button("Clear recently played")) svc.clearRecents();
}

namespace {

// One toolbar for the window and the home screen.  Items flow onto a second row when the
// window is narrow (ui::Flow), and the search box takes a share of the width.
void drawToolbar(App& app, LibraryService& svc, char* search, int& filter, bool home) {
    ui::Flow flow;
    if (svc.focusSearch) {
        ImGui::SetKeyboardFocusHere();
        svc.focusSearch = false;
    }
    ImGui::SetNextItemWidth(ui::fitWidth(0.34f, 10.0f, 22.0f));
    ImGui::InputTextWithHint("##search", "Search", search, 128);
    const bool searchActive = ImGui::IsItemActive();
    if (searchActive && !g_searchActive) g_searchSeq = app.deskPad.seq;   // a pad press after this leaves the box (runDeskCommands)
    g_searchActive = searchActive;
    static const char* filters[] = {"All", "Not verified"};
    float fw = ui::em(9.5f);
    flow.next(fw);
    ImGui::SetNextItemWidth(fw);
    ImGui::Combo("##filter", &filter, filters, 2);
    float sw = ui::em(8.0f);
    flow.next(sw);
    ImGui::SetNextItemWidth(sw);
    ImGui::SliderFloat("##size", &svc.cellScale, lib::kTileScaleMin, lib::kTileScaleMax, "Size %.1fx");
    if (ImGui::IsItemDeactivatedAfterEdit()) svc.saveSettings();

    flow.next(ui::Flow::buttonW("Add folder..."));
    if (ImGui::Button("Add folder...")) svc.addFolderDialog(app.window());
    flow.next(ui::Flow::buttonW("Rescan"));
    if (ImGui::Button("Rescan")) svc.rescan();
    flow.next(ui::Flow::buttonW("Update database"));
    if (ImGui::Button("Update database")) svc.updateDatabase();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Downloads the latest game lists from libretro-database.\n"
                          "Cartridges: %s\nCD images: %s",
                          svc.cartDatInfo().c_str(), svc.discDatInfo().c_str());
    flow.next(ui::Flow::checkW("Download art and cheat packs"));
    bool art = svc.downloadArt;
    if (ImGui::Checkbox("Download art and cheat packs", &art)) svc.setDownloadArt(art);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Fetches missing covers from thumbnails.libretro.com, once per title, and lets the Cheats window\n"
                          "look for community cheat packs on this project's website.\n"
                          "Everything already downloaded keeps working offline.");
    flow.next(ui::Flow::buttonW("Favorites & recents..."));
    if (ImGui::Button("Favorites & recents...")) ImGui::OpenPopup("##listopts");
    if (ImGui::BeginPopup("##listopts")) {
        drawLibraryListOptions(svc);
        ImGui::EndPopup();
    }
    if (!home) {   // the home screen goes away when a game loads: nothing to close
        flow.next(ui::Flow::checkW("Close on launch"));
        bool hide = svc.hideOnLaunch;
        if (ImGui::Checkbox("Close on launch", &hide)) {
            svc.hideOnLaunch = hide;
            svc.saveSettings();
        }
    }
}

// Cartridges / Jaguar CD / [Favorites] / [Recently played] / Save states / BIOS & system.  The tabs carry no
// content of their own: the selected one (remembered across runs) decides what is listed below.  Favorites and
// Recently played have a tab only when their display option (View) says so.  `last` is this tab bar's own idea
// of the tab, so the home screen and the window stay in step when the other one changes it.
void drawSections(LibraryService& svc, const char* id, int& last) {
    size_t n[lib::kTabCount] = {0, 0, 0, 0, 0, 0};
    for (const lib::Entry& e : svc.entries()) n[(int)e.section]++;
    if (lib::showsTab(svc.favoritesShow)) n[lib::TabFavorites] = svc.favoriteEntries().size();
    if (lib::showsTab(svc.recentsShow)) n[lib::TabRecents] = svc.recentEntries().size();
    n[lib::TabStates] = svc.stateGames().size();
    const int cur = svc.activeTab();
    if (svc.section != cur) {   // the remembered tab is hidden now (an option changed): Cartridges
        svc.section = cur;
        svc.saveSettings();
    }
    if (!ImGui::BeginTabBar(id, ImGuiTabBarFlags_FittingPolicyScroll | ImGuiTabBarFlags_NoTooltip)) return;
    for (int t : lib::visibleTabs(svc.favoritesShow, svc.recentsShow)) {
        char label[96];
        std::snprintf(label, sizeof label, "%s (%zu)###sec%d", lib::tabName(t), n[t], t);
        ImGuiTabItemFlags fl = (last != svc.section && svc.section == t) ? ImGuiTabItemFlags_SetSelected : 0;
        if (ImGui::BeginTabItem(label, nullptr, fl)) {
            if (svc.section != t && last == svc.section) {   // the user clicked this tab
                svc.section = t;
                svc.saveSettings();
            }
            ImGui::EndTabItem();
        }
    }
    ImGui::EndTabBar();
    last = svc.section;
}

void drawFolders(LibraryService& svc) {
    const std::vector<std::string>& f = svc.folders();
    if (f.empty()) return;
    char head[32];
    std::snprintf(head, sizeof head, "Folders (%zu)", f.size());
    if (!ImGui::TreeNodeEx(head, ImGuiTreeNodeFlags_None)) return;
    int remove = -1;
    for (size_t i = 0; i < f.size(); i++) {
        ImGui::PushID((int)i);
        if (ImGui::SmallButton("Remove")) remove = (int)i;
        ImGui::SameLine();
        ImGui::TextWrapped("%s", f[i].c_str());
        ImGui::PopID();
    }
    ImGui::TreePop();
    if (remove >= 0) svc.removeFolder((size_t)remove);
}

void drawStatus(App& app, LibraryService& svc) {
    if (svc.scanning()) {
        size_t t = svc.scanTotal();
        if (t > 0) ImGui::TextWrapped("Scanning... hashing %zu of %zu", svc.scanDone(), t);
        else ImGui::TextUnformatted("Scanning...");
    } else {
        std::string s = svc.status();
        if (!s.empty()) ui::hint("%s", s.c_str());
        else ui::hint("%zu games%s", svc.entries().size(), svc.netBusy() ? "  (downloading)" : "");
    }
    if (deskPadShown(app)) {   // the pad's buttons, as its family labels them: on the status line, so no line comes and goes
        ImGui::SameLine(0.0f, ui::em(2.0f));
        ImGui::TextDisabled("%s", deskHintLine(app, svc.activeTab() == lib::TabStates).c_str());
    }
    if (!svc.haveCartDat() && !svc.haveDiscDat()) {
        ui::coloredWrapped(ui::vec4(ui::pal().warning),
                           "No game database found: titles come from file names. Use Update database to download it.");
    }
}

// The Jaguar CD tab: libretro-thumbnails has no repository for the system, so disc art comes from the project
// mirror alone.  Only said while some disc on the tab came up empty, with how to give it a cover.
void drawSectionHint(LibraryService& svc) {
    if (svc.section != (int)lib::Section::Disc) return;
    size_t n = svc.missingArt(lib::Section::Disc);
    if (n == 0) return;
    if (!svc.artMirrorUrl.empty() && svc.downloadArt.load())
        ui::hint("%zu disc%s without art: libretro-thumbnails has no Jaguar CD art; the project mirror covers most "
                 "discs -- right-click a disc to set a cover (or drop a PNG on it).",
                 n, n == 1 ? "" : "s");
    else
        ui::hint("%zu disc%s without art: libretro-thumbnails has no Jaguar CD art -- right-click a disc to set a "
                 "cover (or drop a PNG on it).",
                 n, n == 1 ? "" : "s");
}

// A BIOS or system file: shows what it is, whether the core finds it where it is, and whether it is the one in
// use.  A click opens its menu: Use this BIOS (the core runs exactly this file, any name, any folder), Boot (a boot
// ROM with an empty slot, the CD BIOS with no disc, the Memory Track cartridge), Copy to the system folder (the
// fixed name the core's own search and RetroArch look for).
void drawBiosCell(App& app, LibraryService& svc, const lib::Entry& e, float cellW, float artH, float textH, bool focus) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImGui::PushID(e.file.path.c_str());
    bool clicked = ImGui::InvisibleButton("##cell", ImVec2(cellW, artH + textH));
    bool hovered = ImGui::IsItemHovered();
    padOpensPopup(app, e, focus, p0, ImVec2(cellW, artH + textH), "##bios");   // the pad's Accept / menu on this tile
    const lib::BiosInfo& b = e.bios;
    const lib::BiosUse use = lib::biosUse(b, e.file.path, e.file.size, svc.systemDir);
    const std::string installAs = use.core || svc.systemDir.empty() ? std::string() : lib::biosInstallName(b, e.file.size);
    const bios::Role role = bios::roleOf(b, e.file.size);
    const bool inUse = role != bios::None && app.biosChoice.of(role) == absolutePath(e.file.path);
    const bios::BootInfo boot = bios::bootInfo(b, e.file.size);
    if (clicked) ImGui::OpenPopup("##bios");
    if (ImGui::BeginPopup("##bios")) {
        ImGui::TextDisabled("%s", e.title.c_str());
        ImGui::Separator();
        if (role != bios::None) {
            const std::string useLabel = std::string(inUse ? "In use as the " : "Use as the ") + bios::roleLabel(role);
            if (ImGui::MenuItem(useLabel.c_str(), nullptr, inUse)) {
                app.setBiosChoice(role, inUse ? std::string() : e.file.path);   // clicking the one in use goes back to Auto
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                ui::beginWrappedTooltip();
                ImGui::TextWrapped("The core runs exactly this file as the %s, whatever it is called and wherever it lives. "
                                   "Click again to go back to Auto.%s",
                                   bios::roleLabel(role),
                                   role == bios::Cd ? " It is used when CD games boot through the Real BIOS, for audio CDs, and when you Boot a BIOS."
                                                    : " It is used when cartridges boot through the Real boot ROM, for GPU-only demos, and when you Boot a BIOS.");
                ui::endWrappedTooltip();
            }
        } else {
            ImGui::BeginDisabled();
            ImGui::MenuItem("Use this BIOS");
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip("Only a 128 KiB boot ROM or a 256 KiB CD BIOS can be chosen.");
        }
        ImGui::BeginDisabled(boot.kind == bios::BootKind::None);
        if (ImGui::MenuItem("Boot")) {
            if (boot.kind == bios::BootKind::MemoryTrack) app.openContent(e.file.path);
            else app.bootBios(role, e.file.path);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort)) {
            ui::beginWrappedTooltip();
            ImGui::TextWrapped("%s", boot.text.c_str());
            ui::endWrappedTooltip();
        }
        if (!installAs.empty()) {
            const std::string cp = "Copy to the system folder as " + installAs;
            if (ImGui::MenuItem(cp.c_str())) {
                svc.installBios(e);
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }
    const bool popupOpen = ImGui::IsPopupOpen("##bios");   // under this tile's ID scope
    ImGui::PopID();
    const bool found = use.core;
    const bool readable = b.role == lib::BiosRole::CartBoot || b.role == lib::BiosRole::CdBios;

    ImVec2 a0 = p0, a1(p0.x + cellW, p0.y + artH);
    const bool ring = focus && deskPadShown(app);
    if (ring) deskDrawFocusShadow(dl, a0, a1, 4.0f);
    dl->AddRectFilled(a0, a1, ui::u32(hovered ? ui::pal().cardBgHover : ui::pal().cardBg), 4.0f);
    ImColor c = ImColor::HSV(b.role == lib::BiosRole::CdBios ? 0.58f : b.role == lib::BiosRole::CartBoot ? 0.08f : 0.33f,
                             0.30f, ui::pal().biosArtVal);
    dl->AddRectFilled(ImVec2(a0.x + 6, a0.y + 6), ImVec2(a1.x - 6, a1.y - 6), c, 4.0f);
    const char* label = lib::biosRoleLabel(b.role);
    float fs = ImGui::GetFontSize() * 1.2f;
    ImVec2 ts = ImGui::GetFont()->CalcTextSizeA(fs, cellW - 20.0f, cellW - 20.0f, label);
    dl->AddText(ImGui::GetFont(), fs, ImVec2(p0.x + (cellW - ts.x) * 0.5f, p0.y + (artH - ts.y) * 0.5f),
                ui::u32(ui::pal().placeholderText, 215.0f / 235.0f), label, nullptr, cellW - 20.0f);
    if (hovered) dl->AddRect(a0, a1, ui::u32(ui::pal().hoverEdge), 4.0f, 0, 2.0f);
    if (ring) deskDrawFocusRing(dl, a0, a1, 4.0f);

    float lh = ImGui::GetFontSize() + 2.0f;
    ImVec4 clip(p0.x, p0.y + artH, p0.x + cellW, p0.y + artH + textH);
    // title: at most two lines, clipped above the status line, which is always the tile's last line and short
    ImVec4 clipT(p0.x, p0.y + artH, p0.x + cellW, p0.y + artH + textH - lh);
    dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(p0.x + 2, p0.y + artH + 3), ui::u32(ui::pal().text),
                e.title.c_str(), nullptr, cellW - 4, &clipT);
    const char* st = inUse ? "In use" : found ? "Read by the core" : readable ? "Click to use or boot" : "Not read";
    ImU32 sc = ui::u32(inUse || found ? ui::pal().success : readable ? ui::pal().warning : ui::pal().textMuted);
    dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(p0.x + 2, p0.y + artH + textH - lh), sc, st, nullptr,
                cellW - 4, &clip);

    if (hovered && !popupOpen) {
        ui::beginWrappedTooltip();
        ImGui::TextWrapped("%s", e.title.c_str());
        ImGui::TextDisabled("%s%s", lib::biosRoleLabel(b.role), b.recognized ? "" : " (checksum not in the core's list)");
        ImGui::Separator();
        ImGui::TextDisabled("%s", e.file.path.c_str());
        if (e.file.crcWhole) ImGui::TextDisabled("CRC32 %08X", e.file.crcWhole);
        ImGui::Spacing();
        ImGui::TextWrapped("%s", use.text.c_str());
        ImGui::TextWrapped("Click for: Use this BIOS%s%s.", boot.kind != bios::BootKind::None ? ", Boot" : "",
                           installAs.empty() ? "" : ", Copy to the system folder");
        if (readable)
            ImGui::TextWrapped("The core has built-in copies of the Series K and Model M boot ROMs and both CD BIOS "
                               "images, so nothing is missing without this file.");
        ui::endWrappedTooltip();
    }
}

// A small dark label in a tile's corner ("CD", "Cart").
void drawBadge(ImDrawList* dl, ImVec2 at, const char* label) {
    dl->AddRectFilled(at, ImVec2(at.x + ImGui::CalcTextSize(label).x + 8, at.y + ImGui::GetFontSize() + 2),
                      ui::u32(ui::pal().badgeBg), 3.0f);
    dl->AddText(ImVec2(at.x + 4, at.y + 1), ui::u32(ui::pal().badgeText), label);
}

// The card shown while a tile has no art: a colour from the title, the title itself, and a CD / Cart badge.
void drawPlaceholder(ImDrawList* dl, ImVec2 a0, ImVec2 a1, const lib::Entry& e, bool loading) {
    ImColor c = ImColor::HSV((float)(fnv(e.title) % 360) / 360.0f, ui::pal().placeholderSat, ui::pal().placeholderVal);
    ImVec2 i0(a0.x + 6, a0.y + 6), i1(a1.x - 6, a1.y - 6);
    dl->AddRectFilled(i0, i1, c, 4.0f);
    const float pad = 5.0f;
    const float top = a0.y + 6 + ImGui::GetFontSize() + 8;   // below the badge
    ImVec4 clip(i0.x + pad, top, i1.x - pad, i1.y - pad);
    const float fs = ImGui::GetFontSize();
    const float ww = std::max(8.0f, clip.z - clip.x);
    const std::string& t = e.title.empty() ? e.file.path : e.title;
    ImVec2 ts = ImGui::GetFont()->CalcTextSizeA(fs, 1e9f, ww, t.c_str());
    float y = std::max(top, (top + clip.w - ts.y) * 0.5f);
    dl->AddText(ImGui::GetFont(), fs, ImVec2(clip.x, y), ui::u32(ui::pal().placeholderText), t.c_str(), nullptr, ww, &clip);
    if (loading) {
        ImVec2 ds = ImGui::CalcTextSize("...");
        dl->AddText(ImVec2((a0.x + a1.x - ds.x) * 0.5f, i1.y - ds.y - 2), ui::u32(ui::pal().placeholderDots), "...");
    }
}

// A save state as a picture and two lines: its screenshot (letterboxed into a 4:3 box), "Slot N" and when it was
// saved.  The caller's cursor is where the picture goes; returns the box so the caller can overlay buttons.
void drawStateShot(ImDrawList* dl, LibraryService& svc, const lib::StateInfo& st, ImVec2 p0, ImVec2 box, bool hot,
                   bool ring = false) {
    if (ring) deskDrawFocusShadow(dl, p0, ImVec2(p0.x + box.x, p0.y + box.y), 3.0f);
    dl->AddRectFilled(p0, ImVec2(p0.x + box.x, p0.y + box.y), ui::u32(hot ? ui::pal().stateBgHover : ui::pal().stateBg), 3.0f);
    ArtView v = svc.stateThumb(st);
    if (v.state == ArtView::Ready && v.tex && v.w > 0 && v.h > 0) {
        float k = std::min(box.x / (float)v.w, box.y / (float)v.h);
        float w = (float)v.w * k, h = (float)v.h * k;
        ImVec2 i0(p0.x + (box.x - w) * 0.5f, p0.y + (box.y - h) * 0.5f);
        dl->AddImage(ImTextureRef((ImTextureID)(intptr_t)v.tex), i0, ImVec2(i0.x + w, i0.y + h));
    } else {
        const char* txt = v.state == ArtView::Loading ? "..." : "no screenshot";
        ImVec2 ts = ImGui::CalcTextSize(txt);
        dl->AddText(ImVec2(p0.x + (box.x - ts.x) * 0.5f, p0.y + (box.y - ts.y) * 0.5f), ui::u32(ui::pal().textMuted), txt);
    }
    if (hot) dl->AddRect(p0, ImVec2(p0.x + box.x, p0.y + box.y), ui::u32(ui::pal().hoverEdge), 3.0f, 0, 2.0f);
    if (ring) deskDrawFocusRing(dl, p0, ImVec2(p0.x + box.x, p0.y + box.y), 3.0f);
}

// One line of the tile menu's Load state list: a small screenshot, "Slot N" and the time.  True when clicked.
bool stateMenuRow(LibraryService& svc, const lib::StateInfo& st) {
    const float fs = ImGui::GetFontSize();
    ImVec2 box(fs * 7.0f, fs * 7.0f * 0.75f);
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    const float rowH = box.y + 4.0f;
    bool clicked = ImGui::Selectable("##state", false, ImGuiSelectableFlags_None, ImVec2(fs * 16.5f, rowH));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    drawStateShot(dl, svc, st, ImVec2(p0.x + 2, p0.y + 2), box, false);
    const std::string l1 = (st.disc.empty() ? "" : pathStem(st.disc) + " - ") + lib::stateLabel(st.slot);
    dl->AddText(ImVec2(p0.x + box.x + 10, p0.y + rowH * 0.5f - fs - 1), ImGui::GetColorU32(ImGuiCol_Text), l1.c_str());
    dl->AddText(ImVec2(p0.x + box.x + 10, p0.y + rowH * 0.5f + 1), ImGui::GetColorU32(ImGuiCol_TextDisabled), stamp(st.mtime).c_str());
    return clicked;
}

// The tile's right-click menu.  Built only while open, so none of this runs for every tile every frame.
void drawTileMenu(App& app, LibraryService& svc, const lib::Entry& e) {
    // From the pad's menu the Play row starts the game as the grid's Accept does (no resume prompt, the confirm press guarded).
    if (ImGui::MenuItem("Play")) activateEntry(app, svc, e, app.deskPad.padPopup);
    const lib::StateGame* sg = svc.statesFor(e);
    if (ImGui::BeginMenu("Load state", sg && !sg->states.empty())) {
        int shown = 0;
        for (const lib::StateInfo& st : sg->states) {   // newest first
            if (++shown > 10) break;
            ImGui::PushID(st.path.c_str());
            if (stateMenuRow(svc, st)) svc.launchState(e, st.slot, st.disc);
            ImGui::PopID();
        }
        ImGui::EndMenu();
    }
    ImGui::Separator();
    const bool fav = svc.isFavorite(e);
    if (ImGui::MenuItem(fav ? "Remove from Favorites" : "Add to Favorites")) svc.setFavorite(e, !fav);
    if (svc.isRecent(e) && ImGui::MenuItem("Remove from Recently Played")) svc.removeRecent(e);
    ImGui::Separator();
    if (ImGui::MenuItem("Set cover image...")) svc.setCoverDialog(app.window(), e);
    if (ImGui::MenuItem("Remove cover image", nullptr, false, svc.hasCustomCover(e))) svc.removeCover(e);
    ImGui::Separator();
    if (ImGui::MenuItem(kRevealLabel)) {
        const std::string url = lib::folderUrlOf(e.file.path);
        if (!url.empty()) SDL_OpenURL(url.c_str());
    }
    if (ImGui::MenuItem("Copy path")) ImGui::SetClipboardText(e.file.path.c_str());
    ImGui::Separator();
    ImGui::TextDisabled("Cover: PNG only.  You can also drop a PNG on this tile.");
}

// A game tile.  `strip` = the smaller tiles of a Favorites / Recently played strip (they take no cover drops: the
// same game is in the grid below).  `when` (recents) is when it was played.
void drawCell(App& app, LibraryService& svc, const lib::Entry& e, float cellW, float artH, float textH, bool strip = false,
              int64_t when = 0, bool focus = false) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImVec2 total(cellW, artH + textH);
    ImGui::PushID(e.file.path.c_str());
    bool clicked = ImGui::InvisibleButton("##cell", total);
    bool hovered = ImGui::IsItemHovered();
    padOpensPopup(app, e, focus, p0, total, "##cellmenu");   // the pad's menu (hold Accept, or the menu button)
    if (ImGui::BeginPopupContextItem("##cellmenu")) {
        drawTileMenu(app, svc, e);
        ImGui::EndPopup();
    }
    const bool popupOpen = ImGui::IsPopupOpen("##cellmenu");   // under this tile's ID scope
    ImGui::PopID();
    if (!strip) {   // for a file dropped on the window: which tile is under the pointer (clipped to the visible grid)
        ImVec2 cmin = dl->GetClipRectMin(), cmax = dl->GetClipRectMax();
        LibraryService::CoverTile t;
        t.x0 = std::max(p0.x, cmin.x);
        t.y0 = std::max(p0.y, cmin.y);
        t.x1 = std::min(p0.x + total.x, cmax.x);
        t.y1 = std::min(p0.y + total.y, cmax.y);
        if (t.x1 > t.x0 && t.y1 > t.y0) {
            t.key = lib::coverKey(e);
            t.title = e.title;
            t.datName = e.datName;
            svc.tiles.push_back(std::move(t));
        }
    }

    ImVec2 a0 = p0, a1(p0.x + cellW, p0.y + artH);
    const bool ring = focus && deskPadShown(app);   // the pad's focus (the mouse hover outline below is unchanged)
    if (ring) deskDrawFocusShadow(dl, a0, a1, 4.0f);
    dl->AddRectFilled(a0, a1, ui::u32(hovered ? ui::pal().cardBgHover : ui::pal().cardBg), 4.0f);

    ArtView v = svc.art(e);
    bool placeholder = false;
    if (v.state == ArtView::Ready && v.tex && v.w > 0 && v.h > 0) {
        float k = std::min((cellW - 4.0f) / (float)v.w, (artH - 4.0f) / (float)v.h);
        float w = (float)v.w * k, h = (float)v.h * k;
        ImVec2 i0(p0.x + (cellW - w) * 0.5f, p0.y + (artH - h) * 0.5f);
        dl->AddImage(ImTextureRef((ImTextureID)(intptr_t)v.tex), i0, ImVec2(i0.x + w, i0.y + h));
    } else {
        drawPlaceholder(dl, a0, a1, e, v.state == ArtView::Loading);
        placeholder = true;
    }
    if (hovered) dl->AddRect(a0, a1, ui::u32(ui::pal().hoverEdge), 4.0f, 0, 2.0f);
    if (ring) deskDrawFocusRing(dl, a0, a1, 4.0f);

    // badges
    if (e.file.kind == lib::Kind::Disc) drawBadge(dl, ImVec2(a0.x + 4, a0.y + 4), "CD");
    else if (placeholder) drawBadge(dl, ImVec2(a0.x + 4, a0.y + 4), "Cart");
    if (e.match != lib::Match::Crc) {
        const char* q = e.match == lib::Match::Name ? "?" : "??";
        float w = ImGui::CalcTextSize(q).x + 8;
        dl->AddRectFilled(ImVec2(a1.x - 4 - w, a0.y + 4), ImVec2(a1.x - 4, a0.y + 4 + ImGui::GetFontSize() + 2),
                          ui::u32(ui::pal().uncertainBg), 3.0f);
        dl->AddText(ImVec2(a1.x - w, a0.y + 5), ui::u32(ui::pal().badgeText), q);
    }

    // the favorite star, bottom-right of the art: filled when favorited, an outline on a hovered tile, a click toggles it
    const bool fav = svc.isFavorite(e);
    const float sr = std::max(ImGui::GetFontSize() * 0.75f, 8.0f);   // star radius
    const ImVec2 sc(a1.x - sr - 4.0f, a1.y - sr - 4.0f);
    const ImVec2 mouse = ImGui::GetMousePos();
    const bool overStar = hovered && mouse.x >= sc.x - sr - 2 && mouse.x <= sc.x + sr + 2 && mouse.y >= sc.y - sr - 2 && mouse.y <= sc.y + sr + 2;
    if (fav || hovered) {
        dl->AddCircleFilled(sc, sr + 2.0f, ui::u32(ui::pal().starDisc, overStar ? 1.2f : 1.0f));
        drawStar(dl, sc, sr, fav, overStar);
    }

    // title, clipped to the text box
    ImVec4 clip(p0.x, p0.y + artH, p0.x + cellW, p0.y + artH + textH);
    dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(p0.x + 2, p0.y + artH + 3),
                ui::u32(ui::pal().text), e.title.c_str(), nullptr, cellW - 4, &clip);

    if (overStar && !popupOpen) {
        ImGui::SetTooltip("%s", fav ? "Remove from Favorites" : "Add to Favorites");
    } else if (hovered && !popupOpen) {
        ui::beginWrappedTooltip();
        ImGui::TextWrapped("%s", e.title.c_str());
        ImGui::Separator();
        ImGui::TextDisabled("%s", e.file.path.c_str());
        if (e.file.crcWhole) ImGui::TextDisabled("CRC32 %08X", e.file.crcWhole);
        if (!e.region.empty()) ImGui::TextDisabled("Region: %s", e.region.c_str());
        if (!e.how.empty()) ImGui::TextDisabled("Identified by: %s", e.how.c_str());
        switch (e.match) {
        case lib::Match::Crc: ImGui::TextDisabled("Verified against the database by checksum."); break;
        case lib::Match::Name:
            ImGui::TextDisabled("Matched by file name only: the checksum is not in the database\n"
                                "(modified, translated or a different dump).");
            break;
        default:
            ImGui::TextDisabled(e.file.kind == lib::Kind::Disc && e.file.crcWhole == 0
                                    ? "Not identified: no usable checksum (a .chd without session metadata, a .cdi\n"
                                      "that is not laid out as it describes itself, or a .cue whose track is missing)."
                                    : "Not in the database; titled from the file name.");
            break;
        }
        if (when > 0) ImGui::TextDisabled("Last played: %s", stamp(when).c_str());
        if (placeholder && v.state != ArtView::Loading)
            ImGui::TextDisabled("No cover: right-click to set one, or drop a PNG on this tile.");
        ui::endWrappedTooltip();
    }
    if (clicked) {
        if (overStar) svc.setFavorite(e, !fav);
        else activateEntry(app, svc, e, false);
    }
}

// ---- zoom ------------------------------------------------------------------------------------------------------
// Ctrl+wheel (Cmd on macOS), Ctrl/Cmd + '+' '-' '0', and the trackpad pinch (app.cpp, which asks gridHoverFrame
// whether the pointer is over the grid) all change svc.cellScale, the value the toolbar slider edits.
// ImGui swaps Ctrl and Cmd on macOS, so KeyCtrl is the platform's shortcut key there and KeySuper the other one:
// either zooms.

bool zoomModHeld() {
    const ImGuiIO& io = ImGui::GetIO();
    return io.KeyCtrl || io.KeySuper;
}

// Scrolling is off while the modifier is held: the wheel then means zoom, not scroll.
ImGuiWindowFlags gridWindowFlags() { return zoomModHeld() ? ImGuiWindowFlags_NoScrollWithMouse : ImGuiWindowFlags_None; }

// Called inside the grid's child window, so "hovered" and "focused" are about this view alone: another window
// on top, a text field being edited or a game with no library showing never zooms it.
void handleZoomInput(LibraryService& svc) {
    svc.flushZoom();
    const ImGuiIO& io = ImGui::GetIO();
    const bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);   // the strips are children of the grid
    if (hovered) svc.gridHoverFrame = ImGui::GetFrameCount();
    if (io.WantTextInput) return;
    float steps = 0.0f;
    bool reset = false;
    if (zoomModHeld()) {
        if (hovered && io.MouseWheel != 0.0f) steps += io.MouseWheel;
        // the home screen has no keyboard focus of its own: it counts when no other window has it
        const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) ||
                             !ImGui::IsWindowFocused(ImGuiFocusedFlags_AnyWindow);
        if (focused) {
            if (ImGui::IsKeyPressed(ImGuiKey_Equal) || ImGui::IsKeyPressed(ImGuiKey_KeypadAdd)) steps += 1.0f;
            if (ImGui::IsKeyPressed(ImGuiKey_Minus) || ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract)) steps -= 1.0f;
            if (ImGui::IsKeyPressed(ImGuiKey_0, false) || ImGui::IsKeyPressed(ImGuiKey_Keypad0, false)) reset = true;
        }
    }
    float next = reset ? lib::kTileScaleDefault : steps != 0.0f ? lib::zoomTileScale(svc.cellScale, steps) : svc.cellScale;
    if (next != svc.cellScale) {
        svc.cellScale = next;
        svc.zoomChanged();
    }
}

// ---- Favorites / Recently played strips (top of the Cartridges and Jaguar CD tabs) -------------------------------
// One scrolling row of smaller tiles with that media's favorites / recents.  Part of the grid's scroll area, drawn
// before the grid itself.  The strip's items are in its own ID scope: the same game is also a tile below.
void drawStrip(App& app, LibraryService& svc, const char* id, const char* title, const std::vector<lib::Resolved>& items,
               bool withWhen, bool clearable, DeskRegion region) {
    if (items.empty()) return;
    const float fs = ImGui::GetFontSize();
    const float cellW = fs * 9.0f * svc.cellScale * 0.62f;
    const float artH = cellW * 1.2f;
    const float textH = fs * 3.0f + 6.0f;
    ImGui::PushID(id);
    const float top = ImGui::GetCursorScreenPos().y;
    ImGui::TextDisabled("%s", title);
    if (clearable) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear")) svc.clearRecents();
    }
    const float h = artH + textH + ImGui::GetStyle().ScrollbarSize + 4.0f;
    ImGui::BeginChild("##row", ImVec2(0, h), ImGuiChildFlags_None,
                      ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoScrollWithMouse);   // wheel scrolls the page
    for (size_t i = 0; i < items.size(); i++) {
        if (i > 0) ImGui::SameLine();
        const bool focus = app.deskPad.nav.focused(region, (int)i);
        if (focus && g_df.scroll) {   // inside the row: its own horizontal scroll
            const float x = ImGui::GetCursorScreenPos().x;
            deskEnsureVisibleX(x, x + cellW);
        }
        drawCell(app, svc, *items[i].entry, cellW, artH, textH, true, withWhen ? items[i].when : 0, focus);
    }
    ImGui::EndChild();
    if (g_df.scroll && app.deskPad.nav.region() == region) deskEnsureVisibleY(top, ImGui::GetCursorScreenPos().y);   // the page
    ImGui::PopID();
}

// The two strips of a media tab, filtered like the grid below them (search, "Not verified").  Empty on a tab without them.
struct StripItems {
    std::vector<lib::Resolved> fav, recent;
};

StripItems pickStrips(LibraryService& svc, const std::string& needle, int filter) {
    StripItems out;
    const int tab = svc.activeTab();
    if (tab != lib::TabCart && tab != lib::TabDisc) return out;
    const lib::Section sec = tab == lib::TabDisc ? lib::Section::Disc : lib::Section::Cart;
    auto pick = [&](const std::vector<lib::Resolved>& all) {
        std::vector<lib::Resolved> v;
        for (const lib::Resolved& r : lib::filterBySection(all, sec))
            if (matches(*r.entry, filter, needle)) v.push_back(r);
        return v;
    };
    if (lib::stripOnTab(tab, svc.favoritesShow)) out.fav = pick(svc.favoriteEntries());
    if (lib::stripOnTab(tab, svc.recentsShow)) out.recent = pick(svc.recentEntries());
    return out;
}

void drawStrips(App& app, LibraryService& svc, const StripItems& s) {
    drawStrip(app, svc, "stripfav", "Favorites", s.fav, false, false, DeskRegion::FavStrip);
    drawStrip(app, svc, "striprec", "Recently played", s.recent, true, true, DeskRegion::RecentStrip);
}

// ---- the Save states tab ----------------------------------------------------------------------------------------------------
struct PendingDelete {
    bool ask = false;
    lib::StateInfo state;
    std::string title;
} g_del;

// The shots drawStatesTab will list, in display order, for the pad's focus (key = the game's path + "#" + slot).
void publishStates(App& app, const std::vector<lib::StateGame>& games, LibraryService& svc, const std::string& needle) {
    if (!app.deskPad.active) return;   // a mouse-only user builds none of this
    DeskLists dl;
    for (const lib::StateGame& g : games) {
        const lib::Entry* owner = svc.entryForState(g);
        const std::string title = owner ? owner->title : g.stem;
        if (!needle.empty() && lower(title).find(needle) == std::string::npos && lower(g.stem).find(needle) == std::string::npos) continue;
        for (const lib::StateInfo& st : g.states) {
            const std::string key = (owner ? owner->file.path : g.stem) + "#" + std::to_string(st.slot);
            dl.states.push_back({key, title});
            g_df.states.push_back({owner, st.slot, key});
        }
    }
    app.deskPad.nav.setLists(std::to_string(lib::TabStates), std::move(dl));
    g_df.scroll = app.deskPad.nav.takeScroll() && deskPadShown(app);   // consumed always, acted on only for the pad: the mouse's view stays put
    runDeskCommands(app, svc);
}

void drawStatesTab(App& app, LibraryService& svc, const std::string& needle) {
    const std::vector<lib::StateGame>& games = svc.stateGames();
    publishStates(app, games, svc, needle);
    if (games.empty()) {
        ImGui::TextWrapped("No save states yet.  Press F5 while playing (or use Save states in the Emulation menu) and the "
                           "state is listed here with the screenshot taken when you saved it.  Click one to start the game "
                           "from that moment.");
        return;
    }
    const float fs = ImGui::GetFontSize();
    const float gap = ImGui::GetStyle().ItemSpacing.x;
    const ImVec2 box(fs * 9.0f * std::max(0.8f, svc.cellScale) * 1.25f, fs * 9.0f * std::max(0.8f, svc.cellScale) * 1.25f * 0.75f);
    const float avail = ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ScrollbarSize;
    size_t listed = 0;
    int shot = 0;   // the running index of the listed shots, as publishStates numbered them
    for (const lib::StateGame& g : games) {
        const lib::Entry* owner = svc.entryForState(g);
        const std::string title = owner ? owner->title : g.stem;
        if (!needle.empty() && lower(title).find(needle) == std::string::npos && lower(g.stem).find(needle) == std::string::npos) continue;
        listed++;
        ImGui::PushID(g.stem.c_str());
        ImGui::SeparatorText(title.c_str());
        if (!owner) {
            ImGui::TextDisabled("This game is not in the library (its folder is not added, or the file moved): "
                                "its states cannot be started from here.");
        }
        float x = 0.0f;
        for (const lib::StateInfo& st : g.states) {
            if (x > 0.0f && x + box.x > avail) x = 0.0f;
            else if (x > 0.0f) ImGui::SameLine();
            x += box.x + gap;
            ImGui::PushID(st.slot + 1);
            ImGui::BeginGroup();
            ImVec2 p0 = ImGui::GetCursorScreenPos();
            const bool clicked = ImGui::InvisibleButton("##shot", box);
            const bool hot = ImGui::IsItemHovered();
            const bool focus = app.deskPad.nav.focused(DeskRegion::States, shot++);
            drawStateShot(ImGui::GetWindowDrawList(), svc, st, p0, box, hot && owner, focus && deskPadShown(app));
            if (hot) {
                if (owner) ImGui::SetTooltip("Start %s from this state", title.c_str());
                else ImGui::SetTooltip("%s is not in the library", title.c_str());
            }
            if (clicked && owner) svc.launchState(*owner, st.slot);
            ImGui::TextUnformatted(lib::stateLabel(st.slot).c_str());
            ImGui::TextDisabled("%s", stamp(st.mtime).c_str());
            if (ImGui::SmallButton("Delete")) {
                g_del.ask = true;
                g_del.state = st;
                g_del.title = title;
            }
            ImGui::EndGroup();
            if (focus && g_df.scroll) deskEnsureVisibleY(ImGui::GetItemRectMin().y, ImGui::GetItemRectMax().y);   // picture, labels, Delete
            ImGui::PopID();
        }
        ImGui::PopID();
    }
    if (listed == 0) ImGui::TextDisabled("Nothing matches.");
    // The confirmation lives outside the loops: deleting re-lists the states, and `games` with it.  Delete is a mouse
    // button (the pad's Accept starts the shot): this modal is the mouse's, and the pad stays out of it (deskPadActive).
    if (g_del.ask) {
        ImGui::OpenPopup("Delete save state?");
        g_del.ask = false;
    }
    if (ImGui::BeginPopupModal("Delete save state?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Delete the %s of %s?", g_del.state.slot == lib::kAutoSlot ? "auto-save" : ("slot " + std::to_string(g_del.state.slot)).c_str(),
                    g_del.title.c_str());
        ImGui::TextDisabled("Saved %s.  This cannot be undone.", stamp(g_del.state.mtime).c_str());
        if (ImGui::Button("Delete")) {
            svc.deleteState(g_del.state);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void drawGridBody(App& app, LibraryService& svc, const std::string& needle, int filter) {
    handleZoomInput(svc);
    deskStickScroll(app);   // the right stick (nothing unless the pad is driving)
    svc.tiles.clear();
    svc.tilesFrame = ImGui::GetFrameCount();
    g_df = DeskFrame();
    const int tab = svc.activeTab();
    if (tab == lib::TabStates) {
        drawStatesTab(app, svc, needle);
        return;
    }
    const bool listTab = tab == lib::TabFavorites || tab == lib::TabRecents;
    const std::vector<lib::Entry>& all = svc.entries();
    std::vector<lib::Resolved> shown;
    shown.reserve(all.size());
    size_t inSection = 0;
    if (listTab) {   // both media, in the list's order: favorites by title, recents newest first
        const std::vector<lib::Resolved>& l = tab == lib::TabFavorites ? svc.favoriteEntries() : svc.recentEntries();
        inSection = l.size();
        for (const lib::Resolved& r : l)
            if (matches(*r.entry, filter, needle)) shown.push_back(r);
    } else {
        for (const lib::Entry& e : all) {
            if ((int)e.section == tab) inSection++;
            if (passes(e, tab, filter, needle)) {
                lib::Resolved r;
                r.entry = &e;
                shown.push_back(r);
            }
        }
    }

    // What the pad can focus, in display order: the strips, then the grid.  Published before anything is drawn so the
    // ring and the commands see this frame's lists.
    const StripItems strips = pickStrips(svc, needle, filter);
    const float fs = ImGui::GetFontSize();
    const float cellW = fs * 9.0f * svc.cellScale;
    const float artH = cellW * 1.2f;
    const float textH = fs * 3.0f + 6.0f;
    const float gap = ImGui::GetStyle().ItemSpacing.x;
    const float avail = ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ScrollbarSize;
    const int cols = std::max(1, (int)((avail + gap) / (cellW + gap)));
    if (app.deskPad.active) {   // a mouse-only user builds none of this
        DeskLists dl;
        dl.gridCols = cols;
        for (const lib::Resolved& r : strips.fav) {
            dl.fav.push_back({r.entry->file.path, r.entry->title});
            g_df.fav.push_back(r.entry);
        }
        for (const lib::Resolved& r : strips.recent) {
            dl.recent.push_back({r.entry->file.path, r.entry->title});
            g_df.recent.push_back(r.entry);
        }
        for (const lib::Resolved& r : shown) {
            dl.grid.push_back({r.entry->file.path, r.entry->title});
            g_df.grid.push_back(r.entry);
        }
        app.deskPad.nav.setLists(std::to_string(tab), std::move(dl));
        g_df.scroll = app.deskPad.nav.takeScroll() && deskPadShown(app);   // consumed always, acted on only for the pad
        runDeskCommands(app, svc);
        if (g_tileCmd.active) g_df.scroll = true;   // a command waits for its tile: make sure it gets drawn
    }
    drawStrips(app, svc, strips);

    if (svc.section == (int)lib::Section::Bios) {
        drawBiosPanel(app);
        ImGui::SeparatorText("BIOS and system files found");
    }
    if (inSection == 0) {
        if (svc.scanning()) return;
        if (tab == lib::TabFavorites)
            ImGui::TextWrapped("No favorites yet.  Click the star on a game's tile, or right-click it and choose Add to "
                               "Favorites.");
        else if (tab == lib::TabRecents)
            ImGui::TextWrapped("Nothing played yet.  The games you boot from the library appear here, newest first.");
        else if (tab == (int)lib::Section::Bios)
            ImGui::TextWrapped("No BIOS or system files found in your folders or in the core's system folder (%s). "
                               "None are needed: the core has the Series K and Model M boot ROMs and both CD BIOS images "
                               "built in. Put real ones in the system folder to use them.",
                               svc.systemDir.c_str());
        else if (svc.folders().empty())
            ImGui::TextWrapped("Add a folder of Jaguar cartridge and CD images (Add folder...) and they appear here "
                               "with their names and box art. Dropping a file on the window and File > Open "
                               "keep working without a library.");
        else if (all.empty())
            ImGui::TextWrapped("No Jaguar images were found in the chosen folders.");
        else
            ImGui::TextWrapped(tab == (int)lib::Section::Cart ? "No cartridge images in the chosen folders."
                                                              : "No CD images (.cue, .chd, .cdi) in the chosen folders.");
        return;
    }
    if (shown.empty()) {
        ImGui::TextDisabled("Nothing matches.");
        return;
    }
    if (!listTab && (tab == lib::TabCart || tab == lib::TabDisc) &&
        (lib::stripOnTab(tab, svc.favoritesShow) || lib::stripOnTab(tab, svc.recentsShow)))
        ImGui::SeparatorText(tab == lib::TabCart ? "All cartridges" : "All CDs");

    int rows = ((int)shown.size() + cols - 1) / cols;

    // The clipper only submits the visible rows, so a focused tile out of sight is scrolled to by its row.
    const float row0 = ImGui::GetCursorScreenPos().y, pitch = artH + textH + gap;
    if (g_df.scroll && app.deskPad.nav.region() == DeskRegion::Grid) {
        const float y = row0 + (float)(app.deskPad.nav.index() / cols) * pitch;
        deskEnsureVisibleY(y, y + artH + textH);
    } else if (app.deskPad.nav.region() == DeskRegion::Grid) {   // the right stick scrolled it out of view: focus follows the view
        const int at = deskFollowStick(app, app.deskPad.nav.index(), cols, (int)shown.size(), row0, pitch, artH + textH);
        if (at != app.deskPad.nav.index()) app.deskPad.nav.focusIndex(DeskRegion::Grid, at);   // no bring-into-view scroll
    }
    ImGuiListClipper clip;
    clip.Begin(rows, artH + textH + gap);
    while (clip.Step()) {
        for (int r = clip.DisplayStart; r < clip.DisplayEnd; r++) {
            for (int c = 0; c < cols; c++) {
                size_t i = (size_t)r * (size_t)cols + (size_t)c;
                if (i >= shown.size()) break;
                if (c > 0) ImGui::SameLine();
                const bool focus = app.deskPad.nav.focused(DeskRegion::Grid, (int)i);
                if (shown[i].entry->section == lib::Section::Bios) drawBiosCell(app, svc, *shown[i].entry, cellW, artH, textH, focus);
                else drawCell(app, svc, *shown[i].entry, cellW, artH, textH, false, tab == lib::TabRecents ? shown[i].when : 0, focus);
            }
        }
    }
    clip.End();
}

void drawGrid(App& app, LibraryService& svc, const std::string& needle, int filter) {
    drawGridBody(app, svc, needle, filter);
    finishDeskFrame(app, svc);   // the body publishes and runs the commands before any early return, so Back and the tabs work on an empty tab too
}

// Search and filter are shared by the home screen and the window: one library, one view.
char g_search[128] = "";
int g_filter = kAll;
int g_homeSec = -1, g_winSec = -1;   // each tab bar's last-seen section (see drawSections)

// The first-run home screen: no folders yet.  A friendly block in the middle, wrapped to the window.
void drawEmptyHome(App& app, LibraryService& svc) {
    svc.focusSearch = false;   // there is no search box to focus yet
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float f = ImGui::GetFontSize();
    const float bw = std::max(f * 8.0f, std::min(f * 38.0f, avail.x - f * 2.0f));
    const char* title = "Virtual Jaguar";
    const char* p1 = "Add a folder of Jaguar cartridge and CD images and your games show up here, with names and "
                     "box art. Click one to play it.";
    const char* p2 = "No library needed: open a game with File > Open (Ctrl/Cmd+O), drop a ROM or disc image "
                     "onto this window, or give one on the command line.";
    const char* p3 = "Cartridges: .j64 .jag .rom .abs .cof .prg .bin    CD images: .cue .chd .cdi";
    const float titleH = f * 1.9f * 1.2f;
    const float btnH = f + f * 1.1f;
    const float gap = f * 1.0f;
    const float h1 = ImGui::CalcTextSize(p1, nullptr, false, bw).y;
    const float h2 = ImGui::CalcTextSize(p2, nullptr, false, bw).y;
    const float h3 = ImGui::CalcTextSize(p3, nullptr, false, bw).y;
    const float total = titleH + gap + h1 + gap + btnH + gap * 1.5f + h2 + gap * 0.5f + h3;
    const float x0 = ImGui::GetCursorPosX() + std::max(0.0f, (avail.x - bw) * 0.5f);
    float y0 = ImGui::GetCursorPosY() + std::max(f, (avail.y - total) * 0.42f);

    ImGui::SetCursorPos(ImVec2(x0, y0));
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.9f);
    ImGui::TextUnformatted(title);
    ImGui::PopFont();
    ImGui::SetCursorPos(ImVec2(x0, y0 + titleH + gap));
    ImGui::PushTextWrapPos(x0 + bw);
    ImGui::TextUnformatted(p1);
    ImGui::PopTextWrapPos();
    ImGui::SetCursorPos(ImVec2(x0, y0 + titleH + gap + h1 + gap));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(f * 1.6f, f * 0.55f));
    if (ImGui::Button("Add folder...")) svc.addFolderDialog(app.window());
    ImGui::SameLine();
    if (ImGui::Button("BIOS & boot...")) app.openBiosSettings();
    ImGui::PopStyleVar();
    ImGui::SetCursorPos(ImVec2(x0, y0 + titleH + gap + h1 + gap + btnH + gap * 1.5f));
    ImGui::PushTextWrapPos(x0 + bw);
    ui::hint("%s", p2);
    ImGui::Dummy(ImVec2(1.0f, gap * 0.5f));
    ImGui::SetCursorPosX(x0);
    ui::hint("%s", p3);
    ImGui::PopTextWrapPos();
}

}  // namespace

// The home screen: the library filling the area below the menu bar while no game is loaded.
// A borderless, immovable window drawn first so Settings and the other windows open over it;
// loading a game stops drawing it, closing the game brings it back.
void drawLibraryHome(App& app, float x, float y, float w, float h) {
    LibraryService& svc = app.library;
    ImGui::SetNextWindowPos(ImVec2(x, y));
    ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGuiWindowFlags wf = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                          ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
                          ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ui::vec4(ui::pal().homeBg));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    if (ImGui::Begin("##home", nullptr, wf)) {
        if (svc.folders().empty()) {
            app.deskPad.pending.clear();   // no grid to run the pad's commands
            drawEmptyHome(app, svc);
        } else {
            drawToolbar(app, svc, g_search, g_filter, true);
            drawSections(svc, "##homesec", g_homeSec);
            drawFolders(svc);
            drawStatus(app, svc);
            drawSectionHint(svc);
            ImGui::Separator();
            ImGui::BeginChild("##homegrid", ImVec2(0, 0), ImGuiChildFlags_None, gridWindowFlags());
            drawGrid(app, svc, lower(g_search), g_filter);
            ImGui::EndChild();
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}

void drawLibrary(App& app) {
    LibraryService& svc = app.library;
    svc.pump();   // always: finished scans and decoded art must not pile up while the window is closed
    if (!svc.show || app.homeVisible()) return;   // the home screen already is the library

    ui::nextWindow(120.0f, 80.0f, 30.0f, 16.0f);
    bool open = ui::beginWindow("Game library", &svc.show);
    ui::keepOnScreen();
    if (!open) {
        app.deskPad.pending.clear();   // collapsed: no grid to run the pad's commands
        ImGui::End();
        return;
    }
    drawToolbar(app, svc, g_search, g_filter, false);
    drawSections(svc, "##winsec", g_winSec);
    drawFolders(svc);
    drawStatus(app, svc);
    drawSectionHint(svc);
    ImGui::Separator();
    ImGui::BeginChild("##grid", ImVec2(0, 0), ImGuiChildFlags_None, gridWindowFlags());
    drawGrid(app, svc, lower(g_search), g_filter);
    ImGui::EndChild();
    ImGui::End();
}

}  // namespace vj
