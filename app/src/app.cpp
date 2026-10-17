#include "app.h"
#include "devices_ui.h"
#include "fonts_imgui.h"
#include "ui_scale.h"

#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl3.h"
#include "platform/platform.h"
#include "platform/viewports.h"
#include "theme_style.h"
#include "tv/tv_app_host.h"
#include "tv/tv_shell.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>

namespace vj {

namespace {

const int kMaxRecents = 10;

std::string joinExtensions(const std::string& pipeList) {
    std::string out;
    size_t pos = 0;
    while (pos <= pipeList.size()) {
        size_t bar = pipeList.find('|', pos);
        std::string tok = pipeList.substr(pos, bar == std::string::npos ? std::string::npos : bar - pos);
        if (!tok.empty()) out += (out.empty() ? "" : ";") + tok;
        if (bar == std::string::npos) break;
        pos = bar + 1;
    }
    return out;
}

// SDL's dialog filters must stay valid until the callback runs.
std::string g_filterName = "Atari Jaguar games and discs";
std::string g_filterPattern;
SDL_DialogFileFilter g_filters[2];
std::string g_lastDir;

void SDLCALL dialogDone(void* ud, const char* const* list, int /*filter*/) {
    // May run on another thread: marshal through the event queue.
    App* app = (App*)ud;
    (void)app;
    if (!list || !list[0]) return;
    SDL_Event ev;
    SDL_zero(ev);
    ev.type = SDL_EVENT_USER;
    ev.user.code = 0x56414A47;  // 'VAJG': an "open this path" request
    ev.user.data1 = SDL_strdup(list[0]);
    SDL_PushEvent(&ev);
}

void SDLCALL importDialogDone(void*, const char* const* list, int /*filter*/) {
    if (!list || !list[0]) return;
    SDL_Event ev;
    SDL_zero(ev);
    ev.type = SDL_EVENT_USER;
    ev.user.code = 0x56424A49;  // 'VBJI': an "import this BIOS" request
    ev.user.data1 = SDL_strdup(list[0]);
    SDL_PushEvent(&ev);
}

}  // namespace

App::App() : host(CoreHost::instance()) {
    tvHost_ = std::make_unique<tv::AppHost>(*this);   // headless runs have them too: opening the menu needs no ImGui
    tvShell_ = std::make_unique<tv::Shell>(*tvHost_);
}
App::~App() { shutdown(); }

double App::now() const { return (double)SDL_GetPerformanceCounter() / (double)perfFreq_; }

// Headless with --neuro: the client runs as in the UI loop, and frame `i` waits for its time at 60 fps.
void App::headlessNeuroFrame(uint64_t t0, unsigned i) {
    neuroTick(*this);
    const double due = (double)t0 / (double)perfFreq_ + i / 60.0;
    const double wait = due - now();
    if (wait > 0) SDL_Delay((Uint32)(wait * 1000.0));
}

// ---------------------------------------------------------------------------------
// config
// ---------------------------------------------------------------------------------
void App::loadConfig() {
    cfg.load(paths.configFile);
    if (paths.instance > 1) {
        // A second copy (instance.h): start from the shared settings, but never write them.  Only
        // this window's own size and controller choice are kept, in config-N.ini.
        cfg.overlay(paths.instanceConfig);
        cfg.setPath(paths.instanceConfig);
        cfg.setSavePrefixes({"window.", "input.pads."});
    }
    input.init(cfg, paths.root);
    input.setPadFilter(parsePadFilter(cfg.get(instancePadKey(paths.instance), "all")));
    applySettingsFromConfig();
    g_lastDir = cfg.get("ui.last_dir", "");
    recents.clear();
    for (int i = 0; i < kMaxRecents; i++) {
        std::string r = cfg.get("recent." + std::to_string(i), "");
        if (!r.empty()) recents.push_back(r);
    }
    host.loadUserOptions(cfg.withPrefix("opt."));
    biosChoice = bios::loadSelection(cfg);
}

// The settings that live in plain members, read from the config with their defaults.  Settings > Reset
// erases keys and calls this again, so it is the one place that knows what "default" means.
void App::applySettingsFromConfig() {
    present.integerScale = cfg.getBool("video.integer", false);
    present.linearFilter = cfg.getBool("video.linear", false);
    present.stretch = cfg.getBool("video.stretch", false);
    present.aspectOverride = cfg.getFloat("video.aspect", 0.0f);
    vsync = cfg.getBool("video.vsync", true);
    video.shader.loadSettings(cfg, paths.root);
    volume = cfg.getFloat("audio.volume", 1.0f);
    mute = cfg.getBool("audio.mute", false);
    audioTargetMs = cfg.getFloat("audio.target_ms", 64.0f);
    showFps = cfg.getBool("ui.show_fps", false);
    fontSettings = fonts::load(cfg);
    fonts::request(fontSettings);   // applied before the next frame (fonts_imgui.h); Settings > Reset lands here too
    if (!themeFromCli_) applyThemeChoice(cfg.get("ui.theme", "system"));
    homeLibrary = cfg.getBool("ui.home_library", true);
    autosaveSram = cfg.getBool("saves.autosave", true);
    trayIcon = cfg.getBool("ui.tray", false);
    keepAwake = cfg.getBool("ui.keep_awake", true);
    windowsOutside = cfg.getBool("ui.viewports", viewports::defaultEnabled());
    showInWindowMenu = cfg.getBool("ui.menubar_in_window", false);
    resumeMode = lib::clampResumeMode(cfg.getInt("saves.resume", lib::kResumeDefault));
    input.stickAsDpad = cfg.getBool("input.stick_dpad", true);
}

void App::saveConfig() {
    cfg.setBool("video.integer", present.integerScale);
    cfg.setBool("video.linear", present.linearFilter);
    cfg.setBool("video.stretch", present.stretch);
    cfg.setFloat("video.aspect", present.aspectOverride);
    cfg.setBool("video.vsync", vsync);
    video.shader.saveSettings(cfg);
    cfg.setFloat("audio.volume", volume);
    cfg.setBool("audio.mute", mute);
    cfg.setFloat("audio.target_ms", audioTargetMs);
    cfg.setBool("ui.show_fps", showFps);
    fonts::store(cfg, fontSettings);
    if (!themeFromCli_) cfg.set("ui.theme", themePreviewing_ ? themeBeforePreview_ : theme::choiceKey());
    cfg.setBool("ui.home_library", homeLibrary);
    cfg.setBool("saves.autosave", autosaveSram);
    cfg.setBool("ui.tray", trayIcon);
    cfg.setBool("ui.keep_awake", keepAwake);
    cfg.setBool("ui.viewports", windowsOutside);
    cfg.setBool("ui.menubar_in_window", showInWindowMenu);
    cfg.setInt("saves.resume", resumeMode);
    cfg.setBool("input.stick_dpad", input.stickAsDpad);
    cfg.set("ui.last_dir", g_lastDir);
    bios::storeSelection(cfg, biosChoice);
    for (int i = 0; i < kMaxRecents; i++) {
        std::string k = "recent." + std::to_string(i);
        if (i < (int)recents.size()) cfg.set(k, recents[(size_t)i]);
        else cfg.erase(k);
    }
    cfg.save();
}

void App::addRecent(const std::string& path) {
    recents.erase(std::remove(recents.begin(), recents.end(), path), recents.end());
    recents.insert(recents.begin(), path);
    if ((int)recents.size() > kMaxRecents) recents.resize(kMaxRecents);
    g_lastDir = pathDir(path);
    if (!headless_) platform::noteRecent(path);   // the OS recent-documents list (Dock / Jump List / Explorer)
}

void App::toast(const std::string& text, double seconds, int level) {
    Osd o;
    o.text = text;
    o.born = now();
    o.life = seconds;
    o.level = level;
    osd_.push_back(std::move(o));
    if (osd_.size() > 6) osd_.erase(osd_.begin());
    if (headless_) std::fprintf(stderr, "[osd] %s\n", text.c_str());
}

// ---------------------------------------------------------------------------------
// init / shutdown
// ---------------------------------------------------------------------------------
bool App::initSdl(const CliArgs& a) {
    headless_ = a.headless;
    if (headless_) {
        // SDL dummy drivers: the audio path, the event queue and the file-drop
        // route all run for real; there is just no window and no GL.
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
        SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    }
    // The identifier is the freedesktop / bundle / AppUserModel id: SDL turns it into the Wayland app_id and
    // the X11 WM_CLASS, which is how a desktop matches the window to io.github.libretro.VirtualJaguar.desktop
    // (icon, grouping, "pin to dock").  It must equal the id in app/packaging.
    SDL_SetAppMetadata("Virtual Jaguar", VJ_APP_VERSION, "io.github.libretro.VirtualJaguar");
    SDL_SetAppMetadataProperty(SDL_PROP_APP_METADATA_CREATOR_STRING, "Virtual Jaguar contributors");
    SDL_SetAppMetadataProperty(SDL_PROP_APP_METADATA_COPYRIGHT_STRING, "GPL-3.0-or-later");
    SDL_SetAppMetadataProperty(SDL_PROP_APP_METADATA_URL_STRING, platform::kWebsiteUrl);
    SDL_SetAppMetadataProperty(SDL_PROP_APP_METADATA_TYPE_STRING, "game");
    SDL_InitFlags flags = SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS;
    if (!headless_ || !a.tvTestScript.empty()) flags |= SDL_INIT_GAMEPAD;   // the TV script attaches a virtual pad
    if (!SDL_Init(flags)) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }
    sdlUp_ = true;
    if (!headless_) platform::beforeWindow();   // process identity (Windows AppUserModelID) before any window exists
    perfFreq_ = SDL_GetPerformanceFrequency();
    openDialogEvent_ = SDL_EVENT_USER;
    return true;
}

