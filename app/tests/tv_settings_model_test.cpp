// TV mode (#991): the TV Settings rows built from plain inputs (tv_settings_model.h) -- every visible core option by the core's
// categories with the hide / read-only / custom-value / restart rules, sliders as stepped lists, the app's own toggles, volume,
// aspect, theme, TV mode and shader rows, the groups, and the step arithmetic Left / Right / Accept use.
//
//   vj_tv_settings_model_test
//
// Pure logic: no window, no core, no App.
#include "tv/tv_settings_model.h"

#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "aspect_choices.h"

using namespace vj;
using namespace vj::tv;

static int g_fail = 0, g_checks = 0;

#define CHECK(c)                                                              \
    do {                                                                      \
        g_checks++;                                                           \
        if (!(c)) {                                                           \
            g_fail++;                                                         \
            std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); \
        }                                                                     \
    } while (0)

static OptionDef mkDef(const char* key, const char* desc, const char* cat, std::vector<OptionValue> values, const char* def = "",
                       const char* info = "") {
    OptionDef d;
    d.key = key;
    d.desc = desc;
    d.categoryKey = cat;
    d.values = std::move(values);
    d.defaultValue = def;
    d.info = info;
    return d;
}

// The rows of a few options under one rule set: all visible, none overridden, nothing typed, every option at its default.
struct Rules {
    std::map<std::string, std::string> value;   // current value by key; absent = the default
    std::set<std::string> hidden, overridden;
    std::map<std::string, TypedOption> typed;
    std::vector<SettingRow> rows(const std::vector<OptionCategory>& cats, const std::vector<OptionDef>& defs) const {
        return coreOptionRows(
            cats, defs,
            [&](const OptionDef& d) {
                auto it = value.find(d.key);
                return it == value.end() ? d.defaultValue : it->second;
            },
            [&](const std::string& k) { return hidden.count(k) == 0; }, [&](const std::string& k) { return overridden.count(k) != 0; },
            [&](const std::string& k) -> const TypedOption* {
                auto it = typed.find(k);
                return it == typed.end() ? nullptr : &it->second;
            });
    }
};

// settings_rows_core_option_rules (Phase 5 decision 4): every visible option is a row, by the core's categories.
static void testCoreOptionRows() {
    const std::vector<OptionCategory> cats = {{"video", "Video", ""}, {"speed", "Speed", ""}};
    OptionDef a = mkDef("a", "Plain flat title", "", {{"off", ""}, {"on", "Switched on"}}, "off");
    OptionDef b = mkDef("b", "Flat title", "video", {{"1x", "1x (native)"}, {"2x", ""}}, "1x");
    b.descCategorized = "Resolution";
    OptionDef clock = mkDef("clock", "Clock", "speed", {{"100%", "1x"}, {"200%", "2x"}}, "100%");
    OptionDef gone = mkDef("gone", "Hidden by a gate", "video", {{"x", ""}}, "x");
    OptionDef none = mkDef("none", "Nothing to choose from", "video", {});
    Rules r;
    r.hidden = {"gone"};
    std::vector<SettingRow> rows = r.rows(cats, {a, b, clock, gone, none});
    CHECK(rows.size() == 3);   // `gone` is hidden, `none` has nothing to pick
    CHECK(rows.size() == 3 && rows[0].id == "core:a" && rows[1].id == "core:b" && rows[2].id == "core:clock");
    CHECK(rows.size() == 3 && rows[0].group == "core:" && rows[0].groupLabel == "General" && rows[1].group == "core:video" &&
          rows[1].groupLabel == "Video" && rows[2].group == "core:speed" && rows[2].groupLabel == "Speed");
    CHECK(rows.size() == 3 && rows[0].label == "Plain flat title" && rows[1].label == "Resolution");   // the categorized title wins
    CHECK(rows.size() == 3 && rows[0].choices == std::vector<std::string>({"off", "Switched on"}) && rows[0].index == 0);   // label, else value
    CHECK(rows.size() == 3 && rows[1].choices == std::vector<std::string>({"1x (native)", "2x"}) && rows[1].index == 0 && !rows[1].readOnly &&
          rows[1].note.empty());
    // The current value picks the index; the groups come out in category order, "General" first whatever the definition order.
    r.value["a"] = "on";
    r.value["clock"] = "200%";
    rows = r.rows(cats, {clock, b, a});
    CHECK(rows.size() == 3 && rows[0].id == "core:a" && rows[0].index == 1 && rows[1].id == "core:b" && rows[2].id == "core:clock" &&
          rows[2].index == 1);
    const std::vector<SettingGroup> g = groupsOf(rows);
    CHECK(g.size() == 3 && g[0].key == "core:" && g[1].key == "core:video" && g[2].key == "core:speed" && g[2].label == "Speed");
    // A category with nothing visible makes no group; an option naming a category the core never defined lists under General.
    r.hidden = {"b"};
    OptionDef stray = mkDef("stray", "Stray", "elsewhere", {{"x", ""}});
    rows = r.rows(cats, {a, b, stray});
    CHECK(rows.size() == 2 && rows[0].id == "core:a" && rows[1].id == "core:stray" && groupsOf(rows).size() == 1);
}

