// Settings > "Reset group" buttons and "Reset all settings..." (#833).  The decisions (which keys,
// which options) live in settings_reset.h; this file applies them to the running App and draws the
// buttons and the confirmation.
#pragma once

#include <string>

#include "settings_reset.h"

namespace vj {

class App;

// ---- apply (no ImGui; the tests drive the logic underneath) -------------------------------------------
// Put every setting of one Settings tab back to its default, now, and save (subject to the
// multi-instance policy: Config::save() of a secondary window writes only that window's keys).
void resetAppGroup(App& app, AppGroup g);
// Core options of one category (nullptr = all).  Locked and session-overridden options are skipped.
OptionResetPlan resetCoreOptionGroup(App& app, const std::string* categoryKey);
// Everything the modal lists.  Does nothing unless r.confirmed.
void resetEverything(App& app, const ResetAllRequest& r);

// ---- ImGui ----------------------------------------------------------------------------------------------------
void drawAppGroupReset(App& app, AppGroup g);   // a right-aligned "Reset group" row at the top of a tab
void drawCoreGroupReset(App& app, const std::string& categoryKey, bool filterActive);   // first row in a category
void drawResetAllSettings(App& app);            // "Reset all settings..." button + its modal

// ui.cpp: forget the "Custom..." box state of one option (nullptr = every option) after a reset.
void forgetTypedOptionState(const std::string* key);

}  // namespace vj