bool App::initWindow() {
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
#ifdef __APPLE__
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
#endif
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);

    int w = cfg.getInt("window.w", 960), h = cfg.getInt("window.h", 760);
    w = std::max(320, w);
    h = std::max(240, h);
    const std::string firstTitle = "Virtual Jaguar" + instanceTitleSuffix(paths.instance);
    SDL_WindowFlags wflags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE;
    if (!lowDpi_) wflags |= SDL_WINDOW_HIGH_PIXEL_DENSITY;   // --low-dpi: a 1x framebuffer, for screenshots of a standard display
    if (simDensity_) { w *= (int)simDensity_; h *= (int)simDensity_; }   // --pixel-density: the window holds N pixels per point
    window_ = SDL_CreateWindow(firstTitle.c_str(), w, h, wflags);
    if (!window_) {
        std::fprintf(stderr, "cannot create window: %s\n", SDL_GetError());
        return false;
    }
    gl_ = SDL_GL_CreateContext(window_);
    if (!gl_) {
        std::fprintf(stderr, "cannot create an OpenGL 3.3 core context: %s\n", SDL_GetError());
        return false;
    }
    SDL_GL_MakeCurrent(window_, gl_);
    applyVsync();
    SDL_SetWindowMinimumSize(window_, 320, 240);
    return true;
}

static std::string g_imguiIni;
static float g_simDensity = 1.0f;   // --pixel-density, for the platform handler (a plain function pointer)

bool App::initImGui() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    // No keyboard/gamepad navigation of the UI: those devices belong to the game.
    // (The UI is mouse-driven; WantCaptureKeyboard is then true only while typing.)
    io.ConfigFlags &= ~(ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad);
    g_imguiIni = paths.imguiIni;
    io.IniFilename = g_imguiIni.c_str();
    // Multi-viewport (docking branch): a window dragged outside the main window becomes an OS window of its own.
    // Docking itself stays off -- it would put tab bars and drop targets on windows people already arranged.
    // The ViewportsEnable flag itself follows the setting + the situation every frame (applyViewports()).
    io.ConfigViewportsNoAutoMerge = false;
    io.ConfigViewportsNoTaskBarIcon = true;     // a tool window is not a second app in the Dock / taskbar
    io.ConfigViewportsNoDecoration = false;     // real OS title bars (title = the window's name), native move / close
    io.ConfigDockingWithShift = true;
    ImGuiStyle& st = ImGui::GetStyle();
    st.WindowRounding = 4.0f;
    st.FrameRounding = 3.0f;
    // Colours: the theme in force (Settings > Interface > Appearance; System follows the OS, and
    // SDL_EVENT_SYSTEM_THEME_CHANGED re-applies them live).  Only colours change when the theme does, so the
    // sizes and rounding set here and scaled below stay as they are.
    theme::addListener([this](theme::Scheme) { applyTheme(); });
    theme::refreshSystem();
    // The UI scale is the display's CONTENT scale (the 200% of a 4K Windows/Linux display), nothing more.
    // SDL_GetWindowDisplayScale() is relative to the window's PIXEL size, so it also contains the pixel density
    // (2.0 on every Retina Mac); ImGui's SDL3 backend lays out in window points and already renders at the
    // pixel density (DisplayFramebufferScale), so using it directly doubled everything on a Retina Mac and
    // every window came out twice as wide as intended.  (Dear ImGui's own SDL3 example uses the content scale.)
    float scale = uiScaleOverride;
    if (scale <= 0.0f) {
        scale = uiscale::contentScale(SDL_GetWindowDisplayScale(window_), SDL_GetWindowPixelDensity(window_),
                                      cfg.getFloat("ui.scale", 1.0f));   // user preference on top
    }
    if (scale < 0.5f) scale = 1.0f;
    st.ScaleAllSizes(scale);
    fonts::setCustomDir(paths.root + "fonts/");   // the typeface itself is set up before every frame (fonts::applyPending)
    st.FontScaleDpi = scale;   // (the font size follows the display scale, not the theme: themes never set it)
    uiScale_ = scale;
    theme::applyStyle(st, uiScale_);   // the theme's colours, rounding and spacing at this scale

#ifdef __APPLE__
    const char* glsl = "#version 150";
#else
    const char* glsl = "#version 330 core";
#endif
    if (!ImGui_ImplSDL3_InitForOpenGL(window_, gl_)) return false;
    if (simDensity_) {   // --pixel-density: the multi-viewport path asks the platform handler too
        ImGui::GetPlatformIO().Platform_GetWindowFramebufferScale = [](ImGuiViewport*) {
            return ImVec2(g_simDensity, g_simDensity);
        };
    }
    if (!ImGui_ImplOpenGL3_Init(glsl)) return false;
    imguiUp_ = true;
    if (!video.init()) return false;
    return true;
}

void App::shutdown() {
    if (host.isLoaded()) autoSave();   // quitting: keep where the game was
    discordShutdown(*this);            // take the Discord status down (bounded: never waits long on Discord)
    neuroShutdown(*this);
    ach.shutdown();
    if (host.isLoaded()) host.unloadContent();
    invalidateThumbs();
    shutdownDebugTools();    // the Textures / Object list windows hold GL textures
    video.shutdown();
    if (imguiUp_) {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        imguiUp_ = false;
    }
    audio.close();
    input.shutdown();
    if (gl_) {
        SDL_GL_DestroyContext(gl_);
        gl_ = nullptr;
    }
    if (window_) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }
    if (sdlUp_) {
        SDL_Quit();
        sdlUp_ = false;
    }
}

void App::wireHost() {
    host.onAudio = [this](const int16_t* d, size_t n) {
        audioFramesSeen_ += n;
        if (!fastForward && !mute) audio.push(d, n);
    };
    host.inputPoll = [this]() {
        input.agentControls = neuro.frame();   // Neuro's buttons for this emulated frame
        input.poll(!inputCaptured_);
    };
    input.notify = [this](const std::string& m) { toast(m, 3.0); };
    host.optionLock = &lockedCoreOption;   // the app's own Controls / Devices tabs replace these core options
    host.inputState = [this](unsigned p, unsigned d, unsigned i, unsigned id) {
        return input.state(p, d, i, id);
    };
    host.audioBufferStatus = [this](bool& active, unsigned& occ, bool& underrun) {
        audio.bufferStatus(active, occ, underrun);
    };
    host.onOptionChanged = [this](const std::string& k, const std::string& v, bool erased) {
        if (erased) cfg.erase("opt." + k);
        else cfg.set("opt." + k, v);
    };
    host.setLogVerbose(false);

    // ---- debug tools (debug/debug_session.h) -----------------------------------------------------
    host.onDebugIface = [this](const vj_debug_iface* i) { debug.setIface(i); };
    debug.runUiFrame = [this] { haltedFrame(); };         // the core is parked in retro_run(): UI only
    debug.abortHalt = [this] { return quit; };            // closing the window while halted lets the core go
    debug.registerPlaceholderTools();                     // fixes the Tools menu order; each provider below replaces its entries
    debug.onHalt.push_back([this](int target, unsigned pc, int reason) {
        audio.setPaused(true);                            // no stale buffer droning while the game is stopped
        if (reason == VJ_DBG_STOP_STEP) return;
        const char* who = target == VJ_DBG_TARGET_GPU ? "GPU" : target == VJ_DBG_TARGET_DSP ? "DSP" : "68000";
        char msg[96];
        std::snprintf(msg, sizeof msg, "Halted: %s at $%06X - F9 continues, F10 steps", who, pc);
        toast(msg, 4.0);
    });
    debug.onResume.push_back([this] {
        audio.clear();
        audio.setPaused(paused);
        pacer_.reset();
        lastTick_ = now();                                // the halt must not count as elapsed emulation time
    });

    // Every tool window plugs in through DebugSession::addProvider (addDebugTools, debug/debug_ui.cpp).  After
    // host.onAudio is set above (the audio scope chains onto it) and paths.instance is settled (Textures).
    addDebugTools(*this);
}

void App::syncDebug() {
    debug.setContentLoaded(host.isLoaded());
    debug.setHardcore(ach.hardcoreActive());
    debug.tick(now(), host.isLoaded() && !paused && !debug.halted());
}

// One UI frame without stepping the emulation: events, ImGui, present.  The debugger's halt pump calls this
// repeatedly while the core is parked inside retro_run() at a breakpoint (docs/app-debug-tools.md), so
// nothing here may re-enter the core -- CoreHost refuses state loads, resets, loads and option changes
// while inFrame().
void App::haltedFrame() {
    if (!imguiUp_ || !window_) {
        debug.detachAndResume();   // no UI to release the core with: never leave it parked
        return;
    }
    SDL_Event e;
    while (SDL_PollEvent(&e)) handleEvent(e, false);
    if (quit) return;
    inputCaptured_ = ImGui::GetIO().WantCaptureKeyboard;
    inputFrame(*this);
    ach.pump();
    syncDebug();
    video.upload(host.frame());
    applyViewports();
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    simulateDensity();
    ImGui::NewFrame();
    bool capKb = false;
    drawUi(*this, capKb);
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    presentViewports();
    SDL_GL_SwapWindow(window_);
    updateTitle();
    if (!vsync) SDL_DelayNS(8000000);   // no swap-interval pacing: do not spin while stopped
}