// A current value outside the core's list is kept (a typed RISC clock), shown as custom and selected.
static void testCoreOptionCustomValue() {
    const std::vector<OptionCategory> cats = {{"speed", "Speed", ""}};
    OptionDef clock = mkDef("clock", "RISC overclock", "speed", {{"50%", "0.5x"}, {"100%", "1x (stock)"}}, "100%");
    Rules r;
    r.typed["clock"] = TypedOption{"clock", OptKind::Percent, 25, 400, "%", "25-400"};
    r.value["clock"] = "137%";
    std::vector<SettingRow> rows = r.rows(cats, {clock});
    CHECK(rows.size() == 1 && rows[0].choices.size() == 3 && rows[0].choices.back() == "137% (custom)" && rows[0].index == 2);
    CHECK(rows.size() == 1 && !rows[0].readOnly);   // a typed number is steppable (stepping lands on the presets, the desktop has the box)
    r.value["clock"] = "50%";
    rows = r.rows(cats, {clock});
    CHECK(rows.size() == 1 && rows[0].choices.size() == 2 && rows[0].index == 0);   // a preset is no custom value
    // Presets in numeric order: the custom value sits between its neighbours, so Left / Right reach the nearest presets.
    OptionDef wide = mkDef("clock", "RISC overclock", "speed", {{"50%", "0.5x"}, {"100%", "1x"}, {"200%", "2x"}}, "100%");
    r.value["clock"] = "137%";
    rows = r.rows(cats, {wide});
    CHECK(rows.size() == 1 && rows[0].choices == std::vector<std::string>({"0.5x", "1x", "137% (custom)", "2x"}) && rows[0].index == 2);
    CHECK(coreCustomPos(wide, "137%", &r.typed["clock"]) == 2 && coreCustomPos(wide, "100%", &r.typed["clock"]) == -1 &&
          coreCustomPos(wide, "30%", &r.typed["clock"]) == 0 && coreCustomPos(wide, "", &r.typed["clock"]) == -1);
    // Presets out of order, or no number at all: the custom entry stays last.
    OptionDef jumbled = mkDef("clock", "RISC overclock", "speed", {{"200%", ""}, {"50%", ""}, {"100%", ""}}, "100%");
    CHECK(coreCustomPos(jumbled, "137%", &r.typed["clock"]) == 3 && coreCustomPos(wide, "137%", nullptr) == 3);
}

