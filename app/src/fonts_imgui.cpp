#include "fonts_imgui.h"

#include <cstdio>
#include <cstring>
#include <vector>

#include "paths.h"

#ifdef VJ_HAVE_FREETYPE
#  include "imgui_internal.h"   // ImFontAtlasGetFontLoaderForStbTruetype()
#  include "misc/freetype/imgui_freetype.h"
#endif

namespace vj {
namespace fonts {

namespace {

struct State {
    std::string dir;
    Settings want, lastWant;
    bool dirty = true;
    bool applied = false;
    Settings active;
    std::string warn;
    FontFile ui, monoFile;
    ImFont* uiFont = nullptr;
    ImFont* monoFont = nullptr;
    bool retroVector = false;
    // The font files are read once and handed to the atlas by pointer (FontDataOwnedByAtlas = false);
    // they live until the fonts using them are cleared.
    std::vector<std::vector<unsigned char>> blobs;
};
State g;

#ifdef VJ_HAVE_FREETYPE
unsigned loaderFlags(const Settings& s) {
    unsigned f = 0;
    switch (s.hinting) {
    case Hinting::None: f |= ImGuiFreeTypeLoaderFlags_NoHinting; break;
    case Hinting::Light: f |= ImGuiFreeTypeLoaderFlags_LightHinting; break;
    case Hinting::Full: break;   // FreeType's default: the font's own hinting, or the auto-hinter
    }
    if (!s.antialias) f |= ImGuiFreeTypeLoaderFlags_Monochrome | ImGuiFreeTypeLoaderFlags_MonoHinting;
    return f;
}
#endif

// Picks the rasteriser for the whole atlas: FreeType for the typefaces from files, stb_truetype for the built-in
// Retro pixel font (it renders exactly as before).  A per-font ImFontConfig::FontLoader would be neater, but Dear
// ImGui does not support a custom loader that needs LoaderInit (FreeType's) on a single source -- it crashes --
// so the atlas default is switched instead.  Only legal with no fonts loaded (right after ClearFonts()).
void useLoader(bool freetype) {
#ifdef VJ_HAVE_FREETYPE
    ImGui::GetIO().Fonts->SetFontLoader(freetype ? ImGuiFreeType::GetFontLoader()
                                                 : ImFontAtlasGetFontLoaderForStbTruetype());
#else
    (void)freetype;
#endif
}

// Reads + validates a font file into g.blobs.  Returns the blob index or -1 (with `err`).
int loadBlob(const FontFile& f, std::string& err) {
    std::vector<unsigned char> data;
    if (f.empty() || !readFile(f.path, data)) {
        err = "The font file cannot be read.";
        return -1;
    }
    if (!validateFontData(data.data(), data.size(), f.index, err)) return -1;
    g.blobs.push_back(std::move(data));
    return (int)g.blobs.size() - 1;
}

ImFont* addFace(const FontFile& f, float size, const Settings& s, bool merge, std::string& err) {
    const int b = loadBlob(f, err);
    if (b < 0) return nullptr;
    ImFontConfig cfg;
    cfg.FontDataOwnedByAtlas = false;
    cfg.FontNo = (ImU32)f.index;
    cfg.PixelSnapH = false;     // sub-pixel glyph advances: even spacing at fractional sizes and scales
    cfg.MergeMode = merge;
    cfg.Flags |= ImFontFlags_NoLoadError;
    std::snprintf(cfg.Name, sizeof cfg.Name, "%s", displayName(f).c_str());
#ifdef VJ_HAVE_FREETYPE
    cfg.FontLoaderFlags = loaderFlags(s);   // per source; the atlas-wide loader is picked in reload()
#else
    (void)s;
#endif
    const std::vector<unsigned char>& d = g.blobs[(size_t)b];
    ImFont* font = ImGui::GetIO().Fonts->AddFontFromMemoryTTF((void*)d.data(), (int)d.size(), size, &cfg);
    if (!font) err = "Dear ImGui could not load the font.";
    return font;
}

FontFile fileFor(Family fam, const Settings& s) {
    if (fam == Family::System) return resolve(Role::Ui);
    if (fam == Family::Mono) return resolve(Role::Mono);
    FontFile f;
    f.path = customPath(g.dir, s.custom);
    return f;
}

void reload(const Settings& w) {
    ImGuiIO& io = ImGui::GetIO();
    ImGuiStyle& st = ImGui::GetStyle();
    st.FontSizeBase = w.size;   // AddFontDefault() picks the bitmap or the vector face from this
    io.Fonts->ClearFonts();
    g.blobs.clear();
    g.uiFont = g.monoFont = nullptr;
    g.ui = g.monoFile = FontFile();
    g.warn.clear();
    useLoader(w.family != Family::Retro);

    Settings act = w;
    std::string err;
    if (w.family != Family::Retro) {
        const FontFile f = fileFor(w.family, w);
        if (f.empty()) {
            g.warn = w.family == Family::Custom ? "The custom font file is missing; showing the built-in pixel font."
                                                : "No system font was found; showing the built-in pixel font.";
        } else {
            g.uiFont = addFace(f, w.size, w, false, err);
            if (g.uiFont) g.ui = f;
            else g.warn = "Could not use " + displayName(f) + " (" + err + ") -- showing the built-in pixel font.";
        }
    }
    if (g.uiFont) {
        // Symbols and arrows the main face lacks, then the built-in vector face as the last resort.
        int merged = 0;
        for (const FontFile& fb : candidates(Role::Fallback, hostOs(), systemProbe())) {
            if (merged >= 2) break;
            if (fb.path == g.ui.path || !fileExists(fb.path)) continue;
            std::string e2;
            if (addFace(fb, w.size, w, true, e2)) merged++;
        }
        ImFontConfig cfg;
        cfg.MergeMode = true;
        cfg.SizePixels = w.size;
        io.Fonts->AddFontDefaultVector(&cfg);
    } else {
        act.family = Family::Retro;
        useLoader(false);   // the built-in pixel font always uses stb_truetype
        g.uiFont = io.Fonts->AddFontDefault();
        g.retroVector = st.FontSizeBase * st.FontScaleMain * st.FontScaleDpi >= 15.0f;
    }

    // The monospace face for debug windows.
    if (act.family == Family::Mono) {
        g.monoFont = g.uiFont;
        g.monoFile = g.ui;
    } else if (act.family == Family::Retro) {
        g.monoFont = g.uiFont;   // ProggyClean / ProggyForever are monospace
    } else {
        const FontFile mf = resolve(Role::Mono);
        std::string e3;
        if (!mf.empty()) {
            g.monoFont = addFace(mf, w.size, w, false, e3);
            if (g.monoFont) g.monoFile = mf;
        }
        if (!g.monoFont) g.monoFont = io.Fonts->AddFontDefault();
    }
    io.FontDefault = g.uiFont;
    g.active = act;
    if (!g.warn.empty()) std::fprintf(stderr, "[fonts] %s\n", g.warn.c_str());
}

}  // namespace

void setCustomDir(const std::string& dir) { g.dir = dir; }
const std::string& customDir() { return g.dir; }

void request(const Settings& s) {
    g.want = sanitized(s);
    g.dirty = true;
}

bool applyPending() {
    if (!ImGui::GetCurrentContext()) return false;
    ImGuiStyle& st = ImGui::GetStyle();
    const Settings w = sanitized(g.want);
    if (!g.dirty && g.applied) {
        // Keep the size in force even if something reset the style (a theme change).
        if (st.FontSizeBase != g.active.size) st.FontSizeBase = g.active.size;
        return false;
    }
    g.dirty = false;
    bool reloadNeeded = !g.applied;
    if (g.applied) {
        // Compare with what was REQUESTED last time (a request that fell back keeps asking for the same thing).
        const Settings& p = g.lastWant;
        reloadNeeded = w.family != p.family || (w.family == Family::Custom && w.custom != p.custom) ||
                       (w.family != Family::Retro && freetypeAvailable() &&
                        (w.hinting != p.hinting || w.antialias != p.antialias));
        if (!reloadNeeded && w.family == Family::Retro) {
            // ProggyClean (bitmap) below 15 px, ProggyForever (vector) from there: the size slider can cross it.
            const bool vec = w.size * st.FontScaleMain * st.FontScaleDpi >= 15.0f;
            reloadNeeded = vec != g.retroVector;
        }
        if (!reloadNeeded && w.family != Family::Retro && !g.warn.empty()) reloadNeeded = true;   // retry after a fix
    }
    g.lastWant = w;
    if (reloadNeeded) {
        reload(w);
    } else {
        g.active.size = w.size;
        g.active.hinting = w.hinting;
        g.active.antialias = w.antialias;
        st.FontSizeBase = w.size;
    }
    g.active.size = w.size;
    g.applied = true;
    return true;
}

const Settings& active() { return g.active; }
const std::string& warning() { return g.warn; }
const FontFile& uiFile() { return g.ui; }
const FontFile& monoFile() { return g.monoFile; }

bool freetypeAvailable() {
#ifdef VJ_HAVE_FREETYPE
    return true;
#else
    return false;
#endif
}

ImFont* mono() { return g.monoFont; }

void pushMono() { ImGui::PushFont(g.monoFont, 0.0f); }   // null = keep the current face
void popMono() { ImGui::PopFont(); }

}  // namespace fonts
}  // namespace vj
