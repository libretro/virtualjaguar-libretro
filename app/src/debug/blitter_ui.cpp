// The Blitter inspector window.  See blitter_ui.h.
#include "blitter_ui.h"

#include "blitter_log.h"
#include "debug_session.h"
#include "ui_util.h"

#include "imgui.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>
#include <vector>

namespace vj {

namespace {

struct State {
    DebugSession* session = nullptr;
    bool open = false;
    bool logOn = false;                     // we turned the core's log on
    const vj_debug_iface* logIface = nullptr;   // ... on this interface
    blit::LogTracker tracker;
    uint64_t serial = 0;
    bool frozen = false;
    char filter[40] = "";
    int64_t selected = -1;                  // launch seq
    std::vector<uint8_t> regImage;
    bool haveRegs = false;
};
State g;

// Switches the core's launch log on or off to match `want`.  A replaced interface (the core was torn down
// and reloaded) is forgotten, never called.
void setLog(bool want) {
    const vj_debug_iface* f = g.session ? g.session->iface() : nullptr;
    if (g.logOn && g.logIface != f) {
        g.logOn = false;
        g.logIface = nullptr;
        g.tracker.reset();
    }
    if (want && !g.logOn && f && f->blit_log_enable) {
        f->blit_log_enable(1);
        g.logOn = true;
        g.logIface = f;
        g.tracker.reset();
    } else if (!want && g.logOn) {
        if (g.logIface && g.logIface->blit_log_enable) g.logIface->blit_log_enable(0);
        g.logOn = false;
        g.logIface = nullptr;
    }
}

std::string lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

const char* chanBpp(const blit::Channel& c, char* buf, size_t n) {
    if (c.bpp) std::snprintf(buf, n, "%ubpp w%u", c.bpp, c.width);
    else std::snprintf(buf, n, "?bpp w%u", c.width);
    return buf;
}

void drawChannelDetail(const char* name, unsigned base, unsigned flags, unsigned pixel, unsigned step, bool isDst) {
    blit::Channel c = blit::decodeFlags(flags);
    ImGui::Text("%s (%s)  base $%06X  flags $%08X", name, isDst ? "destination" : "source", base & 0xFFFFFF, flags);
    ImGui::Indent();
    ImGui::TextWrapped("%s", blit::describeChannel(c).c_str());
    ImGui::Text("pixel (%d, %d)   step (%d, %d)", blit::pixelX(pixel), blit::pixelY(pixel), blit::pixelX(step),
                blit::pixelY(step));
    ImGui::Unindent();
}

void drawDetail(const vj_debug_blit& b) {
    blit::Row r = blit::decodeRow(b);
    ImGui::Text("Launch #%u, frame %u, %s blitter", b.seq, b.frame, blit::engineName(b.engine));
    ImGui::Text("B_CMD $%08X", b.cmd);
    ImGui::Indent();
    // each flag as a chip with its explanation as a tooltip
    size_t n = 0;
    const blit::CmdBit* bits = blit::cmdBits(&n);
    ui::Flow flow;
    bool first = true;
    for (size_t i = 0; i < n; i++) {
        if (!(b.cmd & bits[i].mask)) continue;
        const float w = ImGui::CalcTextSize(bits[i].name).x + ImGui::GetStyle().ItemSpacing.x;
        if (!first) flow.next(w);
        first = false;
        ImGui::TextUnformatted(bits[i].name);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", bits[i].tip);
    }
    if (first) ImGui::TextDisabled("(no control bits)");
    ImGui::Text("LFU %X = %s", r.lfu, blit::lfuName(r.lfu));
    if (r.zmode) ImGui::Text("ZMODE inhibit when %s", blit::zmodeName(r.zmode).c_str());
    ImGui::Unindent();
    drawChannelDetail("A1", b.a1_base, b.a1_flags, b.a1_pixel, b.a1_step, !r.dstA2);
    drawChannelDetail("A2", b.a2_base, b.a2_flags, b.a2_pixel, b.a2_step, r.dstA2);
    ImGui::Text("B_COUNT $%08X = %u pixels x %u lines", b.count, r.count.inner, r.count.outer);
}

void drawLiveRegs() {
    if (!ImGui::CollapsingHeader("Live registers")) return;
    if (!g.haveRegs) {
        ImGui::TextDisabled("Not readable right now.");
        return;
    }
    ui::hint("Read without side effects. B_CMD reads back as the status register, not the last command.");
    std::vector<blit::RegValue> v = blit::decodeRegisters(g.regImage.data());
    if (ImGui::BeginTable("##regs", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Register", ImGuiTableColumnFlags_WidthFixed, ui::em(8.0f));
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, ui::em(13.0f));
        ImGui::TableSetupColumn("Decoded");
        for (const blit::RegValue& rv : v) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("%s", rv.reg->name);
            ImGui::TableNextColumn();
            if (rv.reg->bytes == 8) ImGui::Text("%08X %08X", rv.hi, rv.lo);
            else ImGui::Text("$%08X", rv.lo);
            ImGui::TableNextColumn();
            const std::string n = rv.reg->name;
            if (n == "A1_FLAGS" || n == "A2_FLAGS") ImGui::TextWrapped("%s", blit::describeChannel(blit::decodeFlags(rv.lo)).c_str());
            else if (n == "A1_PIXEL" || n == "A2_PIXEL" || n == "A1_STEP" || n == "A2_STEP")
                ImGui::Text("(%d, %d)", blit::pixelX(rv.lo), blit::pixelY(rv.lo));
            else if (n == "B_CMD") ImGui::TextUnformatted(blit::statusText(rv.lo).c_str());
            else if (n == "B_COUNT") ImGui::Text("%u x %u", blit::decodeCount(rv.lo).inner, blit::decodeCount(rv.lo).outer);
        }
        ImGui::EndTable();
    }
}

void drawTable() {
    const auto& rows = g.tracker.rows();
    const std::string f = lower(g.filter);
    const float h = std::max(ui::em(10.0f), ImGui::GetContentRegionAvail().y * 0.5f);
    if (!ImGui::BeginTable("##blits", 7,
                           ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
                               ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp,
                           ImVec2(0, h)))
        return;
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, ui::em(3.5f));
    ImGui::TableSetupColumn("Frame", ImGuiTableColumnFlags_WidthFixed, ui::em(4.0f));
    ImGui::TableSetupColumn("Command", ImGuiTableColumnFlags_WidthStretch, 3.0f);
    ImGui::TableSetupColumn("A1", ImGuiTableColumnFlags_WidthStretch, 1.4f);
    ImGui::TableSetupColumn("A2", ImGuiTableColumnFlags_WidthStretch, 1.4f);
    ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, ui::em(6.0f));
    ImGui::TableSetupColumn("Eng", ImGuiTableColumnFlags_WidthFixed, ui::em(3.0f));
    ImGui::TableHeadersRow();
    for (size_t k = rows.size(); k-- > 0;) {   // newest first
        const vj_debug_blit& b = rows[k];
        const std::string summary = blit::cmdSummary(b.cmd);
        if (!f.empty() && lower(summary).find(f) == std::string::npos) continue;
        blit::Row r = blit::decodeRow(b);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        char id[24];
        std::snprintf(id, sizeof id, "%u##r%u", b.seq, b.seq);
        if (ImGui::Selectable(id, g.selected == (int64_t)b.seq, ImGuiSelectableFlags_SpanAllColumns)) g.selected = b.seq;
        ImGui::TableNextColumn();
        ImGui::Text("%u", b.frame);
        ImGui::TableNextColumn();
        ImGui::TextWrapped("%s", summary.c_str());
        char tmp[48];
        ImGui::TableNextColumn();
        ImGui::Text("%06X %s%s", b.a1_base & 0xFFFFFF, chanBpp(r.a1, tmp, sizeof tmp), r.dstA2 ? "" : " dst");
        ImGui::TableNextColumn();
        ImGui::Text("%06X %s%s", b.a2_base & 0xFFFFFF, chanBpp(r.a2, tmp, sizeof tmp), r.dstA2 ? " dst" : "");
        ImGui::TableNextColumn();
        ImGui::Text("%ux%u", r.count.inner, r.count.outer);
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(b.engine ? "acc" : "fast");
    }
    ImGui::EndTable();
}

}  // namespace

