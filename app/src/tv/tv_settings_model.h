// TV mode (#991): the rows of the TV Settings screen, built from plain inputs.  Pure -- no App, no ImGui -- so the rules
// (a core option the build does not have is hidden, one a session override pins is read-only, a typed value outside the
// core's list is kept and shown as custom) are unit-tested on their own; AppHost only gathers the inputs.
#pragma once

#include <string>
#include <vector>

#include "aspect_choices.h"
#include "emu_snapshot.h"   // OptionDef
#include "tv/tv_host.h"
#include "tv/tv_mode.h"

namespace vj::tv {

constexpr int kSettingGroups = 4;
const char* settingGroupLabel(int group);   // "Picture" "Speed" "Sound" "Interface"; "" outside 0..3

// One core option as a row.  hidden: no definition (no game has run yet), not visible, locked by the app, or nothing to
// choose from.  readOnly + note when a session override (Netplay, --option) decides what the core reads.  A current value
// outside the list (a typed RISC clock) is appended as "<value> (custom)" and selected.  `hidden` may be null.
SettingRow coreOptionRow(const std::string& id, const std::string& label, int group, const OptionDef* def, const std::string& current,
                         bool visible, bool locked, bool overridden, bool* hidden);
SettingRow onOffRow(const std::string& id, const std::string& label, int group, bool on);
SettingRow volumeRow(float volume);                         // "0 %" .. "100 %", index = round(volume * 10)
SettingRow aspectRow(float aspectOverride, const AspectChoice* table, int n);
// keys: system dark light custom:<name>.  A custom theme that is no longer in the list is still the one in force: it is
// appended and selected (and themeKeyAt() gives "" for it, so picking it changes nothing).
SettingRow themeRow(const std::string& choiceKey, const std::vector<std::string>& customNames);
std::string themeKeyAt(int index, const std::vector<std::string>& customNames);   // "" when out of range
SettingRow tvModeRow(TvSetting stored);                     // Off / Manual / Auto
// "Off" then each preset's file name without its extension.  The preset in force when it is in neither list (a file picked
// in the desktop window) is appended as "<stem> (custom)".
SettingRow shaderRow(const std::string& currentPreset, bool enabled, const std::vector<std::string>& presetPaths);
// The Speed group's only row while no game has run this session (its core options are not known yet): a read-only line, never
// steppable, saying why.
SettingRow speedNoteRow();
SettingRow moreRow();                                       // "More settings..." (opens the desktop window): no choices

// Left / Right (wrap = false: stops at the ends) and Accept (wrap = true).  A row with no choices stays where it is.
int stepIndex(const SettingRow& r, int delta, bool wrap);

}  // namespace vj::tv
