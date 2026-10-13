// The "Game library" window: toolbar, folder list, and the box art grid.
#include <algorithm>
#include <cctype>
#include <cstdio>

#include "app.h"
#include "imgui.h"
#include "library_service.h"

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

enum Filter { kAll, kCarts, kDiscs, kUnverified };

bool passes(const lib::Entry& e, int filter, const std::string& needle) {
    if (filter == kCarts && e.file.kind != lib::Kind::Cart) return false;
    if (filter == kDiscs && e.file.kind != lib::Kind::Disc) return false;
    if (filter == kUnverified && e.match == lib::Match::Crc) return false;
    if (needle.empty()) return true;
    return lower(e.title).find(needle) != std::string::npos || lower(e.file.path).find(needle) != std::string::npos;
}

void drawToolbar(App& app, LibraryService& svc, char* search, int& filter) {
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14.0f);
    ImGui::InputTextWithHint("##search", "Search", search, 128);
    ImGui::SameLine();
    static const char* filters[] = {"All", "Cartridges", "CD images", "Not verified"};
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 9.0f);
    ImGui::Combo("##filter", &filter, filters, 4);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7.0f);
    ImGui::SliderFloat("##size", &svc.cellScale, 0.6f, 2.0f, "Size %.1fx");
    if (ImGui::IsItemDeactivatedAfterEdit()) svc.saveSettings();

    if (ImGui::Button("Add folder...")) svc.addFolderDialog(app.window());
    ImGui::SameLine();
    if (ImGui::Button("Rescan")) svc.rescan();
    ImGui::SameLine();
    bool art = svc.downloadArt;
    if (ImGui::Checkbox("Download box art", &art)) svc.setDownloadArt(art);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Fetches missing covers from thumbnails.libretro.com, once per title.\n"
                          "Everything already downloaded keeps working offline.");
    ImGui::SameLine();
    bool hide = svc.hideOnLaunch;
    if (ImGui::Checkbox("Close on launch", &hide)) {
        svc.hideOnLaunch = hide;
        svc.saveSettings();
    }
    ImGui::SameLine();
    if (ImGui::Button("Update database")) svc.updateDatabase();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Downloads the latest game lists from libretro-database.\n"
                          "Cartridges: %s\nCD images: %s",
                          svc.cartDatInfo().c_str(), svc.discDatInfo().c_str());
}

void drawFolders(LibraryService& svc) {
    const std::vector<std::string>& f = svc.folders();
    if (f.empty()) return;
    if (!ImGui::TreeNodeEx("Folders", ImGuiTreeNodeFlags_None)) return;
    int remove = -1;
    for (size_t i = 0; i < f.size(); i++) {
        ImGui::PushID((int)i);
        if (ImGui::SmallButton("Remove")) remove = (int)i;
        ImGui::SameLine();
        ImGui::TextUnformatted(f[i].c_str());
        ImGui::PopID();
    }
    ImGui::TreePop();
    if (remove >= 0) svc.removeFolder((size_t)remove);
}

void drawStatus(LibraryService& svc) {
    if (svc.scanning()) {
        size_t t = svc.scanTotal();
        if (t > 0) ImGui::Text("Scanning... hashing %zu of %zu", svc.scanDone(), t);
        else ImGui::TextUnformatted("Scanning...");
    } else {
        std::string s = svc.status();
        if (!s.empty()) ImGui::TextDisabled("%s", s.c_str());
        else ImGui::TextDisabled("%zu games", svc.entries().size());
    }
    if (svc.netBusy()) {
        ImGui::SameLine();
        ImGui::TextDisabled("  (downloading)");
    }
    if (!svc.haveCartDat() && !svc.haveDiscDat()) {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.4f, 1.0f),
                           "No game database found: titles come from file names. Use Update database to download it.");
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
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(e.title.c_str());
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
        ImGui::EndTooltip();
    }
    if (clicked) svc.launch(e);
}

void drawGrid(App& app, LibraryService& svc, const std::string& needle, int filter) {
    const std::vector<lib::Entry>& all = svc.entries();
    std::vector<const lib::Entry*> shown;
    shown.reserve(all.size());
    for (const lib::Entry& e : all)
        if (passes(e, filter, needle)) shown.push_back(&e);

    if (all.empty()) {
        if (svc.folders().empty())
            ImGui::TextWrapped("Add a folder of Jaguar cartridge and CD images (Add folder...) and they appear here "
                               "with their names and box art. Dropping a file on the window and File > Open "
                               "keep working without a library.");
        else if (!svc.scanning())
            ImGui::TextWrapped("No Jaguar images were found in the chosen folders.");
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
                drawCell(app, svc, *shown[i], cellW, artH, textH);
            }
        }
    }
    clip.End();
}

}  // namespace

void drawLibrary(App& app) {
    LibraryService& svc = app.library;
    svc.pump();   // always: finished scans and decoded art must not pile up while the window is closed
    if (!svc.show) return;

    static char search[128] = "";
    static int filter = kAll;

    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y * 0.5f), ImGuiCond_FirstUseEver,
                            ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(vp->Size.x * 0.8f, vp->Size.y * 0.8f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Game library", &svc.show)) {
        ImGui::End();
        return;
    }
    drawToolbar(app, svc, search, filter);
    drawFolders(svc);
    drawStatus(svc);
    ImGui::Separator();
    ImGui::BeginChild("##grid", ImVec2(0, 0), ImGuiChildFlags_None);
    drawGrid(app, svc, lower(search), filter);
    ImGui::EndChild();
    ImGui::End();
}

}  // namespace vj
