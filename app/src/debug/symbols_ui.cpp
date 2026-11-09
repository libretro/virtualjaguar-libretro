// The Symbols window.  See symbols_ui.h.
#include "symbols_ui.h"

#include "debug_session.h"
#include "paths.h"
#include "symbol_table.h"
#include "ui_util.h"
#include "window_chrome.h"

#include "imgui.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <vector>

namespace vj {

namespace {

constexpr Sint64 kMaxFileBytes = 64 * 1024 * 1024;   // well past any cart and its symbols; keeps disc images out

enum Column { kColBreak, kColValue, kColName, kColKind, kColCpu };

struct State {
    SymbolsUiContext ctx;
    bool open = false;
    bool hooked = false;

    std::vector<Symbol> symbols;
    std::vector<size_t> view;      // indices into symbols, searched and sorted
    bool viewDirty = true;
    char search[128] = "";
    int sortColumn = kColValue;
    bool sortAscending = true;

    bool loaded = false;           // the sources below have been read
    std::string contentPath;       // the content they were read for
    std::string pickedPath;        // a file chosen in the window; wins until the content changes
    std::string sourcePath;        // where the current symbols came from

    std::string status;
    bool statusIsError = false;

    std::mutex pendingLock;        // the dialog's answer can arrive on another thread
    std::string pendingPath;
};
State g;

void setStatus(const std::string& m, bool err) {
    g.status = m;
    g.statusIsError = err;
}

std::string fileName(const std::string& path) {
    const size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

// The same path with the extension replaced.  `path` with no extension just gains one.
std::string withExtension(const std::string& path, const char* ext) {
    const size_t dot = path.find_last_of('.');
    const size_t slash = path.find_last_of("/\\");
    const bool hasExt = dot != std::string::npos && (slash == std::string::npos || dot > slash);
    return (hasExt ? path.substr(0, dot) : path) + ext;
}

SymbolParse readSymbols(const std::string& path, std::vector<Symbol>& out) {
    out.clear();
    SDL_PathInfo info;
    if (!SDL_GetPathInfo(path.c_str(), &info) || info.type != SDL_PATHTYPE_FILE || info.size > (Uint64)kMaxFileBytes)
        return SymbolParse::NotExecutable;
    std::vector<unsigned char> bytes;
    if (!readFile(path, bytes)) return SymbolParse::NotExecutable;
    return parseSymbols(bytes.data(), bytes.size(), out);
}

const char* whyNot(SymbolParse r) {
    switch (r) {
        case SymbolParse::NoSymbols: return "has no symbol table. Link it with rln -s (add -l for local labels).";
        case SymbolParse::Malformed: return "has a damaged symbol table.";
        default: return "is not a .cof or .abs with symbols.";
    }
}

void adopt(const std::string& path, std::vector<Symbol>& symbols) {
    g.symbols.swap(symbols);
    g.sourcePath = path;
    g.viewDirty = true;
    char line[64];
    std::snprintf(line, sizeof line, "%zu symbols from ", g.symbols.size());
    setStatus(line + fileName(path), false);
}

// Fills the table from the picked file, or else the content, or else a .cof beside the content.
void loadSymbols() {
    g.loaded = true;
    g.symbols.clear();
    g.sourcePath.clear();
    g.viewDirty = true;
    std::vector<Symbol> found;
    if (!g.pickedPath.empty()) {
        const SymbolParse r = readSymbols(g.pickedPath, found);
        if (r == SymbolParse::Ok) adopt(g.pickedPath, found);
        else setStatus(fileName(g.pickedPath) + " " + whyNot(r), true);
        return;
    }
    if (g.contentPath.empty()) {
        setStatus("No game is loaded", false);
        return;
    }
    const SymbolParse own = readSymbols(g.contentPath, found);
    if (own == SymbolParse::Ok) {
        adopt(g.contentPath, found);
        return;
    }
    for (const char* ext : {".cof", ".COF"}) {
        const std::string beside = withExtension(g.contentPath, ext);
        if (beside != g.contentPath && readSymbols(beside, found) == SymbolParse::Ok) {
            adopt(beside, found);
            return;
        }
    }
    const std::string name = fileName(g.contentPath);
    if (own == SymbolParse::NotExecutable) setStatus(name + " carries no symbols. Load a .cof, or put one with the same name beside it.", false);
    else setStatus(name + " " + whyNot(own), own == SymbolParse::Malformed);
}

// The current source: the content may have moved (another game, a disc swap), and a picked file may have arrived.
void syncSource() {
    bool changed = !g.loaded;
    const std::string path = g.ctx.contentPath ? g.ctx.contentPath() : std::string();
    if (path != g.contentPath) {
        g.contentPath = path;
        g.pickedPath.clear();
        changed = true;
    }
    {
        std::lock_guard<std::mutex> lk(g.pendingLock);
        if (!g.pendingPath.empty()) {
            g.pickedPath.swap(g.pendingPath);
            g.pendingPath.clear();
            changed = true;
        }
    }
    if (changed) loadSymbols();
}

void rebuildView() {
    if (!g.viewDirty) return;
    g.viewDirty = false;
    g.view.clear();
    const std::string query = g.search;
    for (size_t i = 0; i < g.symbols.size(); i++)
        if (symbolMatches(g.symbols[i], query)) g.view.push_back(i);
    const int column = g.sortColumn;
    const bool ascending = g.sortAscending;
    std::stable_sort(g.view.begin(), g.view.end(), [column, ascending](size_t ia, size_t ib) {
        const Symbol& a = g.symbols[ascending ? ia : ib];
        const Symbol& b = g.symbols[ascending ? ib : ia];
        int c = 0;
        if (column == kColName) c = a.name.compare(b.name);
        else if (column == kColKind) c = (int)a.kind - (int)b.kind;
        else if (column == kColCpu) c = symbolTarget(a.value) - symbolTarget(b.value);
        if (c == 0) c = a.value < b.value ? -1 : a.value > b.value ? 1 : a.name.compare(b.name);
        return c < 0;
    });
}

void toggleBreakpoint(DebugSession& s, int target, const Symbol& sym) {
    const bool had = s.hasBreakpoint(target, sym.value);
    const bool now = s.toggleBreakpoint(target, sym.value);
    if (!had && !now) {
        setStatus("Could not set the breakpoint (the core's table is full, or the debugger is not attached)", true);
        return;
    }
    setStatus(std::string(now ? "Breakpoint set on " : "Breakpoint removed from ") + sym.name + " (" + dbgTargetName(target) + ")", false);
}

void formatValue(char* out, size_t n, uint32_t value) { std::snprintf(out, n, "$%06X", value); }

void drawRowMenu(DebugSession& s, const Symbol& sym) {
    if (!ImGui::BeginPopupContextItem("##symbol_menu")) return;
    ImGui::TextDisabled("%s", sym.name.c_str());
    ImGui::Separator();
    const bool canBreak = symbolCanBreak(sym.value);
    for (int target : {VJ_DBG_TARGET_M68K, VJ_DBG_TARGET_GPU, VJ_DBG_TARGET_DSP}) {
        std::string label = std::string("Breakpoint on ") + dbgTargetName(target);
        if (ImGui::MenuItem(label.c_str(), nullptr, s.hasBreakpoint(target, sym.value), canBreak)) toggleBreakpoint(s, target, sym);
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Show in CPU debugger", nullptr, false, canBreak && g.ctx.showCpu)) g.ctx.showCpu(symbolTarget(sym.value), sym.value);
    if (ImGui::MenuItem("Show in memory viewer", nullptr, false, sym.value <= 0xFFFFFFu && g.ctx.showMemory)) g.ctx.showMemory(sym.value);
    char value[16];
    formatValue(value, sizeof value, sym.value);
    if (ImGui::MenuItem("Copy address")) ImGui::SetClipboardText(value + 1);
    if (ImGui::MenuItem("Copy name")) ImGui::SetClipboardText(sym.name.c_str());
    ImGui::EndPopup();
}

void drawRow(DebugSession& s, const Symbol& sym) {
    const int target = symbolTarget(sym.value);
    const bool canBreak = symbolCanBreak(sym.value);
    ImGui::TableNextRow();

    ImGui::TableSetColumnIndex(kColBreak);
    bool set = s.hasBreakpoint(target, sym.value);
    ImGui::BeginDisabled(!canBreak);
    if (ImGui::Checkbox("##break", &set)) toggleBreakpoint(s, target, sym);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        if (canBreak) ImGui::SetTooltip("Breakpoint on the %s", dbgTargetName(target));
        else ImGui::SetTooltip("Not a code address");
    }

    ImGui::TableSetColumnIndex(kColValue);
    char value[16];
    formatValue(value, sizeof value, sym.value);
    ImGui::TextUnformatted(value);

    ImGui::TableSetColumnIndex(kColName);
    ImGui::Selectable(sym.name.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick);
    if (canBreak && g.ctx.showCpu && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        g.ctx.showCpu(target, sym.value);
    drawRowMenu(s, sym);

    ImGui::TableSetColumnIndex(kColKind);
    ImGui::TextDisabled("%s", symbolKindName(sym.kind));

    ImGui::TableSetColumnIndex(kColCpu);
    ImGui::TextDisabled("%s", dbgTargetName(target));
}

void drawTable(DebugSession& s) {
    const ImGuiTableFlags flags = ImGuiTableFlags_Sortable | ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg |
                                  ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingFixedFit;
    if (!ImGui::BeginTable("##symbols", 5, flags, ImVec2(0, -ImGui::GetFrameHeightWithSpacing()))) return;
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("BP", ImGuiTableColumnFlags_NoSort | ImGuiTableColumnFlags_WidthFixed, ui::em(2.0f), kColBreak);
    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_WidthFixed, ui::em(5.5f), kColValue);
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 0.0f, kColName);
    ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthFixed, ui::em(3.0f), kColKind);
    ImGui::TableSetupColumn("CPU", ImGuiTableColumnFlags_WidthFixed, ui::em(3.5f), kColCpu);
    ImGui::TableHeadersRow();
    if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs()) {
        if (specs->SpecsDirty && specs->SpecsCount > 0) {
            g.sortColumn = (int)specs->Specs[0].ColumnUserID;
            g.sortAscending = specs->Specs[0].SortDirection != ImGuiSortDirection_Descending;
            g.viewDirty = true;
        }
        specs->SpecsDirty = false;
    }
    rebuildView();
    ImGuiListClipper clipper;
    clipper.Begin((int)g.view.size());
    while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++) {
            ImGui::PushID(row);
            drawRow(s, g.symbols[g.view[(size_t)row]]);
            ImGui::PopID();
        }
    }
    ImGui::EndTable();
}

