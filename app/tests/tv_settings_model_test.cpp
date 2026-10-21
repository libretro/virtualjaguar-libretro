// TV mode (#991): the TV Settings rows built from plain inputs (tv_settings_model.h) -- core options with the hide /
// read-only / custom-value rules, the app's own toggles, volume, aspect, theme, TV mode and shader rows, and the step
// arithmetic Left / Right / Accept use.
//
//   vj_tv_settings_model_test
//
// Pure logic: no window, no core, no App.
#include "tv/tv_settings_model.h"

#include <cstdio>
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

// settings_rows_core_option_rules (Review focus 5)
static void testCoreOptionRules() {
    OptionDef d;
    d.key = "virtualjaguar_internal_resolution";
    d.values = {{"1x", "1x"}, {"2x", "2x"}};
    bool hidden = false;
    SettingRow r = coreOptionRow("resolution", "Internal resolution", 0, &d, "2x", true, false, false, &hidden);
    CHECK(!hidden && r.index == 1 && r.choices.size() == 2 && !r.readOnly && r.group == 0);
    CHECK(r.id == "resolution" && r.label == "Internal resolution" && r.note.empty());
    coreOptionRow("resolution", "x", 0, &d, "2x", false, false, false, &hidden);
    CHECK(hidden);   // not visible
    coreOptionRow("resolution", "x", 0, &d, "2x", true, true, false, &hidden);
    CHECK(hidden);   // locked by the app
    r = coreOptionRow("resolution", "x", 0, &d, "2x", true, false, true, &hidden);
    CHECK(!hidden && r.readOnly && !r.note.empty());   // a session override decides what the core reads
    r = coreOptionRow("risc_clock", "x", 1, &d, "137%", true, false, false, &hidden);
    CHECK(!hidden && r.choices.back() == "137% (custom)" && r.index == 2 && r.choices.size() == 3 && r.group == 1);
    coreOptionRow("resolution", "x", 0, nullptr, "", true, false, false, &hidden);
    CHECK(hidden);   // no definition yet (no game has run)
    coreOptionRow("resolution", "x", 0, nullptr, "", true, false, false, nullptr);   // the flag is optional
    // A label of "" shows the value; a label shows instead of it.
    OptionDef e;
    e.values = {{"disabled", ""}, {"enabled", "On (fast)"}};
    r = coreOptionRow("idle_skip", "x", 1, &e, "enabled", true, false, false, &hidden);
    CHECK(!hidden && r.choices[0] == "disabled" && r.choices[1] == "On (fast)" && r.index == 1);
    OptionDef none;
    coreOptionRow("idle_skip", "x", 1, &none, "enabled", true, false, false, &hidden);
    CHECK(hidden);   // nothing to choose from
}

static void testAppRows() {
    CHECK(volumeRow(0.75f).index == 8 && volumeRow(0.75f).choices.size() == 11 && volumeRow(1.0f).choices[10] == "100 %");
    CHECK(volumeRow(0.0f).index == 0 && volumeRow(0.0f).choices[0] == "0 %" && volumeRow(1.0f).index == 10);
    CHECK(volumeRow(0.33f).index == 3 && volumeRow(7.0f).index == 10 && volumeRow(-1.0f).index == 0);   // clamped
    CHECK(volumeRow(0.5f).group == 2 && volumeRow(0.5f).id == "volume");
    CHECK(onOffRow("fps", "Show FPS", 3, true).index == 1 &&
          (onOffRow("fps", "x", 3, false).choices == std::vector<std::string>{"Off", "On"}));
    CHECK(onOffRow("fps", "Show FPS", 3, true).label == "Show FPS" && onOffRow("fps", "x", 3, true).group == 3);
    CHECK(tvModeRow(TvSetting::Auto).index == 2 && tvModeRow(TvSetting::Off).choices[1] == "Manual");
    CHECK(tvModeRow(TvSetting::Manual).index == 1 && tvModeRow(TvSetting::Off).id == "tv_mode" && tvModeRow(TvSetting::Off).group == 3);
    const SettingRow more = moreRow();
    CHECK(more.id == "more" && more.choices.empty() && more.group == 3 && !more.readOnly);
    const SettingRow note = speedNoteRow();   // Speed before a game has run: a line, not a control
    CHECK(note.id == "speed_note" && note.label == "Available once a game has run" && note.group == 1 && note.readOnly && note.choices.empty());
    CHECK(stepIndex(note, +1, true) == 0);
}

static void testAspect() {
    SettingRow a = aspectRow(0.0f, kAspectChoices, kAspectChoiceCount);
    CHECK(a.id == "aspect" && a.group == 0 && a.index == 0 && a.choices.size() == 4 && a.choices[0] == "Core default");
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
    CHECK(t.id == "theme" && t.group == 3);
}

static void testShaderAndStep() {
    SettingRow s = shaderRow("/p/crt-royale.slangp", true, {"/p/crt-royale.slangp", "/u/x/lcd.slangp"});
    CHECK(s.index == 1 && s.choices[0] == "Off" && s.choices[2] == "lcd" && s.choices[1] == "crt-royale" && s.id == "shader" && s.group == 0);
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

static void testGroupLabels() {
    CHECK(std::string(settingGroupLabel(0)) == "Picture" && std::string(settingGroupLabel(1)) == "Speed" &&
          std::string(settingGroupLabel(2)) == "Sound" && std::string(settingGroupLabel(3)) == "Interface");
    CHECK(std::string(settingGroupLabel(4)).empty() && std::string(settingGroupLabel(-1)).empty() && kSettingGroups == 4);
}

int main() {
    testCoreOptionRules();
    testAppRows();
    testAspect();
    testTheme();
    testShaderAndStep();
    testGroupLabels();
    std::printf("tv_settings_model_test: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
