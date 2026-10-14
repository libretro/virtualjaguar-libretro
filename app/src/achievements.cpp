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
        if (!a || !a->memoryReady_) return 0;
        return rc_libretro_memory_read(&a->regions_, address, buffer, numBytes);
    }

    static void RC_CCONV event(const rc_client_event_t* e, rc_client_t* c) {
        if (Achievements* a = self(c)) a->handleEvent(e);
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
            a->persistLogin();
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
                // A stale token: forget it so the next start does not retry forever.
                a->app_->cfg.erase("ra.token");
                a->app_->saveConfig();
            }
            a->toast("RetroAchievements", "Sign-in failed: " + msg, 2, 7);
        }
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
Achievements::Achievements() { rc_libretro_memory_destroy(&regions_); }
Achievements::~Achievements() { shutdown(); }

void Achievements::init(App& app, const CliArgs& args) {
    app_ = &app;
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
    const std::string token = app.cfg.get("ra.token", "");
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
    rc_libretro_memory_destroy(&regions_);
    memoryReady_ = false;
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
    rebuildMemory();
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

void Achievements::onContentLoaded() {
    if (!client_) return;
    loadedPath_ = app_->host.contentPath();
    ranUnrestricted_ = false;   // a fresh boot
    trackers.clear();
    challenges.clear();
    beginLoad();
    if (hardcoreActive()) retro_cheat_reset();   // cheats do not survive into a hardcore session
}

void Achievements::onContentClosed() {
    loading_ = false;
    gameKnown_ = false;
    memoryReady_ = false;
    rc_libretro_memory_destroy(&regions_);
    trackers.clear();
    challenges.clear();
    progress.clear();
    if (client_) rc_client_unload_game(client_);
    checkHardcoreTransition();
}

void Achievements::onReset() {
    ranUnrestricted_ = false;
    if (client_ && rc_client_is_game_loaded(client_)) rc_client_reset(client_);
}

void Achievements::afterFrame() {
    if (!client_) return;
    checkHardcoreTransition();   // never run an emulated frame under the wrong cheat policy
    if (!hardcoreActive()) ranUnrestricted_ = true;   // this game has run (or loaded states) outside hardcore
    if (mockMode() && app_ && app_->window()) headlessFrame(++mockFrames_);   // pokes in a windowed mock session
    if (rc_client_is_game_loaded(client_) && memoryReady_) rc_client_do_frame(client_);
    else rc_client_idle(client_);
}

void Achievements::pump() {
    if (!client_) return;
    http_.pump();
    mockDeliver();
    updateImageNetwork();
    images.pump();
    if (showList && dashTab == 1) gamesTabShown();
    rc_client_idle(client_);
    if (resetPending_) {
        resetPending_ = false;
        // Turning hardcore on mid-game restarts the game (rc_client's rule: the
        // run must start clean).  retro_cheat_reset: cheats may not outlive it.
        retro_cheat_reset();
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

bool Achievements::hardcoreActive() const {
    if (!client_ || !enabled_ || !hardcoreWanted_ || !settingsOk_) return false;
    if (!signedIn() && !loginPending_) return false;
    return loading_ || gameKnown_;
}

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

void Achievements::applyHardcore() {
    if (!client_) return;
    const int want = (hardcoreWanted_ && settingsOk_) ? 1 : 0;
    if (rc_client_get_hardcore_enabled(client_) != want) rc_client_set_hardcore_enabled(client_, want);
}

bool Achievements::allowPause(std::string& why) {
    if (!hardcoreActive() || !rc_client_is_game_loaded(client_)) return true;
    uint32_t frames = 0;
    if (rc_client_can_pause(client_, &frames)) return true;
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
    if (now) retro_cheat_reset();   // whatever is in the core goes, whoever put it there
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
        retro_cheat_reset();
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
        if (client_) rc_client_unload_game(client_);
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
    if (mockMode()) return;   // a canned token must never reach the real config
    // The TOKEN only: the password is not kept anywhere past the request.
    app_->cfg.set("ra.user", u->username);
    app_->cfg.set("ra.token", u->token);
    app_->saveConfig();
}

void Achievements::logout() {
    if (!client_) return;
    rc_client_logout(client_);   // also unloads the game
    loading_ = false;
    gameKnown_ = false;
    loginPending_ = false;
    trackers.clear();
    challenges.clear();
    dashSignedOut();
    if (app_ && !mockMode()) {   // a mock session never owns the real token
        app_->cfg.erase("ra.token");
        app_->saveConfig();
    }
    setStatus("Signed out.");
    toast("RetroAchievements", "Signed out.", 1, 3);
    checkHardcoreTransition();
}

// ---------------------------------------------------------------------------------
// save states
// ---------------------------------------------------------------------------------
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

void Achievements::handleEvent(const rc_client_event_t* e) {
    switch (e->type) {
    case RC_CLIENT_EVENT_ACHIEVEMENT_TRIGGERED: {
        const rc_client_achievement_t* a = e->achievement;
        unlocked_.push_back(a->id);
        char body[320];
        std::snprintf(body, sizeof body, "%s  (%u pt%s)", a->description ? a->description : "", a->points,
                      a->points == 1 ? "" : "s");
        char badge[256] = "";
        if (a->badge_url) std::snprintf(badge, sizeof badge, "%s", a->badge_url);
        toast(std::string("Achievement unlocked: ") + (a->title ? a->title : ""), body, 0, 7, badge);
        {
            SessionUnlock u;
            u.title = a->title ? a->title : "";
            u.description = a->description ? a->description : "";
            u.badgeUrl = badge;
            u.points = a->points;
            u.hardcore = rc_client_get_hardcore_enabled(client_) != 0;
            u.when = (long long)std::time(nullptr);
            sessionUnlocks.insert(sessionUnlocks.begin(), std::move(u));
            if (sessionUnlocks.size() > 50) sessionUnlocks.pop_back();
        }
        std::fprintf(stderr, "[ra] unlocked id=%u \"%s\" %u points%s\n", a->id, a->title ? a->title : "", a->points,
                     rc_client_get_hardcore_enabled(client_) ? " (hardcore)" : "");
        challenges.erase(a->id);
        break;
    }
    case RC_CLIENT_EVENT_GAME_COMPLETED:
        toast(gameTitle_.empty() ? "Game complete" : gameTitle_,
              rc_client_get_hardcore_enabled(client_) ? "Mastered: every achievement earned in hardcore!"
                                                      : "Completed: every achievement earned!",
              0, 8);
        break;
    case RC_CLIENT_EVENT_SUBSET_COMPLETED:
        toast(e->subset && e->subset->title ? e->subset->title : "Set complete", "Every achievement in this set earned!", 0, 7);
        break;
    case RC_CLIENT_EVENT_LEADERBOARD_STARTED:
        toast("Leaderboard attempt started", e->leaderboard && e->leaderboard->title ? e->leaderboard->title : "", 1, 4);
        break;
    case RC_CLIENT_EVENT_LEADERBOARD_FAILED:
        toast("Leaderboard attempt failed", e->leaderboard && e->leaderboard->title ? e->leaderboard->title : "", 2, 4);
        break;
    case RC_CLIENT_EVENT_LEADERBOARD_SUBMITTED:
        toast("Leaderboard submitted", e->leaderboard && e->leaderboard->title ? e->leaderboard->title : "", 1, 5);
        break;
    case RC_CLIENT_EVENT_LEADERBOARD_SCOREBOARD:
        if (e->leaderboard_scoreboard) {
            char body[160];
            std::snprintf(body, sizeof body, "Score %s  -  rank %u of %u", e->leaderboard_scoreboard->submitted_score,
                          e->leaderboard_scoreboard->new_rank, e->leaderboard_scoreboard->num_entries);
            toast("Leaderboard result", body, 1, 7);
        }
        break;
    case RC_CLIENT_EVENT_LEADERBOARD_TRACKER_SHOW:
    case RC_CLIENT_EVENT_LEADERBOARD_TRACKER_UPDATE:
        if (e->leaderboard_tracker) trackers[e->leaderboard_tracker->id] = e->leaderboard_tracker->display;
        break;
    case RC_CLIENT_EVENT_LEADERBOARD_TRACKER_HIDE:
        if (e->leaderboard_tracker) trackers.erase(e->leaderboard_tracker->id);
        break;
    case RC_CLIENT_EVENT_ACHIEVEMENT_CHALLENGE_INDICATOR_SHOW:
        if (e->achievement) challenges[e->achievement->id] = e->achievement->title ? e->achievement->title : "";
        break;
    case RC_CLIENT_EVENT_ACHIEVEMENT_CHALLENGE_INDICATOR_HIDE:
        if (e->achievement) challenges.erase(e->achievement->id);
        break;
    case RC_CLIENT_EVENT_ACHIEVEMENT_PROGRESS_INDICATOR_SHOW:
    case RC_CLIENT_EVENT_ACHIEVEMENT_PROGRESS_INDICATOR_UPDATE:
        if (e->achievement) {
            progress = std::string(e->achievement->title ? e->achievement->title : "") + ": " +
                       e->achievement->measured_progress;
            progressBorn = app_ ? app_->now() : 0;
        }
        break;
    case RC_CLIENT_EVENT_ACHIEVEMENT_PROGRESS_INDICATOR_HIDE:
        progress.clear();
        break;
    case RC_CLIENT_EVENT_RESET:
        resetPending_ = true;   // acted on in pump(), never from inside rc_client
        break;
    case RC_CLIENT_EVENT_SERVER_ERROR:
        if (e->server_error)
            toast("RetroAchievements server error",
                  std::string(e->server_error->api ? e->server_error->api : "") + ": " +
                      (e->server_error->error_message ? e->server_error->error_message : ""),
                  2, 8);
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
    ready.swap(mockQueue_);   // callbacks may queue the next request
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
        uint8_t* ram = nullptr;
        size_t size = 0;
        if (!parseRam(s.action.substr(poke ? 5 : 7), addr, val)) { fail("bad ADDR=VV"); return; }
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
        retro_cheat_set(pokeIndex_++, true, p.code.c_str());
    }
}

bool Achievements::headlessCheck(const CliArgs& a) {
    bool ok = true;
    auto fail = [&](const std::string& why) {
        std::fprintf(stderr, "headless: FAIL ra: %s\n", why.c_str());
        ok = false;
    };
    if (!mockMode()) {
        if (!a.raExpectUnlock.empty() || !a.raExpectNoUnlock.empty() || !a.raExpectRequests.empty() ||
            !a.raExpectHash.empty() || a.raExpectLoadError || a.raExpectHardcore || a.raExpectSoftcore ||
            !a.raPokes.empty() || !a.raSteps.empty() || !a.raExpectNotice.empty())
            fail("--ra-expect-*/--ra-poke given but the mock session is not running (--ra-mock missing or unreadable)");
        return ok;
    }
    for (const std::string& u : mockUnhandled_)
        if (u != "ping") fail("the client made a request the fixtures do not answer: r=" + u);
    if (!a.raPokes.empty() && unlockedBeforePoke_ != 0) fail("an achievement fired BEFORE the cheat was applied (the condition is not a real test)");
    for (unsigned id : a.raExpectUnlock)
        if (std::find(unlocked_.begin(), unlocked_.end(), id) == unlocked_.end())
            fail("achievement " + std::to_string(id) + " did not unlock");
    for (unsigned id : a.raExpectNoUnlock)
        if (std::find(unlocked_.begin(), unlocked_.end(), id) != unlocked_.end())
            fail("achievement " + std::to_string(id) + " unlocked but must not have");
    for (const std::string& r : a.raExpectRequests)
        if (std::find(mockLog_.begin(), mockLog_.end(), r) == mockLog_.end()) fail("no " + r + " request was sent");
    if (!a.raExpectHash.empty() &&
        std::find(mockHashes_.begin(), mockHashes_.end(), a.raExpectHash) == mockHashes_.end()) {
        std::string seen;
        for (const std::string& h : mockHashes_) seen += " " + h;
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
                    mockHashes_.empty() ? "-" : mockHashes_.front().c_str(), mockLog_.size(),
                    [&] {
                        std::string s;
                        for (const std::string& r : mockLog_) s += (s.empty() ? "" : ",") + r;
                        return s;
                    }().c_str());
    return ok;
}

}  // namespace vj
