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
#include "fonts_system.h"
#include "imgui.h"
#include "paths.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
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

    // ---- installed fonts + lazy previews (#935) -------------------------------------------------------------
    {
        // Faces this machine really has that the atlas will accept.
        std::vector<SystemFont> usable;
        for (const SystemFont& f : scanFontDirs(systemFontDirs(hostOs(), systemProbe()))) {
            std::string err;
            if (!validateFontFile(f.path, f.index, err)) continue;
            usable.push_back(f);
            if (usable.size() >= 90) break;
        }
        std::fprintf(stderr, "[fonts] %zu installed faces usable for the preview tests\n", usable.size());
        CHECK(!strict || usable.size() >= 2);

        // Switching between two installed fonts really switches (the reload test compares the id, not just the family).
        if (usable.size() >= 2) {
            Settings s;
            s.family = Family::Installed;
            s.installed = usable[0].id();
            request(s);
            applyPending();
            CHECK(active().family == Family::Installed && uiFile().path == usable[0].path && uiFile().index == usable[0].index);
            s.installed = usable[1].id();
            request(s);
            applyPending();
            CHECK(uiFile().path == usable[1].path && uiFile().index == usable[1].index);
            // an uninstalled one falls back to Retro with a warning instead of leaving the old face
            s.installed = "/no/such/font.ttf|0";
            request(s);
            applyPending();
            CHECK(active().family == Family::Retro && !warning().empty());
        }

        // The debugger font: an explicit choice wins over the UI family; "" goes back to automatic.
        const SystemFont* mono = nullptr;
        for (const SystemFont& f : usable)
            if (f.mono) { mono = &f; break; }
        if (mono) {
            Settings s;
            s.family = Family::System;
            s.debugFont = mono->id();
            request(s);
            applyPending();
            CHECK(monoFile().path == mono->path);
            s.debugFont.clear();
            request(s);
            applyPending();
            CHECK(monoFile().path != mono->path || monoFile().path == resolve(Role::Mono).path);
        }

        // Previews: loaded lazily, a few per frame, bounded, negative-cached, dropped when the typeface reloads.
        apply(Family::Retro);
        CHECK(previewStats().fonts == 0);
        if (usable.size() >= 10) {
            const size_t n = usable.size();
            for (const SystemFont& f : usable) {
                FontFile ff;
                ff.path = f.path;
                ff.index = f.index;
                Preview p = preview(f.id(), ff, f.name);
                CHECK(p.state == PreviewState::Pending && p.font == nullptr);   // never loaded inside the frame
            }
            applyPending();
            CHECK(previewStats().fonts <= kPreviewLoadsPerFrame);               // at most a handful per frame
            CHECK(previewStats().fonts + previewStats().failed <= kPreviewLoadsPerFrame + 1);
            // scroll through the whole list, 12 rows visible at a time, like a clipped list being dragged
            for (int frame = 0; frame < 400; frame++) {
                const size_t top = (size_t)(frame / 2) % n;
                for (size_t k = 0; k < 12; k++) {
                    const SystemFont& f = usable[(top + k) % n];
                    FontFile ff;
                    ff.path = f.path;
                    ff.index = f.index;
                    preview(f.id(), ff, f.name);
                }
                applyPending();
                CHECK(previewStats().fonts <= kPreviewMaxFonts);
                CHECK(previewStats().bytes <= kPreviewMaxBytes + 2 * kPreviewMaxFileBytes);
            }
            const PreviewStats st = previewStats();
            std::fprintf(stderr, "[fonts] previews: %zu fonts, %zu bytes, %zu loads, %zu evictions\n", st.fonts, st.bytes,
                         st.loads, st.evictions);
            CHECK(st.fonts > 0);
            if (n > kPreviewMaxFonts + 8) CHECK(st.evictions > 0);
            // A face that is on screen comes back as Ready with a font; ask for the first until it does.
            FontFile f0;
            f0.path = usable[0].path;
            f0.index = usable[0].index;
            Preview ready;
            for (int i = 0; i < 40 && ready.state != PreviewState::Ready; i++) {
                ready = preview(usable[0].id(), f0, usable[0].name);
                applyPending();
            }
            CHECK(ready.state == PreviewState::Ready && ready.font != nullptr);
        }

        // Things that cannot be previewed fail once, with a reason, and are not asked for again.
        FontFile nofile;
        nofile.path = (tmp / "missing.ttf").string();
        FontFile junk;
        junk.path = (tmp / "junk.ttf").string();
        {
            std::ofstream o(junk.path, std::ios::binary);
            std::string z(2048, 'x');
            o.write(z.data(), (std::streamsize)z.size());
        }
        preview("t:nofile", nofile, "x");
        preview("t:junk", junk, "x");
        applyPending();
        const Preview pn = preview("t:nofile", nofile, "x"), pj = preview("t:junk", junk, "x");
        CHECK(pn.state == PreviewState::Failed && !pn.why.empty() && !pn.usable && pn.font == nullptr);
        CHECK(pj.state == PreviewState::Failed && !pj.why.empty() && !pj.usable && pj.font == nullptr);
        CHECK(previewStats().pending == 0);

        // The built-in pixel font previews too.
        Preview r;
        for (int i = 0; i < 4 && r.state != PreviewState::Ready; i++) {
            r = previewRetro();
            applyPending();
        }
        CHECK(r.state == PreviewState::Ready && r.font != nullptr);

        // A typeface reload drops every preview (their ImFont* died with ClearFonts).
        apply(Family::System);
        CHECK(previewStats().fonts == 0 && previewStats().failed == 0 && previewStats().bytes == 0);
    }

    ImGui::DestroyContext();
    std::error_code ec;
    fs::remove_all(tmp, ec);
    std::printf("fonts_imgui: %d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
