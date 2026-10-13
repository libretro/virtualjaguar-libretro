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
//              wiped.
#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "ach_http.h"
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
    void afterFrame();           // once per emulated frame, right after retro_run
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
    rc_client_t* client() const { return client_; }
    App* app() const { return app_; }

    // ---- headless testing ----------------------------------------------------------
    // Called by the headless runner; prints the verdict and returns false on failure.
    bool headlessSetup(const CliArgs& args);        // --ra-mock etc., before content loads
    void headlessFrame(unsigned frame);             // before each frame
    bool headlessCheck(const CliArgs& args);        // after the frames ran
    bool mockMode() const { return !mockDir_.empty(); }

private:
    friend struct AchCallbacks;

    void beginLoad();
    void rebuildMemory();
    void applyHardcore();                // push the effective hardcore flag into rc_client
    void checkSettings();                // disallowed core options
    void toast(const std::string& title, const std::string& body, int kind, double life = 5);
    void handleEvent(const rc_client_event_t* e);
    void persistLogin();
    uint32_t consoleId(const std::string& path) const;
    void setStatus(const std::string& s) { status_ = s; }
    void mockRespond(const std::string& post, rc_client_server_callback_t cb, void* cbdata);

    App* app_ = nullptr;
    rc_client_t* client_ = nullptr;
    AchHttp http_;
    bool enabled_ = true;
    bool hardcoreWanted_ = false;
    bool unofficial_ = false;
    bool loginPending_ = false;
    bool loading_ = false;           // an identify/load request is in flight
    bool gameKnown_ = false;         // the current content has an achievement set
    bool resetPending_ = false;      // rc_client asked for a reset (hardcore just turned on)
    bool settingsOk_ = true;
    bool memoryReady_ = false;
    bool loadFailed_ = false;        // the last identify/load attempt did not produce a tracked game
    unsigned unlockedBeforePoke_ = 0;
    unsigned pokeIndex_ = 0;
    unsigned mockFrames_ = 0;
    unsigned hardcoreResets_ = 0;      // clean-start restarts performed (tests)
    unsigned loginAt_ = 0;             // headless: delay the mock sign-in to this frame
    bool ranUnrestricted_ = false;     // frames ran (or states loaded) outside hardcore since boot/reset
    double lastSettingsCheck_ = 0;
    std::string blocker_;
    std::string status_;
    std::string gameTitle_;
    std::string userCfgName_;
    std::string loadedPath_;
    rc_libretro_memory_regions_t regions_{};   // address -> host pointers, from the core's memory map

    // headless / mock server
    std::string mockDir_;
    struct MockReply { rc_client_server_callback_t cb; void* cbdata; std::string body; int status; };
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
};

// ImGui (achievements_ui.cpp).
void drawAchievementsMenu(App& app);     // the "Achievements" menu-bar menu
void drawAchievementsUi(App& app);       // windows + toasts + trackers, once per frame

}  // namespace vj
