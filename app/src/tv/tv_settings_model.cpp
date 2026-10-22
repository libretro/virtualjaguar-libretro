#include "tv/tv_settings_model.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>

namespace vj::tv {

namespace {

SettingRow rowOf(const std::string& id, const std::string& label, const std::string& group, const std::string& groupLabel,
                 std::vector<std::string> choices, int index) {
    SettingRow r;
    r.id = id;
    r.label = label;
    r.group = group;
    r.groupLabel = groupLabel;
    r.choices = std::move(choices);
    r.index = index;
    return r;
}

constexpr const char* kCustomSuffix = " (custom)";

bool endsWith(const std::string& s, const char* suffix) {
    const size_t n = std::char_traits<char>::length(suffix);
    return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

bool mentionsRestart(const std::string& text) {
    std::string t;
    for (char c : text) t.push_back((char)std::tolower((unsigned char)c));
    return t.find("restart") != std::string::npos;
}

// "%g": 85 -> "85", 0.55 -> "0.55" (the value of a slider position that is not on the list's grid).
std::string plainNumber(float v) {
    char b[32];
    std::snprintf(b, sizeof b, "%g", (double)v);
    return b;
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

namespace {

// The number a typed option's value stands for ("200%", "8000"); false for a Host key or text that is not one.
bool typedNumber(const TypedOption& t, const std::string& v, long& out) {
    if (t.kind == OptKind::Percent) return parseTypedPercent(v, out);
    if (t.kind == OptKind::Int) return parseTypedInt(v, out);
    return false;
}

// One option as a row of `group`; empty (`ok` false) when it has no row.
SettingRow coreRow(const OptionDef& d, const std::string& group, const std::string& groupLabel, const std::string& current,
                   bool overridden, const TypedOption* typed, bool* ok) {
    SettingRow r = rowOf("core:" + d.key, !d.descCategorized.empty() ? d.descCategorized : d.desc, group, groupLabel, {}, 0);
    *ok = !d.values.empty();
    if (!*ok) return r;
    int found = -1;
    for (size_t i = 0; i < d.values.size(); i++) {
        const OptionValue& v = d.values[i];
        r.choices.push_back(v.label.empty() ? v.value : v.label);
        if (v.value == current) found = (int)i;
    }
    const int custom = coreCustomPos(d, current, typed);
    if (custom >= 0) {   // a value the core's list does not have (a typed RISC clock), where stepping from it goes both ways
        r.choices.insert(r.choices.begin() + custom, current + kCustomSuffix);
        found = custom;
    }
    r.index = std::max(found, 0);
    if (overridden) {   // the desktop row is disabled for the same reason (drawOption)
        r.readOnly = true;
        r.note = "Set by Netplay or --option for this session";
    } else if (typed && typed->kind == OptKind::Host) {   // only ever typed: the desktop window has the box
        r.readOnly = true;
        r.note = "Type it in Desktop settings";
    } else if (mentionsRestart(d.info) || mentionsRestart(d.infoCategorized)) {
        r.note = "Applies when the game next starts";
    }
    return r;
}

}  // namespace

int coreCustomPos(const OptionDef& d, const std::string& current, const TypedOption* typed) {
    if (current.empty()) return -1;
    for (const OptionValue& v : d.values)
        if (v.value == current) return -1;
    const int last = (int)d.values.size();   // text, a Host key, or presets that are not in numeric order: the entry goes last
    long cur = 0;
    if (!typed || !typedNumber(*typed, current, cur)) return last;
    long prev = 0;
    int pos = last;
    for (int i = 0; i < last; i++) {
        long v = 0;
        if (!typedNumber(*typed, d.values[(size_t)i].value, v) || (i > 0 && v < prev)) return last;
        prev = v;
        if (pos == last && v > cur) pos = i;
    }
    return pos;
}

std::vector<SettingRow> coreOptionRows(const std::vector<OptionCategory>& cats, const std::vector<OptionDef>& defs,
                                       const std::function<std::string(const OptionDef&)>& current,
                                       const std::function<bool(const std::string&)>& visible,
                                       const std::function<bool(const std::string&)>& overridden,
                                       const std::function<const TypedOption*(const std::string&)>& typed) {
    std::vector<SettingRow> rows;
    auto add = [&](const OptionDef& d, const std::string& group, const std::string& label) {
        if (visible && !visible(d.key)) return;
        bool ok = false;
        SettingRow r = coreRow(d, group, label, current ? current(d) : d.defaultValue, overridden && overridden(d.key),
                               typed ? typed(d.key) : nullptr, &ok);
        if (ok) rows.push_back(std::move(r));
    };
    auto known = [&](const std::string& key) {
        for (const OptionCategory& c : cats)
            if (c.key == key) return true;
        return false;
    };
    for (const OptionDef& d : defs)   // no category, or one the core never defined: the desktop lists those under General too
        if (d.categoryKey.empty() || !known(d.categoryKey)) add(d, "core:", "General");
    for (const OptionCategory& c : cats)
        for (const OptionDef& d : defs)
            if (d.categoryKey == c.key) add(d, "core:" + c.key, c.desc);
    return rows;
}

SettingRow choiceRow(const std::string& id, const std::string& label, const std::string& group, const std::string& groupLabel,
                     std::vector<std::string> choices, int index) {
    return rowOf(id, label, group, groupLabel, std::move(choices), index);
}

SettingRow sliderRow(const std::string& id, const std::string& label, const std::string& group, const std::string& groupLabel,
                     float current, float lo, float hi, float step, const char* fmt) {
    std::vector<std::string> labels;
    const int n = step > 0 && hi >= lo ? (int)std::lround((hi - lo) / step) + 1 : 1;
    char b[64];
    for (int i = 0; i < n; i++) {
        std::snprintf(b, sizeof b, fmt, (double)(lo + (float)i * step));
        labels.push_back(b);
    }
    const int at = step > 0 ? (int)std::lround((current - lo) / step) : 0;
    int index = -1;
    if (at >= 0 && at < n && std::fabs(current - (lo + (float)at * step)) <= step * 1e-3f) index = at;
    if (index < 0) {   // off the grid (the desktop slider is finer): shown as it is, between its neighbours, selected, kept until the player steps
        int pos = n;
        for (int i = 0; i < n; i++)
            if (lo + (float)i * step > current) {
                pos = i;
                break;
            }
        labels.insert(labels.begin() + pos, plainNumber(current) + kCustomSuffix);
        index = pos;
    }
    return rowOf(id, label, group, groupLabel, std::move(labels), index);
}

float stepValue(const SettingRow& r, int index, float lo, float step, float current) {
    int custom = -1;   // where sliderRow put the "(custom)" entry, if it did
    for (size_t i = 0; i < r.choices.size(); i++)
        if (endsWith(r.choices[i], kCustomSuffix)) custom = (int)i;
    const int grid = (int)r.choices.size() - (custom >= 0 ? 1 : 0);
    if (index < 0 || index >= (int)r.choices.size() || index == custom) return current;   // the "(custom)" entry: nothing to change
    const int g = custom >= 0 && index > custom ? index - 1 : index;
    return g < grid ? lo + (float)g * step : current;
}

std::vector<SettingGroup> groupsOf(const std::vector<SettingRow>& rows) {
    std::vector<SettingGroup> out;
    for (const SettingRow& r : rows) {
        bool seen = false;
        for (const SettingGroup& g : out)
            if (g.key == r.group) {
                seen = true;
                break;
            }
        if (!seen) out.push_back({r.group, r.groupLabel});
    }
    return out;
}

SettingRow onOffRow(const std::string& id, const std::string& label, const std::string& group, const std::string& groupLabel, bool on) {
    return rowOf(id, label, group, groupLabel, {"Off", "On"}, on ? 1 : 0);
}

SettingRow volumeRow(float volume) {
    return sliderRow("volume", "Volume", "sound", "Sound", std::round(std::clamp(volume, 0.0f, 1.0f) * 1000.0f) / 10.0f, 0.0f, 100.0f, 10.0f, "%.0f %%");
}

SettingRow aspectRow(float aspectOverride, const AspectChoice* table, int n) {
    std::vector<std::string> labels;
    int sel = 0;   // as the desktop combo: a value that matches none shows the first
    for (int i = 0; i < n; i++) {
        labels.push_back(table[i].label);
        if (std::abs(aspectOverride - table[i].value) < 1e-3f) sel = i;
    }
    return rowOf("aspect", "Aspect ratio", "picture", "Picture", std::move(labels), sel);
}

SettingRow themeRow(const std::string& choiceKey, const std::vector<std::string>& customNames) {
    SettingRow r = rowOf("theme", "Theme", "interface", "Interface", {"System", "Dark", "Light"}, 0);
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
    return rowOf("tv_mode", "TV mode", "interface", "Interface", {"Off", "Manual", "Auto"}, (int)stored);
}

std::vector<SettingRow> tvAutoRows(TvSetting stored, bool autoFullscreen, bool autoController) {
    if (stored != TvSetting::Auto) return {};
    return {onOffRow("tv_auto_fs", "Auto: in fullscreen", "interface", "Interface", autoFullscreen),
            onOffRow("tv_auto_pad", "Auto: when a controller is used", "interface", "Interface", autoController)};
}

SettingRow shaderRow(const std::string& currentPreset, bool enabled, const std::vector<std::string>& presetPaths) {
    SettingRow r = rowOf("shader", "Shader", "picture", "Picture", {"Off"}, 0);
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

SettingRow moreRow() { return rowOf("more", "Desktop settings\xE2\x80\xA6", "interface", "Interface", {}, 0); }

int stepIndex(const SettingRow& r, int delta, bool wrap) {
    const int n = (int)r.choices.size();
    if (n == 0) return r.index;
    const int i = r.index + delta;
    if (wrap) return ((i % n) + n) % n;
    return std::clamp(i, 0, n - 1);
}

}  // namespace vj::tv
