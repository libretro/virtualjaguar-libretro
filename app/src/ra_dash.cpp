#include "ra_dash.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>

#include "library.h"   // lib::normalizeDiscName
#include "rc_api_info.h"
#include "rc_api_request.h"
#include "rc_api_user.h"

namespace vj {
namespace ra {

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

std::string clean(const std::string& s) {   // one TSV cell
    std::string o = s;
    for (char& c : o)
        if (c == '\t' || c == '\n' || c == '\r') c = ' ';
    return o;
}

std::vector<std::string> splitTab(const std::string& line) {
    std::vector<std::string> out;
    size_t pos = 0;
    for (;;) {
        size_t t = line.find('\t', pos);
        if (t == std::string::npos) {
            out.push_back(line.substr(pos));
            break;
        }
        out.push_back(line.substr(pos, t - pos));
        pos = t + 1;
    }
    return out;
}

unsigned toUnsigned(const std::string& s) { return (unsigned)std::strtoul(s.c_str(), nullptr, 10); }

unsigned shownUnlocked(const GameEntry& g) { return std::max(g.unlocked, g.unlockedHardcore); }

}  // namespace

bool containsNoCase(const std::string& hay, const std::string& needle) {
    if (needle.empty()) return true;
    return lower(hay).find(lower(needle)) != std::string::npos;
}

// ---- parsing -----------------------------------------------------------------------------

bool parseGamesJson(const std::string& json, uint32_t console, std::vector<GameEntry>& out, std::string& err) {
    out.clear();
    rc_api_server_response_t sr;
    std::memset(&sr, 0, sizeof sr);
    sr.body = json.c_str();
    sr.body_length = json.size();
    sr.http_status_code = 200;
    rc_api_fetch_games_list_response_t resp;
    int rc = rc_api_process_fetch_games_list_server_response(&resp, &sr);
    if (rc != RC_OK || !resp.response.succeeded) {
        err = resp.response.error_message ? resp.response.error_message : "the games list could not be read";
        rc_api_destroy_fetch_games_list_response(&resp);
        return false;
    }
    for (uint32_t i = 0; i < resp.num_entries; i++) out.push_back(gameFromEntry(resp.entries[i], console));
    rc_api_destroy_fetch_games_list_response(&resp);
    for (GameEntry& g : out)
        for (std::string& h : g.hashes) h = lower(h);
    return true;
}

bool parseProgressJson(const std::string& json, std::map<uint32_t, Progress>& out, std::string& err) {
    out.clear();
    rc_api_server_response_t sr;
    std::memset(&sr, 0, sizeof sr);
    sr.body = json.c_str();
    sr.body_length = json.size();
    sr.http_status_code = 200;
    rc_api_fetch_all_user_progress_response_t resp;
    int rc = rc_api_process_fetch_all_user_progress_server_response(&resp, &sr);
    if (rc != RC_OK || !resp.response.succeeded) {
        err = resp.response.error_message ? resp.response.error_message : "the progress list could not be read";
        rc_api_destroy_fetch_all_user_progress_response(&resp);
        return false;
    }
    for (uint32_t i = 0; i < resp.num_entries; i++) {
        Progress p;
        p.achievements = resp.entries[i].num_achievements;
        p.unlocked = resp.entries[i].num_unlocked_achievements;
        p.unlockedHardcore = resp.entries[i].num_unlocked_achievements_hardcore;
        out[resp.entries[i].game_id] = p;
    }
    rc_api_destroy_fetch_all_user_progress_response(&resp);
    return true;
}

void applyProgress(std::vector<GameEntry>& games, const std::map<uint32_t, Progress>& p) {
    for (GameEntry& g : games) {
        g.haveProgress = true;
        g.unlocked = g.unlockedHardcore = 0;
        auto it = p.find(g.id);
        if (it == p.end()) continue;
        g.unlockedHardcore = it->second.unlockedHardcore;
        g.unlocked = std::max(it->second.unlocked, it->second.unlockedHardcore);
        if (it->second.achievements) g.achievements = std::max(g.achievements, it->second.achievements);
    }
}

// ---- library matching --------------------------------------------------------------------

std::string normalizeTitle(const std::string& raTitle) {
    // "~Homebrew~ Tower Toppler" -> "Tower Toppler"; "&" reads as "and" on both sides.
    std::string t;
    bool inTag = false;
    for (size_t i = 0; i < raTitle.size(); i++) {
        char c = raTitle[i];
        if (c == '~') {
            inTag = !inTag;
            t += ' ';
            continue;
        }
        if (inTag) continue;
        if (c == '&') {
            t += " and ";
            continue;
        }
        t += c;
    }
    std::string n = lib::normalizeDiscName(t);
    // "The Foo" and "Foo, The" are the same game.
    if (n.compare(0, 4, "the ") == 0) n.erase(0, 4);
    if (n.size() > 4 && n.compare(n.size() - 4, 4, " the") == 0) n.erase(n.size() - 4);
    return n;
}

size_t matchLibrary(std::vector<GameEntry>& games, const std::vector<LibItem>& items) {
    std::map<std::string, size_t> byHash;   // md5 -> item (first wins)
    std::multimap<std::string, size_t> byTitle;
    for (size_t i = 0; i < items.size(); i++) {
        if (!items[i].md5.empty()) byHash.emplace(lower(items[i].md5), i);
        std::string a = normalizeTitle(items[i].datName), b = normalizeTitle(items[i].title);
        if (!a.empty()) byTitle.emplace(a, i);
        if (!b.empty() && b != a) byTitle.emplace(b, i);
    }
    size_t n = 0;
    for (GameEntry& g : games) {
        g.libIndex = -1;
        g.matchedByHash = false;
        const bool disc = g.console == kConsoleJaguarCD;
        for (const std::string& h : g.hashes) {
            auto it = byHash.find(h);
            if (it != byHash.end() && items[it->second].disc == disc) {
                g.libIndex = items[it->second].index;
                g.matchedByHash = true;
                break;
            }
        }
        if (g.libIndex < 0) {
            const std::string key = normalizeTitle(g.title);
            if (!key.empty()) {
                auto range = byTitle.equal_range(key);
                size_t best = (size_t)-1;
                for (auto it = range.first; it != range.second; ++it)
                    if (items[it->second].disc == disc && it->second < best) best = it->second;
                if (best != (size_t)-1) g.libIndex = items[best].index;
            }
        }
        if (g.libIndex >= 0) n++;
    }
    return n;
}

// ---- cache file ----------------------------------------------------------------------------

namespace {
const char* kMagic = "VJRA1";
}

std::string serializeGames(const std::vector<GameEntry>& games, int64_t fetchedAt) {
    std::string out;
    char head[64];
    std::snprintf(head, sizeof head, "%s\t%lld\n", kMagic, (long long)fetchedAt);
    out += head;
    for (const GameEntry& g : games) {
        char nums[96];
        std::snprintf(nums, sizeof nums, "%u\t%u\t%u\t%u\t%u\t", g.id, g.console, g.achievements, g.leaderboards, g.points);
        out += nums;
        out += clean(g.imageUrl);
        out += '\t';
        out += clean(g.title);
        out += '\t';
        for (size_t i = 0; i < g.hashes.size(); i++) {
            if (i) out += ',';
            out += clean(g.hashes[i]);
        }
        out += '\n';
    }
    return out;
}

bool deserializeGames(const std::string& text, std::vector<GameEntry>& games, int64_t& fetchedAt) {
    games.clear();
    fetchedAt = 0;
    size_t pos = 0;
    bool first = true;
    while (pos < text.size()) {
        size_t nl = text.find('\n', pos);
        std::string line = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
        pos = nl == std::string::npos ? text.size() : nl + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        std::vector<std::string> f = splitTab(line);
        if (first) {
            first = false;
            if (f.size() != 2 || f[0] != kMagic) return false;
            fetchedAt = std::strtoll(f[1].c_str(), nullptr, 10);
            continue;
        }
        if (f.size() != 8) {   // a damaged line voids the whole file: refetch rather than trust half of it
            games.clear();
            return false;
        }
        GameEntry g;
        g.id = toUnsigned(f[0]);
        g.console = toUnsigned(f[1]);
        g.achievements = toUnsigned(f[2]);
        g.leaderboards = toUnsigned(f[3]);
        g.points = toUnsigned(f[4]);
        g.imageUrl = f[5];
        g.title = f[6];
        size_t p = 0;
        while (p < f[7].size()) {
            size_t c = f[7].find(',', p);
            std::string h = f[7].substr(p, c == std::string::npos ? std::string::npos : c - p);
            if (!h.empty()) g.hashes.push_back(h);
            if (c == std::string::npos) break;
            p = c + 1;
        }
        if (g.id == 0) {
            games.clear();
            return false;
        }
        games.push_back(std::move(g));
    }
    return !first;
}

std::string gamesCachePath(const std::string& root, uint32_t console) {
    return root + "ra-cache/games-" + std::to_string(console) + ".tsv";
}

bool gamesStale(int64_t fetchedAt, int64_t now, int64_t maxAge) {
    if (fetchedAt <= 0) return true;
    if (now < fetchedAt) return true;   // the clock went backwards: do not trust the stamp
    return now - fetchedAt >= maxAge;
}

// ---- views -----------------------------------------------------------------------------------

const char* gameFilterLabel(GameFilter f) {
    switch (f) {
    case GameFilter::All: return "All games";
    case GameFilter::Owned: return "In my library";
    case GameFilter::InProgress: return "In progress";
    case GameFilter::Completed: return "Completed";
    case GameFilter::NotStarted: return "Not started";
    }
    return "";
}

const char* gameSortLabel(GameSort s) {
    switch (s) {
    case GameSort::Title: return "Title";
    case GameSort::Points: return "Points";
    case GameSort::Achievements: return "Achievements";
    case GameSort::Progress: return "My progress";
    case GameSort::OwnedFirst: return "In my library first";
    }
    return "";
}

std::vector<size_t> viewGames(const std::vector<GameEntry>& games, GameFilter f, GameSort s, const std::string& text) {
    std::vector<size_t> v;
    for (size_t i = 0; i < games.size(); i++) {
        const GameEntry& g = games[i];
        bool ok = true;
        switch (f) {
        case GameFilter::All: break;
        case GameFilter::Owned: ok = g.libIndex >= 0; break;
        case GameFilter::InProgress: ok = g.haveProgress && shownUnlocked(g) > 0 && shownUnlocked(g) < g.achievements; break;
        case GameFilter::Completed: ok = g.haveProgress && g.achievements > 0 && shownUnlocked(g) >= g.achievements; break;
        case GameFilter::NotStarted: ok = g.haveProgress && shownUnlocked(g) == 0; break;
        }
        if (ok && !containsNoCase(g.title, text)) ok = false;
        if (ok) v.push_back(i);
    }
    auto frac = [&](const GameEntry& g) {
        if (!g.haveProgress || g.achievements == 0) return -1.0;
        return (double)shownUnlocked(g) / (double)g.achievements;
    };
    auto titleLess = [&](size_t a, size_t b) {
        const std::string x = lower(games[a].title), y = lower(games[b].title);
        if (x != y) return x < y;
        return games[a].id < games[b].id;
    };
    std::stable_sort(v.begin(), v.end(), [&](size_t a, size_t b) {
        const GameEntry &x = games[a], &y = games[b];
        switch (s) {
        case GameSort::Title: break;
        case GameSort::Points:
            if (x.points != y.points) return x.points > y.points;
            break;
        case GameSort::Achievements:
            if (x.achievements != y.achievements) return x.achievements > y.achievements;
            break;
        case GameSort::Progress: {
            const double fx = frac(x), fy = frac(y);
            if (fx != fy) return fx > fy;
            break;
        }
        case GameSort::OwnedFirst:
            if ((x.libIndex >= 0) != (y.libIndex >= 0)) return x.libIndex >= 0;
            break;
        }
        return titleLess(a, b);
    });
    return v;
}

const char* achFilterLabel(AchFilter f) {
    switch (f) {
    case AchFilter::All: return "All";
    case AchFilter::Unlocked: return "Unlocked";
    case AchFilter::Locked: return "Locked";
    case AchFilter::Hardcore: return "Unlocked in hardcore";
    case AchFilter::Missable: return "Missable";
    }
    return "";
}

const char* achSortLabel(AchSort s) {
    switch (s) {
    case AchSort::Default: return "Default order";
    case AchSort::Title: return "Title";
    case AchSort::Points: return "Points";
    case AchSort::Rarity: return "Rarest first";
    case AchSort::Recent: return "Recently unlocked";
    case AchSort::Type: return "Type";
    }
    return "";
}

const char* achTypeLabel(int type) {
    switch (type) {
    case 1: return "Missable";
    case 2: return "Progression";
    case 3: return "Win condition";
    default: return "";
    }
}

std::vector<size_t> viewAchievements(const std::vector<AchRow>& rows, AchFilter f, AchSort s, const std::string& text) {
    std::vector<size_t> v;
    for (size_t i = 0; i < rows.size(); i++) {
        const AchRow& r = rows[i];
        bool ok = true;
        switch (f) {
        case AchFilter::All: break;
        case AchFilter::Unlocked: ok = r.unlocked(); break;
        case AchFilter::Locked: ok = !r.unlocked(); break;
        case AchFilter::Hardcore: ok = r.unlockedHard; break;
        case AchFilter::Missable: ok = r.type == 1; break;
        }
        if (ok && !(containsNoCase(r.title, text) || containsNoCase(r.description, text))) ok = false;
        if (ok) v.push_back(i);
    }
    std::stable_sort(v.begin(), v.end(), [&](size_t a, size_t b) {
        const AchRow &x = rows[a], &y = rows[b];
        switch (s) {
        case AchSort::Default: break;
        case AchSort::Title: {
            const std::string p = lower(x.title), q = lower(y.title);
            if (p != q) return p < q;
            break;
        }
        case AchSort::Points:
            if (x.points != y.points) return x.points > y.points;
            break;
        case AchSort::Rarity:
            // Unknown rarity (0) sorts last, not first.
            if ((x.rarity > 0) != (y.rarity > 0)) return x.rarity > 0;
            if (x.rarity != y.rarity) return x.rarity < y.rarity;
            break;
        case AchSort::Recent:
            if (x.unlocked() != y.unlocked()) return x.unlocked();
            if (x.unlocked() && x.unlockTime != y.unlockTime) return x.unlockTime > y.unlockTime;
            break;
        case AchSort::Type:
            if (x.type != y.type) return x.type > y.type;
            break;
        }
        return x.order < y.order;
    });
    return v;
}

}  // namespace ra
}  // namespace vj
