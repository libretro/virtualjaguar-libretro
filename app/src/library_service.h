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
#include "library_lists.h"

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
    void start(const Paths& paths, Config& cfg, bool showIfConfigured);   // paths.system is the BIOS folder
    void stop();                    // joins the workers and frees GL textures; GL context must be current

    // ---- once per UI frame (main thread, GL current) ----
    void pump();

    // ---- GL, injected so the pipeline is testable without a context (app.cpp binds VideoOut's) ----
    std::function<unsigned(const void* rgba, int w, int h)> createTexture;
    std::function<void(unsigned)> destroyTexture;
    // Where art and database updates come from (tests point these at a loopback server).
    std::string artBaseUrl, dbBaseUrl;
    // Second source for disc art, tried after artBaseUrl misses: the project site, which hosts the Jaguar CD
    // set until libretro-thumbnails has one.  Config key library.art_mirror ("" = off); the tests point it
    // at a loopback server.
    std::string artMirrorUrl;

    // ---- state the UI reads ----
    bool show = false;
    bool hideOnLaunch = true;
    bool focusSearch = false;   // UI: put the cursor in the search box on the next frame (Ctrl/Cmd+L on the home screen)
    std::atomic<bool> downloadArt{true};   // UI writes, the decode worker reads
    float cellScale = 1.0f;     // tile size multiplier, lib::clampTileScale range
    // Zoom (pinch, Ctrl/Cmd+wheel, Ctrl/Cmd +/-/0): the grid publishes the frame it was under the pointer, the
    // app's pinch handler zooms only then; changes are written to the config once they settle.
    int gridHoverFrame = -1000;
    void zoomChanged();         // a zoom step happened: schedule the config write
    void flushZoom();           // UI, every frame: writes the config once the zoom has settled
    int section = 0;            // lib::Section shown (Cartridges, Jaguar CD, BIOS); remembered in config
    std::string systemDir;      // the core's system folder, for BIOS files and the notes on them
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
    // Copies a BIOS found in a library folder into the core's system folder under the name the core
    // searches for (lib::biosInstallName), never over an existing file.  The result goes to status().
    bool installBios(const lib::Entry& e);
    void saveSettings();

    // Art for an entry: starts loading on first use, returns what is ready now.  Every entry has a slot (the
    // key is lib::coverKey): a cover set on the tile or a PNG in <data>/covers/ shows for unidentified discs too;
    // only identified titles ever reach the network.
    ArtView art(const lib::Entry& e);

    // ---- custom covers (PNG, copied into <data>/covers/custom/) ----
    bool setCover(const lib::Entry& e, const std::string& imagePath);
    void removeCover(const lib::Entry& e);
    bool hasCustomCover(const lib::Entry& e) const;
    void setCoverDialog(SDL_Window* parent, const lib::Entry& e);   // async; the choice arrives through pump()
    std::string coversDir() const { return root_ + "covers/"; }
    // The visible tiles of the last drawn grid, so a file dropped on the window can find the tile under it.
    struct CoverTile {
        float x0, y0, x1, y1;
        std::string key;
        std::string title;
        std::string datName;
    };
    std::vector<CoverTile> tiles;   // UI thread only
    int tilesFrame = -1000;         // the UI frame that filled `tiles`
    const CoverTile* tileAt(float x, float y, int frame) const;
    // A cover set from a drop or the dialog by key (what the tile record carries).
    bool setCoverKey(const std::string& key, const std::string& imagePath);

    // ---- favorites and recently played (library_lists.h; the file and the several-instances rule are explained there) ----
    // Where each shows: lib::ShowAs (hidden / its own tab / strips on the Cartridges + CD tabs / both).  Settings:
    // library.favorites.show and library.recents.show.
    int favoritesShow = lib::kFavoritesDefault;
    int recentsShow = lib::kRecentsDefault;
    bool isFavorite(const lib::Entry& e) const;
    void setFavorite(const lib::Entry& e, bool on);
    bool isRecent(const lib::Entry& e) const;
    void removeRecent(const lib::Entry& e);
    void clearRecents();
    // App::openContent calls this for every game that booted, from the library, a drop, File > Open or the
    // command line: it goes to the front of the recents when it is one of the library's games (or the library has
    // not been scanned yet, so it cannot be told).
    void noteLaunched(const std::string& absPath);
    // The library entries behind the lists, resolved against what the last scan found: favorites by title,
    // recents newest first.  A game whose file is gone (unplugged drive) is skipped, not forgotten.  The
    // reference stays valid until the next call (the lists are rebuilt only when they or the scan changed).
    const std::vector<lib::Resolved>& favoriteEntries();
    const std::vector<lib::Resolved>& recentEntries();
    // The tab to show for the remembered one, given the display options.
    int activeTab() const { return lib::effectiveTab(section, favoritesShow, recentsShow); }

    // ---- save states (the Save states tab, the tile menu's Load state) ----
    // Every game with states in the states folder, newest first.  Re-listed at most once a second (and at once
    // after the library deletes one).
    const std::vector<lib::StateGame>& stateGames();
    const lib::StateGame* statesFor(const lib::Entry& e);   // by the file stem the states are named with
    const lib::Entry* entryForState(const lib::StateGame& g) const { return lib::entryForStem(entries_, g.stem); }
    void invalidateStates() { statesAt_ = -1000.0; }
    ArtView stateThumb(const lib::StateInfo& s);            // the screenshot saved with the state
    bool deleteState(const lib::StateInfo& s);
    // Boot `e` and load its state from `slot` (App handles the event: same game running = just load).
    void launchState(const lib::Entry& e, int slot);
    // How many running copies this is (Paths::instance): shown in the settings help, nothing else changes.
    int instance = 1;

    // Where things live, for the UI and for tests.
    std::string libDir() const { return libDir_; }
    std::string statesDir() const { return statesDir_; }
    std::string userFile() const { return libDir_ + "user.tsv"; }

