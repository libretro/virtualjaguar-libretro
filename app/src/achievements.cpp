#include "achievements.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <ctime>
#include <sstream>

#include "app.h"
#include "secret_store.h"
#include "rc_client.h"
#include "rc_consoles.h"
#include "rc_error.h"
#include "rc_hash.h"
#include "rc_version.h"

extern "C" void retro_cheat_reset(void);
extern "C" void retro_cheat_set(unsigned index, bool enabled, const char* code);

namespace vj {

namespace {

std::string lowerExt(const std::string& path) {
    size_t dot = path.find_last_of('.');
    size_t sep = path.find_last_of("/\\");
    if (dot == std::string::npos || (sep != std::string::npos && dot < sep)) return "";
    std::string e = path.substr(dot + 1);
    for (char& c : e) c = (char)std::tolower((unsigned char)c);
    return e;
}

// "r=NAME" / "m=HASH" out of a urlencoded body (never logged: it carries the
// password or the token).
std::string formValue(const std::string& body, const char* key) {
    std::string k = std::string(key) + "=";
    size_t pos = 0;
    while (pos < body.size()) {
        size_t amp = body.find('&', pos);
        std::string item = body.substr(pos, amp == std::string::npos ? std::string::npos : amp - pos);
        if (item.compare(0, k.size(), k) == 0) return item.substr(k.size());
        if (amp == std::string::npos) break;
        pos = amp + 1;
    }
    return "";
}

}  // namespace

// ---------------------------------------------------------------------------------
// rc_client callbacks (C ABI shims)
// ---------------------------------------------------------------------------------
struct AchCallbacks {
    static Achievements* self(rc_client_t* c) { return (Achievements*)rc_client_get_userdata(c); }

    static uint32_t RC_CCONV readMemory(uint32_t address, uint8_t* buffer, uint32_t numBytes, rc_client_t* c) {
        Achievements* a = self(c);
        if (!a || !a->memoryReady_.load()) return 0;
        if (a->app_ && a->app_->emu.threaded() && !a->app_->emu.onEmuThread()) {
            // Core RAM belongs to the emulation thread (#967).  pump() leaves rc_client_idle to that thread, so the only
            // reader that runs here is a game load completed in the UI's HTTP pump, validating its addresses
            // (rc_client_validate_addresses: only the count matters): answer from the memory map, without touching RAM.
            uint32_t avail = 0;
            if (!rc_libretro_memory_find_avail(&a->regions_, address, &avail) || avail == 0) return 0;
            const uint32_t n = avail < numBytes ? avail : numBytes;
            std::memset(buffer, 0, n);
            if (a->offThreadReads_.fetch_add(1) == 0)
                std::fprintf(stderr, "[ra] address check off the emulation thread answered from the memory map\n");
            return n;
        }
        return rc_libretro_memory_read(&a->regions_, address, buffer, numBytes);
    }

    static void RC_CCONV event(const rc_client_event_t* e, rc_client_t* c) {
        if (Achievements* a = self(c)) a->queueEvent(achEventFrom(*e, rc_client_get_hardcore_enabled(c) != 0));
    }

    static void RC_CCONV log(const char* message, const rc_client_t*) {
        std::fprintf(stderr, "[ra] %s\n", message);
    }

    static void RC_CCONV serverCall(const rc_api_request_t* req, rc_client_server_callback_t cb, void* cbdata,
                                    rc_client_t* c) {
        Achievements* a = self(c);
        if (!a) return;
        const std::string post = req->post_data ? req->post_data : "";
        if (a->mockMode()) {
            a->mockRespond(post, cb, cbdata);
            return;
        }
        HttpRequest hr;
        hr.url = req->url ? req->url : "";
        hr.body = post;
        hr.contentType = req->content_type ? req->content_type : "";
        char clause[96] = "";
        rc_client_get_user_agent_clause(c, clause, sizeof clause);
        hr.userAgent = std::string("VirtualJaguar/") + VJ_APP_VERSION + " " + clause;
        a->http_.request(std::move(hr), [cb, cbdata](HttpResponse r) {
            rc_api_server_response_t sr;
            std::memset(&sr, 0, sizeof sr);
            if (r.status == 0) {
                // No answer at all: tell rc_client it may retry.
                sr.body = r.error.c_str();
                sr.body_length = r.error.size();
                sr.http_status_code = RC_API_SERVER_RESPONSE_RETRYABLE_CLIENT_ERROR;
            } else {
                sr.body = r.body.c_str();
                sr.body_length = r.body.size();
                sr.http_status_code = r.status;
            }
            cb(&sr, cbdata);
        });
    }

    static void RC_CCONV loginDone(int result, const char* error, rc_client_t* c, void* ud) {
        Achievements* a = self(c);
        if (!a) return;
        a->loginPending_ = false;
        if (result == RC_OK) {
            // Only an explicit password sign-in writes anything; the start-up token sign-in (ud != null)
            // must not put the token back into config.ini nor touch the store (#902: secondary copies).
            if (!ud) a->persistLogin();
            const rc_client_user_t* u = rc_client_get_user_info(c);
            a->toast("RetroAchievements", std::string("Signed in as ") + (u ? u->display_name : "?"), 1, 4);
            a->setStatus(std::string("Signed in as ") + (u ? u->display_name : "?"));
            a->showLogin = false;
            a->progressAutoTried_ = false;   // the signed-in user's progress can be fetched now
            a->applyHardcore();
            // Content was opened while signed out: identify it now.
            if (a->app_ && a->app_->host.isLoaded() && !a->loading_ && !rc_client_is_game_loaded(c)) a->beginLoad();
        } else {
            std::string msg = error ? error : "login failed";
            a->setStatus("Sign-in failed: " + msg);
            if (ud && result == RC_INVALID_CREDENTIALS && !a->mockMode()) {
                // A stale token: forget it so the next start does not retry forever.  A secondary copy
                // (#902) never writes the shared credential; the primary does.
                if (a->app_->paths.instance <= 1) {
                    secret::clearToken(a->app_->cfg, secret::active(), a->app_->cfg.get("ra.user", ""));
                    a->app_->saveConfig();
                }
                a->tokenInSettings_ = false;
            }
            a->toast("RetroAchievements", "Sign-in failed: " + msg, 2, 7);
        }
    }

