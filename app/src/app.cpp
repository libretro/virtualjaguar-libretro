#include "app.h"

#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl3.h"

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

}  // namespace

App::App() : host(CoreHost::instance()) {}
App::~App() { shutdown(); }

double App::now() const { return (double)SDL_GetPerformanceCounter() / (double)perfFreq_; }

// ---------------------------------------------------------------------------------
// config
// ---------------------------------------------------------------------------------
void App::loadConfig() {
    cfg.load(paths.configFile);
    input.init(cfg, paths.root);
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
    autosaveSram = cfg.getBool("saves.autosave", true);
    input.stickAsDpad = cfg.getBool("input.stick_dpad", true);
    g_lastDir = cfg.get("ui.last_dir", "");
    recents.clear();
    for (int i = 0; i < kMaxRecents; i++) {
        std::string r = cfg.get("recent." + std::to_string(i), "");
        if (!r.empty()) recents.push_back(r);
    }
    host.loadUserOptions(cfg.withPrefix("opt."));
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
    cfg.setBool("saves.autosave", autosaveSram);
    cfg.setBool("input.stick_dpad", input.stickAsDpad);
    cfg.set("ui.last_dir", g_lastDir);
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
    SDL_SetAppMetadata("Virtual Jaguar", VJ_APP_VERSION, "com.provenance-emu.virtualjaguar");
    SDL_InitFlags flags = SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS;
    if (!headless_) flags |= SDL_INIT_GAMEPAD;
    if (!SDL_Init(flags)) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }
    sdlUp_ = true;
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
    window_ = SDL_CreateWindow("Virtual Jaguar", w, h,
                               SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
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

bool App::initImGui() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    // No keyboard/gamepad navigation of the UI: those devices belong to the game.
    // (The UI is mouse-driven; WantCaptureKeyboard is then true only while typing.)
    io.ConfigFlags &= ~(ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad);
    g_imguiIni = paths.imguiIni;
    io.IniFilename = g_imguiIni.c_str();
    ImGui::StyleColorsDark();
    ImGuiStyle& st = ImGui::GetStyle();
    st.WindowRounding = 4.0f;
    st.FrameRounding = 3.0f;
    float scale = SDL_GetWindowDisplayScale(window_);
    if (scale < 0.5f) scale = 1.0f;
    st.ScaleAllSizes(scale);
    st.FontScaleDpi = scale;

#ifdef __APPLE__
    const char* glsl = "#version 150";
#else
    const char* glsl = "#version 330 core";
#endif
    if (!ImGui_ImplSDL3_InitForOpenGL(window_, gl_)) return false;
    if (!ImGui_ImplOpenGL3_Init(glsl)) return false;
    imguiUp_ = true;
    if (!video.init()) return false;
    return true;
}

void App::shutdown() {
    ach.shutdown();
    if (host.isLoaded()) host.unloadContent();
    invalidateThumbs();
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
    host.inputPoll = [this]() { input.poll(!inputCaptured_); };
    input.portRaw = [this](unsigned p) { return inputPortIsRaw(*this, p); };
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
}

void App::applyVsync() {
    if (gl_) SDL_GL_SetSwapInterval(vsync ? 1 : 0);
}

void App::applyAudioSettings() {
    audio.setVolume(volume);
    audio.setTargetMs(audioTargetMs);
}

void App::setFullscreen(bool fs) {
    fullscreen = fs;
    if (window_) SDL_SetWindowFullscreen(window_, fs);
}

// ---------------------------------------------------------------------------------
// content
// ---------------------------------------------------------------------------------
bool App::openContent(const std::string& pathIn) {
    const std::string path = absolutePath(pathIn);
    host.setDirectories(paths.system, paths.saves);
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
    accum_ = 0;
    lastTick_ = now();
    // Controller types chosen earlier in Settings.
    for (unsigned p = 0; p < 2; p++) {
        int dev = cfg.getInt("port." + std::to_string(p), -1);
        if (dev >= 0) host.setControllerDevice(p, (unsigned)dev);
    }
    input.setContent(contentIdentity(path));   // selects the per-game control overrides
    addRecent(path);
    invalidateThumbs();
    ach.onContentLoaded();   // identify the game, look up its achievements
    cheatsAfterLoad(*this);   // the core drops its cheat list on every unload
    toast("Loaded " + host.contentName());
    saveConfig();
    return true;
}

