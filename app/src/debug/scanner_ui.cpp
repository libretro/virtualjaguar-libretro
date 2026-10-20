// The Cheat scanner window: a Cheat-Engine / RetroArch style memory search over the 2 MB of main RAM.
// New search -> let the game run -> Next scan (changed / increased / equal to ...) until a few addresses are
// left; double-click a value to poke it, or "Add as cheat" to keep it in the Cheats window.
//
// All the logic is cheat_scan.* (the search) and scanner_actions.* (poke / cheat); this file is layout only.
// Everything runs on the main thread between retro_run calls.  Disabled (with the reason) whenever the
// debug tools are -- no core interface, no game, RetroAchievements hardcore -- and "Add as cheat" additionally
// obeys the Cheats window's own hardcore block.
#include "scanner_ui.h"

#include "cheat_scan.h"
#include "cheats.h"
#include "core_host.h"
#include "debug_session.h"
#include "scanner_actions.h"
#include "ui_util.h"
#include "window_chrome.h"

#include "imgui.h"
#include "libretro.h"

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace vj {

namespace {

constexpr size_t kMaxRows = 1000;          // shown; the whole candidate set is kept
constexpr double kLiveInterval = 0.1;      // 10 Hz, like every live debug view

struct ScannerState {
    bool open = false;
    bool hooked = false;

    CheatScan scan;
    // search settings
    int widthIdx = 0;                      // 0: 8 bit, 1: 16 bit, 2: 32 bit
    bool isSigned = false;
    bool aligned = true;
    bool hexView = false;
    int firstModeIdx = 0, nextModeIdx = 0;
    char val1[24] = "";
    char val2[24] = "";
    std::string message;
    bool messageIsError = false;

    // results
    std::vector<uint32_t> rows;            // the first kMaxRows candidates
    bool rowsDirty = true;
    std::vector<int64_t> cur;              // live values of `rows`, refreshed at 10 Hz
    double lastLive = -1;
    uint64_t lastSerial = 0;

    // edit / add
    bool editing = false;
    uint32_t editAddr = 0;
    char editBuf[24] = "";
    bool editFocus = false;
    bool addPopup = false;
    uint32_t addAddr = 0;
    char addDesc[64] = "";
    char addVal[24] = "";
};
ScannerState g;

const unsigned kWidths[3] = {1, 2, 4};
const char* kWidthNames[3] = {"8-bit", "16-bit", "32-bit"};
const ScanMode kFirstModes[] = {ScanMode::Unknown, ScanMode::Equal,   ScanMode::NotEqual,
                                ScanMode::Greater, ScanMode::Less,    ScanMode::Range};
const ScanMode kNextModes[] = {ScanMode::Changed,   ScanMode::Unchanged,   ScanMode::Increased, ScanMode::Decreased,
                               ScanMode::IncreasedBy, ScanMode::DecreasedBy, ScanMode::Equal,     ScanMode::NotEqual,
                               ScanMode::Greater,   ScanMode::Less,        ScanMode::Range};

template <size_t N>
int clampIdx(int i, const ScanMode (&)[N]) {
    return std::max(0, std::min(i, (int)N - 1));
}

// The RAM to search: the core's own pointer (a bulk copy is all a scan makes of it), or, if the core would not
// hand one out, the debug interface's side-effect-free peek.
const uint8_t* ramNow(DebugSession& session, CoreHost& host, size_t& n, std::vector<uint8_t>& fallback) {
    uint8_t* d = nullptr;
    size_t sz = 0;
    if (host.isLoaded() && host.memoryRegion(RETRO_MEMORY_SYSTEM_RAM, d, sz) && d && sz) {
        n = sz;
        return d;
    }
    if (session.snapshotDram(fallback)) {
        n = fallback.size();
        return fallback.data();
    }
    n = 0;
    return nullptr;
}

void say(const std::string& m, bool error) {
    g.message = m;
    g.messageIsError = error;
}

void resetSearch() {
    g.scan.reset();
    g.rows.clear();
    g.cur.clear();
    g.rowsDirty = true;
    g.editing = false;
    g.addPopup = false;
}

void forceClose(const char*) {
    resetSearch();
    g.message.clear();
    g.open = false;
}

bool parseInto(const char* text, int64_t& out, const char* what) {
    if (!CheatScan::parseNumber(text, out)) {
        say(std::string("The ") + what + " is not a number (decimal, 0x1F or $1F).", true);
        return false;
    }
    return true;
}

// Run the scan the controls describe: a first pass when no search is active, a narrowing otherwise.
void doScan(DebugSession& session, CoreHost& host) {
    size_t n = 0;
    std::vector<uint8_t> fb;
    const uint8_t* ram = ramNow(session, host, n, fb);
    if (!ram) {
        say("No RAM to search.", true);
        return;
    }
    const bool first = !g.scan.active();
    ScanSpec s;
    s.mode = first ? kFirstModes[clampIdx(g.firstModeIdx, kFirstModes)] : kNextModes[clampIdx(g.nextModeIdx, kNextModes)];
    if (CheatScan::modeNeedsValue(s.mode)) {
        if (!parseInto(g.val1, s.value, s.mode == ScanMode::Range ? "low value" : "value")) return;
        if (CheatScan::modeNeedsSecondValue(s.mode) && !parseInto(g.val2, s.value2, "high value")) return;
    }
    std::string err;
    bool ok;
    if (first) {
        ScanOptions o;
        o.width = kWidths[std::max(0, std::min(g.widthIdx, 2))];
        o.isSigned = g.isSigned;
        o.aligned = g.aligned;
        ok = g.scan.begin(ram, n, o, s, &err);
    } else {
        ok = g.scan.next(ram, n, s, &err);
    }
    if (!ok) {
        say(err, true);
        return;
    }
    g.rowsDirty = true;
    g.editing = false;
    char b[96];
    std::snprintf(b, sizeof b, "%zu candidate%s after %zu pass%s.", g.scan.count(), g.scan.count() == 1 ? "" : "s",
                  g.scan.passes(), g.scan.passes() == 1 ? "" : "es");
    say(b, false);
}

void refreshRows(DebugSession& session, CoreHost& host, double now) {
    if (g.rowsDirty) {
        g.scan.results(0, kMaxRows, g.rows);
        g.rowsDirty = false;
        g.lastLive = -1;    // read the values now
    }
    if (g.lastLive < 0 || now - g.lastLive >= kLiveInterval || session.refreshSerial() != g.lastSerial) {
        size_t n = 0;
        std::vector<uint8_t> fb;
        const uint8_t* ram = ramNow(session, host, n, fb);
        g.cur.resize(g.rows.size());
        for (size_t i = 0; i < g.rows.size(); i++) g.cur[i] = ram ? g.scan.valueIn(ram, n, g.rows[i]) : 0;
        g.lastLive = now;
        g.lastSerial = session.refreshSerial();
    }
}

std::string fmt(int64_t v, bool hex) {
    const unsigned w = g.scan.options().width;
    std::string s = std::to_string((long long)v);
    if (hex) s += "  $" + CheatScan::formatValue(v, w, true);
    return s;
}

bool parseEdit(const char* text, int64_t& v) {
    const unsigned w = g.scan.options().width;
    if (!CheatScan::parseNumber(text, v)) return false;
    // Either reading of the bits is accepted: -1 and 255 both mean $FF in a byte.
    return v >= CheatScan::minValue(w, true) && v <= CheatScan::maxValue(w, false);
}

void drawControls(DebugSession& session, CoreHost& host) {
    const bool active = g.scan.active();

    ImGui::BeginDisabled(active);
    ImGui::SetNextItemWidth(ui::fitWidth(0.25f, 7.0f, 10.0f));
    if (ImGui::BeginCombo("##width", kWidthNames[std::max(0, std::min(g.widthIdx, 2))])) {
        for (int i = 0; i < 3; i++)
            if (ImGui::Selectable(kWidthNames[i], i == g.widthIdx)) g.widthIdx = i;
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::Checkbox("Signed", &g.isSigned);
    ImGui::SameLine();
    ImGui::Checkbox("Aligned", &g.aligned);
    ImGui::EndDisabled();
    if (active) {
        ImGui::SameLine();
        ui::hint("(locked: press New search to change)");
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && !active) {
        ui::beginWrappedTooltip();
        ImGui::TextUnformatted("Aligned: only addresses a whole value sits at (the 68000 reads words and longs at even addresses).");
        ui::endWrappedTooltip();
    }

    // Mode.
    const ScanMode* modes = active ? kNextModes : kFirstModes;
    const int count = active ? (int)(sizeof kNextModes / sizeof kNextModes[0]) : (int)(sizeof kFirstModes / sizeof kFirstModes[0]);
    int& idx = active ? g.nextModeIdx : g.firstModeIdx;
    idx = std::max(0, std::min(idx, count - 1));
    ImGui::SetNextItemWidth(ui::fitWidth(0.5f, 12.0f, 20.0f));
    if (ImGui::BeginCombo("##mode", CheatScan::modeName(modes[idx]))) {
        for (int i = 0; i < count; i++)
            if (ImGui::Selectable(CheatScan::modeName(modes[i]), i == idx)) idx = i;
        ImGui::EndCombo();
    }
    const ScanMode mode = modes[idx];
    if (CheatScan::modeNeedsValue(mode)) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ui::fitWidth(0.3f, 7.0f, 12.0f));
        bool enter = ImGui::InputTextWithHint("##v1", mode == ScanMode::Range ? "low" : "value", g.val1, sizeof g.val1,
                                              ImGuiInputTextFlags_EnterReturnsTrue);
        if (CheatScan::modeNeedsSecondValue(mode)) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(ui::fitWidth(0.4f, 7.0f, 12.0f));
            enter = ImGui::InputTextWithHint("##v2", "high", g.val2, sizeof g.val2, ImGuiInputTextFlags_EnterReturnsTrue) || enter;
        }
        if (enter) doScan(session, host);
    }

    ui::Flow flow;
    if (ImGui::Button(active ? "Next scan" : "Start search")) doScan(session, host);
    ImGui::BeginDisabled(!g.scan.canUndo());
    flow.next(ui::Flow::buttonW("Undo"));
    if (ImGui::Button("Undo")) {
        g.scan.undo();
        g.rowsDirty = true;
        g.editing = false;
        char b[64];
        std::snprintf(b, sizeof b, "%zu candidates (undid the last scan).", g.scan.count());
        say(b, false);
    }
    ImGui::EndDisabled();
    ImGui::BeginDisabled(!active);
    flow.next(ui::Flow::buttonW("New search"));
    if (ImGui::Button("New search")) {
        resetSearch();
        say("", false);
    }
    ImGui::EndDisabled();
    flow.next(ui::Flow::checkW("Hex"));
    ImGui::Checkbox("Hex", &g.hexView);

    if (!g.message.empty())
        ui::coloredWrapped(g.messageIsError ? ui::vec4(ui::pal().error) : ui::vec4(ui::pal().success), g.message.c_str());
    else if (!active)
        ui::hint("Pick \"Unknown initial value\" (or a value), press Start search, let the game run, then use Next scan "
                 "(Changed, Increased, Equal to ...) until only a few addresses are left.");
}

