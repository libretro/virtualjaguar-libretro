// The Textures window.  See texture_ui.h.
#include "texture_ui.h"

#include "core_host.h"
#include "debug_session.h"
#include "paths.h"
#include "texture_pack.h"
#include "ui_util.h"
#include "video.h"

#include "imgui.h"
#include "titledb.h"   // TitleDBContentCRC(): the same identity the core keys vj_texdump/ and vj_texpacks/ by

#include <SDL3/SDL.h>
#include <SDL3/SDL_dialog.h>

#include <algorithm> #include <cmath>
#include <cctype>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace vj {

namespace {

namespace fs = std::filesystem;

constexpr size_t kThumbCache = 320;      // textures kept (a tile is at most 128x128 RGBA: ~20 MB worst case)
constexpr int kUploadsPerFrame = 4;
constexpr double kPollSeconds = 1.0;     // manifest + folder re-read; PNGs are written once, never rewritten
constexpr double kRetryFailedSeconds = 2.0;

struct Thumb {
    unsigned tex = 0;
    int w = 0, h = 0;          // the tile's own size (before the display upscale)
    bool failed = false;
    double failedAt = 0;
};

// Results of the file dialogs (they finish on another thread, or later): picked up by the next draw.
struct Pending {
    std::mutex m;
    std::string kind;      // "export" | "replace"
    std::string hash;
    std::string path;
    bool have = false;
};

struct DialogReq {
    std::string kind, hash;
};

struct State {
    DebugSession* session = nullptr;
    CoreHost* host = nullptr;
    const Paths* paths = nullptr;
    std::function<SDL_Window*()> window;
    bool open = false;
    bool registered = false;

    uint32_t crc = 0;
    bool crcKnown = false;
    std::string sysDir;
    tex::DumpIndex index;
    std::unordered_set<std::string> packed;     // hashes with a pack file
    double nextPoll = 0;
    uint64_t seenSerial = 0;                    // DebugSession::refreshSerial() the folders were last re-read for
    uint64_t revision = 1;                      // bumps when the entry list or the pack set changes

    std::unordered_map<std::string, Thumb> thumbs;
    tex::LruKeys lru{kThumbCache};
    std::vector<unsigned> retired;               // textures drawn this frame: destroyed at the start of the next

    std::string selected;
    char filter[40] = "";
    bool replacedOnly = false;
    float thumbEm = 7.0f;

    std::vector<size_t> view;                   // indices into index.manifest().entries
    uint64_t viewRevision = 0;
    std::string viewFilter;
    bool viewReplacedOnly = false;

