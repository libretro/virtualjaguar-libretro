#include "ra_images.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>

#include "http.h"
#include "paths.h"

namespace vj {
namespace ra {

namespace {

// [A-Za-z0-9.-] stay, everything else (a separator, '%', '?', '..' tricks) becomes '_'.
std::string safe(const std::string& s) {
    std::string o;
    for (unsigned char c : s) o += (std::isalnum(c) || c == '.' || c == '-') ? (char)c : '_';
    return o;
}

uint32_t fnv1a(const std::string& s) {
    uint32_t h = 2166136261u;
    for (unsigned char c : s) {
        h ^= c;
        h *= 16777619u;
    }
    return h;
}

// PNG bytes -> RGBA, long side <= maxPx.  SDL surfaces only: safe on the worker thread.
bool decode(const unsigned char* data, size_t size, int maxPx, int& outW, int& outH, std::vector<unsigned char>& rgba) {
    SDL_IOStream* io = SDL_IOFromConstMem(data, size);
    if (!io) return false;
    SDL_Surface* s = SDL_LoadPNG_IO(io, true);
    if (!s) return false;
    if (s->w < 1 || s->h < 1 || s->w > 4096 || s->h > 4096) {
        SDL_DestroySurface(s);
        return false;
    }
    const int longSide = std::max(s->w, s->h);
    if (longSide > maxPx) {
        const int tw = std::max(1, (int)((int64_t)s->w * maxPx / longSide));
        const int th = std::max(1, (int)((int64_t)s->h * maxPx / longSide));
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

bool defaultFetch(const std::string& url, std::string& body, std::string& err, const std::atomic<bool>* cancel,
                  size_t maxBytes) {
    http::Response r;
    if (!http::get(url, r, maxBytes, cancel)) {
        err = r.error.empty() ? "download failed" : r.error;
        return false;
    }
    if (r.status != 200) {
        err = "HTTP " + std::to_string(r.status);
        return false;
    }
    body.assign((const char*)r.body.data(), r.body.size());
    return true;
}

}  // namespace

std::string imageCachePath(const std::string& root, const std::string& url, const std::string& version) {
    size_t p = 0;
    if (url.compare(0, 8, "https://") == 0) p = 8;
    else if (url.compare(0, 7, "http://") == 0) p = 7;
    else return "";
    size_t slash = url.find('/', p);
    if (slash == std::string::npos || slash + 1 >= url.size()) return "";
    std::string host = url.substr(p, slash - p);
    std::string path = url.substr(slash + 1);
    std::string query;
    size_t q = path.find_first_of("?#");
    if (q != std::string::npos) {
        query = path.substr(q);
        path.erase(q);
    }
    if (host.empty() || path.empty()) return "";
    // The extension stays out of the name so "a.png" and "a.jpg" cannot differ only by it; the
    // file is always re-encoded as PNG by the server, and anything that is not a PNG is refused.
    size_t dot = path.rfind('.');
    if (dot != std::string::npos && path.size() - dot <= 5) path.erase(dot);
    std::string name = safe(host) + "_" + safe(path);
    if (!query.empty()) {
        char h[16];
        std::snprintf(h, sizeof h, "_q%08x", fnv1a(query));
        name += h;
    }
    if (!version.empty()) name += "_v" + safe(version);
    if (name.size() > 120) {   // a pathological URL: keep the head and a hash of the whole
        char h[16];
        std::snprintf(h, sizeof h, "_%08x", fnv1a(name));
        name = name.substr(0, 100) + h;
    }
    return root + "ra-cache/" + name + ".png";
}

bool looksLikePng(const void* data, size_t size) {
    static const unsigned char sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    return size >= 8 + 8 + 13 && std::memcmp(data, sig, 8) == 0;
}

size_t pruneImageDir(const std::string& dir, uint64_t maxBytes) {
    struct F {
        std::string path;
        uint64_t size;
        SDL_Time mtime;
    };
    std::vector<F> files;
    struct Ctx {
        std::string dir;
        std::vector<F>* out;
    } ctx{dir, &files};
    SDL_EnumerateDirectory(
        dir.c_str(),
        [](void* ud, const char*, const char* name) -> SDL_EnumerationResult {
            Ctx* c = (Ctx*)ud;
            const size_t n = std::strlen(name);
            if (n < 5 || std::strcmp(name + n - 4, ".png") != 0) return SDL_ENUM_CONTINUE;   // games-*.tsv etc. stay
            const std::string p = c->dir + name;
            SDL_PathInfo info;
            if (SDL_GetPathInfo(p.c_str(), &info) && info.type == SDL_PATHTYPE_FILE)
                c->out->push_back(F{p, (uint64_t)info.size, info.modify_time});
            return SDL_ENUM_CONTINUE;
        },
        &ctx);
    uint64_t total = 0;
    for (const F& f : files) total += f.size;
    if (total <= maxBytes) return 0;
    std::sort(files.begin(), files.end(), [](const F& a, const F& b) { return a.mtime < b.mtime; });
    size_t gone = 0;
    for (const F& f : files) {
        if (total <= maxBytes) break;
        if (SDL_RemovePath(f.path.c_str())) {
            total -= f.size;
            gone++;
        }
    }
    return gone;
}

// ---- ImageCache --------------------------------------------------------------------------

ImageCache::ImageCache() {}
ImageCache::~ImageCache() { stop(); }

double ImageCache::nowSeconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
int64_t ImageCache::nowMs() {
    return (int64_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

void ImageCache::start(const std::string& root, const Options& opt) {
    stop();
    root_ = root;
    opt_ = opt;
    cancel_ = false;
    SDL_CreateDirectory((root_ + "ra-cache").c_str());
    worker_.start();
    started_ = true;
    const std::string dir = root_ + "ra-cache/";
    const uint64_t cap = opt_.maxDiskBytes;
    worker_.post([dir, cap] { pruneImageDir(dir, cap); });
}

void ImageCache::stop() {
    cancel_ = true;   // a transfer in flight ends at once
    worker_.stop();
    if (destroyTexture)
        for (auto& kv : slots_)
            if (kv.second.view.tex) destroyTexture(kv.second.view.tex);
    slots_.clear();
    textures_ = 0;
    {
        std::lock_guard<std::mutex> g(mu_);
        done_.clear();
    }
    started_ = false;
}

ImageView ImageCache::get(const std::string& url, const std::string& version) {
    if (!started_ || !createTexture) return ImageView();   // headless: nothing to show
    const std::string path = imageCachePath(root_, url, version);
    if (path.empty()) {
        ImageView v;
        v.state = ImageView::Missing;
        return v;
    }
    Slot& s = slots_[path];
    if (s.path.empty()) {
        s.path = path;
        s.url = url;
        s.wantedMs = std::make_shared<std::atomic<int64_t>>(0);
    }
    s.lastUsed = frame_;
    s.wantedMs->store(nowMs());
    if (s.view.state == ImageView::Missing) {
        const bool retryOffline = s.missOffline && network_;
        if (retryOffline || (!s.missOffline && nowSeconds() - s.missAt > opt_.retrySeconds)) {
            s.view.state = ImageView::None;
        }
    }
    if (s.view.state == ImageView::None) request(s);
    return s.view;
}

void ImageCache::request(Slot& s) {
    s.view.state = ImageView::Loading;
    const std::string path = s.path, url = s.url;
    auto wanted = s.wantedMs;
    worker_.post([this, path, url, wanted] { job(path, url, wanted); });
}

void ImageCache::job(const std::string& path, const std::string& url, std::shared_ptr<std::atomic<int64_t>> wanted) {
    Done d;
    d.path = path;
    size_t n = 0;
    void* mem = SDL_LoadFile(path.c_str(), &n);
    std::string bytes;
    if (mem) {
        bytes.assign((const char*)mem, n);
        SDL_free(mem);
        if (!looksLikePng(bytes.data(), bytes.size())) bytes.clear();   // a damaged cache file: fetch again
    }
    if (bytes.empty()) {
        if (!network_) {
            d.offlineMiss = true;
        } else if (nowMs() - wanted->load() > (int64_t)(opt_.wantedSeconds * 1000)) {
            d.dropped = true;   // scrolled away while it waited
        } else if (!cancel_) {
            std::string body, err;
            fetches_++;
            const bool ok = fetch ? fetch(url, body, err) : defaultFetch(url, body, err, &cancel_, opt_.maxFileBytes);
            if (ok && body.size() <= opt_.maxFileBytes && looksLikePng(body.data(), body.size())) {
                writeFileAtomic(path, body.data(), body.size());   // best effort: a failed write costs a refetch
                bytes.swap(body);
            }
        }
    }
    if (!bytes.empty())
        d.ok = decode((const unsigned char*)bytes.data(), bytes.size(), opt_.maxPx, d.w, d.h, d.rgba);
    std::lock_guard<std::mutex> g(mu_);
    done_.push_back(std::move(d));
}

void ImageCache::pump() {
    frame_++;
    std::vector<Done> ready;
    {
        std::lock_guard<std::mutex> g(mu_);
        const size_t take = std::min(done_.size(), (size_t)std::max(1, opt_.uploadsPerPump));
        ready.assign(std::make_move_iterator(done_.begin()), std::make_move_iterator(done_.begin() + take));
        done_.erase(done_.begin(), done_.begin() + take);
    }
    for (Done& d : ready) {
        auto it = slots_.find(d.path);
        if (it == slots_.end()) continue;   // evicted by stop()
        Slot& s = it->second;
        if (d.dropped) {
            s.view.state = ImageView::None;
            continue;
        }
        if (d.ok && createTexture) {
            const unsigned t = createTexture(d.rgba.data(), d.w, d.h);
            if (t) {
                s.view.state = ImageView::Ready;
                s.view.tex = t;
                s.view.w = d.w;
                s.view.h = d.h;
                textures_++;
                continue;
            }
        }
        s.view.state = ImageView::Missing;
        s.missAt = nowSeconds();
        s.missOffline = d.offlineMiss;
    }
    evict();
}

void ImageCache::evict() {
    if (textures_ <= opt_.maxTextures) return;
    std::vector<Slot*> ready;
    for (auto& kv : slots_)
        if (kv.second.view.state == ImageView::Ready && kv.second.lastUsed + 1 < frame_) ready.push_back(&kv.second);
    std::sort(ready.begin(), ready.end(), [](const Slot* a, const Slot* b) { return a->lastUsed < b->lastUsed; });
    const size_t target = std::max<size_t>(1, opt_.maxTextures * 3 / 4);
    for (Slot* s : ready) {
        if (textures_ <= target) break;
        if (destroyTexture) destroyTexture(s->view.tex);
        s->view = ImageView();   // back to None: asking again reloads it from disk
        textures_--;
    }
}

}  // namespace ra
}  // namespace vj
