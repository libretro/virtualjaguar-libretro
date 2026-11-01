#include "platform/file_access.h"
#include "app.h"
#include "build_channel.h"
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
#include "tv_more_settings.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <thread>

// app/CMakeLists.txt's VJ_TV_DEFAULT defines this for the virtualjaguar target (the single source of the default).  The fallback
// is for a build that compiles app.cpp outside CMake only; keep it equal to the option's default.
#ifndef VJ_TV_DEFAULT_SETTING
#define VJ_TV_DEFAULT_SETTING "auto"
#endif

namespace vj {

std::atomic<bool> g_signalQuit{false};
static_assert(std::atomic<bool>::is_always_lock_free, "g_signalQuit is stored from a signal handler");

namespace {

// SIGINT / SIGTERM in a headless run (installed by App::initSdl): an atomic store, then back to the default action, so a
// second signal kills a run that is wedged inside retro_run().  Both calls are async-signal-safe.
void onSignalQuit(int sig) {
    g_signalQuit.store(true);
    std::signal(sig, SIG_DFL);
}

bool isInputEvent(Uint32 type) {
    switch (type) {
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
    case SDL_EVENT_MOUSE_MOTION:
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP:
    case SDL_EVENT_GAMEPAD_AXIS_MOTION: return true;
    default: return false;
    }
}

// --perf-input: a synthetic key (F24: bound to nothing), pushed from an SDL timer thread so the event lands at a swept
// point of the display period.  Pushed from the loop it would always arrive just after a swap, where the single-thread
// loop polls at once and the threaded loop waits longest, biasing the comparison (#967).
Uint64 SDLCALL perfInputKey(void* window, SDL_TimerID, Uint64) {
    SDL_Event ev;
    SDL_zero(ev);
    ev.type = SDL_EVENT_KEY_DOWN;
    ev.key.windowID = (SDL_WindowID)(uintptr_t)window;
    ev.key.scancode = SDL_SCANCODE_F24;
    ev.key.down = true;
    ev.key.timestamp = SDL_GetTicksNS();
    SDL_PushEvent(&ev);
    ev.type = SDL_EVENT_KEY_UP;
    ev.key.down = false;
    SDL_PushEvent(&ev);
    return 0;   // once
}

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

void SDLCALL discDialogDone(void*, const char* const* list, int /*filter*/) {
    if (!list || !list[0]) return;
    SDL_Event ev;
    SDL_zero(ev);
    ev.type = SDL_EVENT_USER;
    ev.user.code = 0x564A4441;  // 'VJDA': add this disc image to the disc list
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

// EmuThread's view of the app.  Every method runs on the emulation thread (#967).
struct App::EmuClient : EmuThread::Client {
    App& app;
    explicit EmuClient(App& a) : app(a) {}
    void threadStarted() override { app.host.setOwner(std::this_thread::get_id()); }
    void threadStopping() override { app.host.setOwner(std::thread::id()); }
    bool canRun() override { return app.host.isLoaded(); }
    double framePeriod() override { return 1.0 / std::max(1.0, app.host.fps()); }
    double audioQueuedMs() override { return app.emuAudioQueuedMs(); }
    void runFrame() override { app.emuRunOneFrame(); }
    void framePaced(double lateSec, const PacingClock& clock) override {
        app.frameTick_ = clock.ticking() ? clock.startedTick() : 0;
        app.audioModeB_ = clock.ticking();
        app.tracePaced(lateSec, clock);
    }
    bool displayGrid(double now, DisplayGrid& g) override {
        App::ModeBGrid m;
        {
            std::lock_guard<std::mutex> l(app.gridMu_);
            m = app.grid_;
        }
        const uint64_t sdlNow = SDL_GetTicksNS();   // the UI's clock -> this thread's
        // (signed: the tick's vblank is the grid point nearest the swap's return, which can be a little after now)
        if (!m.on || !m.periodNs || (int64_t)(sdlNow - m.tickNs) > (int64_t)App::kGridStaleNs) return false;
        g.tick = m.tick;
        g.tickAt = now - (double)(int64_t)(sdlNow - m.tickNs) / 1e9;
        g.period = (double)m.periodNs / 1e9;
        g.k = m.k;
        app.frameBase_ = m.baseRatio;
        return true;
    }
    bool takeGrid(double now, double& at, double& period) override {
        const uint64_t p = app.takePeriodNs_.load(std::memory_order_acquire);
        const uint64_t t = app.takeNs_.load(std::memory_order_relaxed);
        if (!p || !t) return false;
        const uint64_t sdlNow = SDL_GetTicksNS();   // the UI's clock -> this thread's
        at = now - (double)(int64_t)(sdlNow - t) / 1e9;
        period = (double)p / 1e9;
        return true;
    }
    void afterCommands() override { app.host.publishSnapshot(); }
    void commandFailed(const std::string& what) override {
        app.host.pushMessage("Emulation command failed: " + what, 5000, RETRO_LOG_ERROR);
    }
};

App::App() : host(CoreHost::instance()) {
    tvHost_ = std::make_unique<tv::AppHost>(*this);   // headless runs have them too: opening the menu needs no ImGui
    tvShell_ = std::make_unique<tv::Shell>(*tvHost_);
}
App::~App() { shutdown(); }

double App::now() const { return (double)SDL_GetPerformanceCounter() / (double)perfFreq_; }

#ifdef VJ_HAVE_NEURO
// Headless with --neuro: the client runs as in the UI loop, and frame `i` waits for its time at 60 fps.
void App::headlessNeuroFrame(uint64_t t0, unsigned i) {
    neuroTick(*this);
    syncDebug();   // the 10 Hz refresh serial the watches' copier waits for, as the windowed loop moves it
    const double due = (double)t0 / (double)perfFreq_ + i / 60.0;
    const double wait = due - now();
    if (wait > 0) SDL_Delay((Uint32)(wait * 1000.0));
}
#endif

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
    files::restore(cfg);
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
    pacingDisplay = cfg.get("video.pacing", "jaguar") == "display";
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
    fullscreenAutoHideMenu = cfg.getBool("ui.fullscreen_autohide_menu", false);
    resumeMode = lib::clampResumeMode(cfg.getInt("saves.resume", lib::kResumeDefault));
    input.stickAsDpad = cfg.getBool("input.stick_dpad", true);
    emuThreadPref = cfg.has("ui.emu_thread") ? (cfg.getBool("ui.emu_thread", false) ? 1 : 0) : -1;
    // No stored ui.tv_mode: the build's default (Auto unless the package says otherwise).  An unknown value does the same.
    tvSetting = tv::defaultSetting(VJ_TV_DEFAULT_SETTING);
    if (cfg.has("ui.tv_mode")) tv::settingFromKey(cfg.get("ui.tv_mode"), tvSetting);
    configureTvMode();
    if (tvSetting == tv::TvSetting::Manual && cfg.getBool("ui.tv_manual_on", false)) tvMode.setOn(true);
}

void App::configureTvMode() {
    tv::TvConfig c;
    c.setting = tvSetting;
    c.autoFullscreen = cfg.getBool("ui.tv_auto_fullscreen", true);
    c.autoController = cfg.getBool("ui.tv_auto_controller", true);
    tvMode.configure(c, fullscreen);
    tvNoteFsSeen_ = fullscreen;   // what TvMode now knows: tvFullscreenEdge() feeds it changes only
}

void App::setTvSetting(tv::TvSetting s) {
    const bool wasOn = tvMode.active();
    if (s != tvSetting) cfg.set("ui.tv_mode", tv::settingKey(s));   // on change only: a default that was never chosen stays free to move
    tvSetting = s;
    if (s == tv::TvSetting::Off) cfg.setBool("ui.tv_manual_on", false);   // or a later Manual would switch TV mode on at once
    configureTvMode();                    // starts from off ...
    if (wasOn) tvMode.setOn(true);        // ... so a change of setting stays in TV mode; Off stays off (setOn is a no-op then)
}

void App::setTvAuto(bool fullscreenTrigger, bool on) {
    const bool wasOn = tvMode.active();
    cfg.setBool(fullscreenTrigger ? "ui.tv_auto_fullscreen" : "ui.tv_auto_controller", on);
    configureTvMode();
    if (wasOn) tvMode.setOn(true);
}

// The Auto controller trigger (spec 8, approvals A and B).  Only a button with a menu intent counts: Guide, Share, the
// touchpad and the paddles carry app actions, and a stick or trigger drifts.  While TV mode is on the press is the TV UI's
// (and a fresh mouse budget for a controller-started TV mode); while it is off and the desktop library owns the pad with no
// game loaded, it turns TV mode on and is consumed, so it neither moves the desktop focus nor acts on the TV screen it
// reveals.  TvMode itself checks Auto and the "when a controller is used" box.
bool App::tvAutoPadPress(const SDL_Event& e) {
    if (input.navProfile(e.gbutton.which).intentForButton(e.gbutton.button) == tv::Intent::None) return false;
    if (tvMode.active()) {
        tvMode.onPadPress(true);
        return false;
    }
    const bool typing = imguiUp_ && ImGui::GetCurrentContext() && ImGui::GetIO().WantTextInput;
    if (deskPadActive(*this) && !host.isLoaded() && !typing) tvMode.onPadPress(true);   // a paused game under the library window stays the desktop's
    return tvMode.active();
}

// TvMode re-applies a repeated onFullscreen(true) (it turns TV mode back on after a manual off), and macOS sends enter events
// for space and display changes: it is told of changes only.  setFullscreen() and the two SDL events all land here.
void App::tvFullscreenEdge() {
    if (fullscreen == tvNoteFsSeen_) return;
    tvNoteFsSeen_ = fullscreen;
    tvMode.onFullscreen(fullscreen);
}

void App::saveConfig() {
    cfg.setBool("video.integer", present.integerScale);
    cfg.setBool("video.linear", present.linearFilter);
    cfg.setBool("video.stretch", present.stretch);
    cfg.setFloat("video.aspect", present.aspectOverride);
    cfg.setBool("video.vsync", vsync);
    if (pacingFromCli_ < 0 || pacingDisplay != (pacingFromCli_ == 1))   // (--pacing is for this run only)
        cfg.set("video.pacing", pacingDisplay ? "display" : "jaguar");
    video.shader.saveSettings(cfg);
    cfg.setFloat("audio.volume", volume);
    cfg.setBool("audio.mute", mute);
    cfg.setFloat("audio.target_ms", audioTargetMs);
    cfg.setBool("ui.show_fps", showFps);
    fonts::store(cfg, fontSettings);
    if (!themeFromCli_) cfg.set("ui.theme", themePreviewing_ ? themeBeforePreview_ : theme::choiceKey());
    cfg.setBool("ui.home_library", homeLibrary);
    // ui.tv_mode is never written here: a stored value would pin a default that is meant to move (Auto on nightlies, #991
    // Task 16) for everyone who ran this build.  Only setTvSetting() writes it, on change.
    if (tvSetting == tv::TvSetting::Manual && !tvFromCli_) cfg.setBool("ui.tv_manual_on", tvMode.active());
    cfg.setBool("saves.autosave", autosaveSram);
    cfg.setBool("ui.tray", trayIcon);
    cfg.setBool("ui.keep_awake", keepAwake);
    cfg.setBool("ui.viewports", windowsOutside);
    cfg.setBool("ui.menubar_in_window", showInWindowMenu);
    cfg.setBool("ui.fullscreen_autohide_menu", fullscreenAutoHideMenu);
    if (emuThreadPref >= 0) cfg.setBool("ui.emu_thread", emuThreadPref == 1);   // unwritten until chosen: the channel default applies (build_channel.h)
    else cfg.erase("ui.emu_thread");
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
    toastCount_++;
    Osd o;
    o.text = text;
    o.born = now();
    o.life = seconds;
    o.level = level;
    osd_.push_back(std::move(o));
    if (osd_.size() > 6) osd_.erase(osd_.begin());
    if (headless_) std::fprintf(stderr, "[osd] %s\n", text.c_str());
}

bool App::emuThreadWanted() const { return emuThreadEffective(emuThreadPref, kEmuThreadDefault); }   // never chosen: the build channel (#967 3d)

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
    platform::initNetworking();   // Windows: Winsock for the core's sockets (the GDB stub), headless runs included
    // Headless: a signal is a quit request.  Installed AFTER SDL_Init, so these replace whatever SDL did with the signals
    // (SDL turns them into SDL_EVENT_QUIT, which nothing pumps in a headless run).  Windowed runs keep SDL's own.
    if (headless_) {
        std::signal(SIGINT, onSignalQuit);
        std::signal(SIGTERM, onSignalQuit);
    }
    if (!headless_) platform::beforeWindow();   // process identity (Windows AppUserModelID), UI thread QoS (macOS)
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
    swapGuard_.contextCreated(platform::displayAsleep(window_));
    if (swapGuard_.deadLink())
        std::fprintf(stderr, "[gl] the display is asleep: vsync is off for this run (a swap would wait for a refresh that never comes)\n");
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
    // The backend submits no pad state: the library's popups get ImGui gamepad nav fed with our intents (deskPadBeforeNewFrame).
    ImGui_ImplSDL3_SetGamepadMode(ImGui_ImplSDL3_GamepadMode_Manual, nullptr, 0);
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
    beginQuit();   // a hosted halt ends within one pump wait; the auto-save and the unload below need the core out of it
    // Quitting: keep where the game was -- but never wait on a core that is still halted after beginQuit's bounded wait, and
    // never past 1.5 s (the save's give-up polls the clock): the unload below and the join are the backstop.
    if (host.isLoaded() && !debug.core().halt().halted) {
        const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(1500);
        autoSave([this, end] { return debug.core().halt().halted || std::chrono::steady_clock::now() >= end; });
    }
    discordShutdown(*this);            // take the Discord status down (bounded: never waits long on Discord)
#ifdef VJ_HAVE_NEURO
    neuroShutdown(*this);
#endif
    // Unload (it flushes SRAM) where the core lives, then stop the thread.  A quit ends a halt within one pump wait
    // (beginQuit); a core wedged elsewhere still must not hang the quit: give up after 3 s and exit.
    if (host.isLoaded() && !emu.callFor(std::chrono::milliseconds(3000), [this] { host.unloadContent(); })) {
        std::fprintf(stderr, "[emu] the emulation thread did not answer within 3 s (the core is blocked); exiting without unloading\n");
        std::fflush(stderr);
        std::_Exit(3);   // nonzero: a wedged quit must be visible
    }
    stopEmuThread();
    latchWaker_.stop();   // (both normally stopped by runInteractive; idempotent) nothing may push events past SDL_Quit
    stall_.stop();
    // Only now: until the emulation thread is gone it may be inside rc_client_do_frame or a posted emuIdle.  Still before
    // video.shutdown() -- images.stop() needs the GL context.
    ach.shutdown();
    cheatsShutdown(*this);   // the community store's worker, before `cheats` is destroyed
    invalidateThumbs();
    shutdownDebugTools();    // the Textures / Object list windows hold GL textures
    video.shutdown();
    if (imguiUp_) {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        imguiUp_ = false;
    }
    files::shutdown();
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
        // Threaded: the copies syncEmuFlags() hands over.  Inline: the real flags (the inline TV-script path never syncs).
        const bool ff = host.threaded() ? emuFastForward_.load(std::memory_order_relaxed) : fastForward;
        const bool mu = host.threaded() ? emuMute_.load(std::memory_order_relaxed) : mute;
        if (!ff && !mu) audio.push(d, n);
    };
    host.inputPoll = [this]() {
        if (host.threaded()) {   // the emulation thread: the newest input the UI thread published (input_snapshot.h)
            uint64_t seq = 0;
            const uint64_t ns = inputSnap.take(emuInput_, &seq);
            emuInputSeq_ = seq;   // stamped on the next frame this thread publishes
            if (ns) noteInputLatency(ns);
#ifdef VJ_HAVE_NEURO
            {   // Neuro's buttons for this emulated frame, replayed from the program the UI thread published
                const ControlSet a = neuro::pollAgent(inputSnap, agentPlayer_);
                if (a.any()) emuInput_.addControls(0, a);
            }
#endif
            return;
        }
        if (pendingEventNs_) {   // VJ_PERF_CSV: from the oldest unhandled input event to this poll
            noteInputLatency(pendingEventNs_);
            if (!presentEventNs_) presentEventNs_ = pendingEventNs_;   // and to this iteration's swap
            pendingEventNs_ = 0;
        }
        input.poll(!inputCaptured_);
#ifdef VJ_HAVE_NEURO
        input.addAgentControls(neuro::pollAgent(inputSnap, agentPlayer_));   // Neuro's buttons for this emulated frame
#endif
    };
    input.notify = [this](const std::string& m) { toast(m, 3.0); };
    // The app's own Controls / Devices tabs replace these core options.
    // RetroAchievements hardcore also locks the GDB stub (it reads memory and halts the game): gdbLockedByHardcore_ is
    // current before every load (syncHardcoreLock), which is when CoreHost settles which keys are locked.
    host.optionLock = [this](const std::string& k, std::string& v) {
        if (k == "virtualjaguar_gdb_stub" && gdbLockedByHardcore_.load()) {
            v = "disabled";
            return true;
        }
        return lockedCoreOptionFor(k, v, {{proPorts[0].load(), proPorts[1].load()}});
    };
    host.inputState = [this](unsigned p, unsigned d, unsigned i, unsigned id) {
        return host.threaded() ? emuInput_.state(p, d, i, id) : input.state(p, d, i, id);
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
    // The core hands its interface over (and takes it back) inside a load / unload, which is the emulation thread's
    // call when threaded: that side only swaps the pointer and the views' epoch.  The UI half (breakpoints, regions,
    // closing the windows) is syncCore(), run by syncDebug() each UI frame; inline, there is no other thread, so now.
    host.onDebugIface = [this](const vj_debug_iface* i) {
        debug.onCoreIface(i);
        if (!host.threaded()) debug.syncCore();
    };
    // The windows' one-shot actions reach the core through the emulation thread's debug lane (before EmuThread::start
    // the thread is inline, so both run at once: headless and inline runs are unchanged).
    debug.coreCall = [this](std::function<void()> f) { emu.debugCall(std::move(f)); };
    debug.corePost = [this](std::function<void()> f) { emu.debugPost(std::move(f)); };
    // The halt lives in DebugCore (phase 3b).  Inline the core is parked in retro_run() on the UI thread, so the pump runs
    // one nested UI frame per iteration; the UI thread's mirror of the halt (DebugSession::syncHalt, from syncDebug)
    // fires the UI-side hooks below.  abortHalt reads quitting_ (and the headless signal flag): closing the window while halted lets the core go.
    debug.core().runUiFrame = [this] { haltedFrame(); };   // the core is parked in retro_run(): UI only
    debug.core().abortHalt = [this] { return quitting_.load() || g_signalQuit.load(); };
    debug.core().notifyVerdict = [this] { emu.debugPost([] {}); };   // wakes the pump's lane wait (a no-op inline, where it runs now)
    debug.registerPlaceholderTools();                     // fixes the Tools menu order; each provider below replaces its entries
    debug.core().onHalt.push_back([this](int target, unsigned pc, int reason) {   // the emulation thread (inline: the UI thread)
        audio.setPaused(true);                            // no stale buffer droning while the game is stopped
        if (reason != VJ_DBG_STOP_STEP) std::fprintf(stderr, "[debug] halt: %s at $%06X (%s)\n", dbgTargetName(target), pc, dbgReasonName(reason));
    });
    debug.core().onResume.push_back([this] {
        audio.clear();
        audio.setPaused(emu.paused() && !headless_);   // (headless: emu is paused for advance()-only frames; no audio to hold)
    });
    debug.onHaltUi.push_back([this](int target, unsigned pc, int reason) {   // the UI thread, from syncHalt()
        if (reason == VJ_DBG_STOP_STEP) return;
        char msg[96];
        std::snprintf(msg, sizeof msg, "Halted: %s at $%06X - F9 continues, F10 steps", dbgTargetName(target), pc);
        toast(msg, 4.0);
    });
    debug.onResumeUi.push_back([this] {
        pacer_.reset();
        lastTick_ = now();                                // the halt must not count as elapsed emulation time
    });

    // Every tool window plugs in through DebugSession::addProvider (addDebugTools, debug/debug_ui.cpp).  After
    // host.onAudio is set above (the audio scope chains onto it) and paths.instance is settled (Textures).
    addDebugTools(*this);
}

void App::syncDebug() {
    debug.syncCore();   // the UI half of a core interface swap the emulation thread made (threaded)
    if (!threaded() && !host.inFrame()) debug.core().frameEnded();   // inline: a halt the core left on gdb's say-so
    debug.syncHalt();   // the mirror of the core's halt: the UI-side hooks fire here
    debug.setContentLoaded(host.isLoaded());
    syncHardcoreLock();
    debug.setHardcore(ach.hardcoreActive());
    debug.tick(now(), host.isLoaded() && !paused && !debug.halted());
    debug.wakeViews();   // posts one fill to the debug lane: EmuThread runs it now when not threaded (a halted or paused window is current), else at the next boundary
}

// Emulation thread (inline: the UI thread), after each emulated frame: serves every open window's request from the
// core.  haltedFrame() has no call of its own: it runs syncDebug(), and wakeViews() posts a fill that EmuThread runs inline there.
void App::fillDebugViews() { debug.fillViews(); }

// Emulation thread, the halt pump's wait (DebugCore::waitVerdict): the core is parked in GDBHalt() inside retro_run().
// Nothing here may call a host.* entry that inFrame() refuses, nor touch UI state.
void App::haltWait() {
    emu.serveDebugLane(DebugCore::kPumpWait);
    if (debug.views().pending(debug.refreshSerial())) fillDebugViews();
}

// UI thread, idempotent.  A hosted halt is resumed (CONTINUE, host kept attached) by the pump's next iteration (abortHalt reads quitting_); the poll is
// a <= 200 ms bound on the quit path only, never in the UI loop.
void App::beginQuit() {
    quitting_.store(true);
    if (threaded()) {
        emu.debugPost([] {});
        for (int i = 0; i < 100 && debug.core().halt().halted; i++) SDL_Delay(2);
    }
    debug.syncHalt();
}

// Threaded and halted: release the halt (a lane post: CONTINUE for the halt it finds, the list disarmed so the frame can
// end, host and list kept) and return the give-up for the call that follows -- true only if the core halts AGAIN after
// that release.  `s0` is the mirrored serial: the release has run once releaseSerial() >= s0 (a release still queued is
// waited for, so a Detach made while a step is in flight is not given up on), and a halt newer than the released one
// is the give-up.  Inline, or not halted: the plain halt predicate.
std::function<bool()> App::releaseForCall() {
    if (!threaded() || !debug.halted()) return haltGiveUp();
    const uint64_t s0 = debug.haltSerial();
    debug.releaseHalt();
    return [this, s0] {
        const uint64_t rel = debug.core().releaseSerial();
        const HaltState h = debug.core().halt();
        return rel >= s0 && h.halted && h.serial > rel;
    };
}

// UI thread.  The lock turns on with hardcore being wanted (signed in or signing in), a step ahead of hardcoreActive().
// CoreHost settles which keys are locked at the definitions push, i.e. at a load, so this must be current before every
// load.  A lock that turns OFF mid-game leaves the stub disabled until the next load; one that turns ON needs a reload
// (Achievements::pump).
void App::syncHardcoreLock() { gdbLockedByHardcore_.store(ach.hardcoreNextGame()); }

// Emulation thread (inline: the UI thread, between frames).  The silent debugger host: it never halts by itself, it only
// makes a gdb halt visible to the app.  Attached inside the load call -- before any frame can run, so a gdb_wait boot halt
// is seen -- and again after any frame it is missing (a user Detach drops it; the app's breakpoints go with it).
void App::attachGdbHostOnCore() {
    // Not hardcore-gated on purpose: the host never halts anything by itself, and the app's own attach() / breakNow() stay
    // gated by DebugSession::enabled().
    if (!host.isLoaded() || !debug.core().loaded() || !gdbStubOn_.load() || debug.core().attached()) return;
    if (debug.core().attach()) std::fprintf(stderr, "[debug] gdb host attached (silent): halts are visible to the app\n");
}

// UI thread: 1e9 / the refresh rate of the display the window is on, or 0 when there is no window or the rate is unknown.
uint64_t App::displayPeriodNs() {
    if (!displayPeriodValid_) {
        displayPeriodNs_ = 0;
        nextPresentNs_ = 0;   // a new refresh rate starts a new deadline grid
        if (window_) {
            const SDL_DisplayMode* m = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(window_));
            if (m && m->refresh_rate > 1.0f) displayPeriodNs_ = (uint64_t)(1e9 / (double)m->refresh_rate);
        }
        displayPeriodValid_ = true;
    }
    return displayPeriodNs_;
}

