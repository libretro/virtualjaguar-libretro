#include "tv/tv_settings_model.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace vj::tv {

namespace {

SettingRow rowOf(const std::string& id, const std::string& label, int group, std::vector<std::string> choices, int index) {
    SettingRow r;
    r.id = id;
    r.label = label;
    r.group = group;
    r.choices = std::move(choices);
    r.index = index;
    return r;
}

// The file name without its folders (either separator) and its extension.
std::string stemOf(const std::string& path) {
    const size_t slash = path.find_last_of("/\\");
    std::string s = slash == std::string::npos ? path : path.substr(slash + 1);
    const size_t dot = s.find_last_of('.');
    if (dot != std::string::npos && dot > 0) s.resize(dot);
    return s;
}

}  // namespace

const char* settingGroupLabel(int group) {
    switch (group) {
    case 0: return "Picture";
    case 1: return "Speed";
    case 2: return "Sound";
    case 3: return "Interface";
    }
    return "";
}

SettingRow coreOptionRow(const std::string& id, const std::string& label, int group, const OptionDef* def, const std::string& current,
                         bool visible, bool locked, bool overridden, bool* hidden) {
    SettingRow r = rowOf(id, label, group, {}, 0);
    const bool hide = !def || def->values.empty() || !visible || locked;
    if (hidden) *hidden = hide;
    if (hide) return r;
    int found = -1;
    for (size_t i = 0; i < def->values.size(); i++) {
        const OptionValue& v = def->values[i];
        r.choices.push_back(v.label.empty() ? v.value : v.label);
        if (v.value == current) found = (int)i;
    }
    if (found < 0 && !current.empty()) {   // a typed value the core's list does not have
        r.choices.push_back(current + " (custom)");
        found = (int)r.choices.size() - 1;
    }
    r.index = std::max(found, 0);
    if (overridden) {   // the desktop row is disabled for the same reason (drawOption)
        r.readOnly = true;
        r.note = "Set for this session by Netplay or --option";
    }
    return r;
}

SettingRow onOffRow(const std::string& id, const std::string& label, int group, bool on) {
    return rowOf(id, label, group, {"Off", "On"}, on ? 1 : 0);
}

SettingRow volumeRow(float volume) {
    std::vector<std::string> pct;
    for (int i = 0; i <= 10; i++) pct.push_back(std::to_string(i * 10) + " %");
    return rowOf("volume", "Volume", 2, std::move(pct), std::clamp((int)std::lround(volume * 10.0f), 0, 10));
}

SettingRow aspectRow(float aspectOverride, const AspectChoice* table, int n) {
    std::vector<std::string> labels;
    int sel = 0;   // as the desktop combo: a value that matches none shows the first
    for (int i = 0; i < n; i++) {
        labels.push_back(table[i].label);
        if (std::abs(aspectOverride - table[i].value) < 1e-3f) sel = i;
    }
    return rowOf("aspect", "Aspect ratio", 0, std::move(labels), sel);
}

SettingRow themeRow(const std::string& choiceKey, const std::vector<std::string>& customNames) {
    SettingRow r = rowOf("theme", "Theme", 3, {"System", "Dark", "Light"}, 0);
    for (const std::string& n : customNames) r.choices.push_back("Custom: " + n);
    if (choiceKey == "dark") r.index = 1;
    else if (choiceKey == "light") r.index = 2;
    else if (choiceKey.rfind("custom:", 0) == 0) {
        const std::string name = choiceKey.substr(7);
        const auto it = std::find(customNames.begin(), customNames.end(), name);
        if (it == customNames.end()) r.choices.push_back("Custom: " + name);
        r.index = it == customNames.end() ? (int)r.choices.size() - 1 : 3 + (int)(it - customNames.begin());
    }
    return r;
}

std::string themeKeyAt(int index, const std::vector<std::string>& customNames) {
    if (index == 0) return "system";
    if (index == 1) return "dark";
    if (index == 2) return "light";
    if (index >= 3 && index < 3 + (int)customNames.size()) return "custom:" + customNames[(size_t)index - 3];
    return "";
}

SettingRow tvModeRow(TvSetting stored) {
    return rowOf("tv_mode", "TV mode", 3, {"Off", "Manual", "Auto"}, (int)stored);
}

SettingRow shaderRow(const std::string& currentPreset, bool enabled, const std::vector<std::string>& presetPaths) {
    SettingRow r = rowOf("shader", "Shader", 0, {"Off"}, 0);
    int sel = 0;
    for (size_t i = 0; i < presetPaths.size(); i++) {
        r.choices.push_back(stemOf(presetPaths[i]));
        if (enabled && presetPaths[i] == currentPreset) sel = (int)i + 1;
    }
    if (enabled && !currentPreset.empty() && sel == 0) {
        r.choices.push_back(stemOf(currentPreset) + " (custom)");
        sel = (int)r.choices.size() - 1;
    }
    r.index = sel;
    return r;
}

SettingRow speedNoteRow() {
    SettingRow r = rowOf("speed_note", "Available once a game has run", 1, {}, 0);
    r.readOnly = true;
    return r;
}

SettingRow moreRow() { return rowOf("more", "More settings\xE2\x80\xA6", 3, {}, 0); }

int stepIndex(const SettingRow& r, int delta, bool wrap) {
    const int n = (int)r.choices.size();
    if (n == 0) return r.index;
    const int i = r.index + delta;
    if (wrap) return ((i % n) + n) % n;
    return std::clamp(i, 0, n - 1);
}

}  // namespace vj::tv
