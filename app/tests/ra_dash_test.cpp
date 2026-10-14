// Unit tests for the RetroAchievements dashboard (#836): src/ra_dash.cpp (games list parsing, library
// matching, the daily cache file, filtering and sorting) and src/ra_images.cpp (cache paths, the disk
// cache, offline mode, the bounded texture LRU).  No window, no GL, no internet: the "server" is a
// function that returns PNG bytes, the textures are counters, and the JSON is the same fixture set the
// headless --ra-mock tests answer with (app/tests/ra).
#include <SDL3/SDL.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <thread>

#include "paths.h"
#include "ra_dash.h"
#include "ra_images.h"

#ifndef VJ_RA_FIX
#error "VJ_RA_FIX (the fixture directory) must be defined"
#endif

using namespace vj;

static int g_fail = 0, g_checks = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        g_checks++;                                                              \
        if (!(cond)) {                                                           \
            g_fail++;                                                            \
            std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
        }                                                                        \
    } while (0)

static std::string readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// A small valid PNG (solid colour), made with SDL itself.
static std::string makePng(int w, int h, unsigned rgba) {
    SDL_Surface* s = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_RGBA32);
    SDL_FillSurfaceRect(s, nullptr, SDL_MapSurfaceRGBA(s, rgba >> 24, (rgba >> 16) & 255, (rgba >> 8) & 255, rgba & 255));
    SDL_IOStream* io = SDL_IOFromDynamicMem();
    SDL_SavePNG_IO(s, io, false);
    void* mem = SDL_GetPointerProperty(SDL_GetIOProperties(io), SDL_PROP_IOSTREAM_DYNAMIC_MEMORY_POINTER, nullptr);
    const Sint64 n = SDL_GetIOSize(io);
    std::string out((const char*)mem, (size_t)n);
    SDL_CloseIO(io);
    SDL_DestroySurface(s);
    return out;
}

// ---- pure logic ---------------------------------------------------------------------------

static void testPaths() {
    const std::string root = "/data/";
    const std::string p = ra::imageCachePath(root, "https://media.retroachievements.org/Badge/12345.png");
    CHECK(p == "/data/ra-cache/media.retroachievements.org_Badge_12345.png");
    // locked variant is a different file
    CHECK(ra::imageCachePath(root, "https://media.retroachievements.org/Badge/12345_lock.png") != p);
    // the avatar's version is part of the name
    const std::string a1 = ra::imageCachePath(root, "https://media.retroachievements.org/UserPic/Bob.png", "1700000000");
    const std::string a2 = ra::imageCachePath(root, "https://media.retroachievements.org/UserPic/Bob.png", "1700000001");
    CHECK(a1 != a2 && a1.find("_v1700000000.png") != std::string::npos);
    // nothing escapes ra-cache
    const std::string evil = ra::imageCachePath(root, "https://x.example/../../etc/passwd");
    CHECK(evil.compare(0, 14, "/data/ra-cache") == 0);
    CHECK(evil.substr(evil.find("ra-cache/") + 9).find('/') == std::string::npos);
    CHECK(ra::imageCachePath(root, "https://x.example/%2e%2e/a").find('%') == std::string::npos);
    // a query changes the name; schemes other than http(s) and empty paths are refused
    CHECK(ra::imageCachePath(root, "https://h/a.png?v=1") != ra::imageCachePath(root, "https://h/a.png?v=2"));
    CHECK(ra::imageCachePath(root, "file:///etc/passwd").empty());
    CHECK(ra::imageCachePath(root, "ftp://h/a.png").empty());
    CHECK(ra::imageCachePath(root, "https://h/").empty());
    CHECK(ra::imageCachePath(root, "https://h").empty());
    CHECK(ra::imageCachePath(root, "").empty());
    // very long URL: bounded name
    const std::string longUrl = "https://h/" + std::string(600, 'a') + ".png";
    CHECK(ra::imageCachePath(root, longUrl).size() < 200);
    CHECK(ra::imageCachePath(root, longUrl) != ra::imageCachePath(root, "https://h/" + std::string(600, 'a') + "b.png"));
    CHECK(ra::gamesCachePath(root, 17) == "/data/ra-cache/games-17.tsv");
}

