// Cheats: the libretro .cht database format, per-game selection and persistence.
//
// This file and cheats.cpp are deliberately free of SDL, ImGui and the core's
// run-time state so the parsing, conversion and persistence logic can be unit
// tested on its own (app/tests/cheats_test.cpp).  The core is reached through
// two sinks (retro_cheat_reset / retro_cheat_set) wired up in cheats_core.cpp.
//
// Data flow
//   *.cht files (libretro-database, "cht/Atari - Jaguar/")  --parse-->  ChtSet
//   ChtSet + per-game state (<data>/cheats/games/<key>.ini)  --apply--> retro_cheat_*
//
// What a .cht entry becomes.  The Jaguar database is almost entirely
// RetroArch "memory" cheats: cheatN_address / cheatN_value are DECIMAL, the
// width is cheatN_memory_search_size (3 = 8 bit, 4 = 16 bit, 5 = 32 bit,
// RetroArch's log2-of-bits) and cheatN_big_endian says how the VALUE's bytes
// are laid out in memory.  The core only accepts hex "ADDR:VALUE" codes, so
// every such entry is converted to one or more of those (see
// memoryCheatToCode).  An entry that carries its own cheatN_code (about 10% of
// the database) is passed to the core unchanged.  Anything else (a different
// handler, a "increase by value" cheat type, repeat counts, values outside
// the 2 MB main RAM) is marked unsupported with the reason, never guessed at.
//
// cheatN_enable is IGNORED on purpose: nearly every entry says "true", and
// honouring it would switch every cheat on at load.  A cheat is on only when
// the user turned it on, and that choice is what gets persisted.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "config.h"

namespace vj {

struct ChtCheat {
    std::string desc;
    std::string code;        // core format, segments joined by '+'; "" when unsupported
    std::string id;          // stable: 8 hex digits, FNV-1a of desc + "\n" + code
    unsigned parts = 1;      // >1: several consecutive entries ("... (Part 1)", "(Part 2)") toggled as one
    unsigned segments = 0;   // writes this cheat adds to the core's list
    bool supported = true;
    std::string note;        // why a cheat is unsupported
};

struct ChtSet {
    std::string stem;        // file name without extension
    std::string path;
    std::vector<ChtCheat> cheats;
};

// A set found in a library folder (not parsed until it is selected).
struct ChtRef {
    std::string stem;
    std::string path;
};

// ---- format helpers (pure) --------------------------------------------------------------------

// Parse the text of a .cht file.  Never fails on malformed entries (they come
// back unsupported); returns false only when the text holds no cheat at all.
bool parseCht(const std::string& text, std::vector<ChtCheat>& out);
bool loadChtFile(const std::string& path, ChtSet& out);

// `n` bytes at emulated address `addr`, in MEMORY order, as a core code string
// ("%08X:%02X", "%08X:%04X", "%08X:%08X", joined by '+').  Words and longs are
// only used on even addresses (the 68000 cannot do them elsewhere); the rest
// is written byte by byte, so the memory image is identical either way.
std::string memoryCheatToCode(uint32_t addr, const uint8_t* bytes, unsigned n);

// Does the core accept `code`?  Uses the core's own parser (cheat_parse_one) on
// every '+'/newline separated segment, with the core's length limit.
bool validateCode(const std::string& code, unsigned* segments, std::string* why);

// Lower-case alphanumerics only, with (...) and [...] groups removed:
// "Raiden (World) (Rev A)" and "Raiden" both give "raiden".
std::string normalizeTitle(const std::string& s);
// Lower-case [a-z0-9-] only, safe in a file name.
std::string slugify(const std::string& s);

// ---- the game being played ------------------------------------------------------------------------

struct GameIdentity {
    std::string key;       // file-name-safe: "crc-ab12cd34" (carts), "title-<slug>" (discs / no CRC), "file-<slug>"
    std::string title;     // the core database's title ("Tempest 2000"), or ""
    std::string fileStem;  // the loaded file's name without extension
    uint32_t crc = 0;      // cartridge CRC32 (header-normalised), 0 for discs
    bool valid() const { return !key.empty(); }
};

// ---- the manager ---------------------------------------------------------------------------------

struct UserCheat {
    std::string desc;
    std::string code;
    bool enabled = true;
};

class CheatManager {
public:
    // Core sinks.  Both may be null (unit tests substitute recorders).
    std::function<void()> resetSink;
    std::function<void(unsigned index, bool enabled, const char* code)> setSink;
    std::function<void(const std::string&)> warn;
    // RetroAchievements hardcore (#836): returns true while no cheat may reach the
    // core.  apply() then still empties the core's list but sends nothing; the
    // saved selections are neither touched nor forgotten, so they go back in the
    // next apply() after the block lifts.  Null = never blocked (unit tests).
    std::function<bool()> blocked;
    bool blockedNow() const { return blocked && blocked(); }