const char* App::viewportsNote() const {
    if (!windowsOutside) return "";
    return viewports::explain(viewports::decide(true, SDL_GetCurrentVideoDriver(), fullscreen).why);
}

// Turns ImGui's multi-viewport mode on or off to match the setting and the situation; cheap, called before every
// NewFrame().  ImGui hands windows back to the main window (and closes the extra OS windows) when it goes off.
void App::applyViewports() {
    if (!imguiUp_ || headless_) return;
    const bool want = viewports::decide(windowsOutside, SDL_GetCurrentVideoDriver(), fullscreen).active;
    if (want == viewportsOn_) return;
    ImGuiIO& io = ImGui::GetIO();
    if (want) io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    else io.ConfigFlags &= ~ImGuiConfigFlags_ViewportsEnable;
    viewportsOn_ = want;
}

// After ImGui::Render() + the main window's draw data: draw and present the extra OS windows, then make the main
// window's GL context current again (the extra windows' backends switch it).  Official example_sdl3_opengl3 pattern.
void App::presentViewports() {
    if (!viewportsOn_) return;
    SDL_Window* backupWindow = SDL_GL_GetCurrentWindow();
    SDL_GLContext backupContext = SDL_GL_GetCurrentContext();
    ImGui::UpdatePlatformWindows();
    ImGui::RenderPlatformWindowsDefault();
    SDL_GL_MakeCurrent(backupWindow, backupContext);
}

void App::applyTheme() {
    if (imguiUp_) theme::applyStyle(ImGui::GetStyle(), uiScale_);
}

theme::Store App::themeStore() const { return theme::Store(paths.root + "themes", paths.instance > 1); }

bool App::applyThemeChoice(const std::string& key) {
    if (theme::parseMode(key) == theme::Mode::Custom) {
        theme::Custom c;
        if (themeStore().load(theme::customNameOf(key), c)) {
            theme::setCustom(c);
            return true;
        }
        theme::setMode(theme::Mode::System);   // the theme file is gone (or never was here): System
        return false;
    }
    theme::setMode(theme::parseMode(key));
    return true;
}

void App::setThemeChoice(const std::string& key) {
    themeFromCli_ = false;   // the user chose: from here on the choice is theirs to keep
    themePreviewing_ = false;
    applyThemeChoice(key);
}

void App::themePreview(const theme::Custom& c) {
    if (!themePreviewing_) {
        themeBeforePreview_ = theme::choiceKey();
        themePreviewing_ = true;
    }
    theme::setCustom(c);
}

void App::themePreviewEnd(bool keep) {
    if (!themePreviewing_) return;
    themePreviewing_ = false;
    if (!keep) applyThemeChoice(themeBeforePreview_);
}

void App::applyVsync() {
    pacer_.resetSwap(vsync);
    if (gl_) SDL_GL_SetSwapInterval(vsync ? 1 : 0);
}

// Vsync is a pacing tool for a core that has time to spare (pacing.h).  While the core is behind, SDL_GL_SwapWindow
// would block until the next vblank -- on average half a refresh interval, every UI frame -- on the very thread that
// runs the emulation, so the swap interval is released and restored once the core keeps up again.
void App::syncSwapInterval() {
    if (!gl_) return;
    if (pacer_.updateSwap(vsync)) SDL_GL_SetSwapInterval(pacer_.swapSync ? 1 : 0);
}

void App::applyAudioSettings() {
    audio.setVolume(volume);
    audio.setTargetMs(audioTargetMs);
}

void App::newWindow() {
    std::string err;
    if (!spawnNewInstance(dataDirArg_, err)) {
        toast("Could not open a new window: " + err, 5.0, 3);
        std::fprintf(stderr, "new window failed: %s\n", err.c_str());
        return;
    }
    toast("Opening another window...", 2.0);
}

bool App::inWindowMenuBar() const { return platform::nativeMenuActive() ? showInWindowMenu : true; }

void App::toggleLibrary() {
    if (homeVisible()) library.focusSearch = true;   // already showing: put the cursor in the search box
    else library.show = !library.show;
}

void App::setFullscreen(bool fs) {
    fullscreen = fs;
    if (window_) SDL_SetWindowFullscreen(window_, fs);
}

// ---------------------------------------------------------------------------------
// content
// ---------------------------------------------------------------------------------
void App::applyBios() {
    for (bios::Role r : {bios::CartBoot, bios::Cd}) {
        const std::string& f = !biosOnce_[r].empty() ? biosOnce_[r] : !biosCli_[r].empty() ? biosCli_[r] : biosChoice.of(r);
        host.setBiosPath(r, f);
    }
    host.setBootTarget(bootTarget_);
}

bool App::openContent(const std::string& pathIn) {
    if (host.inFrame()) {
        toast("The debugger has the game halted - resume it first", 2.5, 2);
        return false;
    }
    const std::string path = absolutePath(pathIn);
    if (tvShell().menuOpen()) tvShell().closeMenu();   // the menu belongs to the game it was opened on
    input.clearHolds();                   // a hold or tap pulse in flight belongs to the previous game
    pendingStateSlot_ = kNoPendingSlot;   // a state load queued for the previous game does not follow to this one
    resumeOffer.active = false;
    const bool noResume = suppressResume_ || reloading_;
    if (!reloading_) autoSave();          // leaving a running game for another: keep where it was
    host.setDirectories(paths.system, paths.saves);
    for (std::string& f : biosOnce_) f.clear();   // a game is not a BIOS boot
    bootTarget_ = 0;
    applyBios();
    std::string err;
    if (!host.loadContent(path, err)) {
        ach.onContentClosed();
        cheatsOnClose(*this);
        toast("Could not open " + pathStem(path) + ": " + err, 5.0, 3);
        std::fprintf(stderr, "open failed: %s\n", err.c_str());
        return false;
    }
    // An option override the core does not accept would silently run the default.
    badOverride_ = false;
    for (const std::string& p : host.checkOverrides(true)) {
        badOverride_ = true;
        std::fprintf(stderr, "option error: %s\n", p.c_str());
        toast("--option: " + p, 8.0, 3);
    }
    for (const std::string& p : host.checkOverrides(false))
        std::fprintf(stderr, "option warning (saved setting ignored): %s\n", p.c_str());
    audio.open(host.sampleRate());   // (re)opens, re-formatted for this game's region
    applyAudioSettings();
    audio.clear();
    audio.setPaused(false);
    paused = false;
    pacer_.reset();
    lastTick_ = now();
    input.setContent(contentIdentity(path));   // selects the per-game control overrides
    applyPortDevices(*this);                   // the controller types chosen in Settings, per game or global
    controlsOverlayOnLoad(*this);
    addRecent(path);
    library.noteLaunched(path);   // the library's recently played (library_lists.h)
    invalidateThumbs();
    debug.requestRefresh();
    ach.onContentLoaded();   // identify the game, look up its achievements
    discordNoteGameStarted(*this);
    cheatsAfterLoad(*this);   // the core drops its cheat list on every unload
    runSeconds_ = 0;
    toast("Loaded " + host.contentName());
    if (!noResume) offerResume();
    saveConfig();
    return true;
}

bool App::openNoContent() {
    if (host.inFrame()) {
        toast("The debugger has the game halted - resume it first", 2.5, 2);
        return false;
    }
    if (tvShell().menuOpen()) tvShell().closeMenu();   // the menu belongs to the game it was opened on
    input.clearHolds();                   // a hold or tap pulse in flight belongs to the previous game
    pendingStateSlot_ = kNoPendingSlot;   // booting a BIOS replaces the running game like opening another one does
    resumeOffer.active = false;
    autoSave();
    host.setDirectories(paths.system, paths.saves);
    applyBios();
    std::string err;
    if (!host.loadNoContent(err)) {
        ach.onContentClosed();
        toast("Could not boot: " + err, 5.0, 3);
        return false;
    }
    audio.open(host.sampleRate());
    applyAudioSettings();
    audio.clear();
    paused = false;
    pacer_.reset();
    lastTick_ = now();
    invalidateThumbs();
    ach.onContentClosed();   // no game, so nothing to look up
    discordNoteGameStarted(*this);
    cheatsOnClose(*this);
    input.setContent("");
    applyPortDevices(*this);
    const CoreHost::StagedBios& sb = host.stagedBios(bootTarget_ == VJ_BOOT_CART_BOOT_ROM ? bios::CartBoot : bios::Cd);
    toast(std::string(bootTarget_ == VJ_BOOT_CART_BOOT_ROM ? "Booted the cartridge boot ROM with an empty slot"
                                                           : "Booted the CD BIOS with no disc") +
          (sb.seen ? ": " + bios::describeStaged(sb.source, sb.where, sb.crc) : std::string()));
    return true;
}

bool App::bootBios(bios::Role role, const std::string& file) {
    if (host.inFrame()) {
        toast("The debugger has the game halted - resume it first", 2.5, 2);
        return false;
    }
    for (std::string& f : biosOnce_) f.clear();
    if (role == bios::CartBoot || role == bios::Cd) biosOnce_[role] = file.empty() ? std::string() : absolutePath(file);
    bootTarget_ = role == bios::CartBoot ? VJ_BOOT_CART_BOOT_ROM : VJ_BOOT_CD_BIOS;
    if (host.isLoaded()) host.flushSaveRam();
    return openNoContent();
}

