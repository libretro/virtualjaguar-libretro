// Tools > Object list...: see op_ui.h.
#include "op_ui.h"

#include "app.h"
#include "cheats.h"
#include "op_view.h"
#include "ui_util.h"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>

namespace vj {

namespace {

struct UiState {
    bool open = false;
    OpViewModel vm;
    bool onlyBitmaps = false;
    bool highlight = true;
    bool highlightAll = false;
    bool checker = true;
    bool paintMode = false;
    int zoom = 0;                       // 0 = pick a sensible one when the selection changes
    int zoomEff = 1;                    // what the texture was built with
    uint32_t paintValue = 1;
    int lastPaintX = -1, lastPaintY = -1;
    int hoverRow = -1;
    int lastSel = -2;
    unsigned tex = 0;
    void (*destroyTex)(unsigned) = nullptr;
    int texW = 0, texH = 0;
    uint64_t texSerial = ~0ull;
    int texZoom = -1;
    std::string status;
    std::string pinStatus;
    OpScreenGeom geom;
    uint64_t geomSerial = ~0ull;
    unsigned geomFrameW = 0, geomFrameH = 0;
};
UiState g;

ImVec4 warnColor() { return ImVec4(1.0f, 0.8f, 0.4f, 1.0f); }

void freeTexture() {
    if (g.tex && g.destroyTex) g.destroyTex(g.tex);
    g.tex = 0;
    g.texSerial = ~0ull;
    g.texZoom = -1;
}

// Builds (or drops) the preview texture, upscaled by an integer zoom with nearest neighbour so the
// pixels stay square (VideoOut textures are bilinear).
void syncTexture(const ObjectsContext& ctx) {
    const OpImage* img = g.vm.image();
    if (!img || !ctx.createTexture) {
        if (g.tex) freeTexture();
        return;
    }
    int z = std::max(1, g.zoom);
    while (z > 1 && ((int64_t)img->w * z > 8192 || (int64_t)img->h * z > 8192 || (int64_t)img->w * img->h * z * z > 16 * 1024 * 1024))
        z--;
    if (g.tex && g.texSerial == g.vm.imageSerial() && g.texZoom == z) return;
    std::vector<uint8_t> big;
    const void* src = img->rgba.data();
    int tw = img->w, th = img->h;
    if (z > 1) {
        tw = img->w * z;
        th = img->h * z;
        big.resize((size_t)tw * th * 4);
        for (int y = 0; y < img->h; y++) {
            uint8_t* row0 = &big[(size_t)y * z * tw * 4];
            for (int x = 0; x < img->w; x++) {
                const uint8_t* px = img->rgbaAt(x, y);
                for (int k = 0; k < z; k++) std::memcpy(row0 + ((size_t)x * z + k) * 4, px, 4);
            }
            for (int k = 1; k < z; k++) std::memcpy(row0 + (size_t)k * tw * 4, row0, (size_t)tw * 4);
        }
        src = big.data();
    }
    if (g.tex && g.destroyTex) g.destroyTex(g.tex);
    g.tex = ctx.createTexture(src, tw, th);
    g.destroyTex = ctx.destroyTexture;
    g.texW = tw;
    g.texH = th;
    g.texSerial = g.vm.imageSerial();
    g.texZoom = z;
    g.zoomEff = z;
}

const char* colorModeName(OpColorMode m) {
    switch (m) {
        case OpColorMode::Auto: return "Auto (VMODE)";
        case OpColorMode::Cry: return "CRY";
        case OpColorMode::Rgb16: return "RGB16";
        default: return "Mixed (VARMOD)";
    }
}

// ---- table ---------------------------------------------------------------------------------------------
void objectRowText(const OpObject& o, char* ypos, char* xpos, char* size, char* depth, size_t n) {
    ypos[0] = xpos[0] = size[0] = depth[0] = 0;
    std::snprintf(ypos, n, "%u", o.ypos);
    switch (o.type) {
        case OpType::Bitmap:
        case OpType::Scaled:
            if (o.havePhrase1) {
                std::snprintf(xpos, n, "%d", o.xpos);
                if (o.type == OpType::Scaled && o.havePhrase2)
                    std::snprintf(size, n, "%ux%u>%u", o.widthPixels, o.height, o.scaledWidth);
                else
                    std::snprintf(size, n, "%ux%u", o.widthPixels, o.height);
                std::snprintf(depth, n, "%s", o.bpp ? opDepthName(o.depthCode) : "?");
            }
            break;
        case OpType::Branch: std::snprintf(size, n, "-> %06X", o.link); break;
        default: break;
    }
}

void drawList(const ObjectsContext& ctx, const std::vector<int>& rows) {
    (void)ctx;
    OpViewModel& vm = g.vm;
    g.hoverRow = -1;
    ImGuiTableFlags tf = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY |
                         ImGuiTableFlags_ScrollX | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingFixedFit;
    if (!ImGui::BeginTable("##oplist", 7, tf)) return;
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, ui::em(2.5f));
    ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, ui::em(3.2f));
    ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_WidthFixed, ui::em(5.0f));
    ImGui::TableSetupColumn("Y", ImGuiTableColumnFlags_WidthFixed, ui::em(3.2f));
    ImGui::TableSetupColumn("X", ImGuiTableColumnFlags_WidthFixed, ui::em(3.2f));
    ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, ui::em(7.5f));
    ImGui::TableSetupColumn("Depth", ImGuiTableColumnFlags_WidthFixed, ui::em(4.5f));
    ImGui::TableHeadersRow();
    ImGuiListClipper clip;
    clip.Begin((int)rows.size());
    while (clip.Step()) {
        for (int r = clip.DisplayStart; r < clip.DisplayEnd; r++) {
            int idx = rows[(size_t)r];
            const OpObject& o = vm.objects()[(size_t)idx];
            char ypos[24], xpos[24], size[40], depth[24], num[16];
            objectRowText(o, ypos, xpos, size, depth, sizeof ypos);
            std::snprintf(num, sizeof num, "%d", idx);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::PushID(idx);
            if (o.fromRam) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            if (ImGui::Selectable(num, vm.selected() == idx, ImGuiSelectableFlags_SpanAllColumns)) vm.select(idx);
            if (ImGui::IsItemHovered()) {
                g.hoverRow = idx;
                if (o.fromRam)
                    ImGui::SetTooltip("Found by following the links through RAM after the frame: not part of the captured walk, "
                                      "and HEIGHT / DATA may already have been advanced by the Object Processor.");
            }
            ImGui::PopID();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(opTypeShort(o.type));
            ImGui::TableNextColumn();
            ImGui::Text("%06X", o.address);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(ypos);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(xpos);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(size);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(depth);
            if (o.fromRam) ImGui::PopStyleColor();
        }
    }
    ImGui::EndTable();
}