static void testGamesParsing() {
    std::vector<ra::GameEntry> g17, g77;
    std::string err;
    CHECK(ra::parseGamesJson(readFile(std::string(VJ_RA_FIX) + "/systemgames_17.json"), 17, g17, err));
    CHECK(ra::parseGamesJson(readFile(std::string(VJ_RA_FIX) + "/systemgames_77.json"), 77, g77, err));
    CHECK(g17.size() == 4 && g77.size() == 2);
    CHECK(g17[1].id == 5001 && g17[1].title == "Alien vs Predator" && g17[1].achievements == 40 && g17[1].leaderboards == 2 &&
          g17[1].points == 600 && g17[1].console == 17);
    CHECK(g17[1].hashes.size() == 2 && g17[0].hashes[0] == std::string(32, 'a'));   // lower-cased
    CHECK(g17[1].imageUrl == "https://media.retroachievements.org/Images/000010.png");
    CHECK(g77[0].console == 77 && g77[0].title == "Myst");
    // a failure reply and garbage
    std::vector<ra::GameEntry> bad;
    const bool failed = !ra::parseGamesJson("{\"Success\":false,\"Error\":\"nope\"}", 17, bad, err);
    if (!(failed && err == "nope" && bad.empty())) std::fprintf(stderr, "games failure reply: failed=%d err='%s'\n", failed, err.c_str());
    CHECK(failed && err == "nope" && bad.empty());
    CHECK(!ra::parseGamesJson("not json", 17, bad, err));
    CHECK(ra::parseGamesJson("{\"Success\":true,\"Response\":[]}", 17, bad, err) && bad.empty());

    std::map<uint32_t, ra::Progress> p17, p77;
    CHECK(ra::parseProgressJson(readFile(std::string(VJ_RA_FIX) + "/allprogress_17.json"), p17, err));
    CHECK(ra::parseProgressJson(readFile(std::string(VJ_RA_FIX) + "/allprogress_77.json"), p77, err));
    CHECK(p17.size() == 3 && p17[9001].unlocked == 2 && p17[9001].unlockedHardcore == 1 && p17[5001].achievements == 40);
    CHECK(p77.size() == 1 && p77[6001].unlocked == 5);
    std::map<uint32_t, ra::Progress> none;
    CHECK(!ra::parseProgressJson("{\"Success\":false,\"Error\":\"Credentials invalid\"}", none, err) && err == "Credentials invalid");

    std::vector<ra::GameEntry> all = g17;
    all.insert(all.end(), g77.begin(), g77.end());
    p17.insert(p77.begin(), p77.end());
    ra::applyProgress(all, p17);
    CHECK(all[0].haveProgress && all[0].unlocked == 2 && all[0].unlockedHardcore == 1);
    CHECK(all[1].unlocked == 40);
    CHECK(all[2].haveProgress && all[2].unlocked == 0);   // no row = nothing earned yet
    CHECK(all[4].unlocked == 5 && all[4].unlockedHardcore == 0);
    // hardcore without a softcore count still counts as unlocked
    std::map<uint32_t, ra::Progress> odd;
    odd[9001].unlockedHardcore = 3;
    odd[9001].achievements = 4;
    ra::applyProgress(all, odd);
    CHECK(all[0].unlocked == 3);
}

static std::vector<ra::GameEntry> loadAll() {
    std::vector<ra::GameEntry> g17, g77;
    std::string err;
    ra::parseGamesJson(readFile(std::string(VJ_RA_FIX) + "/systemgames_17.json"), 17, g17, err);
    ra::parseGamesJson(readFile(std::string(VJ_RA_FIX) + "/systemgames_77.json"), 77, g77, err);
    g17.insert(g17.end(), g77.begin(), g77.end());
    return g17;
}

