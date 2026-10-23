// Community cheat store (#1071): fetches the project site's cheat manifest, installs one cartridge's pack after
// verifying it, tracks per-pack state and feeds CheatManager.  No SDL, ImGui or GL: the network and the worker
// thread are injected hooks, so every path runs synchronously and offline in app/tests/cheats_test.cpp.
// Design: docs/cheat-store-design.md "App" sections 1, 3, 4.
//
// Storage (communityDir):
//   manifest.json        the last manifest fetched, verbatim bytes (cap 256 KiB)
//   packs/<CRC>.cht      installed pack, bytes exactly as served (cap 64 KiB); <CRC> = %08X, upper case
//   packs/<CRC>.json     the manifest's packs[i] object recorded at install (what the UI shows offline)
//
// Threading.  Every method is main-thread only.  Network work is handed to `run` as a closure that touches
// nothing but a shared hand-off block (not the store), calls `fetch`, verifies in memory, and queues a result;
// pump() applies results on the main thread (writes the files, updates state, asks CheatManager to reload).
// At most one request is in flight (busy()).  Nothing is written by the closure, and a failure anywhere leaves
// the previous install untouched.
//
// What the app (Task 10) must wire and call:
//   fetch, run, downloadsAllowed        once, at init (see the members); setBaseUrl() if cheats.community.url is set
//   setDir(<data>/cheats/community/)    once; also cm.setCommunityDir(store.packsDir())
//   windowOpened(crc, cfg)              when the Cheats window becomes visible (cheats.community.checked gates it)
//   checkNow() / install(crc)           the "Check for updates" / Install / Update buttons
//   remove(crc, cm)                     the Remove button
//   pump(cm)                            once per UI frame, before drawing the section
//   gameLoaded(crc, cm)                 right after cm.gameLoaded(id) on every game load (cm.gameClosed() ->
//                                       store.gameClosed()), and afterApply(cm) after cheat toggles (see below)
//   stop()                              app shutdown, after the worker is told to stop and before cheats are destroyed
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace vj {

class CheatManager;
class Config;

constexpr const char* kCheatStoreBase = "https://jaguar.provenance-emu.com/cheats/";
constexpr size_t kCheatManifestMaxBytes = 256 * 1024;
constexpr size_t kCheatPackMaxBytes = 64 * 1024;
constexpr int64_t kCheatCheckIntervalSeconds = 6 * 60 * 60;
constexpr const char* kCheatCheckedKey = "cheats.community.checked";   // unix seconds, stored as a decimal string

// One manifest "packs" entry.  Hex fields are normalised (crc32 upper-case, sha256 lower-case).
struct PackMeta {
    std::string crc32, title, name, file, sha256, updated, credit;
    unsigned bytes = 0, cheats = 0, writes = 0;
    bool partial = false, exceedsCoreLimit = false;
    std::vector<std::pair<std::string, std::string>> notes;   // setting (may be empty), reason
    std::vector<std::string> messages;                        // the source script's on-load messages
    std::string json;                                         // the packs[i] object, dumped (the sidecar)
};

// Parse manifest text.  Format 1 only; a pack entry missing crc32|file|sha256|bytes (or not an object) makes the
// whole manifest "not understood"; an entry with a bad hex field, bad byte count or unsafe `file` is skipped on
// its own, and so is a repeated CRC.  `out` is only filled on success; `err` is a status line otherwise.
bool parseManifest(const std::string& text, std::vector<PackMeta>& out, std::string& err);
// The same entry parser for a stored sidecar (the packs[i] object).
bool parsePackMeta(const std::string& text, PackMeta& out);
// size == meta.bytes, sha256 == meta.sha256, >= 1 cheat, vj_rom_crc32 guard present, valid and == crc.
// `err` is the status line on failure.
bool verifyPack(const PackMeta& meta, uint32_t crc, const std::string& body, std::string& err);

enum class PackState {
    NoGame,            // the store has not been told of a game
    Disc,              // crc 0: packs cover cartridges only
    DownloadsOff,      // not installed and downloads are off
    Checking,          // not installed, no manifest yet, a manifest fetch is in flight
    NotOffered,        // not installed, the store has nothing for this CRC
    Offered,           // not installed, the store offers a pack
    Installed,         // installed and current
    UpdateAvailable,   // manifest sha256 differs from the installed sidecar's (or the sidecar is missing)
    NoLongerOffered,   // installed, but a loaded manifest no longer lists it (installed copy kept)
    EditedLocally,     // the .cht on disk hashes differently from its sidecar; Update replaces it
};

class CheatStore {
public:
    CheatStore();
    ~CheatStore() { stop(); }
    CheatStore(const CheatStore&) = delete;
    CheatStore& operator=(const CheatStore&) = delete;

    // ---- hooks (set once) ----
    // GET `url`, at most `maxBytes`; false with `err` set on a transport error, an oversize body, or a status other
    // than 200.  Runs on the `run` thread.  Null = no network.  The app's version should pass cancelFlag() to http::get.
    std::function<bool(const std::string& url, size_t maxBytes, std::string& body, std::string& err)> fetch;
    // Runs a closure off the main thread.  Null = run it inline (unit tests).
    std::function<void(std::function<void()>)> run;
    // The one downloads switch (library.art) AND http::available().  Null = off: nothing is ever fetched.
    std::function<bool()> downloadsAllowed;
    // Unix seconds.  Null = std::time.
    std::function<int64_t()> now;
    // Called with "<pack title>: <message>" (see gameLoaded / afterApply).  Null = silent.
    std::function<void(const std::string&)> message;

    void setDir(const std::string& communityDir);      // creates nothing until the first install; reads the cached manifest
    void setBaseUrl(const std::string& url);           // "" = kCheatStoreBase; a missing trailing '/' is added
    const std::string& dir() const { return dir_; }
    std::string packsDir() const;                      // <dir>/packs  (give it to CheatManager::setCommunityDir)

    // ---- requests ----
    // The Cheats window became visible for this game.  Refreshes the pack's on-disk state, and starts a manifest
    // fetch when crc != 0, downloads are allowed, nothing is in flight and the last good check is >= 6 h old
    // (or the cache is empty).  `cfg` (kept, must outlive the store's use) holds cheats.community.checked.
    void windowOpened(uint32_t crc, Config& cfg);
    // "Check for updates": fetch the manifest now, ignoring the interval.  No-op for discs, with downloads off,
    // or while a request is in flight.
    void checkNow();
    // Fetch, verify and install the offered pack for `crc` (also the Update).  False, with status() set, when it
    // cannot start (downloads off, nothing offered, busy); the outcome arrives through pump().
    bool install(uint32_t crc);
    // Delete the pack and its sidecar.  For the current game also clears its community toggles and reloads.
    // False when nothing was installed or the file could not be deleted.
    bool remove(uint32_t crc, CheatManager& cm);
    // Main thread, once per frame: apply finished requests.
    void pump(CheatManager& cm);
    // Cancel what is in flight (cancelFlag() turns true), wait for started closures, drop their results.
    void stop();
    const std::atomic<bool>& cancelFlag() const;

    // ---- state (valid until the next pump / windowOpened / install / remove) ----
    PackState state(uint32_t crc) const;
    const PackMeta* offered(uint32_t crc) const;      // from the loaded manifest
    const PackMeta* installed(uint32_t crc) const;    // from the sidecar; null when absent (even if the .cht exists)
    const std::string& status() const { return status_; }
    bool busy() const { return busy_; }               // a request is in flight or its result is not pumped yet
    bool manifestLoaded() const { return manifestLoaded_; }

    // ---- pack messages (the source script's on-load notes, e.g. "This version of Iron Soldier 1 IS supported.") ----
    // Each message is shown ONCE per game load, when the game's community pack has at least one cheat enabled and
    // cheats can reach the core (cm.blockedNow() is false).  gameLoaded() starts a new load (call it right after
    // cm.gameLoaded(id)); afterApply() covers cheats turned on later in the same load and is cheap to call often:
    // both emit at most once per load.
    void gameLoaded(uint32_t crc, const CheatManager& cm);
    void afterApply(const CheatManager& cm);
    void gameClosed();                                // forget the game (state() -> NoGame) and the load

private:
    struct Shared;
    struct Installed {
        bool present = false;    // <CRC>.cht exists
        bool hasMeta = false;    // the sidecar parsed
        bool edited = false;     // the .cht's sha256 differs from the sidecar's
        PackMeta meta;
    };
    const Installed& info(uint32_t crc) const;
    void refresh(uint32_t crc);
    void dispatch(std::function<void()> job);
    void startManifestFetch();
    void announce(const CheatManager& cm);
    std::string chtPath(uint32_t crc) const;
    std::string sidecarPath(uint32_t crc) const;

    std::string dir_, base_ = kCheatStoreBase;
    std::string status_;
    std::map<uint32_t, PackMeta> manifest_;
    bool manifestLoaded_ = false;
    mutable std::map<uint32_t, Installed> installed_;
    bool busy_ = false, manifestBusy_ = false;
    Config* cfg_ = nullptr;
    uint32_t crc_ = 0;
    bool haveGame_ = false;
    bool announced_ = false;
    std::shared_ptr<Shared> sh_;
};

}  // namespace vj
