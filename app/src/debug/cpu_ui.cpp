#include "cpu_ui.h"

#include "cpu_view.h"
#include "debug_session.h"
#include "ui_util.h"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace vj {

using namespace cpuview;

namespace {

const ImVec4 kOrange(1.0f, 0.55f, 0.35f, 1.0f);
const ImVec4 kYellow(1.0f, 0.85f, 0.25f, 1.0f);
const ImVec4 kGreen(0.45f, 0.9f, 0.5f, 1.0f);
const ImVec4 kRed(1.0f, 0.4f, 0.4f, 1.0f);

struct TabState {
    unsigned top = 0;                 // first instruction shown
    bool topValid = false;
    bool follow = true;               // keep the PC in view
    bool haveCursor = false;
    unsigned cursor = 0;              // the selected row: Run to cursor, the context menu
    unsigned lastPc = ~0u;
    float wheel = 0;
    char gotoBuf[16] = "";
    std::vector<Line> rows;
    uint64_t rowsSerial = 0;
    unsigned rowsTop = ~0u;
    int rowsN = 0;
    const void* rowsIface = nullptr;
    vj_debug_regs regs, prev;         // the current registers, and what they were at the previous halt
    bool haveRegs = false, havePrev = false;
    uint64_t regsSerial = 0;
    TabState() {
        std::memset(&regs, 0, sizeof regs);
        std::memset(&prev, 0, sizeof prev);
    }
};

struct CpuState {
    bool open = false;
    int tab = VJ_DBG_TARGET_M68K;     // the tab being looked at
    int wantTab = -1;                 // select this tab next frame (a halt on that processor)
    TabState t[VJ_DBG_NUM_TARGETS];
    std::unique_ptr<CpuControl> ctl;
    std::string status;
    bool statusErr = false;
    bool hooked = false;
};
CpuState g;

void setStatus(const std::string& m, bool err) {
    g.status = m;
    g.statusErr = err;
}

unsigned pcOf(int target, const TabState& t) {
    return target == VJ_DBG_TARGET_M68K ? t.regs.pc : t.regs.risc_pc;
}

void refreshRegs(DebugSession& s, int target, TabState& t) {
    // Halted: the machine cannot change under us, but a register edit or a poke elsewhere can -- reading is
    // cheap, so read every frame.  Running: at the session's 10 Hz cadence.
    if (t.haveRegs && !s.halted() && t.regsSerial == s.refreshSerial()) return;
    vj_debug_regs r;
    std::memset(&r, 0, sizeof r);
    const vj_debug_iface* i = s.iface();
    t.haveRegs = i && i->get_regs && i->get_regs(target, &r) != 0;
    if (t.haveRegs) t.regs = r;
    t.regsSerial = s.refreshSerial();
}

// ---- registers ----------------------------------------------------------------------------------------------

// A hex field: editable while halted, a read-only box otherwise.  Returns true (and the value) on Enter.
bool hexField(const char* id, unsigned v, int digits, bool editable, bool changed, unsigned& out) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "%0*X", digits, v);
    ImGui::SetNextItemWidth(ImGui::CalcTextSize("0").x * (float)digits + ImGui::GetStyle().FramePadding.x * 2.0f + 2.0f);
    if (changed) ImGui::PushStyleColor(ImGuiCol_Text, kYellow);
    ImGuiInputTextFlags f = ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_CharsUppercase |
                            ImGuiInputTextFlags_AutoSelectAll;
    if (editable) f |= ImGuiInputTextFlags_EnterReturnsTrue;
    else f |= ImGuiInputTextFlags_ReadOnly;
    bool done = ImGui::InputText(id, buf, sizeof buf, f);
    if (changed) ImGui::PopStyleColor();
    if (!done || !editable) return false;
    return parseHex(buf, out);
}

void setReg(DebugSession& s, int target, int reg, unsigned v, const char* name) {
    const vj_debug_iface* i = s.iface();
    if (!s.halted() || !i || !i->set_reg) return;
    if (i->set_reg(target, reg, v)) {
        s.requestRefresh();
        setStatus(std::string(name) + " set", false);
    } else {
        setStatus(std::string("The core refused to write ") + name, true);
    }
}

