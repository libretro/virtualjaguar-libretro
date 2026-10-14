#include "memory_ui.h"

#include "cheats.h"
#include "debug_session.h"
#include "memory_view.h"
#include "ui_util.h"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace vj {

using namespace memview;

namespace {

struct MemState {
    bool open = false;
    int region = 0;
    unsigned width = 1;                 // cell size in bytes: 1, 2, 4
    bool haveSel = false;
    unsigned sel = 0;                   // selected cell (address, aligned to `width` where the row allows)
    bool editing = false;
    unsigned editAddr = 0, editWidth = 1;
    bool editFocus = false;
    char editBuf[16] = "";
    char gotoBuf[16] = "";
    char findBuf[96] = "";
    int findMode = 0;                   // 0 hex bytes, 1 text
    bool icase = true;
    bool showChanges = true;
    std::unique_ptr<SearchJob> job;
    double pendingRow = -1;             // scroll the grid so this row is centred (next frame), -1 = none
    std::vector<unsigned> history;      // where "Back" goes
    std::string status;
    bool statusErr = false;
    // the visible window, as read last frame
    unsigned winStart = 0, winLen = 0;
    uint64_t winSerial = 0;
    bool winOk = false;
    std::vector<uint8_t> win;
    ChangeTracker track{8};
    int firstRow = 0, visRows = 1;      // for keyboard scrolling
    unsigned perRow = 16;
    bool lastHit = false;
    unsigned lastHitAddr = 0, lastHitSel = 0;
    const void* ifaceSeen = nullptr;    // region table changes with the core
};
MemState g_mem;

bool g_clutOpen = false;

void setStatus(const std::string& m, bool err) {
    g_mem.status = m;
    g_mem.statusErr = err;
}

unsigned alignDown(unsigned a, unsigned w) { return a & ~(w - 1); }

void selectAddr(DebugSession& s, unsigned addr, int regionIdx, bool pushHistory) {
    MemState& m = g_mem;
    const auto& regs = s.regions();
    if (regionIdx < 0) regionIdx = regionIndexAt(regs, addr);
    if (regionIdx < 0) return;
    if (pushHistory && m.haveSel && m.sel != addr) {
        m.history.push_back(m.sel);
        if (m.history.size() > 64) m.history.erase(m.history.begin());
    }
    m.region = regionIdx;
    m.sel = alignDown(addr, m.width);
    if (m.sel < regs[(size_t)regionIdx].start) m.sel = regs[(size_t)regionIdx].start;
    m.haveSel = true;
    m.editing = false;
    m.pendingRow = (double)((m.sel - regs[(size_t)regionIdx].start) / (m.perRow ? m.perRow : 16));
}

// Reads the bytes behind the visible rows (cached until the session says the views should refresh).
void fetchWindow(DebugSession& s, const vj_debug_region& r, unsigned start, unsigned len) {
    MemState& m = g_mem;
    const uint64_t serial = s.refreshSerial();
    if (m.winOk && m.winStart == start && m.winLen == len && m.winSerial == serial) return;
    const bool refresh = serial != m.winSerial;
    std::vector<uint8_t> buf;
    unsigned n = clampToRegion(r, start, len);
    if (n && s.read(start, buf, n)) {
        m.win.swap(buf);
        m.winOk = true;
        m.track.update(start, m.win.data(), (unsigned)m.win.size(), refresh);
    } else {
        m.win.clear();
        m.winOk = false;
    }
    m.winStart = start;
    m.winLen = len;
    m.winSerial = serial;
}

// A new core (or none) invalidates everything the window remembers.
void syncCore(DebugSession& s) {
    MemState& m = g_mem;
    if (m.ifaceSeen == (const void*)s.iface()) return;
    m.ifaceSeen = s.iface();
    m.region = 0;
    m.haveSel = false;
    m.editing = false;
    m.job.reset();
    m.history.clear();
    m.winOk = false;
    m.track.reset();
    m.pendingRow = 0;
    m.lastHit = false;
}

ImVec4 changedCol() { return ui::vec4(ui::pal().warning); }
ImVec4 warnCol() { return ui::vec4(ui::pal().warning); }
ImVec4 errCol() { return ui::vec4(ui::pal().error); }

void beginEdit(DebugSession& s, unsigned addr, unsigned w) {
    MemState& m = g_mem;
    WriteDecision d = decideWrite(s.regions(), addr, w, s.allowRegisterWrites);
    if (!d.ok) {
        char b[64];
        std::snprintf(b, sizeof b, "Cannot edit $%06X: ", addr);
        setStatus(b + d.reason, true);
        return;
    }
    std::vector<uint8_t> b;
    unsigned v = 0;
    if (!s.read(addr, b, w)) return;
    v = decodeBE(b.data(), w);
    std::snprintf(m.editBuf, sizeof m.editBuf, "%s", hexCell(v, w).c_str());
    m.editing = true;
    m.editAddr = addr;
    m.editWidth = w;
    m.editFocus = true;
}

void commitEdit(DebugSession& s) {
    MemState& m = g_mem;
    unsigned v;
    if (!parseCell(m.editBuf, m.editWidth, v)) {
        setStatus("Type up to " + std::to_string(m.editWidth * 2) + " hex digits", true);
        m.editing = false;
        return;
    }
    std::string why;
    int rc = writeCell(s, m.editAddr, m.editWidth, v, &why);
    char buf[160];
    if (rc < 0) {
        std::snprintf(buf, sizeof buf, "Write to $%06X refused: %s", m.editAddr, why.c_str());
        setStatus(buf, true);
    } else {
        std::snprintf(buf, sizeof buf, "Wrote $%s to $%06X", hexCell(v, m.editWidth).c_str(), m.editAddr);
        setStatus(buf, false);
    }
    m.editing = false;
}

void doSearch(DebugSession& s, const vj_debug_region& r) {
    MemState& m = g_mem;
    std::vector<uint8_t> needle;
    if (m.findMode == 0) {
        if (!parseHexBytes(m.findBuf, needle)) {
            setStatus("Hex search: type whole bytes, e.g. DE AD BE EF", true);
            return;
        }
    } else {
        needle = textNeedle(m.findBuf);
        if (needle.empty()) {
            setStatus("Type some text to search for", true);
            return;
        }
    }
    // "Find next" continues after the last hit while the selection is still the cell that hit selected
    // (a 16/32-bit cell is aligned below the hit, which would otherwise find the same hit again).
    unsigned from = 0xFFFFFFFFu;   // before the first byte
    if (m.haveSel && m.sel >= r.start && m.sel - r.start < r.len) from = (m.lastHit && m.sel == m.lastHitSel) ? m.lastHitAddr : m.sel;
    m.job.reset(new SearchJob(s, r, from, needle, m.findMode == 1 && m.icase));
    setStatus("Searching...", false);
}

void pollSearch(DebugSession& s, const vj_debug_region& r) {
    MemState& m = g_mem;
    if (!m.job) return;
    if (!m.job->step(4u << 20)) return;   // 4 MB per frame
    char buf[96];
    if (m.job->found()) {
        unsigned a = m.job->address();
        std::snprintf(buf, sizeof buf, "Found at $%06X", a);
        selectAddr(s, a, -1, true);
        m.lastHit = true;
        m.lastHitAddr = a;
        m.lastHitSel = m.sel;
        setStatus(buf, false);
    } else if (m.job->readFailed()) {
        setStatus("The search could not read that region", true);
    } else {
        std::snprintf(buf, sizeof buf, "Not found in %s", r.name ? r.name : "this region");
        setStatus(buf, false);
    }
    m.job.reset();
}

void drawControls(DebugSession& s, const std::vector<vj_debug_region>& regs) {
    MemState& m = g_mem;
    char label[96];
    const vj_debug_region& cur = regs[(size_t)m.region];

    // ---- row 1: region + cell size ----------------------------------------------------------
    std::snprintf(label, sizeof label, "%s  $%06X-$%06X  %s", cur.name ? cur.name : "?", cur.start,
                  cur.start + cur.len - 1, regionKindText(cur));
    ImGui::SetNextItemWidth(ui::fitWidth(0.62f, 14.0f, 34.0f));
    if (ImGui::BeginCombo("Region", label)) {
        for (size_t i = 0; i < regs.size(); i++) {
            std::snprintf(label, sizeof label, "%s  $%06X-$%06X  %s##r%zu", regs[i].name ? regs[i].name : "?",
                          regs[i].start, regs[i].start + regs[i].len - 1, regionKindText(regs[i]), i);
            if (ImGui::Selectable(label, (int)i == m.region)) {
                m.region = (int)i;
                m.sel = regs[i].start;
                m.haveSel = true;
                m.editing = false;
                m.pendingRow = 0;
                m.job.reset();
            }
        }
        ImGui::EndCombo();
    }
    ui::Flow fl;
    static const struct {
        const char* label;
        unsigned w;
    } kW[] = {{"8-bit", 1}, {"16-bit", 2}, {"32-bit", 4}};
    for (int i = 0; i < 3; i++) {
        if (i > 0) fl.next(ui::Flow::checkW(kW[i].label));
        if (ImGui::RadioButton(kW[i].label, m.width == kW[i].w) && m.width != kW[i].w) {
            m.width = kW[i].w;
            if (m.haveSel) m.sel = alignDown(m.sel, m.width);
            m.editing = false;
        }
    }

    // ---- row 2: go to / follow / back ---------------------------------------------------------------
    ImGui::SetNextItemWidth(ui::em(9.0f));
    bool goEnter = ImGui::InputTextWithHint("##goto", "Go to (hex)", m.gotoBuf, sizeof m.gotoBuf,
                                            ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CharsHexadecimal |
                                                ImGuiInputTextFlags_CharsUppercase);
    ImGui::SameLine();
    if (ImGui::Button("Go") || goEnter) {
        unsigned a = 0;
        int idx = -1;
        std::string err;
        if (resolveGoto(regs, m.gotoBuf, a, idx, err)) {
            selectAddr(s, a, idx, true);
            setStatus("", false);
        } else {
            setStatus(err, true);
        }
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!m.haveSel);
    if (ImGui::Button("Follow pointer")) {
        unsigned v = 0;
        if (s.readU(m.sel, 4, v)) {
            unsigned a = pointerTarget(v);
            int idx = regionIndexAt(regs, a);
            if (idx >= 0) {
                selectAddr(s, a, idx, true);
                char b[64];
                std::snprintf(b, sizeof b, "Followed pointer to $%06X", a);
                setStatus(b, false);
            } else {
                char b[96];
                std::snprintf(b, sizeof b, "$%08X (low 24 bits $%06X) is not mapped to anything", v, a);
                setStatus(b, true);
            }
        }
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Reads the 32-bit value at the selected cell as a big-endian pointer and jumps to it");
    ImGui::SameLine();
    ImGui::BeginDisabled(m.history.empty());
    if (ImGui::Button("Back") && !m.history.empty()) {
        unsigned a = m.history.back();
        m.history.pop_back();
        selectAddr(s, a, -1, false);
    }
    ImGui::EndDisabled();

    // ---- row 3: search --------------------------------------------------------------------------------------
    ImGui::SetNextItemWidth(ui::fitWidth(0.40f, 10.0f, 24.0f));
    bool findEnter = ImGui::InputTextWithHint("##find", m.findMode == 0 ? "Bytes, e.g. DE AD BE EF" : "Text",
                                              m.findBuf, sizeof m.findBuf, ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ui::em(5.5f));
    const char* modes[] = {"Hex", "Text"};
    ImGui::Combo("##fmode", &m.findMode, modes, 2);
    ImGui::SameLine();
    if (m.findMode == 1) {
        ImGui::Checkbox("Aa", &m.icase);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Ignore case");
        ImGui::SameLine();
    }
    if (m.job) {
        if (ImGui::Button("Cancel")) {
            m.job.reset();
            setStatus("Search cancelled", false);
        } else {
            ImGui::SameLine();
            ImGui::ProgressBar(m.job->progress(), ImVec2(ui::em(6.0f), 0));
        }
    } else if (ImGui::Button("Find next") || findEnter) {
        doSearch(s, cur);
    }

    // ---- row 4: writes ----------------------------------------------------------------------------------------------
    ImGui::Checkbox("Allow register writes", &s.allowRegisterWrites);
    ImGui::SameLine();
    ImGui::Checkbox("Highlight changes", &m.showChanges);
    if (s.allowRegisterWrites)
        ui::coloredWrapped(warnCol(),
                           "Register writes are on: a wrong value can hang or crash the game. B_CMD, G_CTRL, D_CTRL, OLP, "
                           "LTXD, RTXD and the CD registers stay blocked.");
    if (!m.status.empty()) ui::coloredWrapped(m.statusErr ? errCol() : ImGui::GetStyleColorVec4(ImGuiCol_Text), m.status.c_str());
}

void drawGrid(DebugSession& s, const vj_debug_region& r, float footerH) {
    MemState& m = g_mem;
    ImGui::BeginChild("##grid", ImVec2(0, -footerH), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
    const float charW = ImGui::CalcTextSize("0").x;
    const float pitch = ImGui::GetTextLineHeightWithSpacing();
    const float lineH = ImGui::GetTextLineHeight();
    const float addrW = charW * 8.0f;                        // "00F000" + gap
    const float cellW = charW * (float)(m.width * 2 + 1);    // digits + gap
    const float availW = ImGui::GetContentRegionAvail().x;
    m.perRow = bytesPerRowFor(m.width, availW - addrW - charW * 2.0f, cellW + charW * (float)m.width);
    const unsigned perRow = m.perRow;
    const unsigned rows = rowCount(r, perRow);
    const unsigned cells = perRow / m.width;
    const float asciiX = addrW + cellW * (float)cells + charW;

    if (m.pendingRow >= 0) {
        float y = (float)(m.pendingRow * pitch) - ImGui::GetWindowHeight() * 0.5f + pitch;
        ImGui::SetScrollY(std::max(0.0f, y));
        m.pendingRow = -1;
    }

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 colText = ImGui::GetColorU32(ImGuiCol_Text);
    const ImU32 colDim = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    const ImU32 colChg = ImGui::GetColorU32(changedCol());
    const ImU32 colSel = ImGui::GetColorU32(ImGuiCol_TextSelectedBg);
    const ImU32 colAddr = theme::isDark() ? IM_COL32(140, 190, 255, 255) : IM_COL32(25, 75, 170, 255);
    const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

    // keyboard navigation (not while typing into a cell or another box)
    if (focused && !m.editing && !ImGui::GetIO().WantTextInput && m.haveSel) {
        int dx = 0, dy = 0;
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) dx = -1;
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) dx = 1;
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) dy = -1;
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) dy = 1;
        if (ImGui::IsKeyPressed(ImGuiKey_PageUp)) dy = -(m.visRows - 1);
        if (ImGui::IsKeyPressed(ImGuiKey_PageDown)) dy = m.visRows - 1;
        if (dx || dy) {
            long long a = (long long)m.sel + (long long)dx * m.width + (long long)dy * perRow;
            a = std::max<long long>(r.start, std::min<long long>(a, (long long)r.start + r.len - m.width));
            m.sel = alignDown((unsigned)a, m.width);
            long long row = (long long)(m.sel - r.start) / perRow;
            if (row < m.firstRow || row >= m.firstRow + m.visRows - 1) m.pendingRow = (double)row;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)) beginEdit(s, m.sel, m.width);
    }

    ImGuiListClipper clipper;
    clipper.Begin((int)rows, pitch);
    bool editDrawn = false;
    while (clipper.Step()) {
        const int r0 = clipper.DisplayStart, r1 = clipper.DisplayEnd;
        if (r1 <= r0) continue;
        m.firstRow = r0;
        m.visRows = std::max(1, r1 - r0);
        const unsigned winStart = r.start + (unsigned)r0 * perRow;
        fetchWindow(s, r, winStart, (unsigned)(r1 - r0) * perRow);
        for (int row = r0; row < r1; row++) {
            const unsigned raddr = r.start + (unsigned)row * perRow;
            const unsigned n = clampToRegion(r, raddr, perRow);
            const ImVec2 p = ImGui::GetCursorScreenPos();
            char abuf[16];
            std::snprintf(abuf, sizeof abuf, "%06X", raddr);
            dl->AddText(p, colAddr, abuf);
            unsigned i = 0;
            unsigned col = 0;
            while (i < n) {
                const unsigned w = (n - i >= m.width) ? m.width : 1;
                const unsigned a = raddr + i;
                const float x = p.x + addrW + (float)col * cellW;
                const bool isSel = m.haveSel && m.sel >= a && m.sel < a + w;
                if (isSel) dl->AddRectFilled(ImVec2(x - 1, p.y - 1), ImVec2(x + cellW - charW * 0.5f, p.y + lineH + 1), colSel);
                if (m.editing && m.editAddr == a && m.editWidth == w) {
                    editDrawn = true;
                    ImGui::SetCursorScreenPos(ImVec2(x - 2, p.y - 2));
                    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 1));
                    ImGui::SetNextItemWidth(cellW + charW);
                    if (m.editFocus) {
                        ImGui::SetKeyboardFocusHere();
                        m.editFocus = false;
                    }
                    bool enter = ImGui::InputText("##cell", m.editBuf, sizeof m.editBuf,
                                                  ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_CharsUppercase |
                                                      ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
                    ImGui::PopStyleVar();
                    const bool lost = ImGui::IsItemDeactivated() && !enter;
                    ImGui::SetCursorScreenPos(p);
                    if (enter) {
                        commitEdit(s);
                        // move on to the next cell, as hex editors do
                        if (a + w + w <= r.start + r.len) m.sel = a + w;
                    } else if (lost) {
                        m.editing = false;   // Escape or a click elsewhere: no write
                    }
                } else {
                    unsigned v = 0;
                    std::string t;
                    bool chg = false;
                    if (m.winOk && a >= m.winStart && a + w - m.winStart <= m.win.size()) {
                        v = decodeBE(&m.win[a - m.winStart], w);
                        t = hexCell(v, w);
                        if (m.showChanges)
                            for (unsigned k = 0; k < w; k++) chg = chg || m.track.changed(a + k);
                    } else {
                        t.assign(w * 2, '?');
                    }
                    dl->AddText(ImVec2(x, p.y), chg ? colChg : (m.winOk && v == 0 ? colDim : colText), t.c_str());
                }
                i += w;
                col++;
            }
            // ASCII column
            if (m.winOk) {
                char ab[2] = {0, 0};
                for (unsigned k = 0; k < n; k++) {
                    const unsigned a = raddr + k;
                    if (a < m.winStart || a - m.winStart >= m.win.size()) continue;
                    ab[0] = asciiChar(m.win[a - m.winStart]);
                    const bool chg = m.showChanges && m.track.changed(a);
                    const bool isSel = m.haveSel && a >= m.sel && a < m.sel + m.width;
                    if (isSel) dl->AddRectFilled(ImVec2(p.x + asciiX + charW * (float)k, p.y - 1),
                                                 ImVec2(p.x + asciiX + charW * (float)(k + 1), p.y + lineH + 1), colSel);
                    dl->AddText(ImVec2(p.x + asciiX + charW * (float)k, p.y), chg ? colChg : colText, ab);
                }
            }
            ImGui::Dummy(ImVec2(asciiX + charW * (float)perRow, lineH));
        }
    }
    clipper.End();
    if (m.editing && !editDrawn) m.editing = false;   // scrolled away

    // mouse: click selects a cell (or a byte in the ASCII column), double click edits
    if (!m.editing && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const ImVec2 mp = ImGui::GetMousePos();
        const float rowF = (mp.y - origin.y) / pitch;
        if (rowF >= 0) {
            const unsigned row = (unsigned)rowF;
            if (row < rows) {
                const unsigned raddr = r.start + row * perRow;
                const unsigned n = clampToRegion(r, raddr, perRow);
                const float dx = mp.x - origin.x - addrW;
                bool hit = false;
                unsigned a = 0, w = m.width;
                if (dx >= 0 && dx < cellW * (float)cells) {
                    unsigned c = (unsigned)(dx / cellW);
                    a = raddr + c * m.width;
                    hit = a < raddr + n;
                    if (hit && n - c * m.width < m.width) w = 1;
                } else if (mp.x - origin.x >= asciiX) {
                    unsigned k = (unsigned)((mp.x - origin.x - asciiX) / charW);
                    if (k < n) {
                        a = alignDown(raddr + k, m.width);
                        hit = true;
                    }
                }
                if (hit) {
                    m.sel = a;
                    m.haveSel = true;
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) beginEdit(s, a, w);
                }
            }
        }
    }
    ImGui::EndChild();
}

