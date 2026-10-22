// RetroAchievements (#836), through rcheevos' rc_client.
//
// How the pieces fit:
//   * Memory   rc_client reads the emulated machine through
//              rc_libretro_memory_read() over the memory map the core publishes
//              with RETRO_ENVIRONMENT_SET_MEMORY_MAPS -- the same rcheevos code
//              RetroArch builds, fed the same map, so an address means the same
//              byte there and here.  Nothing is byte-swapped: the Jaguar RAM is
//              a big-endian image and the set authors use the big-endian operand
//              sizes (0xI 16-bit, 0xG 32-bit).
//   * Hashing  rc_client identifies the game itself: the whole cartridge file
//              (console 17), or a .cue disc (console 77, rcheevos' own cue reader).
//   * Network  rc_client hands every request to serverCall(), which gives it to
//              AchHttp (worker thread, completions pumped on the main thread).
//   * Policy   hardcoreActive() is the single yes/no every restricted feature
//              asks.  The App refuses state loads and unrestricted pausing while
//              it is true; rewind, slow motion, frame advance and cheats do not
//              exist in this branch yet / belong to sibling branches and must
//              consult it.
//   * Secrets  only the user name and the login TOKEN are ever stored; the
//              password lives in a text box until the request is built and is then
//              wiped.  The token goes to the OS credential store (secret_store.h);
//              config.ini keeps it only where no store is available.
#pragma once

#include <cstdint>
#include <atomic>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "ach_event.h"
#include "ach_http.h"
#include "ra_dash.h"
#include "ra_images.h"
#include "rc_client.h"
#include "rc_libretro.h"

namespace vj {

class App;
struct CliArgs;

struct AchToast {
    std::string title;
    std::string body;
    double born = 0;
    double life = 5;
    int kind = 0;   // 0 = unlock, 1 = information, 2 = problem
    std::string iconUrl;   // badge / game icon shown beside the text ("" = none); copied, never a pointer into rc_client
};

// What unlocked while this app was running, for the Profile tab.
struct SessionUnlock {
    std::string title, description, badgeUrl;
    unsigned points = 0;
    bool hardcore = false;
    long long when = 0;
};

class Achievements {
public:
    Achievements();
    ~Achievements();

    // ---- lifecycle (App hooks) ------------------------------------------------
    void init(App& app, const CliArgs& args);   // after the config is loaded
    void shutdown();
    void onContentLoaded();      // a game/disc was just loaded into the core
    void onContentClosed();      // unloaded, failed to load, or no-content boot
    void onReset();              // the emulated machine was reset
    void onDiscChanged();        // a disc swap booted another disc (host.contentPath() moved)
    void afterFrame();           // once per emulated frame, right after retro_run (inline: also does emuFrame())
    // The emulation-thread half of afterFrame (#967): rc_client_do_frame, which reads game memory, and the rich-presence
    // line published every 60 frames.  Inline, afterFrame() calls it.
    void emuFrame();
    // The emulation thread's idle while no frame runs (paused, or no game identified): rc_client's pings and retries.
    void emuIdle();
    // The rich-presence line ("" = none), computed with the frame on the emulation thread; safe from the UI thread.
    std::string richPresence() const;
    // read_memory calls answered from the memory map instead of RAM because they ran off the emulation thread.
    unsigned offThreadReads() const { return offThreadReads_.load(); }
    void pump();                 // once per UI-loop iteration, paused or not

    // ---- hardcore transitions -----------------------------------------------------
    // Called (main thread) whenever hardcoreActive() flips.  The cheat manager uses
    // it to empty the core's cheat list on the way in and to put the saved
    // selections back on the way out.
    std::function<void(bool active)> onHardcoreChanged;
    void checkHardcoreTransition();
    // A message for the player that is also kept for the headless tests.
    void notice(const std::string& title, const std::string& body, int kind = 1, double life = 6);