static void testMatching() {
    CHECK(ra::normalizeTitle("~Homebrew~ Tower Toppler") == ra::normalizeTitle("Tower Toppler (World) (Homebrew)"));
    CHECK(ra::normalizeTitle("Tempest 2000") == ra::normalizeTitle("Tempest 2000 (World)"));
    CHECK(ra::normalizeTitle("The Brain Dead 13") == ra::normalizeTitle("Brain Dead 13, The"));
    CHECK(ra::normalizeTitle("Rock & Roll Racing") == ra::normalizeTitle("Rock and Roll Racing (USA)"));
    CHECK(ra::normalizeTitle("Alien vs Predator") != ra::normalizeTitle("Alien vs Predator 2"));

    std::vector<ra::GameEntry> games = loadAll();
    std::vector<ra::LibItem> items;
    auto add = [&](int idx, bool disc, const char* title, const char* md5 = "") {
        ra::LibItem it;
        it.index = idx;
        it.disc = disc;
        it.title = title;
        it.datName = title;
        it.md5 = md5;
        items.push_back(it);
    };
    // 0: AvP by hash only (title is unrelated), 1: Tower Toppler by title, 2: a disc called "Doom" (must not
    // match the cartridge Doom), 3: Myst disc by title, 4: unrelated, 5: "Alien vs Predator" by title too
    // (the hash match must win, not the first by title).
    add(0, false, "Some Renamed File", std::string(32, 'B').c_str());
    add(1, false, "Tower Toppler (World) (Homebrew)");
    add(2, true, "Doom");
    add(3, true, "Myst (USA) (Disc 1)");
    add(4, false, "Checkered Flag");
    add(5, false, "Alien vs Predator (World)");
    const size_t n = ra::matchLibrary(games, items);
    CHECK(n == 3);
    CHECK(games[1].libIndex == 0 && games[1].matchedByHash);      // AvP: hash beats title
    CHECK(games[2].libIndex == 1 && !games[2].matchedByHash);     // Tower Toppler: title
    CHECK(games[3].libIndex == -1);                               // cartridge Doom: only a disc named Doom exists
    CHECK(games[4].libIndex == 3);                                // Myst (disc)
    CHECK(games[0].libIndex == -1 && games[5].libIndex == -1);
    // matching again from scratch resets earlier marks
    items.clear();
    CHECK(ra::matchLibrary(games, items) == 0 && games[1].libIndex == -1);
    // a title that fits two library files goes to the first
    add(7, false, "Doom");
    add(8, false, "Doom (Rev 1)");
    ra::matchLibrary(games, items);
    CHECK(games[3].libIndex == 7);
}

static void testCacheFile() {
    std::vector<ra::GameEntry> games = loadAll();
    games[0].title = "Tab\tand\nnewline";   // must not break the format
    const std::string text = ra::serializeGames(games, 1700000000);
    std::vector<ra::GameEntry> back;
    int64_t at = 0;
    CHECK(ra::deserializeGames(text, back, at));
    CHECK(at == 1700000000 && back.size() == games.size());
    CHECK(back[0].title == "Tab and newline" && back[1].title == "Alien vs Predator");
    CHECK(back[1].hashes == games[1].hashes && back[1].points == 600 && back[1].leaderboards == 2);
    CHECK(back[5].console == 77 && back[5].imageUrl == games[5].imageUrl);
    // not our file / truncated / damaged lines
    CHECK(!ra::deserializeGames("", back, at));
    CHECK(!ra::deserializeGames("hello\n", back, at));
    CHECK(!ra::deserializeGames("VJRA2\t5\n", back, at));
    CHECK(!ra::deserializeGames(text.substr(0, text.find('\n') + 12), back, at) && back.empty());   // cut inside the first row
    CHECK(ra::deserializeGames("VJRA1\t5\n", back, at) && back.empty() && at == 5);   // a valid empty list
    // daily refresh
    CHECK(ra::gamesStale(0, 1000));
    CHECK(!ra::gamesStale(1000, 1000 + 3600));
    CHECK(ra::gamesStale(1000, 1000 + 24 * 3600));
    CHECK(ra::gamesStale(5000, 1000));   // the clock went backwards
}