void drawFooter(DebugSession& s, const std::vector<vj_debug_region>& regs, CheatManager* cheats) {
    MemState& m = g_mem;
    if (!m.haveSel) {
        ui::hint("Click a cell to select it; double-click or Enter edits it (hex). Arrow keys move.");
        return;
    }
    std::vector<uint8_t> b;
    const int ri = regionIndexAt(regs, m.sel);
    std::string rname = ri >= 0 && regs[(size_t)ri].name ? regs[(size_t)ri].name : "?";
    if (s.read(m.sel, b, 4)) {
        ImGui::TextWrapped("$%06X (%s)   u8 $%02X = %u   u16 $%04X = %u   u32 $%08X = %u", m.sel, rname.c_str(), b[0],
                           (unsigned)b[0], decodeBE(b.data(), 2), decodeBE(b.data(), 2), decodeBE(b.data(), 4),
                           decodeBE(b.data(), 4));
        const unsigned ptr = pointerTarget(decodeBE(b.data(), 4));
        const int pi = regionIndexAt(regs, ptr);
        ui::hint("As a pointer: $%06X%s%s", ptr, pi >= 0 ? "  in " : "  (not mapped)",
                 pi >= 0 && regs[(size_t)pi].name ? regs[(size_t)pi].name : "");
    } else {
        ImGui::Text("$%06X (%s)  unreadable", m.sel, rname.c_str());
    }

    // Add as cheat: the cheat system patches main RAM only.
    const bool inDram = m.sel + m.width <= kDramSize;
    const bool canCheat = cheats && cheats->active() && !cheats->blockedNow() && inDram && b.size() >= m.width;
    ImGui::BeginDisabled(!canCheat);
    if (ImGui::Button("Add as cheat")) {
        std::string code = memoryCheatToCode(m.sel, b.data(), m.width), err;
        char desc[48];
        std::snprintf(desc, sizeof desc, "Memory $%06X", m.sel);
        if (cheats->addUser(desc, code, err)) setStatus("Added cheat " + code + " (Cheats window: Manual codes)", false);
        else setStatus("Could not add the cheat: " + err, true);
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        if (canCheat) ImGui::SetTooltip("Holds the current value of this cell: %s", memoryCheatToCode(m.sel, b.data(), m.width).c_str());
        else if (!cheats) ImGui::SetTooltip("Cheats are not available");
        else if (cheats->blockedNow()) ImGui::SetTooltip("Cheats are off in hardcore mode");
        else if (!inDram) ImGui::SetTooltip("Cheats patch main RAM ($000000-$1FFFFF) only");
        else ImGui::SetTooltip("Load a game first");
    }
    ImGui::SameLine();
    if (ImGui::Button("Edit")) beginEdit(s, m.sel, m.width);
}

}  // namespace

