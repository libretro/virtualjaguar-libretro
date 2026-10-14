// The application object: owns the window, GL context, ImGui, the libretro
// host and the audio/video/input services, and runs the main loop.  The same
// object runs headless (no window, no GL, no ImGui) for the smoke tests.
#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "achievements.h"
#include "audio.h"
#include "cheats.h"
#include "config.h"
#include "core_host.h"
#include "input.h"
#include "instance.h"
#include "library_service.h"
#include "netplay.h"
#include "paths.h"
#include "perf.h"
#include "platform/commands.h"
#include "video.h"

namespace vj {

struct CliArgs {
    std::vector<std::string> content;
    std::string dataDir;
    std::vector<std::pair<std::string, std::string>> options;
    bool headless = false;
    bool fullscreen = false;
    bool noContent = false;
    bool verbose = false;
    std::string screenshot;         // dev: save the window's pixels to this PNG after --screenshot-frame frames, then exit
    unsigned screenshotFrame = 120;
    std::string showWindow;         // dev: open a window at start: settings[-video|-input|-audio|-storage][:FILTER], states, about, cheats, library, login, achievements, keypad
    float uiScale = 0;              // dev: force the UI scale (0 = the display's); stands in for a HiDPI display
    bool stateRoundtrip = false;    // headless: save a state, run on, load it, run on again; both runs must hash alike
    bool allowBlank = false;        // headless: a blank final frame is not a failure (synthetic discs)
    unsigned frames = 300;          // headless frames per content
    std::string expectHash;         // headless: expected hash of the FIRST content
    std::string dumpFrame;          // headless: write the final frame of each game as PNG (<path>.N.png)
    // cheats (#835): the command-line twins of the Cheats window, used by the headless tests
    std::string cheatsDir;                    // extra folder of .cht files
    std::string cheatSet;                     // select this cheat set (file stem) for the game
    std::vector<std::string> cheatOn;         // turn on these database cheats (description, id or position)
    std::vector<std::string> cheatCodes;      // add these manual codes
    std::vector<std::pair<uint32_t, uint8_t>> expectRam;   // headless: main-RAM bytes that must hold at the end
    bool dumpNativeMenu = false;    // dev: print and check the real macOS menu tree, then exit (77 = no native menu here)
    bool printControls = false;     // headless: print Player 1's controls cheat sheet (devices_ui.h)
    int expectControls = -1;        // headless: 1 = the controls overlay must be showing after load, 0 = hidden
    bool showHelp = false;
    bool showVersion = false;
    unsigned perfFrames = 0;        // dev: profile this many UI frames (after a warm-up), print the phase costs, exit
    bool perfGpu = false;           // dev: glFinish around the shader chain / UI so the numbers include GPU time
    unsigned perfShaderW = 0, perfShaderH = 0;   // dev: run the shader chain at this output size (4K cost on a 1080p display)
    bool perfNoReuse = false;       // dev: disable the shader output reuse (before/after comparison)
    bool perfPause = false;         // dev: pause after loading (idle-cost profile)
    // RetroAchievements (#836): the headless test hooks.  There is no way to sign in
    // headless; the only server is the canned-JSON mock, so a test can never reach
    // (or authenticate against) the real service.
    std::string raMock;                              // directory of <api>.json canned replies
    bool raHardcore = false;                         // run the mock session in hardcore
    std::vector<std::pair<unsigned, std::string>> raPokes;   // FRAME:CODE -> core cheat switched on at FRAME
    std::vector<unsigned> raExpectUnlock;            // achievement ids that must fire
    std::vector<unsigned> raExpectNoUnlock;          // ... and ids that must not
    std::vector<std::string> raExpectRequests;       // API names ("awardachievement") that must have been sent
    std::string raExpectHash;                        // the game hash the client must have asked about
    bool raExpectLoadError = false;                  // the game must NOT be identified
    bool raExpectHardcore = false;                   // hardcore active + state load refused
    bool raExpectSoftcore = false;                   // softcore: save + load state work, progress sidecar written
    unsigned raLoginAt = 0;                          // delay the mock sign-in to this frame (a late sign-in)
    std::vector<std::pair<unsigned, std::string>> raSteps;   // FRAME:ACTION (hardcore:on|off, signout, poke:A=V, expect:A=V)
    std::vector<std::string> raExpectNotice;         // a player-facing notice containing this text must have been shown
    int raExpectReset = -1;                          // exact number of clean-start restarts (-1 = don't check)
    bool raExpectBlocked = false;                    // hardcore wanted, but a disallowed core option must keep it off
    NetplayCli netplay;                              // --netplay*: the command-line twin of the Netplay window
};

struct Osd {
    std::string text;
    double born = 0;
    double life = 3;
    int level = 1;
};

struct SlotThumb {
    unsigned tex = 0;
    int w = 0, h = 0;
    SDL_Time mtime = 0;
};

class App {
public:
    App();
    ~App();
    int run(const CliArgs& args);   // returns the process exit code

