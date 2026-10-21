// The Palette (CLUT) window: the 256 x 16-bit colour look-up table at $F00400 (table A; table B at
// $F00600 is its mirror -- JTRM v8: "writing to either address range writes to both tables").
#include "debug_session.h"
#include "memory_ui.h"
#include "memory_view.h"
#include "ui_util.h"
#include "window_chrome.h"

#include "imgui.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace vj {

using namespace memview;

namespace {

struct ClutState {
    ColorMode mode = ColorMode::Cry;
    int sel = 0;
    bool haveSel = false;
    CryCodec cry;
    std::vector<unsigned> a, b;        // the 256 raw entries of table A and B
    uint64_t serial = 0;
    bool ok = false;
    bool seen = false;                 // a buffer has been adopted (before that the table is "being read", not unreadable)
    DebugViews::Id id = DebugViews::kNoId;   // the published view: both tables in one request
    float pick[3] = {0, 0, 0};          // the colour picker's own value while it is being dragged
    char rawBuf[8] = "";
    int rawFor = -1;
    unsigned rawShown = 0;
    bool rawActive = false;
    int pickFor = -1;
    unsigned pickRaw = 0;
    std::string status;
    bool statusErr = false;
    uint64_t epochSeen = ~0ull;        // DebugViews::epoch() last adopted
};
ClutState g_clut;

ImVec4 warnCol() { return ui::vec4(ui::pal().warning); }
ImVec4 errCol() { return ui::vec4(ui::pal().error); }

ImU32 rgbU32(uint32_t rgb) { return IM_COL32((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, 255); }

// Both tables are one published request (table B is table A + 0x200).
static_assert(kClutB == kClutA + kClutEntries * 2, "the CLUT tables are adjacent");
constexpr unsigned kClutBytes = 2 * kClutEntries * 2;

// A closed or disabled window holds no request; reopening adopts the next answer whatever its serial.
void releaseView(DebugSession& s) {
    ClutState& c = g_clut;
    if (c.id == DebugViews::kNoId) return;
    s.views().clear(c.id);
    c.id = DebugViews::kNoId;
    c.serial = 0;
    c.ok = c.seen = false;
}

void refresh(DebugSession& s) {
    ClutState& c = g_clut;
    if (c.id == DebugViews::kNoId) {
        ViewRequest q;
        q.kind = ViewRequest::Kind::Memory;
        q.addr = kClutA;
        q.len = kClutBytes;
        c.id = s.views().set(DebugViews::kNoId, q);
    }
    ViewBuffer b;
    if (!s.views().read(c.id, b) || b.serial == c.serial) return;
    c.serial = b.serial;
    c.seen = true;
    c.ok = b.ok && b.bytes.size() == kClutBytes;
    if (!c.ok) return;
    c.a.resize(kClutEntries);
    c.b.resize(kClutEntries);
    for (unsigned i = 0; i < kClutEntries; i++) {
        c.a[i] = decodeBE(&b.bytes[i * 2], 2);
        c.b[i] = decodeBE(&b.bytes[kClutEntries * 2 + i * 2], 2);
    }
}

// A write to either table lands in both (the core mirrors it), so the window shows it at once instead of waiting
// for the next published buffer; that buffer then carries the same value.
void showWritten(unsigned idx, unsigned raw) {
    ClutState& c = g_clut;
    if (c.ok && idx < kClutEntries) c.a[idx] = c.b[idx] = raw;
}

std::string describe(ColorMode m, unsigned raw) {
    char buf[96];
    if (m == ColorMode::Cry)
        std::snprintf(buf, sizeof buf, "CRY: cyan %u, red %u, intensity $%02X", (raw >> 12) & 0xF, (raw >> 8) & 0xF, raw & 0xFF);
    else
        std::snprintf(buf, sizeof buf, "RGB16: red %u, blue %u, green %u", (raw >> 11) & 0x1F, (raw >> 6) & 0x1F, raw & 0x3F);
    return buf;
}

}  // namespace

namespace clut_test {
bool entryA(unsigned index, unsigned& raw) {
    if (!g_clut.ok || index >= kClutEntries) return false;
    raw = g_clut.a[index];
    return true;
}
}  // namespace clut_test

void drawClutWindow(DebugSession& s) {
    ClutState& c = g_clut;
    if (!clutWindowOpen()) {
        releaseView(s);
        return;
    }
    ui::nextWindow(36.0f, 46.0f, 22.0f, 24.0f);
    bool open = true;
    const bool shown = ui::beginWindow("Palette (CLUT)", &open);
    ui::keepOnScreen();
    if (!open) clutWindowOpen() = false;
    if (!shown) {
        releaseView(s);
        ImGui::End();
        return;
    }
    if (!s.enabled()) {
        releaseView(s);
        ui::coloredWrapped(warnCol(), s.disabledReason().c_str());
        ImGui::End();
        return;
    }
    if (c.epochSeen != s.views().epoch()) {   // a new core / content: nothing remembered applies
        c.epochSeen = s.views().epoch();
        c.ok = c.seen = false;
        c.serial = 0;
        c.haveSel = false;
        c.status.clear();
    }
    refresh(s);
    s.wakeViews();   // the request is set: inline it is filled now, threaded one fill is posted
    if (!c.ok) {
        ImGui::TextWrapped(c.seen ? "The colour table could not be read." : "Reading the colour table...");
        ImGui::End();
        return;
    }
    // CRY needs the core's own colour table (once per core).
    const bool cryOk = c.cry.buildFrom(s);
    if (!cryOk && c.mode == ColorMode::Cry) c.mode = ColorMode::Rgb16;

    ImGui::TextUnformatted("Show entries as");
    ImGui::SameLine();
    ImGui::BeginDisabled(!cryOk);
    if (ImGui::RadioButton("CRY", c.mode == ColorMode::Cry)) c.mode = ColorMode::Cry;
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::RadioButton("RGB16", c.mode == ColorMode::Rgb16)) c.mode = ColorMode::Rgb16;
    ui::hint("Indexed (1-8 bpp) objects look their pixels up here; whether an entry is CRY or RGB16 depends on the "
             "video mode (VMODE) the game sets. 256 entries at $F00400.");

