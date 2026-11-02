#include "settings_reset.h"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace vj {

namespace {

bool startsWith(const std::string& s, const char* p) { return s.compare(0, std::strlen(p), p) == 0; }

struct Exact {
    const char* key;
    KeyClass cls;
};

// Exact app-setting keys.  Anything not listed here or in classifyKey()'s prefixes is Keep: a new
// config key is safe from a reset until someone decides it belongs to a group.
const Exact kExact[] = {
    {"video.integer", KeyClass::Video},       {"video.linear", KeyClass::Video},
    {"video.stretch", KeyClass::Video},       {"video.aspect", KeyClass::Video},
    {"video.vsync", KeyClass::Video},         {"video.pacing", KeyClass::Video},
    // shader.folder is a path the user picked, like a library folder: kept.
    {"shader.enabled", KeyClass::Shader},     {"shader.preset", KeyClass::Shader},
    {"audio.volume", KeyClass::Audio},        {"audio.mute", KeyClass::Audio},
    {"audio.target_ms", KeyClass::Audio},
    {"input.stick_dpad", KeyClass::Mapping},  {"input.kb.port", KeyClass::Mapping},
    {"saves.autosave", KeyClass::Storage},
    // Settings > Interface: the app's own look and behaviour (the keys keep their pre-#933 names).
    {"ui.show_fps", KeyClass::Interface},     {"ui.theme", KeyClass::Interface},      {"ui.controls.autoshow", KeyClass::Interface},
    {"ui.home_library", KeyClass::Interface}, {"saves.resume", KeyClass::Interface},
    {"ui.tray", KeyClass::Interface},         {"ui.keep_awake", KeyClass::Interface},   // Interface > Desktop
    {"ui.tv_mode", KeyClass::Interface},      {"ui.tv_auto_fullscreen", KeyClass::Interface},   // Interface > TV mode (#991)
    {"ui.tv_auto_controller", KeyClass::Interface}, {"ui.tv_manual_on", KeyClass::Interface},
    {"ui.tv_hint_shown", KeyClass::Interface},
    {"ui.fullscreen_autohide_menu", KeyClass::Interface},
    {"ui.viewports", KeyClass::Interface},    {"ui.menubar_in_window", KeyClass::Interface},
    {"ui.emu_thread", KeyClass::Interface},   // Interface > Performance > Emulation thread (#967)
    {"library.art", KeyClass::Interface},
    {"library.hide_on_launch", KeyClass::Interface}, {"library.size", KeyClass::Interface},
    {"library.section", KeyClass::Interface},
    {"library.favorites.show", KeyClass::Interface}, {"library.recents.show", KeyClass::Interface},
    // discord.app_id is an ID the user chose, like a path: kept.
    {"discord.enabled", KeyClass::Discord},   {"discord.art", KeyClass::Discord},   {"discord.buttons", KeyClass::Discord},
};

}  // namespace

KeyClass classifyKey(const std::string& k) {
    if (startsWith(k, "opt.")) return KeyClass::CoreOption;
    for (const Exact& e : kExact)
        if (k == e.key) return e.cls;
    // Trailing dots matter: "input.pads.N" is a window's own pad choice (kept), "input.pad." a binding layer.
    if (startsWith(k, "input.game.")) {
        // input.game.<id>.<rest>; the id is a slug (no dots), see contentIdentity().
        size_t dot = k.find('.', std::strlen("input.game."));
        if (dot == std::string::npos) return KeyClass::Keep;
        const std::string rest = k.substr(dot + 1);
        if (startsWith(rest, "pad.") || startsWith(rest, "padguid.") || startsWith(rest, "kb.")) return KeyClass::Mapping;
        if (startsWith(rest, "port.") || startsWith(rest, "slot.") || startsWith(rest, "dev.")) return KeyClass::PerGame;
        return KeyClass::Keep;
    }
    if (startsWith(k, "input.pad.") || startsWith(k, "input.padguid.") || startsWith(k, "input.kb.")) return KeyClass::Mapping;
    // The global device choices; "port.N" is the legacy spelling of input.port.N.
    if (startsWith(k, "input.port.") || startsWith(k, "input.slot.") || startsWith(k, "input.dev.") || startsWith(k, "port."))
        return KeyClass::Input;
    return KeyClass::Keep;
}

