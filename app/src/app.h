// The application object: owns the window, GL context, ImGui, the libretro
// host and the audio/video/input services, and runs the main loop.  The same
// object runs headless (no window, no GL, no ImGui) for the smoke tests.
#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "achievements.h"
#include "audio.h"
#include "bios.h"
#include "cheats.h"
#include "config.h"
#include "core_host.h"
#include "debug/debug_session.h"
#include "fonts.h"
#include "discord_presence.h"
#include "neuro_agent.h"
#include "input.h"
#include "instance.h"
#include "library_service.h"
#include "netplay.h"
#include "pacing.h"
#include "paths.h"
#include "perf.h"
#include "platform/commands.h"
#include "theme_custom.h"
#include "video.h"

namespace vj {

namespace tv {
class AppHost;
class Shell;
}  // namespace tv

struct CliArgs {
    std::vector<std::string> content;
    std::string dataDir;
    std::vector<std::pair<std::string, std::string>> options;
    bool headless = false;
    bool fullscreen = false;
    bool noContent = false;         // boot the console with no cartridge: the CD BIOS (or --boot-bios)
    std::string bootBios;           // --boot-bios cart|cd: which BIOS a no-content boot enters ("" = cd)
    std::string cartBios, cdBios;   // --cart-bios / --cd-bios FILE: use this BIOS file for this run (not saved)
    std::vector<std::string> expectBios;   // headless: "cart:builtin|system|file" / "cd:..." must be what the core staged
    bool verbose = false;
    std::string screenshot;         // dev: save the window's pixels to this PNG after --screenshot-frame frames, then exit
    unsigned screenshotFrame = 120;
    std::string showWindow;         // dev: open a window at start: settings[-video|-input|-audio|-storage|-interface][:FILTER], states, about, cheats, library, login, achievements, keypad
    std::string theme;              // --theme system|dark|light: the colour theme for this run (not saved); "" = the saved choice
    float uiScale = 0;              // dev: force the UI scale (0 = the display's); stands in for a HiDPI display
    std::string font, fontThen;     // dev: --font SPEC (override ui.font.* for this run), --font-then SPEC (switch to it live after 60 frames)
    unsigned pixelDensity = 0;      // dev: --pixel-density N: lay out in points but render N pixels per point (a Retina display on a 1x or offscreen window), for screenshots
    bool lowDpi = false;            // dev: no high-pixel-density window (a 1x framebuffer even on a Retina Mac): screenshots of a 1x display
    std::string neuroUrl;           // --neuro URL: connect to this Neuro API server for this run (not saved)
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
    std::string tvTestScript;       // replay this script of key / pad / focus events against the TV menu (tv_script.cpp); headless it replaces the run, windowed it runs between frames
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
    DebugSession debug;      // the core's debug interface: Tools menu, halt pump, breakpoints (debug/debug_session.h)
    discord::Presence discordPresence;   // Discord Rich Presence (discord_app.cpp); idle without an application ID
    struct DiscordState {        // what discordTick() remembers between calls
        double lastTick = -1;
        long long appStartUnix = 0, gameStartUnix = 0;
        std::string artKey, artUrl;
        double artAt = 0;
    } discordState;
    neuro::Agent neuro;              // the Neuro API client (neuro_app.cpp): an AI plays as Player 1
    std::string neuroCliUrl;         // --neuro URL: on for this run, whatever the config says

