// The "Game library" window: toolbar, folder list, and the box art grid.
#include <algorithm>
#include <cctype>
#include <cstdio>

#include "app.h"
#include "imgui.h"
#include "library_service.h"
#include "ui_util.h"

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

bool passes(const lib::Entry& e, int section, int filter, const std::string& needle) {
    if ((int)e.section != section) return false;
    if (filter == kUnverified && e.match == lib::Match::Crc) return false;
    if (needle.empty()) return true;
    return lower(e.title).find(needle) != std::string::npos || lower(e.file.path).find(needle) != std::string::npos;
}

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
    static const char* filters[] = {"All", "Not verified"};
    float fw = ui::em(9.5f);
    flow.next(fw);
    ImGui::SetNextItemWidth(fw);
    ImGui::Combo("##filter", &filter, filters, 2);
    float sw = ui::em(8.0f);
    flow.next(sw);
    ImGui::SetNextItemWidth(sw);
    ImGui::SliderFloat("##size", &svc.cellScale, 0.6f, 2.0f, "Size %.1fx");
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
    flow.next(ui::Flow::checkW("Download box art"));
    bool art = svc.downloadArt;
    if (ImGui::Checkbox("Download box art", &art)) svc.setDownloadArt(art);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Fetches missing covers from thumbnails.libretro.com, once per title.\n"
                          "Everything already downloaded keeps working offline.");
    if (!home) {   // the home screen goes away when a game loads: nothing to close
        flow.next(ui::Flow::checkW("Close on launch"));
        bool hide = svc.hideOnLaunch;
        if (ImGui::Checkbox("Close on launch", &hide)) {
            svc.hideOnLaunch = hide;
            svc.saveSettings();
        }
    }
}