bool& memoryWindowOpen() { return g_mem.open; }
bool& clutWindowOpen() { return g_clutOpen; }

void showMemoryAt(DebugSession& s, unsigned addr) {
    syncCore(s);
    g_mem.open = true;
    const auto& regs = s.regions();
    int idx = regionIndexAt(regs, addr);
    if (idx >= 0) selectAddr(s, addr, idx, true);
}

void drawMemoryWindow(DebugSession& s, CheatManager* cheats) {
    MemState& m = g_mem;
    if (!m.open) return;
    ui::nextWindow(52.0f, 36.0f, 28.0f, 18.0f);
    bool open = true;
    const bool shown = ImGui::Begin("Memory", &open);
    ui::keepOnScreen();
    if (!open) m.open = false;
    if (!shown) {
        ImGui::End();
        return;
    }
    if (!s.enabled()) {
        ui::coloredWrapped(warnCol(), s.disabledReason().c_str());
        ImGui::End();
        return;
    }
    const auto& regs = s.regions();
    if (regs.empty()) {
        ImGui::TextWrapped("The core reported no memory regions.");
        ImGui::End();
        return;
    }
    syncCore(s);
    if (m.region < 0 || m.region >= (int)regs.size()) m.region = 0;
    drawControls(s, regs);
    pollSearch(s, regs[(size_t)m.region]);
    const float footerH = ImGui::GetTextLineHeightWithSpacing() * 3.0f + ImGui::GetFrameHeightWithSpacing() + 4.0f;
    drawGrid(s, regs[(size_t)m.region], footerH);
    drawFooter(s, regs, cheats);
    ImGui::End();
}

void drawMemoryTools(DebugSession& s, CheatManager* cheats) {
    drawMemoryWindow(s, cheats);
    drawClutWindow(s);
}

void registerMemoryTools(DebugSession& s) {
    DebugTool mem;
    mem.id = "memory";
    mem.label = "Memory viewer...";
    mem.isOpen = [] { return g_mem.open; };
    mem.toggle = [] { g_mem.open = !g_mem.open; };
    s.registerTool(mem);
    DebugTool clut;
    clut.id = "clut";
    clut.label = "Palette (CLUT)...";
    clut.isOpen = [] { return g_clutOpen; };
    clut.toggle = [] { g_clutOpen = !g_clutOpen; };
    s.registerTool(clut);
    s.onForcedClose.push_back([](const char*) {
        g_mem.open = false;
        g_mem.editing = false;
        g_mem.job.reset();
        g_clutOpen = false;
    });
}

}  // namespace vj