// ---- field table -----------------------------------------------------------------------------------------
void fieldRow(const char* name, const char* fmt, ...) IM_FMTARGS(2);
void fieldRow(const char* name, const char* fmt, ...) {
    char b[256];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextDisabled("%s", name);
    ImGui::TableNextColumn();
    ImGui::TextWrapped("%s", b);
}

void drawFields(const OpObject& o) {
    if (!ImGui::BeginTable("##opfields", 2, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg)) return;
    ImGui::TableSetupColumn("f", ImGuiTableColumnFlags_WidthFixed, ui::em(8.0f));
    ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch);
    fieldRow("Object", "%s at $%06X", opTypeName(o.type), o.address);
    fieldRow("Source", "%s", o.fromRam ? "RAM, read after the frame (HEIGHT / DATA may be advanced)"
                                       : "snapshot of the Object Processor's walk (before write-back)");
    switch (o.type) {
        case OpType::Bitmap:
        case OpType::Scaled:
            fieldRow("YPOS", "%u half-lines (display line %u)", o.ypos, o.ypos / 2);
            fieldRow("HEIGHT", "%u", o.height);
            fieldRow("LINK", "$%06X", o.link);
            fieldRow("DATA", "$%06X", o.data);
            if (o.havePhrase1) {
                fieldRow("XPOS", "%d", o.xpos);
                fieldRow("DEPTH", "%u = %s", o.depthCode, opDepthName(o.depthCode));
                fieldRow("PITCH", "%u", o.pitch);
                fieldRow("DWIDTH", "%u phrases (%u bytes per line)", o.dwidth, o.dwidth * 8);
                if (o.iwidth == o.iwidthEff)
                    fieldRow("IWIDTH", "%u phrases = %u px", o.iwidth, o.iwidth * o.pixelsPerPhrase);
                else
                    fieldRow("IWIDTH", "%u (illegal; the emulator draws %u)", o.iwidth, o.iwidthEff);
                if (o.bpp && o.bpp <= 4)
                    fieldRow("INDEX", "$%02X  CLUT entry base $%02X", o.index, o.clutBase);
                else
                    fieldRow("INDEX", "$%02X%s", o.index, o.bpp == 8 ? "  (unused at 8 bpp)" : "");
                fieldRow("Flags", "%s%s%s%s", o.reflect ? "REFLECT " : "", o.rmw ? "RMW " : "", o.trans ? "TRANS " : "",
                         o.release ? "RELEASE" : "");
                fieldRow("FIRSTPIX", "%u  (skips %u px of each line's first phrase)", o.firstpix, o.skipPixels);
                fieldRow("Draws", "%u px wide", o.widthPixels);
            }
            if (o.havePhrase2) {
                fieldRow("HSCALE", "$%02X = %.3f", o.hscale, o.hscale / 32.0);
                fieldRow("VSCALE", "$%02X = %.3f", o.vscale, o.vscale / 32.0);
                fieldRow("REMAINDER", "$%02X = %.3f", o.remainder, o.remainder / 32.0);
                fieldRow("On screen", "%u px wide", o.scaledWidth);
            }
            break;
        case OpType::Gpu:
            fieldRow("YPOS", "%u (the emulator fires the object whenever the OP reaches it)", o.ypos);
            fieldRow("DATA", "$%013llX  (OB0-3 for the interrupt handler)", (unsigned long long)o.gpuData);
            break;
        case OpType::Branch:
            fieldRow("CC", "%u: %s", o.cc, opBranchCondName(o.cc));
            fieldRow("YPOS", "%u", o.ypos);
            fieldRow("LINK", "$%06X", o.link);
            break;
        case OpType::Stop:
            fieldRow("DATA", "$%015llX", (unsigned long long)o.stopData);
            fieldRow("Interrupt", "%s", o.stopInterrupt ? "yes (bit 3 set)" : "no (this emulator raises it only when bit 3 is set)");
            break;
        default: fieldRow("TYPE", "%u (undefined)", o.rawType); break;
    }
    fieldRow("Raw", "%08X %08X", (unsigned)(o.p0 >> 32), (unsigned)o.p0);
    if (o.phrases >= 2) fieldRow("", "%08X %08X", (unsigned)(o.p1 >> 32), (unsigned)o.p1);
    if (o.phrases >= 3) fieldRow("", "%08X %08X", (unsigned)(o.p2 >> 32), (unsigned)o.p2);
    ImGui::EndTable();
    if (!o.warning.empty()) ui::coloredWrapped(warnColor(), o.warning.c_str());
}