// Cartridges / Jaguar CD / BIOS & system.  The tabs carry no content of their own: the selected one
// (remembered across runs) decides what the grid below lists.  `last` is this tab bar's own idea of
// the section, so the home screen and the window stay in step when the other one changes it.
void drawSections(LibraryService& svc, const char* id, int& last) {
    size_t n[3] = {0, 0, 0};
    for (const lib::Entry& e : svc.entries()) n[(int)e.section]++;
    static const char* names[3] = {"Cartridges", "Jaguar CD", "BIOS & system"};
    static const char* ids[3] = {"###sec0", "###sec1", "###sec2"};
    if (!ImGui::BeginTabBar(id, ImGuiTabBarFlags_FittingPolicyScroll | ImGuiTabBarFlags_NoTooltip)) return;
    for (int i = 0; i < 3; i++) {
        char label[64];
        std::snprintf(label, sizeof label, "%s (%zu)%s", names[i], n[i], ids[i]);
        ImGuiTabItemFlags fl = (last != svc.section && svc.section == i) ? ImGuiTabItemFlags_SetSelected : 0;
        if (ImGui::BeginTabItem(label, nullptr, fl)) {
            if (svc.section != i && last == svc.section) {   // the user clicked this tab
                svc.section = i;
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

void drawStatus(LibraryService& svc) {
    if (svc.scanning()) {
        size_t t = svc.scanTotal();
        if (t > 0) ImGui::TextWrapped("Scanning... hashing %zu of %zu", svc.scanDone(), t);
        else ImGui::TextUnformatted("Scanning...");
    } else {
        std::string s = svc.status();
        if (!s.empty()) ui::hint("%s", s.c_str());
        else ui::hint("%zu games%s", svc.entries().size(), svc.netBusy() ? "  (downloading)" : "");
    }
    if (!svc.haveCartDat() && !svc.haveDiscDat()) {
        ui::coloredWrapped(ImVec4(1.0f, 0.8f, 0.4f, 1.0f),
                           "No game database found: titles come from file names. Use Update database to download it.");
    }
}

// A BIOS or system file: shows what it is and whether the core will read it where it is.  It is not
// content, so a click does nothing.
void drawBiosCell(LibraryService& svc, const lib::Entry& e, float cellW, float artH, float textH) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImGui::PushID(e.file.path.c_str());
    ImGui::InvisibleButton("##cell", ImVec2(cellW, artH + textH));
    bool hovered = ImGui::IsItemHovered();
    ImGui::PopID();
    const lib::BiosInfo& b = e.bios;
    const lib::BiosUse use = lib::biosUse(b, e.file.path, e.file.size, svc.systemDir);
    const bool found = use.core;
    const bool readable = b.role == lib::BiosRole::CartBoot || b.role == lib::BiosRole::CdBios;

    ImVec2 a0 = p0, a1(p0.x + cellW, p0.y + artH);
    dl->AddRectFilled(a0, a1, hovered ? IM_COL32(52, 56, 66, 255) : IM_COL32(32, 34, 42, 255), 4.0f);
    ImColor c = ImColor::HSV(b.role == lib::BiosRole::CdBios ? 0.58f : b.role == lib::BiosRole::CartBoot ? 0.08f : 0.33f,
                             0.30f, 0.42f);
    dl->AddRectFilled(ImVec2(a0.x + 6, a0.y + 6), ImVec2(a1.x - 6, a1.y - 6), c, 4.0f);
    const char* label = lib::biosRoleLabel(b.role);
    float fs = ImGui::GetFontSize() * 1.2f;
    ImVec2 ts = ImGui::GetFont()->CalcTextSizeA(fs, cellW - 20.0f, cellW - 20.0f, label);
    dl->AddText(ImGui::GetFont(), fs, ImVec2(p0.x + (cellW - ts.x) * 0.5f, p0.y + (artH - ts.y) * 0.5f),
                IM_COL32(255, 255, 255, 215), label, nullptr, cellW - 20.0f);
    if (hovered) dl->AddRect(a0, a1, IM_COL32(120, 170, 255, 255), 4.0f, 0, 2.0f);

    float lh = ImGui::GetFontSize() + 2.0f;
    ImVec4 clip(p0.x, p0.y + artH, p0.x + cellW, p0.y + artH + textH);
    // title: at most two lines, clipped above the status line, which is always the tile's last line and short
    ImVec4 clipT(p0.x, p0.y + artH, p0.x + cellW, p0.y + artH + textH - lh);
    dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(p0.x + 2, p0.y + artH + 3), IM_COL32(230, 230, 230, 255),
                e.title.c_str(), nullptr, cellW - 4, &clipT);
    const char* st = found ? "Read by the core" : readable ? "Not read here" : "Not read";
    ImU32 sc = found ? IM_COL32(120, 210, 130, 255) : readable ? IM_COL32(230, 180, 90, 255) : IM_COL32(150, 150, 150, 255);
    dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(p0.x + 2, p0.y + artH + textH - lh), sc, st, nullptr,
                cellW - 4, &clip);

    if (hovered) {
        ui::beginWrappedTooltip();
        ImGui::TextWrapped("%s", e.title.c_str());
        ImGui::TextDisabled("%s%s", lib::biosRoleLabel(b.role), b.recognized ? "" : " (checksum not in the core's list)");
        ImGui::Separator();
        ImGui::TextDisabled("%s", e.file.path.c_str());
        if (e.file.crcWhole) ImGui::TextDisabled("CRC32 %08X", e.file.crcWhole);
        ImGui::Spacing();
        ImGui::TextWrapped("%s", use.text.c_str());
        if (readable)
            ImGui::TextWrapped("The core has built-in copies of the Series K and Model M boot ROMs and both CD BIOS "
                               "images, so nothing is missing without this file.");
        ui::endWrappedTooltip();
    }
}

void drawCell(App& app, LibraryService& svc, const lib::Entry& e, float cellW, float artH, float textH) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImVec2 total(cellW, artH + textH);
    ImGui::PushID(e.file.path.c_str());
    bool clicked = ImGui::InvisibleButton("##cell", total);
    bool hovered = ImGui::IsItemHovered();
    ImGui::PopID();

    ImVec2 a0 = p0, a1(p0.x + cellW, p0.y + artH);
    dl->AddRectFilled(a0, a1, hovered ? IM_COL32(60, 64, 76, 255) : IM_COL32(38, 40, 48, 255), 4.0f);

    ArtView v = svc.art(e);
    if (v.state == ArtView::Ready && v.tex && v.w > 0 && v.h > 0) {
        float k = std::min((cellW - 4.0f) / (float)v.w, (artH - 4.0f) / (float)v.h);
        float w = (float)v.w * k, h = (float)v.h * k;
        ImVec2 i0(p0.x + (cellW - w) * 0.5f, p0.y + (artH - h) * 0.5f);
        dl->AddImage(ImTextureRef((ImTextureID)(intptr_t)v.tex), i0, ImVec2(i0.x + w, i0.y + h));
    } else {
        // Placeholder: a colour from the title and its first letter; "..." while the art is on its way.
        ImColor c = ImColor::HSV((float)(fnv(e.title) % 360) / 360.0f, 0.35f, 0.45f);
        dl->AddRectFilled(ImVec2(a0.x + 6, a0.y + 6), ImVec2(a1.x - 6, a1.y - 6), c, 4.0f);
        char glyph[2] = {e.title.empty() ? '?' : (char)std::toupper((unsigned char)e.title[0]), 0};
        const char* txt = v.state == ArtView::Loading ? "..." : glyph;
        float fs = ImGui::GetFontSize() * 2.2f;
        ImVec2 ts = ImGui::GetFont()->CalcTextSizeA(fs, 1e9f, 0.0f, txt);
        dl->AddText(ImGui::GetFont(), fs, ImVec2(p0.x + (cellW - ts.x) * 0.5f, p0.y + (artH - ts.y) * 0.5f),
                    IM_COL32(255, 255, 255, 200), txt);
    }
    if (hovered) dl->AddRect(a0, a1, IM_COL32(120, 170, 255, 255), 4.0f, 0, 2.0f);

    // badges
    if (e.file.kind == lib::Kind::Disc) {
        dl->AddRectFilled(ImVec2(a0.x + 4, a0.y + 4), ImVec2(a0.x + 4 + ImGui::CalcTextSize("CD").x + 8, a0.y + 4 + ImGui::GetFontSize() + 2),
                          IM_COL32(20, 20, 20, 200), 3.0f);
        dl->AddText(ImVec2(a0.x + 8, a0.y + 5), IM_COL32(255, 255, 255, 255), "CD");
    }
    if (e.match != lib::Match::Crc) {
        const char* q = e.match == lib::Match::Name ? "?" : "??";
        float w = ImGui::CalcTextSize(q).x + 8;
        dl->AddRectFilled(ImVec2(a1.x - 4 - w, a0.y + 4), ImVec2(a1.x - 4, a0.y + 4 + ImGui::GetFontSize() + 2),
                          IM_COL32(150, 90, 20, 220), 3.0f);
        dl->AddText(ImVec2(a1.x - w, a0.y + 5), IM_COL32(255, 255, 255, 255), q);
    }

    // title, clipped to the text box
    ImVec4 clip(p0.x, p0.y + artH, p0.x + cellW, p0.y + artH + textH);
    dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(p0.x + 2, p0.y + artH + 3),
                IM_COL32(230, 230, 230, 255), e.title.c_str(), nullptr, cellW - 4, &clip);

    if (hovered) {
        ui::beginWrappedTooltip();
        ImGui::TextWrapped("%s", e.title.c_str());
        ImGui::Separator();
        ImGui::TextDisabled("%s", e.file.path.c_str());
        if (e.file.crcWhole) ImGui::TextDisabled("CRC32 %08X", e.file.crcWhole);
        if (!e.region.empty()) ImGui::TextDisabled("Region: %s", e.region.c_str());
        switch (e.match) {
        case lib::Match::Crc: ImGui::TextDisabled("Verified against the database by checksum."); break;
        case lib::Match::Name:
            ImGui::TextDisabled("Matched by file name only: the checksum is not in the database\n"
                                "(modified, translated or a different dump).");
            break;
        default:
            ImGui::TextDisabled(e.file.kind == lib::Kind::Disc && e.file.crcWhole == 0
                                    ? "Not identified (only .cue sets are checksummed)."
                                    : "Not in the database; titled from the file name.");
            break;
        }
        ui::endWrappedTooltip();
    }
    if (clicked) svc.launch(e);
}

