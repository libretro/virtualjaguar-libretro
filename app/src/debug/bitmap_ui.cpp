#include "bitmap_ui.h"
#include "memory_view.h"
#include "ui_util.h"
#include "imgui.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>

namespace vj {
namespace {
BitmapViewModel vm;
char address[16] = "000000", palette[16] = "F00400", exportPath[512] = "";
std::string status;
unsigned tex = 0;
void (*destroyTex)(unsigned) = nullptr;
uint64_t textureSerial = ~0ull;
int zoom = 1;
void freeTexture() {
    if (tex && destroyTex) destroyTex(tex);
    tex = 0; textureSerial = ~0ull;
}
bool setAddress(DebugSession& s, unsigned a) {
    const int idx = memview::regionIndexAt(s.regions(), a);
    if (idx < 0) { status = "Address is outside the mapped regions"; return false; }
    const auto& r = s.regions()[(size_t)idx];
    vm.settings.address = a;
    vm.settings.end = r.start + std::min(r.len, 0x1000000u - r.start);
    std::snprintf(address, sizeof address, "%06X", a);
    vm.changed(); freeTexture(); status.clear();
    return true;
}
void nudge(DebugSession& s, int64_t delta) {
    const int idx = memview::regionIndexAt(s.regions(), vm.settings.address);
    if (idx < 0) return;
    const auto& r = s.regions()[(size_t)idx];
    const auto a = std::clamp<int64_t>((int64_t)vm.settings.address + delta, r.start, vm.settings.end - 1);
    setAddress(s, (unsigned)a);
}
}
void showBitmapAt(DebugSession& s, unsigned a) { setAddress(s, a); }
void releaseBitmap() { freeTexture(); vm.clear(); }
namespace bitmap_test { const BitmapViewModel& model() { return vm; } }

void drawBitmap(DebugSession& s, const BitmapUiContext& ctx) {
    auto& q = vm.settings;
    bool changed = false;
    ImGui::SetNextItemWidth(ui::em(9));
    const bool go = ImGui::InputText("Start (hex)", address, sizeof address, ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if (ImGui::Button("Go##bitmap") || go) {
        unsigned a;
        if (!memview::parseAddress(address, a)) status = "Enter a 24-bit hex address";
        else setAddress(s, a);
    }
    ImGui::TextWrapped("Range ends at $%06X (exclusive)", q.end);
    int width = (int)q.width, height = (int)q.height, depth = (int)q.depth, color = (int)q.color;
    ImGui::SetNextItemWidth(ui::em(10));
    if (ImGui::InputInt("Width (pixels)", &width, 1, 8)) { q.width = (unsigned)std::clamp(width, 1, 4096); changed = true; }
    ImGui::SetNextItemWidth(ui::em(10));
    if (ImGui::InputInt("Height (0 = to range end)", &height)) { q.height = (unsigned)std::clamp(height, 0, 0x1000000); changed = true; }
    const char* depths[] = {"1 bpp", "2 bpp", "4 bpp", "8 bpp", "16 bpp", "24 bpp (32-bit storage)"};
    ImGui::SetNextItemWidth(ui::em(18));
    if (ImGui::Combo("Depth", &depth, depths, 6)) { q.depth = (unsigned)depth; changed = true; }
    const char* colors[] = {"Auto (VMODE)", "CRY", "RGB16", "Mixed"};
    ImGui::SetNextItemWidth(ui::em(18));
    if (ImGui::Combo("Colour", &color, colors, 4)) { q.color = (OpColorMode)color; changed = true; }
    ImGui::SetNextItemWidth(ui::em(9));
    if (ImGui::InputText("CLUT (hex, live = F00400)", palette, sizeof palette, ImGuiInputTextFlags_EnterReturnsTrue)) {
        unsigned a;
        if (memview::parseAddress(palette, a) && a <= 0x1000000 - 512) { q.clutAddress = a; changed = true; }
        else status = "CLUT must hold 512 bytes inside the bus";
    }
    const unsigned row = bitmapRowBytes(q.width, q.depth);
    ImGui::Text("%u bytes per row", row);
    if (ImGui::Button("- row")) nudge(s, -(int64_t)row);
    ImGui::SameLine(); if (ImGui::Button("+ row")) nudge(s, row);
    ImGui::SameLine(); if (ImGui::Button("- phrase")) nudge(s, -8);
    ImGui::SameLine(); if (ImGui::Button("+ phrase")) nudge(s, 8);
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput && !ImGui::IsAnyItemActive()) {
        int dw = 0;
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) dw = ImGui::GetIO().KeyShift ? -8 : -1;
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) dw = ImGui::GetIO().KeyShift ? 8 : 1;
        if (dw) { q.width = (unsigned)std::clamp((int)q.width + dw, 1, 4096); changed = true; }
        const int64_t step = ImGui::GetIO().KeyShift ? 8 : row;
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) nudge(s, -step);
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) nudge(s, step);
    }
    ui::hint("Left/Right: width +/-1 (Shift: 8). Up/Down: address +/-row (Shift: phrase).");
    if (changed) { vm.changed(); freeTexture(); }
    vm.update(s);
    if (ImGui::Button("Guess width / depth")) vm.guess(s);
    // Buttons mutate the model: copy candidates before a click invalidates them.
    const auto guesses = vm.guesses();
    ui::Flow flow;
    for (const auto& c : guesses.candidates) {
        char label[48]; std::snprintf(label, sizeof label, "%u px (%u bytes)", c.width, c.stride);
        flow.next(ui::Flow::buttonW(label));
        if (ImGui::Button(label)) { q.width = c.width; vm.changed(); freeTexture(); }
    }
    const char* hint = guesses.hint == BitmapDepthHint::Indexed8 ? "8 bpp likely: similar even/odd byte distributions" :
                       guesses.hint == BitmapDepthHint::Color16 ? "16 bpp / CRY likely: distinct chroma/intensity distributions" :
                       "8 vs 16 bpp: inconclusive";
    ImGui::TextWrapped("%s", hint);
    if (!guesses.note.empty()) ui::hint("%s", guesses.note.c_str());
    const auto& result = vm.result();
    const auto& img = result.image;
    if (!result.error.empty()) ImGui::TextWrapped("%s", result.error.c_str());
    if (result.ok) {
        if (!img.note.empty()) ui::hint("%s", img.note.c_str());
        if (!exportPath[0]) {
            const auto path = std::filesystem::path(ctx.exportDir) / "bitmap.png";
            std::snprintf(exportPath, sizeof exportPath, "%s", path.string().c_str());
        }
        ImGui::SetNextItemWidth(ui::fitWidth(0.6f, 12, 36));
        ImGui::InputText("PNG path", exportPath, sizeof exportPath);
        if (ImGui::Button("Export PNG")) {
            const std::filesystem::path path(exportPath);
            std::error_code ec;
            if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), ec);
            std::string err;
            status = opSavePng(img, path.string(), err) ? "Saved " + path.string() : "Export failed: " + err;
        }
        ImGui::SameLine();
        const auto obj = bitmapObject(q);
        const bool canOpen = ctx.openObject && !(q.address & 7) && !(row & 7) &&
                             q.width == obj.iwidth * obj.pixelsPerPhrase && obj.height <= 1023 && obj.iwidth <= 1023;
        ImGui::BeginDisabled(!canOpen);
        if (ImGui::Button("Open as OP object")) {
            OpRenderOptions opt; opt.color = q.color; opt.clutAddress = q.clutAddress;
            ctx.openObject(obj, opt);
        }
        ImGui::EndDisabled();
        if (!canOpen && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("OP objects require phrase-aligned data/rows, a whole number of phrases and height <= 1023");
        ImGui::SetNextItemWidth(ui::em(8)); ImGui::SliderInt("Zoom", &zoom, 1, 8);
        if (textureSerial != vm.imageSerial()) {
            freeTexture();
            if (ctx.createTexture) { tex = ctx.createTexture(img.rgba.data(), img.w, img.h); destroyTex = ctx.destroyTexture; }
            textureSerial = vm.imageSerial();
        }
        if (!status.empty()) ImGui::TextWrapped("%s", status.c_str());
        ImGui::BeginChild("##bitmapimage", ImVec2(0, 0), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
        if (tex) {
            // ImGui's nearest sampler keeps raw pixel edges sharp at integer zoom.
            if (ImGui::GetPlatformIO().DrawCallback_SetSamplerNearest)
                ImGui::GetWindowDrawList()->AddCallback(ImGui::GetPlatformIO().DrawCallback_SetSamplerNearest, nullptr);
            ImGui::Image((ImTextureID)(intptr_t)tex, ImVec2((float)img.w * zoom, (float)img.h * zoom));
            ImGui::GetWindowDrawList()->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
        } else ImGui::Text("%d x %d pixels", img.w, img.h);
        ImGui::EndChild();
    }
    if (!result.ok && !status.empty()) ImGui::TextWrapped("%s", status.c_str());
}
}