void App::setBiosChoice(bios::Role role, const std::string& path) {
    if (role != bios::CartBoot && role != bios::Cd) return;
    biosChoice.of(role) = path.empty() ? std::string() : absolutePath(path);
    bios::storeSelection(cfg, biosChoice);
    cfg.save();
    applyBios();   // the core reads it at the next load
}

bool App::importBios(const std::string& path, bool trustSize) {
    const bios::Imported r = bios::importFile(absolutePath(path), paths.system, trustSize);
    toast(r.ok ? r.message + " It is now the " + bios::roleLabel(r.role) + " in use; restart the game to apply it."
               : r.message,
          5.0, r.ok ? 1 : 2);
    if (!r.ok) return false;
    setBiosChoice(r.role, r.path);
    library.rescan();
    return true;
}

bool App::reloadContent() {
    std::string p = host.contentPath();
    if (p.empty()) {
        if (host.isLoaded()) return openNoContent();
        return false;
    }
    reloading_ = true;   // a deliberate restart: neither keeps nor resumes an auto-save
    bool ok = openContent(p);
    reloading_ = false;
    return ok;
}

void App::closeContent() {
    if (!host.isLoaded()) return;
    if (host.inFrame()) {
        toast("The debugger has the game halted - resume it first", 2.5, 2);
        return;
    }
    if (tvShell().menuOpen()) tvShell().closeMenu();   // gives the pause back before the game goes
    input.clearHolds();
    pendingStateSlot_ = kNoPendingSlot;
    resumeOffer.active = false;
    autoSave();
    host.unloadContent();
    debug.requestRefresh();
    ach.onContentClosed();
    cheatsOnClose(*this);
    audio.clear();
    invalidateThumbs();
    input.setContent("");
}

void App::reset() {
    if (!host.isLoaded()) return;
    if (host.inFrame()) {
        toast("The debugger has the game halted - resume it first", 2.5, 2);
        return;
    }
    host.reset();
    debug.requestRefresh();
    ach.onReset();
    toast("Reset");
}

void App::togglePause() {
    if (menuHeldPause_) {   // the TV menu holds the pause: a toggle here would be overwritten when it closes
        toast("Close the menu to resume", 1.5);
        return;
    }
    if (!paused) {
        // Hardcore rations pausing (rc_client_can_pause); softcore never refuses.
        std::string why;
        if (!ach.allowPause(why)) {
            toast(why, 2.5, 2);
            return;
        }
    }
    paused = !paused;
    audio.setPaused(paused);
    toast(paused ? "Paused" : "Resumed", 1.2);
}

// The TV in-game menu's pause: the same gate and the same pause as togglePause(), without its toast, and it keeps
// what was true before so that a game the player had paused stays paused when the menu closes.  A netplay session
// keeps running (a pause would stall the other side), so there the menu only lays over the picture.
bool App::menuPause(std::string& why) {
    if (menuHeldPause_) return true;   // already open
    if (netplay.configured()) {
        input.setMenuOwnsInput(true);   // running on, but not for the pads: the menu has them
        return true;
    }
    if (!paused) {
        if (!ach.allowPause(why)) return false;   // hardcore rations pausing (rc_client_can_pause)
        paused = true;
        audio.setPaused(true);
        pausedBeforeMenu_ = false;
    } else {
        pausedBeforeMenu_ = true;
    }
    menuHeldPause_ = true;
    input.setMenuOwnsInput(true);
    return true;
}

void App::menuResume() {
    input.setMenuOwnsInput(false);   // before the caller arms the release guard
    if (!menuHeldPause_) return;
    menuHeldPause_ = false;
    if (paused != pausedBeforeMenu_) {
        paused = pausedBeforeMenu_;
        audio.setPaused(paused);
    }
}

tv::Shell& App::tvShell() { return *tvShell_; }

void App::showOpenDialog() {
    if (!window_) return;
    g_filterPattern = joinExtensions(CoreHost::validExtensions());
    g_filters[0].name = g_filterName.c_str();
    g_filters[0].pattern = g_filterPattern.c_str();
    g_filters[1].name = "All files";
    g_filters[1].pattern = "*";
    SDL_ShowOpenFileDialog(dialogDone, this, window_, g_filters, 2,
                           g_lastDir.empty() ? nullptr : g_lastDir.c_str(), false);
}

// ---- resume where I left off ------------------------------------------------------------------------------------
// Policy in library_lists.h (shouldAutoSave / resumeAction), tested in vj_library_tests.  The auto-save is its own
// slot (lib::kAutoSlot): one file pair per game, always the latest, written like any state (temp file + rename).
bool App::autoSave() {
    if (headless_ || !host.isLoaded() || host.contentPath().empty()) return false;   // headless runs and BIOS-only boots never
    if (!lib::shouldAutoSave(resumeMode, !ach.allowStateLoad(), netplay.configured(), runSeconds_, host.crashEvents() > 0))
        return false;
    return saveState(lib::kAutoSlot, true);
}

void App::offerResume() {
    if (headless_ || host.contentPath().empty()) return;
    const bool exists = fileExists(statePath(lib::kAutoSlot));
    switch (lib::resumeAction(resumeMode, !ach.allowStateLoad(), netplay.configured(), exists)) {
    case lib::ResumeAction::Load:
        pendingStateSlot_ = lib::kAutoSlot;   // after the first frames, like a state picked in the library
        pendingStateFrames_ = 2;
        break;
    case lib::ResumeAction::Ask:
        resumeOffer.active = true;
        resumeOffer.savedAt = slotTime(lib::kAutoSlot);
        resumeOffer.game = host.contentName();
        break;
    case lib::ResumeAction::None: break;
    }
}

void App::answerResume(bool resume, bool always) {
    if (!resumeOffer.active) return;
    resumeOffer.active = false;
    if (resume) {
        if (always) {
            resumeMode = lib::ResumeAlways;
            saveConfig();
        }
        loadState(lib::kAutoSlot);
    }
}

// The library's save-state tab and tile menu: the game running already (same file) just loads the state;
// anything else is opened first and the state loaded once it has run a couple of frames.
void App::openAndLoadState(const std::string& pathIn, int slot) {
    if (pathIn.empty() || slot < lib::kAutoSlot) return;
    const std::string path = absolutePath(pathIn);
    if (!ach.allowStateLoad()) {   // before leaving the running game for one whose state would be refused
        toast("Hardcore mode: loading save states is disabled", 3.5, 2);
        return;
    }
    if (slot >= 0) stateSlot = slot;
    if (host.isLoaded() && host.contentPath() == path) {
        loadState(slot);
        return;
    }
    suppressResume_ = true;   // the state asked for replaces the auto-save offer
    handleDrop(path);
    suppressResume_ = false;
    if (host.isLoaded() && host.contentPath() == path) pendingStateSlot_ = slot;   // cleared if the open failed
    pendingStateFrames_ = 2;
}

void App::showImportBiosDialog() {
    if (!window_) return;
    static SDL_DialogFileFilter filters[2] = {{"BIOS images", "j64;rom;bin;jag"}, {"All files", "*"}};
    SDL_ShowOpenFileDialog(importDialogDone, this, window_, filters, 2, g_lastDir.empty() ? nullptr : g_lastDir.c_str(), false);
}

void App::handleDrop(const std::string& path) {
    if (path.empty()) return;
    // A boot ROM or CD BIOS dropped on the window (or picked with File > Open) is a BIOS to import, not a game:
    // running it as a cartridge only shows garbage.
    if (bios::importRole(path) != bios::None) {
        importBios(path);
        return;
    }
    // Save the old game's SRAM first (openContent -> loadContent does too, but
    // an early flush keeps a failed open from losing anything).
    if (host.isLoaded()) host.flushSaveRam();
    openContent(path);
}

// ---------------------------------------------------------------------------------
// save states
// ---------------------------------------------------------------------------------
std::string App::statePath(int slot) const {
    if (slot == lib::kAutoSlot) return lib::autoStatesDir(paths.states, paths.instance) + lib::stateFileName(host.contentName(), slot);
    return paths.states + lib::stateFileName(host.contentName(), slot);
}
std::string App::thumbPath(int slot) const {
    if (slot == lib::kAutoSlot) return lib::autoStatesDir(paths.states, paths.instance) + lib::thumbFileName(host.contentName(), slot);
    return paths.states + lib::thumbFileName(host.contentName(), slot);
}
bool App::slotExists(int slot) const {
    return host.isLoaded() && fileExists(statePath(slot));
}
SDL_Time App::slotTime(int slot) const {
    SDL_PathInfo info;
    if (SDL_GetPathInfo(statePath(slot).c_str(), &info)) return info.modify_time;
    return 0;
}