    // ---- policy ----------------------------------------------------------------
    // True while RetroAchievements hardcore rules are in force: signed in,
    // hardcore wanted, no disallowed setting, and a game that is (or is being)
    // looked up.  Everything below is derived from it.
    bool hardcoreActive() const;
    // Would hardcore apply to the next game that starts?  hardcoreActive() minus the "a game is (being) looked up" part:
    // with no game running it is false whatever this says, so a screen that offers a state load before any game is
    // up (TV Home's Resume) asks this instead.
    bool hardcoreNextGame() const;
    bool allowStateLoad() const { return !hardcoreActive(); }
    bool allowCheats() const { return !hardcoreActive(); }
    bool allowRewind() const { return !hardcoreActive(); }
    bool allowSlowMotion() const { return !hardcoreActive(); }
    bool allowFrameAdvance() const { return !hardcoreActive(); }
    // Pausing is rate-limited in hardcore (rc_client_can_pause).  `why` gets the
    // text to show when it returns false.
    bool allowPause(std::string& why);

    // ---- save-state hooks -------------------------------------------------------
    // The runtime's trigger state travels with each state file as <state>.rcp, so
    // a restored state does not replay stale hit counts.
    void onStateSaved(const std::string& statePath);
    void onStateLoaded(const std::string& statePath);

    // ---- account / settings (UI) ------------------------------------------------
    bool available() const { return client_ != nullptr; }
    bool signedIn() const;
    bool loginBusy() const { return loginPending_; }
    std::string userName() const;
    void login(const std::string& user, const std::string& password);
    void logout();
    bool hardcoreWanted() const { return hardcoreWanted_; }
    void setHardcoreWanted(bool on);
    bool unofficialWanted() const { return unofficial_; }
    void setUnofficialWanted(bool on);
    bool enabled() const { return enabled_; }
    void setEnabled(bool on);
    const std::string& statusLine() const { return status_; }
    // The setting that is stopping hardcore ("virtualjaguar_pal = enabled"), or "".
    const std::string& hardcoreBlocker() const { return blocker_; }

    // ---- UI state (achievements_ui.cpp) ------------------------------------------
    bool showList = false;
    bool showLogin = false;
    char userBuf[64] = "";
    char passBuf[128] = "";
    std::vector<AchToast> toasts;
    std::map<unsigned, std::string> trackers;       // leaderboard id -> display
    std::map<unsigned, std::string> challenges;     // achievement id -> title
    std::string progress;                           // transient progress indicator text
    double progressBorn = 0;
    // Copies only: rcheevos objects remain protected until their strings and counters are copied.
    struct ClientSnapshot {
        bool signedIn = false, hardcore = false;
        uint32_t gameId = 0;
        std::string gameTitle, displayName;
        uint32_t score = 0, softcoreScore = 0, unreadMessages = 0;
        rc_client_user_game_summary_t summary{};
    };
    ClientSnapshot clientSnapshot() const;
    App* app() const { return app_; }

    // ---- pictures and the dashboard (achievements_games.cpp, achievements_ui.cpp) ----------
    ra::ImageCache images;                  // avatar, game icon, badges: disk cache + bounded textures
    bool imagesWanted() const { return imagesWanted_; }
    void setImagesWanted(bool on);          // download pictures (ra.images); off = what is on disk, nothing new
    // URLs rc_client derives for the loaded game / the signed-in user / one achievement.  "" when there is none.
    std::string gameIconUrl() const;
    std::string avatarUrl(std::string& version) const;
    // The loaded game's achievements as plain rows (see ra::AchRow); false when no game is tracked.
    bool achievementRows(std::vector<ra::AchRow>& rows) const;
    std::vector<SessionUnlock> sessionUnlocks;
    // The Jaguar games tab: called every frame the tab is visible.  Loads the cached list, refreshes it
    // when it is more than a day old (or empty), and fetches the signed-in user's progress.
    void gamesTabShown();
    void refreshGames(bool manual);         // the Refresh button: games list + progress
    bool gamesBusy() const { return gamesPending_ > 0 || progressPending_ > 0; }
    const std::vector<ra::GameEntry>& games() const { return games_; }
    int64_t gamesFetchedAt() const { return gamesFetchedAt_; }
    const std::string& gamesError() const { return gamesError_; }
    bool gamesFromCache() const { return gamesFromCache_; }
    bool progressKnown() const { return progressKnown_; }
    void playGame(const ra::GameEntry& g);  // launches the library file that runs this game
    bool libraryHashing() const { return hashing_; }
    // ra:: view state the window keeps between frames.
    ra::GameFilter gameFilter = ra::GameFilter::All;
    ra::GameSort gameSort = ra::GameSort::Title;
    char gameText[64] = "";
    ra::AchFilter achFilter = ra::AchFilter::All;
    ra::AchSort achSort = ra::AchSort::Default;
    char achText[64] = "";
    int dashTab = 0;                        // 0 this game, 1 Jaguar games, 2 profile