void blitterToolInit(DebugSession& session) {
    g.session = &session;
    DebugTool t;
    t.id = "blitter";
    t.label = "Blitter log...";
    t.isOpen = [] { return g.open; };
    t.toggle = [] { g.open = !g.open; };
    session.registerTool(t);
    session.onForcedClose.push_back([](const char*) {
        g.open = false;
        setLog(false);
    });
}

void blitterToolDraw() {
    if (!g.session) return;
    // The core's log runs only while the window is open and usable.
    const bool want = g.open && g.session->enabled();
    setLog(want);
    if (!g.open) return;

    ui::nextWindow(56.0f, 42.0f, 32.0f, 20.0f);
    bool shown = ImGui::Begin("Blitter log", &g.open);
    ui::keepOnScreen();
    if (shown) {
        ui::hint("The blitter launches the game made, newest first. The core keeps the last %d; launches that scroll "
                 "out before this list is refreshed are counted as missed.",
                 (int)VJ_DBG_BLIT_LOG_SIZE);
        if (!g.session->enabled()) {
            ui::coloredWrapped(ImVec4(1.0f, 0.8f, 0.4f, 1.0f), g.session->disabledReason().c_str());
        } else {
            const vj_debug_iface* f = g.session->iface();
            const uint64_t ser = g.session->refreshSerial();
            if (ser != g.serial) {
                g.serial = ser;
                if (!g.frozen) g.tracker.sync(f);
                g.haveRegs = g.session->read(blit::kRegBase, g.regImage, blit::kRegBytes);
            }
            ImGui::Checkbox("Freeze", &g.frozen);
            ImGui::SameLine();
            if (ImGui::Button("Clear")) {
                g.tracker.reset();
                g.selected = -1;
                // restart the core's numbering so the list and the core stay in step
                if (f && f->blit_log_enable) {
                    f->blit_log_enable(0);
                    f->blit_log_enable(1);
                }
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(ui::fitWidth(0.4f, 8.0f, 16.0f));
            ImGui::InputTextWithHint("##filter", "Filter flags (srcen, gourd...)", g.filter, sizeof g.filter);
            ImGui::TextDisabled("%zu shown  |  %u launches seen  |  %u missed", g.tracker.rows().size(), g.tracker.total(),
                                g.tracker.lost());
            drawTable();
            const vj_debug_blit* sel = nullptr;
            for (const vj_debug_blit& b : g.tracker.rows())
                if ((int64_t)b.seq == g.selected) sel = &b;
            ImGui::Spacing();
            if (sel) drawDetail(*sel);
            else ui::hint("Select a launch for its full decode.");
            ImGui::Spacing();
            drawLiveRegs();
        }
    }
    ImGui::End();
}

}  // namespace vj