    // ---- state ----------------------------------------------------------------
    bool paused = false;
    bool fastForward = false;
    bool quit = false;
    bool fullscreen = false;
    bool menuPinned = true;
    bool showInWindowMenu = false;    // macOS: also show the in-window menu bar under the native one (ui.menubar_in_window; F1 toggles)
    bool inWindowMenuBar() const;     // is an ImGui menu bar being drawn at all
    bool showSettings = false;
    bool showStates = false;
    bool showAbout = false;
    bool showCheats = false;
    fonts::Settings fontSettings;     // Settings > Interface > Font (ui.font.*); applied live through fonts_imgui.h
    bool homeLibrary = true;          // no game loaded: the game library is the home screen (ui.home_library)
    bool showNetplay = false;
    CheatManager cheats;
    std::string devSelectTab;         // dev: --show-window settings-video etc. opens Settings on that tab ("Video", "Input", ...)
    float uiScaleOverride = 0;        // dev: --ui-scale
    unsigned simDensity_ = 0;         // dev: --pixel-density (0 / 1 = off)
    void simulateDensity();
    void logDensity();
    bool densityLogged_ = false;
    unsigned densityFrames_ = 0;
    bool lowDpi_ = false;             // dev: --low-dpi
    int stateSlot = 0;
    int resumeMode = 1;               // lib::ResumeMode: Off / Ask / Always, "Resume where I left off" (saves.resume)
    struct ResumeOffer {              // "Resume from <time>?" asked after a game booted (resumeMode == Ask)
        bool active = false;
        SDL_Time savedAt = 0;
        std::string game;
    } resumeOffer;
    void answerResume(bool resume, bool always);   // the prompt's buttons
    char optionFilter[64] = "";   // core-options search box
    std::vector<std::string> recents;

    // Video settings (persisted)
    PresentOptions present;
    bool vsync = true;
    float volume = 1.0f;
    bool mute = false;
    float audioTargetMs = 64.0f;
    bool showFps = false;
    float uiScale_ = 1.0f;            // the scale the style was set up at (the display's content scale times ui.scale)
    std::string themeBeforePreview_;  // the choice to keep/restore while the theme editor previews an unsaved theme
    bool themePreviewing_ = false;
    bool themeFromCli_ = false;       // --theme was given: this run's choice is not read from / written to config.ini (ui.theme)
    bool autosaveSram = true;
    bool trayIcon = false;     // ui.tray: a tray / menu-bar icon (Settings > Interface)
    bool windowsOutside = false;   // ui.viewports: Settings / tool windows may become their own OS windows (viewports.h)
    bool keepAwake = true;     // ui.keep_awake: no screensaver / display sleep while a game runs

    // ---- actions ----------------------------------------------------------------
    bool openContent(const std::string& path);
    bool autoSave();                  // write this game's auto-save when the policy allows (lib::shouldAutoSave)
    void offerResume();               // a game booted: load / ask / nothing, by the policy and the option
    void openAndLoadState(const std::string& path, int slot);   // the library: open `path`, then load its state `slot`
    bool openNoContent();             // boots the console with no cartridge, through the BIOS bootBios() last chose
    // Boots a BIOS on its own (BIOS & system tab, File > Boot BIOS): a cartridge boot ROM runs against an empty
    // slot, the CD BIOS comes up with no disc.  `file` is used for this boot only ("" = the saved choice).
    bool bootBios(bios::Role role, const std::string& file = std::string());
    // The saved BIOS choice (config.ini bios.cart / bios.cd); "" = Auto.  Takes effect at the next load.
    bios::Selection biosChoice;
    void setBiosChoice(bios::Role role, const std::string& path);
    bool importBios(const std::string& path, bool trustSize = false);   // copy into <system>/bios/, choose it, rescan
    bool reloadContent();
    void closeContent();
    void showOpenDialog();
    void showImportBiosDialog();      // a file picker that imports the BIOS picked (BIOS & boot)
    bool homeVisible() const { return homeLibrary && !host.isLoaded(); }   // is the library standing in for the game?
    void newWindow();                 // File > New Window (Ctrl/Cmd+N): another copy of the app (instance.h)
    void toggleLibrary();             // Ctrl/Cmd+L: the library window, or focus the home screen's search box
    void reset();
    void togglePause();
    // The TV in-game menu's pause (tv/tv_app_host.cpp): like togglePause() but silent, and it remembers whether the
    // game was already paused, so closing the menu gives back exactly that.  False = refused (hardcore rations pausing;
    // `why` says so).  Under netplay it returns true without pausing: the link keeps running.
    bool menuPause(std::string& why);
    void menuResume();
    tv::Shell& tvShell();
    void setFullscreen(bool fs);
    void applyVsync();
    void syncSwapInterval();
    void applyTheme();                // the Dear ImGui style for the theme in force (theme.h); a no-op before the UI exists
    void setThemeChoice(const std::string& key);   // Settings > Interface > Appearance: system | dark | light | custom:<name>; remembered
    bool applyThemeChoice(const std::string& key); // the same without remembering; false when a custom theme is missing (System is used)
    void themePreview(const theme::Custom& c);     // the theme editor's live preview: shown, not remembered until saved
    void themePreviewEnd(bool keep);               // the editor closed (keep: its theme was saved and chosen); otherwise the previous choice returns
    theme::Store themeStore() const;               // <data>/themes/, read-only in a secondary copy (#902)
    bool showThemeEditor = false;
    void applyAudioSettings();