bool App::openNoContent() {
    host.setDirectories(paths.system, paths.saves);
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
    accum_ = 0;
    lastTick_ = now();
    invalidateThumbs();
    ach.onContentClosed();   // no game, so nothing to look up
    cheatsOnClose(*this);
    input.setContent("");
    toast("Booted with no cartridge");
    return true;
}

bool App::reloadContent() {
    std::string p = host.contentPath();
    if (p.empty()) {
        if (host.isLoaded()) return openNoContent();
        return false;
    }
    return openContent(p);
}

void App::closeContent() {
    if (!host.isLoaded()) return;
    host.unloadContent();
    ach.onContentClosed();
    cheatsOnClose(*this);
    audio.clear();
    invalidateThumbs();
    input.setContent("");
}

void App::reset() {
    if (!host.isLoaded()) return;
    host.reset();
    ach.onReset();
    toast("Reset");
}

void App::togglePause() {
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

void App::handleDrop(const std::string& path) {
    if (path.empty()) return;
    // Save the old game's SRAM first (openContent -> loadContent does too, but
    // an early flush keeps a failed open from losing anything).
    if (host.isLoaded()) host.flushSaveRam();
    openContent(path);
}

// ---------------------------------------------------------------------------------
// save states
// ---------------------------------------------------------------------------------
std::string App::statePath(int slot) const {
    return paths.states + host.contentName() + ".slot" + std::to_string(slot) + ".state";
}
std::string App::thumbPath(int slot) const {
    return paths.states + host.contentName() + ".slot" + std::to_string(slot) + ".png";
}
bool App::slotExists(int slot) const {
    return host.isLoaded() && fileExists(statePath(slot));
}
SDL_Time App::slotTime(int slot) const {
    SDL_PathInfo info;
    if (SDL_GetPathInfo(statePath(slot).c_str(), &info)) return info.modify_time;
    return 0;
}

bool App::saveState(int slot) {
    if (!host.isLoaded()) {
        toast("No game loaded");
        return false;
    }
    std::string err;
    if (!host.saveStateToFile(statePath(slot), err)) {
        toast("Save state failed: " + err, 4.0, 3);
        return false;
    }
    const Frame& f = host.frame();
    if (f.width && f.height) {
        SDL_Surface* s = SDL_CreateSurfaceFrom((int)f.width, (int)f.height, SDL_PIXELFORMAT_XRGB8888,
                                               (void*)f.pixels.data(), (int)f.width * 4);
        if (s) {
            SDL_SavePNG(s, thumbPath(slot).c_str());
            SDL_DestroySurface(s);
        }
    }
    auto it = thumbs_.find(slot);
    if (it != thumbs_.end()) {
        VideoOut::destroyTexture(it->second.tex);
        thumbs_.erase(it);
    }
    ach.onStateSaved(statePath(slot));
    toast("State saved to slot " + std::to_string(slot));
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
        toast("Slot " + std::to_string(slot) + " is empty");
        return false;
    }
    std::string err;
    if (!host.loadStateFromFile(statePath(slot), err)) {
        toast("Load state failed: " + err, 4.0, 3);
        return false;
    }
    audio.clear();
    cheatsAfterStateLoad(*this);
    ach.onStateLoaded(statePath(slot));
    toast("State loaded from slot " + std::to_string(slot));
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
    bool mod = (k.mod & (SDL_KMOD_GUI | SDL_KMOD_CTRL)) != 0;
    bool alt = (k.mod & SDL_KMOD_ALT) != 0;
    bool shift = (k.mod & SDL_KMOD_SHIFT) != 0;
    switch (k.key) {
    case SDLK_F11: setFullscreen(!fullscreen); break;
    case SDLK_RETURN:
        if (alt) setFullscreen(!fullscreen);
        break;
    case SDLK_ESCAPE:
        if (fullscreen) setFullscreen(false);
        break;
    case SDLK_F1: menuPinned = !menuPinned; break;
    case SDLK_F5: saveState(stateSlot); break;
    case SDLK_F7: loadState(stateSlot); break;
    case SDLK_F6:
        stateSlot = (stateSlot + (shift ? 9 : 1)) % 10;
        toast("State slot " + std::to_string(stateSlot), 1.2);
        break;
    case SDLK_F8: togglePause(); break;
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
        if (mod) library.show = !library.show;
        break;
    default: break;
    }
}

