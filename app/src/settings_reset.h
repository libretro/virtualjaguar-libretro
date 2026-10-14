// "Reset group" / "Reset all settings" for the Settings window (#833): which config keys belong to
// which part of Settings, and what a reset touches.  No ImGui and no App, so the headless tests
// (tests/settings_reset_test.cpp) drive exactly the code the buttons run.
//
// Three kinds of state, three rules:
//   * core options  ("opt.<key>" in config.ini, CoreHost::userOptions()): reset through
//     CoreHost::resetOptionToDefault() so the core hears about it.  Options the app locks
//     (CoreHost::optionLocked) and options a session override pins (--option, the Netplay window)
//     are skipped and reported, so the UI can say so.
//   * app settings  (video.*, audio.*, ...): classified by classifyKey(); a reset erases the keys
//     of one class and the caller re-reads the defaults from the (now empty) config, so there is a
//     single place that knows each default (App::applySettingsFromConfig).
//   * everything else (library folders, recents, covers, RetroAchievements login, window size,
//     per-window pad choice, cheats ...) is KeyClass::Keep and no reset ever erases it.
//
// Writes go through Config::erase(), so the multi-instance rule (#902) holds without code here: a
// secondary window's Config::save() writes only its own prefixes, so a reset applies to that
// window's session and is never persisted into the shared config.ini.
#pragma once

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "config.h"
#include "core_host.h"

namespace vj {

enum class KeyClass {
    Keep,        // never reset
    CoreOption,  // opt.*  (handled through CoreHost, never erased from Config directly)
    Video,       // Settings > Video (picture)
    Shader,      // Settings > Video > Shader
    Audio,       // Settings > Audio
    Input,       // Settings > Input > Devices & players, everywhere scope
    Storage,     // Settings > Storage (+ the library display options)
    Discord,     // Settings > Discord (the three toggles; the application ID override is kept)
    Mapping,     // key / button bindings, stick as D-pad, keyboard port (global and per game)
    PerGame,     // per-game device choices (ports, players, tuning): input.game.<id>.* that are not bindings
};

KeyClass classifyKey(const std::string& configKey);

// The app-side groups that have a "Reset group" button: one per Settings tab.  Video also covers
// the Shader section of that tab.
enum class AppGroup { Video, Audio, Input, Storage, Discord };
std::vector<KeyClass> groupClasses(AppGroup g);
const char* groupName(AppGroup g);

// Erases the keys of every class one Settings tab owns.  Returns how many it erased.
size_t resetGroupKeys(Config& cfg, AppGroup g);

// Erases from `cfg` every key of class `c` (never Keep / CoreOption).  Returns how many it erased.
size_t eraseKeyClass(Config& cfg, KeyClass c);

// ---- Reset all ------------------------------------------------------------------------------------
struct ResetAllRequest {
    bool alsoMappings = false;   // key / button bindings, stick as D-pad, keyboard port
    bool alsoPerGame = false;    // per-game controller ports, players and tuning
    bool confirmed = false;      // set only by the modal's Reset button; nothing happens without it
};

// Which key classes the request resets, in the order they are erased.
std::vector<KeyClass> classesFor(const ResetAllRequest& r);

// Erases the app-setting keys the request names.  Does nothing unless r.confirmed.  Returns the count.
size_t resetAllAppKeys(Config& cfg, const ResetAllRequest& r);

// ---- core options ---------------------------------------------------------------------------------------
struct OptionResetPlan {
    std::vector<std::string> reset;               // keys to reset (they hold a saved value)
    std::vector<std::string> skippedLocked;       // the app forces these (Controls / Devices tabs)
    std::vector<std::string> skippedOverridden;   // a session override decides what the core reads
    bool needsRestart = false;                    // some reset option only applies when content is (re)loaded
};

// A core option that only applies on the next load ("Restart required" in its text).
bool optionNeedsRestart(const OptionDef& d);

// What a reset of the options in `categoryKey` would do (nullptr = every option, loaded or not).
// `defs` may be empty (before any game loads); then only a whole reset can be planned, from `saved`.
OptionResetPlan planOptionReset(const std::vector<OptionDef>& defs, const std::map<std::string, std::string>& saved,
                                const std::string* categoryKey, const std::function<bool(const std::string&)>& locked,
                                const std::function<bool(const std::string&)>& overridden);

// Plans against a CoreHost, then resets the planned keys one by one.  Returns the plan that ran.
OptionResetPlan resetCoreOptions(CoreHost& host, const std::string* categoryKey);

}  // namespace vj