    std::string statePath(int slot) const;
    std::string thumbPath(int slot) const;
    bool saveState(int slot, bool quiet = false);   // slot lib::kAutoSlot = the auto-save
    bool loadState(int slot);
    bool slotExists(int slot) const;
    SDL_Time slotTime(int slot) const;
    const SlotThumb* slotThumb(int slot);   // lazily loaded GL texture, or null
    void invalidateThumbs();

    void toast(const std::string& text, double seconds = 3.0, int level = 1);
    void addRecent(const std::string& path);
    void saveConfig();
    void applySettingsFromConfig();   // re-read the member settings from cfg (defaults for erased keys); Settings > Reset
    void applyBios();                 // hands the BIOS choice (and a one-off boot's file) to the core host

    // ---- native-OS integration (desktop.cpp; platform/platform.h) -------------------------
    void runNativeCommand(platform::Cmd cmd, int arg);   // a native menu / tray click, from the event loop
    void presentViewports();                             // draws + presents the extra OS windows (after the main draw data)
    bool fromExtraWindow(const SDL_Event& e) const;
    void applyViewports();                               // ImGui multi-viewport mode to match windowsOutside + the situation (per frame)
    bool viewportsActive() const { return viewportsOn_; }
    const char* viewportsNote() const;                   // why windowsOutside is ignored right now ("" = not)
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
    // tv_script.cpp: --tv-test-script.  Headless it is the whole run (runTvScript); windowed, runInteractive steps it between frames.
    int runTvScript(const CliArgs& a);
    struct TvScriptRun;
    std::shared_ptr<TvScriptRun> tvScript_;
    bool tvScriptBegin(const CliArgs& a, bool windowed);
    int tvScriptStep();
    void tvScriptDrain();
    void tvScriptEnd();
    void saveWindowScreenshot(const std::string& path);

    void desktopInit();       // after the window exists: native menus, tray, dark title bar
    void desktopFrame();      // every frame: refresh the native menus from the app state
    void desktopShutdown();
    platform::MenuState menuState();
    void handleEvent(const SDL_Event& e, bool headless);
    void handleDrop(const std::string& path);
    void handleHotkey(const SDL_KeyboardEvent& k);
    void stepEmulation();
    void headlessNeuroFrame(uint64_t t0, unsigned i);
    void haltedFrame();               // one UI frame WITHOUT stepping the emulation: the debugger's halt pump runs it
    void syncDebug();                 // content loaded / hardcore -> the debug session
    void frameUi();
    void updateTitle();
    uint32_t frameHash() const;

