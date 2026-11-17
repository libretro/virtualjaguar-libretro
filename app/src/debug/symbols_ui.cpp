// The Symbols window.  See symbols_ui.h.
#include "symbols_ui.h"

#include "debug_session.h"
#include "paths.h"
#include "symbol_store.h"
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

    SymbolStore* store = nullptr;  // the session's table (DebugSession::symbols()): shared with the CPU debugger
    uint64_t storeGen = ~0ull;     // SymbolStore::generation() the view and the status line were built for
    bool shown = false;            // the window has looked at the store since it was last closed by force (forget)
    std::vector<size_t> view;      // indices into the store's symbols, searched and sorted
    bool viewDirty = true;
    char search[128] = "";
    int sortColumn = kColValue;
    bool sortAscending = true;

    std::string status;
    bool statusIsError = false;

    std::mutex pendingLock;        // the dialog's answer can arrive on another thread
    std::string pendingPath;
};
State g;

const std::vector<Symbol>& symbols() {
    static const std::vector<Symbol> none;
    return g.store && g.shown ? g.store->symbols() : none;   // (a window closed by force lists nothing)
}

void setStatus(const std::string& m, bool err) {
    g.status = m;
    g.statusIsError = err;
}

// How the store reads a file here: through the app's own file access, regular files only, with the size cap.
bool readSymbolFile(const std::string& path, std::vector<unsigned char>& out) {
    SDL_PathInfo info;
    if (!SDL_GetPathInfo(path.c_str(), &info) || info.type != SDL_PATHTYPE_FILE || info.size > (Uint64)kMaxFileBytes) return false;
    return readFile(path, out);
}

// The current source: the content may have moved (another game, a disc swap, a rebuild), and a picked file may have
// arrived.  The store reads only when something changed; a new read rebuilds the view and the status line.
void syncSource(bool reload = false) {
    if (!g.store) return;
    SymbolStore& st = *g.store;
    st.setContent(g.ctx.contentPath ? g.ctx.contentPath() : std::string());
    {
        std::lock_guard<std::mutex> lk(g.pendingLock);
        if (!g.pendingPath.empty()) {
            st.setPicked(g.pendingPath);
            g.pendingPath.clear();
        }
    }
    if (reload) st.invalidate();
    st.ensure();
    if (g.storeGen == st.generation() && g.shown) return;
    g.shown = true;
    g.storeGen = st.generation();
    g.viewDirty = true;
    setStatus(st.status(), st.statusIsError());
}

void rebuildView() {
    if (!g.viewDirty) return;
    g.viewDirty = false;
    g.view.clear();
    const std::string query = g.search;
    const std::vector<Symbol>& all = symbols();
    for (size_t i = 0; i < all.size(); i++)
        if (symbolMatches(all[i], query)) g.view.push_back(i);
    const int column = g.sortColumn;
    const bool ascending = g.sortAscending;
    std::stable_sort(g.view.begin(), g.view.end(), [column, ascending, &all](size_t ia, size_t ib) {
        const Symbol& a = all[ascending ? ia : ib];
        const Symbol& b = all[ascending ? ib : ia];
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
    // The memory watchpoint (one slot): for a variable, "who writes this?".
    const bool canWatch = sym.value <= 0xFFFFFFu && s.watchSupported();
    const DebugWatch& w = s.watch();
    const bool watched = w.set && w.addr == sym.value;
    if (ImGui::BeginMenu("Watch (68000)", canWatch)) {
        static const struct {
            const char* label;
            unsigned len, kind;
        } kWatch[] = {{"Writes to the byte", 1, VJ_DBG_WATCH_WRITE},  {"Writes to the word", 2, VJ_DBG_WATCH_WRITE},
                      {"Writes to the long", 4, VJ_DBG_WATCH_WRITE},  {"Reads of the word", 2, VJ_DBG_WATCH_READ},
                      {"Reads and writes of the long", 4, VJ_DBG_WATCH_WRITE | VJ_DBG_WATCH_READ}};
        for (const auto& k : kWatch) {
            if (!ImGui::MenuItem(k.label, nullptr, watched && w.len == k.len && w.kind == k.kind)) continue;
            if (s.setWatch(sym.value, k.len, k.kind)) setStatus("Watching " + sym.name + ": the 68000 halts when it touches it", false);
            else setStatus("Could not set the watchpoint (a gdb client holds the one slot, or the debugger is not attached)", true);
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Clear the watchpoint", nullptr, false, w.set)) {
            s.clearWatch();
            setStatus("Watchpoint cleared", false);
        }
        ImGui::EndMenu();
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
            drawRow(s, symbols()[g.view[(size_t)row]]);
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
        if (ImGui::Button("Reload")) syncSource(true);
        drawTable(s);
        if (g.statusIsError) ImGui::TextColored(ui::vec4(ui::pal().warning), "%s", g.status.c_str());
        else ImGui::TextDisabled("%s", g.status.c_str());
    }
    ImGui::End();
}

// The tools were switched off: the window closes and a file picked by hand is dropped (it belonged to that game).  The
// table itself is the session's and stays for the CPU debugger; the next load points it at the new content.
void forget() {
    g.open = false;
    g.view.clear();
    g.viewDirty = true;
    g.storeGen = ~0ull;
    g.shown = false;
    if (g.store) g.store->dropPicked();
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
    g.store = &s.symbols();
    g.store->reader = readSymbolFile;
    if (g.hooked) return;   // one session per process; the hook is added once
    g.hooked = true;
    s.onForcedClose.push_back([](const char* why) {
        // A reload of the same program keeps the window: App marked the table stale, the next draw reads the new build.
        if (why && !std::strcmp(why, "reload")) {
            g.storeGen = ~0ull;
            return;
        }
        forget();
    });
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
    return symbols()[g.view[row]].name;
}
void setSearch(const std::string& query) {
    std::snprintf(g.search, sizeof g.search, "%s", query.c_str());
    g.viewDirty = true;
}
void toggleBreakpoint(DebugSession& s, size_t row) {
    rebuildView();
    const Symbol& sym = symbols()[g.view[row]];
    vj::toggleBreakpoint(s, symbolTarget(sym.value), sym);
}
const std::string& status() { return g.status; }
bool statusIsError() { return g.statusIsError; }
}  // namespace symbols_test

}  // namespace vj
