// The game library's personal lists, pure logic (no ImGui, no GL, no threads): favorites, recently played,
// the display options for both, the tab layout, and the save states found on disk.  Unit-tested headlessly
// (app/tests/library_tests.cpp).  library_service.cpp wires these to the UI.
//
// Persistence and the several-instances rule (instance.h, app/README.md "Running several instances"):
//   favorites and recents live in ONE small shared file, <data>/library/user.tsv, never in config.ini.
//   Every running copy may change them, and a change is an OPERATION, never a snapshot: the store re-reads the
//   file, applies the one change (star a game, record a play, remove a recent, clear the recents) and writes the
//   whole file atomically (writeFileAtomic: temp file + rename).  So a second copy can neither freeze a stale
//   list over the first copy's, nor undo what it did; two copies acting in the same few milliseconds can at
//   worst lose one of the two operations.  Each copy re-reads the file when it changes (UserStore::refresh).
//   The display options ("show as tab / in sections") are ordinary settings (library.favorites.show,
//   library.recents.show in config.ini) and follow the settings rule: a second copy reads them and does not
//   write them.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "library.h"

namespace vj {
namespace lib {

// ---- display options and tabs ----------------------------------------------------------------------------------
// For favorites and for recently played, separately: where do they show?
enum ShowAs { ShowHidden = 0, ShowTab = 1, ShowSections = 2, ShowBoth = 3 };
const int kShowAsCount = 4;
int clampShowAs(int v, int def);                 // out of range -> def
bool showsTab(int mode);                         // its own tab (both media)
bool showsStrip(int mode);                       // a strip at the top of the Cartridges and Jaguar CD tabs
const char* showAsLabel(int mode);               // "Hidden", "Tab", "In the Cartridges and CD tabs", "Tab and sections"
const int kFavoritesDefault = ShowBoth;          // default: a tab and a strip
const int kRecentsDefault = ShowBoth;

// Library tab ids.  0..2 are the Section values (stored in library.section since the first release), so the
// new tabs are appended; the order they are SHOWN in is kTabOrder.
enum Tab { TabCart = 0, TabDisc = 1, TabBios = 2, TabFavorites = 3, TabRecents = 4, TabStates = 5 };
const int kTabCount = 6;
// The tabs on screen, in order: Cartridges, Jaguar CD, [Favorites], [Recently played], Save states, BIOS & system.
std::vector<int> visibleTabs(int favoritesMode, int recentsMode);
// The tab to actually show for a wanted (persisted) one: Cartridges when that tab is hidden or unknown.
int effectiveTab(int wanted, int favoritesMode, int recentsMode);
const char* tabName(int tab);                    // "Cartridges", "Favorites", ...
// Does the favorites / recents strip belong on this tab?  Only Cartridges and Jaguar CD, only in a strip mode.
bool stripOnTab(int tab, int mode);

// ---- personal lists --------------------------------------------------------------------------------------------
// A game as the lists remember it.  Three handles because each can change: the path when a folder moves, the
// checksum never (until the file is edited), the key (coverKey) when an update identifies a game that was
// unidentified.  Two items are the same game when ANY non-empty handle matches.
struct UserItem {
    std::string key;     // coverKey(entry)
    std::string path;    // absolute
    std::string title;   // for the file, so a human can read it; the UI shows the entry's own title
    uint32_t crc = 0;    // file.crcWhole, 0 = none
    int64_t when = 0;    // favorites: time added; recents: time played (seconds since the epoch)
};
UserItem userItemFor(const Entry& e, int64_t when);
bool sameGame(const UserItem& a, const UserItem& b);

struct UserLists {
    std::vector<UserItem> favorites;   // in the order added
    std::vector<UserItem> recents;     // most recently played first
};
const size_t kMaxRecentGames = 20;

std::string serializeUserLists(const UserLists& l);
UserLists parseUserLists(const std::string& text);   // junk lines are skipped

// The operations (on an in-memory copy; UserStore applies them to the file's copy).
bool hasFavorite(const UserLists& l, const UserItem& it);
void setFavoriteIn(UserLists& l, const UserItem& it, bool on);
void recordPlayIn(UserLists& l, const UserItem& it, size_t cap = kMaxRecentGames);   // to the front, deduplicated, capped
void removeRecentIn(UserLists& l, const UserItem& it);
bool hasRecent(const UserLists& l, const UserItem& it);

// A list item matched to the library entry that stands for it now.
struct Resolved {
    const Entry* entry = nullptr;
    int64_t when = 0;
};
// Matches by path, then checksum, then key; a game appears once; items with no entry (an unplugged drive, a
// removed folder) are skipped but stay in the list; BIOS / system files never match.  Order is the items' order.
std::vector<Resolved> resolveUserItems(const std::vector<UserItem>& items, const std::vector<Entry>& entries);
void sortByTitle(std::vector<Resolved>& v);   // case-insensitive, stable
// Only the entries of one section (the strips on the Cartridges and Jaguar CD tabs).
std::vector<Resolved> filterBySection(const std::vector<Resolved>& v, Section s);

// ---- the shared file ---------------------------------------------------------------------------------------------
class UserStore {
public:
    void open(const std::string& file);   // reads it (a missing file is an empty list)
    bool isOpen() const { return !file_.empty(); }
    // Re-reads the file when it changed since the last read.  True when the lists changed.
    bool refresh();
    const UserLists& lists() const { return lists_; }
    unsigned generation() const { return gen_; }   // bumped whenever lists() changes

