// Settings > "Reset group" and "Reset all settings..." (#833): the logic behind the buttons.
//
//   vj_settings_reset_test [ROM]
//
// Part 1 (no core): which config key belongs to which group; a group reset erases only that group's keys;
// Reset all needs its confirmation, clears the right keys and leaves the excluded data alone (library
// folders, recents, RetroAchievements login, window size, per-window pad choice ...), with the mapping and
// per-game checkboxes off and on; a secondary window's reset never reaches the shared config.ini (#902).
// Part 2 (the real core, with a ROM): resetting the options of one category resets only that category,
// skips an option the app locks and one a session override pins, and Reset all clears every saved value
// (including one for an option the core never registered) while the skipped ones stay.
#include "config.h"
#include "core_host.h"
#include "settings_reset.h"
#include "typed_options.h"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace vj;
namespace fs = std::filesystem;

static int g_checks = 0, g_fail = 0;
#define CHECK(c)                                                                         \
    do {                                                                                 \
        g_checks++;                                                                      \
        if (!(c)) {                                                                      \
            g_fail++;                                                                    \
            std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #c);            \
        }                                                                                \
    } while (0)

static std::set<std::string> keysOf(const Config& c) {
    std::set<std::string> s;
    for (const auto& kv : c.withPrefix("")) s.insert(kv.first);
    return s;
}