void drawWindow(DebugSession& s) {
    ui::nextWindow(40.0f, 36.0f, 28.0f, 16.0f);
    const bool shown = ui::beginWindow("Symbols", &g.open);
    ui::keepOnScreen();
    if (shown) {
        const float buttons = ui::em(9.0f);
        ImGui::SetNextItemWidth(-buttons);
        if (ImGui::InputTextWithHint("##symbol_search", "Search names or hex values", g.search, sizeof g.search)) g.viewDirty = true;
        ImGui::SameLine();
        ImGui::BeginDisabled(!g.ctx.pickFile);
        if (ImGui::Button("Load...")) g.ctx.pickFile();
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Reload")) loadSymbols();
        drawTable(s);
        if (g.statusIsError) ImGui::TextColored(ui::vec4(ui::pal().warning), "%s", g.status.c_str());
        else ImGui::TextDisabled("%s", g.status.c_str());
    }
    ImGui::End();
}

void forget() {
    g.open = false;
    g.loaded = false;
    g.symbols.clear();
    g.view.clear();
    g.viewDirty = true;
    g.contentPath.clear();
    g.pickedPath.clear();
    g.sourcePath.clear();
    setStatus("", false);
}

}  // namespace

void registerSymbolsTool(DebugSession& s, const SymbolsUiContext& ctx) {
    g.ctx = ctx;
    DebugTool t;
    t.id = "symbols";
    t.label = "Symbols...";
    t.isOpen = [] { return g.open; };
    t.toggle = [] { g.open = !g.open; };
    s.registerTool(t);
    if (g.hooked) return;   // one session per process; the hook is added once
    g.hooked = true;
    s.onForcedClose.push_back([](const char*) { forget(); });
}

void drawSymbolsTool(DebugSession& s) {
    if (!g.open) return;
    syncSource();
    drawWindow(s);
}

void symbolsToolFilePicked(const std::string& path) {
    std::lock_guard<std::mutex> lk(g.pendingLock);
    g.pendingPath = path;
}

namespace symbols_test {
size_t rowCount() {
    rebuildView();
    return g.view.size();
}
const std::string& rowName(size_t row) {
    rebuildView();
    return g.symbols[g.view[row]].name;
}
void setSearch(const std::string& query) {
    std::snprintf(g.search, sizeof g.search, "%s", query.c_str());
    g.viewDirty = true;
}
void toggleBreakpoint(DebugSession& s, size_t row) {
    rebuildView();
    const Symbol& sym = g.symbols[g.view[row]];
    vj::toggleBreakpoint(s, symbolTarget(sym.value), sym);
}
const std::string& status() { return g.status; }
bool statusIsError() { return g.statusIsError; }
}  // namespace symbols_test

}  // namespace vj