    bool allZero = true;
    for (int i = 0; i < 256 && allZero; i++) allZero = c.a[(size_t)i] == 0;
    if (allZero)
        ui::hint("Every entry is 0 (black): this game has not loaded a palette, or only uses 16-bit pixels.");

    // ---- the grid of swatches ----------------------------------------------------------------------------
    const float spacing = 2.0f;
    const float avail = ImGui::GetContentRegionAvail().x;
    const float sw = std::max(12.0f, std::min(30.0f, (avail - spacing * 15.0f) / 16.0f));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2((sw + spacing) * 16.0f, (sw + spacing) * 16.0f));
    int hover = -1;
    if (ImGui::IsItemHovered()) {
        const ImVec2 mp = ImGui::GetMousePos();
        const int cx = (int)((mp.x - origin.x) / (sw + spacing)), cy = (int)((mp.y - origin.y) / (sw + spacing));
        if (cx >= 0 && cx < 16 && cy >= 0 && cy < 16) hover = cy * 16 + cx;
    }
    for (int i = 0; i < 256; i++) {
        const float x = origin.x + (float)(i % 16) * (sw + spacing), y = origin.y + (float)(i / 16) * (sw + spacing);
        const uint32_t rgb = swatchColor(c.mode, c.a[(size_t)i], c.cry);
        dl->AddRectFilled(ImVec2(x, y), ImVec2(x + sw, y + sw), rgbU32(rgb));
        if (c.haveSel && c.sel == i)
            {   // selection: a text-colour frame with a window-colour keyline so it reads on any swatch
            dl->AddRect(ImVec2(x - 1, y - 1), ImVec2(x + sw + 1, y + sw + 1), ui::u32(ui::pal().text), 0.0f, 0, 2.0f);
            dl->AddRect(ImVec2(x + 1, y + 1), ImVec2(x + sw - 1, y + sw - 1), ImGui::GetColorU32(ImGuiCol_WindowBg));
        }
        else if (hover == i)
            dl->AddRect(ImVec2(x, y), ImVec2(x + sw, y + sw), ui::u32(ui::pal().text, 0.8f));
        if (c.a[(size_t)i] != c.b[(size_t)i])   // tables A and B differ (cannot happen through the bus; flag it)
            dl->AddRect(ImVec2(x + 1, y + 1), ImVec2(x + sw - 1, y + sw - 1), ui::u32(ui::pal().error));
    }
    if (hover >= 0) {
        const unsigned raw = c.a[(size_t)hover];
        const uint32_t rgb = swatchColor(c.mode, raw, c.cry);
        ui::beginWrappedTooltip();
        ImGui::Text("Index %d ($%02X)", hover, hover);
        ImGui::Text("Raw $%04X at $%06X", raw, clutAddrA((unsigned)hover));
        ImGui::TextUnformatted(describe(c.mode, raw).c_str());
        ImGui::Text("RGB #%06X", rgb);
        ui::endWrappedTooltip();
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            c.sel = hover;
            c.haveSel = true;
            c.rawFor = -1;
        }
    }

    // ---- the selected entry ---------------------------------------------------------------------------------------
    ImGui::Separator();
    if (!c.haveSel) {
        ui::hint("Click a swatch to see its value and edit it.");
        ImGui::End();
        return;
    }
    const unsigned idx = (unsigned)c.sel;
    const unsigned raw = c.a[idx];
    const uint32_t rgb = swatchColor(c.mode, raw, c.cry);
    ImGui::Text("Entry %u ($%02X)   raw $%04X   RGB #%06X", idx, idx, raw, rgb);
    ui::hint("%s", describe(c.mode, raw).c_str());
    if (c.a[idx] != c.b[idx]) ui::coloredWrapped(errCol(), "Table B holds a different value for this entry.");

    const bool canEdit = s.allowRegisterWrites;
    if (!canEdit) {
        ImGui::Checkbox("Allow register writes", &s.allowRegisterWrites);
        ui::hint("Editing the palette writes TOM's registers; tick this to enable it (the same switch as in the Memory window).");
    } else {
        ImGui::Checkbox("Allow register writes", &s.allowRegisterWrites);
        ui::coloredWrapped(warnCol(), "Register writes are on. Each change writes both CLUT tables.");
    }
    ImGui::BeginDisabled(!canEdit);
    // The picker keeps the colour the user chose (the table holds the nearest reproducible one); it resyncs
    // when another entry is selected or the entry changes behind its back.
    if (c.pickFor != c.sel || c.pickRaw != raw) {
        c.pick[0] = (float)((rgb >> 16) & 0xFF) / 255.0f;
        c.pick[1] = (float)((rgb >> 8) & 0xFF) / 255.0f;
        c.pick[2] = (float)(rgb & 0xFF) / 255.0f;
        c.pickFor = c.sel;
        c.pickRaw = raw;
    }
    ImGui::SetNextItemWidth(ui::fitWidth(0.9f, 14.0f, 30.0f));
    if (ImGui::ColorPicker3("##pick", c.pick, ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_NoSmallPreview |
                                                  ImGuiColorEditFlags_PickerHueBar | ImGuiColorEditFlags_InputRGB)) {
        const uint32_t nrgb = ((uint32_t)(c.pick[0] * 255.0f + 0.5f) << 16) | ((uint32_t)(c.pick[1] * 255.0f + 0.5f) << 8) |
                              (uint32_t)(c.pick[2] * 255.0f + 0.5f);
        const unsigned nraw = encodeSwatch(c.mode, nrgb, c.cry);
        if (nraw != raw) {
            std::string why;
            if (writeCell(s, clutAddrA(idx), 2, nraw, &why) < 0) {
                c.status = "Write refused: " + why;
                c.statusErr = true;
            } else {
                c.status.clear();
                c.pickRaw = nraw;   // the table will read back as this: do not resync the picker to it
                showWritten(idx, nraw);
            }
        }
    }
    ImGui::EndDisabled();

    // raw hex entry
    if (!c.rawActive && (c.rawFor != c.sel || c.rawShown != raw)) {
        std::snprintf(c.rawBuf, sizeof c.rawBuf, "%04X", raw);
        c.rawFor = c.sel;
        c.rawShown = raw;
    }
    ImGui::BeginDisabled(!canEdit);
    ImGui::SetNextItemWidth(ui::em(6.0f));
    const bool enter = ImGui::InputText("Raw", c.rawBuf, sizeof c.rawBuf,
                                        ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_CharsUppercase |
                                            ImGuiInputTextFlags_EnterReturnsTrue);
    c.rawActive = ImGui::IsItemActive();
    ImGui::SameLine();
    const bool apply = ImGui::Button("Apply");
    ImGui::EndDisabled();
    if (enter || apply) {
        unsigned v;
        std::string why;
        if (!parseCell(c.rawBuf, 2, v)) {
            c.status = "Type up to 4 hex digits";
            c.statusErr = true;
        } else if (writeCell(s, clutAddrA(idx), 2, v, &why) < 0) {
            c.status = "Write refused: " + why;
            c.statusErr = true;
        } else {
            c.status.clear();
            c.rawFor = -1;
            showWritten(idx, v);
        }
    }
    if (!c.status.empty()) ui::coloredWrapped(c.statusErr ? errCol() : ImGui::GetStyleColorVec4(ImGuiCol_Text), c.status.c_str());
    if (ImGui::Button("Show in Memory viewer")) showMemoryAt(s, clutAddrA(idx));
    ImGui::End();
}

}  // namespace vj
