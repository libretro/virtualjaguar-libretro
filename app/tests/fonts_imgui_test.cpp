// The Dear ImGui half of the UI fonts (fonts_imgui.cpp): symbol coverage in every typeface.  A headless ImGui
// context with no renderer: the atlas is only asked which glyphs its fonts contain.
//
//   * Retro (the built-in pixel font) gets the same symbol fallback faces System UI uses, so the arrows, star and
//     check mark that draw in System UI also draw in Retro instead of "?"
//   * switching typeface and back keeps the symbols
//
//   vj_fonts_imgui_test
#include "fonts.h"
#include "fonts_imgui.h"
#include "imgui.h"
#include "paths.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <vector>

namespace fs = std::filesystem;
using namespace vj;
using namespace vj::fonts;

static int g_checks = 0, g_fail = 0;
#define CHECK(c) do { g_checks++; if (!(c)) { g_fail++; std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

static void apply(Family fam) {
    Settings s;
    s.family = fam;
    request(s);
    applyPending();
    CHECK(active().family == fam || fam != Family::Retro);
}

static bool has(ImFont* f, uint32_t cp) { return f && f->IsGlyphInFont((ImWchar)cp); }

int main() {
    const bool strict = std::getenv("FONTS_STRICT") != nullptr;
    const fs::path tmp = fs::temp_directory_path() / ("vj_fonts_imgui_test_" + std::to_string(std::rand()));
    fs::create_directories(tmp);
    setCustomDir(tmp.string() + "/");

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    io.DisplaySize = ImVec2(640, 480);

    // What the symbol faces cover on THIS machine (CI machines have whatever fonts they have): the fallback chain's
    // usable files, then the system UI face.
    const std::vector<uint32_t> marks = {0x2190, 0x2192, 0x2191, 0x2193, 0x2605, 0x2713};
    std::vector<FontFile> faces;
    for (const FontFile& fb : candidates(Role::Fallback, hostOs(), systemProbe()))
        if (fileExists(fb.path)) faces.push_back(fb);
    const FontFile ui = resolve(Role::Ui);
    if (!ui.empty()) faces.push_back(ui);
    std::vector<uint32_t> covered;
    for (uint32_t cp : marks) {
        std::vector<uint32_t> one(1, cp);
        for (const FontFile& fb : faces) {
            std::string err;
            if (!validateFontFile(fb.path, fb.index, err)) continue;   // a face the atlas would refuse (no Latin)
            if (missingGlyphs(fb.path, fb.index, one).empty()) {
                covered.push_back(cp);
                break;
            }
        }
    }
    std::fprintf(stderr, "[fonts] the symbol fallback chain on this machine covers %zu of %zu marks\n", covered.size(),
                 marks.size());
    CHECK(!strict || !covered.empty());

    // Retro: pixel font + the fallbacks.
    apply(Family::Retro);
    ImFont* retro = io.FontDefault;
    CHECK(retro != nullptr);
    CHECK(has(retro, 'A') && has(retro, '?'));   // the pixel font itself is intact
    for (uint32_t cp : covered) {
        const bool ok = has(retro, cp);
        if (!ok) std::fprintf(stderr, "FAIL Retro has no glyph for U+%04X though the fallback chain does\n", cp);
        CHECK(ok);
    }

    // System UI draws them too, and a trip through System UI and back leaves Retro with them.
    apply(Family::System);
    if (active().family == Family::System) {
        for (uint32_t cp : covered) CHECK(has(io.FontDefault, cp));
    }
    apply(Family::Retro);
    for (uint32_t cp : covered) CHECK(has(io.FontDefault, cp));

    ImGui::DestroyContext();
    std::error_code ec;
    fs::remove_all(tmp, ec);
    std::printf("fonts_imgui: %d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
