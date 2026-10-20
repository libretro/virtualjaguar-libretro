// The achievements dashboard's data (#836): picture URLs, the loaded game's rows, the list of Jaguar
// and Jaguar CD games that have achievements, the signed-in user's progress in them, and the match
// against the local library.  Pure logic is in ra_dash.cpp; this is the part that talks to rc_client.
//
// rc_client APIs used (rcheevos 12.5, no web API key anywhere):
//   rc_client_begin_fetch_game_list(console)          r=systemgames: every game with a set, plus the MD5s it accepts
//   rc_client_begin_fetch_all_user_progress(console)  r=allprogress: this user's unlock counts, one request per console
//   rc_client_game_get_image_url / rc_client_user_get_image_url / rc_client_achievement_t::badge_url
#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <ctime>

#include "achievements.h"
#include "app.h"
#include "rc_client.h"
#include "rc_consoles.h"
#include "rc_error.h"
#include "rc_hash.h"

namespace vj {

struct AchGamesCallbacks {
    static Achievements* self(rc_client_t* c) { return (Achievements*)rc_client_get_userdata(c); }

    static void RC_CCONV gameList(int result, const char* error, rc_client_game_list_t* list, rc_client_t* c, void* ud) {
        Achievements* a = self(c);
        if (!a) {
            if (list) rc_client_destroy_game_list(list);
            return;
        }
        const uint32_t console = (uint32_t)(uintptr_t)ud;
        if (result == RC_OK && list) {
            std::vector<ra::GameEntry> v;
            for (uint32_t i = 0; i < list->num_entries; i++) v.push_back(ra::gameFromEntry(list->entries[i], console));
            for (ra::GameEntry& g : v)
                for (std::string& h : g.hashes)
                    for (char& ch : h) ch = (char)std::tolower((unsigned char)ch);
            a->gamesIncoming_[console] = std::move(v);
            a->gamesIncomingOk_[console] = true;
        } else if (result == RC_ABORTED) {
            a->gamesAborted_ = true;
        } else {
            a->gamesIncomingOk_[console] = false;
            a->gamesError_ = error && *error ? error : "the games list could not be fetched";
        }
        if (list) rc_client_destroy_game_list(list);
        if (--a->gamesPending_ <= 0) {
            a->gamesPending_ = 0;
            a->finishGames();
        }
    }