    // ---- library ----------------------------------------------------------
    // Directories searched (in order) for *.cht files.  A directory named
    // "Atari - Jaguar" inside any of them is searched too, so a checkout of
    // libretro-database/cht can be pointed at directly.  The first directory
    // that holds a given file name wins.
    void setLibraryDirs(const std::vector<std::string>& dirs);
    const std::vector<std::string>& libraryDirs() const { return dirs_; }
    void rescan();
    const std::vector<ChtRef>& library() const { return library_; }

    // Per-game state lives in <dir>/<key>.ini.
    void setStateDir(const std::string& dir) { stateDir_ = dir; }

    // ---- the current game -----------------------------------------------------
    void gameLoaded(const GameIdentity& id);   // loads state, picks the set, applies
    void gameClosed();                         // clears the core's list as well
    bool active() const { return game_.valid(); }
    const GameIdentity& game() const { return game_; }

    // ---- the cheat set --------------------------------------------------------------
    // Library entries whose normalised name equals the game's title or file stem.
    const std::vector<size_t>& matches() const { return matches_; }
    // Library entries whose name merely contains / is contained in the game's
    // (>= 5 characters): offered, never chosen automatically.
    const std::vector<size_t>& suggestions() const { return suggestions_; }
    const std::string& selectedStem() const { return selected_; }
    bool selectSet(const std::string& stem);   // "" = none; persisted; false: not in the library
    const ChtSet* selectedSet() const { return setLoaded_ ? &set_ : nullptr; }
    // When the saved set is gone from the library, or several sets match and none was chosen.
    std::string setStatus() const;

    // ---- toggles (each persists and re-applies) --------------------------------------------
    bool isEnabled(const ChtCheat& c) const { return enabled_.count(c.id) != 0; }
    void setEnabled(const ChtCheat& c, bool on);
    // For scripts / the command line: description (case-insensitive), id, or list position.
    bool setEnabledByName(const std::string& name, bool on);
    void disableAll();
    unsigned enabledCount() const;

    // ---- manual codes (persisted per game) ---------------------------------------------------
    // `code` is a core-format code, several joined by '+' or newlines.
    bool addUser(const std::string& desc, const std::string& code, std::string& err);
    void removeUser(size_t i);
    void setUserEnabled(size_t i, bool on);
    const std::vector<UserCheat>& userCheats() const { return user_; }

    // ---- core sync ----------------------------------------------------------------------------------
    // Reset the core's list and set every enabled cheat, in a fixed order, so
    // overlapping writes ("Part 1" then "Part 2") always land the same way.
    // Called after every load (the core drops its list on unload) and after a
    // state load.
    void apply();
    unsigned appliedSegments() const { return appliedSegments_; }
    static constexpr unsigned kCoreLimit = 256;   // CHEAT_MAX_ENTRIES in src/core/cheat.h

    static constexpr unsigned kUserIndexBase = 10000;

private:
    std::string statePath() const;
    void loadState();
    void touch();                 // writes the per-game file
    void pickSet();
    void loadSelectedSet();

    std::vector<std::string> dirs_;
    std::vector<ChtRef> library_;
    std::string stateDir_;

    GameIdentity game_;
    Config state_;
    std::vector<size_t> matches_, suggestions_;
    std::string selected_;        // the effective set stem ("" = none)
    bool selectedSaved_ = false;  // selected_ came from (or was written to) the state file
    ChtSet set_;
    bool setLoaded_ = false;
    std::map<std::string, bool> enabled_;     // cheat id -> on (only true entries)
    std::vector<UserCheat> user_;
    unsigned appliedSegments_ = 0;
};

}  // namespace vj