// ---- paint value controls ----------------------------------------------------------------------------------
void drawPaintControls(const vj_debug_iface* iface, const OpObject& o, const OpImage& img) {
    OpViewModel& vm = g.vm;
    uint32_t mx = opPixelMax(o);
    if (g.paintValue > mx) g.paintValue = mx;
    ImGui::Checkbox("Paint pixels", &g.paintMode);
    if (ImGui::IsItemHovered())
        ui::beginWrappedTooltip(),
            ImGui::TextUnformatted("Left-drag paints the value below straight into main RAM (cartridge data is read-only). "
                                   "Right-click picks the value under the cursor."),
            ui::endWrappedTooltip();
    if (!g.paintMode) return;
    if (o.bpp <= 8) {
        int v = (int)g.paintValue;
        ImGui::SetNextItemWidth(ui::fitWidth(0.4f, 8.0f, 20.0f));
        if (ImGui::SliderInt("Pixel value", &v, 0, (int)mx)) g.paintValue = (uint32_t)v;
        unsigned pal = (o.clutBase | g.paintValue) & 0xFF;
        unsigned w = pal < img.clut.size() ? img.clut[pal] : 0;
        uint32_t rgb = opColorToRgb(iface, img.mode, w);
        ImGui::SameLine();
        ImGui::ColorButton("##palswatch", ImVec4(((rgb >> 16) & 255) / 255.0f, ((rgb >> 8) & 255) / 255.0f, (rgb & 255) / 255.0f, 1.0f),
                           ImGuiColorEditFlags_NoTooltip, ImVec2(ImGui::GetFrameHeight(), ImGui::GetFrameHeight()));
        ImGui::SameLine();
        ImGui::TextDisabled("CLUT[%u] = $%04X", pal, w);
    } else if (o.bpp == 16) {
        unsigned v = g.paintValue;
        ImGui::SetNextItemWidth(ui::fitWidth(0.3f, 6.0f, 10.0f));
        if (ImGui::InputScalar("Colour word", ImGuiDataType_U32, &v, nullptr, nullptr, "%04X",
                               ImGuiInputTextFlags_CharsHexadecimal))
            g.paintValue = v & 0xFFFF;
        if (img.mode == OpColorMode::Rgb16) {
            int r = (g.paintValue >> 11) & 31, b = (g.paintValue >> 6) & 31, gg = g.paintValue & 63;
            bool ch = false;
            ImGui::SetNextItemWidth(ui::fitWidth(0.3f, 6.0f, 12.0f));
            ch |= ImGui::SliderInt("R", &r, 0, 31);
            ImGui::SetNextItemWidth(ui::fitWidth(0.3f, 6.0f, 12.0f));
            ch |= ImGui::SliderInt("G", &gg, 0, 63);
            ImGui::SetNextItemWidth(ui::fitWidth(0.3f, 6.0f, 12.0f));
            ch |= ImGui::SliderInt("B", &b, 0, 31);
            if (ch) g.paintValue = ((unsigned)r << 11) | ((unsigned)b << 6) | (unsigned)gg;
        } else {
            int c = (g.paintValue >> 12) & 15, r = (g.paintValue >> 8) & 15, y = g.paintValue & 255;
            bool ch = false;
            ImGui::SetNextItemWidth(ui::fitWidth(0.3f, 6.0f, 12.0f));
            ch |= ImGui::SliderInt("Cyan", &c, 0, 15);
            ImGui::SetNextItemWidth(ui::fitWidth(0.3f, 6.0f, 12.0f));
            ch |= ImGui::SliderInt("Red", &r, 0, 15);
            ImGui::SetNextItemWidth(ui::fitWidth(0.3f, 6.0f, 12.0f));
            ch |= ImGui::SliderInt("Intensity", &y, 0, 255);
            if (ch) g.paintValue = ((unsigned)c << 12) | ((unsigned)r << 8) | (unsigned)y;
        }
    } else {
        float col[3] = {((g.paintValue >> 16) & 255) / 255.0f, ((g.paintValue >> 8) & 255) / 255.0f, (g.paintValue & 255) / 255.0f};
        if (ImGui::ColorEdit3("Colour", col, ImGuiColorEditFlags_NoInputs)) {
            g.paintValue = ((uint32_t)std::lround(col[0] * 255) << 16) | ((uint32_t)std::lround(col[1] * 255) << 8) |
                           (uint32_t)std::lround(col[2] * 255);
        }
    }
    if (vm.hasLastEdit() && ImGui::Button("Pin last edit as cheat")) g.pinStatus = "pin";
}