bool App::saveState(int slot, bool quiet) {
    if (!host.isLoaded()) {
        if (!quiet) toast("No game loaded");
        return false;
    }
    if (slot == lib::kAutoSlot) SDL_CreateDirectory(lib::autoStatesDir(paths.states, paths.instance).c_str());
    std::string err;
    if (!host.saveStateToFile(statePath(slot), err)) {   // written atomically (temp file + rename)
        if (!quiet) toast("Save state failed: " + err, 4.0, 3);
        return false;
    }
    const Frame& f = host.frame();
    if (f.width && f.height) {
        SDL_Surface* s = SDL_CreateSurfaceFrom((int)f.width, (int)f.height, SDL_PIXELFORMAT_XRGB8888,
                                               (void*)f.pixels.data(), (int)f.width * 4);
        if (s) {
            // the screenshot is replaced atomically too: the library tab may be reading it
            const std::string dst = thumbPath(slot), tmp = dst + "." + std::to_string(processId()) + ".tmp";
            if (SDL_SavePNG(s, tmp.c_str())) {
                if (!SDL_RenamePath(tmp.c_str(), dst.c_str())) SDL_RemovePath(tmp.c_str());
            }
            SDL_DestroySurface(s);
        }
    }
    auto it = thumbs_.find(slot);
    if (it != thumbs_.end()) {
        VideoOut::destroyTexture(it->second.tex);
        thumbs_.erase(it);
    }
    ach.onStateSaved(statePath(slot));
    if (!quiet) toast("State saved to " + lib::stateLabel(slot));
    return true;
}

bool App::loadState(int slot) {
    if (!host.isLoaded()) {
        toast("No game loaded");
        return false;
    }
    if (!ach.allowStateLoad()) {
        toast("Hardcore mode: loading save states is disabled", 3.5, 2);
        return false;
    }
    if (!slotExists(slot)) {
        toast(lib::stateLabel(slot) + " is empty");
        return false;
    }
    std::string err;
    if (!host.loadStateFromFile(statePath(slot), err)) {
        toast("Load state failed: " + err, 4.0, 3);
        return false;
    }
    audio.clear();
    cheatsAfterStateLoad(*this);
    debug.requestRefresh();
    ach.onStateLoaded(statePath(slot));
    toast("State loaded from " + lib::stateLabel(slot));
    return true;
}

const SlotThumb* App::slotThumb(int slot) {
    if (!host.isLoaded() || !imguiUp_) return nullptr;
    SDL_PathInfo info;
    if (!SDL_GetPathInfo(thumbPath(slot).c_str(), &info)) return nullptr;
    auto it = thumbs_.find(slot);
    if (it != thumbs_.end() && it->second.mtime == info.modify_time) return &it->second;
    if (it != thumbs_.end()) {
        VideoOut::destroyTexture(it->second.tex);
        thumbs_.erase(it);
    }
    SDL_Surface* png = SDL_LoadPNG(thumbPath(slot).c_str());
    if (!png) return nullptr;
    SDL_Surface* rgba = SDL_ConvertSurface(png, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(png);
    if (!rgba) return nullptr;
    SlotThumb t;
    t.w = rgba->w;
    t.h = rgba->h;
    t.mtime = info.modify_time;
    // Tight copy: the surface pitch need not equal width * 4.
    std::vector<unsigned char> tight((size_t)t.w * t.h * 4);
    for (int y = 0; y < t.h; y++)
        std::memcpy(&tight[(size_t)y * t.w * 4], (const unsigned char*)rgba->pixels + (size_t)y * rgba->pitch,
                    (size_t)t.w * 4);
    SDL_DestroySurface(rgba);
    t.tex = VideoOut::createTextureRGBA(tight.data(), t.w, t.h);
    return &(thumbs_[slot] = t);
}

void App::invalidateThumbs() {
    if (imguiUp_)
        for (auto& kv : thumbs_) VideoOut::destroyTexture(kv.second.tex);
    thumbs_.clear();
}

// ---------------------------------------------------------------------------------
// events / hotkeys
// ---------------------------------------------------------------------------------
void App::handleHotkey(const SDL_KeyboardEvent& k) {
    // With a native menu bar the Cmd shortcuts belong to it (it consumes them before SDL sees a key), so the
    // in-window Ctrl/Cmd hotkeys must not fire a second time.
    bool mod = (k.mod & (SDL_KMOD_GUI | SDL_KMOD_CTRL)) != 0 && !platform::nativeMenuActive();
    bool alt = (k.mod & SDL_KMOD_ALT) != 0;
    bool shift = (k.mod & SDL_KMOD_SHIFT) != 0;
    switch (k.key) {
    case SDLK_F11: setFullscreen(!fullscreen); break;
    case SDLK_RETURN:
        if (alt) setFullscreen(!fullscreen);
        break;
    case SDLK_ESCAPE:
        if (tvShell().ownsInput() && !tvShell().backAtRoot()) break;   // Back on a TV screen, not "leave fullscreen"
        if (fullscreen) setFullscreen(false);
        break;
    case SDLK_F1:
        if (platform::nativeMenuActive()) showInWindowMenu = !showInWindowMenu;   // reach anything the native menu lacks
        else menuPinned = !menuPinned;
        break;
    case SDLK_F5: saveState(stateSlot); break;
    case SDLK_F7: loadState(stateSlot); break;
    case SDLK_F6:
        stateSlot = (stateSlot + (shift ? 9 : 1)) % 10;
        toast("State slot " + std::to_string(stateSlot), 1.2);
        break;
    case SDLK_F8: togglePause(); break;
    case SDLK_F9:   // debugger: continue when halted, otherwise break at the next instruction
        if (debug.halted()) debug.resume();
        else if (debug.enabled()) debug.breakNow();
        break;
    case SDLK_O:
        if (mod) showOpenDialog();
        break;
    case SDLK_R:
        if (mod) reset();
        break;
    case SDLK_COMMA:
        if (mod) showSettings = !showSettings;
        break;
    case SDLK_L:
        if (mod) toggleLibrary();
        break;
    case SDLK_N:
        if (mod) newWindow();
        break;
    default: break;
    }
}

// True for a window / pointer event that belongs to one of ImGui's extra OS windows (viewports): Settings dragged
// out of the main window and the like.  Closing one is ImGui's business (it only closes that window), and its
// pointer coordinates are relative to it, so the light gun, the mouse capture and the drop handling must not see
// them.  Keys are still delivered: they go to whichever window has focus and ImGui's WantCaptureKeyboard guards them.
bool App::fromExtraWindow(const SDL_Event& e) const {
    if (!viewportsOn_ || !window_) return false;
    SDL_WindowID id = 0;
    if (e.type >= SDL_EVENT_WINDOW_FIRST && e.type <= SDL_EVENT_WINDOW_LAST) id = e.window.windowID;
    else if (e.type == SDL_EVENT_MOUSE_MOTION) id = e.motion.windowID;
    else if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN || e.type == SDL_EVENT_MOUSE_BUTTON_UP) id = e.button.windowID;
    else if (e.type == SDL_EVENT_MOUSE_WHEEL) id = e.wheel.windowID;
    else if (e.type == SDL_EVENT_DROP_FILE || e.type == SDL_EVENT_DROP_POSITION) id = e.drop.windowID;
    return id != 0 && id != SDL_GetWindowID(window_);
}

void App::handleEvent(const SDL_Event& e, bool headless) {
    if (!headless && imguiUp_) ImGui_ImplSDL3_ProcessEvent(&e);
    if (fromExtraWindow(e)) return;   // ImGui has it; the game and the app's own window handling are about the main window
    // F10 steps while a processor is halted (it releases a captured mouse otherwise, which input.cpp owns).
    if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat && e.key.scancode == SDL_SCANCODE_F10 && debug.halted() &&
        !(imguiUp_ && ImGui::GetIO().WantTextInput)) {
        debug.step();
        return;
    }
    input.feedHoldEdges(e);   // before anything can consume the event, so a release is never lost
    tvHost_->noteInput(e);    // which device the hints should name (the shell consumes events InputManager never sees)
    if (tvShell().ownsInput() && tvShell().handleEvent(e, SDL_GetTicks())) return;   // the TV UI has the pads and keys
    if (input.handleEvent(e)) return;
    switch (e.type) {
    case SDL_EVENT_QUIT:
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        quit = true;
        break;
    case SDL_EVENT_DROP_POSITION:
        dropX_ = e.drop.x;
        dropY_ = e.drop.y;
        break;
    case SDL_EVENT_DROP_FILE:
        if (!e.drop.data) break;
        {
            // A PNG dropped on a library tile is that tile's cover; anything else opens as before.
            const std::string path = e.drop.data;
            if (imguiUp_ && (homeVisible() || library.show) && pathExt(path) == "png") {
                const float x = (e.drop.x != 0.0f || e.drop.y != 0.0f) ? e.drop.x : dropX_;
                const float y = (e.drop.x != 0.0f || e.drop.y != 0.0f) ? e.drop.y : dropY_;
                const LibraryService::CoverTile* t = library.tileAt(x + ImGui::GetMainViewport()->Pos.x, y + ImGui::GetMainViewport()->Pos.y, ImGui::GetFrameCount());
                if (t) {
                    library.setCoverKey(t->key, path);
                    break;
                }
            }
            handleDrop(path);
        }
        break;
    case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
        fullscreen = true;    // also the green button / Ctrl+Cmd+F on macOS, which never go through setFullscreen()
        break;
    case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
        fullscreen = false;
        break;
    case SDL_EVENT_USER: {
        platform::Cmd ncmd = platform::Cmd::None;
        int narg = 0;
        if (platform::decodeCommandEvent(e, ncmd, narg)) {   // a native menu / tray click (platform.h)
            runNativeCommand(ncmd, narg);
        } else if (e.user.code == 0x56414A47 && e.user.data1) {
            std::string p = (const char*)e.user.data1;
            SDL_free(e.user.data1);
            handleDrop(p);
        } else if (e.user.code == 0x56414A53 && e.user.data1) {   // 'VAJS': the library's "load this state"
            std::string p = (const char*)e.user.data1;
            SDL_free(e.user.data1);
            openAndLoadState(p, (int)(intptr_t)e.user.data2 - 1000);
        } else if (e.user.code == 0x56424A49 && e.user.data1) {   // 'VBJI': import this BIOS file
            std::string p = (const char*)e.user.data1;
            SDL_free(e.user.data1);
            importBios(p, true);   // picked in the BIOS import dialog: taken by size when nothing recognises it
        }
        break;
    }
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP: {
        bool captured = imguiUp_ && ImGui::GetIO().WantCaptureKeyboard;
        if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat && !captured) handleHotkey(e.key);
        // (a key-down under the TV menu is the menu's, not the game's; a key-up always goes through, or a key pressed
        // before the menu opened would stay down in the core)
        if (!captured && host.isLoaded() && (e.type == SDL_EVENT_KEY_UP || !tvShell().menuOpen())) {
            unsigned rk = InputManager::retroKey(e.key.scancode);
            if (rk && !(e.type == SDL_EVENT_KEY_DOWN && input.withholdsKey((int)e.key.scancode)))
                host.sendKeyEvent(e.type == SDL_EVENT_KEY_DOWN, rk, 0, 0);
        }
        if (e.key.scancode == SDL_SCANCODE_TAB && !captured) fastForward = (e.type == SDL_EVENT_KEY_DOWN);
        break;
    }
    case SDL_EVENT_SYSTEM_THEME_CHANGED:   // the OS switched between light and dark: a System choice follows
        theme::handleEvent(e);
        break;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        fastForward = false;
        break;
    case SDL_EVENT_PINCH_UPDATE:
        // Trackpad pinch (macOS; SDL sends the change since the last update): zooms the library's tiles, and only
        // while the library is showing with the pointer over its grid and no text field being edited.
        if (imguiUp_ && (homeVisible() || library.show) && !ImGui::GetIO().WantTextInput &&
            ImGui::GetFrameCount() - library.gridHoverFrame <= 1) {
            library.cellScale = lib::pinchTileScale(library.cellScale, e.pinch.scale);
            library.zoomChanged();
        }
        break;
    default: break;
    }
}

