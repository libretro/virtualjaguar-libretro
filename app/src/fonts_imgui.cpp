#include "fonts_imgui.h"

#include <cstdio>
#include <cstring>
#include <map>
#include <vector>

#include <SDL3/SDL.h>

#include "fonts_system.h"
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
    // Lazy previews (fonts_imgui.h).  Every ImFont* here dies with ClearFonts(), so reload() drops the whole cache.
    struct PreviewEntry {
        Preview p;
        std::vector<unsigned char> blob;   // the font file; lives as long as the font does
        unsigned lastUsed = 0;
        size_t bytes = 0;
    };
    struct PreviewReq {
        std::string key, text;
        FontFile file;
        bool retro = false;
    };
    std::map<std::string, PreviewEntry> previews;
    std::vector<PreviewReq> previewReqs;
    unsigned frame = 0;
    size_t previewBytes = 0, loads = 0, evictions = 0;
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

// `snap`: whole-pixel glyph advances (the Retro pixel font's grid); the vector faces keep sub-pixel advances.
ImFont* addFace(const FontFile& f, float size, const Settings& s, bool merge, std::string& err, bool snap = false) {
    const int b = loadBlob(f, err);
    if (b < 0) return nullptr;
    ImFontConfig cfg;
    cfg.FontDataOwnedByAtlas = false;
    cfg.FontNo = (ImU32)f.index;
    cfg.PixelSnapH = snap;      // vector faces: sub-pixel glyph advances, even spacing at fractional sizes and scales
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
    if (fam == Family::Installed) {
        if (parseFontId(s.installed, f) && !fileExists(f.path)) f = FontFile();   // uninstalled since it was chosen
        return f;
    }
    f.path = customPath(g.dir, s.custom);
    return f;
}

void dropPreviews() {   // the atlas is about to be cleared: every cached ImFont* is about to dangle
    g.previews.clear();
    g.previewReqs.clear();
    g.previewBytes = 0;
}

// Symbols and arrows the main face lacks (the System UI fallback files), merged into the font just added.
// size 0 = the destination's own (implicit) size, which the built-in Retro font has.  At most two faces.
void mergeSymbolFallbacks(float size, const Settings& w, const std::string& skipPath, bool snap) {
    int merged = 0;
    for (const FontFile& fb : candidates(Role::Fallback, hostOs(), systemProbe())) {
        if (merged >= 2) break;
        if (fb.path == skipPath || !fileExists(fb.path)) continue;
        std::string e2;
        if (addFace(fb, size, w, true, e2, snap)) merged++;
    }
}