void drawAddPopup(DebugSession& session, CheatManager& cheats) {
    if (g.addPopup) {
        ImGui::OpenPopup("Add as cheat##scan");
        g.addPopup = false;
    }
    if (!ImGui::BeginPopupModal("Add as cheat##scan", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::Text("Address %06X, %u-bit", (unsigned)g.addAddr, g.scan.options().width * 8);
    ImGui::SetNextItemWidth(ui::em(18));
    ImGui::InputTextWithHint("##adesc", "Name", g.addDesc, sizeof g.addDesc);
    ImGui::SetNextItemWidth(ui::em(10));
    ImGui::InputText("Value to hold", g.addVal, sizeof g.addVal);
    static std::string err;
    if (!err.empty()) ui::coloredWrapped(ui::vec4(ui::pal().error), err.c_str());
    if (ImGui::Button("Add")) {
        int64_t v = 0;
        if (!parseEdit(g.addVal, v)) {
            err = "That value does not fit a " + std::to_string(g.scan.options().width * 8) + "-bit number.";
        } else if (scanAddCheat(cheats, session, g.addAddr, v, g.scan.options().width, g.addDesc, err)) {
            err.clear();
            say("Added to the Cheats window: " + scanCheatCode(g.addAddr, v, g.scan.options().width), false);
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        err.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void drawResults(DebugSession& session, CheatManager& cheats, CoreHost& host) {
    if (!g.scan.active()) return;
    const unsigned w = g.scan.options().width;
    const size_t total = g.scan.count();
    refreshRows(session, host, ImGui::GetTime());
    if (total > g.rows.size())
        ui::hint("Showing the first %zu of %zu candidates (the rest are kept; narrow the search to see them).", g.rows.size(), total);
    else
        ui::hint("%zu candidate%s. Double-click a value to change it.", total, total == 1 ? "" : "s");

    const bool canCheat = !cheats.blockedNow() && cheats.active();
    if (!canCheat)
        ui::coloredWrapped(ui::vec4(ui::pal().warning),
                           cheats.blockedNow() ? "Cheats are disabled in hardcore mode." : "Load a game to add cheats.");

    ImGuiTableFlags tf = ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                         ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp;
    if (!ImGui::BeginTable("##scanres", 4, tf, ImVec2(0, 0))) return;
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_WidthFixed, ui::em(7));
    ImGui::TableSetupColumn("Current");
    ImGui::TableSetupColumn("Previous");
    ImGui::TableSetupColumn("##act", ImGuiTableColumnFlags_WidthFixed, ui::Flow::buttonW("Add as cheat"));
    ImGui::TableHeadersRow();

    ImGuiListClipper clip;
    clip.Begin((int)g.rows.size());
    while (clip.Step()) {
        for (int i = clip.DisplayStart; i < clip.DisplayEnd; i++) {
            const uint32_t addr = g.rows[(size_t)i];
            ImGui::PushID(i);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            char a[16];
            std::snprintf(a, sizeof a, "%06X", (unsigned)addr);
            ImGui::Selectable(a, g.editing && g.editAddr == addr,
                              ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap);
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0) && !g.editing) {
                g.editing = true;
                g.editAddr = addr;
                g.editFocus = true;
                std::snprintf(g.editBuf, sizeof g.editBuf, "%lld", (long long)g.cur[(size_t)i]);
            }
            ImGui::TableSetColumnIndex(1);
            if (g.editing && g.editAddr == addr) {
                if (g.editFocus) {
                    ImGui::SetKeyboardFocusHere();
                    g.editFocus = false;
                }
                ImGui::SetNextItemWidth(-FLT_MIN);
                const bool commit = ImGui::InputText("##edit", g.editBuf, sizeof g.editBuf,
                                                     ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
                if (commit) {
                    int64_t v = 0;
                    std::string err;
                    if (!parseEdit(g.editBuf, v)) say("That value does not fit a " + std::to_string(w * 8) + "-bit number.", true);
                    else if (!scanPoke(session, addr, v, w, err)) say("Could not write: " + err, true);
                    else say("Wrote " + CheatScan::formatValue(v, w, true) + " at " + a, false);
                    g.editing = false;
                    g.lastLive = -1;
                } else if (ImGui::IsItemDeactivated() || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                    g.editing = false;   // Escape, or the focus went elsewhere
                }
            } else {
                ImGui::TextUnformatted(fmt(g.cur[(size_t)i], g.hexView).c_str());
            }
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(fmt(g.scan.previous(addr), g.hexView).c_str());
            ImGui::TableSetColumnIndex(3);
            ImGui::BeginDisabled(!canCheat);
            if (ImGui::SmallButton("Add as cheat")) {
                g.addPopup = true;
                g.addAddr = addr;
                g.addDesc[0] = 0;
                std::snprintf(g.addVal, sizeof g.addVal, "%lld", (long long)g.cur[(size_t)i]);
            }
            ImGui::EndDisabled();
            ImGui::PopID();
        }
    }
    ImGui::EndTable();
}

}  // namespace

namespace scanner_test {
void setOptions(int widthIdx, bool isSigned, bool aligned) {
    g.widthIdx = widthIdx;
    g.isSigned = isSigned;
    g.aligned = aligned;
}
void setMode(int firstModeIdx, int nextModeIdx) {
    g.firstModeIdx = firstModeIdx;
    g.nextModeIdx = nextModeIdx;
}
void setValues(const char* v1, const char* v2) {
    std::snprintf(g.val1, sizeof g.val1, "%s", v1 ? v1 : "");
    std::snprintf(g.val2, sizeof g.val2, "%s", v2 ? v2 : "");
}
void scan(DebugSession& session, CoreHost& host) { doScan(session, host); }
void undo() {
    g.scan.undo();
    g.rowsDirty = true;
}
void newSearch() { resetSearch(); }
size_t candidates() { return g.scan.count(); }
const std::string& message() { return g.message; }
size_t shownRows() { return g.rows.size(); }
void beginEdit(size_t row, const char* text) {
    if (row >= g.rows.size()) return;
    g.editing = true;
    g.editAddr = g.rows[row];
    g.editFocus = true;
    std::snprintf(g.editBuf, sizeof g.editBuf, "%s", text);
}
void openAdd(size_t row, const char* desc) {
    if (row >= g.rows.size() || row >= g.cur.size()) return;
    g.addPopup = true;
    g.addAddr = g.rows[row];
    std::snprintf(g.addDesc, sizeof g.addDesc, "%s", desc);
    std::snprintf(g.addVal, sizeof g.addVal, "%lld", (long long)g.cur[row]);
}
}  // namespace scanner_test

bool scannerIsOpen() { return g.open; }
void scannerSetOpen(bool open) { g.open = open; }

void registerScannerTool(DebugSession& session) {
    DebugTool t;
    t.id = "scanner";
    t.label = "Cheat scanner...";
    t.isOpen = [] { return g.open; };
    t.toggle = [] { g.open = !g.open; };
    session.registerTool(t);
    if (!g.hooked) {
        g.hooked = true;
        session.onForcedClose.push_back(forceClose);   // hardcore, content closed, no core: the search is void
    }
}

void drawScanner(DebugSession& session, CheatManager& cheats, CoreHost& host) {
    if (!g.open) return;
    ui::nextWindow(46.0f, 34.0f, 30.0f, 18.0f);
    bool shown = ui::beginWindow("Cheat scanner", &g.open);
    ui::keepOnScreen();
    if (!shown) {
        ImGui::End();
        return;
    }
    if (!session.enabled()) {
        // Greyed: nothing here may run (hardcore, no game, no core interface).
        ui::coloredWrapped(ui::vec4(ui::pal().warning), session.disabledReason().c_str());
        if (g.scan.active()) resetSearch();
        ImGui::End();
        return;
    }
    drawControls(session, host);
    ImGui::Separator();
    drawResults(session, cheats, host);
    drawAddPopup(session, cheats);
    ImGui::End();
}

}  // namespace vj