// An override (Netplay, --option) pins the value: read-only with the reason.  A Host-kind typed key can only be typed.  A "restart"
// in the help is a note, not a lock.
static void testCoreOptionNotes() {
    const std::vector<OptionCategory> cats = {{"network", "Network", ""}};
    OptionDef mode = mkDef("mode", "Network Link", "network", {{"auto", "Automatic"}, {"off", "Off"}}, "auto");
    OptionDef host = mkDef("host", "Link host", "network", {{"192.168.1.2", ""}, {"10.0.0.2", ""}}, "192.168.1.2");
    OptionDef boot = mkDef("boot", "Boot", "network", {{"hle", ""}, {"real", ""}}, "hle", "Restart required: applied when content is loaded.");
    OptionDef quiet = mkDef("quiet", "Quiet", "network", {{"a", ""}, {"b", ""}}, "a", "Takes effect at once.");
    Rules r;
    r.overridden = {"mode"};
    r.typed["host"] = TypedOption{"host", OptKind::Host, 0, 0, "", "name or IPv4 address"};
    std::vector<SettingRow> rows = r.rows(cats, {mode, host, boot, quiet});
    CHECK(rows.size() == 4);
    CHECK(rows.size() == 4 && rows[0].readOnly && !rows[0].note.empty() && rows[0].note.find("Netplay") != std::string::npos);
    CHECK(rows.size() == 4 && rows[1].readOnly && rows[1].note == "Type it in Desktop settings");
    CHECK(rows.size() == 4 && !rows[2].readOnly && rows[2].note == "Applies when the game next starts");
    CHECK(rows.size() == 4 && !rows[3].readOnly && rows[3].note.empty());
    // A typed host that is not one of the presets: shown as custom, still read-only.
    r.value["host"] = "my.pc.local";
    rows = r.rows(cats, {mode, host});
    CHECK(rows.size() == 2 && rows[1].choices.back() == "my.pc.local (custom)" && rows[1].index == 2 && rows[1].readOnly);
}