    // ---- services (public: the UI drives them) -------------------------------
    Paths paths;
    Config cfg;
    CoreHost& host;
    AudioOut audio;
    VideoOut video;
    InputManager input;
    LibraryService library;   // optional game library (#838); idle until start()ed
    Achievements ach;   // RetroAchievements (#836)
    Perf perf;          // dev: --perf-frames
    Netplay netplay{host};   // link-cable play over the network

    // ---- state ----------------------------------------------------------------
    bool paused = false;
    bool fastForward = false;
    bool quit = false;
    bool fullscreen = false;
    bool menuPinned = true;
    bool showSettings = false;
    bool showStates = false;
    bool showAbout = false;
    bool showCheats = false;
    bool homeLibrary = true;          // no game loaded: the game library is the home screen (ui.home_library)
    bool showNetplay = false;
    CheatManager cheats;
    std::string devSelectTab;         // dev: --show-window settings-video etc. opens Settings on that tab ("Video", "Input", ...)
    float uiScaleOverride = 0;        // dev: --ui-scale
    int stateSlot = 0;
    char optionFilter[64] = "";   // core-options search box
    std::vector<std::string> recents;

    // Video settings (persisted)
    PresentOptions present;
    bool vsync = true;
    float volume = 1.0f;
    bool mute = false;
    float audioTargetMs = 64.0f;
    bool showFps = false;
    bool autosaveSram = true;
    bool trayIcon = false;     // ui.tray: a tray / menu-bar icon (Settings > Storage)
    bool keepAwake = true;     // ui.keep_awake: no screensaver / display sleep while a game runs

    // ---- actions ----------------------------------------------------------------
    bool openContent(const std::string& path);
    bool openNoContent();
    bool reloadContent();
    void closeContent();
    void showOpenDialog();
    bool homeVisible() const { return homeLibrary && !host.isLoaded(); }   // is the library standing in for the game?
    void newWindow();                 // File > New Window (Ctrl/Cmd+N): another copy of the app (instance.h)
    void toggleLibrary();             // Ctrl/Cmd+L: the library window, or focus the home screen's search box
    void reset();
    void togglePause();
    void setFullscreen(bool fs);
    void applyVsync();
    void applyAudioSettings();

    std::string statePath(int slot) const;
    std::string thumbPath(int slot) const;
    bool saveState(int slot);
    bool loadState(int slot);
    bool slotExists(int slot) const;
    SDL_Time slotTime(int slot) const;
    const SlotThumb* slotThumb(int slot);   // lazily loaded GL texture, or null
    void invalidateThumbs();

    void toast(const std::string& text, double seconds = 3.0, int level = 1);
    void addRecent(const std::string& path);
    void saveConfig();

    // ---- native-OS integration (desktop.cpp; platform/platform.h) -------------------------
    void runNativeCommand(platform::Cmd cmd, int arg);   // a native menu / tray click, from the event loop
    bool applyTray();                                    // makes the tray match trayIcon; false = not available here