// ---- preview ----------------------------------------------------------------------------------------------------
void drawPreview(DebugSession& s, const ObjectsContext& ctx, const OpObject& o) {
    OpViewModel& vm = g.vm;
    const OpImage* img = vm.image();
    if (!img) {
        if (!vm.imageError().empty()) ui::coloredWrapped(warnColor(), vm.imageError().c_str());
        return;
    }
    // Controls.
    ImGui::SetNextItemWidth(ui::fitWidth(0.3f, 7.0f, 12.0f));
    if (ImGui::SliderInt("Zoom", &g.zoom, 1, 16, "%dx")) g.texSerial = ~0ull;
    ImGui::SameLine();
    ImGui::Checkbox("Checkerboard", &g.checker);
    {
        ImGui::SetNextItemWidth(ui::fitWidth(0.3f, 8.0f, 14.0f));
        if (ImGui::BeginCombo("Colour", colorModeName(vm.render.color))) {
            const OpColorMode ms[4] = {OpColorMode::Auto, OpColorMode::Cry, OpColorMode::Rgb16, OpColorMode::Mixed};
            for (OpColorMode m : ms)
                if (ImGui::Selectable(colorModeName(m), vm.render.color == m)) {
                    vm.render.color = m;
                    vm.invalidateImage();
                }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (ImGui::Checkbox("TRANS", &vm.render.honorTrans)) vm.invalidateImage();
        ImGui::SameLine();
        if (ImGui::Checkbox("REFLECT", &vm.render.honorReflect)) vm.invalidateImage();
        if (vm.render.color == OpColorMode::Auto) {
            ImGui::SameLine();
            ui::hint("(%s)", colorModeName(img->mode));
        }
    }
    if (!img->note.empty()) ui::coloredWrapped(warnColor(), img->note.c_str());

    syncTexture(ctx);
    // The image, in a scrolling child so a big bitmap does not stretch the window.
    float footer = ImGui::GetFrameHeightWithSpacing() * 4.0f;
    ImGui::BeginChild("##oppreview", ImVec2(0, std::max(ui::em(6.0f), ImGui::GetContentRegionAvail().y - footer)),
                      ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
    if (g.tex) {
        float z = (float)g.zoomEff;
        ImVec2 size((float)img->w * z, (float)img->h * z);
        ImVec2 p0 = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        if (g.checker) {
            const float t = 8.0f;
            int nx = (int)std::ceil(size.x / t), ny = (int)std::ceil(size.y / t);
            if ((int64_t)nx * ny <= 30000) {
                for (int ty = 0; ty < ny; ty++)
                    for (int tx = 0; tx < nx; tx++) {
                        ImU32 c = ((tx + ty) & 1) ? IM_COL32(70, 70, 70, 255) : IM_COL32(100, 100, 100, 255);
                        ImVec2 a(p0.x + tx * t, p0.y + ty * t);
                        dl->AddRectFilled(a, ImVec2(std::min(a.x + t, p0.x + size.x), std::min(a.y + t, p0.y + size.y)), c);
                    }
            } else {
                dl->AddRectFilled(p0, ImVec2(p0.x + size.x, p0.y + size.y), IM_COL32(85, 85, 85, 255));
            }
        }
        ImGui::Image(ImTextureRef((ImTextureID)(intptr_t)g.tex), size);
        bool hovered = ImGui::IsItemHovered();
        if (hovered) {
            ImVec2 m = ImGui::GetIO().MousePos;
            int px = (int)std::floor((m.x - p0.x) / z), py = (int)std::floor((m.y - p0.y) / z);
            if (px >= 0 && py >= 0 && px < img->w && py < img->h) {
                OpPixelLoc L = opPixelLocation(o, *img, px, py);
                const uint8_t* c = img->rgbaAt(px, py);
                uint32_t rv = img->rawAt(px, py);
                dl->AddRect(ImVec2(p0.x + px * z, p0.y + py * z), ImVec2(p0.x + (px + 1) * z, p0.y + (py + 1) * z),
                            IM_COL32(255, 255, 0, 255));
                ImGui::BeginTooltip();
                ImGui::Text("(%d, %d)  value $%X  rgb %02X%02X%02X%s", px, py, rv, c[0], c[1], c[2], c[3] ? "" : "  transparent");
                if (L.ok) ImGui::TextDisabled("memory $%06X", L.addr);
                ImGui::EndTooltip();
                if (g.paintMode) {
                    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) g.paintValue = std::min(rv, opPixelMax(o));
                    if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && (px != g.lastPaintX || py != g.lastPaintY)) {
                        g.lastPaintX = px;
                        g.lastPaintY = py;
                        if (rv != g.paintValue) vm.paint(s, px, py, g.paintValue, g.status);
                    }
                }
            }
        }
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) g.lastPaintX = g.lastPaintY = -1;
    }
    ImGui::EndChild();

    // Footer: edit controls, export.
    drawPaintControls(s.iface(), o, *img);
    if (g.pinStatus == "pin") {
        g.pinStatus.clear();
        const OpViewModel::Edit& e = vm.lastEdit();
        if (!ctx.cheats) {
            g.pinStatus = "cheats are not available";
        } else if (e.addr + e.n > DebugSession::kDramSize) {
            g.pinStatus = "only main RAM (000000-1FFFFF) can be a cheat";
        } else {
            char desc[64];
            std::snprintf(desc, sizeof desc, "Object %06X pixel edit", o.address);
            std::string err;
            if (ctx.cheats->addUser(desc, memoryCheatToCode(e.addr, e.bytes, e.n), err)) g.pinStatus = "added to Cheats as \"" + std::string(desc) + "\"";
            else g.pinStatus = "cheat refused: " + err;
        }
    }
    if (ImGui::Button("Export PNG")) {
        std::string dir = ctx.exportDir;
        std::error_code ec;
        if (!dir.empty()) std::filesystem::create_directories(dir, ec);
        char nm[96];
        std::snprintf(nm, sizeof nm, "%s_object_%06X.png", ctx.contentName.empty() ? "vj" : ctx.contentName.c_str(), o.address);
        std::string path = dir + nm, err;
        g.status = opSavePng(*img, path, err) ? "saved " + path : "PNG export failed: " + err;
    }
    if (!g.pinStatus.empty()) ui::hint("%s", g.pinStatus.c_str());
    if (!g.status.empty()) ui::hint("%s", g.status.c_str());
}