static void testSliderRows() {
    SettingRow s = sliderRow("audio_buffer", "Audio buffer", "sound", "Sound", 80, 20, 200, 10, "%.0f ms");
    CHECK(s.choices.size() == 19 && s.index == 6 && s.choices[0] == "20 ms" && s.choices[6] == "80 ms" && s.choices.back() == "200 ms");
    CHECK(s.id == "audio_buffer" && s.label == "Audio buffer" && s.group == "sound" && s.groupLabel == "Sound" && !s.readOnly);
    // A value off the grid is inserted between its neighbours and selected: Left and Right step to the nearest grid values.
    s = sliderRow("audio_buffer", "Audio buffer", "sound", "Sound", 85, 20, 200, 10, "%.0f ms");
    CHECK(s.choices.size() == 20 && s.choices[6] == "80 ms" && s.choices[7] == "85 ms (custom)" && s.choices[8] == "90 ms" && s.index == 7);
    CHECK(stepValue(s, 8, 20, 10, 85) == 90 && stepValue(s, 6, 20, 10, 85) == 80);
    // The "(custom)" label keeps the unit the grid labels use ("%%" is one percent sign; "x" and " s" follow the number).
    CHECK(sliderRow("volume", "Volume", "sound", "Sound", 55.5f, 0, 100, 10, "%.0f %%").choices[6] == "55.5 % (custom)");
    CHECK(sliderRow("tile_size", "Tile size", "library", "Library", 1.25f, 0.5f, 2.0f, 0.5f, "%.1fx").choices[2] == "1.25x (custom)");
    CHECK(sliderRow("neuro_interval", "Pause", "neuro", "Neuro", 1.25f, 0.5f, 2.0f, 0.5f, "%.1f s").choices[2] == "1.25 s (custom)");
    CHECK(sliderRow("v", "V", "g", "G", 85, 20, 200, 10, "%.0f").choices[7] == "85 (custom)");   // no unit in the format, none in the label
    CHECK(stepValue(s, 0, 20, 10, 85) == 20 && stepValue(s, 19, 20, 10, 85) == 200);
    CHECK(stepValue(s, 7, 20, 10, 85) == 85);                        // the "(custom)" entry changes nothing
    CHECK(stepValue(s, -1, 20, 10, 85) == 85 && stepValue(s, 20, 20, 10, 85) == 85);   // out of range too
    // The app's own default (64 ms) is off the grid: it sits between 60 and 70, and Left is 60, Right is 70.
    s = sliderRow("audio_buffer", "Audio buffer", "sound", "Sound", 64, 20, 200, 10, "%.0f ms");
    CHECK(s.choices.size() == 20 && s.index == 5 && s.choices[4] == "60 ms" && s.choices[5] == "64 ms (custom)" && s.choices[6] == "70 ms");
    CHECK(stepValue(s, stepIndex(s, -1, false), 20, 10, 64) == 60 && stepValue(s, stepIndex(s, +1, false), 20, 10, 64) == 70);
    // On the grid at either end; a value beyond the ends is custom, last (or first), not clamped.
    CHECK(sliderRow("x", "x", "g", "G", 20, 20, 200, 10, "%.0f").index == 0 && sliderRow("x", "x", "g", "G", 200, 20, 200, 10, "%.0f").index == 18);
    s = sliderRow("x", "x", "g", "G", 230, 20, 200, 10, "%.0f");
    CHECK(s.choices.size() == 20 && s.choices.back() == "230 (custom)" && s.index == 19);
    CHECK(stepValue(s, 18, 20, 10, 230) == 200 && stepValue(s, 19, 20, 10, 230) == 230);
    s = sliderRow("x", "x", "g", "G", 5, 20, 200, 10, "%.0f");
    CHECK(s.choices.size() == 20 && s.choices[0] == "5 (custom)" && s.index == 0 && stepValue(s, 1, 20, 10, 5) == 20);
    CHECK(stepValue(sliderRow("x", "x", "g", "G", 80, 20, 200, 10, "%.0f"), 18, 20, 10, 80) == 200);   // no custom entry: the last index is a grid one
    // Fractions: the tile size (0.5 to 2.5 by 0.1) and the Neuro pause (0 to 10 by 0.5).
    s = sliderRow("tile_size", "Tile size", "library", "Library", 1.0f, 0.5f, 2.5f, 0.1f, "%.1fx");
    CHECK(s.choices.size() == 21 && s.choices[0] == "0.5x" && s.choices[5] == "1.0x" && s.index == 5 && s.choices.back() == "2.5x");
    CHECK(sliderRow("tile_size", "x", "g", "G", 1.3f, 0.5f, 2.5f, 0.1f, "%.1fx").index == 8);   // 1.3f is not exactly 0.5 + 8 * 0.1: still on the grid
    s = sliderRow("tile_size", "x", "g", "G", 1.25f, 0.5f, 2.5f, 0.1f, "%.1fx");
    CHECK(s.choices.size() == 22 && s.choices[8] == "1.25x (custom)" && s.index == 8);
    CHECK(std::fabs(stepValue(s, 9, 0.5f, 0.1f, 1.25f) - 1.3f) < 1e-4f && std::fabs(stepValue(s, 7, 0.5f, 0.1f, 1.25f) - 1.2f) < 1e-4f);
    s = sliderRow("neuro_interval", "Pause", "neuro", "Neuro", 1.0f, 0, 10, 0.5f, "%.1f s");
    CHECK(s.choices.size() == 21 && s.choices[2] == "1.0 s" && s.index == 2 && s.choices[3] == "1.5 s");
    // Stepping uses the same arithmetic as every row.
    s = sliderRow("audio_buffer", "x", "g", "G", 20, 20, 200, 10, "%.0f");
    CHECK(stepIndex(s, -1, false) == 0 && stepIndex(s, -1, true) == 18 && stepIndex(s, 1, false) == 1);
}