// ---------------------------------------------------------------------------------
// emulation stepping
// ---------------------------------------------------------------------------------
void App::stepEmulation() {
    double t = now();
    double dt = t - lastTick_;
    lastTick_ = t;
    if (!host.isLoaded() || paused || debug.halted()) {
        pacer_.reset();
        return;
    }
    double period = 1.0 / std::max(1.0, host.fps());
    int frames = 0;
    if (fastForward) {
        frames = 4;
        pacer_.reset();
    } else {
        frames = pacer_.plan(dt, period);
    }
    for (int i = 0; i < frames; i++) {
        host.wantVideo = (i == frames - 1);
        if (!fastForward) audio.update();
        uint64_t r0 = perf.on ? perf.tick() : 0;
        host.runFrame();
        if (r0) perf.add(Perf::Retro, 1000.0 * (double)(perf.tick() - r0) / (double)perf.freq);
        perf.emulated();
        ach.afterFrame();
        netplay.afterFrame();
        fpsWindowFrames_++;
        runSeconds_ += 1.0 / std::max(1.0, host.fps());   // emulated time of this game (paused time does not count)
    }
    host.wantVideo = true;
    if (!fastForward && frames > 0) {
        // The frames just run took real time that the next step would otherwise only see as dt: charge it now, so
        // "is a frame already due again?" is answered from the true backlog (Pacer::behind, pacing.h).
        double t2 = now();
        pacer_.charge(t2 - lastTick_, period);
        lastTick_ = t2;
    } else {
        pacer_.behind = false;
    }
    if (pendingStateSlot_ != kNoPendingSlot && frames > 0) {
        pendingStateFrames_ -= frames;
        if (pendingStateFrames_ <= 0) {
            const int slot = pendingStateSlot_;
            pendingStateSlot_ = kNoPendingSlot;
            loadState(slot);
        }
    }
    for (const OsdMessage& m : host.takeMessages())
        toast(m.text, m.durationMs / 1000.0, m.level);
}

void App::updateTitle() {
    double t = now();
    if (t - fpsWindowStart_ >= 1.0) {
        fpsShown_ = (float)((double)fpsWindowFrames_ / (t - fpsWindowStart_));
        fpsWindowFrames_ = 0;
        fpsWindowStart_ = t;
    }
    if (!window_ || t - lastTitle_ < 1.0) return;
    lastTitle_ = t;
    std::string title = "Virtual Jaguar";
    if (host.isLoaded() && !host.contentName().empty()) title += " - " + host.contentName();
    title += instanceTitleSuffix(paths.instance);
    if (paused) title += " [paused]";
    SDL_SetWindowTitle(window_, title.c_str());
}

uint32_t App::frameHash() const {
    const Frame& f = host.frame();
    uint32_t h = 2166136261u;  // FNV-1a, same as test/harness/harness.c
    const unsigned char* p = (const unsigned char*)f.pixels.data();
    size_t n = (size_t)f.width * f.height * 4;
    for (size_t i = 0; i < n; i++) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}

// ---------------------------------------------------------------------------------
// run
// ---------------------------------------------------------------------------------
int App::run(const CliArgs& a) {
    if (!initSdl(a)) return 1;
    std::string err;
    std::string dataDir = a.dataDir;
    if (a.headless && dataDir.empty()) {
        // A clean, private data root: a previous run's SRAM must never leak into
        // a smoke test (it would make "same ROM, same hash" meaningless).
        std::error_code ec;
        std::filesystem::path tmp = std::filesystem::temp_directory_path(ec);
        if (ec) tmp = ".";
        tmp /= "vj-headless-" + std::to_string((unsigned long long)SDL_GetPerformanceCounter());
        headlessTmp_ = tmp.string();
        dataDir = headlessTmp_;
    }
    if (!paths.resolve(dataDir, err)) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }
    dataDirArg_ = a.dataDir.empty() ? "" : absolutePath(a.dataDir);
    // Which copy is this?  The first on a data folder is instance 1 and behaves exactly as it always
    // did; later ones get their own save folder and never write the shared settings (instance.h).
    const int inst = instanceLock_.acquire(paths.root);
    if (inst == 0) {
        std::fprintf(stderr, "%d copies of Virtual Jaguar are already running on %s\n", kMaxInstances, paths.root.c_str());
        return 1;
    }
    if (!paths.applyInstance(inst, err)) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }
    host.setLogVerbose(a.verbose);
    if (!a.theme.empty()) {
        themeFromCli_ = true;
        applyThemeChoice(a.theme);
    }
    loadConfig();
    if (paths.instance > 1) {
        host.setSaveFallbackDir(paths.savesShared);
        std::fprintf(stderr, "[app] instance %d (saves in %s)\n", paths.instance, paths.saves.c_str());
    }
    wireHost();
    ach.init(*this, a);
    cheatsInit(*this, a);
    host.setLogVerbose(a.verbose);
    for (const auto& kv : a.options) host.overrideOption(kv.first, kv.second);
    if (!a.cartBios.empty()) biosCli_[bios::CartBoot] = absolutePath(a.cartBios);
    if (!a.cdBios.empty()) biosCli_[bios::Cd] = absolutePath(a.cdBios);
    bootTarget_ = a.bootBios == "cart" ? VJ_BOOT_CART_BOOT_ROM : VJ_BOOT_CD_BIOS;
    if (!netplayInit(*this, a)) return 2;
    neuroInit(*this, a);

    int rc = a.headless ? (a.tvTestScript.empty() ? runHeadless(a) : runTvScript(a)) : runInteractive(a);

    saveConfig();
    shutdown();
    if (!headlessTmp_.empty()) {
        std::error_code ec;
        std::filesystem::remove_all(headlessTmp_, ec);   // our own temp dir only
    }
    return rc;
}