    // ---- headless testing ----------------------------------------------------------
    // Called by the headless runner; prints the verdict and returns false on failure.
    bool headlessSetup(const CliArgs& args);        // --ra-mock etc., before content loads
    void headlessFrame(unsigned frame);             // before each frame
    bool headlessCheck(const CliArgs& args);        // after the frames ran
    bool mockMode() const { return !mockDir_.empty(); }
    // True when the login token had to stay in config.ini because no system keyring answered.
    bool tokenInSettingsFile() const { return tokenInSettings_; }

private:
    friend struct AchCallbacks;
    friend struct AchGamesCallbacks;

    void beginLoad();
    void rebuildMemory();
    void applyHardcore();                // push the effective hardcore flag into rc_client
    void checkSettings();                // disallowed core options
    void toast(const std::string& title, const std::string& body, int kind, double life = 5, const std::string& icon = "");
    void handleEvent(const AchEvent& e);
    void queueEvent(AchEvent e);
    void drainEvents();
    // The core belongs to the emulation thread (#967): run there (inline / already there: at once).
    void coreCall(const std::function<void()>& fn);
    void corePost(std::function<void()> fn);
    void persistLogin();
    uint32_t consoleId(const std::string& path) const;
    void setStatus(const std::string& s) { status_ = s; }
    void mockRespond(const std::string& post, rc_client_server_callback_t cb, void* cbdata);

    App* app_ = nullptr;
    // Frame evaluation and UI snapshots share this lock. Mutations still dispatch to the core owner;
    // acquire inside coreCall, never while the UI is waiting for an owner command. Recursive because
    // rcheevos invokes our callbacks synchronously and those may query the client again.
    mutable std::recursive_mutex clientMu_;
    rc_client_t* client_ = nullptr;
    AchHttp http_;
    bool enabled_ = true;
    bool hardcoreWanted_ = false;
    bool unofficial_ = false;
    bool loginPending_ = false;
    bool tokenInSettings_ = false;   // the token is in config.ini (no system keyring), not in the credential store
    bool loading_ = false;           // an identify/load request is in flight
    bool gameKnown_ = false;         // the current content has an achievement set
    bool resetPending_ = false;      // rc_client asked for a reset (hardcore just turned on)
    bool restartTried_ = false;      // the pending restart was attempted already (cheat reset posted, reload line logged)
    bool restartGaveUpLogged_ = false;
    bool settingsOk_ = true;
    std::atomic<bool> memoryReady_{false};
    bool loadFailed_ = false;        // the last identify/load attempt did not produce a tracked game
    unsigned unlockedBeforePoke_ = 0;
    unsigned pokeIndex_ = 0;
    unsigned mockFrames_ = 0;
    uint64_t framesSeen_ = 0;          // threaded: EmuThread::framesRun() at the last pump()
    double lastIdlePost_ = 0;          // threaded: when pump() last posted emuIdle()
    unsigned hardcoreResets_ = 0;      // clean-start restarts performed (tests)
    unsigned loginAt_ = 0;             // headless: delay the mock sign-in to this frame
    std::atomic<bool> ranUnrestricted_{false};     // frames ran (or states loaded) outside hardcore since boot/reset
    double lastSettingsCheck_ = 0;
    std::string blocker_;
    std::string status_;
    std::string gameTitle_;
    std::string userCfgName_;
    std::string loadedPath_;
    rc_libretro_memory_regions_t regions_{};   // address -> host pointers, from the core's memory map