static void testChoiceRowAndGroups() {
    SettingRow c = choiceRow("resume", "Resume where I left off", "library", "Library", {"Off", "Ask", "Always"}, 1);
    CHECK(c.id == "resume" && c.label == "Resume where I left off" && c.group == "library" && c.groupLabel == "Library" &&
          c.choices.size() == 3 && c.index == 1 && !c.readOnly && c.note.empty());
    // groupsOf: first-seen order, each key once, the label of the first row that names it.
    std::vector<SettingRow> rows = {choiceRow("a", "A", "interface", "Interface", {"x"}, 0), choiceRow("b", "B", "picture", "Picture", {"x"}, 0),
                                    choiceRow("c", "C", "interface", "Interface", {"x"}, 0), choiceRow("d", "D", "core:video", "Video", {"x"}, 0),
                                    choiceRow("e", "E", "picture", "Picture", {"x"}, 0)};
    const std::vector<SettingGroup> g = groupsOf(rows);
    CHECK(g.size() == 3 && g[0].key == "interface" && g[0].label == "Interface" && g[1].key == "picture" && g[2].key == "core:video" &&
          g[2].label == "Video");
    CHECK(groupsOf({}).empty());
}

static void testAppRows() {
    CHECK(volumeRow(0.7f).index == 7 && volumeRow(0.7f).choices.size() == 11 && volumeRow(1.0f).choices[10] == "100 %");
    CHECK(volumeRow(0.0f).index == 0 && volumeRow(0.0f).choices[0] == "0 %" && volumeRow(1.0f).index == 10);
    // The desktop slider is finer than the list: a volume between two steps is shown as it is, selected, and kept until stepped.
    CHECK(volumeRow(0.75f).index == 8 && volumeRow(0.75f).choices.size() == 12 && volumeRow(0.75f).choices[8] == "75 % (custom)");
    CHECK(volumeRow(7.0f).index == 10 && volumeRow(-1.0f).index == 0);   // out of range: the ends
    CHECK(volumeRow(0.5f).group == "sound" && volumeRow(0.5f).groupLabel == "Sound" && volumeRow(0.5f).id == "volume");
    CHECK(stepValue(volumeRow(0.75f), 9, 0.0f, 10.0f, 75.0f) == 80.0f && stepValue(volumeRow(0.75f), 7, 0.0f, 10.0f, 75.0f) == 70.0f &&
          stepValue(volumeRow(0.75f), 8, 0.0f, 10.0f, 75.0f) == 75.0f);   // Left 70, Right 80, the custom entry itself changes nothing
    CHECK(stepIndex(volumeRow(0.75f), -1, false) == 7 && stepIndex(volumeRow(0.75f), +1, false) == 9);
    CHECK(onOffRow("fps", "Show FPS", "interface", "Interface", true).index == 1 &&
          (onOffRow("fps", "x", "interface", "Interface", false).choices == std::vector<std::string>{"Off", "On"}));
    CHECK(onOffRow("fps", "Show FPS", "interface", "Interface", true).label == "Show FPS" &&
          onOffRow("fps", "x", "interface", "Interface", true).group == "interface" &&
          onOffRow("fps", "x", "interface", "Interface", true).groupLabel == "Interface");
    CHECK(tvModeRow(TvSetting::Auto).index == 2 && tvModeRow(TvSetting::Off).choices[1] == "Manual");
    CHECK(tvModeRow(TvSetting::Manual).index == 1 && tvModeRow(TvSetting::Off).id == "tv_mode" && tvModeRow(TvSetting::Off).group == "interface");
    // The two Auto checkboxes exist only under Auto (#991 phase 4): under Off / Manual they have no effect, so they are not offered.
    CHECK(tvAutoRows(TvSetting::Manual, true, true).empty() && tvAutoRows(TvSetting::Off, true, true).empty());
    const std::vector<SettingRow> a = tvAutoRows(TvSetting::Auto, true, false);
    CHECK((a.size() == 2 && a[0].id == "tv_auto_fs" && a[0].index == 1 && a[1].id == "tv_auto_pad" && a[1].index == 0 && a[0].group == "interface" &&
           a[1].choices == std::vector<std::string>{"Off", "On"}));   // (the braces' comma needs the extra parentheses)
    CHECK(a.size() == 2 && a[0].label == "Auto: in fullscreen" && a[1].label == "Auto: when a controller is used" && a[1].group == "interface" && a[1].groupLabel == "Interface");
    const SettingRow more = moreRow();
    CHECK(more.id == "more" && more.label == "Desktop settings\xE2\x80\xA6" && more.choices.empty() && more.group == "interface" && !more.readOnly);
}