private:
    struct Slot {
        ArtView::State state = ArtView::None;
        unsigned tex = 0;
        int w = 0, h = 0;
        double lastWanted = 0;       // time of the last art() call (monotonic seconds)
        unsigned gen = 0;            // bumped when a cover is set or removed: results of an older job are dropped
        double retryAt = 0;          // transient failure: ask again after this time (monotonic seconds)
    };
    struct ArtJob {                  // what a decode / fetch job needs to know about its tile
        std::string key, title, datName;
        lib::Section section = lib::Section::Cart;
        unsigned gen = 0;
    };
    struct Decoded {
        std::string key;
        unsigned gen = 0;
        int w = 0, h = 0;
        bool transientFail = false;  // nothing cached and the network failed (not a 404)
        std::vector<unsigned char> rgba;   // empty = no art
    };

    void loadDats();
    void reidentify();
    void saveCache(const std::vector<lib::ScanFile>& files);
    void loadCache();
    void decodeJob(const ArtJob& job);
    void fetchJob(const ArtJob& job, const std::string& type, size_t source);
    bool wanted(const std::string& key);
    void invalidateArt(const std::string& key);   // main thread: forget what the slot holds, load again
    void setStatus(const std::string& s);
    void publish(Decoded&& d);
    double nowSec() const;

    // paths/config (set in start)
    std::string root_, libDir_, dbDir_, cachePath_, bundledDir_, statesDir_;
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
    std::vector<std::pair<std::string, std::string>> pickedCovers_;   // (tile key, image path), dialog thread -> main
    bool datsChanged_ = false;
    std::string status_;
    uint64_t zoomSaveAt_ = 0;   // SDL ticks (ms) when to write library.size, 0 = nothing pending
    std::map<std::string, double> netFail_;   // "key|type|source" -> time of last transient failure
    std::atomic<bool> cancel_{false};
    std::atomic<bool> scanCancel_{false};
    std::atomic<bool> scanning_{false};
    std::atomic<size_t> scanDone_{0}, scanTotal_{0};
    std::atomic<int> netJobs_{0};

    // personal lists
    lib::UserStore user_;
    unsigned entriesGen_ = 1;                  // bumped when entries_ is rebuilt
    unsigned resolvedUserGen_ = 0, resolvedEntriesGen_ = 0;
    std::vector<lib::Resolved> favResolved_, recResolved_;
    double userCheckAt_ = 0;
    void resolveLists();
    // save states
    std::vector<lib::StateGame> stateGames_;
    double statesAt_ = -1000.0;
    struct ThumbSlot {
        ArtView::State state = ArtView::None;
        unsigned tex = 0;
        int w = 0, h = 0;
        int64_t mtimeNs = 0;         // of the state file this was made from
        double lastWanted = 0;
    };
    std::map<std::string, ThumbSlot> stateThumbs_;   // by state path; main thread only
    int thumbLoads_ = 0;                             // decoded this frame (reset by pump)

    Worker scanW_, decodeW_, netW_;
    static void SDLCALL folderDone(void* ud, const char* const* list, int filter);
    static void SDLCALL coverDone(void* ud, const char* const* list, int filter);
};


class App;
void drawLibraryListOptions(LibraryService& svc);   // library_ui.cpp: where Favorites / Recently played show (also in Settings)
void drawLibrary(App& app);   // library_ui.cpp: the Game library window (also pumps the service)

}  // namespace vj
