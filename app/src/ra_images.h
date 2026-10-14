// Pictures for the RetroAchievements windows (#836): the player's avatar, the game icon and the
// achievement badges.  Nothing here knows about rc_client or ImGui.
//
//   * disk       <data>/ra-cache/<host>_<path>[_v<version>].png.  Badges and game icons never change
//                for a given URL, so a file is kept until the folder outgrows its cap (oldest files go
//                first); the avatar passes a version so a changed picture is fetched once.
//   * memory     PNG decoded (<= maxPx on the long side) and uploaded as a GL texture by pump() on the
//                main thread, a few per frame; the least recently drawn textures are destroyed when
//                the count passes maxTextures and are simply reloaded from disk if asked for again.
//   * network    one worker thread (http::get, so a stalled transfer can be cancelled and never shares
//                a queue with rc_client's own requests).  A request is dropped if nobody asked for it
//                in the last few seconds (a list scrolled past).  setNetwork(false) -- the app's
//                "downloads off" switch, or a mock session -- serves what is on disk and fetches
//                nothing; files that were missing are looked up again once it is switched back on.
//   * failures   a miss is remembered for retrySeconds so a 404 is not requested every frame.
//
// All public calls are for the main thread (the one that owns the GL context).
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "library_service.h"   // Worker

namespace vj {
namespace ra {

// The file a URL is cached in, "" for a URL that cannot be cached (not http(s), or no path).
// `root` ends with a separator.  Pure: no file access.
std::string imageCachePath(const std::string& root, const std::string& url, const std::string& version = "");
// True for a PNG signature followed by at least an IHDR chunk's worth of bytes.
bool looksLikePng(const void* data, size_t size);
// Deletes the oldest *.png files in `dir` until the rest fit in `maxBytes`; returns how many went.
size_t pruneImageDir(const std::string& dir, uint64_t maxBytes);

struct ImageView {
    enum State { None, Loading, Ready, Missing } state = None;
    unsigned tex = 0;
    int w = 0, h = 0;
};

class ImageCache {
public:
    struct Options {
        size_t maxTextures = 256;
        int maxPx = 128;                 // long side after decoding
        int uploadsPerPump = 8;
        double retrySeconds = 300;
        double wantedSeconds = 5;        // a queued download nobody asked for since is dropped
        uint64_t maxDiskBytes = 48u << 20;
        size_t maxFileBytes = 2u << 20;  // a bigger download is not a badge
    };

    ImageCache();
    ~ImageCache();

    // GL, injected like LibraryService's.  Without createTexture (headless) nothing is loaded.
    std::function<unsigned(const void* rgba, int w, int h)> createTexture;
    std::function<void(unsigned)> destroyTexture;
    // Blocking fetch of `url` into `body`; false (with `err`) on any failure or a non-200 status.  The
    // default is http::get.  Tests install a fake.  Runs on the worker thread.
    std::function<bool(const std::string& url, std::string& body, std::string& err)> fetch;

    void start(const std::string& root, const Options& opt);
    void start(const std::string& root) { start(root, Options()); }
    void stop();   // joins the worker and destroys every texture (the GL context must still be current)

    void setNetwork(bool on) { network_ = on; }
    bool network() const { return network_; }

    ImageView get(const std::string& url, const std::string& version = "");
    void pump();   // once per UI frame

    size_t textureCount() const { return textures_; }
    size_t pending() { return worker_.pending(); }
    unsigned fetches() const { return fetches_; }   // downloads attempted (tests)

private:
    struct Slot {
        std::string url, path;
        ImageView view;
        uint64_t lastUsed = 0;
        double missAt = 0;
        bool missOffline = false;   // missed only because the network was off
        std::shared_ptr<std::atomic<int64_t>> wantedMs;
    };
    struct Done {
        std::string path;
        bool ok = false;
        bool offlineMiss = false;
        bool dropped = false;
        int w = 0, h = 0;
        std::vector<unsigned char> rgba;
    };
    void request(Slot& s);
    void job(const std::string& path, const std::string& url, std::shared_ptr<std::atomic<int64_t>> wanted);
    void evict();
    static double nowSeconds();
    static int64_t nowMs();

    std::string root_;
    Options opt_;
    Worker worker_;
    bool started_ = false;
    std::atomic<bool> cancel_{false};
    std::atomic<bool> network_{true};
    std::atomic<unsigned> fetches_{0};
    std::map<std::string, Slot> slots_;   // by cache path
    std::mutex mu_;
    std::vector<Done> done_;
    size_t textures_ = 0;
    uint64_t frame_ = 1;
};

}  // namespace ra
}  // namespace vj