static std::string slurp(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// A config with one key of every kind the app writes.
static void fill(Config& c) {
    const char* keys[] = {
        // Video (+ Shader)
        "video.integer", "video.linear", "video.stretch", "video.aspect", "video.vsync", "ui.show_fps", "ui.theme",
        "shader.enabled", "shader.preset", "shader.folder",
        // Audio
        "audio.volume", "audio.mute", "audio.target_ms",
        // Input devices (global)
        "input.port.0", "input.port.1", "input.slot.0", "input.dev.mouse_speed", "input.dev.deadzone", "port.0",
        "ui.controls.autoshow",
        // Storage + library display
        "saves.autosave", "ui.home_library", "library.art", "library.hide_on_launch", "library.size", "library.section",
        "ui.tray", "ui.keep_awake", "ui.viewports", "ui.menubar_in_window", "ui.emu_thread",
        "ui.tv_mode", "ui.tv_auto_fullscreen", "ui.tv_auto_controller", "ui.tv_manual_on", "ui.tv_hint_shown",
        "library.favorites.show", "library.recents.show", "saves.resume",
        // Discord
        "discord.enabled", "discord.art", "discord.buttons",
        // Mappings
        "input.pad.A", "input.padguid.030000.B", "input.kb.A", "input.kb.port", "input.stick_dpad",
        "input.padguid.030000.nav.family", "input.padguid.030000.nav.confirm",   // a model's menu-button layer (#1021)
        "input.game.disc-myst.pad.A", "input.game.cart-1234.kb.B", "input.game.cart-1234.padguid.0300.C",
        // Per-game device choices
        "input.game.cart-1234.port.0", "input.game.cart-1234.slot.2", "input.game.disc-myst.dev.mouse_speed",
        // Never reset
        "library.dir.0", "library.dir.1", "library.art_mirror", "recent.0", "recent.1", "ui.last_dir", "ui.scale",
        "window.w", "window.h", "input.pads.1", "input.pads.2", "ui.controls.seen_pads", "ra.user", "ra.token", "ra.enabled",
        "ra.hardcore", "discord.app_id", "netplay.port", "netplay.join_addr", "cheats.dir", "user.count", "user.0.desc",
    };
    for (const char* k : keys) c.set(k, "x");
    c.set("opt.virtualjaguar_frameskip_max", "7");
}

static std::set<std::string> klass(const Config& c, KeyClass k) {
    std::set<std::string> s;
    for (const auto& kv : c.withPrefix(""))
        if (classifyKey(kv.first) == k) s.insert(kv.first);
    return s;
}

static void testClassify() {
    CHECK(classifyKey("video.vsync") == KeyClass::Video);
    CHECK(classifyKey("ui.show_fps") == KeyClass::Interface);   // #933: the Interface tab owns the app's own look and behaviour
    CHECK(classifyKey("ui.theme") == KeyClass::Interface);      // Reset on that tab returns the theme to System
    CHECK(classifyKey("ui.controls.autoshow") == KeyClass::Interface);
    CHECK(classifyKey("ui.home_library") == KeyClass::Interface && classifyKey("saves.resume") == KeyClass::Interface);
    CHECK(classifyKey("ui.tray") == KeyClass::Interface && classifyKey("ui.keep_awake") == KeyClass::Interface);
    CHECK(classifyKey("ui.emu_thread") == KeyClass::Interface);   // #967: Settings > Interface > Emulation thread
    for (const char* k : {"ui.tv_mode", "ui.tv_auto_fullscreen", "ui.tv_auto_controller", "ui.tv_manual_on", "ui.tv_hint_shown"})
        CHECK(classifyKey(k) == KeyClass::Interface);   // TV mode (#991): the Interface tab owns it
    CHECK(classifyKey("ui.font.family") == KeyClass::Keep && classifyKey("ui.scale") == KeyClass::Keep);
    CHECK(classifyKey("shader.preset") == KeyClass::Shader);
    CHECK(classifyKey("shader.folder") == KeyClass::Keep);   // a path the user picked
    CHECK(classifyKey("audio.target_ms") == KeyClass::Audio);
    CHECK(classifyKey("input.port.1") == KeyClass::Input);
    CHECK(classifyKey("port.1") == KeyClass::Input);          // the legacy spelling
    CHECK(classifyKey("input.dev.deadzone") == KeyClass::Input);
    CHECK(classifyKey("discord.enabled") == KeyClass::Discord && classifyKey("discord.art") == KeyClass::Discord && classifyKey("discord.buttons") == KeyClass::Discord);
    CHECK(classifyKey("discord.app_id") == KeyClass::Keep);   // an ID the user chose
    CHECK(classifyKey("saves.autosave") == KeyClass::Storage);
    CHECK(classifyKey("library.size") == KeyClass::Interface);
    CHECK(classifyKey("library.favorites.show") == KeyClass::Interface && classifyKey("library.recents.show") == KeyClass::Interface);
    CHECK(classifyKey("library.dir.0") == KeyClass::Keep);
    CHECK(classifyKey("opt.virtualjaguar_x") == KeyClass::CoreOption);
    // The trailing dot is what separates a binding layer from a window's own pad choice.
    CHECK(classifyKey("input.pad.A") == KeyClass::Mapping);
    CHECK(classifyKey("input.pads.1") == KeyClass::Keep);
    CHECK(classifyKey("input.padguid.0300.A") == KeyClass::Mapping);
    CHECK(classifyKey("input.padguid.0300.nav.family") == KeyClass::Mapping);    // the menu-button layer resets with the controls (#1021)
    CHECK(classifyKey("input.padguid.0300.nav.pagenext") == KeyClass::Mapping);
    CHECK(classifyKey("input.kb.port") == KeyClass::Mapping);
    CHECK(classifyKey("input.game.cart-1.kb.A") == KeyClass::Mapping);
    CHECK(classifyKey("input.game.cart-1.padguid.03.A") == KeyClass::Mapping);
    CHECK(classifyKey("input.game.cart-1.port.0") == KeyClass::PerGame);
    CHECK(classifyKey("input.game.cart-1.slot.3") == KeyClass::PerGame);
    CHECK(classifyKey("input.game.cart-1.dev.mouse_speed") == KeyClass::PerGame);
    CHECK(classifyKey("input.game.cart-1") == KeyClass::Keep);
    CHECK(classifyKey("ra.token") == KeyClass::Keep);
    CHECK(classifyKey("window.w") == KeyClass::Keep);
    CHECK(classifyKey("something.new") == KeyClass::Keep);    // unknown keys are safe from a reset
}

static void testGroupReset() {
    for (AppGroup g : {AppGroup::Video, AppGroup::Audio, AppGroup::Input, AppGroup::Storage, AppGroup::Interface, AppGroup::Discord}) {
        Config c;
        fill(c);
        const std::set<std::string> before = keysOf(c);
        std::set<std::string> owned;
        for (KeyClass k : groupClasses(g)) {
            std::set<std::string> part = klass(c, k);
            owned.insert(part.begin(), part.end());
        }
        CHECK(!owned.empty());   // control: the group owns something, so "erased exactly these" can fail
        const size_t n = resetGroupKeys(c, g);
        CHECK(n == owned.size());
        const std::set<std::string> after = keysOf(c);
        for (const std::string& k : owned) CHECK(after.count(k) == 0);
        std::set<std::string> expect = before;
        for (const std::string& k : owned) expect.erase(k);
        CHECK(after == expect);   // nothing outside the group changed
    }
    // Spot checks by name: Audio leaves Video and Input alone; Video takes the shader switch, not the folder.
    Config c;
    fill(c);
    resetGroupKeys(c, AppGroup::Audio);
    CHECK(!c.has("audio.volume") && c.has("video.vsync") && c.has("input.port.0") && c.has("opt.virtualjaguar_frameskip_max"));
    resetGroupKeys(c, AppGroup::Video);
    CHECK(!c.has("shader.enabled") && !c.has("shader.preset") && c.has("shader.folder") && c.has("ui.show_fps") && c.has("ui.theme"));   // the Interface keys are not Video's any more
    resetGroupKeys(c, AppGroup::Input);
    CHECK(!c.has("input.port.0") && !c.has("port.0") && c.has("ui.controls.autoshow"));
    resetGroupKeys(c, AppGroup::Storage);
    CHECK(!c.has("saves.autosave") && c.has("saves.resume") && c.has("library.size") && c.has("ui.home_library"));
    resetGroupKeys(c, AppGroup::Interface);
    CHECK(!c.has("ui.show_fps") && !c.has("ui.theme") && !c.has("ui.controls.autoshow") && !c.has("saves.resume") &&
          !c.has("library.size") && !c.has("ui.tray") && c.has("ui.scale") && c.has("library.dir.0"));
    CHECK(c.has("input.pad.A") && c.has("input.game.cart-1234.port.0") && c.has("input.pads.1"));   // mappings / per game / window pad kept
}

static void testResetAll() {
    // Unconfirmed: nothing happens.
    {
        Config c;
        fill(c);
        const std::set<std::string> before = keysOf(c);
        ResetAllRequest r;
        r.alsoMappings = r.alsoPerGame = true;   // even the destructive boxes do nothing without the confirmation
        CHECK(!r.confirmed);
        CHECK(resetAllAppKeys(c, r) == 0);
        CHECK(keysOf(c) == before);
    }
    const std::set<std::string> kept = {
        "library.dir.0", "library.dir.1", "library.art_mirror", "recent.0", "recent.1", "ui.last_dir", "ui.scale", "window.w",
        "window.h", "input.pads.1", "input.pads.2", "ui.controls.seen_pads", "ra.user", "ra.token", "ra.enabled", "ra.hardcore",
        "discord.app_id", "netplay.port", "netplay.join_addr", "cheats.dir", "user.count", "user.0.desc", "shader.folder",
        "opt.virtualjaguar_frameskip_max"};   // core options go through CoreHost, not Config
    const std::set<std::string> mappings = {"input.pad.A", "input.padguid.030000.B", "input.kb.A", "input.kb.port",
        "input.stick_dpad", "input.padguid.030000.nav.family", "input.padguid.030000.nav.confirm", "input.game.disc-myst.pad.A", "input.game.cart-1234.kb.B", "input.game.cart-1234.padguid.0300.C"};
    const std::set<std::string> perGame = {"input.game.cart-1234.port.0", "input.game.cart-1234.slot.2",
                                           "input.game.disc-myst.dev.mouse_speed"};
    for (int variant = 0; variant < 4; variant++) {
        Config c;
        fill(c);
        ResetAllRequest r;
        r.alsoMappings = variant & 1;
        r.alsoPerGame = (variant & 2) != 0;
        r.confirmed = true;
        resetAllAppKeys(c, r);
        const std::set<std::string> after = keysOf(c);
        for (const std::string& k : kept) CHECK(after.count(k) == 1);
        for (const std::string& k : mappings) CHECK((after.count(k) == 1) == !r.alsoMappings);
        for (const std::string& k : perGame) CHECK((after.count(k) == 1) == !r.alsoPerGame);
        // Every app setting is gone, whatever the boxes say.
        for (const char* k : {"video.integer", "video.vsync", "ui.show_fps", "ui.theme", "shader.enabled", "shader.preset", "audio.volume",
                              "audio.mute", "input.port.0", "input.dev.deadzone", "port.0", "ui.controls.autoshow", "saves.autosave",
                              "ui.home_library", "library.art", "library.hide_on_launch", "library.size", "library.section",
                              "library.favorites.show", "library.recents.show", "saves.resume",
                              "ui.tray", "ui.keep_awake", "ui.viewports", "ui.menubar_in_window", "ui.emu_thread", "discord.enabled", "discord.art",
                              "discord.buttons", "ui.tv_mode", "ui.tv_auto_fullscreen", "ui.tv_auto_controller", "ui.tv_manual_on",
                              "ui.tv_hint_shown"})
            CHECK(after.count(k) == 0);
        // Nothing but these three kinds of key is left.
        for (const std::string& k : after) {
            const bool ok = kept.count(k) || mappings.count(k) || perGame.count(k);
            if (!ok) std::fprintf(stderr, "  left over: %s\n", k.c_str());
            CHECK(ok);
        }
    }
}

// #902: a second window never writes the shared config.ini; a reset there lasts for its session.
static void testSecondaryWindow(const fs::path& tmp) {
    const std::string shared = (tmp / "config.ini").string(), mine = (tmp / "config-2.ini").string();
    {
        Config p;
        p.load(shared);
        fill(p);
        p.save();
    }
    const std::string before = slurp(shared);
    CHECK(!before.empty());

    Config c;
    c.load(shared);
    c.setPath(mine);
    c.setSavePrefixes({"window.", "input.pads."});
    ResetAllRequest r;
    r.alsoMappings = r.alsoPerGame = true;
    r.confirmed = true;
    CHECK(resetAllAppKeys(c, r) > 0);
    c.erase("opt.virtualjaguar_frameskip_max");
    CHECK(!c.has("video.integer"));   // applied to this window's session
    c.save();
    CHECK(slurp(shared) == before);                                        // the shared file is byte-identical
    const std::string own = slurp(mine);
    CHECK(own.find("video.") == std::string::npos && own.find("opt.") == std::string::npos);   // and so is not copied
}

// ---- planner on synthetic definitions -----------------------------------------------------------------------
static OptionDef def(const char* key, const char* cat, const char* desc = "") {
    OptionDef d;
    d.key = key;
    d.categoryKey = cat;
    d.desc = desc;
    d.values.push_back({"a", "A"});
    d.values.push_back({"b", "B"});
    d.defaultValue = "a";
    return d;
}

static void testPlanner() {
    std::vector<OptionDef> defs = {def("k_video1", "video"), def("k_video2", "video"), def("k_video3", "video"),
                                   def("k_video4", "video"), def("k_sys", "system", "Cart BIOS Type (Restart)"),
                                   def("k_top", "")};
    std::map<std::string, std::string> saved = {{"k_video1", "b"}, {"k_video2", "b"}, {"k_video3", "b"}, {"k_sys", "b"},
                                                {"k_top", "b"}, {"k_gone", "b"}};
    auto locked = [](const std::string& k) { return k == "k_video2"; };
    auto pinned = [](const std::string& k) { return k == "k_video3" || k == "k_video4"; };   // k_video4 has no saved value
    const std::string video = "video", sys = "system", top = "";

    OptionResetPlan p = planOptionReset(defs, saved, &video, locked, pinned);
    CHECK(p.reset == std::vector<std::string>({"k_video1"}));
    CHECK(p.skippedLocked == std::vector<std::string>({"k_video2"}));
    CHECK(p.skippedOverridden == std::vector<std::string>({"k_video3", "k_video4"}));
    CHECK(!p.needsRestart);

    p = planOptionReset(defs, saved, &sys, locked, pinned);
    CHECK(p.reset == std::vector<std::string>({"k_sys"}) && p.needsRestart);   // "(Restart)" in its text
    p = planOptionReset(defs, saved, &top, locked, pinned);
    CHECK(p.reset == std::vector<std::string>({"k_top"}));

    // Everything: also the saved value of an option the core never registered; before any load there are no defs.
    p = planOptionReset(defs, saved, nullptr, locked, pinned);
    CHECK(p.reset.size() == 4 && p.skippedLocked.size() == 1 && p.skippedOverridden.size() == 2);
    CHECK(std::find(p.reset.begin(), p.reset.end(), "k_gone") != p.reset.end());
    p = planOptionReset({}, saved, nullptr, nullptr, nullptr);
    CHECK(p.reset.size() == saved.size());
    // A category plan with no definitions can't know what is in the category: nothing.
    p = planOptionReset({}, saved, &video, nullptr, nullptr);
    CHECK(p.reset.empty());
    // Nothing saved, nothing to do (the button is disabled).
    p = planOptionReset(defs, {}, &video, nullptr, nullptr);
    CHECK(p.reset.empty());
}

// ---- the real core --------------------------------------------------------------------------------------------
static std::string g_lockKey;

static const OptionDef* findDef(CoreHost& h, const std::string& key) {
    for (const OptionDef& d : h.optionDefs())
        if (d.key == key) return &d;
    return nullptr;
}

static std::string otherValue(const OptionDef& d, const std::string& cur) {
    for (const OptionValue& v : d.values)
        if (v.value != cur) return v.value;
    return cur;
}

static void testCore(const char* rom, const fs::path& tmp) {
    fs::create_directories(tmp / "saves");
    fs::create_directories(tmp / "system");
    CoreHost& host = CoreHost::instance();
    host.setDirectories((tmp / "system").string() + "/", (tmp / "saves").string() + "/");
    host.inputPoll = []() {};
    host.inputState = [](unsigned, unsigned, unsigned, unsigned) -> int16_t { return 0; };
    host.optionLock = [](const std::string& k, std::string& v) {
        v.clear();
        return !g_lockKey.empty() && k == g_lockKey;
    };
    Config cfg;   // what the app's onOptionChanged writes
    host.onOptionChanged = [&cfg](const std::string& k, const std::string& v, bool erased) {
        if (erased) cfg.erase("opt." + k);
        else cfg.set("opt." + k, v);
    };
    std::string err;
    if (!host.loadContent(rom, err)) {
        std::fprintf(stderr, "FAIL load: %s\n", err.c_str());
        g_fail++;
        return;
    }
    host.runFrame();

    // Pick a category with four plain options (A), and another option elsewhere (B).
    std::map<std::string, std::vector<std::string>> byCat;
    for (const OptionDef& d : host.optionDefs())
        if (!d.categoryKey.empty() && d.values.size() >= 2 && !typedOptionFor(d.key)) byCat[d.categoryKey].push_back(d.key);
    std::string catA, catB;
    for (const auto& kv : byCat)
        if (catA.empty() && kv.second.size() >= 4) catA = kv.first;
    for (const auto& kv : byCat)
        if (kv.first != catA && catB.empty()) catB = kv.first;
    CHECK(!catA.empty() && !catB.empty());
    if (catA.empty() || catB.empty()) return;
    const std::string k1 = byCat[catA][0], k2 = byCat[catA][1], kLock = byCat[catA][2], kPin = byCat[catA][3], k3 = byCat[catB][0];

    g_lockKey = kLock;   // the app locks this one: re-load so the host evaluates the rule
    if (!host.loadContent(rom, err)) { std::fprintf(stderr, "FAIL reload: %s\n", err.c_str()); g_fail++; return; }
    host.runFrame();
    CHECK(host.optionLocked(kLock));

    for (const std::string& k : {k1, k2, kLock, kPin, k3}) {
        const OptionDef* d = findDef(host, k);
        CHECK(d != nullptr);
        if (d) host.setOptionValue(k, otherValue(*d, host.optionValue(*d)));
    }
    const OptionDef* dPin = findDef(host, kPin);
    host.overrideOption(kPin, otherValue(*dPin, dPin->defaultValue));   // a session override pins it
    CHECK(host.optionOverridden(kPin));
    for (const std::string& k : {k1, k2, kLock, kPin, k3}) CHECK(cfg.has("opt." + k));   // control: all five are saved
    const OptionDef* d1 = findDef(host, k1);
    CHECK(host.optionValue(*d1) != d1->defaultValue);

    const unsigned pushes = host.optionPushes();
    OptionResetPlan p = resetCoreOptions(host, &catA);
    CHECK(host.optionPushes() == pushes);   // like the per-option reset, no re-push mid-draw
    CHECK(p.reset.size() == 2);
    CHECK(p.skippedLocked == std::vector<std::string>({kLock}));
    CHECK(p.skippedOverridden == std::vector<std::string>({kPin}));
    CHECK(!cfg.has("opt." + k1) && !cfg.has("opt." + k2));                 // reset: erased from the config
    CHECK(cfg.has("opt." + kLock) && cfg.has("opt." + kPin));             // skipped: still saved
    CHECK(cfg.has("opt." + k3) && host.userOptions().count(k3) == 1);     // another category: untouched
    CHECK(host.optionValue(*findDef(host, k1)) == findDef(host, k1)->defaultValue);
    CHECK(host.optionValue(*findDef(host, kPin)) != findDef(host, kPin)->defaultValue);   // the override still decides
    CHECK(host.optionValue(*findDef(host, k3)) != findDef(host, k3)->defaultValue);

    // Everything, including a saved value for an option the core never registered.
    host.loadUserOptions({{"virtualjaguar_not_an_option", "x"}});
    cfg.set("opt.virtualjaguar_not_an_option", "x");
    p = resetCoreOptions(host, nullptr);
    CHECK(!cfg.has("opt." + k3) && !cfg.has("opt.virtualjaguar_not_an_option"));
    CHECK(host.userOptions().count(kLock) == 1 && host.userOptions().count(kPin) == 1);   // still skipped
    CHECK(host.userOptions().size() == 2);
    // A second reset has nothing left to do.
    p = resetCoreOptions(host, nullptr);
    CHECK(p.reset.empty());
    host.unloadContent();
}

int main(int argc, char** argv) {
    testClassify();
    testGroupReset();
    testResetAll();
    testPlanner();
    std::error_code ec;
    fs::path tmp = fs::temp_directory_path(ec) / ("vj-settings-reset-" + std::to_string((long long)std::time(nullptr)));
    fs::create_directories(tmp, ec);
    testSecondaryWindow(tmp);
    if (argc > 1) testCore(argv[1], tmp);
    else std::printf("settings_reset: no ROM given, core part skipped\n");
    fs::remove_all(tmp, ec);
    std::printf("settings_reset: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