void reload(const Settings& w) {
    ImGuiIO& io = ImGui::GetIO();
    ImGuiStyle& st = ImGui::GetStyle();
    st.FontSizeBase = w.size;   // AddFontDefault() picks the bitmap or the vector face from this
    dropPreviews();
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
            g.warn = w.family == Family::Custom
                         ? "The custom font file is missing; showing the built-in pixel font."
                     : w.family == Family::Installed
                         ? "The chosen installed font is no longer available; showing the built-in pixel font."
                         : "No system font was found; showing the built-in pixel font.";
        } else {
            g.uiFont = addFace(f, w.size, w, false, err);
            if (g.uiFont) g.ui = f;
            else g.warn = "Could not use " + displayName(f) + " (" + err + ") -- showing the built-in pixel font.";
        }
    }
    if (g.uiFont) {
        // Symbols and arrows the main face lacks, then the built-in vector face as the last resort.
        mergeSymbolFallbacks(w.size, w, g.ui.path, false);
        // Any installed font can be the UI font now, and many lack the check mark and arrows the symbol faces do not
        // have either (Georgia): the system UI face is the last stop before the built-in vector face.
        {
            const FontFile sysUi = resolve(Role::Ui);
            std::string e6;
            if (!sysUi.empty() && sysUi.path != g.ui.path) addFace(sysUi, w.size, w, true, e6, false);
        }
        ImFontConfig cfg;
        cfg.MergeMode = true;
        cfg.SizePixels = w.size;
        io.Fonts->AddFontDefaultVector(&cfg);
    } else {
        act.family = Family::Retro;
        useLoader(false);   // the built-in pixel font always uses stb_truetype
        g.uiFont = io.Fonts->AddFontDefault();
        // The pixel font has no arrows, stars or check marks: the same symbol faces System UI uses fill in (one
        // face per glyph, so favourites and check marks draw in every typeface).
        if (g.uiFont) {
            mergeSymbolFallbacks(0.0f, w, std::string(), true);
            // Last resort for what those lack (macOS: Apple Symbols has no check mark and ZapfDingbats has no
            // Latin, so it cannot be a merged face): the system UI face itself.
            const FontFile sysUi = resolve(Role::Ui);
            std::string e4;
            if (!sysUi.empty()) addFace(sysUi, 0.0f, w, true, e4, true);
        }
        g.retroVector = st.FontSizeBase * st.FontScaleMain * st.FontScaleDpi >= 15.0f;
    }

    // The monospace face for debug windows: the user's pick first (Settings::debugFont), else by the UI family.
    if (!w.debugFont.empty()) {
        if (w.debugFont == "@retro") {
            g.monoFont = act.family == Family::Retro ? g.uiFont : io.Fonts->AddFontDefault();
        } else {
            FontFile mf;
            std::string e5;
            if (parseFontId(w.debugFont, mf) && fileExists(mf.path)) {
                g.monoFont = addFace(mf, w.size, w, false, e5);
                if (g.monoFont) g.monoFile = mf;
            }
        }
    }
    if (g.monoFont) {
        // explicit choice in force
    } else if (act.family == Family::Mono) {
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

static bool applySettingsNow() {
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
                       (w.family == Family::Installed && w.installed != p.installed) || w.debugFont != p.debugFont ||
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

namespace {

// Between two frames: forget the least recently used previews beyond the caps (never one drawn in the last frame or
// two), then load what rows asked for during the previous frame.
void pumpPreviews() {
    ImGuiIO& io = ImGui::GetIO();
    g.frame++;
    std::vector<State::PreviewReq> reqs;
    reqs.swap(g.previewReqs);   // rows ask again every frame while they are Pending and visible
    size_t budget = kPreviewLoadsPerFrame;
    size_t want = 0;   // faces about to be loaded: make room for them first, or a full cache would starve new rows
    for (const State::PreviewReq& r : reqs)
        if (want < budget && !g.previews.count(r.key)) want++;
    const size_t room = kPreviewMaxFonts > want ? kPreviewMaxFonts - want : 0;
    for (;;) {
        size_t loaded = 0;
        std::map<std::string, State::PreviewEntry>::iterator oldest = g.previews.end();
        for (auto it = g.previews.begin(); it != g.previews.end(); ++it) {
            if (!it->second.p.font) continue;
            loaded++;
            if (it->second.lastUsed + 1 < g.frame && (oldest == g.previews.end() || it->second.lastUsed < oldest->second.lastUsed))
                oldest = it;
        }
        if ((loaded <= room && g.previewBytes <= kPreviewMaxBytes) || oldest == g.previews.end()) break;
        io.Fonts->RemoveFont(oldest->second.p.font);
        g.previewBytes -= oldest->second.bytes;
        g.previews.erase(oldest);
        g.evictions++;
    }

    size_t loadedNow = 0;
    for (const auto& kv : g.previews)
        if (kv.second.p.font) loadedNow++;
    for (const State::PreviewReq& r : reqs) {
        if (budget == 0) break;
        if (g.previews.count(r.key)) continue;
        // Everything loaded is on screen right now (nothing was evictable): wait rather than grow without bound.
        if (loadedNow >= kPreviewMaxFonts || g.previewBytes >= kPreviewMaxBytes) break;
        State::PreviewEntry e;
        e.lastUsed = g.frame;
        e.p.state = PreviewState::Failed;
        if (r.retro) {
            ImFontConfig cfg;
            cfg.Flags |= ImFontFlags_NoLoadError;
            cfg.SizePixels = g.active.size;
            e.p.font = io.Fonts->AddFontDefaultVector(&cfg);
            if (e.p.font) e.p.state = PreviewState::Ready;
            else e.p.why = "unavailable";
            budget--;
        } else {
            SDL_PathInfo info;
            if (r.file.empty() || !SDL_GetPathInfo(r.file.path.c_str(), &info) || info.type != SDL_PATHTYPE_FILE) {
                e.p.why = "file not found";
                e.p.usable = false;
            } else if (info.size > kPreviewMaxFileBytes) {
                e.p.why = "too large to preview";   // still selectable: validateFontData allows up to 48 MB
            } else {
                budget--;
                std::string err;
                if (!readFile(r.file.path, e.blob)) {
                    e.p.why = "cannot be read";
                    e.p.usable = false;
                } else if (!validateFontData(e.blob.data(), e.blob.size(), r.file.index, err)) {
                    e.p.why = err.find("Latin") != std::string::npos ? "no Latin letters" : "not a usable font";
                    e.p.usable = false;
                } else if (!dataCoversText(e.blob.data(), e.blob.size(), r.file.index, r.text)) {
                    e.p.why = "cannot draw its own name";
                } else {
                    ImFontConfig cfg;
                    cfg.FontDataOwnedByAtlas = false;
                    cfg.FontNo = (ImU32)r.file.index;
                    cfg.PixelSnapH = false;
                    cfg.Flags |= ImFontFlags_NoLoadError;
                    std::snprintf(cfg.Name, sizeof cfg.Name, "preview %s", displayName(r.file).c_str());
#ifdef VJ_HAVE_FREETYPE
                    cfg.FontLoaderFlags = loaderFlags(g.active);
#endif
                    e.p.font = io.Fonts->AddFontFromMemoryTTF(e.blob.data(), (int)e.blob.size(), 0.0f, &cfg);
                    if (e.p.font) {
                        loadedNow++;
                        e.p.state = PreviewState::Ready;
                        e.bytes = e.blob.size();
                        g.loads++;
                    } else {
                        e.p.why = "cannot be loaded";
                    }
                }
            }
            if (!e.p.font) std::vector<unsigned char>().swap(e.blob);   // nothing to keep for a failure
        }
        g.previewBytes += e.bytes;
        g.previews[r.key] = std::move(e);
    }
}

}  // namespace

bool applyPending() {
    const bool changed = applySettingsNow();
    if (ImGui::GetCurrentContext() && g.applied) pumpPreviews();
    return changed;
}

Preview preview(const std::string& key, const FontFile& file, const std::string& text) {
    auto it = g.previews.find(key);
    if (it != g.previews.end()) {
        it->second.lastUsed = g.frame;
        return it->second.p;
    }
    for (const State::PreviewReq& r : g.previewReqs)
        if (r.key == key) return Preview();
    State::PreviewReq r;
    r.key = key;
    r.text = text;
    r.file = file;
    g.previewReqs.push_back(std::move(r));
    return Preview();
}

Preview previewRetro() {
    auto it = g.previews.find("@retro");
    if (it != g.previews.end()) {
        it->second.lastUsed = g.frame;
        return it->second.p;
    }
    for (const State::PreviewReq& r : g.previewReqs)
        if (r.retro) return Preview();
    State::PreviewReq r;
    r.key = "@retro";
    r.retro = true;
    g.previewReqs.push_back(std::move(r));
    return Preview();
}

PreviewStats previewStats() {
    PreviewStats s;
    for (const auto& kv : g.previews) {
        if (kv.second.p.font) s.fonts++;
        else s.failed++;
    }
    s.bytes = g.previewBytes;
    s.pending = g.previewReqs.size();
    s.loads = g.loads;
    s.evictions = g.evictions;
    return s;
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