static void testGameViews() {
    std::vector<ra::GameEntry> games = loadAll();
    std::map<uint32_t, ra::Progress> p;
    p[9001] = ra::Progress{4, 4, 2};      // completed
    p[5001] = ra::Progress{40, 10, 0};    // in progress
    p[6001] = ra::Progress{20, 0, 0};     // not started
    ra::applyProgress(games, p);
    games[2].libIndex = 0;                // Tower Toppler owned
    auto titles = [&](const std::vector<size_t>& v) {
        std::string s;
        for (size_t i : v) s += (s.empty() ? "" : "|") + games[i].title;
        return s;
    };
    CHECK(ra::viewGames(games, ra::GameFilter::All, ra::GameSort::Title, "").size() == 6);
    CHECK(titles(ra::viewGames(games, ra::GameFilter::All, ra::GameSort::Title, "")) ==
          "Alien vs Predator|Doom|Highlander: The Last of the MacLeods|Mock Game (yarc)|Myst|~Homebrew~ Tower Toppler");
    CHECK(titles(ra::viewGames(games, ra::GameFilter::Owned, ra::GameSort::Title, "")) == "~Homebrew~ Tower Toppler");
    CHECK(titles(ra::viewGames(games, ra::GameFilter::Completed, ra::GameSort::Title, "")) == "Mock Game (yarc)");
    CHECK(titles(ra::viewGames(games, ra::GameFilter::InProgress, ra::GameSort::Title, "")) == "Alien vs Predator");
    // Doom has progress "0 of 30" only if a row exists; here it has none, but applyProgress gave every game a row of 0
    CHECK(ra::viewGames(games, ra::GameFilter::NotStarted, ra::GameSort::Title, "").size() == 4);
    CHECK(titles(ra::viewGames(games, ra::GameFilter::All, ra::GameSort::Title, "MYST")) == "Myst");
    CHECK(ra::viewGames(games, ra::GameFilter::All, ra::GameSort::Title, "zzz").empty());
    CHECK(titles(ra::viewGames(games, ra::GameFilter::All, ra::GameSort::Points, "")).compare(0, 17, "Alien vs Predator") == 0);
    CHECK(titles(ra::viewGames(games, ra::GameFilter::All, ra::GameSort::Achievements, "")).compare(0, 17, "Alien vs Predator") == 0);
    CHECK(titles(ra::viewGames(games, ra::GameFilter::All, ra::GameSort::Progress, "")).compare(0, 16, "Mock Game (yarc)") == 0);
    CHECK(titles(ra::viewGames(games, ra::GameFilter::All, ra::GameSort::OwnedFirst, "")).compare(0, 24, "~Homebrew~ Tower Toppler") == 0);
    // without progress (signed out): progress filters match nothing, All still works
    for (ra::GameEntry& g : games) g.haveProgress = false;
    CHECK(ra::viewGames(games, ra::GameFilter::NotStarted, ra::GameSort::Title, "").empty());
    CHECK(ra::viewGames(games, ra::GameFilter::Completed, ra::GameSort::Title, "").empty());
    CHECK(ra::viewGames(games, ra::GameFilter::All, ra::GameSort::Progress, "").size() == 6);
}