static void testAspect() {
    SettingRow a = aspectRow(0.0f, kAspectChoices, kAspectChoiceCount);
    CHECK(a.id == "aspect" && a.group == "picture" && a.groupLabel == "Picture" && a.index == 0 && a.choices.size() == 4 && a.choices[0] == "Core default");
    CHECK(aspectRow(16.0f / 9.0f, kAspectChoices, kAspectChoiceCount).index == 2);
    CHECK(aspectRow(4.0f / 3.0f + 1e-4f, kAspectChoices, kAspectChoiceCount).index == 1);   // as the desktop combo matches
    CHECK(aspectRow(2.39f, kAspectChoices, kAspectChoiceCount).index == 0);                 // none of them: the first
}

static void testTheme() {
    const std::vector<std::string> names = {"Amber", "Zed"};
    SettingRow t = themeRow("custom:Amber", names);
    CHECK(t.index == 3 && t.choices[3] == "Custom: Amber" && t.choices.size() == 5 && themeKeyAt(1, {}) == "dark");
    CHECK(themeRow("system", names).index == 0 && themeRow("dark", names).index == 1 && themeRow("light", names).index == 2);
    CHECK(themeRow("custom:Zed", names).index == 4 && themeRow("system", {}).choices.size() == 3);
    CHECK(themeKeyAt(0, names) == "system" && themeKeyAt(2, names) == "light" && themeKeyAt(3, names) == "custom:Amber" &&
          themeKeyAt(4, names) == "custom:Zed");
    CHECK(themeKeyAt(5, names).empty() && themeKeyAt(-1, names).empty());
    // A custom theme whose file is gone is still the one in force: it is listed and selected, and picking it does nothing.
    t = themeRow("custom:Gone", names);
    CHECK(t.choices.size() == 6 && t.choices.back() == "Custom: Gone" && t.index == 5 && themeKeyAt(5, names).empty());
    CHECK(t.id == "theme" && t.group == "interface");
}

static void testShaderAndStep() {
    SettingRow s = shaderRow("/p/crt-royale.slangp", true, {"/p/crt-royale.slangp", "/u/x/lcd.slangp"});
    CHECK(s.index == 1 && s.choices[0] == "Off" && s.choices[2] == "lcd" && s.choices[1] == "crt-royale" && s.id == "shader" && s.group == "picture");
    CHECK(shaderRow("/p/crt-royale.slangp", false, {"/p/crt-royale.slangp"}).index == 0);
    CHECK(shaderRow("", true, {"/p/a.slangp"}).index == 0);   // enabled with no preset: nothing to show but Off
    // A preset outside both lists (a file picked in the desktop window) is listed, marked, and selected.
    SettingRow c = shaderRow("C:\\mine\\hand-made.slangp", true, {"/p/a.slangp"});
    CHECK(c.choices.size() == 3 && c.choices[2] == "hand-made (custom)" && c.index == 2);
    CHECK(stepIndex(s, +1, false) == 2 && stepIndex(s, +1, true) == 2);
    s.index = 2;
    CHECK(stepIndex(s, +1, false) == 2 && stepIndex(s, +1, true) == 0 && stepIndex(s, -1, false) == 1);
    s.index = 0;
    CHECK(stepIndex(s, -1, false) == 0 && stepIndex(s, -1, true) == 2);
    CHECK(stepIndex(moreRow(), +1, true) == 0 && stepIndex(moreRow(), -1, false) == 0);   // no choices: nothing moves
}

int main() {
    testCoreOptionRows();
    testCoreOptionCustomValue();
    testCoreOptionNotes();
    testSliderRows();
    testChoiceRowAndGroups();
    testAppRows();
    testAspect();
    testTheme();
    testShaderAndStep();
    std::printf("tv_settings_model_test: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