// ---- on-screen outlines ----------------------------------------------------------------------------------------------
void drawOutline(const OpObject& o, const ObjectsContext& ctx, const OpScreenGeom& geom, ImU32 col, int idx, bool label) {
    OpScreenRect r = opScreenRect(o, geom);
    if (!r.valid || ctx.frameW == 0 || ctx.frameH == 0 || ctx.picW <= 0 || ctx.picH <= 0) return;
    float sx = ctx.picW / (float)ctx.frameW, sy = ctx.picH / (float)ctx.frameH;
    ImVec2 a(ctx.picX + r.x * sx, ctx.picY + r.y * sy);
    ImVec2 b(a.x + std::max(1.0f, r.w * sx), a.y + std::max(1.0f, r.h * sy));
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    dl->PushClipRect(ImVec2(ctx.picX, ctx.picY), ImVec2(ctx.picX + ctx.picW, ctx.picY + ctx.picH), true);
    dl->AddRect(a, b, col, 0.0f, 0, 2.0f);
    if (label) {
        char t[24];
        std::snprintf(t, sizeof t, "#%d", idx);
        dl->AddText(ImVec2(a.x + 3, a.y + 2), col, t);
    }
    dl->PopClipRect();
}

}  // namespace

// ---- public -------------------------------------------------------------------------------------------------------------------
void registerObjectsTool(App& app) {
    DebugSession& session = app.debug;
    DebugTool t;
    t.id = "objects";
    t.label = "Object list...";
    t.isOpen = [] { return g.open; };
    t.toggle = [] { g.open = !g.open; };
    t.draw = [&app] {
        ObjectsContext ctx;
        const PicRect& pic = app.input.picture();
        ctx.picX = pic.x;
        ctx.picY = pic.y;
        ctx.picW = pic.w;
        ctx.picH = pic.h;
        ctx.frameW = app.video.frameWidth();
        ctx.frameH = app.video.frameHeight();
        ctx.emulationRunning = !app.paused && !app.debug.halted();
        if (app.paused && !app.debug.halted() && app.host.isLoaded() && !app.netplay.configured())
            ctx.stepFrame = [&app] {
                app.host.runFrame();                 // one frame, so a pending snapshot can finish while paused
                app.video.upload(app.host.frame());
            };
        ctx.createTexture = &VideoOut::createTextureRGBA;
        ctx.destroyTexture = &VideoOut::destroyTexture;
        ctx.cheats = &app.cheats;
        ctx.exportDir = app.paths.root + "debug/";
        ctx.contentName = app.host.contentName();
        drawObjectsWindow(app.debug, ctx);
    };
    session.registerTool(t);
    session.onForcedClose.push_back([](const char*) {
        g.open = false;
        g.vm.clear();
        g.status.clear();
    });
}

