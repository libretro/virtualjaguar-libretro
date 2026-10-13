#include "library_service.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

#include "config.h"
#include "http.h"
#include "paths.h"

namespace vj {

namespace {

const char* const kDatCarts = "Atari - Jaguar.dat";
const char* const kDatDiscs = "Atari - Jaguar CD.dat";
const char* const kDefaultDbBase = "https://raw.githubusercontent.com/libretro/libretro-database/master/metadat/";
const size_t kMaxTextures = 192;       // ~256 px thumbnails: about 70 MB of VRAM at most
const int kMaxThumbPx = 256;           // long side after downsizing
const size_t kMaxArtBytes = 8u << 20;
const int kMaxFolders = 32;
const int kUploadsPerFrame = 4;
const double kWantedWindow = 5.0;      // seconds: a queued download is dropped if nobody asked since
const double kRetryAfterFailure = 300.0;

bool isPng(const std::vector<unsigned char>& b) {
    static const unsigned char sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    return b.size() > 64 && std::memcmp(b.data(), sig, 8) == 0;
}

// PNG bytes -> RGBA, long side <= kMaxThumbPx.  Runs on the decode thread: SDL surfaces only.
bool decodePng(const std::vector<unsigned char>& bytes, int& outW, int& outH, std::vector<unsigned char>& rgba) {
    SDL_IOStream* io = SDL_IOFromConstMem(bytes.data(), bytes.size());
    if (!io) return false;
    SDL_Surface* s = SDL_LoadPNG_IO(io, true);
    if (!s) return false;
    if (s->w < 1 || s->h < 1 || s->w > 8192 || s->h > 8192) {
        SDL_DestroySurface(s);
        return false;
    }
    int tw = s->w, th = s->h;
    int longSide = std::max(tw, th);
    if (longSide > kMaxThumbPx) {
        tw = std::max(1, (int)((int64_t)s->w * kMaxThumbPx / longSide));
        th = std::max(1, (int)((int64_t)s->h * kMaxThumbPx / longSide));
    }
    // Halve while more than 2x too big (an exact 2x2 average), then one final step.
    while (s->w > tw * 2 && s->h > th * 2) {
        SDL_Surface* h = SDL_ScaleSurface(s, s->w / 2, s->h / 2, SDL_SCALEMODE_LINEAR);
        SDL_DestroySurface(s);
        if (!h) return false;
        s = h;
    }
    if (s->w != tw || s->h != th) {
        SDL_Surface* h = SDL_ScaleSurface(s, tw, th, SDL_SCALEMODE_LINEAR);
        SDL_DestroySurface(s);
        if (!h) return false;
        s = h;
    }
    SDL_Surface* c = SDL_ConvertSurface(s, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(s);
    if (!c) return false;
    outW = c->w;
    outH = c->h;
    rgba.resize((size_t)outW * outH * 4);
    for (int y = 0; y < outH; y++)   // the surface pitch need not equal width * 4
        std::memcpy(&rgba[(size_t)y * outW * 4], (const unsigned char*)c->pixels + (size_t)y * c->pitch, (size_t)outW * 4);
    SDL_DestroySurface(c);
    return true;
}

bool pathExists(const std::string& p, int64_t* mtimeSec = nullptr) {
    SDL_PathInfo info;
    if (!SDL_GetPathInfo(p.c_str(), &info)) return false;
    if (mtimeSec) *mtimeSec = (int64_t)(info.modify_time / 1000000000LL);
    return true;
}

const size_t kMaxCoverBytes = 32u << 20;   // a cover on disk (the thumbnail cache is capped at kMaxArtBytes)

// A cover, size-checked so a huge file cannot exhaust memory on the decode thread.
bool readArtFile(const std::string& path, std::vector<unsigned char>& out) {
    SDL_PathInfo info;
    if (!SDL_GetPathInfo(path.c_str(), &info) || info.type != SDL_PATHTYPE_FILE || info.size > kMaxCoverBytes) return false;
    return readFile(path, out);
}

std::string lowerCopy(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

}  // namespace

// ---- Worker ---------------------------------------------------------------------------------------

void Worker::start() {
    if (th_.joinable()) return;
    quit_ = false;
    th_ = std::thread([this] { loop(); });
}

void Worker::post(std::function<void()> job) {
    {
        std::lock_guard<std::mutex> g(m_);
        q_.push_back(std::move(job));
    }
    cv_.notify_one();
}

void Worker::stop() {
    {
        std::lock_guard<std::mutex> g(m_);
        quit_ = true;
        q_.clear();
    }
    cv_.notify_all();
    if (th_.joinable()) th_.join();
}

size_t Worker::pending() {
    std::lock_guard<std::mutex> g(m_);
    return q_.size() + (busy_ ? 1 : 0);
}

void Worker::loop() {
    for (;;) {
        std::function<void()> job;
        {
            std::unique_lock<std::mutex> lk(m_);
            cv_.wait(lk, [this] { return quit_ || !q_.empty(); });
            if (quit_) return;
            job = std::move(q_.front());
            q_.pop_front();
            busy_ = true;
        }
        job();
        {
            std::lock_guard<std::mutex> g(m_);
            busy_ = false;
        }
    }
}

// ---- LibraryService -----------------------------------------------------------------------------------

LibraryService::LibraryService() : artBaseUrl(lib::kThumbBase), dbBaseUrl(kDefaultDbBase), artMirrorUrl(lib::kMirrorBase) {}

LibraryService::~LibraryService() {
    cancel_ = true;
    scanCancel_ = true;
    scanW_.stop();
    decodeW_.stop();
    netW_.stop();
    // GL textures are freed in stop(), which needs the context; by now it may be gone.
}

double LibraryService::nowSec() const { return (double)SDL_GetTicks() / 1000.0; }

void LibraryService::setStatus(const std::string& s) {
    std::lock_guard<std::mutex> g(m_);
    status_ = s;
}

std::string LibraryService::status() {
    std::lock_guard<std::mutex> g(m_);
    return status_;
}

bool LibraryService::netBusy() { return netJobs_.load() > 0 || netW_.pending() > 0; }

std::string LibraryService::cartDatInfo() const {
    if (carts_.empty()) return "none";
    return carts_.header("version") + ", " + std::to_string(carts_.size()) + " titles";
}
std::string LibraryService::discDatInfo() const {
    if (discs_.empty()) return "none";
    return discs_.header("version") + ", " + std::to_string(discs_.size()) + " titles";
}

void LibraryService::start(const Paths& paths, Config& cfg, bool showIfConfigured) {
    if (started_) return;
    cfg_ = &cfg;
    root_ = paths.root;
    libDir_ = root_ + "library/";
    dbDir_ = libDir_ + "db/";
    cachePath_ = libDir_ + "scan.tsv";
    SDL_CreateDirectory(dbDir_.c_str());
    const char* base = SDL_GetBasePath();
    bundledDir_ = base ? std::string(base) + "libretro-db/" : std::string();

    folders_.clear();
    for (int i = 0; i < kMaxFolders; i++) {
        std::string d = cfg.get("library.dir." + std::to_string(i), "");
        if (!d.empty()) folders_.push_back(d);
    }
    downloadArt = cfg.getBool("library.art", true);
    if (cfg.has("library.art_mirror")) artMirrorUrl = cfg.get("library.art_mirror", "");   // "" turns the mirror off
    hideOnLaunch = cfg.getBool("library.hide_on_launch", true);
    cellScale = lib::clampTileScale(cfg.getFloat("library.size", lib::kTileScaleDefault));
    section = std::min(2, std::max(0, cfg.getInt("library.section", 0)));
    systemDir = paths.system;

    loadDats();
    loadCache();
    reidentify();

    scanW_.start();
    decodeW_.start();
    netW_.start();
    started_ = true;
    glAlive_ = true;
    rescan();   // also with no folders: the system folder's BIOS files
    show = showIfConfigured && !folders_.empty();
}

void LibraryService::stop() {
    if (zoomSaveAt_) saveSettings();
    cancel_ = true;
    scanCancel_ = true;
    scanW_.stop();
    decodeW_.stop();
    netW_.stop();
    if (glAlive_ && destroyTexture) {
        for (auto& kv : slots_)
            if (kv.second.tex) destroyTexture(kv.second.tex);
        glAlive_ = false;
    }
    slots_.clear();
    textureCount_ = 0;
    started_ = false;
}

void LibraryService::zoomChanged() { zoomSaveAt_ = SDL_GetTicks() + 600; }

void LibraryService::flushZoom() {
    if (zoomSaveAt_ && SDL_GetTicks() >= zoomSaveAt_) saveSettings();
}

void LibraryService::saveSettings() {
    zoomSaveAt_ = 0;
    if (!cfg_) return;
    for (int i = 0; i < kMaxFolders; i++) {
        std::string k = "library.dir." + std::to_string(i);
        if (i < (int)folders_.size()) cfg_->set(k, folders_[(size_t)i]);
        else cfg_->erase(k);
    }
    cfg_->setBool("library.art", downloadArt);
    cfg_->setBool("library.hide_on_launch", hideOnLaunch);
    cfg_->setFloat("library.size", cellScale);
    cfg_->setInt("library.section", section);
    cfg_->save();
}

// ---- dats and identification --------------------------------------------------------------------------------

void LibraryService::loadDats() {
    struct Spec {
        const char* file;
        lib::Dat* out;
    } specs[2] = {{kDatCarts, &carts_}, {kDatDiscs, &discs_}};
    for (const Spec& sp : specs) {
        lib::Dat user, bundled;
        bool haveUser = user.load(dbDir_ + sp.file);
        bool haveBundled = !bundledDir_.empty() && bundled.load(bundledDir_ + sp.file);
        // The newer dat wins, so a stale download never shadows a fresher one shipped with an update.
        if (haveUser && (!haveBundled || user.header("version") >= bundled.header("version")))
            *sp.out = std::move(user);
        else if (haveBundled)
            *sp.out = std::move(bundled);
        else
            *sp.out = lib::Dat();
    }
}

void LibraryService::reidentify() {
    entries_.clear();
    identified_ = 0;
    entries_.reserve(files_.size());
    for (const lib::ScanFile& f : files_) {
        entries_.push_back(lib::identify(f, &carts_, &discs_));
        if (entries_.back().match == lib::Match::Crc) identified_++;
    }
    std::sort(entries_.begin(), entries_.end(), [](const lib::Entry& a, const lib::Entry& b) {
        std::string ta = lowerCopy(a.title), tb = lowerCopy(b.title);
        return ta != tb ? ta < tb : a.file.path < b.file.path;
    });
}

void LibraryService::loadCache() {
    files_.clear();
    std::vector<unsigned char> bytes;
    if (!readFile(cachePath_, bytes)) return;
    lib::ScanCache c = lib::parseCache(std::string(bytes.begin(), bytes.end()));
    for (auto& kv : c) files_.push_back(kv.second);
}

void LibraryService::saveCache(const std::vector<lib::ScanFile>& files) {
    std::string text = lib::serializeCache(files);
    writeFileAtomic(cachePath_, text.data(), text.size());
}

// ---- folders and scanning ------------------------------------------------------------------------------------

void LibraryService::addFolder(const std::string& pathIn) {
    if (pathIn.empty()) return;
    std::string p = absolutePath(pathIn);
    while (p.size() > 1 && (p.back() == '/' || p.back() == '\\')) p.pop_back();
    if (std::find(folders_.begin(), folders_.end(), p) != folders_.end()) return;
    if ((int)folders_.size() >= kMaxFolders) return;
    folders_.push_back(p);
    saveSettings();
    rescan();
}

void LibraryService::removeFolder(size_t index) {
    if (index >= folders_.size()) return;
    folders_.erase(folders_.begin() + (long)index);
    saveSettings();
    rescan();
}

void SDLCALL LibraryService::folderDone(void* ud, const char* const* list, int) {
    // May run on another thread: just queue the choice for pump().
    LibraryService* self = (LibraryService*)ud;
    if (!list || !list[0]) return;
    std::lock_guard<std::mutex> g(self->m_);
    self->pickedFolders_.push_back(list[0]);
}

void LibraryService::addFolderDialog(SDL_Window* parent) {
    std::string def = folders_.empty() ? std::string() : folders_.back();
    SDL_ShowOpenFolderDialog(&LibraryService::folderDone, this, parent, def.empty() ? nullptr : def.c_str(), false);
}

bool LibraryService::installBios(const lib::Entry& e) {
    if (systemDir.empty()) {
        setStatus("No system folder to copy the BIOS into.");
        return false;
    }
    const std::string name = lib::biosInstallName(e.bios, e.file.size);
    if (name.empty()) {
        setStatus("The core would not read this file from its system folder either.");
        return false;
    }
    std::string dir = systemDir;
    if (!dir.empty() && dir.back() != '/' && dir.back() != '\\') dir += '/';
    const std::string dest = dir + name;
    SDL_PathInfo pi;
    if (SDL_GetPathInfo(dest.c_str(), &pi)) {   // never replace what the user already has there
        setStatus(name + " is already in the system folder; remove or rename it first to replace it.");
        return false;
    }
    size_t sz = 0;
    void* data = SDL_LoadFile(e.file.path.c_str(), &sz);
    bool ok = data && sz == e.file.size && writeFileAtomic(dest, data, sz);
    SDL_free(data);
    setStatus(ok ? "Copied to the system folder as " + name + "." : "Could not copy " + name + " to the system folder.");
    if (ok) rescan();
    return ok;
}

void LibraryService::rescan() {
    if (!started_) return;
    {   // covers dropped into <data>/covers/ since a tile last looked: ask again
        std::lock_guard<std::mutex> g(m_);
        for (auto& kv : slots_)
            if (kv.second.state == ArtView::Missing) kv.second.state = ArtView::None;
    }
    if (folders_.empty() && systemDir.empty()) {
        files_.clear();
        reidentify();
        saveCache(files_);
        return;
    }
    lib::ScanCache cache;
    for (const lib::ScanFile& f : files_) cache[f.path] = f;
    lib::ScanOptions opt;
    opt.folders = folders_;
    opt.systemFolder = systemDir;   // BIOS / system files there show in the BIOS section
    scanCancel_ = true;      // an older scan still running stops early
    scanning_ = true;
    scanDone_ = 0;
    scanTotal_ = 0;
    scanW_.post([this, cache, opt]() {
        scanCancel_ = false;
        std::vector<lib::ScanFile> res = lib::scanFolders(
            opt, cache, &scanCancel_, [this](size_t done, size_t total) {
                scanDone_ = done;
                scanTotal_ = total;
            });
        if (scanCancel_.load() || cancel_.load()) {
            // superseded by a newer rescan(), which will publish its own result
            if (cancel_.load()) scanning_ = false;
            return;
        }
        saveCache(res);
        {
            std::lock_guard<std::mutex> g(m_);
            scanResult_ = std::move(res);
            scanResultReady_ = true;
        }
        scanning_ = false;
    });
}

// ---- database update --------------------------------------------------------------------------------------------

void LibraryService::updateDatabase() {
    if (!started_) return;
    if (!http::available()) {
        setStatus("This build has no HTTP support, so the game database cannot be updated.");
        return;
    }
    setStatus("Updating game database...");
    netJobs_++;
    netW_.post([this]() {
        struct Src {
            const char* file;
            std::string url;
        } srcs[2] = {{kDatCarts, dbBaseUrl + "no-intro/" + lib::urlEncode(kDatCarts)},
                     {kDatDiscs, dbBaseUrl + "redump/" + lib::urlEncode(kDatDiscs)}};
        size_t got = 0;
        std::string problem;
        for (const Src& s : srcs) {
            if (cancel_.load()) break;
            http::Response r;
            if (!http::get(s.url, r, 8u << 20, &cancel_)) {
                problem = r.error;
                continue;
            }
            if (r.status != 200) {
                problem = "HTTP " + std::to_string(r.status);
                continue;
            }
            lib::Dat check;
            if (!check.parse(std::string(r.body.begin(), r.body.end())) || check.size() < 5) {
                problem = "the downloaded file is not a game database";
                continue;
            }
            if (writeFileAtomic(dbDir_ + s.file, r.body.data(), r.body.size())) got++;
            else problem = "cannot write " + dbDir_ + s.file;
        }
        if (got > 0) {
            std::lock_guard<std::mutex> g(m_);
            datsChanged_ = true;
        }
        if (cancel_.load()) { /* shutting down */ }
        else if (got == 2) setStatus("Game database updated.");
        else setStatus("Game database update failed: " + problem);
        netJobs_--;
    });
}

// ---- launching ---------------------------------------------------------------------------------------------------------

void LibraryService::launch(const lib::Entry& e) {
    // The same "open this path" event the file dialog posts; App::handleEvent turns it into openContent.
    SDL_Event ev;
    SDL_zero(ev);
    ev.type = SDL_EVENT_USER;
    ev.user.code = 0x56414A47;
    ev.user.data1 = SDL_strdup(e.file.path.c_str());
    SDL_PushEvent(&ev);
    if (hideOnLaunch) show = false;
}

void LibraryService::setDownloadArt(bool on) {
    downloadArt = on;
    if (on) {
        // Titles marked missing only because downloads were off get another chance.
        std::lock_guard<std::mutex> g(m_);
        for (auto& kv : slots_)
            if (kv.second.state == ArtView::Missing) kv.second.state = ArtView::None;
        netFail_.clear();
    }
    saveSettings();
}

// ---- box art ---------------------------------------------------------------------------------------------------------------

bool LibraryService::wanted(const std::string& key) {
    std::lock_guard<std::mutex> g(m_);
    auto it = slots_.find(key);
    return it != slots_.end() && nowSec() - it->second.lastWanted < kWantedWindow;
}

void LibraryService::publish(Decoded&& d) {
    std::lock_guard<std::mutex> g(m_);
    decoded_.push_back(std::move(d));
}

ArtView LibraryService::art(const lib::Entry& e) {
    ArtView v;
    ArtJob job;
    job.key = lib::coverKey(e);
    job.title = e.title;
    job.datName = e.datName;
    job.section = e.section;
    bool request = false;
    {
        std::lock_guard<std::mutex> g(m_);
        Slot& s = slots_[job.key];
        double t = nowSec();
        s.lastWanted = t;
        if (s.state == ArtView::Missing && s.retryAt > 0 && t > s.retryAt) s.state = ArtView::None;
        if (s.state == ArtView::None) {
            s.state = ArtView::Loading;
            request = true;
        }
        job.gen = s.gen;
        v.state = s.state;
        v.tex = s.tex;
        v.w = s.w;
        v.h = s.h;
    }
    if (request) decodeW_.post([this, job]() { decodeJob(job); });
    return v;
}

void LibraryService::invalidateArt(const std::string& key) {
    std::lock_guard<std::mutex> g(m_);
    auto it = slots_.find(key);
    if (it == slots_.end()) return;
    Slot& s = it->second;
    if (s.tex && destroyTexture) {
        destroyTexture(s.tex);
        if (textureCount_) textureCount_--;
    }
    s.tex = 0;
    s.w = s.h = 0;
    s.state = ArtView::None;
    s.retryAt = 0;
    s.gen++;
}

const LibraryService::CoverTile* LibraryService::tileAt(float x, float y, int frame) const {
    if (frame - tilesFrame > 2) return nullptr;   // the grid is not on screen
    for (const CoverTile& t : tiles)
        if (x >= t.x0 && x < t.x1 && y >= t.y0 && y < t.y1) return &t;
    return nullptr;
}

// ---- custom covers ---------------------------------------------------------------------------------------------------------

bool LibraryService::hasCustomCover(const lib::Entry& e) const {
    return pathExists(lib::customCoverPath(root_, lib::coverKey(e)));
}

bool LibraryService::setCover(const lib::Entry& e, const std::string& imagePath) {
    return setCoverKey(lib::coverKey(e), imagePath);
}

// Validates the image (PNG signature + a full decode), then keeps a verbatim copy: the original stays
// available for a larger tile size later, and the user's file is never touched.
bool LibraryService::setCoverKey(const std::string& key, const std::string& imagePath) {
    if (!started_) return false;
    std::vector<unsigned char> bytes;
    if (!readArtFile(imagePath, bytes)) {
        setStatus("Cover not set: cannot read that file (or it is over 32 MB).");
        return false;
    }
    int w = 0, h = 0;
    std::vector<unsigned char> px;
    if (!isPng(bytes) || !decodePng(bytes, w, h, px)) {
        setStatus("Cover not set: only PNG images are supported (up to 8192 x 8192).");
        return false;
    }
    const std::string dest = lib::customCoverPath(root_, key);
    SDL_CreateDirectory((root_ + "covers/").c_str());
    SDL_CreateDirectory((root_ + "covers/custom/").c_str());
    if (!writeFileAtomic(dest, bytes.data(), bytes.size())) {
        setStatus("Cover not set: cannot write into the covers folder.");
        return false;
    }
    invalidateArt(key);
    setStatus("Cover image set.");
    return true;
}

void LibraryService::removeCover(const lib::Entry& e) {
    const std::string key = lib::coverKey(e);
    const std::string p = lib::customCoverPath(root_, key);
    if (pathExists(p)) SDL_RemovePath(p.c_str());
    invalidateArt(key);
    setStatus("Cover image removed.");
}

namespace {
struct CoverDialog {
    LibraryService* self;
    std::string key;
};
}  // namespace

void SDLCALL LibraryService::coverDone(void* ud, const char* const* list, int) {
    // May run on another thread: just queue the choice for pump().
    CoverDialog* d = (CoverDialog*)ud;
    if (list && list[0]) {
        std::lock_guard<std::mutex> g(d->self->m_);
        d->self->pickedCovers_.emplace_back(d->key, list[0]);
    }
    delete d;
}

void LibraryService::setCoverDialog(SDL_Window* parent, const lib::Entry& e) {
    static const SDL_DialogFileFilter filters[] = {{"PNG images", "png"}};
    SDL_ShowOpenFileDialog(&LibraryService::coverDone, new CoverDialog{this, lib::coverKey(e)}, parent, filters, 1, nullptr,
                           false);
}

// ---- box art pipeline ------------------------------------------------------------------------------------------------------

// Decode thread: a cover on disk (set on the tile, then the local art folder) -> pixels; otherwise a cached
// download; otherwise, for an identified title, hand the download to the net thread.
void LibraryService::decodeJob(const ArtJob& job) {
    if (cancel_.load()) return;
    if (!wanted(job.key)) {
        std::lock_guard<std::mutex> g(m_);
        auto it = slots_.find(job.key);
        if (it != slots_.end() && it->second.state == ArtView::Loading) it->second.state = ArtView::None;
        return;
    }
    for (const std::string& path : lib::localCoverPaths(root_, job.key, job.title, job.datName)) {
        if (!pathExists(path)) continue;
        std::vector<unsigned char> bytes;
        Decoded d;
        d.key = job.key;
        d.gen = job.gen;
        if (readArtFile(path, bytes) && decodePng(bytes, d.w, d.h, d.rgba)) {   // a bad file of the user's is skipped, never deleted
            publish(std::move(d));
            return;
        }
    }
    const std::string& key = job.datName;   // the libretro-thumbnails name; empty = never on the network
    bool transient = false;
    if (!key.empty()) {
        // Sources in order (libretro-thumbnails, then for a disc the site mirror).  Each has its own cache folder and
        // its own miss markers, so mirror art never hides upstream art: when upstream's marker expires and it
        // finally has the picture, that copy is cached first in line and wins from then on.
        const std::vector<lib::ArtSource> sources = lib::artSources(job.section, artBaseUrl, artMirrorUrl);
        for (const char* type : lib::kThumbTypes) {
            bool sourceDown = false;   // an earlier source failed transiently: use caches, but let no later source answer in its place
            for (size_t si = 0; si < sources.size(); si++) {
                const lib::ArtSource& src = sources[si];
                std::string cache = lib::thumbnailCachePath(root_, type, key, src.system);
                if (pathExists(cache)) {
                    std::vector<unsigned char> bytes;
                    Decoded d;
                    d.key = job.key;
                    d.gen = job.gen;
                    if (readFile(cache, bytes) && decodePng(bytes, d.w, d.h, d.rgba)) {
                        publish(std::move(d));
                        return;
                    }
                    SDL_RemovePath(cache.c_str());   // corrupt or partial: fetch it again next time
                }
                if (sourceDown) continue;
                int64_t missTime = 0;
                std::string miss = lib::thumbnailMissPath(root_, type, key, src.system);
                if (pathExists(miss, &missTime)) {
                    SDL_Time nowNs = 0;
                    SDL_GetCurrentTime(&nowNs);
                    if ((int64_t)(nowNs / 1000000000LL) - missTime < src.missRetrySeconds) continue;
                    SDL_RemovePath(miss.c_str());
                }
                if (!downloadArt || !http::available()) continue;
                {
                    std::lock_guard<std::mutex> g(m_);
                    auto f = netFail_.find(key + "|" + type + "|" + std::to_string(si));
                    if (f != netFail_.end() && nowSec() - f->second < kRetryAfterFailure) {
                        transient = true;
                        sourceDown = true;
                        continue;
                    }
                }
                const std::string t = type;
                netW_.post([this, job, t, si]() { fetchJob(job, t, si); });
                return;
            }
        }
    }
    Decoded none;
    none.key = job.key;
    none.gen = job.gen;
    none.transientFail = transient;
    publish(std::move(none));
}

// Net thread: one download.  Always ends by asking the decode thread to look again.
void LibraryService::fetchJob(const ArtJob& job, const std::string& type, size_t source) {
    if (cancel_.load()) return;
    if (!wanted(job.key)) {
        std::lock_guard<std::mutex> g(m_);
        auto it = slots_.find(job.key);
        if (it != slots_.end() && it->second.state == ArtView::Loading) it->second.state = ArtView::None;
        return;
    }
    const std::string& key = job.datName;
    const std::vector<lib::ArtSource> sources = lib::artSources(job.section, artBaseUrl, artMirrorUrl);
    if (source >= sources.size()) {   // the mirror was switched off since the job was queued
        if (!cancel_.load()) decodeW_.post([this, job]() { decodeJob(job); });
        return;
    }
    const lib::ArtSource& src = sources[source];
    netJobs_++;
    http::Response r;
    bool ok = http::get(lib::thumbnailUrl(type, key, src.base, src.system), r, kMaxArtBytes, &cancel_);
    std::string cache = lib::thumbnailCachePath(root_, type, key, src.system);
    std::string dir = cache.substr(0, cache.find_last_of('/') + 1);
    if (ok && r.status == 200 && isPng(r.body)) {
        SDL_CreateDirectory(dir.c_str());
        writeFileAtomic(cache, r.body.data(), r.body.size());
    } else if (ok && r.status == 404) {
        SDL_CreateDirectory(dir.c_str());
        writeFileAtomic(lib::thumbnailMissPath(root_, type, key, src.system), "", 0);
    } else {
        {
            std::lock_guard<std::mutex> g(m_);
            netFail_[key + "|" + type + "|" + std::to_string(source)] = nowSec();
        }
        if (!cancel_.load())
            setStatus(std::string("Box art download failed: ") +
                      (ok ? "HTTP " + std::to_string(r.status) : r.error));
    }
    netJobs_--;
    if (!cancel_.load()) decodeW_.post([this, job]() { decodeJob(job); });
}

// ---- per frame ---------------------------------------------------------------------------------------------------------------

void LibraryService::pump() {
    if (!started_) return;
    std::vector<std::string> picked;
    std::vector<std::pair<std::string, std::string>> covers;
    std::vector<Decoded> dec;
    std::vector<lib::ScanFile> scan;
    bool haveScan = false, dats = false;
    {
        std::lock_guard<std::mutex> g(m_);
        picked.swap(pickedFolders_);
        covers.swap(pickedCovers_);
        if (scanResultReady_) {
            scan.swap(scanResult_);
            scanResultReady_ = false;
            haveScan = true;
        }
        dats = datsChanged_;
        datsChanged_ = false;
        dec.swap(decoded_);
    }
    for (const std::string& p : picked) addFolder(p);
    for (const auto& c : covers) setCoverKey(c.first, c.second);
    if (dats) loadDats();
    if (haveScan) files_ = std::move(scan);
    if (dats || haveScan) {
        reidentify();
        if (haveScan) {
            char buf[160];
            size_t bios = 0;
            for (const lib::Entry& en : entries_)
                if (en.section == lib::Section::Bios) bios++;
            std::snprintf(buf, sizeof buf, "%zu games, %zu identified, %zu BIOS / system files",
                          entries_.size() - bios, identified_, bios);
            // keep an error message from a download visible; overwrite only the neutral ones
            std::string cur = status();
            if (cur.empty() || cur.find("failed") == std::string::npos) setStatus(buf);
        }
    }

    // GL upload, a few per frame so scrolling a page of new art never stalls a frame.
    size_t used = 0;
    for (; used < dec.size() && used < (size_t)kUploadsPerFrame; used++) {
        Decoded& d = dec[used];
        std::lock_guard<std::mutex> g(m_);
        auto it = slots_.find(d.key);
        if (it == slots_.end()) continue;
        Slot& s = it->second;
        if (d.gen != s.gen) continue;   // a cover was set or removed since this job started: a fresh job follows
        if (d.rgba.empty() || !createTexture) {
            s.state = ArtView::Missing;
            s.retryAt = d.transientFail ? nowSec() + kRetryAfterFailure : 0;
        } else {
            if (s.tex) {
                destroyTexture(s.tex);
                textureCount_--;
            }
            s.tex = createTexture(d.rgba.data(), d.w, d.h);
            s.w = d.w;
            s.h = d.h;
            s.state = ArtView::Ready;
            textureCount_++;
        }
    }
    if (used < dec.size()) {
        std::lock_guard<std::mutex> g(m_);
        decoded_.insert(decoded_.begin(), std::make_move_iterator(dec.begin() + (long)used),
                        std::make_move_iterator(dec.end()));
    }

    // Keep VRAM bounded: evict the least recently shown textures (re-decoded from disk on demand).
    if (textureCount_ > kMaxTextures) {
        std::lock_guard<std::mutex> g(m_);
        std::vector<std::pair<double, std::string>> order;
        for (auto& kv : slots_)
            if (kv.second.tex) order.emplace_back(kv.second.lastWanted, kv.first);
        std::sort(order.begin(), order.end());
        double t = nowSec();
        for (size_t i = 0; i < order.size() && textureCount_ > kMaxTextures * 3 / 4; i++) {
            Slot& s = slots_[order[i].second];
            if (t - s.lastWanted < 1.0) break;   // everything left is on screen
            destroyTexture(s.tex);
            s.tex = 0;
            s.state = ArtView::None;
            textureCount_--;
        }
    }
}

}  // namespace vj
