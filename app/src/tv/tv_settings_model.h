// TV mode (#991): the rows of the TV Settings screen, built from plain inputs.  Pure -- no App, no ImGui -- so the rules
// (a core option the build does not have is hidden, one a session override pins is read-only, a typed value outside the
// core's list is kept and shown as custom) are unit-tested on their own; AppHost only gathers the inputs.
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "aspect_choices.h"
#include "emu_snapshot.h"   // OptionDef, OptionCategory
#include "typed_options.h"
#include "tv/tv_host.h"
#include "tv/tv_mode.h"

namespace vj::tv {

// A rail item: its key (SettingRow::group) and what the rail shows (SettingRow::groupLabel).
struct SettingGroup {
    std::string key, label;
};

// Every visible core option as a row, in category order: the options with no category first under {"core:", "General"}
// (an option naming a category the core never defined counts as none), then each category in definition order under
// {"core:" + cat.key, cat.desc}; a category with nothing to show makes no group.  id = "core:" + key, label = the
// categorized title (else the flat one), value = the choice's label (else its value).  The rules of decision 4:
//   * not visible (the core's own gates and the app's locks both end in `visible`), or nothing to choose from: no row;
//   * `overridden` (Netplay, --option): read-only, with the note that says so;
//   * a value that is not one of the core's presets (a typed RISC clock): "<value> (custom)" is selected, placed by
//     coreCustomPos (between its numeric neighbours for an ordered typed number, else last);
//   * a Host-kind typed key (the Network Link address) is read-only: it can only be typed, in the desktop window;
//   * an option whose help says it needs a restart carries the note "Applies when the game next starts".
// `current`, `visible`, `overridden` and `typed` are per option, so the model never sees CoreHost.
std::vector<SettingRow> coreOptionRows(const std::vector<OptionCategory>& cats, const std::vector<OptionDef>& defs,
                                       const std::function<std::string(const OptionDef&)>& current,
                                       const std::function<bool(const std::string&)>& visible,
                                       const std::function<bool(const std::string&)>& overridden,
                                       const std::function<const TypedOption*(const std::string&)>& typed);
// Where the "(custom)" entry of an option row sits in its choices (and the row's index), or -1 when `current` is one of the
// core's values or empty.  A typed numeric option whose presets run in numeric order gets it between its neighbours, so Left and
// Right step to the nearest presets; any other (a text, an address, presets out of order) gets it last.  AppHost maps a stepped
// index back to `def.values` with it.
int coreCustomPos(const OptionDef& d, const std::string& current, const TypedOption* typed);
SettingRow choiceRow(const std::string& id, const std::string& label, const std::string& group, const std::string& groupLabel,
                     std::vector<std::string> choices, int index);
// A slider as a stepped list: "lo", "lo+step", ... "hi" printed with fmt ("%.0f ms"); a value off the grid is inserted between
// its grid neighbours (last when beyond "hi") as "<value> (custom)" and selected, so Left / Right step to the nearest grid values.
// stepValue() maps an index back (the "(custom)" index gives `current` unchanged).
SettingRow sliderRow(const std::string& id, const std::string& label, const std::string& group, const std::string& groupLabel,
                     float current, float lo, float hi, float step, const char* fmt);
float stepValue(const SettingRow& r, int index, float lo, float step, float current);
std::vector<SettingGroup> groupsOf(const std::vector<SettingRow>& rows);   // first-seen order, keys unique

SettingRow onOffRow(const std::string& id, const std::string& label, const std::string& group, const std::string& groupLabel, bool on);
SettingRow volumeRow(float volume);                         // Sound: "0 %" .. "100 %" in tens (a volume between two is shown as custom)
SettingRow aspectRow(float aspectOverride, const AspectChoice* table, int n);   // Picture
// keys: system dark light custom:<name>.  A custom theme that is no longer in the list is still the one in force: it is
// appended and selected (and themeKeyAt() gives "" for it, so picking it changes nothing).
SettingRow themeRow(const std::string& choiceKey, const std::vector<std::string>& customNames);   // Interface
std::string themeKeyAt(int index, const std::vector<std::string>& customNames);   // "" when out of range
SettingRow tvModeRow(TvSetting stored);                     // Interface: Off / Manual / Auto
// The two "Auto: ..." On / Off rows (tv_auto_fs, tv_auto_pad; Interface) that follow the TV mode row: offered only while the
// stored setting is Auto, since they decide nothing under Off or Manual.  Empty otherwise.
std::vector<SettingRow> tvAutoRows(TvSetting stored, bool autoFullscreen, bool autoController);
// "Off" then each preset's file name without its extension.  The preset in force when it is in neither list (a file picked
// in the desktop window) is appended as "<stem> (custom)".  Picture.
SettingRow shaderRow(const std::string& currentPreset, bool enabled, const std::vector<std::string>& presetPaths);
SettingRow moreRow();                                       // Interface: "Desktop settings..." (opens the desktop window): no choices

// Left / Right (wrap = false: stops at the ends) and Accept (wrap = true).  A row with no choices stays where it is.
int stepIndex(const SettingRow& r, int delta, bool wrap);

}  // namespace vj::tv