    std::string status;
    bool statusIsError = false;
    Pending pending;
};
State g;

tex::OptionIO optionIO() {
    tex::OptionIO io;
    CoreHost* h = g.host;
    io.get = [h](const std::string& k) { return h ? h->optionValueFor(k, "disabled") : std::string("disabled"); };
    io.set = [h](const std::string& k, const std::string& v) {
        if (h) h->setOptionValue(k, v);
    };
    return io;
}

void SDLCALL dialogDone(void* ud, const char* const* files, int) {
    std::unique_ptr<DialogReq> req((DialogReq*)ud);
    if (!files || !files[0] || !req) return;
    std::lock_guard<std::mutex> lk(g.pending.m);
    g.pending.kind = req->kind;
    g.pending.hash = req->hash;
    g.pending.path = files[0];
    g.pending.have = true;
}

// ImGui draws a frame's images after the window code returns, so a texture that was submitted this frame
// must outlive it: retire() parks it, flushRetired() (first thing next frame) destroys it.
void retire(unsigned tex) {
    if (tex) g.retired.push_back(tex);
}
void flushRetired() {
    for (unsigned t : g.retired) VideoOut::destroyTexture(t);
    g.retired.clear();
}

void destroyThumbs() {
    for (auto& kv : g.thumbs) retire(kv.second.tex);
    g.thumbs.clear();
    g.lru.clear();
}

void setStatus(const std::string& s, bool error) {
    g.status = s;
    g.statusIsError = error;
}

void resetForGame() {
    destroyThumbs();
    g.selected.clear();
    g.packed.clear();
    g.view.clear();
    g.viewRevision = 0;
    g.revision++;
    g.status.clear();
}

void closeWindow(const char*) {
    g.open = false;
    destroyThumbs();
}

// The game and the folders, about once a second.
void pollFiles(double now) {
    if (now < g.nextPoll) return;
    g.nextPoll = now + kPollSeconds;
    if (!g.session || !g.paths) return;
    const uint32_t crc = TitleDBContentCRC();
    const std::string sys = g.paths->system;
    if (!g.crcKnown || crc != g.crc || sys != g.sysDir) {
        g.crc = crc;
        g.crcKnown = true;
        g.sysDir = sys;
        g.index.setDir(tex::dumpDir(sys, crc));
        resetForGame();
    }
    if (g.index.poll()) g.revision++;
    std::unordered_set<std::string> now_ = tex::listPack(tex::packDir(sys, crc));
    if (now_ != g.packed) {
        g.packed.swap(now_);
        g.revision++;
    }
}

std::string lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

void rebuildView() {
    const std::string f = lower(g.filter);
    if (g.viewRevision == g.revision && g.viewFilter == f && g.viewReplacedOnly == g.replacedOnly) return;
    g.viewRevision = g.revision;
    g.viewFilter = f;
    g.viewReplacedOnly = g.replacedOnly;
    g.view.clear();
    const std::vector<tex::Entry>& es = g.index.manifest().entries;
    for (size_t i = 0; i < es.size(); i++) {
        if (g.replacedOnly && !g.packed.count(es[i].hash)) continue;
        if (!f.empty()) {
            char sz[32];
            std::snprintf(sz, sizeof sz, "%ux%u", es[i].w, es[i].h);
            if (es[i].hash.find(f) == std::string::npos && std::string(sz).find(f) == std::string::npos) continue;
        }
        g.view.push_back(i);
    }
}

// Loads one preview into a texture (integer-upscaled when tiny, so the pixels stay crisp under the
// linear filter).  False if the PNG could not be read (it may still be being written).
bool loadThumb(const std::string& hash, Thumb& t) {
    std::string path = tex::previewFile(g.index.dir(), hash);
    if (path.empty()) return false;
    SDL_Surface* png = SDL_LoadPNG(path.c_str());
    if (!png) return false;
    SDL_Surface* rgba = SDL_ConvertSurface(png, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(png);
    if (!rgba) return false;
    const int w = rgba->w, h = rgba->h;
    if (w < 1 || h < 1 || w > 4096 || h > 4096) {
        SDL_DestroySurface(rgba);
        return false;
    }
    const int k = std::max(1, std::min(8, 128 / std::max(w, h)));   // 16x16 -> 8x, 64x64 -> 2x, 128+ -> 1x
    std::vector<unsigned char> out((size_t)w * k * h * k * 4);
    for (int y = 0; y < h; y++) {
        const unsigned char* src = (const unsigned char*)rgba->pixels + (size_t)y * rgba->pitch;
        for (int ky = 0; ky < k; ky++) {
            unsigned char* dst = &out[((size_t)y * k + ky) * (size_t)w * k * 4];
            for (int x = 0; x < w; x++)
                for (int kx = 0; kx < k; kx++) std::memcpy(dst + ((size_t)x * k + kx) * 4, src + (size_t)x * 4, 4);
        }
    }
    SDL_DestroySurface(rgba);
    t.tex = VideoOut::createTextureRGBA(out.data(), w * k, h * k);
    t.w = w;
    t.h = h;
    return t.tex != 0;
}

// The texture for a tile, loading it if the per-frame budget allows.  0 = not (yet) available.
unsigned thumbFor(const std::string& hash, int& uploads, double now) {
    auto it = g.thumbs.find(hash);
    if (it != g.thumbs.end()) {
        Thumb& t = it->second;
        if (t.tex) {
            for (const std::string& ev : g.lru.touch(hash)) {
                auto e = g.thumbs.find(ev);
                if (e != g.thumbs.end()) {
                    retire(e->second.tex);
                    g.thumbs.erase(e);
                }
            }
            return t.tex;
        }
        if (!t.failed || now - t.failedAt < kRetryFailedSeconds) return 0;
    }
    if (uploads >= kUploadsPerFrame) return 0;
    uploads++;
    Thumb t;
    if (!loadThumb(hash, t)) {
        t.failed = true;
        t.failedAt = now;
        g.thumbs[hash] = t;
        return 0;
    }
    g.thumbs[hash] = t;
    for (const std::string& ev : g.lru.touch(hash)) {
        auto e = g.thumbs.find(ev);
        if (e != g.thumbs.end() && ev != hash) {
            retire(e->second.tex);
            g.thumbs.erase(e);
        }
    }
    return g.thumbs[hash].tex;
}

void openFolder(const std::string& dir, bool create) {
    std::error_code ec;
    if (create) fs::create_directories(fs::u8path(dir), ec);
    if (!fs::is_directory(fs::u8path(dir), ec)) {
        setStatus("The folder does not exist yet: " + dir, true);
        return;
    }
    if (!SDL_OpenURL(tex::fileUrl(dir).c_str())) setStatus(std::string("Could not open the folder: ") + SDL_GetError(), true);
}

void handlePending() {
    std::string kind, hash, path;
    {
        std::lock_guard<std::mutex> lk(g.pending.m);
        if (!g.pending.have) return;
        kind = g.pending.kind;
        hash = g.pending.hash;
        path = g.pending.path;
        g.pending.have = false;
    }
    if (kind == "export") {
        tex::InstallResult r = tex::exportPreview(tex::previewFile(g.index.dir(), hash), path);
        setStatus(r.ok ? "Exported to " + r.dest : "Export failed: " + r.error, !r.ok);
    } else if (kind == "replace") {
        const tex::Entry* e = g.index.manifest().find(hash);
        tex::ReplaceOutcome o = tex::replaceTile(optionIO(), g.session->iface(), g.sysDir, g.crc, hash, e ? e->w : 0,
                                                 e ? e->h : 0, e ? e->bpp : 0, path);
        setStatus(o.message, !o.ok);
        if (o.ok) {
            g.packed = tex::listPack(tex::packDir(g.sysDir, g.crc));
            g.revision++;
        }
    }
}

void startDialog(const char* kind, const std::string& hash) {
    SDL_Window* win = g.window ? g.window() : nullptr;
    if (!win) return;
    DialogReq* req = new DialogReq{kind, hash};
    if (std::strcmp(kind, "export") == 0) {
        SDL_ShowOpenFolderDialog(dialogDone, req, win, nullptr, false);
    } else {
        static SDL_DialogFileFilter flt[2] = {{"PNG images", "png"}, {"All files", "*"}};
        SDL_ShowOpenFileDialog(dialogDone, req, win, flt, 2, nullptr, false);
    }
}

void drawGrid(double now) {
    rebuildView();
    const float cell = ui::em(g.thumbEm);
    const float pad = ImGui::GetStyle().ItemSpacing.x;
    const float avail = ImGui::GetContentRegionAvail().x;
    const int cols = std::max(1, (int)((avail + pad) / (cell + pad)));
    const int rows = ((int)g.view.size() + cols - 1) / cols;
    const std::vector<tex::Entry>& es = g.index.manifest().entries;
    int uploads = 0;

    if (g.view.empty()) {
        if (es.empty())
            ui::hint("Nothing dumped for this game yet. Turn on \"Dump textures\" above and play; tiles the game "
                     "blits appear here as they are first used.");
        else
            ui::hint("No tile matches the filter.");
        return;
    }
    ImGuiListClipper clip;
    clip.Begin(rows, cell + ImGui::GetStyle().ItemSpacing.y);
    while (clip.Step()) {
        for (int row = clip.DisplayStart; row < clip.DisplayEnd; row++) {
            for (int c = 0; c < cols; c++) {
                const size_t vi = (size_t)row * cols + c;
                if (vi >= g.view.size()) break;
                const tex::Entry& e = es[g.view[vi]];
                if (c > 0) ImGui::SameLine();
                ImGui::PushID(e.hash.c_str());
                ImVec2 p = ImGui::GetCursorScreenPos();
                const bool sel = e.hash == g.selected;
                if (ImGui::InvisibleButton("##tile", ImVec2(cell, cell))) g.selected = e.hash;
                const bool hov = ImGui::IsItemHovered();
                ImDrawList* dl = ImGui::GetWindowDrawList();
                dl->AddRectFilled(p, ImVec2(p.x + cell, p.y + cell), ImGui::GetColorU32(ImGuiCol_FrameBg));
                unsigned t = thumbFor(e.hash, uploads, now);
                if (t) {
                    const Thumb& th = g.thumbs[e.hash];
                    const float inner = cell - 4.0f;
                    float s = std::min(inner / (float)th.w, inner / (float)th.h);
                    if (s >= 1.0f) s = std::floor(s);   // whole-number zoom for small tiles
                    const float w = th.w * s, h = std::max(2.0f, th.h * s);   // a 701x1 strip stays visible
                    ImVec2 a(p.x + (cell - w) * 0.5f, p.y + (cell - h) * 0.5f);
                    dl->AddImage(ImTextureRef((ImTextureID)(intptr_t)t), a, ImVec2(a.x + w, a.y + h));
                } else {
                    dl->AddText(ImVec2(p.x + 4, p.y + 4), ImGui::GetColorU32(ImGuiCol_TextDisabled), "...");
                }
                if (g.packed.count(e.hash)) {   // a pack file exists for this tile
                    dl->AddRectFilled(ImVec2(p.x + cell - ImGui::GetFontSize() - 6, p.y), ImVec2(p.x + cell, p.y + ImGui::GetFontSize() + 2),
                                      IM_COL32(40, 140, 70, 230));
                    dl->AddText(ImVec2(p.x + cell - ImGui::GetFontSize() - 2, p.y + 1), IM_COL32(255, 255, 255, 255), "R");
                }
                if (sel) dl->AddRect(p, ImVec2(p.x + cell, p.y + cell), ImGui::GetColorU32(ImGuiCol_PlotHistogram), 0, 0, 2.0f);
                else if (hov) dl->AddRect(p, ImVec2(p.x + cell, p.y + cell), ImGui::GetColorU32(ImGuiCol_Border), 0, 0, 1.0f);
                if (hov) {
                    ui::beginWrappedTooltip();
                    ImGui::Text("%s", e.hash.c_str());
                    if (e.w) ImGui::Text("%ux%u%s", e.w, e.h, e.bpp ? (std::string(", ") + std::to_string(e.bpp) + "bpp").c_str() : "");
                    ui::endWrappedTooltip();
                }
                ImGui::PopID();
            }
        }
    }
    clip.End();
}

void drawDetails() {
    const tex::Entry* e = g.selected.empty() ? nullptr : g.index.manifest().find(g.selected);
    if (!e) {
        ui::hint("Select a tile to see its details, export it or replace its art.");
        return;
    }
    const bool packed = g.packed.count(e->hash) != 0;
    int dummyUploads = 0;
    unsigned t = thumbFor(e->hash, dummyUploads, ImGui::GetTime());
    if (t && g.thumbs.count(e->hash)) {
        const Thumb& th = g.thumbs[e->hash];
        float s = std::min(ui::em(9.0f) / (float)th.w, ui::em(9.0f) / (float)th.h);
        if (s >= 1.0f) s = std::floor(s);
        ImGui::Image(ImTextureRef((ImTextureID)(intptr_t)t), ImVec2(th.w * s, std::max(2.0f, th.h * s)));
        ImGui::SameLine();
    }
    ImGui::BeginGroup();
    ImGui::Text("Hash   %s", e->hash.c_str());
    if (e->w) ImGui::Text("Size   %ux%u", e->w, e->h);
    if (e->bpp) ImGui::Text("Format %ubpp%s", e->bpp, e->bpp == 16 ? " (CRY or RGB16; the preview shows the dump option's choice)" : "");
    if (e->inManifest) {
        ImGui::Text("First seen at frame %u, source $%06X", e->frame, e->src);
        if (!e->clut.empty()) ImGui::Text("Palette CRC %s%s", e->clut.c_str(), e->sightings ? (" (+" + std::to_string(e->sightings) + " more)").c_str() : "");
        if (!e->flags.empty() && e->flags != "-") ImGui::Text("Blit flags: %s", e->flags.c_str());
    } else {
        ui::hint("Not in the manifest yet (the core writes it in chunks).");
    }
    ImGui::Text("Replacement: %s", packed ? "pack file present" : "none");
    ImGui::EndGroup();

    ui::Flow flow;
    if (ImGui::Button("Export...")) startDialog("export", e->hash);
    flow.next(ui::Flow::buttonW("Replace..."));
    if (ImGui::Button("Replace...")) startDialog("replace", e->hash);
    if (ImGui::IsItemHovered()) {
        ui::beginWrappedTooltip();
        ImGui::TextUnformatted("Pick a PNG for this tile. It must be 8-bit and either exactly the tile's size, or exactly twice "
                               "it with Internal Resolution at 2x. It is copied to the pack folder under the tile's hash.");
        ui::endWrappedTooltip();
    }
}

void drawBody(double now) {
    ui::hint("Textures the game copies with the blitter (16bpp sprites, tiles, UI) are captured here. Textures the GPU "
             "draws itself (for example Doom's walls) never pass through the blitter, so they can't be captured or "
             "replaced.");
    if (!g.session->enabled()) {
        ImGui::Spacing();
        ui::coloredWrapped(ui::vec4(ui::pal().warning), g.session->disabledReason().c_str());
        return;
    }
    tex::OptionIO io = optionIO();
    const int instance = g.paths ? g.paths->instance : 1;

    ImGui::Spacing();
    bool dumping = tex::optionOn(io, tex::kOptDump);
    ImGui::BeginDisabled(!tex::dumpingAllowed(instance) && !dumping);
    if (ImGui::Checkbox("Dump textures while playing", &dumping)) tex::setDumping(io, dumping, instance);
    ImGui::EndDisabled();
    if (!tex::dumpingAllowed(instance))
        ui::hint("Dumping is off in extra copies of the app (instance %d): the dump folder and its manifest are shared "
                 "by every copy running the same game. Use the first copy to dump.",
                 instance);
    bool use = tex::optionOn(io, tex::kOptReplace);
    if (ImGui::Checkbox("Use texture packs", &use)) io.set(tex::kOptReplace, use ? "enabled" : "disabled");
    if (ImGui::IsItemHovered()) {
        ui::beginWrappedTooltip();
        ImGui::TextUnformatted("Show replacement art from the pack folder in place of the game's own tiles. Presentation "
                               "only: the emulated machine, save states and netplay are unaffected.");
        ui::endWrappedTooltip();
    }

    ui::Flow flow;
    if (ImGui::Button("Reload packs")) {
        const vj_debug_iface* f = g.session->iface();
        if (f && f->size >= offsetof(vj_debug_iface, texreplace_reload) + sizeof f->texreplace_reload && f->texreplace_reload) {
            int active = f->texreplace_reload();
            setStatus(active ? "Packs reloaded." : "Packs reloaded; replacement is off or the folder is empty.", false);
        } else {
            setStatus("This core cannot reload packs; restart the game.", true);
        }
    }
    flow.next(ui::Flow::buttonW("Open pack folder"));
    if (ImGui::Button("Open pack folder")) openFolder(tex::packDir(g.sysDir, g.crc), true);
    flow.next(ui::Flow::buttonW("Open dump folder"));
    if (ImGui::Button("Open dump folder")) openFolder(g.index.dir(), false);

    if (!g.status.empty()) {
        if (g.statusIsError) ui::coloredWrapped(ui::vec4(ui::pal().error), g.status.c_str());
        else ImGui::TextWrapped("%s", g.status.c_str());
    }
    const tex::Manifest& m = g.index.manifest();
    ui::hint("%zu tiles%s%s  |  %zu replaced  |  %s", m.entries.size(), m.title.empty() ? "" : "  |  ",
             m.title.c_str(), g.packed.size(), g.index.dir().c_str());

    ImGui::SetNextItemWidth(ui::fitWidth(0.35f, 8.0f, 16.0f));
    ImGui::InputTextWithHint("##filter", "Filter (hash or 16x8)", g.filter, sizeof g.filter);
    ImGui::SameLine();
    ImGui::Checkbox("Replaced only", &g.replacedOnly);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ui::fitWidth(0.5f, 6.0f, 12.0f));
    ImGui::SliderFloat("##size", &g.thumbEm, 4.0f, 14.0f, "Size %.0f");

    const float detailsH = ImGui::GetTextLineHeightWithSpacing() * 9.5f;
    float gridH = ImGui::GetContentRegionAvail().y - detailsH - ImGui::GetStyle().ItemSpacing.y;
    gridH = std::max(gridH, ui::em(8.0f));
    if (ImGui::BeginChild("##grid", ImVec2(0, gridH), ImGuiChildFlags_Borders)) drawGrid(now);
    ImGui::EndChild();
    drawDetails();
}

}  // namespace