    // rc_client events, copied where they were raised and applied in pump() on the UI thread (#967).
    std::mutex eventMu_;
    std::vector<AchEvent> events_;
    // Bumped by onContentLoaded / onContentClosed (inside call()s).  Events carry the generation they were queued in;
    // drainEvents drops the stale transient ones (a tracker from game A must not outlive A).
    std::atomic<uint32_t> eventGen_{0};
    std::atomic<unsigned> offThreadReads_{0};
    // The rich-presence line, evaluated on the emulation thread every 60 frames.
    mutable std::mutex rpMu_;
    std::string richPresence_;
    unsigned rpFrames_ = 0;

    // headless / mock server
    std::string mockDir_;
    struct MockReply { rc_client_server_callback_t cb; void* cbdata; std::string body; int status; };
    std::mutex mockMu_;                       // guards the four below: an award reaches serverCall from the emulation thread
    std::deque<MockReply> mockQueue_;
    void mockDeliver();
    std::vector<std::string> mockLog_;
    std::vector<std::string> mockHashes_;
    std::vector<std::string> mockUnhandled_;
    std::vector<unsigned> unlocked_;          // achievement ids that fired
    struct Poke { unsigned frame; std::string code; bool done; };
    std::vector<Poke> pokes_;
    std::string mockUser_ = "MockUser";
    bool lastHardcore_ = false;
    std::vector<std::string> noticeLog_;
    struct Step { unsigned frame; std::string action; bool done; };
    std::vector<Step> steps_;
    std::vector<std::string> stepFailures_;
    void runStep(const Step& s, unsigned frame);

    // pictures + dashboard
    bool imagesWanted_ = true;
    std::vector<ra::GameEntry> games_;
    int64_t gamesFetchedAt_ = 0;
    std::string gamesError_;
    bool gamesInit_ = false;           // the cache file was read
    bool gamesFromCache_ = false;
    bool gamesAutoTried_ = false;      // one automatic refresh per session; after that only the button
    bool progressAutoTried_ = false;
    bool progressKnown_ = false;
    int gamesPending_ = 0;
    int progressPending_ = 0;
    bool gamesAborted_ = false;
    std::map<uint32_t, std::vector<ra::GameEntry>> gamesIncoming_;   // console -> fetched list
    std::map<uint32_t, bool> gamesIncomingOk_;
    std::map<uint32_t, ra::Progress> progress_;
    bool progressOk_ = false;
    size_t libraryMatched_ = (size_t)-1;   // library size at the last match
    unsigned hashGenMatched_ = 0;
    // MD5 of the library's cartridges (RA lists the hashes it accepts), computed on one worker thread.
    std::thread hashThread_;
    std::mutex hashMu_;
    std::map<std::string, std::string> cartMd5_;   // path -> md5 ("" = not hashable)
    std::atomic<unsigned> hashGen_{0};
    std::atomic<bool> hashStop_{false};
    std::atomic<bool> hashing_{false};
    void loadGamesCache();
    void finishGames();
    void finishProgress();
    void rematchLibrary();
    void startHashing();
    void updateImageNetwork();
    void dashSignedOut();
    void headlessGamesStep(const std::string& action, unsigned frame);
};

// ImGui (achievements_ui.cpp).
void drawAchievementsMenu(App& app);     // the "Achievements" menu-bar menu
void drawAchievementsUi(App& app);       // windows + toasts + trackers, once per frame

}  // namespace vj
