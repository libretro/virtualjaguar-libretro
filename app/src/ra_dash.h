// The logic behind the RetroAchievements dashboard (#836), with no ImGui, no GL, no
// threads and no network, so it is unit-tested headlessly (app/tests/ra_dash_test.cpp).
//
//   * GameEntry     one game that has achievements on RetroAchievements (console 17
//                   Atari Jaguar, 77 Atari Jaguar CD), plus the signed-in user's
//                   progress and, once matched, the library file that runs it
//   * parse*        the server's JSON through rcheevos' own response parsers
//                   (the same code rc_client_begin_fetch_game_list uses), so the
//                   fixtures under app/tests/ra exercise the real schema
//   * matchLibrary  hash first (RA lists the MD5s it accepts), then title
//   * serialize*    the once-a-day cache of the games list (a small TSV; the
//                   rc_client callback hands over parsed data, not the raw body)
//   * view*         filter + sort for the games table and the achievement list
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace vj {
namespace ra {

constexpr uint32_t kConsoleJaguar = 17;     // RC_CONSOLE_ATARI_JAGUAR
constexpr uint32_t kConsoleJaguarCD = 77;   // RC_CONSOLE_ATARI_JAGUAR_CD
constexpr int64_t kGamesMaxAge = 24 * 3600; // refresh the games list at most daily on its own

// ---- games -----------------------------------------------------------------------------

struct GameEntry {
    uint32_t id = 0;
    uint32_t console = 0;            // 17 or 77
    std::string title;
    unsigned achievements = 0;
    unsigned leaderboards = 0;
    unsigned points = 0;
    std::string imageUrl;
    std::vector<std::string> hashes;   // supported MD5s, lower case
    // The signed-in user's progress (haveProgress false until it arrived, or when signed out).
    bool haveProgress = false;
    unsigned unlocked = 0;
    unsigned unlockedHardcore = 0;
    // The library file that runs this game (-1 = none).
    int libIndex = -1;
    bool matchedByHash = false;
};

struct Progress {
    unsigned achievements = 0;
    unsigned unlocked = 0;
    unsigned unlockedHardcore = 0;
};

// Server JSON ("r=systemgames" / "r=allprogress" replies) -> plain structs.  False with
// `err` set when rcheevos rejects the reply.
bool parseGamesJson(const std::string& json, uint32_t console, std::vector<GameEntry>& out, std::string& err);
bool parseProgressJson(const std::string& json, std::map<uint32_t, Progress>& out, std::string& err);
// The same conversion for rc_client's own list type (a template over the identically
// shaped rc_client_game_list_entry_t / rc_api_game_list_entry_t).
template <class Entry>
GameEntry gameFromEntry(const Entry& e, uint32_t console) {
    GameEntry g;
    g.id = e.id;
    g.console = console;
    g.title = e.name ? e.name : "";
    g.achievements = e.num_achievements;
    g.leaderboards = e.num_leaderboards;
    g.points = e.points;
    g.imageUrl = e.image_url ? e.image_url : "";
    for (uint32_t i = 0; i < e.num_supported_hashes; i++)
        if (e.supported_hashes && e.supported_hashes[i]) g.hashes.push_back(e.supported_hashes[i]);
    return g;
}
// Fills in each game's progress from `p` (games without a row get "0 of N unlocked").
void applyProgress(std::vector<GameEntry>& games, const std::map<uint32_t, Progress>& p);

// ---- the library ------------------------------------------------------------------------

struct LibItem {
    int index = 0;           // the caller's index back to its library entry
    bool disc = false;       // Jaguar CD (console 77) or cartridge (17)
    std::string title;       // dat name, else the cleaned file name
    std::string datName;
    std::string md5;         // lower case, "" = not hashed
};

// RA titles carry tags ("~Homebrew~ Tower Toppler", "Doom | Doom (Jaguar)"): the part a library
// name can equal, normalised for comparison.
std::string normalizeTitle(const std::string& raTitle);
// Marks every game that a library item runs; returns how many.  Hash match wins over title; a
// title that fits two items goes to the first.  Console must agree (cart vs disc).
size_t matchLibrary(std::vector<GameEntry>& games, const std::vector<LibItem>& items);

// ---- the cache file ------------------------------------------------------------------------

std::string serializeGames(const std::vector<GameEntry>& games, int64_t fetchedAt);
// False for anything that is not a cache file of this format version.
bool deserializeGames(const std::string& text, std::vector<GameEntry>& games, int64_t& fetchedAt);
std::string gamesCachePath(const std::string& root, uint32_t console);   // <root>ra-cache/games-<console>.tsv
bool gamesStale(int64_t fetchedAt, int64_t now, int64_t maxAge = kGamesMaxAge);

// ---- views -----------------------------------------------------------------------------------

enum class GameFilter { All, Owned, InProgress, Completed, NotStarted };
enum class GameSort { Title, Points, Achievements, Progress, OwnedFirst };
// Indexes into `games`, filtered by `f` and the case-insensitive substring `text`, then sorted
// (stable, title as the tie-break).  Progress filters need progress: with none they match nothing
// but All/Owned.
std::vector<size_t> viewGames(const std::vector<GameEntry>& games, GameFilter f, GameSort s, const std::string& text);
const char* gameFilterLabel(GameFilter f);
const char* gameSortLabel(GameSort s);

// ---- achievements -----------------------------------------------------------------------------

struct AchRow {
    uint32_t id = 0;
    std::string title;
    std::string description;
    unsigned points = 0;
    bool unlockedSoft = false;
    bool unlockedHard = false;
    int64_t unlockTime = 0;
    int bucket = 0;              // rc_client's bucket (locked, unlocked, unsupported, ...), list order
    bool unsupported = false;
    bool unofficial = false;
    float rarity = 0;            // percent of players who earned it
    int type = 0;                // rc_client achievement type (progression, win condition, missable)
    std::string progress;        // measured progress while locked ("5/10"), or ""
    std::string badgeUrl;        // earned
    std::string badgeLockedUrl;  // greyed
    size_t order = 0;            // position in the incoming list (the default sort)
    bool unlocked() const { return unlockedSoft || unlockedHard; }
};

enum class AchFilter { All, Unlocked, Locked, Hardcore, Missable };
enum class AchSort { Default, Title, Points, Rarity, Recent, Type };
std::vector<size_t> viewAchievements(const std::vector<AchRow>& rows, AchFilter f, AchSort s, const std::string& text);
const char* achFilterLabel(AchFilter f);
const char* achSortLabel(AchSort s);
const char* achTypeLabel(int type);   // "" for a standard achievement

// Case-insensitive substring test (ASCII).
bool containsNoCase(const std::string& hay, const std::string& needle);

}  // namespace ra
}  // namespace vj