void textureToolInit(DebugSession& session, CoreHost& host, const Paths& paths, std::function<SDL_Window*()> window) {
    g.session = &session;
    g.host = &host;
    g.paths = &paths;
    g.window = std::move(window);
    for (const auto& kv : tex::sessionOverrides(paths.instance)) host.overrideOption(kv.first, kv.second);
    if (!g.registered) {
        g.registered = true;
        session.onForcedClose.push_back(closeWindow);
    }
    DebugTool t;
    t.id = "textures";
    t.label = "Textures...";
    t.isOpen = [] { return g.open; };
    t.toggle = [] {
        g.open = !g.open;
        if (g.open) g.nextPoll = 0;
        else destroyThumbs();
    };
    session.registerTool(t);
}

void textureToolDraw() {
    flushRetired();
    if (!g.open || !g.session) return;
    const double now = ImGui::GetTime();
    ui::nextWindow(52.0f, 40.0f, 30.0f, 22.0f);
    bool shown = ImGui::Begin("Textures", &g.open);
    ui::keepOnScreen();
    if (shown) {
        if (g.session->enabled()) {
            // A state load / reset / frame advance (the session's refresh serial) re-reads the folders without waiting
            // out the 1 s poll -- but at most 4 times a second, since the serial also ticks at 10 Hz while the game runs.
            const uint64_t ser = g.session->refreshSerial();
            if (ser != g.seenSerial) {
                g.seenSerial = ser;
                if (g.nextPoll <= now + kPollSeconds - 0.25) g.nextPoll = 0;   // the last poll was >= 0.25 s ago
            }
            pollFiles(now);
            handlePending();
        }
        drawBody(now);
    }
    ImGui::End();
    if (!g.open) destroyThumbs();
}

void textureToolShutdown() {
    destroyThumbs();
    flushRetired();
    g.open = false;
}

}  // namespace vj