    double now() const;
    SDL_Window* window() const { return window_; }
    float fpsMeasured() const { return fpsShown_; }
    std::vector<Osd>& osd() { return osd_; }

private:
    bool initSdl(const CliArgs& a);
    bool initWindow();
    bool initImGui();
    void shutdown();
    void loadConfig();
    void wireHost();

    int runInteractive(const CliArgs& a);
    int runHeadless(const CliArgs& a);
    void saveWindowScreenshot(const std::string& path);

    void desktopInit();       // after the window exists: native menus, tray, dark title bar
    void desktopFrame();      // every frame: refresh the native menus from the app state
    void desktopShutdown();
    platform::MenuState menuState();
    void handleEvent(const SDL_Event& e, bool headless);
    void handleDrop(const std::string& path);
    void handleHotkey(const SDL_KeyboardEvent& k);
    void stepEmulation();
    void frameUi();
    void updateTitle();
    uint32_t frameHash() const;

    bool headless_ = false;
    bool badOverride_ = false;   // a --option key/value the loaded core does not accept
    bool sdlUp_ = false;
    SDL_Window* window_ = nullptr;
    SDL_GLContext gl_ = nullptr;
    bool imguiUp_ = false;
    float dropX_ = 0.0f, dropY_ = 0.0f;   // last SDL_EVENT_DROP_POSITION (window coordinates)
    uint32_t openDialogEvent_ = 0;
    uint64_t menuStateTick_ = 0;       // desktopFrame throttle
    bool inputCaptured_ = false;

    // pacing
    double lastTick_ = 0;
    double accum_ = 0;
    uint64_t perfFreq_ = 1;

    // measured fps
    double fpsWindowStart_ = 0;
    uint64_t fpsWindowFrames_ = 0;
    float fpsShown_ = 0;
    double lastSramFlush_ = 0;
    double lastTitle_ = 0;

    std::vector<Osd> osd_;
    std::map<int, SlotThumb> thumbs_;
    InstanceLock instanceLock_;       // held for the whole run; the number lives in paths.instance
    std::string dataDirArg_;          // --data-dir as given (forwarded to a New Window copy)
    std::string headlessTmp_;
    unsigned headlessLoads_ = 0;

    // headless results, one per loaded content
    struct HeadlessResult {
        std::string path;
        uint32_t hash = 0;
        unsigned w = 0, h = 0;
        size_t audioFrames = 0;
        bool uniform = true;
        unsigned optVisible = 0, optTotal = 0;
    };
    std::vector<HeadlessResult> results_;
    size_t audioFramesSeen_ = 0;
};

// ImGui frame builder (ui.cpp).
void drawUi(App& app, bool& wantCaptureKeyboard);

// Cheats (cheats_core.cpp / cheats_ui.cpp).
void cheatsInit(App& app, const CliArgs& a);        // wire the core sinks, find the library
void cheatsAfterLoad(App& app);                     // a game was (re)loaded: its cheats go back in
void cheatsAfterStateLoad(App& app);                // a state was restored
void cheatsOnClose(App& app);                       // no game any more
void cheatsSetUserDir(App& app, const std::string& dir);
bool cheatsHeadlessCheck(App& app, const CliArgs& a);   // --expect-ram and rejected cheat options
void drawCheats(App& app);
// Desktop integration settings (desktop.cpp): the tray / menu-bar icon and "keep the display awake".
void drawDesktopSettings(App& app);
// Game library home screen (library_ui.cpp): drawn into the area below the menu bar while no game is loaded.
void drawLibraryHome(App& app, float x, float y, float w, float h);
// Netplay (netplay_ui.cpp): the window, the menu, and the headless/CLI glue.
bool netplayInit(App& app, const CliArgs& a);               // log tap + --netplay*; false = bad arguments
int netplayHeadlessCheck(App& app, const CliArgs& a);       // 0 ok, 1 fail, 77 skip (nobody discovered)
void drawNetplayMenu(App& app);
void drawNetplay(App& app);
// Controller remapping UI and helpers (remap_ui.cpp).
void drawControlsTab(App& app);
void drawKeypadOverlay(App& app);

}  // namespace vj