void drawGrid(App& app, LibraryService& svc, const std::string& needle, int filter) {
    const std::vector<lib::Entry>& all = svc.entries();
    std::vector<const lib::Entry*> shown;
    shown.reserve(all.size());
    size_t inSection = 0;
    for (const lib::Entry& e : all) {
        if ((int)e.section == svc.section) inSection++;
        if (passes(e, svc.section, filter, needle)) shown.push_back(&e);
    }

    if (inSection == 0) {
        if (svc.scanning()) return;
        if (svc.section == (int)lib::Section::Bios)
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
            ImGui::TextWrapped(svc.section == (int)lib::Section::Cart ? "No cartridge images in the chosen folders."
                                                                       : "No CD images (.cue, .chd, .cdi) in the chosen folders.");
        return;
    }
    if (shown.empty()) {
        ImGui::TextDisabled("Nothing matches.");
        return;
    }

    float fs = ImGui::GetFontSize();
    float cellW = fs * 9.0f * svc.cellScale;
    float artH = cellW * 1.2f;
    float textH = fs * 3.0f + 6.0f;
    float gap = ImGui::GetStyle().ItemSpacing.x;
    float avail = ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ScrollbarSize;
    int cols = std::max(1, (int)((avail + gap) / (cellW + gap)));
    int rows = ((int)shown.size() + cols - 1) / cols;

    ImGuiListClipper clip;
    clip.Begin(rows, artH + textH + gap);
    while (clip.Step()) {
        for (int r = clip.DisplayStart; r < clip.DisplayEnd; r++) {
            for (int c = 0; c < cols; c++) {
                size_t i = (size_t)r * (size_t)cols + (size_t)c;
                if (i >= shown.size()) break;
                if (c > 0) ImGui::SameLine();
                if (shown[i]->section == lib::Section::Bios) drawBiosCell(svc, *shown[i], cellW, artH, textH);
                else drawCell(app, svc, *shown[i], cellW, artH, textH);
            }
        }
    }
    clip.End();
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
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.045f, 0.045f, 0.055f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    if (ImGui::Begin("##home", nullptr, wf)) {
        if (svc.folders().empty()) {
            drawEmptyHome(app, svc);
        } else {
            drawToolbar(app, svc, g_search, g_filter, true);
            drawSections(svc, "##homesec", g_homeSec);
            drawFolders(svc);
            drawStatus(svc);
            ImGui::Separator();
            ImGui::BeginChild("##homegrid", ImVec2(0, 0), ImGuiChildFlags_None);
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
    bool open = ImGui::Begin("Game library", &svc.show);
    ui::keepOnScreen();
    if (!open) {
        ImGui::End();
        return;
    }
    drawToolbar(app, svc, g_search, g_filter, false);
    drawSections(svc, "##winsec", g_winSec);
    drawFolders(svc);
    drawStatus(svc);
    ImGui::Separator();
    ImGui::BeginChild("##grid", ImVec2(0, 0), ImGuiChildFlags_None);
    drawGrid(app, svc, lower(g_search), g_filter);
    ImGui::EndChild();
    ImGui::End();
}

}  // namespace vj