void App::noteInputLatency(uint64_t eventNs) {
    const uint64_t t = SDL_GetTicksNS();
    // An event older than 250 ms waited out a pause or a load, not the pacing: it would only pollute the average.
    if (!eventNs || t <= eventNs || t - eventNs > 250000000ull) return;
    latSumNs_.fetch_add(t - eventNs, std::memory_order_relaxed);
    latCount_.fetch_add(1, std::memory_order_relaxed);
}

// Once per UI iteration (VJ_PERF_CSV): the audio underruns and the input latency measured since the last one.
void App::perfSample() {
    if (!perf.on) return;
    const uint64_t u = audio.underruns();
    perf.curUnder = (unsigned)(u - underrunsSeen_);
    underrunsSeen_ = u;
    const uint64_t n = latCount_.exchange(0, std::memory_order_relaxed);
    const uint64_t s = latSumNs_.exchange(0, std::memory_order_relaxed);
    perf.curLat = n ? (float)((double)s / (double)n / 1e6) : -1.0f;
    perf.curPres = presCount_ ? (float)((double)presSumNs_ / (double)presCount_ / 1e6) : -1.0f;
    presSumNs_ = presCount_ = 0;
}

// ---- the emulation thread (#967) ------------------------------------------------------------------------
void App::startEmuThread() {
    if (!emuClient_) emuClient_.reset(new EmuClient(*this));
    host.post = [this](std::function<void()> fn) { emu.post(std::move(fn)); };
    emu.setWaitHook([this](double waited, unsigned tick) { busyFrame(waited, tick); });
    testSlowCallMs_ = 0;
    if (const char* slow = SDL_getenv("VJ_TEST_SLOW_CALL_MS")) {
        const long ms = std::strtol(slow, nullptr, 10);
        testSlowCallMs_ = ms < 0 ? 0u : ms > 5000 ? 5000u : (unsigned)ms;
    }
    // Before the thread exists: a std::function assigned while the thread runs is a data race with the pump reading it.
    debug.core().waitVerdict = [this] { haltWait(); };   // the halt pump's wait (emulation thread)
    // VJ_TEST_EMU_START_FAIL: take the failure branch below as a real emu.start() failure does (emu_thread_start_fallback).
    if (SDL_getenv("VJ_TEST_EMU_START_FAIL") || !emu.start(EmuThread::Mode::Threaded, *emuClient_)) {
        host.post = nullptr;
        emuThreadThisRun_ = false;   // Settings offers "Restart the app to apply" again
        debug.core().waitVerdict = nullptr;
        std::fprintf(stderr, "[emu] could not start the emulation thread; running emulation on the UI thread\n");
        return;
    }
    syncEmuFlags();
    std::fprintf(stderr, "[emu] emulation thread on\n");
}

// The emulation thread is busy with a call() the UI waits on (#967): keep the window alive and say so.  Called by
// EmuThread::call() on this thread -- possibly in the middle of an ImGui frame -- so: no ImGui, no call(), no event
// dispatch (SDL_PumpEvents only queues them), no read of a field only a call() writes (CoreHost counts those).
void App::busyFrame(double waited, unsigned tick) {
    if (tick == 0) std::fprintf(stderr, "[emu] busy: a call has taken over 250 ms\n");
    if (!window_ || !imguiUp_) return;
    struct UiWaiting {   // cleared again on any exit
        CoreHost& h;
        explicit UiWaiting(CoreHost& host) : h(host) { h.setUiWaiting(true); }
        ~UiWaiting() { h.setUiWaiting(false); }
    } waiting(host);
    SDL_PumpEvents();
    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(window_, &w, &h);
    if (w > 0 && h > 0) {
        video.drawBusy(w, h, waited);
        swapWindow();
    }
}

void App::stopEmuThread() {
    if (!emu.threaded()) return;
    if (!emu.stop(std::chrono::milliseconds(3000))) {
        std::fprintf(stderr, "[emu] the emulation thread did not stop within 3 s (the core is blocked); exiting without unloading\n");
        std::fflush(stderr);
        std::_Exit(3);   // (nonzero: a wedged quit must be visible) the core still belongs to that thread: nothing here may touch it, nor run destructors over it
    }
    host.post = nullptr;
}

// One emulated frame on the emulation thread.  Inline mode keeps stepEmulation().
void App::emuRunOneFrame() {
    // The lanes were drained before this call: a Detach posted by the UI has run, so the silent host is missing now.  Put it
    // back BEFORE the frame, or a gdb breakpoint firing in this frame would halt with no pump and no mirror.
    attachGdbHostOnCore();
    const uint64_t tick = frameTick_;   // (advance() frames are not paced: no tick, and the controller in charge stays)
    frameTick_ = 0;
    if (!emuFastForward_.load(std::memory_order_relaxed)) {
        // One audio controller at a time, switched at this frame boundary.  Mode A: underruns only (the pacing follows
        // the audio clock).  Mode B: the stream plays at the display's rate (base) and AudioOut trims around it.
        const double base = audioModeB_ ? frameBase_ : 1.0;
        if (base != audio.baseRatio()) audio.setBaseRatio(base);
        audio.update(audioModeB_);
    }
    host.wantVideo = true;
    PerfTrace::Emu te = tracePaced_;
    if (trace_.enabled()) {
        te.startNs = SDL_GetTicksNS();
        te.dueNs = tracePacedSet_ ? te.startNs - te.dueNs : te.startNs;   // tracePaced() left the lateness there
        tracePacedSet_ = false;
    }
    const uint64_t r0 = SDL_GetPerformanceCounter();
    host.runFrame();
    debug.core().frameEnded();   // a halt gdb (or the halt timeout) ended inside this frame is closed on our side
    attachGdbHostOnCore();       // the frame itself (or a lane command run after the drain) dropped it: back before the next one
    retroTicks_.fetch_add(SDL_GetPerformanceCounter() - r0, std::memory_order_relaxed);
    runFrames_.fetch_add(1, std::memory_order_relaxed);
    ach.emuFrame();   // rc_client_do_frame: reads game RAM, so here
    if (audio.queuedMs() > 1000.0f) audio.clear();   // a device that stopped pulling: keep the queue (and memory) bounded
    const Frame& f = host.frame();
    if (f.serial != lastSentSerial_ && f.width && f.height) {
        FrameExchange::Slot& s = frames.writeBuffer();
        s.frame.width = f.width;
        s.frame.height = f.height;
        s.frame.serial = f.serial;
        s.frame.pixels.assign(f.pixels.begin(), f.pixels.end());
        s.aspect = host.aspect();
        s.inputSeq = emuInputSeq_;
        s.tick = tick;
        frames.publish();
        lastSentSerial_ = f.serial;
    }
    if (tick) {   // mode B: the UI's latch wait ends now (also when the core made no new picture: nothing to wait for)
        publishedTick_.store(tick, std::memory_order_release);
        SDL_Event ev;
        SDL_zero(ev);
        ev.type = SDL_EVENT_USER;
        ev.user.code = kFrameReadyCode;
        SDL_PushEvent(&ev);
    }
    if (trace_.enabled()) {
        te.endNs = SDL_GetTicksNS();
        te.serial = f.serial;
    }
    host.publishSnapshot();
    // After the snapshot: the windows' buffers carry this frame's state.  endNs above is taken before the copy (the
    // frame cost other measurements compare must not move), so the copy is timed on its own: the row's fill_ns.
    const uint64_t fill0 = trace_.enabled() ? SDL_GetTicksNS() : 0;
    fillDebugViews();
    if (trace_.enabled()) {
        te.fillNs = SDL_GetTicksNS() - fill0;
        trace_.emu(te);
    }
}