int App::runInteractive(const CliArgs& a) {
    uiScaleOverride = a.uiScale;
    lowDpi_ = a.lowDpi || a.pixelDensity > 1;   // --pixel-density builds its own density on a 1x window
    simDensity_ = a.pixelDensity > 1 ? std::min(4u, a.pixelDensity) : 0;
    g_simDensity = simDensity_ ? (float)simDensity_ : 1.0f;
    if (!initWindow() || !initImGui()) return 1;
    desktopInit();
    if (!a.font.empty()) {
        if (!fonts::applySpec(a.font, fontSettings)) std::fprintf(stderr, "--font: cannot read '%s'\n", a.font.c_str());
        fonts::request(fontSettings);
    }
    unsigned fontFrames = 0;
    applyAudioSettings();
    audio.open(48043.6);
    fullscreen = false;
    if (a.fullscreen) setFullscreen(true);
    if (a.showWindow.rfind("settings", 0) == 0) {
        showSettings = true;
        if (a.showWindow.rfind("settings-video", 0) == 0) devSelectTab = "Video";
        else if (a.showWindow.rfind("settings-input", 0) == 0) devSelectTab = "Input";
        else if (a.showWindow.rfind("settings-audio", 0) == 0) devSelectTab = "Audio";
        else if (a.showWindow.rfind("settings-storage", 0) == 0) devSelectTab = "Storage";
        else if (a.showWindow.rfind("settings-interface", 0) == 0) devSelectTab = "Interface";
        else if (a.showWindow.rfind("settings-discord", 0) == 0) devSelectTab = "Discord";
        else if (a.showWindow.rfind("settings-neuro", 0) == 0) devSelectTab = "Neuro";
        else if (a.showWindow.rfind("settings-bios", 0) == 0) devSelectTab = "BIOS";
        size_t c = a.showWindow.find(':');   // settings:FILTER pre-fills the options search box
        if (c != std::string::npos) std::snprintf(optionFilter, sizeof optionFilter, "%s", a.showWindow.c_str() + c + 1);
    }
    else if (a.showWindow == "states") showStates = true;
    else if (a.showWindow == "about") showAbout = true;
    else if (a.showWindow == "cheats") showCheats = true;
    else if (a.showWindow == "login") ach.showLogin = true;
    else if (a.showWindow == "achievements") ach.showList = true;
    else if (a.showWindow == "keypad") input.overlay.open = true;
    else if (a.showWindow == "netplay") showNetplay = true;
    else if (a.showWindow == "theme-editor") showThemeEditor = true;
    else if (a.showWindow == "controls") input.controls.show = true;
    else if (openDebugWindow(debug, a.showWindow)) {}   // memory, palette, scanner, objects, blitter, textures, scope
    unsigned uiFrames = 0;

    std::string firstErr;
    if (paths.instance > 1)
        toast("Instance " + std::to_string(paths.instance) + ": game saves go to saves/instance-" +
                  std::to_string(paths.instance) + "/, and settings changes are not kept (except window size and controllers)",
              7.0);
    if (a.noContent) openNoContent();
    else if (!a.content.empty()) openContent(a.content[0]);
    library.createTexture = &VideoOut::createTextureRGBA;
    library.destroyTexture = &VideoOut::destroyTexture;
    ach.images.createTexture = &VideoOut::createTextureRGBA;   // RetroAchievements avatar / icons / badges
    ach.images.destroyTexture = &VideoOut::destroyTexture;
    // With the home-screen library the boot screen already is the library; the floating window is for in-game use.
    library.start(paths, cfg, !host.isLoaded() && !homeLibrary);
    if (a.showWindow == "library") library.show = true;

    // dev: drive the TV menu from a script (a screenshot of it).  A script that cannot start ends the run through the normal teardown.
    const bool scriptFailed = !a.tvTestScript.empty() && !tvScriptBegin(a, true);
    if (scriptFailed) quit = true;

    lastTick_ = now();
    fpsWindowStart_ = now();
    lastSramFlush_ = now();
    perf.init(a.perfFrames, a.perfGpu);
    video.shader.reuseOutput = !a.perfNoReuse;
    perf.forceW = a.perfShaderW;
    perf.forceH = a.perfShaderH;
    video.perf = &perf;

    while (!quit) {
        if (perf.frameBegin()) {
            int pw = 0, ph = 0;
            SDL_GetWindowSizeInPixels(window_, &pw, &ph);
            perf.note = "window " + std::to_string(pw) + "x" + std::to_string(ph) + " px, vsync " + (vsync ? "on" : "off") +
                        ", shader " + (video.shader.active() ? pathStem(video.shader.preset) : std::string("none")) +
                        ", source " + std::to_string(video.frameWidth()) + "x" + std::to_string(video.frameHeight());
            perf.note += ", chain runs " + std::to_string(video.shader.chainRuns()) + " reuses " +
                         std::to_string(video.shader.chainReuses());
            perf.report(host.contentName().c_str());
            break;
        }
        if (a.perfPause && perf.seen == 30 && !paused && host.isLoaded()) togglePause();   // after a few real frames
        SDL_Event e;
        while (SDL_PollEvent(&e)) handleEvent(e, false);
        if (quit) break;
        if (tvScript_) tvScriptStep();   // the script's lines that are due (the virtual pad stays until shutdown: the hints name it)

        inputCaptured_ = ImGui::GetIO().WantCaptureKeyboard;
        inputFrame(*this);   // mouse capture, the controls-overlay key
        ach.pump();
        syncDebug();
        discordTick(*this);
        neuroTick(*this);
        perf.lap(Perf::Events);
        stepEmulation();
        perf.lap(Perf::Step);
        video.setSourceFps((float)host.fps());
        video.upload(host.frame());
        if (perf.on && perf.gpu) Perf::finish();
        perf.lap(Perf::Upload);

        if (!a.fontThen.empty() && ++fontFrames == 60) {
            if (fonts::applySpec(a.fontThen, fontSettings)) fonts::request(fontSettings);
        }
        fonts::applyPending();   // a typeface change made last frame, before anything draws with the old one
        applyViewports();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        simulateDensity();
        ImGui::NewFrame();
        if (a.verbose && !densityLogged_ && ++densityFrames_ >= 5) logDensity();
        bool capKb = false;
        drawUi(*this, capKb);
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        if (perf.on && perf.gpu) Perf::finish();
        perf.lap(Perf::Ui);
        if (!a.screenshot.empty() && ++uiFrames == a.screenshotFrame) {
            saveWindowScreenshot(a.screenshot);
            quit = true;
        }
        presentViewports();
        SDL_GL_SwapWindow(window_);
        perf.lap(Perf::Present);

        updateTitle();
        desktopFrame();
        double t = now();
        if (autosaveSram && host.isLoaded() && t - lastSramFlush_ > 5.0) {
            host.flushSaveRam();
            lastSramFlush_ = t;
        }
        syncSwapInterval();
        if (!pacer_.swapSync) {
            // No swap-interval pacing: sleep until the next emulated frame is due -- never when the core is behind.
            double period = 1.0 / std::max(1.0, host.fps());
            double wait = (host.isLoaded() && !paused) ? pacer_.sleepFor(t - lastTick_, period) : 0.008;
            if (wait > 0) SDL_DelayNS((Uint64)(wait * 1e9));
        }
        perf.lap(Perf::Sleep);
        perf.curSync = pacer_.swapSync;
        perf.frameEnd();
    }

    int w = 0, h = 0;
    if (window_ && !fullscreen) {
        SDL_GetWindowSize(window_, &w, &h);
        cfg.setInt("window.w", w);
        cfg.setInt("window.h", h);
    }
    if (host.isLoaded()) host.flushSaveRam(true);
    tvScriptEnd();
    desktopShutdown();
    library.stop();   // joins its threads and frees its textures while the GL context is still alive
    if (a.verbose) {
        double secs = now() - fpsWindowStart_;
        std::fprintf(stderr, "[app] audio: queued %.1f ms, ratio %.5f, underruns %llu\n", audio.queuedMs(),
                     audio.ratio(), (unsigned long long)audio.underruns());
        (void)secs;
    }
    return scriptFailed ? 2 : 0;
}

// --verbose, once, inside a frame: the numbers that decide how crisp the text is.  The glyph atlas bakes at
// "baked density"; on a Retina display it must equal the pixel density (2), or glyphs are rasterised at 1x and the GPU
// stretches them.
void App::logDensity() {
    densityLogged_ = true;
    ImGuiIO& io = ImGui::GetIO();
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImFont* f = ImGui::GetFont();
    ImFontBaked* b = f ? f->GetFontBaked(ImGui::GetFontSize()) : nullptr;
    std::fprintf(stderr,
                 "[ui] pixel density %.2f, SDL display scale %.2f, io framebuffer scale %.2f, main viewport framebuffer "
                 "scale %.2f, UI scale %.2f (FontScaleDpi %.2f), font %.1f px baked at density %.2f, viewports %s\n",
                 SDL_GetWindowPixelDensity(window_), SDL_GetWindowDisplayScale(window_), io.DisplayFramebufferScale.x,
                 vp ? vp->FramebufferScale.x : 0.0f, uiScale_, ImGui::GetStyle().FontScaleDpi, ImGui::GetFontSize(),
                 b ? b->RasterizerDensity : 0.0f, viewportsOn_ ? "on" : "off");
}

// --pixel-density N (dev): the backend reports a window of N*W x N*H pixels at 1 pixel per point; what a Retina Mac
// reports is W x H points at N pixels per point.  Rewrites the two numbers the backend just set so the UI lays out
// in points and rasterises at N, and the screenshot shows what the display would.
void App::simulateDensity() {
    if (!simDensity_) return;
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(io.DisplaySize.x / (float)simDensity_, io.DisplaySize.y / (float)simDensity_);
    io.DisplayFramebufferScale = ImVec2((float)simDensity_, (float)simDensity_);
}

void App::saveWindowScreenshot(const std::string& path) {
    typedef void (*ReadPixelsFn)(int, int, int, int, unsigned, unsigned, void*);
    ReadPixelsFn rp = (ReadPixelsFn)SDL_GL_GetProcAddress("glReadPixels");
    if (!rp || !window_) return;
    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(window_, &w, &h);
    std::vector<unsigned char> px((size_t)w * h * 4), flipped((size_t)w * h * 4);
    rp(0, 0, w, h, 0x1908 /*GL_RGBA*/, 0x1401 /*GL_UNSIGNED_BYTE*/, px.data());
    for (int y = 0; y < h; y++)
        std::memcpy(&flipped[(size_t)y * w * 4], &px[(size_t)(h - 1 - y) * w * 4], (size_t)w * 4);
    SDL_Surface* s = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_RGBA32, flipped.data(), w * 4);
    if (s) {
        SDL_SavePNG(s, path.c_str());
        SDL_DestroySurface(s);
    }
}