    // Each one: re-read, apply, write atomically.  False when the file could not be written (the in-memory
    // lists still hold the change, so the session keeps working).
    bool setFavorite(const UserItem& it, bool on);
    bool recordPlay(const UserItem& it);
    bool removeRecent(const UserItem& it);
    bool clearRecents();
    bool clearFavorites();

private:
    template <class F> bool apply(F&& f);
    bool read();
    bool write();
    std::string file_;
    UserLists lists_;
    unsigned gen_ = 0;
    int64_t seenMtime_ = -1;
    int64_t seenSize_ = -1;
};

// ---- save states on disk -----------------------------------------------------------------------------------------
// App::statePath / thumbPath build their names here, so the app and the library tab cannot disagree.
// Slots 0..N are the numbered ones (F5 / F7); kAutoSlot is the auto-save ("Resume where I left off"): its own
// file pair per game, "<stem>.auto.state" / "<stem>.auto.png", only the latest kept.
const int kAutoSlot = -1;
std::string stateFileName(const std::string& stem, int slot);   // "<stem>.slot<N>.state", or "<stem>.auto.state"
std::string thumbFileName(const std::string& stem, int slot);   // "<stem>.slot<N>.png", or "<stem>.auto.png"
std::string stateLabel(int slot);                                // "Slot 3" / "Auto-save"
// "Iron Soldier.slot3.state" -> stem "Iron Soldier", slot 3; "Iron Soldier.auto.state" -> slot kAutoSlot.  False
// for anything else (thumbnails, sidecars).
bool parseStateFileName(const std::string& name, std::string& stem, int& slot);

// ---- resume where I left off: the policy (pure, so the tests state it) ------------------------------------------
enum ResumeMode { ResumeOff = 0, ResumeAsk = 1, ResumeAlways = 2 };
const int kResumeModeCount = 3;
const int kResumeDefault = ResumeAsk;
// An auto-save is written only when the game ran at least this long: under it the machine is still on the boot
// logo / BIOS screen, and a quick open-and-close would replace a real auto-save with a title screen.
const double kAutoSaveMinSeconds = 5.0;
int clampResumeMode(int v);
const char* resumeModeLabel(int mode);   // "Off", "Ask", "Always"
// Write one when a game is closed, left for another game, or the app quits?  Never with the option off, in
// RetroAchievements hardcore (it refuses state loads), during netplay (a state would desynchronise the link),
// for a game that ran under kAutoSaveMinSeconds, or one the crash watchdog flagged (resuming would land in the hang).
bool shouldAutoSave(int mode, bool hardcore, bool netplay, double ranSeconds, bool crashed);
// What to do when a game boots and has (or has not) an auto-save.
enum class ResumeAction { None, Load, Ask };
ResumeAction resumeAction(int mode, bool hardcore, bool netplay, bool autoSaveExists);
// Where this running copy keeps auto-saves: the states folder for the first copy, states/instance-N/ for copy N
// (instance.h: copies do not share them, so two windows on one game cannot overwrite each other's).  Ends with a
// separator.  The numbered states stay shared, as before.
std::string autoStatesDir(const std::string& statesDir, int instance);

struct StateInfo {
    int slot = 0;
    std::string disc{};      // merged playlist rows: the disc this state belongs to, otherwise empty
    std::string path;        // the .state file
    std::string thumbPath;   // its screenshot, "" when there is none
    int64_t mtime = 0;       // seconds since the epoch
    int64_t mtimeNs = 0;     // the same, finer: a screenshot cache is stale when this differs
    uint64_t size = 0;
};
struct StateGame {
    std::string stem;                 // the file name without extension: what App::statePath keys states by
    std::vector<StateInfo> states;    // newest first
    int64_t newest = 0;
};
// Every game with at least one state in `dir` (ends with a separator), most recently saved first.
std::vector<StateGame> listStates(const std::string& dir);
// What copy `instance` shows: the shared numbered states, and its OWN auto-saves (autoStatesDir), not another copy's.
std::vector<StateGame> listStatesFor(const std::string& statesDir, int instance);
// Removes the state, its screenshot and the RetroAchievements sidecar (<state>.rcp).  False when the state
// file could not be removed.
bool deleteState(const StateInfo& s);
// The game a stem belongs to: the first library entry whose file stem is `stem` (BIOS files excluded), or null.
const Entry* entryForStem(const std::vector<Entry>& entries, const std::string& stem);

// ---- show in the file manager ----------------------------------------------------------------------------------------
// A file:// URL of the folder holding `path` (percent-encoded per segment; Windows paths become file:///C:/...).
std::string folderUrlOf(const std::string& path);

}  // namespace lib
}  // namespace vj