// Emulation thread (VJ_PERF_TRACE): the pacing of the frame emuRunOneFrame() runs next.
void App::tracePaced(double lateSec, const PacingClock& clock) {
    if (!trace_.enabled()) return;
    tracePaced_.dueNs = lateSec > 0 ? (uint64_t)(lateSec * 1e9) : 0;   // a lateness until the frame starts
    tracePaced_.trim = (float)clock.trim();
    tracePaced_.filtQMs = (float)clock.filteredQueueMs();
    tracePaced_.queuedMs = (float)emuAudioQueuedMs();
    tracePaced_.audio = clock.audioLocked() ? 1 : 0;
    tracePaced_.refill = clock.refilling() ? 1 : 0;
    tracePaced_.mode = clock.ticking() ? 1 : 0;
    tracePaced_.tickSeq = clock.ticking() ? clock.startedTick() : 0;
    tracePaced_.ticksDropped = clock.droppedTicks();
    tracePaced_.lockReason = (unsigned char)lockReasonCode_.load(std::memory_order_relaxed);
    tracePacedSet_ = true;
}

// UI thread, threaded: this iteration takes its frame now.  While the swaps sit on a tracked vblank grid (vsync on, the
// latch's phase locked, whether or not it waits), the takes repeat every refresh, and the emulation thread keeps its
// frame ends off them (PacingClock::steer).
void App::publishTakeGrid(uint64_t takeNs) {
    const uint64_t p = (paceSteerOn_ && wantSync() && latch_.locked()) ? latch_.periodNs() : 0;
    takeNs_.store(takeNs, std::memory_order_relaxed);
    takePeriodNs_.store(p, std::memory_order_release);
}

// UI thread, threaded, right after every swap (#1038): the display lock's verdict, and in mode B the tick this swap
// returned on.  The tick counts vblanks, not swaps: an iteration that missed one moves it by two, and the emulation
// thread runs the frame it owes.  As the single-thread loop does after its swap, the input that came in while the UI
// built and swapped is handed over first, so the tick's frame polls it.
void App::displayLockAfterSwap(uint64_t swap0, uint64_t swap1) {
    DisplayLockInput in;
    const uint64_t mp = latch_.measuredPeriodNs();
    in.matchDisplay = pacingDisplay;
    in.vsync = wantSync() && swap1 - swap0 >= LateLatch::kBlockedNs;   // a swap that did not block is no vblank
    in.latchLocked = latch_.locked() && mp;   // (not measured yet: not a clock to follow yet)
    in.displayPeriod = (double)mp / 1e9;
    in.corePeriod = host.isLoaded() && host.fps() > 1.0 ? 1.0 / host.fps() : 0;
    in.fastForward = fastForward;
    in.linked = netplay.link() != LinkState::Off;
    lockVerdict_ = displayLock_.evaluate(in);
    lockReasonCode_.store((int)lockVerdict_.reason, std::memory_order_relaxed);
    if (lockVerdict_.reason != loggedReason_) {   // (the transitions, for the log: Settings shows the live state)
        loggedReason_ = lockVerdict_.reason;
        std::fprintf(stderr, "[emu] frame pacing: %s (%s), display %.3f Hz, k=%u, base ratio %.5f\n",
                     lockVerdict_.mode == PacingMode::B ? "mode B" : "mode A", DisplayLock::name(lockVerdict_.reason),
                     mp ? 1e9 / (double)mp : 0.0, lockVerdict_.k, lockVerdict_.baseRatio);
    }
    displayLockNotice();
    ModeBGrid g;
    if (lockVerdict_.mode == PacingMode::B) {
        const uint64_t v = latch_.nearestVblankNs(swap1);
        tickSeq_ += (tickNs_ && v > tickNs_) ? std::max<uint64_t>(1, (v - tickNs_ + mp / 2) / mp) : 1;
        tickNs_ = v;
        if (std::fabs(lockVerdict_.baseRatio - gridBase_) > kBaseStep || !grid_.on) gridBase_ = lockVerdict_.baseRatio;
        SDL_Event e;
        while (!quit && SDL_PollEvent(&e))
            if (!LatchWaker::isWake(e) && !isFrameReady(e)) handleEvent(e, false);
        publishInput();
        g.on = true;
        g.tick = tickSeq_;
        g.tickNs = v;
        g.periodNs = mp;
        g.k = lockVerdict_.k;
        g.baseRatio = gridBase_;
    } else {
        tickNs_ = 0;
        gridBase_ = 1.0;
    }
    {
        std::lock_guard<std::mutex> l(gridMu_);
        grid_ = g;
    }
    if (g.on) emu.notifyTick();
}

// The display's grid changed, or may have (another display, a new mode, fullscreen): mode B settles and measures again.
void App::displayChanged() {
    displayLock_.reset();
    latch_.resetMeasure();
    if (window_) latchUpdatePeriod();   // (inline it only sets a period nothing reads)
}

const char* App::lockReasonText(LockReason r) {
    switch (r) {
        case LockReason::Locked: return "following the display";
        case LockReason::Setting: return "Jaguar rate";
        case LockReason::NoVsync: return "vsync is off";
        case LockReason::Unlocked: return "the display's refresh is not steady";
        case LockReason::Ratio: return "not a multiple";
        case LockReason::Settling: return "measuring the display";
        case LockReason::FastForward: return "fast-forward";
        case LockReason::Link: return "link play";
    }
    return "";
}

// One message per change of a fallback that lasted (two seconds), while Match display is chosen.  Settling and
// fast-forward never show one (every start, every press); a latch that flaps (Unlocked and back) shows one at most per
// lasting change.  Back to following the display after a message: says so once.
void App::displayLockNotice() {
    const LockReason r = lockVerdict_.reason;
    if (r != heldReason_) {
        heldReason_ = r;
        heldSwaps_ = 0;
    }
    if (r == LockReason::Setting) noticeReason_ = LockReason::Setting;   // chosen again later: report from scratch
    if (!pacingDisplay || r == LockReason::Settling || r == LockReason::FastForward || r == LockReason::Setting) return;
    if (++heldSwaps_ != 120 || r == noticeReason_) return;
    const LockReason was = noticeReason_;
    noticeReason_ = r;
    char msg[160];
    const double displayHz = latch_.measuredPeriodNs() ? 1e9 / (double)latch_.measuredPeriodNs()
                             : latch_.periodNs()         ? 1e9 / (double)latch_.periodNs()
                                                         : 0.0;
    if (r == LockReason::Locked) {
        if (was == LockReason::Setting) return;   // the usual case: no news
        std::snprintf(msg, sizeof msg, "Match display: following the display again (%.2f Hz)", displayHz);
    } else if (r == LockReason::Ratio) {
        std::snprintf(msg, sizeof msg, "Match display: using Jaguar rate (%.2f Hz is not a multiple of %.2f Hz)",
                      displayHz, host.fps());
    } else {
        std::snprintf(msg, sizeof msg, "Match display: using Jaguar rate (%s)", lockReasonText(r));
    }
    toast(msg, 4.0);
}

// Settings > Video > Frame pacing: what it is doing now.
std::string App::pacingStatus() const {
    char buf[160];
    if (!threaded()) return pacingDisplay ? "Match display needs the emulation thread: running at the Jaguar rate" : "";
    const DisplayLockVerdict& v = lockVerdict_;
    if (v.mode == PacingMode::B) {
        const double hz = latch_.measuredPeriodNs() ? 1e9 / (double)latch_.measuredPeriodNs() : 0.0;
        std::snprintf(buf, sizeof buf, "Now: following the display, %.2f Hz, %u refresh%s per frame", hz, v.k,
                      v.k == 1 ? "" : "es");
        return buf;
    }
    if (!pacingDisplay) return "Now: the Jaguar's own rate, kept by the sound card";
    if (v.reason == LockReason::Ratio) {
        const uint64_t p = latch_.measuredPeriodNs() ? latch_.measuredPeriodNs() : latch_.periodNs();
        std::snprintf(buf, sizeof buf, "Now: Jaguar rate (%.2f Hz is not a multiple of %.2f Hz)", p ? 1e9 / (double)p : 0.0,
                      host.fps());
        return buf;
    }
    std::snprintf(buf, sizeof buf, "Now: Jaguar rate (%s)", lockReasonText(v.reason));
    return buf;
}

// UI thread (VJ_PERF_TRACE): one row per iteration, right after its swap.
void App::traceUi(uint64_t takeNs, uint64_t serial, uint64_t swap0, uint64_t swap1) {
    if (!trace_.enabled()) return;
    PerfTrace::Ui u{};
    u.takeNs = takeNs;
    u.serial = serial;
    u.swap0Ns = swap0;
    u.swap1Ns = swap1;
    u.wakeNs = traceWakeNs_;
    u.waitEndNs = threaded() ? latchedAtNs_ : 0;
    u.budgetNs = latch_.budgetNs();
    u.targetNs = traceTargetNs_;
    u.locked = latch_.locked() ? 1 : 0;
    u.waitEvents = traceWaitEvents_;
    u.waitOverNs = traceWaitOverNs_;
    u.waitOverBy = traceWaitOverBy_;
    u.waitCpuNs = stall_.takeCpuNs();
    trace_.ui(u);
    traceWakeNs_ = traceTargetNs_ = traceWaitOverNs_ = 0;
    traceWaitEvents_ = 0;
    traceWaitOverBy_ = 0;
}

// The audio clock for mode A, or -1 when there is none to follow: no device, a paused stream, mute (nothing is
// queued) or fast-forward (the audio is dropped).  PacingClock then uses the wall clock.
double App::emuAudioQueuedMs() const {
    if (!audio.isOpen() || audio.paused() || emuMute_.load(std::memory_order_relaxed) ||
        emuFastForward_.load(std::memory_order_relaxed))
        return -1.0;
    return audio.queuedMs();
}

// UI thread, every iteration and after a pause or a load: the switches the emulation side reads.
void App::syncEmuFlags() {
    emuFastForward_.store(fastForward, std::memory_order_relaxed);
    emuMute_.store(mute, std::memory_order_relaxed);
    emu.setFastForward(fastForward);
    emu.setPaused(paused || headless_);   // headless: frames only through advance(), so a run is exactly --frames long
    emu.setAudioTargetMs(audioTargetMs);
}

// Threaded mode, once per UI iteration (#967): hand the newest input over and adopt what the emulation thread
// published.  Never waits for the emulation.
void App::threadedFrame() {
    publishInput();
    host.pinSnapshot();
    netplay.afterFrame();   // completes an endpoint change once enough emulated frames have run
    const uint64_t fr = emu.framesRun();
    for (uint64_t k = emuFramesSeen_; k < fr; k++) perf.emulated();
    fpsWindowFrames_ += fr - emuFramesSeen_;
    emuFramesSeen_ = fr;
    const uint64_t rt = retroTicks_.exchange(0, std::memory_order_relaxed);
    if (rt) perf.add(Perf::Retro, 1000.0 * (double)rt / (double)perfFreq_);
    if (pendingStateSlot_ != kNoPendingSlot && fr >= pendingStateAt_) {   // openAndLoadState / resume: after 2 frames
        const int slot = pendingStateSlot_;
        pendingStateSlot_ = kNoPendingSlot;
        loadState(slot);
    }
    for (const OsdMessage& m : host.takeMessages()) toast(m.text, m.durationMs / 1000.0, m.level);
}

// Threaded: hand the input the UI thread sees now to the emulation thread, and remember which publish carried an input
// event, so the swap that first shows a frame which polled it can time event -> present (VJ_PERF_CSV).
void App::publishInput() {
    input.poll(!inputCaptured_, inputSnap.takenSince());   // on this thread: SDL_GetMouseState is main-thread only
    const uint64_t seq = inputSnap.publish(input.coreInput(), pendingEventNs_);
    if (pendingEventNs_ && perf.on) {
        if (presentQueue_.size() >= 64) presentQueue_.pop_front();
        presentQueue_.push_back({seq, pendingEventNs_});
    }
    pendingEventNs_ = 0;
}

// Right after the swap (VJ_PERF_CSV): time the input events whose frame this swap shows.  With vsync the swap returns
// at the vblank in both loops, so its return is the same photon proxy for both.
void App::notePresented(uint64_t swapEndNs) {
    if (!perf.on) return;
    auto note = [&](uint64_t eventNs) {
        if (!eventNs || swapEndNs <= eventNs || swapEndNs - eventNs > 250000000ull) return;   // as noteInputLatency
        presSumNs_ += swapEndNs - eventNs;
        presCount_++;
    };
    if (!threaded()) {
        note(presentEventNs_);
        presentEventNs_ = 0;
        return;
    }
    while (!presentQueue_.empty() && presentQueue_.front().first <= shownSeq_) {
        note(presentQueue_.front().second);
        presentQueue_.pop_front();
    }
}