void App::handleEvent(const SDL_Event& e, bool headless) {
    if (!headless && imguiUp_) ImGui_ImplSDL3_ProcessEvent(&e);
    if (input.handleEvent(e)) return;
    switch (e.type) {
    case SDL_EVENT_QUIT:
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        quit = true;
        break;
    case SDL_EVENT_DROP_FILE:
        if (e.drop.data) handleDrop(e.drop.data);
        break;
    case SDL_EVENT_USER:
        if (e.user.code == 0x56414A47 && e.user.data1) {
            std::string p = (const char*)e.user.data1;
            SDL_free(e.user.data1);
            handleDrop(p);
        }
        break;
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP: {
        bool captured = imguiUp_ && ImGui::GetIO().WantCaptureKeyboard;
        if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat && !captured) handleHotkey(e.key);
        if (!captured && host.isLoaded()) {
            unsigned rk = InputManager::retroKey(e.key.scancode);
            if (rk) host.sendKeyEvent(e.type == SDL_EVENT_KEY_DOWN, rk, 0, 0);
        }
        if (e.key.scancode == SDL_SCANCODE_TAB && !captured) fastForward = (e.type == SDL_EVENT_KEY_DOWN);
        break;
    }
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        fastForward = false;
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
    if (!host.isLoaded() || paused) {
        accum_ = 0;
        return;
    }
    double period = 1.0 / std::max(1.0, host.fps());
    int frames = 0;
    if (fastForward) {
        frames = 4;
        accum_ = 0;
    } else {
        accum_ += dt;
        if (accum_ > 0.25) accum_ = period;  // stalled (window drag, debugger): resync
        while (accum_ >= period && frames < 3) {
            frames++;
            accum_ -= period;
        }
        if (accum_ > period * 2) accum_ = 0;
    }
    for (int i = 0; i < frames; i++) {
        host.wantVideo = (i == frames - 1);
        if (!fastForward) audio.update();
        host.runFrame();
        ach.afterFrame();
        fpsWindowFrames_++;
    }
    host.wantVideo = true;
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
    host.setLogVerbose(a.verbose);
    loadConfig();
    wireHost();
    ach.init(*this, a);
    cheatsInit(*this, a);
    host.setLogVerbose(a.verbose);
    for (const auto& kv : a.options) host.overrideOption(kv.first, kv.second);

    int rc = a.headless ? runHeadless(a) : runInteractive(a);

    saveConfig();
    shutdown();
    if (!headlessTmp_.empty()) {
        std::error_code ec;
        std::filesystem::remove_all(headlessTmp_, ec);   // our own temp dir only
    }
    return rc;
}