bool objectsWindowOpen() { return g.open; }
void setObjectsWindowOpen(bool open) { g.open = open; }

void releaseObjectsTextures() { freeTexture(); }

void drawObjectsWindow(DebugSession& session, const ObjectsContext& ctx) {
    OpViewModel& vm = g.vm;
    if (!g.open) {
        if (g.tex) freeTexture();
        return;
    }
    ui::nextWindow(68.0f, 46.0f, 36.0f, 24.0f);
    bool vis = ImGui::Begin("Object list", &g.open);
    ui::keepOnScreen();
    if (!vis) {
        ImGui::End();
        return;
    }
    if (!session.enabled()) {
        ImGui::TextWrapped("%s", session.disabledReason().c_str());
        ImGui::End();
        if (g.tex) freeTexture();
        return;
    }

    vm.update(session);
    if (vm.selected() != g.lastSel) {
        g.lastSel = vm.selected();
        g.zoom = 0;
        g.lastPaintX = g.lastPaintY = -1;
        g.pinStatus.clear();
        if (const OpObject* o = vm.selectedObject()) {
            if (const OpImage* im = vm.image()) {
                (void)o;
                int big = std::max(im->w, im->h);
                g.zoom = std::clamp(320 / std::max(1, big), 1, 8);
            }
        }
    }
    if (g.zoom == 0) g.zoom = 2;

    // Toolbar.
    if (ImGui::Button("Arm snapshot")) {
        vm.arm(session);
        if (!ctx.emulationRunning && ctx.stepFrame) ctx.stepFrame();
    }
    if (ImGui::IsItemHovered())
        ui::beginWrappedTooltip(),
            ImGui::TextUnformatted("Captures the display list at the next frame, before the Object Processor rewrites "
                                   "HEIGHT / DATA / REMAINDER while it draws."),
            ui::endWrappedTooltip();
    ImGui::SameLine();
    ImGui::Checkbox("Live", &vm.live);
    ImGui::SameLine();
    if (ImGui::Checkbox("Follow links", &vm.followLinks)) vm.arm(session);
    if (ImGui::IsItemHovered())
        ui::beginWrappedTooltip(),
            ImGui::TextUnformatted("The snapshot is the first walk after arming, an early half-line, so a list gated by "
                                   "branch objects shows only the part above the first gate. Following the links through RAM "
                                   "lists the rest (read after the frame)."),
            ui::endWrappedTooltip();
    ImGui::SameLine();
    ImGui::Checkbox("Highlight on screen", &g.highlight);
    ImGui::SameLine();
    ImGui::Checkbox("All", &g.highlightAll);
    ImGui::SameLine();
    ImGui::Checkbox("Bitmaps only", &g.onlyBitmaps);
    if (vm.waiting()) {
        if (!ctx.emulationRunning)
            ui::hint("Waiting for the next frame: the game is paused%s.", ctx.stepFrame ? "" : " (resume it or advance a frame)");
        else
            ui::hint("Waiting for the next frame...");
        if (!ctx.emulationRunning && ctx.stepFrame) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Step a frame")) ctx.stepFrame();
        }
    }
    if (vm.haveSnapshot()) {
        size_t extra = vm.objects().size() - vm.snapshotCount();
        if (extra)
            ui::hint("%zu objects captured on half-line %u, %zu more found by following links (dimmed).  "
                     "X/Y are line-buffer pixels / half-lines; the outline is approximate.",
                     vm.snapshotCount(), vm.halfline(), extra);
        else
            ui::hint("%zu objects, captured on half-line %u.  X/Y are line-buffer pixels / half-lines; the outline is approximate.",
                     vm.objects().size(), vm.halfline());
    }
    else if (!vm.waiting())
        ui::hint("No snapshot yet.");
    ImGui::Separator();

    // Rows.
    std::vector<int> rows;
    rows.reserve(vm.objects().size());
    for (size_t i = 0; i < vm.objects().size(); i++)
        if (!g.onlyBitmaps || vm.objects()[i].isBitmap()) rows.push_back((int)i);

    bool wide = ImGui::GetContentRegionAvail().x > ui::em(46.0f);
    if (wide) {
        ImGui::BeginChild("##oplistpane", ImVec2(ImGui::GetContentRegionAvail().x * 0.5f, 0), ImGuiChildFlags_None);
    } else {
        ImGui::BeginChild("##oplistpane", ImVec2(0, ui::em(11.0f)), ImGuiChildFlags_None);
    }
    drawList(ctx, rows);
    ImGui::EndChild();
    if (wide) ImGui::SameLine();
    ImGui::BeginChild("##opdetail", ImVec2(0, 0), ImGuiChildFlags_Borders);
    if (const OpObject* o = vm.selectedObject()) {
        // Fields on top (scrolls when long), the preview gets the rest.
        float avail = ImGui::GetContentRegionAvail().y;
        float fieldsH = o->isBitmap() ? std::min(avail * 0.38f, ui::em(15.0f)) : 0.0f;
        ImGui::BeginChild("##opfieldspane", ImVec2(0, fieldsH), ImGuiChildFlags_None);
        drawFields(*o);
        ImGui::EndChild();
        if (o->isBitmap()) {
            ImGui::Spacing();
            drawPreview(session, ctx, *o);
        }
    } else {
        ui::hint("Select an object.");
    }
    ImGui::EndChild();
    ImGui::End();

    // Outlines over the game picture.
    if (g.highlight && vm.haveSnapshot() && ctx.frameW && ctx.picW > 0) {
        if (g.geomSerial != session.refreshSerial() || g.geomFrameW != ctx.frameW || g.geomFrameH != ctx.frameH) {
            g.geom = vm.readGeom(session, ctx.frameW, ctx.frameH);
            g.geomSerial = session.refreshSerial();
            g.geomFrameW = ctx.frameW;
            g.geomFrameH = ctx.frameH;
        }
        if (g.highlightAll)
            for (int i : rows) {
                const OpObject& o = vm.objects()[(size_t)i];
                if (o.drawable() && i != vm.selected()) drawOutline(o, ctx, g.geom, IM_COL32(80, 200, 255, 140), i, false);
            }
        if (g.hoverRow >= 0 && g.hoverRow != vm.selected() && g.hoverRow < (int)vm.objects().size())
            drawOutline(vm.objects()[(size_t)g.hoverRow], ctx, g.geom, IM_COL32(255, 255, 255, 200), g.hoverRow, true);
        if (const OpObject* o = vm.selectedObject()) drawOutline(*o, ctx, g.geom, IM_COL32(255, 230, 40, 255), vm.selected(), true);
    }
}

}  // namespace vj