    static void RC_CCONV allProgress(int result, const char* error, rc_client_all_user_progress_t* list, rc_client_t* c,
                                     void* ud) {
        Achievements* a = self(c);
        if (!a) {
            if (list) rc_client_destroy_all_user_progress(list);
            return;
        }
        if (result == RC_OK && list) {
            for (uint32_t i = 0; i < list->num_entries; i++) {
                ra::Progress p;
                p.achievements = list->entries[i].num_achievements;
                p.unlocked = list->entries[i].num_unlocked_achievements;
                p.unlockedHardcore = list->entries[i].num_unlocked_achievements_hardcore;
                a->progress_[list->entries[i].game_id] = p;
            }
            a->progressOk_ = true;
        } else if (result != RC_ABORTED && a->gamesError_.empty()) {
            a->gamesError_ = std::string("progress: ") + (error && *error ? error : "could not be fetched");
        }
        if (list) rc_client_destroy_all_user_progress(list);
        if (--a->progressPending_ <= 0) {
            a->progressPending_ = 0;
            a->finishProgress();
        }
    }
};

// ---- picture URLs -------------------------------------------------------------------------

std::string Achievements::gameIconUrl() const {
    std::lock_guard<std::recursive_mutex> lock(clientMu_);
    const rc_client_game_t* g = client_ ? rc_client_get_game_info(client_) : nullptr;
    if (!g || g->id == 0) return "";
    char buf[256];
    return rc_client_game_get_image_url(g, buf, sizeof buf) == RC_OK ? buf : "";
}

std::string Achievements::avatarUrl(std::string& version) const {
    std::lock_guard<std::recursive_mutex> lock(clientMu_);
    version.clear();
    const rc_client_user_t* u = client_ ? rc_client_get_user_info(client_) : nullptr;
    if (!u) return "";
    char buf[256];
    if (rc_client_user_get_image_url(u, buf, sizeof buf) != RC_OK) return "";
    if (u->avatar_last_updated) version = std::to_string((long long)u->avatar_last_updated);
    return buf;
}

bool Achievements::achievementRows(std::vector<ra::AchRow>& rows) const {
    std::lock_guard<std::recursive_mutex> lock(clientMu_);
    rows.clear();
    rc_client_t* c = client_;
    const rc_client_game_t* g = c ? rc_client_get_game_info(c) : nullptr;
    if (!c || !rc_client_is_game_loaded(c) || !g || g->id == 0) return false;
    rc_client_achievement_list_t* list = rc_client_create_achievement_list(
        c, RC_CLIENT_ACHIEVEMENT_CATEGORY_CORE_AND_UNOFFICIAL, RC_CLIENT_ACHIEVEMENT_LIST_GROUPING_LOCK_STATE);
    if (!list) return true;
    for (uint32_t b = 0; b < list->num_buckets; b++) {
        const rc_client_achievement_bucket_t& bk = list->buckets[b];
        for (uint32_t i = 0; i < bk.num_achievements; i++) {
            const rc_client_achievement_t* ac = bk.achievements[i];
            ra::AchRow r;
            r.id = ac->id;
            r.title = ac->title ? ac->title : "";
            r.description = ac->description ? ac->description : "";
            r.points = ac->points;
            r.unlockedSoft = (ac->unlocked & RC_CLIENT_ACHIEVEMENT_UNLOCKED_SOFTCORE) != 0;
            r.unlockedHard = (ac->unlocked & RC_CLIENT_ACHIEVEMENT_UNLOCKED_HARDCORE) != 0;
            r.unlockTime = (int64_t)ac->unlock_time;
            r.bucket = bk.bucket_type;
            r.unsupported = bk.bucket_type == RC_CLIENT_ACHIEVEMENT_BUCKET_UNSUPPORTED ||
                            ac->state == RC_CLIENT_ACHIEVEMENT_STATE_DISABLED;
            r.unofficial = bk.bucket_type == RC_CLIENT_ACHIEVEMENT_BUCKET_UNOFFICIAL ||
                           ac->category == RC_CLIENT_ACHIEVEMENT_CATEGORY_UNOFFICIAL;
            r.rarity = ac->rarity;
            r.type = ac->type;
            r.progress = ac->measured_progress;
            if (ac->badge_url) r.badgeUrl = ac->badge_url;
            if (ac->badge_locked_url) r.badgeLockedUrl = ac->badge_locked_url;
            r.order = rows.size();
            rows.push_back(std::move(r));
        }
    }
    rc_client_destroy_achievement_list(list);
    return true;
}

void Achievements::updateImageNetwork() {
    // Mock sessions never reach the network; "download art" off (Library) means offline; the HTTP backend
    // may be missing.  Pictures already on disk are served in every case.
    const bool net = !mockMode() && imagesWanted_ && AchHttp::available() && app_ && app_->library.downloadArt.load();
    images.setNetwork(net);
}

// ---- the games list ------------------------------------------------------------------------------

void Achievements::loadGamesCache() {
    if (mockMode() || !app_) return;   // a mock session must neither read nor write the real cache
    std::vector<ra::GameEntry> all;
    int64_t oldest = 0;
    bool both = true;
    for (uint32_t console : {ra::kConsoleJaguar, ra::kConsoleJaguarCD}) {
        size_t n = 0;
        void* data = SDL_LoadFile(ra::gamesCachePath(app_->paths.root, console).c_str(), &n);
        std::vector<ra::GameEntry> v;
        int64_t at = 0;
        const bool ok = data && ra::deserializeGames(std::string((const char*)data, n), v, at);
        if (data) SDL_free(data);
        if (!ok) {
            both = false;
            continue;
        }
        for (ra::GameEntry& g : v) g.console = console;
        all.insert(all.end(), v.begin(), v.end());
        oldest = oldest == 0 ? at : std::min(oldest, at);
    }
    if (all.empty()) return;
    games_ = std::move(all);
    gamesFetchedAt_ = both ? oldest : 0;   // half a cache is stale by definition
    gamesFromCache_ = true;
}

void Achievements::refreshGames(bool manual) {
    if (!client_ || gamesPending_ > 0 || progressPending_ > 0) return;
    if (!AchHttp::available() && !mockMode()) {
        gamesError_ = "This build has no HTTP support, so the games list cannot be fetched.";
        return;
    }
    gamesError_.clear();
    gamesAborted_ = false;
    gamesIncoming_.clear();
    gamesIncomingOk_.clear();
    gamesAutoTried_ = true;
    gamesPending_ = 2;
    coreCall([&] {
        for (uint32_t console : {ra::kConsoleJaguar, ra::kConsoleJaguarCD})
            rc_client_begin_fetch_game_list(client_, console, &AchGamesCallbacks::gameList, (void*)(uintptr_t)console);
    });
    if (signedIn()) {
        progressAutoTried_ = true;
        progressOk_ = false;
        progress_.clear();
        progressPending_ = 2;
        coreCall([&] {
            for (uint32_t console : {ra::kConsoleJaguar, ra::kConsoleJaguarCD})
                rc_client_begin_fetch_all_user_progress(client_, console, &AchGamesCallbacks::allProgress, nullptr);
        });
    }
    (void)manual;
}

void Achievements::finishGames() {
    if (gamesAborted_) return;
    std::vector<ra::GameEntry> merged;
    bool allOk = true, anyOk = false;
    for (uint32_t console : {ra::kConsoleJaguar, ra::kConsoleJaguarCD}) {
        auto ok = gamesIncomingOk_.find(console);
        if (ok != gamesIncomingOk_.end() && ok->second) {
            anyOk = true;
            std::vector<ra::GameEntry>& v = gamesIncoming_[console];
            merged.insert(merged.end(), v.begin(), v.end());
        } else {
            allOk = false;   // keep what we had for this console
            for (const ra::GameEntry& g : games_)
                if (g.console == console) merged.push_back(g);
        }
    }
    if (!anyOk) return;   // gamesError_ says why; the old list (cache) stays on screen
    games_ = std::move(merged);
    gamesFromCache_ = false;
    if (allOk) {
        gamesFetchedAt_ = (int64_t)std::time(nullptr);
        gamesError_.clear();
    }
    if (progressKnown_) ra::applyProgress(games_, progress_);
    libraryMatched_ = (size_t)-1;   // re-match on the next frame
    if (!mockMode() && app_) {
        for (uint32_t console : {ra::kConsoleJaguar, ra::kConsoleJaguarCD}) {
            auto ok = gamesIncomingOk_.find(console);
            if (ok == gamesIncomingOk_.end() || !ok->second) continue;
            const std::string text = ra::serializeGames(gamesIncoming_[console], gamesFetchedAt_ ? gamesFetchedAt_ : (int64_t)std::time(nullptr));
            SDL_CreateDirectory((app_->paths.root + "ra-cache").c_str());
            writeFileAtomic(ra::gamesCachePath(app_->paths.root, console), text.data(), text.size());
        }
    }
}

void Achievements::finishProgress() {
    if (!progressOk_) return;
    progressKnown_ = true;
    ra::applyProgress(games_, progress_);
}

void Achievements::dashSignedOut() {
    progress_.clear();
    progressKnown_ = false;
    progressOk_ = false;
    progressAutoTried_ = false;
    sessionUnlocks.clear();
    for (ra::GameEntry& g : games_) {
        g.haveProgress = false;
        g.unlocked = g.unlockedHardcore = 0;
    }
}

void Achievements::gamesTabShown() {
    if (!client_ || !app_) return;
    if (!gamesInit_) {
        gamesInit_ = true;
        loadGamesCache();
    }
    if (libraryMatched_ != app_->library.entries().size() || hashGenMatched_ != hashGen_.load()) rematchLibrary();
    // Automatic fetches obey the downloads switch; the Refresh button is the player's explicit say-so.
    const bool autoOk = mockMode() || (app_->library.downloadArt.load() && AchHttp::available());
    if (gamesBusy() || !autoOk) return;
    if (!gamesAutoTried_ && (games_.empty() || ra::gamesStale(gamesFetchedAt_, (int64_t)std::time(nullptr)))) {
        refreshGames(false);
    } else if (signedIn() && !progressAutoTried_ && !progressKnown_) {
        // Games are fresh but this session has not asked for the user's progress yet.
        progressAutoTried_ = true;
        progressOk_ = false;
        progress_.clear();
        progressPending_ = 2;
        coreCall([&] {
            for (uint32_t console : {ra::kConsoleJaguar, ra::kConsoleJaguarCD})
                rc_client_begin_fetch_all_user_progress(client_, console, &AchGamesCallbacks::allProgress, nullptr);
        });
    }
}

// ---- library ----------------------------------------------------------------------------------------

void Achievements::startHashing() {
    if (hashing_ || !app_) return;
    std::vector<std::string> todo;
    {
        std::lock_guard<std::mutex> g(hashMu_);
        for (const lib::Entry& e : app_->library.entries())
            if (e.section == lib::Section::Cart && !cartMd5_.count(e.file.path)) todo.push_back(e.file.path);
    }
    if (todo.empty()) return;
    if (hashThread_.joinable()) hashThread_.join();
    hashing_ = true;
    hashStop_ = false;
    hashThread_ = std::thread([this, todo] {
        unsigned n = 0;
        for (const std::string& path : todo) {
            if (hashStop_) break;
            std::string md5;
            SDL_PathInfo info;
            if (SDL_GetPathInfo(path.c_str(), &info) && info.type == SDL_PATHTYPE_FILE && info.size <= (16u << 20)) {
                char hash[33] = "";
                if (rc_hash_generate_from_file(hash, RC_CONSOLE_ATARI_JAGUAR, path.c_str())) md5 = hash;
            }
            {
                std::lock_guard<std::mutex> g(hashMu_);
                cartMd5_[path] = md5;
            }
            if (++n % 8 == 0) hashGen_++;
        }
        hashGen_++;
        hashing_ = false;
    });
}

void Achievements::rematchLibrary() {
    if (!app_) return;
    const std::vector<lib::Entry>& es = app_->library.entries();
    if (libraryMatched_ != es.size()) startHashing();
    libraryMatched_ = es.size();
    hashGenMatched_ = hashGen_.load();
    std::vector<ra::LibItem> items;
    {
        std::lock_guard<std::mutex> g(hashMu_);
        for (size_t i = 0; i < es.size(); i++) {
            const lib::Entry& e = es[i];
            if (e.section == lib::Section::Bios) continue;
            ra::LibItem it;
            it.index = (int)i;
            it.disc = e.section == lib::Section::Disc;
            it.title = e.title;
            it.datName = e.datName;
            auto m = cartMd5_.find(e.file.path);
            if (m != cartMd5_.end()) it.md5 = m->second;
            items.push_back(std::move(it));
        }
    }
    ra::matchLibrary(games_, items);
}

void Achievements::playGame(const ra::GameEntry& g) {
    if (!app_ || g.libIndex < 0) return;
    const std::vector<lib::Entry>& es = app_->library.entries();
    if ((size_t)g.libIndex >= es.size()) return;
    app_->library.launch(es[(size_t)g.libIndex]);
}

// ---- headless steps (--ra-step FRAME:games:...) --------------------------------------------------------

void Achievements::headlessGamesStep(const std::string& action, unsigned frame) {
    auto fail = [&](const std::string& why) {
        stepFailures_.push_back("frame " + std::to_string(frame) + " [games:" + action + "]: " + why);
    };
    if (action == "refresh") {
        refreshGames(true);
        return;
    }
    // expect:TOTAL:WITH_PROGRESS -- the list arrived and the user's progress was applied.
    unsigned total = 0, started = 0;
    if (std::sscanf(action.c_str(), "expect:%u:%u", &total, &started) == 2) {
        unsigned got = 0, haveStarted = 0;
        for (const ra::GameEntry& g : games_) {
            got++;
            if (g.haveProgress && g.unlocked > 0) haveStarted++;
        }
        if (gamesBusy()) fail("the requests are still in flight");
        else if (got != total) fail("expected " + std::to_string(total) + " games, have " + std::to_string(got) + " (" + gamesError_ + ")");
        else if (haveStarted != started)
            fail("expected " + std::to_string(started) + " games with unlocks, have " + std::to_string(haveStarted));
        else std::printf("ra-step: frame %u games %u, with progress %u ok\n", frame, got, haveStarted);
        return;
    }
    fail("unknown games step");
}

}  // namespace vj