// Threaded + vsync (#967, the late latch): wait until shortly before the next vblank and hand every input event to the
// emulation thread while the event wait is active, so the next emulated frame can poll it. The final <4 ms use a
// precise sleep to absorb the event wait's usual overshoot; input arriving in that tail is polled next iteration.
// The iteration then takes the newest finished frame, builds the UI and swaps just in time.  While the latch does not
// know where the vblanks are (late_latch.h), it returns at once: the unlatched loop.
void App::latchWait() {
    if (latchChecks_++ % 60 == 0) latchUpdatePeriod();   // the window may have moved to another display
    const uint64_t wake = latch_.wakeAt(SDL_GetTicksNS());
    // Mode B (#1038): the wait ends as soon as the frame for this iteration's tick is finished, and the UI builds and
    // swaps at once, as the single-thread loop does.  Past the wake time it goes on for that frame only, up to the last
    // moment a build still makes the vblank (LateLatch::lastChanceNs): a heavy frame (Cybermorph, ~14 ms) finishes after
    // the padded wake time but still fits the period, as it does inline.
    const uint64_t waitTick = lockVerdict_.mode == PacingMode::B ? tickSeq_ : 0;
    const uint64_t until = waitTick && latch_.lastChanceNs() > wake ? latch_.lastChanceNs() : wake;
    const auto frameReady = [&] { return waitTick && publishedTick_.load(std::memory_order_acquire) >= waitTick; };
    traceWakeNs_ = wake;
    traceTargetNs_ = latch_.targetNs();
    const auto over = [this](uint64_t wantNs, unsigned char by) {   // VJ_PERF_TRACE: a blocking call that overran
        const uint64_t now = SDL_GetTicksNS();
        if (now > wantNs && now - wantNs > traceWaitOverNs_) {
            traceWaitOverNs_ = now - wantNs;
            traceWaitOverBy_ = by;
        }
    };
    for (;;) {
        const uint64_t t = SDL_GetTicksNS();
        if (t >= until || quit || frameReady()) return;
        const uint64_t left = until - t;
        const Sint32 ms = (Sint32)LateLatch::eventWaitMs(left);
        if (!ms) {
            stall_.enter(until, 2, trace_.uiRows());
            if (!waitTick) {
                SDL_DelayPrecise(left);
            } else {   // mode B: the frame's wake event cannot end a precise sleep, so look for it every 250 us
                for (uint64_t n = SDL_GetTicksNS(); n < until && !frameReady(); n = SDL_GetTicksNS())
                    SDL_DelayPrecise(std::min<uint64_t>(until - n, 250000));
            }
            stall_.leave();
            over(until, 2);
            return;
        }
        SDL_Event e;
        const uint64_t due = t + (uint64_t)ms * 1000000ull;
        stall_.enter(due, 1, trace_.uiRows());
        const bool got = waitEventUntil(e, due, ms);
        stall_.leave();
        if (!got) {
            over(due, 1);
            continue;
        }
        do {
            if (LatchWaker::isWake(e) || isFrameReady(e)) continue;
            handleEvent(e, false);
            traceWaitEvents_++;
        } while (!quit && SDL_PollEvent(&e));
        publishInput();
    }
}

// The UI thread's timed event wait: true with the first event before `dueNs` (SDL_GetTicksNS), false once it is due
// (or, rarely, early on a wake left from the previous wait: callers re-check the time).
// `ms` is the same deadline in whole ms from now, for SDL_WaitEventTimeout.  With the waker (macOS), its thread ends the
// wait on time and SDL's timeout is only a backstop (latch_waker.h: SDL's own timeout can wake the UI thread 3-20 ms
// late).
bool App::waitEventUntil(SDL_Event& e, uint64_t dueNs, Sint32 ms) {
    if (!latchWaker_.running()) return SDL_WaitEventTimeout(&e, ms);
    latchWaker_.arm(dueNs);
    const bool got = SDL_WaitEventTimeout(&e, ms + LatchWaker::kBackstopMs);
    latchWaker_.disarm();
    return got && !LatchWaker::isWake(e);
}

// Threaded, vsync off (or a swap that did not block): sleep to the next point of a display-period grid instead of a
// period after the swap, so the loop runs at the display rate whatever the build costs (LateLatch::advanceGridNs).
// Events that arrive during the nap are handled and published at once, as in latchWait(): input is not delayed by the nap.
// (SDL 3.4 has no nanosecond SDL_WaitEventTimeoutNS: the wait is in whole ms, the last 2 ms a precise sleep.)
void App::presentNap() {
    const uint64_t period = displayPeriodNs();
    uint64_t now = SDL_GetTicksNS();
    nextPresentNs_ = LateLatch::advanceGridNs(nextPresentNs_, now, period);
    const uint64_t deadline = now + LateLatch::gridNapNs(now, nextPresentNs_, period);
    for (;;) {
        now = SDL_GetTicksNS();
        if (now >= deadline || quit) return;
        const uint64_t left = deadline - now;
        if (left < 2000000) {
            SDL_DelayPrecise(left);
            return;
        }
        SDL_Event e;
        const Sint32 ms = (Sint32)((left - 1000000) / 1000000);
        if (!waitEventUntil(e, now + (uint64_t)ms * 1000000ull, ms)) continue;
        do {
            if (!LatchWaker::isWake(e)) handleEvent(e, false);
        } while (!quit && SDL_PollEvent(&e));
        publishInput();
    }
}

// Called right after every threaded swap: the lock is taken and given up in LateLatch::onSwap, so this is where a change
// shows, whether or not latchWait ran this iteration.  The counters tell "locked and good" from "locked and missing".
void App::latchLog() {
    if (latch_.locked() == latchLogged_) return;
    latchLogged_ = latch_.locked();
    std::fprintf(stderr, "[emu] late latch %s: period %.3f ms, budget %.2f ms, pad %.1f ms, misses %u, releases %u\n",
                 latchLogged_ ? "locked" : "released", (double)latch_.periodNs() / 1e6, (double)latch_.budgetNs() / 1e6,
                 (double)latch_.padNs() / 1e6, latch_.misses(), latch_.released());
}

// The display's refresh period for the latch (0 = unknown: it stays off).
void App::latchUpdatePeriod() {
    const SDL_DisplayMode* m = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(window_));
    uint64_t ns = 0;
    if (m && m->refresh_rate_numerator > 0 && m->refresh_rate_denominator > 0)
        ns = (uint64_t)(1e9 * (double)m->refresh_rate_denominator / (double)m->refresh_rate_numerator);
    else if (m && m->refresh_rate > 0.0f)
        ns = (uint64_t)(1e9 / (double)m->refresh_rate);
    latch_.setPeriodNs(ns);
}

// UI thread, threaded: a content switch (load / unload; the call ran frames.clear() on the emulation thread) must not
// leave the previous game's last picture on screen until the new game's first frame arrives.
void App::adoptFrameEpoch() {
    const uint64_t ep = frames.epoch();
    if (ep == shownEpoch_) return;
    shownEpoch_ = ep;
    video.invalidate();
}

// The newest finished frame: the core's own buffer inline, FrameExchange's newest when threaded (the UI thread then
// never reads the core's buffer).
const Frame& App::shownFrame() {
    static const Frame kNone;
    if (!threaded()) return host.frame();
    if (const FrameExchange::Slot* s = frames.takeNewest()) return s->frame;
    const FrameExchange::Slot* c = frames.current();
    return c ? c->frame : kNone;
}

// advance() gives up the moment a halt holds the core (the budget stays registered and runs after Continue); a headless
// run has no UI to resume it, so it keeps asking in 10 ms slices -- a gdb client's `continue` ends the wait -- and stops
// only when a quit was requested (SIGINT / SIGTERM, B5).
bool App::headlessAdvance(unsigned n) {
    bool done = emu.advance(n, haltGiveUp());
    while (!done && !quitRequested() && !emu.abandoned()) {
        SDL_Delay(10);
        done = emu.advance(0, haltGiveUp());
    }
    return done;
}

void App::runHeadlessFrames(unsigned n) {
    if (threaded()) {
        headlessAdvance(n);
        return;
    }
    for (unsigned i = 0; i < n; i++) {
        audio.update();
        attachGdbHostOnCore();   // a Detach (or the last frame) dropped the silent host: back before each frame
        host.runFrame();
        headlessAfterFrame();
    }
}

// Headless inline, after each host.runFrame(): the halt a gdb client held inside that frame (gdb_wait, a breakpoint, Break)
// ended with its `continue`.  No syncDebug() runs here, so close it on the core's side and refresh the mirror, or
// haltedNow() stays true after the client let go (it blocks the hardcore reload retry).  Same pair as the threaded
// emuRunOneFrame / syncDebug.
void App::headlessAfterFrame() {
    debug.core().frameEnded();
    debug.syncHalt();
}

double App::runSeconds() const { return (double)runFrames_.load() / std::max(1.0, host.fps()); }

// One UI frame without stepping the emulation: events, ImGui, present.  The debugger's halt pump calls this
// repeatedly while the core is parked inside retro_run() at a breakpoint (docs/app-debug-tools.md), so
// nothing here may re-enter the core -- CoreHost refuses state loads, resets, loads and option changes
// while inFrame().
void App::haltedFrame() {
    debug.syncHalt();   // first: the F9 / F10 below must meet the halt the core is parked in, not the one before it
    if (!imguiUp_ || !window_ || threaded()) {   // threaded, the pump runs haltWait(), never this frame
        // Headless inline: no UI to release the core with, so the app's own halts (a breakpoint or Break from a script)
        // and a quit are released at once.  A halt gdb caused stays: returning keeps the core's 2 ms socket loop serving
        // gdb, which is what gdb_wait and a breakpoint set from gdb ask for (the client continues it).
        if (quitRequested()) quitting_.store(true);   // the pump's abortHalt answers CONTINUE (never DETACH: a gdb breakpoint may re-halt)
        else if (!debug.haltByGdb()) debug.detach();   // (inline: the post runs now, inside the pump: CONTINUE, the host dropped at the frame end)
        return;
    }
    SDL_Event e;
    while (SDL_PollEvent(&e)) handleEvent(e, false);
    if (quit) {
        quitting_.store(true);   // the pump's abortHalt reads this: the core resumes when this frame returns
        return;
    }
    inputCaptured_ = ImGui::GetIO().WantCaptureKeyboard;
    inputFrame(*this);
    ach.pump();
    cheatsPump(*this);   // community store installs/checks finish during a halt too
    syncDebug();
    video.upload(host.frame());
    applyViewports();
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    deskPadBeforeNewFrame(*this);   // the library popups' pad nav flags, after the backend's own (desktop_nav_app.h)
    simulateDensity();
    ImGui::NewFrame();
    bool capKb = false;
    drawUi(*this, capKb);
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    presentViewports();
    swapWindow();
    updateTitle();
    if (!wantSync()) SDL_DelayNS(8000000);   // no swap-interval pacing: do not spin while stopped
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
    pacer_.resetSwap(wantSync());
    if (gl_) SDL_GL_SetSwapInterval(wantSync() ? 1 : 0);
}

// Before a swap (#1081).  On macOS SDL's vsync is a CVDisplayLink that ticks once per refresh and a swap waits for the
// tick.  The link is made from the active displays when the GL context is created: with the display asleep it never
// ticks, not even after the display wakes, and the swap blocks forever (initWindow() sets SwapGuard::deadLink for that;
// nothing can end a swap that is already waiting).  A display that falls asleep mid-run did not block on develop (the
// link survives), so the check below is defensive: interval 0 while it sleeps, the user's setting on wake.  Interval 0
// is the vsync-off path -- the swap returns at once, the loop's own clock paces -- so the emulation goes on unseen.
// Throttled: the awake path pays one clock read per swap.  Call before the swap's timestamps, not inside them.
void App::guardSwap() {
    if (!gl_) return;
    const uint64_t t = SDL_GetTicksNS();
    if (t < nextDisplayPollNs_) return;
    nextDisplayPollNs_ = t + 250000000ull;
    if (!swapGuard_.update(platform::displayAsleep(window_))) return;
    std::fprintf(stderr, "[gl] the display %s: swap interval %d\n", swapGuard_.asleep() ? "went to sleep" : "woke",
                 wantSync() ? 1 : 0);
    applyVsync();
}

void App::swapWindow() {
    guardSwap();
    SDL_GL_SwapWindow(window_);
}