int App::runInteractive(const CliArgs& a) {
    if (!initWindow() || !initImGui()) return 1;
    applyAudioSettings();
    audio.open(48043.6);
    fullscreen = false;
    if (a.fullscreen) setFullscreen(true);
    if (a.showWindow.rfind("settings", 0) == 0) {
        showSettings = true;
        devSelectVideoTab = a.showWindow.rfind("settings-video", 0) == 0;
        size_t c = a.showWindow.find(':');   // settings:FILTER pre-fills the options search box
        if (c != std::string::npos) std::snprintf(optionFilter, sizeof optionFilter, "%s", a.showWindow.c_str() + c + 1);
    }
    else if (a.showWindow == "states") showStates = true;
    else if (a.showWindow == "about") showAbout = true;
    else if (a.showWindow == "cheats") showCheats = true;
    unsigned uiFrames = 0;

    std::string firstErr;
    if (a.noContent) openNoContent();
    else if (!a.content.empty()) openContent(a.content[0]);
    library.createTexture = &VideoOut::createTextureRGBA;
    library.destroyTexture = &VideoOut::destroyTexture;
    library.start(paths, cfg, !host.isLoaded());
    if (a.showWindow == "library") library.show = true;

    lastTick_ = now();
    fpsWindowStart_ = now();
    lastSramFlush_ = now();

    while (!quit) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) handleEvent(e, false);
        if (quit) break;

        inputCaptured_ = ImGui::GetIO().WantCaptureKeyboard;
        ach.pump();
        stepEmulation();
        video.setSourceFps((float)host.fps());
        video.upload(host.frame());

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        bool capKb = false;
        drawUi(*this, capKb);
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        if (!a.screenshot.empty() && ++uiFrames == a.screenshotFrame) {
            saveWindowScreenshot(a.screenshot);
            quit = true;
        }
        SDL_GL_SwapWindow(window_);

        updateTitle();
        double t = now();
        if (autosaveSram && host.isLoaded() && t - lastSramFlush_ > 5.0) {
            host.flushSaveRam();
            lastSramFlush_ = t;
        }
        if (!vsync) {
            // No swap-interval pacing: sleep until the next emulated frame is due.
            double period = 1.0 / std::max(1.0, host.fps());
            double wait = (host.isLoaded() && !paused) ? period - accum_ - 0.001 : 0.008;
            if (wait > 0.0005) SDL_DelayNS((Uint64)(std::min(wait, 0.016) * 1e9));
        }
    }

    int w = 0, h = 0;
    if (window_ && !fullscreen) {
        SDL_GetWindowSize(window_, &w, &h);
        cfg.setInt("window.w", w);
        cfg.setInt("window.h", h);
    }
    if (host.isLoaded()) host.flushSaveRam(true);
    library.stop();   // joins its threads and frees its textures while the GL context is still alive
    if (a.verbose) {
        double secs = now() - fpsWindowStart_;
        std::fprintf(stderr, "[app] audio: queued %.1f ms, ratio %.5f, underruns %llu\n", audio.queuedMs(),
                     audio.ratio(), (unsigned long long)audio.underruns());
        (void)secs;
    }
    return 0;
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
    if (list.empty()) {
        std::fprintf(stderr, "headless: no content given\n");
        return 2;
    }
    int rc = 0;
    for (const std::string& path : list) {
        // A private, empty save directory per load.
        std::string saves = paths.root + "saves" + std::to_string(headlessLoads_++) + "/";
        SDL_CreateDirectory(saves.c_str());
        paths.saves = saves;

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

        if (badOverride_) {
            std::fprintf(stderr, "headless: FAIL an --option override was rejected by the core\n");
            return 2;
        }
        audioFramesSeen_ = 0;
        for (unsigned i = 0; i < a.frames; i++) {
            ach.headlessFrame(i);
            audio.update();
            host.runFrame();
            ach.afterFrame();
            ach.pump();
            if ((i & 15) == 15) audio.clear();   // the dummy device drains in real time
            for (const OsdMessage& m : host.takeMessages())
                std::fprintf(stderr, "[osd] %s\n", m.text.c_str());
        }
        if (!cheatsHeadlessCheck(*this, a)) rc = 1;
        if (!ach.headlessCheck(a)) rc = 1;
        const Frame& f = host.frame();
        HeadlessResult r;
        r.path = path;
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
                    r.optVisible, r.optTotal, path.c_str());
        std::fflush(stdout);
        if (r.w == 0 || r.h == 0) {
            std::fprintf(stderr, "headless: FAIL %s produced no video frame\n", path.c_str());
            rc = 1;
        }
        if (r.uniform && !a.allowBlank) {
            std::fprintf(stderr, "headless: FAIL %s ended on a uniform (blank) frame\n", path.c_str());
            rc = 1;
        }
        if (r.audioFrames == 0) {
            std::fprintf(stderr, "headless: FAIL %s produced no audio\n", path.c_str());
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
