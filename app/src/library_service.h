// The game library as the app sees it: the folder list, the scanned entries, the dat
// files, and the box art pipeline.  Pure logic is in library.h; this adds threads,
// files and GL textures.
//
// Threads (all started by start(), none in --headless):
//   scan    walks the folders and hashes what changed
//   decode  reads cached art from disk, decodes + downsizes it (SDL_LoadPNG, no GL)
//   net     downloads art and database updates; blocking libcurl/WinHTTP calls live here
// The main thread owns every GL texture and all UI-visible state: workers hand results
// over under a mutex and pump() (called once per UI frame) applies them.  Nothing here
// ever waits on the network from the main thread or the emulation loop.
#pragma once

#include <SDL3/SDL.h>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "library.h"

namespace vj {

class Config;
struct Paths;

// One background thread draining a FIFO of jobs.
class Worker {
public:
    ~Worker() { stop(); }
    void start();
    void post(std::function<void()> job);
    void stop();                  // drops queued jobs, joins
    size_t pending();

private:
    void loop();
    std::thread th_;
    std::mutex m_;
    std::condition_variable cv_;
    std::deque<std::function<void()>> q_;
    bool quit_ = false;
    bool busy_ = false;
};

struct ArtView {
    enum State { None, Loading, Ready, Missing } state = None;
    unsigned tex = 0;
    int w = 0, h = 0;
};

class LibraryService {
public:
    LibraryService();
    ~LibraryService();

    // ---- lifecycle (main thread) ----
    void start(const Paths& paths, Config& cfg, bool showIfConfigured);
    void stop();                    // joins the workers and frees GL textures; GL context must be current

    // ---- once per UI frame (main thread, GL current) ----
    void pump();

    // ---- GL, injected so the pipeline is testable without a context (app.cpp binds VideoOut's) ----
    std::function<unsigned(const void* rgba, int w, int h)> createTexture;
    std::function<void(unsigned)> destroyTexture;
    // Where art and database updates come from (tests point these at a loopback server).
    std::string artBaseUrl, dbBaseUrl;

    // ---- state the UI reads ----
    bool show = false;
    bool hideOnLaunch = true;
    std::atomic<bool> downloadArt{true};   // UI writes, the decode worker reads
    float cellScale = 1.0f;
    const std::vector<lib::Entry>& entries() const { return entries_; }
    const std::vector<std::string>& folders() const { return folders_; }
    bool scanning() const { return scanning_.load(); }
    size_t scanDone() const { return scanDone_.load(); }
    size_t scanTotal() const { return scanTotal_.load(); }
    std::string status();           // "Updated game database", "Art download failed: ...", ...
    size_t identified() const { return identified_; }
    bool haveCartDat() const { return !carts_.empty(); }
    bool haveDiscDat() const { return !discs_.empty(); }
    std::string cartDatInfo() const;
    std::string discDatInfo() const;
    bool netBusy();

    // ---- actions ----
    void addFolder(const std::string& path);
    void removeFolder(size_t index);
    void addFolderDialog(SDL_Window* parent);   // async; the choice arrives through pump()
    void rescan();
    void updateDatabase();                       // download newer dat files (user action)
    void launch(const lib::Entry& e);            // asks the app to open it (a drop-style event)
    void setDownloadArt(bool on);
    void saveSettings();

    // Art for an entry: starts loading on first use, returns what is ready now.
    ArtView art(const lib::Entry& e);

    // Where things live, for the UI and for tests.
    std::string libDir() const { return libDir_; }

private:
    struct Slot {
        ArtView::State state = ArtView::None;
        unsigned tex = 0;
        int w = 0, h = 0;
        double lastWanted = 0;       // time of the last art() call (monotonic seconds)
        double retryAt = 0;          // transient failure: ask again after this time (monotonic seconds)
    };
    struct Decoded {
        std::string key;
        int w = 0, h = 0;
        bool transientFail = false;  // nothing cached and the network failed (not a 404)
        std::vector<unsigned char> rgba;   // empty = no art
    };

    void loadDats();
    void reidentify();
    void saveCache(const std::vector<lib::ScanFile>& files);
    void loadCache();
    void requestArt(const std::string& key);
    void decodeJob(const std::string& key);
    void fetchJob(const std::string& key, const std::string& type);
    bool wanted(const std::string& key);
    void setStatus(const std::string& s);
    void publish(Decoded&& d);
    double nowSec() const;

    // paths/config (set in start)
    std::string root_, libDir_, dbDir_, cachePath_, bundledDir_;
    Config* cfg_ = nullptr;
    bool started_ = false;
    bool glAlive_ = false;

    // state owned by the main thread
    std::vector<std::string> folders_;
    std::vector<lib::ScanFile> files_;
    std::vector<lib::Entry> entries_;
    lib::Dat carts_, discs_;
    size_t identified_ = 0;
    std::map<std::string, Slot> slots_;       // guarded by m_ (workers read lastWanted/state)
    size_t textureCount_ = 0;

    // shared with workers
    std::mutex m_;
    std::vector<Decoded> decoded_;            // worker -> main
    std::vector<lib::ScanFile> scanResult_;   // worker -> main
    bool scanResultReady_ = false;
    std::vector<std::string> pickedFolders_;  // dialog thread -> main
    bool datsChanged_ = false;
    std::string status_;
    std::map<std::string, double> netFail_;   // "key|type" -> time of last transient failure
    std::atomic<bool> cancel_{false};
    std::atomic<bool> scanCancel_{false};
    std::atomic<bool> scanning_{false};
    std::atomic<size_t> scanDone_{0}, scanTotal_{0};
    std::atomic<int> netJobs_{0};

    Worker scanW_, decodeW_, netW_;
    static void SDLCALL folderDone(void* ud, const char* const* list, int filter);
};


class App;
void drawLibrary(App& app);   // library_ui.cpp: the Game library window (also pumps the service)

}  // namespace vj