int App::runHeadless(const CliArgs& a) {
    audio.open(48043.6);
    applyAudioSettings();
    std::vector<std::string> list = a.content;
    // --no-content / --boot-bios: one BIOS-only boot (no cartridge, no disc), reported under the path "(no content)".
    const bool noContentRun = list.empty() && a.noContent;
    if (noContentRun) list.push_back("");
    if (list.empty()) {
        std::fprintf(stderr, "headless: no content given\n");
        return 2;
    }
    int rc = 0;
    for (const std::string& path : list) {
        const std::string label = noContentRun ? std::string("(no content)") : path;
        // A private, empty save directory per load.
        std::string saves = paths.root + "saves" + (paths.instance > 1 ? "-i" + std::to_string(paths.instance) + "-" : "") +
                            std::to_string(headlessLoads_++) + "/";
        SDL_CreateDirectory(saves.c_str());
        paths.saves = saves;

        if (noContentRun) {
            if (!bootBios(a.bootBios == "cart" ? bios::CartBoot : bios::Cd) || !host.isLoaded()) {
                std::fprintf(stderr, "headless: FAIL could not boot the BIOS\n");
                return 1;
            }
        } else {
            // Content arrives exactly the way a drag-and-drop does: through the event
            // queue, into the same handler the window uses.
            SDL_Event ev;
            SDL_zero(ev);
            ev.type = SDL_EVENT_DROP_FILE;
            ev.drop.data = path.c_str();
            SDL_PushEvent(&ev);
            SDL_Event got;
            while (SDL_PollEvent(&got)) handleEvent(got, true);
            if (!host.isLoaded() || host.contentPath() != absolutePath(path)) {
                std::fprintf(stderr, "headless: FAIL could not load %s\n", path.c_str());
                return 1;
            }
        }
        for (const std::string& want : a.expectBios) {
            const bios::Role role = want.compare(0, 4, "cart") == 0 ? bios::CartBoot : bios::Cd;
            const std::string src = want.substr(want.find(':') + 1);
            const CoreHost::StagedBios& sb = host.stagedBios(role);
            const char* got = !sb.seen ? "nothing" : sb.source == bios::SrcBuiltin ? "builtin" : sb.source == bios::SrcSystem ? "system" : "file";
            std::fprintf(stderr, "BIOS %s: %s%s\n", bios::roleLabel(role),
                         sb.seen ? bios::describeStaged(sb.source, sb.where, sb.crc).c_str() : "(not staged)",
                         sb.rejected ? "  [a named file was refused]" : "");
            if (src != got) {
                std::fprintf(stderr, "headless: FAIL expected the %s from '%s', the core ran '%s'\n", bios::roleLabel(role),
                             src.c_str(), got);
                return 1;
            }
        }

        if (badOverride_) {
            std::fprintf(stderr, "headless: FAIL an --option override was rejected by the core\n");
            return 2;
        }
        if (a.printControls) std::printf("%s", controlsOverlayText(*this).c_str());
        if (a.expectControls >= 0 && (input.controls.show ? 1 : 0) != a.expectControls) {
            std::fprintf(stderr, "headless: FAIL the controls overlay is %s after the load, expected %s\n",
                         input.controls.show ? "showing" : "hidden", a.expectControls ? "showing" : "hidden");
            return 1;
        }
        audioFramesSeen_ = 0;
        uint64_t hlT0 = SDL_GetPerformanceCounter(), hlRun = 0;
        double hlCpu0 = Perf::cpuSeconds();
        for (unsigned i = 0; i < a.frames; i++) {
            ach.headlessFrame(i);
            if (!neuroCliUrl.empty()) headlessNeuroFrame(hlT0, i);   // --neuro: real time, so she can keep up
            audio.update();
            uint64_t hr0 = SDL_GetPerformanceCounter();
            host.runFrame();
            hlRun += SDL_GetPerformanceCounter() - hr0;
            ach.afterFrame();
            ach.pump();
            if ((i & 15) == 15) audio.clear();   // the dummy device drains in real time
            for (const OsdMessage& m : host.takeMessages())
                std::fprintf(stderr, "[osd] %s\n", m.text.c_str());
        }
        {
            double wall = (double)(SDL_GetPerformanceCounter() - hlT0) / (double)perfFreq_;
            double run = (double)hlRun / (double)perfFreq_;
            double cpu = Perf::cpuSeconds() - hlCpu0;
            char cpuTxt[48] = "n/a";
            if (hlCpu0 >= 0 && cpu > 0) std::snprintf(cpuTxt, sizeof cpuTxt, "%.3f s (%.1f fps)", cpu, a.frames / cpu);
            std::fprintf(stderr, "TIMING %u frames: loop %.3f s (%.1f fps), retro_run %.3f s (%.1f fps), cpu %s %s\n",
                         a.frames, wall, a.frames / wall, run, a.frames / run, cpuTxt, label.c_str());
            int nr = netplayHeadlessCheck(*this, a);
            if (nr == 1) rc = 1;
            else if (nr == 77 && rc == 0) rc = 77;
        }
        if (!cheatsHeadlessCheck(*this, a)) rc = 1;
        if (!ach.headlessCheck(a)) rc = 1;
        const Frame& f = host.frame();
        HeadlessResult r;
        r.path = label;
        r.hash = frameHash();
        r.w = f.width;
        r.h = f.height;
        r.audioFrames = audioFramesSeen_;
        r.optVisible = host.visibleOptionCount();
        r.optTotal = (unsigned)host.optionDefs().size();
        r.uniform = true;
        for (size_t i = 1; i < f.pixels.size(); i++)
            if ((f.pixels[i] & 0xFFFFFF) != (f.pixels[0] & 0xFFFFFF)) { r.uniform = false; break; }
        results_.push_back(r);
        if (!a.dumpFrame.empty() && f.width && f.height) {
            SDL_Surface* sf = SDL_CreateSurfaceFrom((int)f.width, (int)f.height, SDL_PIXELFORMAT_XRGB8888,
                                                    (void*)f.pixels.data(), (int)f.width * 4);
            if (sf) {
                SDL_SavePNG(sf, (a.dumpFrame + "." + std::to_string(results_.size() - 1) + ".png").c_str());
                SDL_DestroySurface(sf);
            }
        }
        if (a.stateRoundtrip) {
            // Save-state path end to end: slot file, thumbnail PNG, restore.
            if (!saveState(0) || !fileExists(statePath(0)) || !fileExists(thumbPath(0))) {
                std::fprintf(stderr, "headless: FAIL state or thumbnail was not written\n");
                return 1;
            }
            for (int i = 0; i < 60; i++) { audio.update(); host.runFrame(); }
            uint32_t h1 = frameHash();
            if (!loadState(0)) {
                std::fprintf(stderr, "headless: FAIL could not load the state back\n");
                return 1;
            }
            for (int i = 0; i < 60; i++) { audio.update(); host.runFrame(); }
            uint32_t h2 = frameHash();
            std::printf("STATE roundtrip %08x %08x\n", h1, h2);
            if (!cheatsHeadlessCheck(*this, a)) rc = 1;   // the cheats must still hold after a restore
            if (h1 != h2) {
                std::fprintf(stderr, "headless: FAIL state restore diverged (%08x vs %08x)\n", h1, h2);
                rc = 1;
            }
        }
        std::printf("HASH %08x %ux%u audio_frames=%zu options=%u/%u %s\n", r.hash, r.w, r.h, r.audioFrames,
                    r.optVisible, r.optTotal, label.c_str());
        std::fflush(stdout);
        if (r.w == 0 || r.h == 0) {
            std::fprintf(stderr, "headless: FAIL %s produced no video frame\n", label.c_str());
            rc = 1;
        }
        if (r.uniform && !a.allowBlank) {
            std::fprintf(stderr, "headless: FAIL %s ended on a uniform (blank) frame\n", label.c_str());
            rc = 1;
        }
        if (r.audioFrames == 0) {
            std::fprintf(stderr, "headless: FAIL %s produced no audio\n", label.c_str());
            rc = 1;
        }
    }

    // The same content loaded twice in one process must end on the same frame:
    // that is the static-state reset (loading game A, then B, then A again).
    for (size_t i = 0; i < results_.size(); i++)
        for (size_t j = i + 1; j < results_.size(); j++)
            if (results_[i].path == results_[j].path &&
                (results_[i].optVisible != results_[j].optVisible || results_[i].optTotal != results_[j].optTotal)) {
                std::fprintf(stderr,
                             "headless: FAIL %s shows %u/%u options, then %u/%u after another game was loaded "
                             "in between -- option visibility leaked across a reload\n",
                             results_[i].path.c_str(), results_[i].optVisible, results_[i].optTotal,
                             results_[j].optVisible, results_[j].optTotal);
                rc = 1;
            }
    for (size_t i = 0; i < results_.size(); i++)
        for (size_t j = i + 1; j < results_.size(); j++)
            if (results_[i].path == results_[j].path && results_[i].hash != results_[j].hash) {
                std::fprintf(stderr,
                             "headless: FAIL %s hashed %08x then %08x after another game was loaded "
                             "in between -- core state leaked across a reload\n",
                             results_[i].path.c_str(), results_[i].hash, results_[j].hash);
                rc = 1;
            }

    if (!a.expectHash.empty() && !results_.empty()) {
        char got[16];
        std::snprintf(got, sizeof got, "%08x", results_[0].hash);
        std::string want = a.expectHash;
        std::transform(want.begin(), want.end(), want.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        if (want != got) {
            std::fprintf(stderr, "headless: FAIL frame hash %s, expected %s\n", got, want.c_str());
            rc = 1;
        }
    }
    if (rc == 0) std::printf("headless: OK (%zu load(s))\n", results_.size());
    return rc;
}

}  // namespace vj