// label + field on one line; commits through set_reg
void regLine(DebugSession& s, int target, int reg, const char* name, unsigned v, unsigned prev, bool havePrev, int digits,
             const char* idSuffix = "") {
    const bool editable = s.halted();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(name);
    ImGui::SameLine();
    char id[48];
    std::snprintf(id, sizeof id, "##%s%d%s", name, reg, idSuffix);
    unsigned nv = 0;
    if (hexField(id, v, digits, editable, havePrev && s.halted() && v != prev, nv)) setReg(s, target, reg, nv, name);
}

void flagChecks(DebugSession& s, int target, int reg, const char* regName, unsigned value, const std::vector<Flag>& flags) {
    const bool editable = s.halted();
    ui::Flow flow;
    bool first = true;
    for (const Flag& f : flags) {
        float w = ui::Flow::checkW(f.name);
        if (!first) flow.next(w);
        first = false;
        bool on = f.set;
        ImGui::BeginDisabled(!editable);
        char id[32];
        std::snprintf(id, sizeof id, "%s##f%s", f.name, regName);
        if (ImGui::Checkbox(id, &on)) setReg(s, target, reg, value ^ f.mask, regName);
        ImGui::EndDisabled();
    }
}

void draw68kRegs(DebugSession& s, TabState& t) {
    const vj_debug_regs& r = t.regs;
    const vj_debug_regs& p = t.prev;
    const bool hp = t.havePrev;
    if (ImGui::BeginTable("##r68", 4, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoSavedSettings)) {
        for (int i = 0; i < 8; i++) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::PushID(i);
            ImGui::AlignTextToFramePadding();
            ImGui::Text("D%d", i);
            ImGui::TableSetColumnIndex(1);
            unsigned nv = 0;
            if (hexField("##d", r.d[i], 8, s.halted(), hp && s.halted() && r.d[i] != p.d[i], nv))
                setReg(s, VJ_DBG_TARGET_M68K, VJ_DBG_REG_68K_D0 + i, nv, reg68kName(i));
            ImGui::TableSetColumnIndex(2);
            ImGui::AlignTextToFramePadding();
            ImGui::Text("A%d", i);
            ImGui::TableSetColumnIndex(3);
            if (hexField("##a", r.a[i], 8, s.halted(), hp && s.halted() && r.a[i] != p.a[i], nv))
                setReg(s, VJ_DBG_TARGET_M68K, VJ_DBG_REG_68K_A0 + i, nv, reg68kName(8 + i));
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::Separator();
    regLine(s, VJ_DBG_TARGET_M68K, VJ_DBG_REG_68K_PC, "PC ", r.pc, p.pc, hp, 8);
    ImGui::SameLine();
    regLine(s, VJ_DBG_TARGET_M68K, VJ_DBG_REG_68K_SR, "SR ", r.sr, p.sr, hp, 4);
    flagChecks(s, VJ_DBG_TARGET_M68K, VJ_DBG_REG_68K_SR, "SR", r.sr, decodeSr(r.sr));
    ImGui::AlignTextToFramePadding();
    ImGui::Text("%s mode, interrupt mask %u", srSupervisor(r.sr) ? "Supervisor" : "User", srIpl(r.sr));
    regLine(s, VJ_DBG_TARGET_M68K, VJ_DBG_REG_68K_USP, "USP", r.usp, p.usp, hp, 8);
    ImGui::SameLine();
    regLine(s, VJ_DBG_TARGET_M68K, VJ_DBG_REG_68K_SSP, "SSP", r.ssp, p.ssp, hp, 8);
    ui::hint("A7 is %s.", srSupervisor(r.sr) ? "SSP" : "USP");
}

void drawRiscRegs(DebugSession& s, int target, TabState& t) {
    const bool dsp = target == VJ_DBG_TARGET_DSP;
    const vj_debug_regs& r = t.regs;
    const vj_debug_regs& p = t.prev;
    const bool hp = t.havePrev;
    const RiscFlags fl = decodeRiscFlags(r.flags, dsp);
    const RiscCtrl ct = decodeRiscCtrl(r.ctrl, dsp);

    regLine(s, target, VJ_DBG_REG_RISC_PC, "PC   ", r.risc_pc, p.risc_pc, hp, 8);
    regLine(s, target, VJ_DBG_REG_RISC_FLAGS, "FLAGS", r.flags, p.flags, hp, 8);
    std::vector<Flag> flags = {{"Z", fl.zero, 1u}, {"C", fl.carry, 2u}, {"N", fl.nega, 4u}, {"IMASK", fl.imask, 8u}, {"REGPAGE", fl.regpage, 0x4000u}};
    flagChecks(s, target, VJ_DBG_REG_RISC_FLAGS, "FLAGS", r.flags, flags);
    const unsigned cur = riscActiveBank(r.flags);
    ImGui::TextWrapped("Interrupt enables: %s.  Register bank in use: %u%s.", intList(fl.intEnable, dsp).c_str(), cur,
                       fl.imask ? " (IMASK forces bank 0)" : "");
    if (fl.dmaen) ui::coloredWrapped(kRed, "DMAEN is set: LOAD/STORE at DMA priority (JTRM warns against it).");
    ImGui::Separator();
    ImGui::TextUnformatted(dsp ? "D_CTRL" : "G_CTRL");
    ImGui::SameLine();
    ImGui::TextColored(ct.go ? kGreen : kRed, "%s", ct.go ? (dsp ? "DSPGO: running" : "GPUGO: running") : "stopped");
    ImGui::TextWrapped("$%08X  %s%s%sinterrupt latches: %s", r.ctrl, ct.singleStep ? "SINGLE_STEP " : "",
                       ct.singleGo ? "SINGLE_GO " : "", ct.busHog ? "BUS_HOG " : "", intList(ct.latch, dsp).c_str());
    ImGui::Separator();

    if (ImGui::BeginTable("##rrisc", 3, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoSavedSettings)) {
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("bank 0", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("bank 1", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
        ImGui::TableSetColumnIndex(0);
        ImGui::TableHeader("");
        for (unsigned b = 0; b < 2; b++) {
            ImGui::TableSetColumnIndex((int)b + 1);
            char h[24];
            std::snprintf(h, sizeof h, b == cur ? "bank %u *" : "bank %u", b);
            if (b == cur) ImGui::PushStyleColor(ImGuiCol_Text, kGreen);
            ImGui::TableHeader(h);
            if (b == cur) ImGui::PopStyleColor();
        }
        for (int i = 0; i < 32; i++) {
            ImGui::TableNextRow();
            ImGui::PushID(i);
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::Text("r%d", i);
            for (unsigned b = 0; b < 2; b++) {
                ImGui::TableSetColumnIndex((int)b + 1);
                if (b == cur) ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, IM_COL32(40, 90, 50, 90));
                const unsigned v = b ? r.bank1[i] : r.bank0[i];
                const unsigned pv = b ? p.bank1[i] : p.bank0[i];
                ImGui::PushID((int)b);
                unsigned nv = 0;
                if (hexField("##v", v, 8, s.halted(), hp && s.halted() && v != pv, nv)) {
                    char nm[16];
                    std::snprintf(nm, sizeof nm, "r%d (bank %u)", i, b);
                    setReg(s, target, regRiscBank((int)b, i), nv, nm);
                }
                ImGui::PopID();
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

// ---- disassembly ---------------------------------------------------------------------------------------------

struct BpInfo {
    bool set = false, enabled = false;
};

BpInfo bpAt(DebugSession& s, int target, unsigned addr) {
    BpInfo b;
    for (const DebugBreakpoint& x : s.breakpoints())
        if (x.target == target && x.addr == addr) {
            b.set = true;
            b.enabled = x.enabled;
        }
    return b;
}

void toggleBp(DebugSession& s, int target, unsigned addr) {
    bool had = s.hasBreakpoint(target, addr);
    bool now = s.toggleBreakpoint(target, addr);
    if (!had && !now) setStatus("Could not set the breakpoint (the core's table is full, or the debugger is not attached)", true);
    else setStatus(now ? "Breakpoint set" : "Breakpoint removed", false);
}

void gotoAddr(TabState& t, unsigned a) {
    a &= 0x00FFFFFEu;
    t.top = a;
    t.topValid = true;
    t.follow = false;
    t.cursor = a;
    t.haveCursor = true;
}

void drawDisasm(DebugSession& s, int target, TabState& t, float height) {
    ImGui::BeginChild("##dis", ImVec2(0, height), ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    const vj_debug_iface* iface = s.iface();
    const float pitch = ImGui::GetTextLineHeight() + ImGui::GetStyle().ItemSpacing.y;
    const float cw = ImGui::CalcTextSize("0").x;
    const float width = ImGui::GetContentRegionAvail().x;
    int n = (int)std::floor(ImGui::GetContentRegionAvail().y / pitch);
    n = std::max(n, 4);
    const bool havePc = t.haveRegs;
    const unsigned pc = havePc ? pcOf(target, t) : 0;

    // Mouse wheel scrolls by instructions (variable-length code: walk, do not index).
    ImGuiIO& io = ImGui::GetIO();
    if (ImGui::IsWindowHovered() && io.MouseWheel != 0.0f) {
        t.wheel += io.MouseWheel * 3.0f;
        int whole = (int)t.wheel;
        t.wheel -= (float)whole;
        if (whole != 0 && t.topValid) {
            t.follow = false;
            for (; whole > 0; whole--) t.top = prevAddr(iface, target, t.top);
            for (; whole < 0; whole++) t.top = nextAddr(iface, target, t.top);
        }
    }
    if (!t.topValid) {
        t.top = havePc ? backStart(iface, target, pc, 4) : 0;
        t.topValid = true;
    }

    auto fill = [&] {
        if (t.rowsSerial != s.refreshSerial() || t.rowsTop != t.top || t.rowsN != n || t.rowsIface != iface || s.halted()) {
            t.rows = disasmForward(iface, target, t.top, n);
            t.rowsSerial = s.refreshSerial();
            t.rowsTop = t.top;
            t.rowsN = n;
            t.rowsIface = iface;
        }
    };
    fill();
    if (t.follow && havePc && pc != ~0u) {
        bool visible = false;
        for (int i = 0; i < n - 1 && i < (int)t.rows.size(); i++)
            if (t.rows[(size_t)i].addr == pc) visible = true;
        if (!visible || (pc != t.lastPc && t.rows.size() && pc < t.rows[0].addr)) {
            t.top = backStart(iface, target, pc, std::min(4, n / 4));
            fill();
        }
    }
    t.lastPc = pc;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 colText = ImGui::GetColorU32(ImGuiCol_Text);
    const ImU32 colDim = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    const ImU32 colPcBg = IM_COL32(220, 170, 30, 70);
    const float gutter = pitch * 1.3f;
    const float xAddr = gutter;
    const float xBytes = xAddr + cw * 8.5f;
    const float xText = xBytes + cw * 17.0f;
    const bool temp = g.ctl && g.ctl->temp().active() && g.ctl->temp().target() == target;

    for (int i = 0; i < (int)t.rows.size(); i++) {
        const Line& L = t.rows[(size_t)i];
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const bool isPc = havePc && L.addr == pc;
        const bool sel = t.haveCursor && t.cursor == L.addr;
        const BpInfo bp = bpAt(s, target, L.addr);
        if (isPc) dl->AddRectFilled(p, ImVec2(p.x + width, p.y + pitch - 1.0f), colPcBg);
        ImGui::PushID(i);
        ImGui::Selectable("##row", sel, ImGuiSelectableFlags_None, ImVec2(width, pitch - ImGui::GetStyle().ItemSpacing.y));
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
            if (io.MousePos.x < p.x + gutter) toggleBp(s, target, L.addr);
            else {
                t.cursor = L.addr;
                t.haveCursor = true;
            }
        }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            t.cursor = L.addr;
            t.haveCursor = true;
        }
        if (ImGui::BeginPopupContextItem("##ctx")) {
            ImGui::Text("$%06X", L.addr);
            ImGui::Separator();
            if (ImGui::MenuItem(bp.set ? "Remove breakpoint" : "Set breakpoint")) toggleBp(s, target, L.addr);
            if (ImGui::MenuItem("Run to here", nullptr, false, s.halted())) g.ctl->runTo(target, L.addr);
            if (ImGui::MenuItem("Set PC here", nullptr, false, s.halted() && !isPc))
                setReg(s, target, target == VJ_DBG_TARGET_M68K ? VJ_DBG_REG_68K_PC : VJ_DBG_REG_RISC_PC, L.addr, "PC");
            if (ImGui::MenuItem("Start the listing here")) gotoAddr(t, L.addr);
            if (ImGui::MenuItem("Copy address")) {
                char b[16];
                std::snprintf(b, sizeof b, "%06X", L.addr);
                ImGui::SetClipboardText(b);
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();

        const float cy = p.y + (pitch - ImGui::GetStyle().ItemSpacing.y) * 0.5f;
        const float r = pitch * 0.32f;
        if (bp.set) {
            if (bp.enabled) dl->AddCircleFilled(ImVec2(p.x + gutter * 0.5f, cy), r, IM_COL32(230, 60, 60, 255));
            else dl->AddCircle(ImVec2(p.x + gutter * 0.5f, cy), r, IM_COL32(230, 60, 60, 255), 12, 1.5f);
        } else if (temp && g.ctl->temp().addr() == L.addr) {
            dl->AddRectFilled(ImVec2(p.x + gutter * 0.5f - r, cy - r), ImVec2(p.x + gutter * 0.5f + r, cy + r),
                              IM_COL32(255, 150, 60, 255));
        } else if (isPc) {
            const float x = p.x + gutter * 0.5f;
            dl->AddTriangleFilled(ImVec2(x - r, cy - r), ImVec2(x - r, cy + r), ImVec2(x + r, cy), IM_COL32(255, 215, 60, 255));
        }
        char a[16];
        std::snprintf(a, sizeof a, "$%06X", L.addr);
        dl->AddText(ImVec2(p.x + xAddr, p.y), isPc ? colText : colDim, a);
        std::string bytes = L.bytes;
        if (bytes.size() > 14) bytes = bytes.substr(0, 14) + "..";
        dl->AddText(ImVec2(p.x + xBytes, p.y), colDim, bytes.c_str());
        dl->AddText(ImVec2(p.x + xText, p.y), L.valid ? colText : colDim, L.text.c_str());
    }
    ImGui::EndChild();
}

// ---- breakpoint list --------------------------------------------------------------------------------------------

void drawBreakpoints(DebugSession& s, float height) {
    const auto& bps = s.breakpoints();
    ImGui::Text("Breakpoints (%d)", (int)bps.size());
    ImGui::SameLine();
    const int tab = g.tab;
    TabState& t = g.t[tab];
    ImGui::BeginDisabled(!t.haveCursor);
    if (ImGui::SmallButton("Add at cursor") && t.haveCursor) toggleBp(s, tab, t.cursor);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(bps.empty());
    if (ImGui::SmallButton("Remove all")) s.clearBreakpoints();
    ImGui::EndDisabled();
    ImGui::BeginChild("##bps", ImVec2(0, height), ImGuiChildFlags_Borders);
    if (bps.empty()) ui::hint("Click the margin beside an instruction, or right-click it.");
    // The list can change under the loop (remove): walk a copy.
    std::vector<DebugBreakpoint> copy = bps;
    for (size_t i = 0; i < copy.size(); i++) {
        const DebugBreakpoint& b = copy[i];
        ImGui::PushID((int)i);
        bool en = b.enabled;
        if (ImGui::Checkbox("##en", &en)) s.setBreakpointEnabled(b.target, b.addr, en);
        ImGui::SameLine();
        char lbl[48];
        std::snprintf(lbl, sizeof lbl, "%s  $%06X", targetName(b.target), b.addr);
        if (ImGui::Selectable(lbl, false, ImGuiSelectableFlags_None, ImVec2(ui::em(12), 0))) {
            g.wantTab = b.target;
            gotoAddr(g.t[b.target], b.addr);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Show in the listing");
        ImGui::SameLine();
        if (ImGui::SmallButton("Remove")) s.removeBreakpoint(b.target, b.addr);
        ImGui::PopID();
    }
    ImGui::EndChild();
}

// ---- toolbar -------------------------------------------------------------------------------------------------------

bool button(const char* label, bool enabled, const char* tip) {
    ImGui::BeginDisabled(!enabled);
    bool c = ImGui::Button(label);
    ImGui::EndDisabled();
    if (tip && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ui::beginWrappedTooltip(), ImGui::TextUnformatted(tip), ui::endWrappedTooltip();
    return c;
}

void drawBanner(DebugSession& s) {
    if (s.halted()) {
        const int ht = s.haltedTarget();
        ImGui::TextColored(kOrange, "HALTED");
        ImGui::SameLine();
        ImGui::Text("%s at $%06X", targetName(ht), s.haltedPc());
        const char* why = stopReasonName(s.haltReason());
        if (*why) {
            ImGui::SameLine();
            ImGui::TextDisabled("(%s)", why);
        }
        if (g.tab != ht) {
            ImGui::SameLine();
            ImGui::TextDisabled("- looking at the %s", targetName(g.tab));
        }
        return;
    }
    ImGui::TextColored(kGreen, "Running");
    ImGui::SameLine();
    ui::hint("Registers refresh 10 times a second.  Break (F9) to inspect and edit.");
    for (int i = 0; i < VJ_DBG_NUM_TARGETS; i++)
        if (g.ctl->pending(i)) {
            const TabState& t = g.t[i];
            const bool stopped = i != VJ_DBG_TARGET_M68K && t.haveRegs && !(t.regs.ctrl & 1u);
            ui::coloredWrapped(kYellow, stopped ? (std::string("Break armed on the ") + targetName(i) +
                                                    ": it is stopped, the halt happens when the game starts it.").c_str()
                                                : (std::string("Break armed on the ") + targetName(i) + ".").c_str());
        }
}

void drawToolbar(DebugSession& s) {
    const int tab = g.tab;
    const bool halted = s.halted();
    const bool other = halted && s.haltedTarget() != tab;
    ui::Flow flow;
    if (button("Break  F9", !halted, "Stop the machine at the next instruction of the processor on this tab.")) g.ctl->breakIn(tab);
    flow.next(ui::Flow::buttonW("Continue  F9"));
    if (button("Continue  F9", halted, "Resume everything.")) g.ctl->cont();
    flow.next(ui::Flow::buttonW("Step  F10"));
    const std::string stepTip = other ? std::string("The ") + targetName(s.haltedTarget()) + " is the one that is halted.  This resumes the machine and halts again at the " +
                                            targetName(tab) + "'s next instruction."
                                      : std::string("Execute one instruction.");
    if (button("Step  F10", halted, stepTip.c_str())) g.ctl->stepIn(tab);
    flow.next(ui::Flow::buttonW("Step over"));
    const char* overTip = tab == VJ_DBG_TARGET_M68K
                              ? "Like Step, but a JSR, BSR or TRAP runs to the instruction after it (a temporary breakpoint; a breakpoint inside the call still stops)."
                              : "The GPU and DSP have no call instruction (a call is MOVE PC,Rn + JUMP, and JUMP/JR run the next instruction as a delay slot), so this is the same as Step.";
    if (button("Step over", halted, overTip)) g.ctl->stepOver(tab);
    flow.next(ui::Flow::buttonW("Run to cursor"));
    const TabState& t = g.t[tab];
    if (button("Run to cursor", halted && t.haveCursor, "Resume until the selected line is reached (click a line first).") && t.haveCursor)
        g.ctl->runTo(tab, t.cursor);
}

void drawNav(DebugSession& s) {
    const int tab = g.tab;
    TabState& t = g.t[tab];
    ImGui::Checkbox("Follow PC", &t.follow);
    if (ImGui::IsItemHovered()) ui::beginWrappedTooltip(), ImGui::TextUnformatted("Keep the instruction about to run in view.  Scrolling or Go to turns it off."), ui::endWrappedTooltip();
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Go to");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ui::fitWidth(0.3f, 8.0f, 12.0f));
    if (ImGui::InputTextWithHint("##goto", "$address", t.gotoBuf, sizeof t.gotoBuf,
                                 ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_CharsUppercase |
                                     ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) {
        unsigned a = 0;
        if (parseHex(t.gotoBuf, a, true)) {
            gotoAddr(t, a);
            setStatus("", false);
        } else {
            setStatus("Not an address", true);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("PC")) {
        t.follow = true;
        t.lastPc = ~0u;
        t.topValid = false;
    }
    if (ImGui::IsItemHovered()) ui::beginWrappedTooltip(), ImGui::TextUnformatted("Back to the program counter, and follow it."), ui::endWrappedTooltip();
    (void)s;
}

void drawWindow(DebugSession& s) {
    ui::nextWindow(74, 46, 54, 30);
    if (!ImGui::Begin("CPU debugger", &g.open)) {
        ui::keepOnScreen();
        ImGui::End();
        return;
    }
    ui::keepOnScreen();
    if (!s.enabled()) {
        ui::hint("%s", s.disabledReason().c_str());
        ImGui::End();
        return;
    }
    if (!g.ctl) g.ctl = std::make_unique<CpuControl>(s);

    drawBanner(s);
    drawToolbar(s);
    drawNav(s);
    if (!g.ctl->note().empty()) ui::coloredWrapped(kYellow, g.ctl->note().c_str());

    if (ImGui::BeginTabBar("##cputabs")) {
        static const char* const labels[VJ_DBG_NUM_TARGETS] = {"68000###t0", "GPU###t1", "DSP###t2"};
        static const char* const haltedLabels[VJ_DBG_NUM_TARGETS] = {"68000 *###t0", "GPU *###t1", "DSP *###t2"};
        for (int i = 0; i < VJ_DBG_NUM_TARGETS; i++) {
            ImGuiTabItemFlags f = ImGuiTabItemFlags_None;
            if (g.wantTab == i) f |= ImGuiTabItemFlags_SetSelected;
            const bool here = s.halted() && s.haltedTarget() == i;
            if (ImGui::BeginTabItem(here ? haltedLabels[i] : labels[i], nullptr, f)) {
                g.tab = i;
                ImGui::EndTabItem();
            }
        }
        g.wantTab = -1;
        ImGui::EndTabBar();
    }

    const int tab = g.tab;
    TabState& t = g.t[tab];
    refreshRegs(s, tab, t);
    if (!t.haveRegs) {
        ui::hint("The core did not return the %s registers.", targetName(tab));
        ImGui::End();
        return;
    }

    const float bpH = ImGui::GetTextLineHeightWithSpacing() * 5.0f + ImGui::GetStyle().WindowPadding.y * 2.0f;
    const float footer = ImGui::GetTextLineHeightWithSpacing() * 1.4f;
    if (ImGui::BeginTable("##cpubody", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_NoSavedSettings)) {
        ImGui::TableSetupColumn("regs", ImGuiTableColumnFlags_WidthFixed, ui::em(tab == VJ_DBG_TARGET_M68K ? 26.0f : 25.0f));
        ImGui::TableSetupColumn("code", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        const float bodyH = std::max(ui::em(10), ImGui::GetContentRegionAvail().y - footer);
        ImGui::BeginChild("##regs", ImVec2(0, bodyH));
        if (tab == VJ_DBG_TARGET_M68K) draw68kRegs(s, t);
        else drawRiscRegs(s, tab, t);
        ImGui::EndChild();
        ImGui::TableSetColumnIndex(1);
        ImGui::BeginChild("##code", ImVec2(0, bodyH));
        drawDisasm(s, tab, t, std::max(ui::em(8), bodyH - bpH - ImGui::GetTextLineHeightWithSpacing() * 2.0f));
        drawBreakpoints(s, bpH);
        ImGui::EndChild();
        ImGui::EndTable();
    }
    if (!g.status.empty()) {
        if (g.statusErr) ui::coloredWrapped(kRed, g.status.c_str());
        else ui::hint("%s", g.status.c_str());
    } else if (!s.halted()) {
        ui::hint("Edits and stepping are available while the machine is halted.");
    }
    ImGui::End();
}

}  // namespace

bool& cpuWindowOpen() { return g.open; }

void showCpuAt(DebugSession&, int target, unsigned addr) {
    if (target < 0 || target >= VJ_DBG_NUM_TARGETS) return;
    g.open = true;
    g.wantTab = target;
    g.tab = target;
    gotoAddr(g.t[target], addr);
}

void registerCpuTools(DebugSession& s) {
    DebugTool cpu;
    cpu.id = "cpu";
    cpu.label = "CPU debugger...";
    cpu.shortcut = "F9 / F10";
    cpu.isOpen = [] { return g.open; };
    cpu.toggle = [] { g.open = !g.open; };
    s.registerTool(cpu);
    if (g.hooked) return;   // one session per process; the hooks are added once
    g.hooked = true;
    g.ctl = std::make_unique<CpuControl>(s);
    s.onHalt.push_back([&s](int target, unsigned pc, int reason) {
        if (!g.ctl) g.ctl = std::make_unique<CpuControl>(s);
        g.ctl->onHalt(target, pc, reason);
        for (TabState& t : g.t) {
            t.prev = t.regs;
            t.havePrev = t.haveRegs;
            t.regsSerial = 0;
        }
        if (target < 0 || target >= VJ_DBG_NUM_TARGETS) return;
        g.wantTab = target;
        g.tab = target;
        if (reason != VJ_DBG_STOP_STEP) {
            g.open = true;          // a breakpoint or a Break: bring the window up
            g.t[target].follow = true;
        }
        g.t[target].lastPc = ~0u;
        setStatus("", false);
    });
    s.onForcedClose.push_back([](const char*) {
        g.open = false;
        if (g.ctl) g.ctl->forget();
        for (TabState& t : g.t) {
            t.haveRegs = t.havePrev = false;
            t.rows.clear();
            t.topValid = false;
        }
    });
}

namespace {

// Developer aid (not a user feature): VJ_CPU_DEV=break[:gpu|:dsp][:bp=HEX[,HEX]] opens the window about 90 UI
// frames after a game starts and breaks into it, optionally with breakpoints set -- so a screenshot of the
// halted window can be taken from a windowed run without synthesising key presses.
void devHook(DebugSession& s) {
    static const char* const env = std::getenv("VJ_CPU_DEV");
    static int frames = 0;
    static bool done = false;
    if (!env || done || !s.enabled()) return;
    if (++frames < 90) return;
    done = true;
    int target = VJ_DBG_TARGET_M68K;
    if (std::strstr(env, ":gpu")) target = VJ_DBG_TARGET_GPU;
    else if (std::strstr(env, ":dsp")) target = VJ_DBG_TARGET_DSP;
    if (const char* bp = std::strstr(env, ":bp=")) {
        for (const char* p = bp + 4; *p;) {
            char* end = nullptr;
            unsigned long a = std::strtoul(p, &end, 16);
            if (end == p) break;
            s.addBreakpoint(target, (unsigned)a);
            p = *end == ',' ? end + 1 : end;
        }
    }
    g.open = true;
    g.wantTab = target;
    g.tab = target;
    if (!g.ctl) g.ctl = std::make_unique<CpuControl>(s);
    g.ctl->breakIn(target);
}

}  // namespace

void drawCpuTools(DebugSession& s) {
    devHook(s);
    if (!g.open) return;
    drawWindow(s);
}

}  // namespace vj