// Vsync is a pacing tool for a core that has time to spare (pacing.h).  While the core is behind, SDL_GL_SwapWindow
// would block until the next vblank -- on average half a refresh interval, every UI frame -- on the very thread that
// runs the emulation, so the swap interval is released and restored once the core keeps up again.
void App::syncSwapInterval() {
    if (!gl_) return;
    if (pacer_.updateSwap(wantSync())) SDL_GL_SetSwapInterval(pacer_.swapSync ? 1 : 0);
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

bool App::inWindowMenuBar() const { return menuBarDrawn; }

void App::toggleLibrary() {
    if (homeVisible()) library.focusSearch = true;   // already showing: put the cursor in the search box
    else library.show = !library.show;
}

void App::setFullscreen(bool fs) {
    fullscreen = fs;
    fullscreenMenu.update(fullscreen, fullscreenAutoHideMenu, menuPinned);
    if (window_) SDL_SetWindowFullscreen(window_, fs);
    tvFullscreenEdge();
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
    if (haltedNow() && !threaded()) {   // inline, the call would run inside retro_run; threaded, it detaches first
        toast(kHaltedToast, 2.5, 2);
        return false;
    }
    syncHardcoreLock();   // the load below settles which options are locked
    const std::string path = absolutePath(pathIn);
    if (tvShell().menuOpen()) tvShell().closeMenu();   // the menu belongs to the game it was opened on
    input.clearHolds();                   // a hold or tap pulse in flight belongs to the previous game
    pendingStateSlot_ = kNoPendingSlot;   // a state load queued for the previous game does not follow to this one
    resumeOffer.active = false;
    const bool noResume = suppressResume_ || reloading_;
    // Threaded and halted: release the halt first (the core resumes), THEN save where the game was -- the auto-save waits
    // out the resume instead of being skipped.
    const std::function<bool()> giveUp = releaseForCall();
    if (!reloading_) autoSave(giveUp);    // leaving a running game for another: keep where it was
    for (std::string& f : biosOnce_) f.clear();   // a game is not a BIOS boot
    bootTarget_ = 0;
    std::string err;
    std::vector<std::string> overrideErrors;
    // Everything that touches the core runs where the core lives: on the emulation thread at a frame boundary when
    // threaded (this thread waits), right here otherwise.
    const bool ok = emu.call([&] { return openContentOnCore(path, err, overrideErrors); }, giveUp);
    debug.rearmBreakpoints();   // the list is per game (a no-op after the swap); back in the core if the old game stayed
    if (!ok && err.empty()) err = "the debugger halted the game again";
    host.pinSnapshot();
    seenDiscSwaps_ = host.discSwaps();
    if (threaded()) adoptFrameEpoch();
    if (!ok) {
        toast("Could not open " + pathStem(path) + ": " + err, 5.0, 3);
        std::fprintf(stderr, "open failed: %s\n", err.c_str());
        return false;
    }
    for (const std::string& p : overrideErrors) toast("--option: " + p, 8.0, 3);
    paused = false;
    syncEmuFlags();
    pacer_.reset();
    lastTick_ = now();
    controlsOverlayOnLoad(*this);
    addRecent(path);
    library.noteLaunched(path);   // the library's recently played (library_lists.h)
    invalidateThumbs();
    debug.requestRefresh();
    discordNoteGameStarted(*this);
    toast("Loaded " + host.contentName());
    if (!noResume) offerResume();
    saveConfig();
    return true;
}

// The part of openContent that touches the core, and what feeds it (#967: the emulation thread runs it).
bool App::openContentOnCore(const std::string& path, std::string& err, std::vector<std::string>& overrideErrors) {
    if (testSlowCallMs_) SDL_Delay(testSlowCallMs_);   // VJ_TEST_SLOW_CALL_MS (smoke_busy_threaded)
    host.setDirectories(paths.system, paths.saves);
    applyBios();
    frames.clear();   // no frame of the previous game is shown after this
    // Before the load, on the thread that runs the frames: every frame of this game is counted, however soon it runs
    // after the call returns.  A state load queued for this game (resume / library) waits for 2 of them.
    runFrames_.store(0);
    pendingStateAt_ = emu.framesRun() + 2;
    gdbStubOn_.store(false);
    if (!host.loadContent(path, err)) {
        ach.onContentClosed();
        cheatsOnClose(*this);
        return false;
    }
    // An option override the core does not accept would silently run the default.
    badOverride_ = false;
    for (const std::string& p : host.checkOverrides(true)) {
        badOverride_ = true;
        std::fprintf(stderr, "option error: %s\n", p.c_str());
        overrideErrors.push_back(p);
    }
    for (const std::string& p : host.checkOverrides(false))
        std::fprintf(stderr, "option warning (saved setting ignored): %s\n", p.c_str());
    audio.open(host.sampleRate());   // (re)opens, re-formatted for this game's region
    applyAudioSettings();
    audio.clear();
    audio.setPaused(false);
    input.setContent(contentIdentity(path));   // selects the per-game control overrides
    applyPortDevices(*this);                   // before the first frame: the controller types chosen in Settings
    ach.onContentLoaded();                     // identify the game, look up its achievements
    cheatsAfterLoad(*this);                    // the core drops its cheat list on every unload
    host.publishSnapshot();
    // optionValueFor ignores locks, so the lock is checked separately.
    gdbStubOn_.store(!host.optionLocked("virtualjaguar_gdb_stub") &&
                     host.optionValueFor("virtualjaguar_gdb_stub", "disabled") == "enabled");
    attachGdbHostOnCore();   // before the call returns, so before any frame (a gdb_wait boot halt is seen)
    return true;
}

bool App::openNoContent() {
    if (haltedNow() && !threaded()) {
        toast(kHaltedToast, 2.5, 2);
        return false;
    }
    syncHardcoreLock();   // the load below settles which options are locked
    if (tvShell().menuOpen()) tvShell().closeMenu();   // the menu belongs to the game it was opened on
    input.clearHolds();                   // a hold or tap pulse in flight belongs to the previous game
    pendingStateSlot_ = kNoPendingSlot;   // booting a BIOS replaces the running game like opening another one does
    resumeOffer.active = false;
    const std::function<bool()> giveUp = releaseForCall();
    autoSave(giveUp);
    std::string err;
    const bool ok = emu.call([&] { return openNoContentOnCore(err); }, giveUp);
    debug.rearmBreakpoints();   // see openContent
    if (!ok && err.empty()) err = "the debugger halted the game again";
    host.pinSnapshot();
    seenDiscSwaps_ = host.discSwaps();
    if (threaded()) adoptFrameEpoch();
    if (!ok) {
        toast("Could not boot: " + err, 5.0, 3);
        return false;
    }
    paused = false;
    syncEmuFlags();
    pacer_.reset();
    lastTick_ = now();
    invalidateThumbs();
    discordNoteGameStarted(*this);
    const CoreHost::StagedBios& sb = host.stagedBios(bootTarget_ == VJ_BOOT_CART_BOOT_ROM ? bios::CartBoot : bios::Cd);
    toast(std::string(bootTarget_ == VJ_BOOT_CART_BOOT_ROM ? "Booted the cartridge boot ROM with an empty slot"
                                                           : "Booted the CD BIOS with no disc") +
          (sb.seen ? ": " + bios::describeStaged(sb.source, sb.where, sb.crc) : std::string()));
    return true;
}

bool App::openNoContentOnCore(std::string& err) {
    host.setDirectories(paths.system, paths.saves);
    applyBios();
    frames.clear();
    gdbStubOn_.store(false);
    if (!host.loadNoContent(err)) {
        ach.onContentClosed();
        return false;
    }
    audio.open(host.sampleRate());
    applyAudioSettings();
    audio.clear();
    ach.onContentClosed();   // no game, so nothing to look up
    cheatsOnClose(*this);
    input.setContent("");
    applyPortDevices(*this);
    host.publishSnapshot();
    // optionValueFor ignores locks, so the lock is checked separately.
    gdbStubOn_.store(!host.optionLocked("virtualjaguar_gdb_stub") &&
                     host.optionValueFor("virtualjaguar_gdb_stub", "disabled") == "enabled");
    attachGdbHostOnCore();   // before the call returns, so before any frame (a gdb_wait boot halt is seen)
    return true;
}

bool App::bootBios(bios::Role role, const std::string& file) {
    if (haltedNow()) {   // both modes: the flush below is a plain call, and the boot that follows should not surprise a debugging user
        toast(kHaltedToast, 2.5, 2);
        return false;
    }
    for (std::string& f : biosOnce_) f.clear();
    if (role == bios::CartBoot || role == bios::Cd) biosOnce_[role] = file.empty() ? std::string() : absolutePath(file);
    bootTarget_ = role == bios::CartBoot ? VJ_BOOT_CART_BOOT_ROM : VJ_BOOT_CD_BIOS;
    if (host.isLoaded()) emuCall([this] { host.flushSaveRam(); });
    return openNoContent();
}

void App::setBiosChoice(bios::Role role, const std::string& path) {
    if (role != bios::CartBoot && role != bios::Cd) return;
    biosChoice.of(role) = path.empty() ? std::string() : absolutePath(path);
    bios::storeSelection(cfg, biosChoice);
    cfg.save();
    emuCall([this] { applyBios(); });   // the core reads it at the next load; threaded: where the core lives
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

// VJ_TEST_RESTART_GIVEUPS=N (achievements_hardcore_retry tests): the first N hardcore restarts report what a core that
// halted again before the call ran reports (a gdb breakpoint still armed cannot be staged in a headless run).
bool App::testRestartGiveUp() {
    if (testRestartGiveUps_ < 0) {
        const char* e = SDL_getenv("VJ_TEST_RESTART_GIVEUPS");
        testRestartGiveUps_ = e ? (int)std::max(0L, std::strtol(e, nullptr, 10)) : 0;
    }
    if (testRestartGiveUps_ == 0) return false;
    testRestartGiveUps_--;
    return true;
}

bool App::reloadContent() {
    std::string p = host.loadedPath();   // after a disc swap too: the .srm and the disc list start from what was opened
    if (p.empty()) {
        if (host.isLoaded()) return openNoContent();
        return false;
    }
    if (host.isLoaded() && testRestartGiveUp()) {
        toast(kHaltedToast, 2.5, 2);
        return false;
    }
    reloading_ = true;   // a deliberate restart: neither keeps nor resumes an auto-save
    bool ok = openContent(p);
    reloading_ = false;
    return ok;
}

void App::closeContent() {
    if (!host.isLoaded()) return;
    if (haltedNow() && !threaded()) {
        toast(kHaltedToast, 2.5, 2);
        return;
    }
    if (tvShell().menuOpen()) tvShell().closeMenu();   // gives the pause back before the game goes
    input.clearHolds();
    pendingStateSlot_ = kNoPendingSlot;
    resumeOffer.active = false;
    const std::function<bool()> giveUp = releaseForCall();
    autoSave(giveUp);
    bool ran = false;
    emu.call(
        [this, &ran] {
            ran = true;
            host.unloadContent();
            gdbStubOn_.store(false);
            ach.onContentClosed();
            cheatsOnClose(*this);
            audio.clear();
            input.setContent("");
            frames.clear();
            host.publishSnapshot();
        },
        giveUp);
    if (!ran) {   // the debugger halted the game again before the call could run: still loaded
        debug.rearmBreakpoints();   // the list was disarmed for the release: back in the core
        toast(kHaltedToast, 2.5, 2);
        return;
    }
    host.pinSnapshot();
    if (threaded()) adoptFrameEpoch();
    debug.requestRefresh();
    invalidateThumbs();
}

bool App::reset() {
    if (!host.isLoaded()) return false;
    if (haltedNow() && !threaded()) {
        toast(kHaltedToast, 2.5, 2);
        return false;
    }
    if (testRestartGiveUp()) {
        toast(kHaltedToast, 2.5, 2);
        return false;
    }
    // A call, not a post: retro_reset can re-stage the CD BIOS, which the BIOS panel reads.
    // ach.onReset() drives rc_client (rc_client_reset raises events): on the emulation thread, with do_frame.
    // Threaded and halted: release the halt first (the reset runs once the core resumes); the breakpoint list survives
    // and is back in the core before the first frame after the reset (#1081 M1).
    const bool ran = callReleased([this] {
        host.reset();
        ach.onReset();
    });
    debug.requestRefresh();
    toast(ran ? std::string("Reset") : std::string(kHaltedToast), ran ? 3.0 : 2.5, ran ? 1 : 2);   // not run: halted again
    return ran;
}

void App::togglePause() {
    if (menuHeldPause_ || menuRunsOn_) {   // the TV menu holds the pause: a toggle here would be overwritten when it closes
        // (Hardcore's run-on menu holds none, but its banner promises the game keeps running: a pause behind it would stall the game
        // the player cannot see, and the menu would resume it as it closes.)
        toast(menuHeldPause_ ? "Close the menu to resume" : "The menu has the controller", 1.5);   // (nothing is paused behind a run-on menu)
        return;
    }
    if (!paused) {
        // Hardcore rations pausing (rc_client_can_pause); softcore never refuses.
        std::string why;
        if (!pauseAllowed(why)) {
            toast(why, 2.5, 2);
            return;
        }
    }
    paused = !paused;
    audio.setPaused(paused);
    syncEmuFlags();
    toast(paused ? "Paused" : "Resumed", 1.2);
}

// The TV in-game menu's pause: the same gate and the same pause as togglePause(), without its toast, and it keeps
// what was true before so that a game the player had paused stays paused when the menu closes.  A netplay session
// keeps running (a pause would stall the other side), so there the menu only lays over the picture; hardcore's refusal to pause
// (rc_client_can_pause) opens the menu the same way, so Settings stays reachable mid-game: the menu owns the pads, the game runs on, and
// the menu's banner says so.  `why` (the pause-decay countdown) is left unset: nothing toasts it.
bool App::pauseAllowed(std::string& why) {
    if (testDenyPause) {
        why = "Hardcore: pausing is available again in 3 s";
        return false;
    }
    return ach.allowPause(why);
}

bool App::menuPause(std::string& why) {
    if (menuHeldPause_ || menuRunsOn_) return true;   // already open
    if (netplay.configured()) {
        input.setMenuOwnsInput(true);   // running on, but not for the pads: the menu has them
        return true;
    }
    if (!paused) {
        if (!pauseAllowed(why)) {   // hardcore rations pausing (rc_client_can_pause): the menu opens anyway, over the running game
            menuRunsOn_ = true;
            input.setMenuOwnsInput(true);
            return true;
        }
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
    menuRunsOn_ = false;
    if (!menuHeldPause_) return;
    menuHeldPause_ = false;
    if (paused != pausedBeforeMenu_) {
        paused = pausedBeforeMenu_;
        audio.setPaused(paused);
    }
}

tv::Shell& App::tvShell() { return *tvShell_; }

void App::openLibrary() {
    if (tvMode.active()) tvShell().openLibrary();   // (as Settings: the in-game menu opens even when hardcore will not pause; a refused menu toasts and nothing opens)
    else toggleLibrary();
}

void App::openSettings() {
    if (tvMode.active()) tvShell().openSettings();   // (the in-game menu opens even when hardcore will not pause: the game runs on under it)
    else showSettings = true;
}

void App::openBiosSettings() {
    devSelectTab = "BIOS";
    if (tvMode.active()) {
        if (host.isLoaded() && !tvShell().menuOpen() && !tvShell().openMenu()) return;   // the pads are the window's only over the menu
        tvMoreSettingsOpen(*this);
    } else {
        showSettings = true;
    }
}

void App::showOpenDialog() {
    if (!window_) return;
    g_filterPattern = joinExtensions(CoreHost::validExtensions()) + ";m3u";   // multi-disc playlists: the app reads them (m3u.h)
    g_filters[0].name = g_filterName.c_str();
    g_filters[0].pattern = g_filterPattern.c_str();
    g_filters[1].name = "All files";
    g_filters[1].pattern = "*";
    vj::files::openFile(dialogDone, this, window_, g_filters, 2,
                           g_lastDir.empty() ? nullptr : g_lastDir.c_str(), false);
}

// ---- discs ------------------------------------------------------------------------------------------------------
// Every disc command is a call(), not a post(): inserting a disc can re-stage the CD BIOS, which writes the staged-BIOS
// record the BIOS panel reads (#967).
void App::showDiscDialog() {
    if (!window_) return;
    static const SDL_DialogFileFilter kFilters[2] = {{"Jaguar CD images", "cue;cdi;chd"}, {"All files", "*"}};
    vj::files::openFile(discDialogDone, this, window_, kFilters, 2, g_lastDir.empty() ? nullptr : g_lastDir.c_str(),
                           false);
}

bool App::addDisc(const std::string& path) {
    if (!host.isLoaded()) return false;
    if (haltedNow() && !threaded()) {
        toast(kHaltedToast, 2.5, 2);
        return false;
    }
    int idx = -1;
    bool wasEjected = false, nowEjected = false;
    const bool ran = callReleased([&] {
        wasEjected = host.discInfo().ejected;
        idx = host.discAppend(path);
        // The same as picking it in File > Disc: with the tray closed this swaps at once, with it open the disc goes in
        // when the tray closes.
        if (idx >= 0) host.discSetIndex((unsigned)idx);
        nowEjected = host.discInfo().ejected;
    });
    if (!ran) {   // the debugger halted the game again before the call could run
        toast(kHaltedToast, 2.5, 2);
        return false;
    }
    host.pinSnapshot();
    followDiscSwap();
    if (idx < 0) {
        toast("Not added: " + pathStem(path) + " is not a CD image (cue, cdi or chd)", 3.0, 2);
        return false;
    }
    if (!wasEjected && nowEjected) {   // the core left the tray open and put the previous disc back
        toast("Could not start " + pathStem(path) + " - the tray is open", 3.0, 2);
        return false;
    }
    toast((wasEjected ? "Disc selected: " : "Disc inserted: ") + pathStem(path));
    debug.requestRefresh();
    return true;
}

// After a disc swap, host.contentPath() names the disc that booted (CoreHost::followMountedDisc).  What follows it:
//   * save states and their thumbnails (statePath): the core refuses a state taken on another disc, so a slot belongs
//     to the disc it was saved on -- and the library's save-state tab finds it under that disc;
//   * the window title, Discord (both read contentPath every frame), achievements (rc_client's change-media) and
//     cheats (keyed on the per-title row the core re-reads on an insert, titledb_probe_disc);
// and what stays with what was opened (host.loadedPath()):
//   * the .srm: the core keeps the save RAM -- the EEPROM pair and the Memory Track every CD game shares -- across an
//     insert, so it is still the file it was read from (CoreHost::saveRamPath);
//   * Restart, recents and the library's recently played: reopening has to start from that .srm and that disc;
//   * the per-game control overrides (input.setContent), which are picked once per open.
void App::followDiscSwap() {
    const unsigned n = host.discSwaps();
    if (n == seenDiscSwaps_) return;
    const unsigned prev = seenDiscSwaps_;
    seenDiscSwaps_ = n;
    if (!host.isLoaded()) return;
    pendingStateSlot_ = kNoPendingSlot;   // a state queued for the previous disc would be refused now
    resumeOffer.active = false;           // the offer was for the previous disc's auto-save
    // On the emulation thread, like the load-time hooks they mirror (openContentOnCore).
    bool ran = false;
    emuCall([this, &ran] {
        ran = true;
        ach.onDiscChanged();
        cheatsOnClose(*this);
        cheatsAfterLoad(*this);
    });
    if (!ran) {   // gave up (a halt): the swap is still unfollowed; the UI loop calls this again next iteration
        seenDiscSwaps_ = prev;
        return;
    }
    invalidateThumbs();
    debug.requestRefresh();
}

// ---- resume where I left off ------------------------------------------------------------------------------------
// Policy in library_lists.h (shouldAutoSave / resumeAction), tested in vj_library_tests.  The auto-save is its own
// slot (lib::kAutoSlot): one file pair per game, always the latest, written like any state (temp file + rename).
bool App::autoSave(const std::function<bool()>& giveUp) {
    if (headless_ || !host.isLoaded() || host.contentPath().empty()) return false;   // headless runs and BIOS-only boots never
    if (!lib::shouldAutoSave(resumeMode, !ach.allowStateLoad(), netplay.configured(), runSeconds(), host.crashEvents() > 0))
        return false;
    return saveState(lib::kAutoSlot, true, giveUp);
}

void App::offerResume() {
    if (headless_ || host.contentPath().empty()) return;
    const bool exists = fileExists(statePath(lib::kAutoSlot));
    switch (lib::resumeAction(resumeMode, !ach.allowStateLoad(), netplay.configured(), exists)) {
    case lib::ResumeAction::Load:
        pendingStateSlot_ = lib::kAutoSlot;   // after the first frames, like a state picked in the library
        pendingStateFrames_ = 2;   // (threaded: pendingStateAt_, set by openContentOnCore)
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
void App::openAndLoadState(const std::string& pathIn, int slot, const std::string& stateDisc) {
    if (pathIn.empty() || slot < lib::kAutoSlot) return;
    const std::string path = absolutePath(pathIn);
    if (!ach.allowStateLoad()) {   // before leaving the running game for one whose state would be refused
        toast("Hardcore mode: loading save states is disabled", 3.5, 2);
        return;
    }
    if (slot >= 0) stateSlot = slot;
    // The running game: the disc in the tray, or what was opened (an .m3u, or the disc before a swap).
    auto running = [&] { return host.isLoaded() && (host.contentPath() == path || host.loadedPath() == path); };
    // States remain named for their mounted disc. Reopen the playlist, then select that disc before loading one.
    auto selectStateDisc = [&] {
        if (stateDisc.empty() || host.contentPath() == stateDisc) return true;
        emuCall([&] {
            const auto discs = host.discInfo().paths;
            auto it = std::find(discs.begin(), discs.end(), stateDisc);
            if (it != discs.end()) {
                host.discSetIndex((unsigned)(it - discs.begin()));
                host.discSetEject(false);
            }
        });
        host.pinSnapshot();
        followDiscSwap();
        if (host.contentPath() != stateDisc) { toast("Could not mount the state disc", 3.5, 2); return false; }
        return true;
    };
    if (running()) {
        if (selectStateDisc()) loadState(slot);
        return;
    }
    suppressResume_ = true;   // the state asked for replaces the auto-save offer
    handleDrop(path);
    suppressResume_ = false;
    if (running() && selectStateDisc()) pendingStateSlot_ = slot;   // cleared if the open / mount failed
    pendingStateAt_ = emu.framesRun() + 2;
    pendingStateFrames_ = 2;   // (threaded: pendingStateAt_, set by openContentOnCore)
}

void App::showImportBiosDialog() {
    if (!window_) return;
    static SDL_DialogFileFilter filters[2] = {{"BIOS images", "j64;rom;bin;jag"}, {"All files", "*"}};
    vj::files::openFile(importDialogDone, this, window_, filters, 2, g_lastDir.empty() ? nullptr : g_lastDir.c_str(), false);
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
    if (host.isLoaded()) emuCall([this] { host.flushSaveRam(); });
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

bool App::saveState(int slot, bool quiet, const std::function<bool()>& giveUpIn) {
    if (!host.isLoaded()) {
        if (!quiet) toast("No game loaded");
        return false;
    }
    // A save needs the core at a frame boundary; a halted core is not at one.  Refuse, unless the caller has just released
    // the halt (releaseForCall's predicate: the save then waits for the resume).
    std::function<bool()> giveUp = giveUpIn;
    if (!giveUp) {
        if (debug.halted()) {
            if (!quiet) toast(kHaltedToast, 2.5, 2);
            return false;
        }
        giveUp = haltGiveUp();
    }
    if (slot == lib::kAutoSlot) SDL_CreateDirectory(lib::autoStatesDir(paths.states, paths.instance).c_str());
    const std::string path = statePath(slot);
    std::string err;
    Frame thumb;
    // On the emulation thread: the state and the frame it was taken on.  Right after a load that frame is empty, so
    // there is no thumbnail -- never the previous game's picture.
    const bool ok = emu.call(
        [&] {
            if (!host.saveStateToFile(path, err)) return false;   // written atomically (temp file + rename)
            thumb = host.frame();
            ach.onStateSaved(path);
            return true;
        },
        giveUp);
    if (!ok) {
        if (!quiet) toast(err.empty() ? std::string(kHaltedToast) : "Save state failed: " + err, err.empty() ? 2.5 : 4.0, err.empty() ? 2 : 3);
        return false;
    }
    if (thumb.width && thumb.height) {
        SDL_Surface* s = SDL_CreateSurfaceFrom((int)thumb.width, (int)thumb.height, SDL_PIXELFORMAT_XRGB8888,
                                               (void*)thumb.pixels.data(), (int)thumb.width * 4);
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
    if (!quiet) toast("State saved to " + lib::stateLabel(slot));
    return true;
}

bool App::loadState(int slot) {
    if (!host.isLoaded()) {
        toast("No game loaded");
        return false;
    }
    if (debug.halted()) {
        toast(kHaltedToast, 2.5, 2);
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
    const std::string path = statePath(slot);
    std::string err;
    const bool ok = emuCall([&] {
        if (!host.loadStateFromFile(path, err)) return false;
        audio.clear();
        cheatsAfterStateLoad(*this);
        ach.onStateLoaded(path);
        return true;
    });
    if (!ok) {
        toast(err.empty() ? std::string(kHaltedToast) : "Load state failed: " + err, err.empty() ? 2.5 : 4.0, err.empty() ? 2 : 3);
        return false;
    }
    debug.requestRefresh();
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
        if (fullscreen && fullscreenAutoHideMenu) {
            fullscreenMenu.update(fullscreen, fullscreenAutoHideMenu, menuPinned);
            fullscreenMenu.togglePin(menuPinned);   // a temporary reveal never changes ui.menubar_in_window
        } else if (platform::nativeMenuActive()) showInWindowMenu = !showInWindowMenu;   // reach anything the native menu lacks
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
        if (mod) {   // a desktop window open (incl. "More settings...") closes; else Settings opens (TV mode: the TV screen, which a second press leaves be)
            if (showSettings) showSettings = false;
            else openSettings();
        }
        break;
    case SDLK_L:
        if (mod) openLibrary();
        break;
    case SDLK_N:
        if (mod) newWindow();
        break;
    case SDLK_T:   // View > TV mode, fixed (not a rebindable action); the game keeps running.  A no-op when the setting is Off
        if (mod && shift && !input.capturing() && !input.uiWantsKeyboard) toggleTvMode();
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
    if (LatchWaker::isWake(e)) return;   // a late latch wake that arrived after its wait ended
    if (!headless && imguiUp_) ImGui_ImplSDL3_ProcessEvent(&e);
    if (fromExtraWindow(e)) return;   // ImGui has it; the game and the app's own window handling are about the main window
    if (isInputEvent(e.type) && !pendingEventNs_) pendingEventNs_ = e.common.timestamp;
    // F10 steps while a processor is halted (it releases a captured mouse otherwise, which input.cpp owns).
    if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat && e.key.scancode == SDL_SCANCODE_F10 && debug.halted() &&
        !(imguiUp_ && ImGui::GetIO().WantTextInput)) {
        debug.step();
        return;
    }
    input.feedHoldEdges(e);   // before anything can consume the event, so a release is never lost
    tvHost_->noteInput(e);    // which device the hints should name (the shell consumes events InputManager never sees)
    if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && tvAutoPadPress(e)) return;   // Auto: this press turned TV mode on, and is only that
    if (tvMoreSettingsHandleEvent(*this, e, SDL_GetTicks())) return;   // "More settings" is up: its window has the pads and keys, even over the in-game menu (tv_more_settings.h)
    if (tvShell().ownsInput() && !input.capturing() && tvShell().handleEvent(e, SDL_GetTicks())) return;   // the TV UI has the pads and keys (not while a binding is being captured)
    // The Auto triggers (#991): the mouse and the keyboard hand a controller-started TV mode back (the pad's half, tvAutoPadPress, runs
    // before the shell: its presses are the shell's).  Desktop nav below sees a press only when it did not switch TV mode on.
    if (e.type == SDL_EVENT_MOUSE_MOTION && !input.mouseCaptured()) tvMode.onMouseMove(e.motion.xrel, e.motion.yrel);
    if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat && !(e.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI)) &&
        !(imguiUp_ && ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureKeyboard)) {
        const SDL_Keycode k = SDL_GetKeyFromScancode(e.key.scancode, SDL_KMOD_NONE, false);
        if (k >= 0x20 && k <= 0x7E) tvMode.onNonNavKey();   // a printable key the TV UI did not use (F-keys, Esc and shortcuts never count)
    }
    if (deskPadHandleEvent(*this, e, SDL_GetTicks())) return;   // the desktop library has the pad (desktop_nav_app.h)
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
            files::remember(e.drop.data);
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
    case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
    case SDL_EVENT_DISPLAY_CURRENT_MODE_CHANGED:   // the same display switched its refresh rate
        displayPeriodValid_ = false;   // the next displayPeriodNs() reads the new display's refresh rate
        displayChanged();
        break;
    case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
        displayChanged();
        fullscreen = true;    // also the green button / Ctrl+Cmd+F on macOS, which never go through setFullscreen()
        fullscreenMenu.update(fullscreen, fullscreenAutoHideMenu, menuPinned);
        tvFullscreenEdge();
        break;
    case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
        displayChanged();
        fullscreen = false;
        fullscreenMenu.update(fullscreen, fullscreenAutoHideMenu, menuPinned);
        tvFullscreenEdge();
        break;
    case SDL_EVENT_USER: {
        platform::Cmd ncmd = platform::Cmd::None;
        int narg = 0;
        if (platform::decodeCommandEvent(e, ncmd, narg)) {   // a native menu / tray click (platform.h)
            runNativeCommand(ncmd, narg);
        } else if (e.user.code == 0x56414A47 && e.user.data1) {
            std::string p = (const char*)e.user.data1;
            SDL_free(e.user.data1);
            suppressResume_ = e.user.data2 != nullptr;   // a TV launch starts fresh: the resume prompt needs a mouse
            handleDrop(p);
            suppressResume_ = false;
        } else if (e.user.code == 0x56414A53 && e.user.data1) {   // 'VAJS': the library's "load this state"
            std::string p = (const char*)e.user.data1;
            SDL_free(e.user.data1);
            openAndLoadState(p, (int)(intptr_t)e.user.data2 - 1000);
        } else if (e.user.code == 0x56414A50 && e.user.data1) {   // 'VAJP': a playlist's disc-owned state
            std::unique_ptr<PlaylistStateLaunch> request((PlaylistStateLaunch*)e.user.data1);
            openAndLoadState(request->playlist, (int)(intptr_t)e.user.data2 - 1000, request->disc);
        } else if (e.user.code == 0x564A4441 && e.user.data1) {   // 'VJDA': File > Add disc image...
            std::string p = (const char*)e.user.data1;
            SDL_free(e.user.data1);
            addDisc(p);
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
    PerfTrace::Emu teLast{};   // the last frame's row waits for the view copy below (its fill_ns)
    for (int i = 0; i < frames; i++) {
        host.wantVideo = (i == frames - 1);
        if (!fastForward) audio.update();
        uint64_t r0 = perf.on ? perf.tick() : 0;
        PerfTrace::Emu te{};
        if (trace_.enabled()) te.dueNs = te.startNs = SDL_GetTicksNS();
        attachGdbHostOnCore();   // inline: a user Detach (or a frame of this loop) dropped the silent host; back before EACH frame
        host.runFrame();
        if (trace_.enabled()) {
            te.endNs = SDL_GetTicksNS();
            te.serial = host.frame().serial;
            te.trim = 1.0f;
            te.filtQMs = te.queuedMs = audio.queuedMs();
            if (i == frames - 1) teLast = te;
            else trace_.emu(te);
        }
        if (r0) perf.add(Perf::Retro, 1000.0 * (double)(perf.tick() - r0) / (double)perf.freq);
        perf.emulated();
        ach.afterFrame();
        netplay.afterFrame();
        fpsWindowFrames_++;
        runFrames_.fetch_add(1, std::memory_order_relaxed);   // emulated time of this game (paused time does not count)
    }
    if (frames > 0) {
        // Once per UI frame, not per emulated frame (fast-forward runs four).
        const uint64_t fill0 = trace_.enabled() ? SDL_GetTicksNS() : 0;
        fillDebugViews();
        if (trace_.enabled()) {
            teLast.fillNs = SDL_GetTicksNS() - fill0;
            trace_.emu(teLast);
        }
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

uint32_t App::frameHash(const Frame& f) const {
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
    // The emulation thread (#967) is decided once per run: switching it live would move the core between threads.
    // Headless runs stay inline unless asked (the smoke tests' hashes are inline hashes), whatever the setting says.
    emuThreadThisRun_ = a.emuThread >= 0 ? a.emuThread == 1 : (!a.headless && emuThreadWanted());
    // The headless TV script steps host.runFrame() itself and never starts the thread, so it always runs inline.
    if (a.headless && !a.tvTestScript.empty() && emuThreadThisRun_) {
        emuThreadThisRun_ = false;
        std::fprintf(stderr, "[emu] --emu-thread on is ignored with --headless --tv-test-script (the script runs the core inline)\n");
    }
    // The build channel and the choice this run was made from (#967 3d): every start, headless included (the smokes read it).
    std::fprintf(stderr, "[emu] channel=%s, emulation thread default %s, this run %s\n", kBuildChannel,
                 kEmuThreadDefault ? "on" : "off", emuThreadThisRun_ ? "on" : "off");
    std::fprintf(stderr, "[emu] ui.emu_thread %s -> wanted %s\n", emuThreadPref < 0 ? "unset" : emuThreadPref ? "on" : "off",
                 emuThreadWanted() ? "on" : "off");
    if (a.tv) {
        tvFromCli_ = true;
        tvMode.setOn(true);   // after the config: a no-op when the setting is Off
    }
    if (paths.instance > 1) {
        host.setSaveFallbackDir(paths.savesShared);
        std::fprintf(stderr, "[app] instance %d (saves in %s)\n", paths.instance, paths.saves.c_str());
    }
    wireHost();
    ach.init(*this, a);   // with the emulation thread on, rc_client_do_frame runs there (Achievements::emuFrame)
    cheatsInit(*this, a);
    host.setLogVerbose(a.verbose);
    for (const auto& kv : a.options) host.overrideOption(kv.first, kv.second);
    if (!a.cartBios.empty()) biosCli_[bios::CartBoot] = absolutePath(a.cartBios);
    if (!a.cdBios.empty()) biosCli_[bios::Cd] = absolutePath(a.cdBios);
    bootTarget_ = a.bootBios == "cart" ? VJ_BOOT_CART_BOOT_ROM : VJ_BOOT_CD_BIOS;
    if (!netplayInit(*this, a)) return 2;
#ifdef VJ_HAVE_NEURO
    neuroInit(*this, a);
#endif

    // The core's option table, before any game and before the emulation thread exists (it publishes the table at once):
    // the TV Settings and the desktop Core tab show every option from the first frame (#991).  After loadUserOptions,
    // wireHost (the lock) and the --option overrides, so the display rules read what the first game will.
    host.probeOptions();

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
    emu.setPreciseWake(a.emuWake != 0);   // (read when the thread starts)
    if (emuThreadThisRun_) startEmuThread();   // after the window and the audio device (design section 7)
    lateLatchOn_ = a.lateLatch != 0;
    paceSteerOn_ = a.paceSteer != 0;
    if (a.pacing >= 0) pacingDisplay = (pacingFromCli_ = a.pacing) == 1;
    if (a.latchWake != 0 && !headless_) latchWaker_.start();   // macOS only (latch_waker.h)
    fullscreen = false;
    if (a.fullscreen) setFullscreen(true);
    // G1 gate drivers (#967): --show-window tools opens every Tools window; --perf-halt re-opens them while halted.  The
    // same open test as openDebugWindow (a second toggle would CLOSE the window).
    auto openAllTools = [this](const char* suffix) {
        for (const char* id : {"memory", "clut", "scanner", "cpu", "objects", "blitter", "textures", "scope"}) {
            DebugTool* t = debug.findTool(id);
            if (t && t->toggle && !(t->isOpen && t->isOpen())) t->toggle();
            std::fprintf(stderr, "[app] tool %s open=%d%s\n", id, (t && t->isOpen && t->isOpen()) ? 1 : 0, suffix);
        }
    };
    if (a.showWindow.rfind("settings", 0) == 0) {
        showSettings = true;
        if (a.showWindow.rfind("settings-video", 0) == 0) devSelectTab = "Video";
        else if (a.showWindow.rfind("settings-input", 0) == 0) devSelectTab = "Input";
        else if (a.showWindow.rfind("settings-audio", 0) == 0) devSelectTab = "Audio";
        else if (a.showWindow.rfind("settings-storage", 0) == 0) devSelectTab = "Storage";
        else if (a.showWindow.rfind("settings-interface", 0) == 0) devSelectTab = "Interface";
        else if (a.showWindow.rfind("settings-discord", 0) == 0) devSelectTab = "Discord";
#ifdef VJ_HAVE_NEURO
        else if (a.showWindow.rfind("settings-neuro", 0) == 0) devSelectTab = "Neuro";
#endif
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
    else if (a.showWindow == "tools") openAllTools("");
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
    library.start(paths, cfg, !host.isLoaded() && !homeLibrary && !tvMode.active());   // (TV mode's Library is the TV screen, never this window)
    if (a.showWindow == "library") library.show = true;

    // dev: drive the TV menu from a script (a screenshot of it).  A script that cannot start ends the run through the normal teardown.
    const bool scriptFailed = !a.tvTestScript.empty() && !tvScriptBegin(a, true);
    if (scriptFailed) quit = true;

    lastTick_ = now();
    fpsWindowStart_ = now();
    lastSramFlush_ = now();
    perf.init(a.perfFrames, a.perfGpu);
    trace_.init(a.perfFrames);
    if (trace_.enabled()) stall_.start(trace_.prefix);
    uint64_t traceSerial = 0;
    video.shader.reuseOutput = !a.perfNoReuse;
    perf.forceW = a.perfShaderW;
    perf.forceH = a.perfShaderH;
    video.perf = &perf;

    while (!quitRequested()) {
        if (perf.frameBegin()) {
            int pw = 0, ph = 0;
            SDL_GetWindowSizeInPixels(window_, &pw, &ph);
            perf.note = "window " + std::to_string(pw) + "x" + std::to_string(ph) + " px, vsync " + (vsync ? "on" : "off") +
                        ", shader " + (video.shader.active() ? pathStem(video.shader.preset) : std::string("none")) +
                        ", source " + std::to_string(video.frameWidth()) + "x" + std::to_string(video.frameHeight());
            perf.note += ", chain runs " + std::to_string(video.shader.chainRuns()) + " reuses " +
                         std::to_string(video.shader.chainReuses());
            perf.note += std::string(", emulation thread ") + (threaded() ? "on" : "off");
            perf.note += std::string(", late latch ") +
                         (threaded() && wantSync() && lateLatchOn_
                              ? std::string(latch_.locked() ? "locked" : "not locked") + " (misses " +
                                    std::to_string(latch_.misses()) + ", released " + std::to_string(latch_.released()) +
                                    ", pad " + std::to_string(latch_.padNs() / 1000000.0).substr(0, 4) + " ms)"
                              : std::string("off"));
            if (threaded()) {
                perf.note += std::string(", pace steering ") + (paceSteerOn_ ? "on" : "off") + " (" +
                             std::to_string(emu.phaseSteps()) + " steps)";
                perf.note += std::string(", frame pacing ") + (pacingDisplay ? "match display" : "jaguar rate") + " (" +
                             (emu.modeB() ? "mode B, k=" + std::to_string(lockVerdict_.k) : std::string("mode A")) + ", " +
                             DisplayLock::name(lockVerdict_.reason) + ", ticks dropped " +
                             std::to_string(emu.droppedTicks()) + ", display " +
                             std::to_string(latch_.measuredPeriodNs() ? 1e9 / (double)latch_.measuredPeriodNs() : 0.0).substr(0, 6) +
                             " Hz)";
            }
            perf.report(host.contentName().c_str());
            stall_.stop();
            trace_.write();
            break;
        }
        if (a.perfPause && perf.seen == 30 && !paused && host.isLoaded()) togglePause();   // after a few real frames
        if (a.perfHalt && perf.on && host.isLoaded()) {   // CSV row r = iteration warmup + r: rows ~130-410 are the halt
            if (perf.seen == perf.warmup + 120) {
                if (debug.enabled()) { debug.breakNow(); std::fprintf(stderr, "[perf] halt requested\n"); }
                else std::fprintf(stderr, "[perf] halt unavailable\n");
            } else if (perf.seen == perf.warmup + 240) {
                openAllTools(" (halted)");
                std::fprintf(stderr, "[perf] halted=%d\n", debug.halted() ? 1 : 0);
            } else if (perf.seen == perf.warmup + 420) {
                const bool was = debug.halted();
                if (was) debug.resume();
                std::fprintf(stderr, "[perf] resumed=%d\n", was ? 1 : 0);
            }
        }
        SDL_Event e;
        while (SDL_PollEvent(&e)) handleEvent(e, false);
        if (quit) break;
        if (tvScript_) tvScriptStep();   // the script's lines that are due (the virtual pad stays until shutdown: the hints name it)

        inputCaptured_ = ImGui::GetIO().WantCaptureKeyboard;
        inputFrame(*this);   // mouse capture, the controls-overlay key
        ach.pump();
        cheatsPump(*this);   // community cheat store: finished requests, pack messages (also with the window closed)
        syncDebug();
        discordTick(*this);
#ifdef VJ_HAVE_NEURO
        neuroTick(*this);
#endif
        perf.lap(Perf::Events);
        syncEmuFlags();
        if (threaded()) threadedFrame();
        else stepEmulation();
        followDiscSwap();   // the disc menus (ImGui, native, TV) swap through the core; this catches up after any of them
        perf.lap(Perf::Step);
        video.setSourceFps((float)host.fps());
        const Uint64 takeNs = SDL_GetTicksNS();
        if (threaded()) publishTakeGrid(takeNs);
        if (!threaded()) video.upload(host.frame());
        else {
            adoptFrameEpoch();
            if (const FrameExchange::Slot* s = frames.takeNewest()) {   // nothing new: redraw the one on screen
                video.upload(s->frame);
                shownAspect_ = s->aspect;
                shownSeq_ = s->inputSeq;
            }
        }
        if (trace_.enabled()) {
            const FrameExchange::Slot* cur = threaded() ? frames.current() : nullptr;
            traceSerial = threaded() ? (cur ? cur->frame.serial : 0) : host.frame().serial;
        }
        if (perf.on && perf.gpu) Perf::finish();
        perf.lap(Perf::Upload);

        if (!a.fontThen.empty() && ++fontFrames == 60) {
            if (fonts::applySpec(a.fontThen, fontSettings)) fonts::request(fontSettings);
        }
        fonts::applyPending();   // a typeface change made last frame, before anything draws with the old one
        applyViewports();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        deskPadBeforeNewFrame(*this);
        simulateDensity();
        ImGui::NewFrame();
        if (a.verbose && !densityLogged_ && ++densityFrames_ >= 5) logDensity();
        bool capKb = false;
        drawUi(*this, capKb);
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        if (perf.on && perf.gpu) Perf::finish();
        perf.lap(Perf::Ui);
        tvScriptFrameEnd();   // the script's `shot`: this frame's TV drawing is in the buffer, not yet swapped
        if (!a.screenshot.empty() && ++uiFrames == a.screenshotFrame) {
            saveWindowScreenshot(a.screenshot);
            quit = true;
        }
        presentViewports();
        guardSwap();   // before the timestamps: the latch must not see it as swap time
        const Uint64 swap0 = SDL_GetTicksNS();
        SDL_GL_SwapWindow(window_);
        const Uint64 swap1 = SDL_GetTicksNS();
        const Uint64 swapNs = swap1 - swap0;
        notePresented(swap1);
        traceUi(takeNs, traceSerial, swap0, swap1);
        if (threaded()) {
            if (latchedAtNs_) latch_.noteBuild(swap0 - latchedAtNs_);
            latch_.onSwap(swap0, swap1);
            latchLog();
            displayLockAfterSwap(swap0, swap1);
        }
        perf.lap(Perf::Present);

        updateTitle();
        desktopFrame();
        double t = now();
        if (autosaveSram && host.isLoaded() && t - lastSramFlush_ > 5.0) {
            // Threaded: on the emulation thread, never waited for, and at most one post queued (#1081 M2: the frame lane is
            // held for the whole of a debugger halt, so a periodic post must coalesce, not accumulate).
            if (!threaded()) host.flushSaveRam();
            else if (!sramPosted_.exchange(true))
                emu.post([this] {
                    sramPosted_ = false;
                    host.flushSaveRam();
                });
            lastSramFlush_ = t;
        }
        if (threaded()) {
            // The emulation keeps its own time on its own thread; this loop only presents.  Vsync on and the swap
            // blocked: the late latch waits until just before the next vblank, publishing input as it comes, and the
            // swap paces the rest.  Off, or a swap that returned at once with vsync on (a minimized or occluded window
            // does not block): a short nap, so a new frame still shows within a few ms without spinning a core.
            if (wantSync() && lateLatchOn_ && swapNs >= LateLatch::kBlockedNs) latchWait();
            else if (!wantSync() || swapNs < LateLatch::kBlockedNs)
                presentNap();
            latchedAtNs_ = SDL_GetTicksNS();
            latch_.noteWaitEnd(latchedAtNs_);
        } else {
            syncSwapInterval();
            if (!pacer_.swapSync) {
                // No swap-interval pacing: sleep until the next emulated frame is due -- never when the core is behind.
                double period = 1.0 / std::max(1.0, host.fps());
                double wait = (host.isLoaded() && !paused) ? pacer_.sleepFor(t - lastTick_, period) : 0.008;
                if (wait > 0) SDL_DelayNS((Uint64)(wait * 1e9));
            }
        }
        perf.lap(Perf::Sleep);
        perf.curSync = threaded() ? wantSync() : pacer_.swapSync;
        perfSample();
        if (a.perfInput && perf.on && perf.seen % 30 == 29)   // delays 0.5 .. 16.7 ms in 37 steps: every phase at 60 Hz
            SDL_AddTimerNS(500000ull + (uint64_t)(perfInputK_++ % 37) * 450000ull, perfInputKey,
                           (void*)(uintptr_t)SDL_GetWindowID(window_));
        perf.frameEnd();
    }
    beginQuit();   // before anything below waits on the core: a hosted halt must be gone
    latchWaker_.stop();   // (it pushes SDL events: not after SDL is gone)

    int w = 0, h = 0;
    if (window_ && !fullscreen) {
        SDL_GetWindowSize(window_, &w, &h);
        cfg.setInt("window.w", w);
        cfg.setInt("window.h", h);
    }
    if (host.isLoaded()) {
        // Bounded: a core still halted after beginQuit's wait (or wedged elsewhere) is not waited for -- the unload in
        // shutdown() and the join are the backstop, and they exit the process rather than hang.
        if (!threaded()) host.flushSaveRam(true);
        else if (debug.core().halt().halted)
            std::fprintf(stderr, "[emu] the core is still halted at quit; not flushing the save RAM here\n");
        else if (!emu.callFor(std::chrono::milliseconds(1000), [this] { host.flushSaveRam(true); }))
            std::fprintf(stderr, "[emu] the save RAM flush did not complete within 1 s; going on\n");
    }
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

// --disc-swap: the disc commands end to end, through the entry points the menus use (addDisc for File > Add disc
// image..., runNativeCommand for the tray and File > Disc), reading the disc state the menus read -- the pinned
// snapshot when threaded.
bool App::discSwapHeadlessCheck(const std::string& path) {
    auto menuDisc = [this] {
        host.pinSnapshot();
        followDiscSwap();   // what the UI loop does after each iteration
        return host.discInfo();
    };
    // What follows the disc in the tray and what stays with the opened one (followDiscSwap), as the UI reads it.
    const std::string opened = host.loadedPath(), srm = host.saveRamPath();
    auto identity = [&](const std::string& disc, const char* when) {
        const std::string want = lib::stateFileName(pathStem(disc), stateSlot);
        const std::string state = statePath(stateSlot);
        bool ok = true;
        if (host.contentPath() != disc || host.contentName() != pathStem(disc)) {
            std::fprintf(stderr, "headless: FAIL --disc-swap: %s, the content is '%s', expected '%s'\n", when,
                         host.contentPath().c_str(), disc.c_str());
            ok = false;
        }
        if (state.size() < want.size() || state.compare(state.size() - want.size(), want.size(), want) != 0) {
            std::fprintf(stderr, "headless: FAIL --disc-swap: %s, the state slot is %s, expected one named %s\n", when,
                         state.c_str(), want.c_str());
            ok = false;
        }
        if (host.loadedPath() != opened || host.saveRamPath() != srm) {
            std::fprintf(stderr, "headless: FAIL --disc-swap: %s, the .srm moved to %s (opened %s, .srm %s)\n", when,
                         host.saveRamPath().c_str(), opened.c_str(), srm.c_str());
            ok = false;
        }
        return ok;
    };
    DiscInfo d = menuDisc();
    if (!d.available || d.count != 1 || d.index != 0 || d.ejected) {
        std::fprintf(stderr, "headless: FAIL --disc-swap: the loaded disc is not in the disc list (%u image(s))\n", d.count);
        return false;
    }
    const std::string first = d.labels[0];
    if (!addDisc(absolutePath(path))) {
        std::fprintf(stderr, "headless: FAIL --disc-swap: could not add and insert %s\n", path.c_str());
        return false;
    }
    runHeadlessFrames(60);
    d = menuDisc();
    if (d.count != 2 || d.index != 1 || d.ejected) {
        std::fprintf(stderr, "headless: FAIL --disc-swap: after the add, %u image(s), index %u, tray %s\n", d.count,
                     d.index, d.ejected ? "open" : "closed");
        return false;
    }
    const std::string second = d.labels[1];
    const unsigned swaps = host.discSwaps();
    if (!identity(absolutePath(path), "after the add")) return false;
    // A state saved now belongs to the second disc (the core would refuse it on the first).
    if (!saveState(stateSlot, true) || !fileExists(statePath(stateSlot))) {
        std::fprintf(stderr, "headless: FAIL --disc-swap: no state was written for the second disc\n");
        return false;
    }
    runNativeCommand(platform::Cmd::DiscTray, 0);   // Open tray
    d = menuDisc();
    if (!d.ejected) {
        std::fprintf(stderr, "headless: FAIL --disc-swap: Open tray left the tray closed\n");
        return false;
    }
    if (!identity(absolutePath(path), "with the tray open")) return false;   // still running the second disc
    runNativeCommand(platform::Cmd::DiscSelect, 0);   // File > Disc > the first disc
    runNativeCommand(platform::Cmd::DiscTray, 0);     // Close tray: the first disc boots again
    d = menuDisc();
    if (d.ejected || d.index != 0) {
        std::fprintf(stderr, "headless: FAIL --disc-swap: Close tray left index %u, tray %s\n", d.index,
                     d.ejected ? "open (the insert failed)" : "closed");
        return false;
    }
    if (!identity(opened, "back on the first disc")) return false;
    if (host.discSwaps() != swaps + 1) {
        std::fprintf(stderr, "headless: FAIL --disc-swap: %u disc swap(s) after the swap back, expected 1\n",
                     host.discSwaps() - swaps);
        return false;
    }
    runHeadlessFrames(60);
    std::printf("DISC content %s -> %s -> %s, save RAM %s\n", pathStem(opened).c_str(), pathStem(path).c_str(),
                host.contentName().c_str(), pathStem(host.saveRamPath()).c_str());
    std::printf("DISC swap %s -> %s -> %s (%s)\n", first.c_str(), second.c_str(), d.labels[0].c_str(),
                threaded() ? "emulation thread" : "inline");
    return true;
}

// --disc-insert: File > Add disc image... on the CD BIOS with no disc (#646), the boot that exists to take one.  The
// menus must show the disc section with an empty list, and the add must reach the core's insert: the PASS line quotes
// the core's own "disk control: inserted" log line, tapped for the duration of the add.
bool App::discInsertHeadlessCheck(const std::string& path) {
    host.pinSnapshot();
    DiscInfo d = host.discInfo();
    if (!d.available) {
        std::fprintf(stderr, "headless: FAIL --disc-insert: the disc menu is hidden (no CD unit in this boot)\n");
        return false;
    }
    if (d.count != 0 || d.ejected) {
        std::fprintf(stderr, "headless: FAIL --disc-insert: the tray is not empty and closed (%u image(s), tray %s)\n",
                     d.count, d.ejected ? "open" : "closed");
        return false;
    }
    // The tap is set and cleared where the core lives, so it never changes under a running frame.
    std::string inserted;
    std::function<void(int, const char*)> prevLog;
    emuCall([&] {
        prevLog = host.onLog;
        host.onLog = [&inserted, &prevLog](int level, const char* text) {
            if (std::strstr(text, "disk control: inserted")) inserted = text;
            if (prevLog) prevLog(level, text);
        };
    });
    const bool added = addDisc(absolutePath(path));
    emuCall([&] { host.onLog = prevLog; });
    if (!added) {
        std::fprintf(stderr, "headless: FAIL --disc-insert: could not add and insert %s\n", path.c_str());
        return false;
    }
    runHeadlessFrames(60);
    host.pinSnapshot();
    d = host.discInfo();
    if (!d.available || d.count != 1 || d.index != 0 || d.ejected) {
        std::fprintf(stderr, "headless: FAIL --disc-insert: after the add, %u image(s), index %u, tray %s\n", d.count,
                     d.index, d.ejected ? "open" : "closed");
        return false;
    }
    if (inserted.empty()) {
        std::fprintf(stderr, "headless: FAIL --disc-insert: the core never logged an insert\n");
        return false;
    }
    const size_t at = inserted.find("disk control:");
    std::printf("DISC insert %s into the empty tray (%s): %s\n", d.labels[0].c_str(),
                threaded() ? "emulation thread" : "inline", inserted.c_str() + at);
    return true;
}

// --expect-discs: an .m3u's discs all reach the disc list File > Disc reads (the pinned snapshot when threaded), and the
// two identities split the way followDiscSwap describes: the playlist is what was opened (the .srm, Restart, recents),
// the disc in the tray is the content (the title, the save-state slots, achievements) -- the first disc at boot, the
// last one once File > Disc has put it in.
bool App::discListHeadlessCheck(unsigned want) {
    auto menuDisc = [this] {
        host.pinSnapshot();
        followDiscSwap();   // what the UI loop does after each iteration
        return host.discInfo();
    };
    const std::string opened = host.loadedPath(), srm = host.saveRamPath();
    std::vector<std::string> discs;
    std::string err;
    if (!CoreHost::readPlaylist(opened, discs, err) || discs.size() != want) {
        std::fprintf(stderr, "headless: FAIL --expect-discs: %s is not a playlist of %u disc(s) (%s)\n", opened.c_str(),
                     want, err.empty() ? (std::to_string(discs.size()) + " listed").c_str() : err.c_str());
        return false;
    }
    auto identity = [&](size_t disc, const char* when) {
        bool ok = true;
        const std::string slot = lib::stateFileName(pathStem(discs[disc]), stateSlot), state = statePath(stateSlot);
        if (host.contentPath() != discs[disc] || host.contentName() != pathStem(discs[disc])) {
            std::fprintf(stderr, "headless: FAIL --expect-discs: %s, the content is '%s', expected '%s'\n", when,
                         host.contentPath().c_str(), discs[disc].c_str());
            ok = false;
        }
        if (state.size() < slot.size() || state.compare(state.size() - slot.size(), slot.size(), slot) != 0) {
            std::fprintf(stderr, "headless: FAIL --expect-discs: %s, the state slot is %s, expected one named %s\n", when,
                         state.c_str(), slot.c_str());
            ok = false;
        }
        if (host.loadedPath() != opened || host.saveRamPath() != srm || pathStem(srm) != pathStem(opened)) {
            std::fprintf(stderr, "headless: FAIL --expect-discs: %s, the .srm is %s, expected the playlist's (%s)\n", when,
                         host.saveRamPath().c_str(), opened.c_str());
            ok = false;
        }
        return ok;
    };
    DiscInfo d = menuDisc();
    if (!d.available || d.count != want || d.index != 0 || d.ejected) {
        std::fprintf(stderr, "headless: FAIL --expect-discs: %u image(s), index %u, tray %s; expected %u with the first in\n",
                     d.count, d.index, d.ejected ? "open" : "closed", want);
        return false;
    }
    if (!identity(0, "at boot")) return false;
    if (recents.empty() || recents[0] != opened) {
        std::fprintf(stderr, "headless: FAIL --expect-discs: the recents list starts with %s, not %s\n",
                     recents.empty() ? "nothing" : recents[0].c_str(), opened.c_str());
        return false;
    }
    std::string names;
    for (const std::string& l : d.labels) names += (names.empty() ? "" : ", ") + l;
    if (want > 1) {
        runNativeCommand(platform::Cmd::DiscTray, 0);   // Open tray
        d = menuDisc();                                  // the tray toggle reads the pinned state, as a menu would
        if (!d.ejected) {
            std::fprintf(stderr, "headless: FAIL --expect-discs: Open tray left the tray closed\n");
            return false;
        }
        runNativeCommand(platform::Cmd::DiscSelect, (int)want - 1);   // File > Disc > the last disc
        runNativeCommand(platform::Cmd::DiscTray, 0);                 // Close tray: it boots
        d = menuDisc();
        if (d.ejected || d.index != want - 1) {
            std::fprintf(stderr, "headless: FAIL --expect-discs: inserting the last disc left index %u, tray %s\n", d.index,
                         d.ejected ? "open (the insert failed)" : "closed");
            return false;
        }
        if (!identity(want - 1, "after inserting the last disc")) return false;
        runHeadlessFrames(60);
        if (d.paths != discs || !saveState(8, true)) {
            std::fprintf(stderr, "headless: FAIL --expect-discs: disc paths / state save\n");
            return false;
        }
        // Use the real library event in both running-session and fresh-open paths. The second disc owns slot 8.
        for (bool reopen : {false, true}) {
            if (reopen) closeContent();
            else { emuCall([&] { host.discSetIndex(0); }); menuDisc(); }
            lib::Entry owner;
            owner.file.path = discs.back();
            owner.statePlaylist = opened;
            library.launchState(owner, 8);
            SDL_PumpEvents();
            SDL_Event event;
            const int got = SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_EVENT_USER, SDL_EVENT_USER);
            if (got != 1 || event.user.code != 0x56414A50) {
                std::fprintf(stderr, "headless: FAIL --expect-discs: missing playlist state event\n");
                return false;
            }
            handleEvent(event, true);
            for (int tries = 0; pendingStateSlot_ != kNoPendingSlot && tries < 8; ++tries) {
                if (threaded()) { runHeadlessFrames(1); threadedFrame(); }
                else { lastTick_ = now() - 0.02; stepEmulation(); }
            }
            d = menuDisc();
            if (!identity(want - 1, "after playlist state resume") || d.count != want || d.index != want - 1 ||
                osd_.empty() || osd_.back().text != "State loaded from " + lib::stateLabel(8)) {
                std::fprintf(stderr, "headless: FAIL --expect-discs: %s playlist state resume\n", reopen ? "fresh" : "running");
                return false;
            }
        }
    }
    std::printf("DISC list %u: %s; content %s -> %s, save RAM %s (%s)\n", d.count, names.c_str(),
                pathStem(discs[0]).c_str(), host.contentName().c_str(), pathStem(host.saveRamPath()).c_str(),
                threaded() ? "emulation thread" : "inline");
    return true;
}

int App::runHeadless(const CliArgs& a) {
    audio.open(48043.6);
    applyAudioSettings();
    syncEmuFlags();
    if (emuThreadThisRun_) startEmuThread();   // --emu-thread on: the same run through the emulation thread
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
            if (!host.isLoaded() || host.loadedPath() != absolutePath(path)) {   // the opened file (an .m3u boots its first disc)
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
        cheatsPump(*this);   // the store learns of the load (and says a pack's messages for cheats the command line turned on)
        audioFramesSeen_ = 0;
        uint64_t hlT0 = SDL_GetPerformanceCounter(), hlRun = 0;
        double hlCpu0 = Perf::cpuSeconds();
        if (threaded()) {
            retroTicks_.store(0);
            if (ach.mockMode() || !neuroCliUrl.empty()) {
                // RetroAchievements steps and pokes are frame-numbered and Neuro plays in real time: one frame at a
                // time, in the inline loop's order (the steps, Neuro's tick and wait, then the frame --
                // rc_client_do_frame runs after it, on the emulation thread, and the replica runs in the core's poll
                // -- then the UI's pump).
                for (unsigned i = 0; i < a.frames && !quitRequested(); i++) {
                    ach.headlessFrame(i);
#ifdef VJ_HAVE_NEURO
                    if (!neuroCliUrl.empty()) headlessNeuroFrame(hlT0, i);   // neuroTick (+ syncDebug), then waits for i's time
#endif
                    if (!headlessAdvance(1)) break;
                    ach.pump();
                    if ((i & 15) == 15) emuCall([this] { audio.clear(); });
                    host.pinSnapshot();
                    for (const OsdMessage& m : host.takeMessages()) std::fprintf(stderr, "[osd] %s\n", m.text.c_str());
                }
            } else {
                for (unsigned i = 0; i < a.frames && !quitRequested();) {
                    const unsigned n = std::min(16u, a.frames - i);   // the dummy device drains in real time: clear every 16
                    if (!headlessAdvance(n)) break;
                    i += n;
                    emuCall([this] { audio.clear(); });
                    for (const OsdMessage& m : host.takeMessages()) std::fprintf(stderr, "[osd] %s\n", m.text.c_str());
                }
            }
            hlRun = retroTicks_.exchange(0);
            host.pinSnapshot();
        } else {
            for (unsigned i = 0; i < a.frames && !quitRequested(); i++) {
                ach.headlessFrame(i);
#ifdef VJ_HAVE_NEURO
                if (!neuroCliUrl.empty()) headlessNeuroFrame(hlT0, i);   // --neuro: real time, so she can keep up
#endif
                audio.update();
                uint64_t hr0 = SDL_GetPerformanceCounter();
                attachGdbHostOnCore();   // inline: the silent host is back before each frame
                host.runFrame();
                hlRun += SDL_GetPerformanceCounter() - hr0;
#ifdef VJ_HAVE_NEURO
                if (!neuroCliUrl.empty()) fillDebugViews();   // Neuro's watches: the copier, on this thread when inline
#endif
                headlessAfterFrame();
                ach.afterFrame();
                ach.pump();
                if ((i & 15) == 15) audio.clear();   // the dummy device drains in real time
                for (const OsdMessage& m : host.takeMessages())
                    std::fprintf(stderr, "[osd] %s\n", m.text.c_str());
            }
        }
        if (quitRequested()) {   // a quit ended the loops above (a halt may still hold the core): leave before the checks
#ifdef VJ_HAVE_NEURO
            if (!neuroCliUrl.empty()) neuroHeadlessReport(*this);   // UI-thread state only: a signalled --neuro run still reports
#endif
            beginQuit();
            std::printf("headless: quit requested, exiting cleanly\n");
            std::fflush(stdout);
            return 0;
        }
        {
            double wall = (double)(SDL_GetPerformanceCounter() - hlT0) / (double)perfFreq_;
            double run = (double)hlRun / (double)perfFreq_;
            double cpu = Perf::cpuSeconds() - hlCpu0;
            char cpuTxt[48] = "n/a";
            if (hlCpu0 >= 0 && cpu > 0) std::snprintf(cpuTxt, sizeof cpuTxt, "%.3f s (%.1f fps)", cpu, a.frames / cpu);
            std::fprintf(stderr, "TIMING %u frames: loop %.3f s (%.1f fps), retro_run %.3f s (%.1f fps), cpu %s %s\n",
                         a.frames, wall, a.frames / wall, run, a.frames / run, cpuTxt, label.c_str());
#ifdef VJ_HAVE_NEURO
            if (!neuroCliUrl.empty()) neuroHeadlessReport(*this);
#endif
            int nr = netplayHeadlessCheck(*this, a);
            if (nr == 1) rc = 1;
            else if (nr == 77 && rc == 0) rc = 77;
        }
        if (!a.discSwap.empty() && !discSwapHeadlessCheck(a.discSwap)) rc = 1;
        if (!a.discInsert.empty() && !discInsertHeadlessCheck(a.discInsert)) rc = 1;
        if (a.expectDiscs && !discListHeadlessCheck(a.expectDiscs)) rc = 1;
        if (!emuCall([&] { return cheatsHeadlessCheck(*this, a); })) rc = 1;
        if (!cheatsHeadlessCommunity(*this, a)) rc = 1;
        if (!ach.headlessCheck(a)) rc = 1;
        const Frame& f = shownFrame();
        HeadlessResult r;
        r.path = label;
        r.hash = frameHash(f);
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
            runHeadlessFrames(60);
            uint32_t h1 = frameHash(shownFrame());
            if (!loadState(0)) {
                std::fprintf(stderr, "headless: FAIL could not load the state back\n");
                return 1;
            }
            runHeadlessFrames(60);
            uint32_t h2 = frameHash(shownFrame());
            std::printf("STATE roundtrip %08x %08x\n", h1, h2);
            if (!emuCall([&] { return cheatsHeadlessCheck(*this, a); })) rc = 1;   // the cheats must still hold after a restore
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
    if (rc == 0) std::printf("headless: OK (%zu load(s)%s)\n", results_.size(), threaded() ? ", emulation thread" : "");
    return rc;
}

}  // namespace vj