static void testAchievementViews() {
    std::vector<ra::AchRow> rows(5);
    auto set = [&](int i, const char* title, const char* desc, unsigned pts, bool soft, bool hard, long long when, float rarity, int type) {
        rows[i].id = 100 + i;
        rows[i].title = title;
        rows[i].description = desc;
        rows[i].points = pts;
        rows[i].unlockedSoft = soft || hard;
        rows[i].unlockedHard = hard;
        rows[i].unlockTime = when;
        rows[i].rarity = rarity;
        rows[i].type = type;
        rows[i].order = (size_t)i;
    };
    set(0, "Zeta", "Finish level 1", 5, false, false, 0, 40.0f, 2);
    set(1, "alpha", "Collect 100 rings", 25, true, false, 1000, 10.0f, 0);
    set(2, "Mid", "Beat the boss", 10, false, true, 3000, 0.0f, 3);
    set(3, "Beta", "Secret room", 50, false, false, 0, 2.5f, 1);
    set(4, "Gamma", "Speedrun", 10, true, false, 2000, 20.0f, 0);
    auto ids = [&](const std::vector<size_t>& v) {
        std::string s;
        for (size_t i : v) s += (s.empty() ? "" : ",") + std::to_string(rows[i].id);
        return s;
    };
    CHECK(ids(ra::viewAchievements(rows, ra::AchFilter::All, ra::AchSort::Default, "")) == "100,101,102,103,104");
    CHECK(ids(ra::viewAchievements(rows, ra::AchFilter::Unlocked, ra::AchSort::Default, "")) == "101,102,104");
    CHECK(ids(ra::viewAchievements(rows, ra::AchFilter::Locked, ra::AchSort::Default, "")) == "100,103");
    CHECK(ids(ra::viewAchievements(rows, ra::AchFilter::Hardcore, ra::AchSort::Default, "")) == "102");
    CHECK(ids(ra::viewAchievements(rows, ra::AchFilter::Missable, ra::AchSort::Default, "")) == "103");
    CHECK(ids(ra::viewAchievements(rows, ra::AchFilter::All, ra::AchSort::Title, "")) == "101,103,104,102,100");   // case-insensitive
    CHECK(ids(ra::viewAchievements(rows, ra::AchFilter::All, ra::AchSort::Points, "")) == "103,101,102,104,100");   // ties keep list order
    CHECK(ids(ra::viewAchievements(rows, ra::AchFilter::All, ra::AchSort::Rarity, "")) == "103,101,104,100,102");    // unknown rarity last
    CHECK(ids(ra::viewAchievements(rows, ra::AchFilter::All, ra::AchSort::Recent, "")) == "102,104,101,100,103");    // newest first, locked after
    CHECK(ids(ra::viewAchievements(rows, ra::AchFilter::All, ra::AchSort::Type, "")) == "102,100,103,101,104");
    CHECK(ids(ra::viewAchievements(rows, ra::AchFilter::All, ra::AchSort::Default, "RINGS")) == "101");   // searches the description too
    CHECK(ids(ra::viewAchievements(rows, ra::AchFilter::Locked, ra::AchSort::Default, "level")) == "100");
    CHECK(ra::viewAchievements(rows, ra::AchFilter::Unlocked, ra::AchSort::Default, "level").empty());
    CHECK(ra::viewAchievements({}, ra::AchFilter::All, ra::AchSort::Title, "").empty());
}

// ---- the image cache -----------------------------------------------------------------------------

struct Gl {
    unsigned next = 1;
    std::set<unsigned> live;
    unsigned created = 0, destroyed = 0;
    void bind(ra::ImageCache& c) {
        c.createTexture = [this](const void*, int, int) {
            created++;
            live.insert(next);
            return next++;
        };
        c.destroyTexture = [this](unsigned t) {
            destroyed++;
            live.erase(t);
        };
    }
};

