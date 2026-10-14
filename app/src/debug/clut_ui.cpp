// The Palette (CLUT) window: the 256 x 16-bit colour look-up table at $F00400 (table A; table B at
// $F00600 is its mirror -- JTRM v8: "writing to either address range writes to both tables").
#include "debug_session.h"
#include "memory_ui.h"
#include "memory_view.h"
#include "ui_util.h"

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
    float pick[3] = {0, 0, 0};          // the colour picker's own value while it is being dragged
    char rawBuf[8] = "";
    int rawFor = -1;
    unsigned rawShown = 0;
    bool rawActive = false;
    int pickFor = -1;
    unsigned pickRaw = 0;
    std::string status;
    bool statusErr = false;
    const void* ifaceSeen = nullptr;
};
ClutState g_clut;

const ImVec4 kWarn(1.0f, 0.75f, 0.35f, 1.0f);
const ImVec4 kErr(1.0f, 0.5f, 0.5f, 1.0f);

ImU32 rgbU32(uint32_t rgb) { return IM_COL32((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, 255); }

void refresh(DebugSession& s) {
    ClutState& c = g_clut;
    const uint64_t ser = s.refreshSerial();
    if (c.ok && ser == c.serial) return;
    std::vector<uint8_t> ra, rb;
    c.ok = s.read(kClutA, ra, kClutEntries * 2) && s.read(kClutB, rb, kClutEntries * 2);
    c.serial = ser;
    if (!c.ok) return;
    c.a.resize(kClutEntries);
    c.b.resize(kClutEntries);
    for (unsigned i = 0; i < kClutEntries; i++) {
        c.a[i] = decodeBE(&ra[i * 2], 2);
        c.b[i] = decodeBE(&rb[i * 2], 2);
    }
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

void drawClutWindow(DebugSession& s) {
    ClutState& c = g_clut;
    if (!clutWindowOpen()) return;
    ui::nextWindow(34.0f, 32.0f, 22.0f, 18.0f);
    bool open = true;
    const bool shown = ImGui::Begin("Palette (CLUT)", &open);
    ui::keepOnScreen();
    if (!open) clutWindowOpen() = false;
    if (!shown) {
        ImGui::End();
        return;
    }
    if (!s.enabled()) {
        ui::coloredWrapped(kWarn, s.disabledReason().c_str());
        ImGui::End();
        return;
    }
    if (c.ifaceSeen != (const void*)s.iface()) {
        c.ifaceSeen = s.iface();
        c.ok = false;
        c.haveSel = false;
        c.status.clear();
    }
    refresh(s);
    if (!c.ok) {
        ImGui::TextWrapped("The colour table could not be read.");
        ImGui::End();
        return;
    }
    // CRY needs the core's own colour table (once per core).
    const bool cryOk = c.cry.build(s.iface());
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
            dl->AddRect(ImVec2(x - 1, y - 1), ImVec2(x + sw + 1, y + sw + 1), IM_COL32(255, 255, 255, 255), 0.0f, 0, 2.0f);
        else if (hover == i)
            dl->AddRect(ImVec2(x, y), ImVec2(x + sw, y + sw), IM_COL32(255, 255, 255, 200));
        if (c.a[(size_t)i] != c.b[(size_t)i])   // tables A and B differ (cannot happen through the bus; flag it)
            dl->AddRect(ImVec2(x + 1, y + 1), ImVec2(x + sw - 1, y + sw - 1), IM_COL32(255, 60, 60, 255));
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
    if (c.a[idx] != c.b[idx]) ui::coloredWrapped(kErr, "Table B holds a different value for this entry.");

    const bool canEdit = s.allowRegisterWrites;
    if (!canEdit) {
        ImGui::Checkbox("Allow register writes", &s.allowRegisterWrites);
        ui::hint("Editing the palette writes TOM's registers; tick this to enable it (the same switch as in the Memory window).");
    } else {
        ImGui::Checkbox("Allow register writes", &s.allowRegisterWrites);
        ui::coloredWrapped(kWarn, "Register writes are on. Each change writes both CLUT tables.");
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
        }
    }
    if (!c.status.empty()) ui::coloredWrapped(c.statusErr ? kErr : ImGui::GetStyleColorVec4(ImGuiCol_Text), c.status.c_str());
    if (ImGui::Button("Show in Memory viewer")) showMemoryAt(s, clutAddrA(idx));
    ImGui::End();
}

}  // namespace vj