    static constexpr int kNoPendingSlot = -1000;
    int pendingStateSlot_ = kNoPendingSlot;       // openAndLoadState: load this slot after the new game has run a few frames
    int pendingStateFrames_ = 0;
    double runSeconds_ = 0;           // emulated seconds of the loaded game (auto-save needs a few)
    bool reloading_ = false;          // reloadContent: a restart, not a new game
    bool suppressResume_ = false;     // the library opened a game to load a chosen state: no auto-save offer
    bool headless_ = false;
    bool badOverride_ = false;   // a --option key/value the loaded core does not accept
    bool sdlUp_ = false;
    SDL_Window* window_ = nullptr;
    SDL_GLContext gl_ = nullptr;
    bool imguiUp_ = false;
    float dropX_ = 0.0f, dropY_ = 0.0f;   // last SDL_EVENT_DROP_POSITION (window coordinates)
    uint32_t openDialogEvent_ = 0;
    bool viewportsOn_ = false;         // ImGuiConfigFlags_ViewportsEnable is set right now
    uint64_t menuStateTick_ = 0;       // desktopFrame throttle
    bool inputCaptured_ = false;
    bool menuHeldPause_ = false;      // menuPause() took the game's pause (menuResume gives it back)
    bool pausedBeforeMenu_ = false;   // ... and the game was already paused by the user
    std::unique_ptr<tv::AppHost> tvHost_;   // the TV module's view of this App
    std::unique_ptr<tv::Shell> tvShell_;

    // pacing
    double lastTick_ = 0;
    Pacer pacer_;
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
    std::string biosOnce_[3];         // a one-off BIOS boot's file per role (bootBios): beats everything, until the next load
    std::string biosCli_[3];          // --cart-bios / --cd-bios: beats the saved choice for the whole run
    unsigned bootTarget_ = 0;         // what a no-content boot enters (VJ_BOOT_*)

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
// Debug tools (debug/debug_ui.cpp): the Tools menu, the "halted" bar, and every tool window's draw hook.
void drawToolsMenu(App& app);
void drawDebugOverlay(App& app, float areaX, float areaY);   // the "halted" bar
void drawDebugWindows(App& app);                            // every registered provider's and tool's draw hook
void addDebugTools(App& app);                               // plug every tool window into app.debug (DebugSession::addProvider)
void shutdownDebugTools();                                  // before the GL context goes: frees the windows' textures
// Dev (--show-window): opens a Tools window by name -- memory, palette|clut, scanner, objects, blitter, textures,
// scope -- once the session can use it (retried each frame until then).  False if the name is not a debug tool.
bool openDebugWindow(DebugSession& session, const std::string& name);
// Netplay (netplay_ui.cpp): the window, the menu, and the headless/CLI glue.
bool netplayInit(App& app, const CliArgs& a);               // log tap + --netplay*; false = bad arguments
int netplayHeadlessCheck(App& app, const CliArgs& a);       // 0 ok, 1 fail, 77 skip (nobody discovered)
void drawNetplayMenu(App& app);
void drawNetplay(App& app);
// Discord Rich Presence (discord_app.cpp).
void discordTick(App& app);                          // UI loop, every iteration: cheap, rate-limits itself
void discordNoteGameStarted(App& app);               // a game booted: the "elapsed" timer restarts
void discordShutdown(App& app);                      // clears the status and stops the worker
std::string discordConfiguredAppId(const Config& cfg);
// Neuro API (neuro_app.cpp): let Neuro-sama, or any Neuro API backend, play (docs/neuro.md).
void neuroInit(App& app, const CliArgs& a);          // config, profiles; connects when enabled
void neuroTick(App& app);                            // UI loop, every iteration: the socket, the game, the next force
void neuroShutdown(App& app);                        // closes the connection
void drawNeuroTab(App& app);                         // Settings > Neuro   // config discord.app_id, else the compile-time default ("" = none)
// Themes (theme_ui.cpp): Settings > Interface > Appearance, the theme editor window and its file-dialog mailbox.
void drawAppearanceSettings(App& app);
void drawThemeEditor(App& app);
void themeFrameHook(App& app);
// BIOS & boot (bios_ui.cpp): the BIOS choice and boot controls, shown in Settings and above the library's BIOS tab.
void drawBiosPanel(App& app);
// Controller remapping UI and helpers (remap_ui.cpp).
void drawControlsTab(App& app);
void drawKeypadOverlay(App& app);

}  // namespace vj