static bool settle(ra::ImageCache& c, int ms = 5000) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < end) {
        c.pump();
        if (c.pending() == 0) {
            c.pump();   // collect what the last job just queued
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
}

static ra::ImageView ask(ra::ImageCache& c, const std::string& url) {
    c.get(url);
    settle(c);
    return c.get(url);
}

// SDL_RemovePath only removes empty directories: empty `dir` of files first.
static void wipe(const std::string& dir) {
    SDL_EnumerateDirectory(
        dir.c_str(),
        [](void* ud, const char* d, const char* name) -> SDL_EnumerationResult {
            SDL_RemovePath((std::string(d) + name).c_str());
            return SDL_ENUM_CONTINUE;
        },
        nullptr);
    SDL_RemovePath(dir.c_str());
}

static void testImageCache() {
    const std::string root = std::string(SDL_GetBasePath() ? SDL_GetBasePath() : "") + "ra_dash_test_data/";
    wipe(root + "ra-cache");
    wipe(root + "prune");
    SDL_CreateDirectory(root.c_str());
    const std::string png = makePng(64, 64, 0xff8000ff);
    CHECK(ra::looksLikePng(png.data(), png.size()));
    CHECK(!ra::looksLikePng("<html>404 not found</html> and some more padding text", 53));

    const std::string urlA = "https://media.retroachievements.org/Badge/1.png";
    const std::string urlB = "https://media.retroachievements.org/Badge/2.png";
    const std::string urlBad = "https://media.retroachievements.org/Badge/html.png";
    const std::string urlGone = "https://media.retroachievements.org/Badge/404.png";
    std::set<std::string> asked;
    auto fakeFetch = [&](const std::string& url, std::string& body, std::string& err) {
        asked.insert(url);
        if (url == urlBad) {
            body = "<html><body>A login page, not a picture, long enough to pass a size check....</body></html>";
            return true;
        }
        if (url == urlGone) {
            err = "HTTP 404";
            return false;
        }
        body = png;
        return true;
    };

    {   // download -> disk -> texture
        Gl gl;
        ra::ImageCache c;
        gl.bind(c);
        c.fetch = fakeFetch;
        c.start(root);
        ra::ImageView v = c.get(urlA);
        CHECK(v.state == ra::ImageView::Loading);
        CHECK(settle(c));
        v = c.get(urlA);
        CHECK(v.state == ra::ImageView::Ready && v.tex != 0 && v.w == 64 && v.h == 64);
        CHECK(c.fetches() == 1 && c.textureCount() == 1);
        CHECK(fileExists(ra::imageCachePath(root, urlA)));
        c.get(urlA);   // memory hit: no new job
        CHECK(c.pending() == 0 && c.fetches() == 1);

        // an HTML body is not a picture: nothing is saved, the slot is Missing
        v = ask(c, urlBad);
        CHECK(v.state == ra::ImageView::Missing);
        CHECK(!fileExists(ra::imageCachePath(root, urlBad)));
        // a failed download too, and it is not asked again at once
        v = ask(c, urlGone);
        CHECK(v.state == ra::ImageView::Missing);
        const unsigned before = c.fetches();
        for (int i = 0; i < 20; i++) c.get(urlGone);
        settle(c);
        CHECK(c.fetches() == before);
        c.stop();
        CHECK(gl.created == gl.destroyed && gl.live.empty());   // stop() frees every texture
    }
    {   // a new run, offline: served from disk with no fetch at all
        Gl gl;
        ra::ImageCache c;
        gl.bind(c);
        unsigned fetched = 0;
        c.fetch = [&](const std::string&, std::string&, std::string&) {
            fetched++;
            return false;
        };
        c.setNetwork(false);
        c.start(root);
        ra::ImageView v = ask(c, urlA);
        CHECK(v.state == ra::ImageView::Ready);
        // not on disk + offline = Missing, nothing fetched, nothing written
        v = ask(c, urlB);
        CHECK(v.state == ra::ImageView::Missing);
        CHECK(fetched == 0 && !fileExists(ra::imageCachePath(root, urlB)));
        // switch downloads back on: the earlier miss is looked up again
        c.fetch = fakeFetch;
        c.setNetwork(true);
        c.get(urlB);
        settle(c);
        v = c.get(urlB);
        CHECK(v.state == ra::ImageView::Ready && asked.count(urlB) == 1 && fileExists(ra::imageCachePath(root, urlB)));
        c.stop();
        CHECK(gl.created == gl.destroyed);
    }
    {   // headless (no createTexture): nothing happens
        ra::ImageCache c;
        c.fetch = fakeFetch;
        c.start(root);
        CHECK(c.get(urlA).state == ra::ImageView::None);
        c.stop();
    }
    {   // a damaged file on disk is fetched again, not shown
        const std::string url = "https://media.retroachievements.org/Badge/damaged.png";
        const std::string path = ra::imageCachePath(root, url);
        const std::string junk(200, 'x');
        writeFileAtomic(path, junk.data(), junk.size());
        Gl gl;
        ra::ImageCache c;
        gl.bind(c);
        c.fetch = fakeFetch;
        c.start(root);
        CHECK(ask(c, url).state == ra::ImageView::Ready && asked.count(url) == 1);
        c.stop();
    }
    {   // retry after the delay
        Gl gl;
        ra::ImageCache c;
        gl.bind(c);
        c.fetch = fakeFetch;
        ra::ImageCache::Options o;
        o.retrySeconds = 0.05;
        c.start(root, o);
        ask(c, urlGone);
        const unsigned before = c.fetches();
        std::this_thread::sleep_for(std::chrono::milliseconds(120));
        c.get(urlGone);
        settle(c);
        CHECK(c.fetches() == before + 1);
        c.stop();
    }
    {   // bounded memory: at most maxTextures live, the least recently asked for go first, reloads come from disk
        Gl gl;
        ra::ImageCache c;
        gl.bind(c);
        c.fetch = fakeFetch;
        ra::ImageCache::Options o;
        o.maxTextures = 4;
        o.uploadsPerPump = 2;
        c.start(root, o);
        std::vector<std::string> urls;
        for (int i = 0; i < 10; i++) urls.push_back("https://media.retroachievements.org/Badge/lru" + std::to_string(i) + ".png");
        for (const std::string& u : urls) {
            c.get(u);
            settle(c);   // one per "frame", like scrolling
            c.pump();
            c.pump();
        }
        for (const std::string& u : urls) c.get(u);   // all ten asked for now; only the memory cap limits what stays
        for (int i = 0; i < 40; i++) {
            c.pump();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        settle(c);
        for (int i = 0; i < 6; i++) c.pump();
        CHECK(c.textureCount() <= 4 + 2);   // a pump may upload up to its quota before evict() trims
        CHECK(gl.live.size() == c.textureCount());
        CHECK(gl.destroyed > 0);
        const unsigned fetchesBefore = c.fetches();
        // ask for the first one again: it was evicted, so it comes back from DISK (no new download)
        ask(c, urls[0]);
        ask(c, urls[0]);
        CHECK(c.get(urls[0]).state == ra::ImageView::Ready);
        CHECK(c.fetches() == fetchesBefore);
        c.stop();
        CHECK(gl.created == gl.destroyed && gl.live.empty());
    }
    {   // the disk cap removes the oldest pictures and leaves other files alone
        const std::string dir = root + "prune/";
        wipe(dir);
        SDL_CreateDirectory(dir.c_str());
        for (int i = 0; i < 4; i++) {
            writeFileAtomic(dir + "p" + std::to_string(i) + ".png", png.data(), png.size());
            if (i < 3) SDL_Delay(1100);   // file times have one-second granularity on some filesystems
        }
        writeFileAtomic(dir + "games-17.tsv", "VJRA1\t1\n", 8);
        const uint64_t cap = png.size() * 2 + 10;
        CHECK(ra::pruneImageDir(dir, cap) == 2);
        CHECK(!fileExists(dir + "p0.png") && !fileExists(dir + "p1.png") && fileExists(dir + "p2.png") && fileExists(dir + "p3.png"));
        CHECK(fileExists(dir + "games-17.tsv"));
        CHECK(ra::pruneImageDir(dir, cap) == 0);
    }
    wipe(root + "ra-cache");
    wipe(root + "prune");
    SDL_RemovePath(root.c_str());
}

int main() {
    testPaths();
    testGamesParsing();
    testMatching();
    testCacheFile();
    testGameViews();
    testAchievementViews();
    testImageCache();
    if (g_fail) {
        std::fprintf(stderr, "ra_dash_test: %d of %d checks FAILED\n", g_fail, g_checks);
        return 1;
    }
    std::printf("ra_dash_test: %d checks passed\n", g_checks);
    return 0;
}