std::vector<KeyClass> groupClasses(AppGroup g) {
    switch (g) {
    case AppGroup::Video: return {KeyClass::Video, KeyClass::Shader};
    case AppGroup::Audio: return {KeyClass::Audio};
    case AppGroup::Input: return {KeyClass::Input};
    case AppGroup::Storage: return {KeyClass::Storage};
    case AppGroup::Interface: return {KeyClass::Interface};
    case AppGroup::Discord: return {KeyClass::Discord};
    }
    return {};
}

const char* groupName(AppGroup g) {
    switch (g) {
    case AppGroup::Video: return "Video";
    case AppGroup::Audio: return "Audio";
    case AppGroup::Input: return "Input";
    case AppGroup::Storage: return "Storage";
    case AppGroup::Interface: return "Interface";
    case AppGroup::Discord: return "Discord";
    }
    return "";
}

size_t eraseKeyClass(Config& cfg, KeyClass c) {
    if (c == KeyClass::Keep || c == KeyClass::CoreOption) return 0;
    std::vector<std::string> doomed;
    for (const auto& kv : cfg.withPrefix(""))
        if (classifyKey(kv.first) == c) doomed.push_back(kv.first);
    for (const std::string& k : doomed) cfg.erase(k);
    return doomed.size();
}

size_t resetGroupKeys(Config& cfg, AppGroup g) {
    size_t n = 0;
    for (KeyClass c : groupClasses(g)) n += eraseKeyClass(cfg, c);
    return n;
}

std::vector<KeyClass> classesFor(const ResetAllRequest& r) {
    std::vector<KeyClass> v = {KeyClass::Video, KeyClass::Shader, KeyClass::Audio, KeyClass::Input, KeyClass::Storage, KeyClass::Interface, KeyClass::Discord};
    if (r.alsoMappings) v.push_back(KeyClass::Mapping);
    if (r.alsoPerGame) v.push_back(KeyClass::PerGame);
    return v;
}

size_t resetAllAppKeys(Config& cfg, const ResetAllRequest& r) {
    if (!r.confirmed) return 0;
    size_t n = 0;
    for (KeyClass c : classesFor(r)) n += eraseKeyClass(cfg, c);
    return n;
}

// ---- core options ----------------------------------------------------------------------------------------

bool optionNeedsRestart(const OptionDef& d) {
    auto has = [](const std::string& s) {
        std::string l = s;
        for (char& c : l) c = (char)std::tolower((unsigned char)c);
        return l.find("restart") != std::string::npos;
    };
    return has(d.desc) || has(d.descCategorized) || has(d.info) || has(d.infoCategorized);
}

OptionResetPlan planOptionReset(const std::vector<OptionDef>& defs, const std::map<std::string, std::string>& saved,
                                const std::string* categoryKey, const std::function<bool(const std::string&)>& locked,
                                const std::function<bool(const std::string&)>& overridden) {
    OptionResetPlan p;
    for (const auto& kv : saved) {   // saved values only: nothing to do for an option that is already at its default
        const OptionDef* def = nullptr;
        for (const OptionDef& d : defs)
            if (d.key == kv.first) { def = &d; break; }
        if (categoryKey && (!def || def->categoryKey != *categoryKey)) continue;
        if (locked && locked(kv.first)) { p.skippedLocked.push_back(kv.first); continue; }
        if (overridden && overridden(kv.first)) { p.skippedOverridden.push_back(kv.first); continue; }
        p.reset.push_back(kv.first);
        if (def && optionNeedsRestart(*def)) p.needsRestart = true;
    }
    // A pinned option with no saved value has nothing to reset, but it is what the user sees greyed out
    // in the group, so it is reported too.
    if (overridden) {
        for (const OptionDef& d : defs) {
            if (categoryKey && d.categoryKey != *categoryKey) continue;
            if (saved.count(d.key) || (locked && locked(d.key)) || !overridden(d.key)) continue;
            p.skippedOverridden.push_back(d.key);
        }
    }
    return p;
}

OptionResetPlan resetCoreOptions(CoreHost& host, const std::string* categoryKey) {
    // The plan is a copy of the keys: resetOptionToDefault() erases from userOptions() as it goes.
    OptionResetPlan p = planOptionReset(
        host.optionDefs(), host.userOptions(), categoryKey, [&](const std::string& k) { return host.optionLocked(k); },
        [&](const std::string& k) { return host.optionOverridden(k); });
    for (const std::string& k : p.reset) host.resetOptionToDefault(k);
    return p;
}

}  // namespace vj