    static void RC_CCONV mediaChanged(int result, const char* error, rc_client_t* c, void*) {
        Achievements* a = self(c);
        if (!a || result == RC_OK || result == RC_ABORTED) return;
        const std::string msg = error ? error : "the new disc could not be identified";
        a->toast("RetroAchievements", msg, result == RC_HARDCORE_DISABLED ? 1 : 2, 6);
        a->setStatus("Disc change: " + msg);
        // RC_HARDCORE_DISABLED: rcheevos turned hardcore off for an unidentified disc.  Not applyHardcore(), which
        // would turn it straight back on; pump()'s checkHardcoreTransition passes the change on.  The next open
        // applies the user's choice again.
    }

    static void RC_CCONV loadDone(int result, const char* error, rc_client_t* c, void*) {
        Achievements* a = self(c);
        if (!a) return;
        a->loading_ = false;
        if (result == RC_ABORTED) return;
        if (result == RC_OK) {
            const rc_client_game_t* g = rc_client_get_game_info(c);
            a->gameKnown_ = g && g->id != 0;
            a->gameTitle_ = g && g->title ? g->title : "";
            rc_client_user_game_summary_t s;
            rc_client_get_user_game_summary(c, &s);
            char body[160];
            if (s.num_core_achievements == 0)
                std::snprintf(body, sizeof body, "No achievements for this game yet.");
            else
                std::snprintf(body, sizeof body, "%u of %u achievements unlocked, %u of %u points%s", s.num_unlocked_achievements,
                              s.num_core_achievements, s.points_unlocked, s.points_core,
                              rc_client_get_hardcore_enabled(c) ? "  (hardcore)" : "");
            a->toast(a->gameTitle_.empty() ? "RetroAchievements" : a->gameTitle_, body, 1, 6, a->gameIconUrl());
            a->setStatus(a->gameTitle_ + ": " + body);
        } else {
            a->gameKnown_ = false;
            a->loadFailed_ = true;
            std::string msg = error ? error : "unknown error";
            a->toast("RetroAchievements", msg, result == RC_NO_GAME_LOADED ? 1 : 2, 6);
            a->setStatus("Not tracking this game: " + msg);
        }
        a->applyHardcore();
        // Hardcore starts from a clean boot.  Turning it on mid-game raises
        // rc_client's RESET itself; this covers the other doors in -- signing in,
        // or enabling RetroAchievements, while a game that has been running
        // unrestricted (states loaded, cheats on) is already open.
        if (result == RC_OK && a->gameKnown_ && rc_client_get_hardcore_enabled(c) && a->ranUnrestricted_) {
            a->resetPending_ = true;
            a->toast("Hardcore mode", "Restarting the game: a hardcore run must start from a clean boot.", 1, 6);
        }
    }
};

// ---------------------------------------------------------------------------------
// construction / lifecycle
// ---------------------------------------------------------------------------------
// The core belongs to the emulation thread (#967): run there (inline / already there: at once).
void Achievements::coreCall(const std::function<void()>& fn) {
    if (app_) app_->emu.call([&fn] { fn(); });
    else fn();
}
void Achievements::corePost(std::function<void()> fn) {
    if (app_) app_->emu.post(std::move(fn));
    else fn();
}

Achievements::Achievements() { rc_libretro_memory_destroy(&regions_); }
Achievements::~Achievements() { shutdown(); }

void Achievements::init(App& app, const CliArgs& args) {
    app_ = &app;
    // Headless runs and mock sessions must never reach the real credential store: swap in the
    // in-memory one before anything else can ask for a secret.
    if (args.headless || !args.raMock.empty()) secret::useMemoryBackend();
    enabled_ = app.cfg.getBool("ra.enabled", true);
    hardcoreWanted_ = app.cfg.getBool("ra.hardcore", false);   // softcore until asked for
    unofficial_ = app.cfg.getBool("ra.unofficial", false);
    imagesWanted_ = app.cfg.getBool("ra.images", true);
    userCfgName_ = app.cfg.get("ra.user", "");
    std::snprintf(userBuf, sizeof userBuf, "%s", userCfgName_.c_str());

    client_ = rc_client_create(&AchCallbacks::readMemory, &AchCallbacks::serverCall);
    if (!client_) return;
    if (!args.headless) images.start(app.paths.root);   // headless: no GL, no pictures
    rc_client_set_userdata(client_, this);
    rc_client_set_event_handler(client_, &AchCallbacks::event);
    rc_client_enable_logging(client_, args.verbose ? RC_CLIENT_LOG_LEVEL_INFO : RC_CLIENT_LOG_LEVEL_WARN,
                             &AchCallbacks::log);
    rc_client_set_hardcore_enabled(client_, hardcoreWanted_ ? 1 : 0);
    rc_client_set_unofficial_enabled(client_, unofficial_ ? 1 : 0);

    if (args.headless || !args.raMock.empty()) {
        // Headless runs never touch the network and never sign in for real: the
        // data directory is a fresh temp dir and the only server is the mock.
        // (--ra-mock also works with a window, for screenshots of the UI.)
        if (!headlessSetup(args)) enabled_ = false;
        if (args.showWindow == "ra-list") showList = true;
        if (args.showWindow == "ra-games") { showList = true; dashTab = 1; }
        if (args.showWindow == "ra-games-owned") { showList = true; dashTab = 1; gameFilter = ra::GameFilter::Owned; }   // dev: screenshots
        if (args.showWindow == "ra-profile") { showList = true; dashTab = 2; }
        if (args.showWindow == "ra-login") showLogin = true;
        return;
    }
    if (args.showWindow == "ra-login") showLogin = true;
    if (args.showWindow == "ra-games") { showList = true; dashTab = 1; }
    if (!AchHttp::available()) setStatus("This build has no HTTP support, so RetroAchievements is unavailable.");
    // The saved token lives in the system credential store (a plain-text one in config.ini is moved
    // there now).  Done even with RetroAchievements switched off, so no token lingers in the file.
    const secret::TokenLoad saved = secret::loadToken(app.cfg, secret::active(), userCfgName_, app.paths.instance <= 1);
    tokenInSettings_ = saved.inSettingsFile;
    const std::string& token = saved.token;
    if (enabled_ && !userCfgName_.empty() && !token.empty() && AchHttp::available()) {
        loginPending_ = true;
        rc_client_begin_login_with_token(client_, userCfgName_.c_str(), token.c_str(), &AchCallbacks::loginDone,
                                         (void*)1 /* token login */);
    }
}

void Achievements::shutdown() {
    hashStop_ = true;
    if (hashThread_.joinable()) hashThread_.join();
    http_.shutdown();
    if (client_) {
        rc_client_destroy(client_);
        client_ = nullptr;
    }
    rc_libretro_memory_destroy(&regions_);
    memoryReady_ = false;
    loading_ = false;
    gameKnown_ = false;
    images.stop();   // while the GL context is still current: it destroys the textures
}

uint32_t Achievements::consoleId(const std::string& path) const {
    const std::string e = lowerExt(path);
    // The core's own disc list (libretro.c: need_fullpath "cue|cdi|chd").  NOT bin:
    // that is a cartridge extension for this core, and hashing it as a disc fails.
    if (e == "cue" || e == "chd" || e == "cdi") return RC_CONSOLE_ATARI_JAGUAR_CD;
    return RC_CONSOLE_ATARI_JAGUAR;
}

void Achievements::rebuildMemory() {
    memoryReady_ = false;   // first: an off-thread address check must never see the flag up while regions_ is torn down
    rc_libretro_memory_destroy(&regions_);
    if (!app_) return;
    CoreHost& host = app_->host;
    auto info = [](uint32_t id, rc_libretro_core_memory_info_t* out) {
        uint8_t* d = nullptr;
        size_t n = 0;
        CoreHost::instance().memoryRegion(id, d, n);
        out->data = d;
        out->size = n;
    };
    memoryReady_ = rc_libretro_memory_init(&regions_, host.memoryMap(), info, consoleId(host.contentPath())) != 0;
}

// Threaded, the toast and status writes below run on the emulation thread inside an EmuThread::call(), while the UI
// thread waits (#967): it may write the UI-side fields.
void Achievements::beginLoad() {
    if (!client_ || !app_ || !enabled_) return;
    CoreHost& host = app_->host;
    if (!host.isLoaded() || host.contentPath().empty()) return;   // no-content boot: nothing to look up
    if (!signedIn() && !loginPending_) return;
    loadFailed_ = false;

    const std::string path = host.contentPath();
    const std::string ext = lowerExt(path);
    if (ext == "chd" || ext == "cdi") {
        // rcheevos' disc reader handles cue/bin, gdi and raw tracks only.  Say so
        // rather than hashing garbage (the game still runs, without achievements).
        toast("RetroAchievements", "Disc images in ." + ext + " format cannot be identified yet; use a .cue/.bin image.", 1, 7);
        setStatus("Not tracking this game: ." + ext + " discs cannot be identified (use .cue/.bin)");
        loadFailed_ = true;
        return;
    }
    coreCall([this] { rebuildMemory(); });
    if (!memoryReady_) {
        toast("RetroAchievements", "The core did not publish a usable memory map.", 2, 6);
        return;
    }
    checkSettings();
    applyHardcore();
    loading_ = true;
    gameKnown_ = false;
    rc_client_begin_identify_and_load_game(client_, consoleId(path), path.c_str(), nullptr, 0, &AchCallbacks::loadDone,
                                           nullptr);
}

// Threaded, this runs on the emulation thread inside an EmuThread::call(), while the UI thread waits (#967): it may
// write the UI-side fields.
void Achievements::onContentLoaded() {
    if (!client_) return;
    eventGen_.fetch_add(1);   // events the previous game queued but the UI has not pumped yet are stale
    loadedPath_ = app_->host.contentPath();
    ranUnrestricted_ = false;   // a fresh boot
    if (app_) framesSeen_ = app_->emu.framesRun();   // pump() must not count frames that ran before this as the new game's
    {
        std::lock_guard<std::mutex> l(rpMu_);
        richPresence_.clear();   // a replacement load never shows the previous game's line
    }
    rpFrames_ = 0;
    trackers.clear();
    challenges.clear();
    beginLoad();
    if (hardcoreActive()) corePost([] { retro_cheat_reset(); });   // cheats do not survive into a hardcore session
}

// rc_client's own multi-disc path: the session (and hardcore) carry on when the new disc belongs to the same game,
// and rcheevos decides what an unknown disc means.  A disc it cannot hash (.chd / .cdi) is let through as "[NO HASH]".
// Threaded, this runs on the emulation thread inside an EmuThread::call(), while the UI thread waits (#967): it may
// write the UI-side fields.
void Achievements::onDiscChanged() {
    if (!client_ || !app_) return;
    const std::string path = app_->host.contentPath();
    if (path.empty() || path == loadedPath_) return;
    loadedPath_ = path;
    if (!loading_ && !rc_client_is_game_loaded(client_)) {   // the first disc was not identified: try this one
        loadFailed_ = false;
        beginLoad();
        return;
    }
    rc_client_begin_identify_and_change_media(client_, path.c_str(), nullptr, 0, &AchCallbacks::mediaChanged, nullptr);
}

// Threaded, this runs on the emulation thread inside an EmuThread::call(), while the UI thread waits (#967): it may
// write the UI-side fields.
void Achievements::onContentClosed() {
    eventGen_.fetch_add(1);
    loading_ = false;
    gameKnown_ = false;
    memoryReady_ = false;
    rc_libretro_memory_destroy(&regions_);
    {
        std::lock_guard<std::mutex> l(rpMu_);
        richPresence_.clear();
    }
    rpFrames_ = 0;
    trackers.clear();
    challenges.clear();
    progress.clear();
    if (client_) rc_client_unload_game(client_);
    checkHardcoreTransition();
}

void Achievements::onReset() {
    ranUnrestricted_ = false;
    if (app_) framesSeen_ = app_->emu.framesRun();   // as onContentLoaded
    if (client_ && rc_client_is_game_loaded(client_)) rc_client_reset(client_);
}

void Achievements::afterFrame() {
    if (!client_) return;
    checkHardcoreTransition();   // never run an emulated frame under the wrong cheat policy
    if (!hardcoreActive()) ranUnrestricted_ = true;   // this game has run (or loaded states) outside hardcore
    if (mockMode() && app_ && app_->window()) headlessFrame(++mockFrames_);   // pokes in a windowed mock session
    emuFrame();
}

// After each emulated frame, on the emulation thread (threaded) -- or inline from afterFrame().  Reads game memory.
void Achievements::emuFrame() {
    if (!client_) return;
    if (rc_client_is_game_loaded(client_) && memoryReady_.load()) {
        rc_client_do_frame(client_);   // ends with rc_client_idle(): the scheduled pings evaluate rich presence here
        if (++rpFrames_ >= 60) {
            rpFrames_ = 0;
            char rp[256] = "";
            if (rc_client_has_rich_presence(client_)) rc_client_get_rich_presence_message(client_, rp, sizeof rp);
            std::lock_guard<std::mutex> l(rpMu_);
            richPresence_ = rp;
        }
    } else {
        rc_client_idle(client_);
    }
}

// The emulation thread's idle while no frame runs (paused, or no game identified): pings and retries.
void Achievements::emuIdle() {
    if (client_) rc_client_idle(client_);
}

// The rich-presence line, computed on the emulation thread (it evaluates game memory) every 60 frames.
std::string Achievements::richPresence() const {
    std::lock_guard<std::mutex> l(rpMu_);
    return richPresence_;
}

void Achievements::pump() {
    if (!client_) return;
    http_.pump();
    mockDeliver();
    drainEvents();
    updateImageNetwork();
    images.pump();
    if (showList && dashTab == 1) gamesTabShown();
    // Threaded, rc_client_idle belongs to the emulation thread: it runs inside rc_client_do_frame, or through emuIdle
    // posted here while no frame runs (paused, or no game identified), so pings and retries keep going.
    const bool threaded = app_ && app_->emu.threaded();
    if (!threaded) {
        rc_client_idle(client_);
    } else {
        const uint64_t ran = app_->emu.framesRun();
        if (ran != framesSeen_) {
            if (!hardcoreActive()) ranUnrestricted_ = true;
            if (mockMode() && app_->window())
                for (uint64_t f = framesSeen_; f < ran; f++) headlessFrame(++mockFrames_);
            framesSeen_ = ran;
        } else if (app_->now() - lastIdlePost_ > 1.0) {
            lastIdlePost_ = app_->now();
            app_->emu.post([this] { emuIdle(); });
        }
    }
    drainEvents();   // events raised by rc_client_idle and by completions delivered in this pump
    if (resetPending_) {
        resetPending_ = false;
        // Turning hardcore on mid-game restarts the game (rc_client's rule: the
        // run must start clean).  retro_cheat_reset: cheats may not outlive it.
        corePost([] { retro_cheat_reset(); });
        hardcoreResets_++;
        if (app_ && app_->host.isLoaded()) app_->reset();
    }
    checkHardcoreTransition();
    if (app_) {
        const double t = app_->now();
        if (t - lastSettingsCheck_ > 0.5) {
            lastSettingsCheck_ = t;
            const bool was = settingsOk_;
            checkSettings();
            if (was != settingsOk_) {
                applyHardcore();
                if (!settingsOk_ && hardcoreWanted_)
                    toast("Hardcore paused", blocker_ + " is not allowed in hardcore mode.", 2, 8);
            }
        }
    }
}

// ---------------------------------------------------------------------------------
// policy
// ---------------------------------------------------------------------------------
bool Achievements::signedIn() const { return client_ && rc_client_get_user_info(client_) != nullptr; }

std::string Achievements::userName() const {
    const rc_client_user_t* u = client_ ? rc_client_get_user_info(client_) : nullptr;
    return u ? u->display_name : "";
}

bool Achievements::hardcoreNextGame() const {
    if (!client_ || !enabled_ || !hardcoreWanted_ || !settingsOk_) return false;
    return signedIn() || loginPending_;
}

bool Achievements::hardcoreActive() const { return hardcoreNextGame() && (loading_ || gameKnown_); }

void Achievements::checkSettings() {
    settingsOk_ = true;
    blocker_.clear();
    if (!app_) return;
    CoreHost& host = app_->host;
    const rc_disallowed_setting_t* dis = rc_libretro_get_disallowed_settings(host.coreName());
    if (!dis) return;
    for (const OptionDef& d : host.optionDefs()) {
        std::string v = host.optionValue(d);
        if (!rc_libretro_is_setting_allowed(dis, d.key.c_str(), v.c_str())) {
            settingsOk_ = false;
            blocker_ = d.key + " = " + v;
            return;
        }
    }
}

// rc_client is driven by the emulation thread (rc_client_do_frame / rc_client_idle run there), and it raises events
// from the thread that mutates it: every mutating call from the UI thread goes through coreCall (#967).  Inline, or
// already on the emulation thread, coreCall just runs it.
void Achievements::applyHardcore() {
    if (!client_) return;
    const int want = (hardcoreWanted_ && settingsOk_) ? 1 : 0;
    coreCall([this, want] {
        if (rc_client_get_hardcore_enabled(client_) != want) rc_client_set_hardcore_enabled(client_, want);
    });
}

bool Achievements::allowPause(std::string& why) {
    if (!hardcoreActive() || !rc_client_is_game_loaded(client_)) return true;
    uint32_t frames = 0;
    int ok = 1;
    coreCall([&] { ok = rc_client_can_pause(client_, &frames); });   // it advances the pause-decay state do_frame also touches
    if (ok) return true;
    const double fps = app_ ? std::max(1.0, app_->host.fps()) : 60.0;
    char buf[96];
    std::snprintf(buf, sizeof buf, "Hardcore: pausing is available again in %.0f s", std::ceil(frames / fps));
    why = buf;
    return false;
}

void Achievements::checkHardcoreTransition() {
    const bool now = hardcoreActive();
    if (now == lastHardcore_) return;
    lastHardcore_ = now;
    if (now) corePost([] { retro_cheat_reset(); });   // whatever is in the core goes, whoever put it there
    if (onHardcoreChanged) onHardcoreChanged(now);
}

void Achievements::notice(const std::string& title, const std::string& body, int kind, double life) {
    noticeLog_.push_back(title + ": " + body);
    toast(title, body, kind, life);
}

void Achievements::setHardcoreWanted(bool on) {
    hardcoreWanted_ = on;
    if (app_) {
        app_->cfg.setBool("ra.hardcore", on);
        app_->saveConfig();
    }
    checkSettings();
    applyHardcore();
    if (on && !settingsOk_) {
        toast("Hardcore blocked", blocker_ + " is not allowed in hardcore mode. Change it, then restart the game.", 2, 8);
    } else if (on) {
        corePost([] { retro_cheat_reset(); });
        toast("Hardcore mode", "State loading and cheats are off. Enabling it restarts the game.", 1, 6);
    } else {
        toast("Softcore mode", "State loading is available again.", 1, 4);
    }
    checkHardcoreTransition();
}

void Achievements::setImagesWanted(bool on) {
    imagesWanted_ = on;
    if (app_) {
        app_->cfg.setBool("ra.images", on);
        app_->saveConfig();
    }
    updateImageNetwork();
}

void Achievements::setUnofficialWanted(bool on) {
    unofficial_ = on;
    if (client_) rc_client_set_unofficial_enabled(client_, on ? 1 : 0);
    if (app_) {
        app_->cfg.setBool("ra.unofficial", on);
        app_->saveConfig();
    }
}

void Achievements::setEnabled(bool on) {
    enabled_ = on;
    if (app_) {
        app_->cfg.setBool("ra.enabled", on);
        app_->saveConfig();
    }
    if (!on) {
        loading_ = false;
        gameKnown_ = false;
        if (client_) coreCall([this] { rc_client_unload_game(client_); });   // frees the game do_frame runs on
        checkHardcoreTransition();
    } else if (app_ && app_->host.isLoaded()) {
        beginLoad();
    }
}

// ---------------------------------------------------------------------------------
// account
// ---------------------------------------------------------------------------------
void Achievements::login(const std::string& user, const std::string& password) {
    if (!client_ || loginPending_) return;
    if (!AchHttp::available() && !mockMode()) {
        setStatus("This build has no HTTP support, so RetroAchievements is unavailable.");
        return;
    }
    loginPending_ = true;
    setStatus("Signing in...");
    rc_client_begin_login_with_password(client_, user.c_str(), password.c_str(), &AchCallbacks::loginDone, nullptr);
}

void Achievements::persistLogin() {
    const rc_client_user_t* u = client_ ? rc_client_get_user_info(client_) : nullptr;
    if (!u || !app_) return;
    if (mockMode()) return;   // a canned token must never reach the real store or config
    // The TOKEN only: the password is not kept anywhere past the request.  It goes to the system
    // credential store; only when that refuses does it stay in config.ini (with a hint in the UI).
    const std::string prevUser = app_->cfg.get("ra.user", "");
    tokenInSettings_ = !secret::saveToken(app_->cfg, secret::active(), u->username, prevUser, u->token);
    app_->cfg.set("ra.user", u->username);
    userCfgName_ = u->username;
    app_->saveConfig();
}

void Achievements::logout() {
    if (!client_) return;
    coreCall([this] { rc_client_logout(client_); });   // also unloads the game: not while do_frame runs on it
    loading_ = false;
    gameKnown_ = false;
    loginPending_ = false;
    trackers.clear();
    challenges.clear();
    dashSignedOut();
    if (app_ && !mockMode()) {   // a mock session never owns the real token
        secret::clearToken(app_->cfg, secret::active(), app_->cfg.get("ra.user", ""));
        app_->saveConfig();
    }
    tokenInSettings_ = false;
    setStatus("Signed out.");
    toast("RetroAchievements", "Signed out.", 1, 3);
    checkHardcoreTransition();
}

// ---------------------------------------------------------------------------------
// save states
// ---------------------------------------------------------------------------------
// Threaded, this runs on the emulation thread inside an EmuThread::call(), while the UI thread waits (#967): it may
// write the UI-side fields.
void Achievements::onStateSaved(const std::string& statePath) {
    const std::string side = statePath + ".rcp";
    if (!client_ || !rc_client_is_game_loaded(client_)) {
        SDL_RemovePath(side.c_str());
        return;
    }
    const size_t n = rc_client_progress_size(client_);
    if (n == 0) {
        SDL_RemovePath(side.c_str());
        return;
    }
    std::vector<uint8_t> buf(n);
    if (rc_client_serialize_progress_sized(client_, buf.data(), n) == RC_OK) writeFileAtomic(side, buf.data(), n);
}

// Threaded, this runs on the emulation thread inside an EmuThread::call(), while the UI thread waits (#967): it may
// write the UI-side fields.
void Achievements::onStateLoaded(const std::string& statePath) {
    if (!client_ || !rc_client_is_game_loaded(client_)) return;
    size_t n = 0;
    void* data = SDL_LoadFile((statePath + ".rcp").c_str(), &n);
    int rc = RC_INVALID_STATE;
    if (data) {
        rc = rc_client_deserialize_progress_sized(client_, (const uint8_t*)data, n);
        SDL_free(data);
    }
    // An older state without (or with a bad) sidecar: start the runtime clean
    // rather than judging the restored machine against stale hit counts.
    if (rc != RC_OK) rc_client_reset(client_);
}

// ---------------------------------------------------------------------------------
// events
// ---------------------------------------------------------------------------------
void Achievements::toast(const std::string& title, const std::string& body, int kind, double life,
                         const std::string& icon) {
    AchToast t;
    t.iconUrl = icon;
    t.title = title;
    t.body = body;
    t.kind = kind;
    t.life = life;
    t.born = app_ ? app_->now() : 0;
    toasts.push_back(std::move(t));
    if (toasts.size() > 5) toasts.erase(toasts.begin());
    if (app_ && !app_->window()) std::fprintf(stderr, "[ra] %s: %s\n", title.c_str(), body.c_str());
}

void Achievements::queueEvent(AchEvent e) {
    e.gen = eventGen_.load();
    std::lock_guard<std::mutex> l(eventMu_);
    events_.push_back(std::move(e));
}

void Achievements::drainEvents() {
    std::vector<AchEvent> ready;
    {
        std::lock_guard<std::mutex> l(eventMu_);
        ready.swap(events_);
    }
    const uint32_t gen = eventGen_.load();
    for (const AchEvent& e : ready) {
        if (e.gen != gen && !achKeepStaleEvent(e.type)) continue;   // the old game's tracker / indicator: no HIDE would come
        handleEvent(e);
    }
}

void Achievements::handleEvent(const AchEvent& e) {
    switch (e.type) {
    case RC_CLIENT_EVENT_ACHIEVEMENT_TRIGGERED: {
        unlocked_.push_back(e.id);
        char body[320];
        std::snprintf(body, sizeof body, "%s  (%u pt%s)", e.description.c_str(), e.points, e.points == 1 ? "" : "s");
        char badge[256] = "";
        if (!e.badgeUrl.empty()) std::snprintf(badge, sizeof badge, "%s", e.badgeUrl.c_str());
        toast(std::string("Achievement unlocked: ") + e.title, body, 0, 7, badge);
        {
            SessionUnlock u;
            u.title = e.title;
            u.description = e.description;
            u.badgeUrl = badge;
            u.points = e.points;
            u.hardcore = e.hardcore;
            u.when = (long long)std::time(nullptr);
            sessionUnlocks.insert(sessionUnlocks.begin(), std::move(u));
            if (sessionUnlocks.size() > 50) sessionUnlocks.pop_back();
        }
        std::fprintf(stderr, "[ra] unlocked id=%u \"%s\" %u points%s\n", e.id, e.title.c_str(), e.points,
                     e.hardcore ? " (hardcore)" : "");
        challenges.erase(e.id);
        break;
    }
    case RC_CLIENT_EVENT_GAME_COMPLETED:
        toast(gameTitle_.empty() ? "Game complete" : gameTitle_,
              e.hardcore ? "Mastered: every achievement earned in hardcore!" : "Completed: every achievement earned!", 0, 8);
        break;
    case RC_CLIENT_EVENT_SUBSET_COMPLETED:
        toast(!e.subsetTitle.empty() ? e.subsetTitle : "Set complete", "Every achievement in this set earned!", 0, 7);
        break;
    case RC_CLIENT_EVENT_LEADERBOARD_STARTED:
        toast("Leaderboard attempt started", e.title, 1, 4);
        break;
    case RC_CLIENT_EVENT_LEADERBOARD_FAILED:
        toast("Leaderboard attempt failed", e.title, 2, 4);
        break;
    case RC_CLIENT_EVENT_LEADERBOARD_SUBMITTED:
        toast("Leaderboard submitted", e.title, 1, 5);
        break;
    case RC_CLIENT_EVENT_LEADERBOARD_SCOREBOARD: {
        char body[160];
        std::snprintf(body, sizeof body, "Score %s  -  rank %u of %u", e.submittedScore.c_str(), e.newRank, e.numEntries);
        toast("Leaderboard result", body, 1, 7);
        break;
    }
    case RC_CLIENT_EVENT_LEADERBOARD_TRACKER_SHOW:
    case RC_CLIENT_EVENT_LEADERBOARD_TRACKER_UPDATE:
        trackers[e.id] = e.display;
        break;
    case RC_CLIENT_EVENT_LEADERBOARD_TRACKER_HIDE:
        trackers.erase(e.id);
        break;
    case RC_CLIENT_EVENT_ACHIEVEMENT_CHALLENGE_INDICATOR_SHOW:
        challenges[e.id] = e.title;
        break;
    case RC_CLIENT_EVENT_ACHIEVEMENT_CHALLENGE_INDICATOR_HIDE:
        challenges.erase(e.id);
        break;
    case RC_CLIENT_EVENT_ACHIEVEMENT_PROGRESS_INDICATOR_SHOW:
    case RC_CLIENT_EVENT_ACHIEVEMENT_PROGRESS_INDICATOR_UPDATE:
        progress = e.title + ": " + e.measured;
        progressBorn = app_ ? app_->now() : 0;
        break;
    case RC_CLIENT_EVENT_ACHIEVEMENT_PROGRESS_INDICATOR_HIDE:
        progress.clear();
        break;
    case RC_CLIENT_EVENT_RESET:
        resetPending_ = true;   // acted on in pump(), never from inside rc_client
        break;
    case RC_CLIENT_EVENT_SERVER_ERROR:
        toast("RetroAchievements server error", e.api + ": " + e.error, 2, 8);
        break;
    case RC_CLIENT_EVENT_DISCONNECTED:
        toast("RetroAchievements", "Cannot reach the server. Unlocks are kept and sent when it is back.", 2, 8);
        break;
    case RC_CLIENT_EVENT_RECONNECTED:
        toast("RetroAchievements", "Reconnected: pending unlocks were sent.", 1, 5);
        break;
    default: break;
    }
}

// ---------------------------------------------------------------------------------
// headless mock server
// ---------------------------------------------------------------------------------
void Achievements::mockRespond(const std::string& post, rc_client_server_callback_t cb, void* cbdata) {
    const std::string name = formValue(post, "r");
    std::lock_guard<std::mutex> lock(mockMu_);   // an award arrives here from the emulation thread when it is on
    mockLog_.push_back(name);
    if (name == "gameid" || name == "achievementsets" || name == "patch") {
        const std::string h = formValue(post, "m");
        if (!h.empty()) mockHashes_.push_back(h);
    }
    // <r>_<s|c>.json first (systemgames_17.json / allprogress_77.json: one list per console), then <r>.json.
    std::string fixture = mockDir_ + "/" + name + ".json";
    for (const char* key : {"s", "c"}) {
        const std::string v = formValue(post, key);
        if (v.empty() || (name != "systemgames" && name != "allprogress")) continue;
        std::ifstream probe(mockDir_ + "/" + name + "_" + v + ".json", std::ios::binary);
        if (probe) fixture = mockDir_ + "/" + name + "_" + v + ".json";
    }
    std::ifstream f(fixture, std::ios::binary);
    std::string body;
    int status = 200;
    if (f) {
        std::stringstream ss;
        ss << f.rdbuf();
        body = ss.str();
        // awardachievement echoes the id that was sent.
        const std::string id = formValue(post, "a");
        for (size_t at; !id.empty() && (at = body.find("@ID@")) != std::string::npos;) body.replace(at, 4, id);
    } else {
        mockUnhandled_.push_back(name);
        std::fprintf(stderr, "[ra-mock] no fixture for r=%s (looked in %s)\n", name.c_str(), mockDir_.c_str());
        body = "{\"Success\":false,\"Error\":\"mock: no fixture\"}";
        status = 404;
    }
    // Delivered from pump(), like a real reply: the production code never sees
    // a response arrive inside the call that asked for it.
    mockQueue_.push_back(MockReply{cb, cbdata, std::move(body), status});
}

void Achievements::mockDeliver() {
    std::deque<MockReply> ready;
    {
        std::lock_guard<std::mutex> l(mockMu_);
        ready.swap(mockQueue_);   // callbacks may queue the next request: never call one with the lock held
    }
    for (MockReply& r : ready) {
        rc_api_server_response_t sr;
        std::memset(&sr, 0, sizeof sr);
        sr.body = r.body.c_str();
        sr.body_length = r.body.size();
        sr.http_status_code = r.status;
        r.cb(&sr, r.cbdata);
    }
}

bool Achievements::headlessSetup(const CliArgs& a) {
    if (a.raMock.empty()) return false;   // headless without a mock: achievements stay off
    mockDir_ = a.raMock;
    hardcoreWanted_ = a.raHardcore;
    rc_client_set_hardcore_enabled(client_, hardcoreWanted_ ? 1 : 0);
    for (const auto& p : a.raPokes) pokes_.push_back(Poke{p.first, p.second, false});
    for (const auto& s : a.raSteps) steps_.push_back(Step{s.first, s.second, false});
    loginAt_ = a.raLoginAt;
    if (loginAt_ == 0) {
        loginPending_ = true;
        // The mock accepts any token; this is the login path of a real start-up.
        rc_client_begin_login_with_token(client_, mockUser_.c_str(), "mock-token", &AchCallbacks::loginDone, (void*)1);
    }
    return true;
}

// FRAME:ACTION steps of a headless run.  hardcore:on|off and signout go through the
// same calls as the menu; poke/expect touch main RAM straight through the core's
// pointer (poke = a test scribbling over a byte so a cheat has something to
// restore; expect = the assertion).
void Achievements::runStep(const Step& s, unsigned frame) {
    auto fail = [&](const std::string& why) {
        stepFailures_.push_back("frame " + std::to_string(frame) + " [" + s.action + "]: " + why);
    };
    auto parseRam = [&](const std::string& t, unsigned& addr, unsigned& val) {
        return std::sscanf(t.c_str(), "%x=%x", &addr, &val) == 2;
    };
    if (s.action == "hardcore:on") setHardcoreWanted(true);
    else if (s.action == "hardcore:off") setHardcoreWanted(false);
    else if (s.action == "signout") logout();
    else if (s.action.compare(0, 6, "games:") == 0) headlessGamesStep(s.action.substr(6), frame);
    else if (s.action.compare(0, 5, "poke:") == 0 || s.action.compare(0, 7, "expect:") == 0) {
        const bool poke = s.action[0] == 'p';
        unsigned addr = 0, val = 0;
        if (!parseRam(s.action.substr(poke ? 5 : 7), addr, val)) { fail("bad ADDR=VV"); return; }
        // Core RAM is the emulation thread's (#967): the access runs there; the verdict is computed inside.
        coreCall([&] {
            uint8_t* ram = nullptr;
            size_t size = 0;
            if (!app_->host.memoryRegion(RETRO_MEMORY_SYSTEM_RAM, ram, size) || addr >= size) { fail("RAM not readable"); return; }
            if (poke) {
                ram[addr] = (uint8_t)val;
            } else if (ram[addr] != (uint8_t)val) {
                char buf[96];
                std::snprintf(buf, sizeof buf, "RAM 0x%06X is %02X, expected %02X", addr, (unsigned)ram[addr], val);
                fail(buf);
            } else {
                std::printf("ra-step: frame %u RAM 0x%06X = %02X ok\n", frame, addr, val);
            }
        });
    } else {
        fail("unknown step");
    }
}

void Achievements::headlessFrame(unsigned frame) {
    for (Step& s : steps_) {
        if (s.done || s.frame > frame) continue;
        s.done = true;
        runStep(s, frame);
    }
    if (loginAt_ && frame >= loginAt_ && !loginPending_ && !signedIn()) {
        // A late sign-in: the game has been running for a while already.
        loginPending_ = true;
        rc_client_begin_login_with_token(client_, mockUser_.c_str(), "mock-token", &AchCallbacks::loginDone, (void*)1);
    }
    for (Poke& p : pokes_) {
        if (p.done || p.frame > frame) continue;
        p.done = true;
        unlockedBeforePoke_ = (unsigned)unlocked_.size();
        std::fprintf(stderr, "[ra-test] frame %u: core cheat \"%s\" on (negative control: %u unlocks so far)\n", frame,
                     p.code.c_str(), unlockedBeforePoke_);
        coreCall([&] { retro_cheat_set(pokeIndex_, true, p.code.c_str()); });
        pokeIndex_++;
    }
}

bool Achievements::headlessCheck(const CliArgs& a) {
    bool ok = true;
    auto fail = [&](const std::string& why) {
        std::fprintf(stderr, "headless: FAIL ra: %s\n", why.c_str());
        ok = false;
    };
    // A snapshot of the mock server's records (an award reaches it from the emulation thread when that is on); the
    // lock is not held across the state save / load below.
    std::vector<std::string> mockLog, mockHashes, mockUnhandled;
    {
        std::lock_guard<std::mutex> l(mockMu_);
        mockLog = mockLog_;
        mockHashes = mockHashes_;
        mockUnhandled = mockUnhandled_;
    }
    if (!mockMode()) {
        if (!a.raExpectUnlock.empty() || !a.raExpectNoUnlock.empty() || !a.raExpectRequests.empty() ||
            !a.raExpectHash.empty() || a.raExpectLoadError || a.raExpectHardcore || a.raExpectSoftcore ||
            !a.raPokes.empty() || !a.raSteps.empty() || !a.raExpectNotice.empty())
            fail("--ra-expect-*/--ra-poke given but the mock session is not running (--ra-mock missing or unreadable)");
        return ok;
    }
    for (const std::string& u : mockUnhandled)
        if (u != "ping") fail("the client made a request the fixtures do not answer: r=" + u);
    if (!a.raPokes.empty() && unlockedBeforePoke_ != 0) fail("an achievement fired BEFORE the cheat was applied (the condition is not a real test)");
    for (unsigned id : a.raExpectUnlock)
        if (std::find(unlocked_.begin(), unlocked_.end(), id) == unlocked_.end())
            fail("achievement " + std::to_string(id) + " did not unlock");
    for (unsigned id : a.raExpectNoUnlock)
        if (std::find(unlocked_.begin(), unlocked_.end(), id) != unlocked_.end())
            fail("achievement " + std::to_string(id) + " unlocked but must not have");
    for (const std::string& r : a.raExpectRequests)
        if (std::find(mockLog.begin(), mockLog.end(), r) == mockLog.end()) fail("no " + r + " request was sent");
    if (!a.raExpectHash.empty() &&
        std::find(mockHashes.begin(), mockHashes.end(), a.raExpectHash) == mockHashes.end()) {
        std::string seen;
        for (const std::string& h : mockHashes) seen += " " + h;
        fail("game hash " + a.raExpectHash + " was never sent (sent:" + seen + ")");
    }
    if (a.raExpectLoadError && !loadFailed_) fail("expected the game NOT to be identified, but it was");
    if (!a.raExpectLoadError && loadFailed_ &&
        (!a.raExpectUnlock.empty() || !a.raExpectRequests.empty() || !a.raExpectHash.empty()))
        fail("the game was not identified");

    if (a.raExpectHardcore) {
        // Hardcore must be on and must refuse a state load; saving stays allowed.
        if (!hardcoreActive()) fail("hardcore is not active");
        if (rc_client_get_hardcore_enabled(client_) != 1) fail("rc_client is not in hardcore");
        if (!app_->saveState(0)) fail("saving a state must still work in hardcore");
        if (app_->loadState(0)) fail("a state load was ALLOWED in hardcore");
        if (allowCheats() || allowRewind() || allowSlowMotion() || allowFrameAdvance()) fail("a restricted feature is allowed in hardcore");
    }
    for (const std::string& f : stepFailures_) fail(f);
    for (const Step& s : steps_)
        if (!s.done) fail("step at frame " + std::to_string(s.frame) + " [" + s.action + "] never ran (too few --frames?)");
    for (const std::string& want : a.raExpectNotice) {
        bool seen = false;
        for (const std::string& n : noticeLog_)
            if (n.find(want) != std::string::npos) seen = true;
        if (!seen) fail("no notice containing \"" + want + "\" was shown");
    }
    if (a.raExpectReset >= 0 && (int)hardcoreResets_ != a.raExpectReset)
        fail("expected " + std::to_string(a.raExpectReset) + " hardcore restart(s), saw " + std::to_string(hardcoreResets_));
    if (a.raExpectBlocked) {
        // Hardcore was asked for, but a core option rcheevos disallows must keep it
        // off (and say which one) -- the player gets softcore, not a silent bypass.
        if (!hardcoreWanted_) fail("hardcore was not requested");
        if (hardcoreActive()) fail("hardcore is active despite a disallowed setting");
        if (hardcoreBlocker().find("virtualjaguar_pal") == std::string::npos)
            fail("the blocking setting was not reported (got \"" + hardcoreBlocker() + "\")");
        if (rc_client_get_hardcore_enabled(client_) != 0) fail("rc_client is still in hardcore");
        if (!allowStateLoad()) fail("state loads are refused although hardcore is off");
    }
    if (a.raExpectSoftcore) {
        if (hardcoreActive()) fail("hardcore is unexpectedly active");
        if (!app_->saveState(0)) fail("could not save a state");
        if (!fileExists(app_->statePath(0) + ".rcp")) fail("the achievement-progress sidecar was not written");
        if (!app_->loadState(0)) fail("a state load was refused in softcore");
    }
    if (ok)
        std::printf("ra: OK (%zu unlock(s), game hash %s, %zu request(s): %s)\n", unlocked_.size(),
                    mockHashes.empty() ? "-" : mockHashes.front().c_str(), mockLog.size(),
                    [&] {
                        std::string s;
                        for (const std::string& r : mockLog) s += (s.empty() ? "" : ",") + r;
                        return s;
                    }().c_str());
    return ok;
}

}  // namespace vj
