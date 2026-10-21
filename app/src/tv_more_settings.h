// TV mode (#991): "More settings..." -- the desktop Settings window opened from a TV screen (the Settings row on the Base layer, or in
// the in-game menu's Settings slot), driven by the pad and the keyboard through Dear ImGui's own navigation.  The App-side glue next to
// desktop_nav_app.cpp: the TV shell steps aside on the Base layer by itself (a desktop window is open), but with the in-game menu
// open it keeps the input, so while the window is up this takes the pad (and the arrow keys, Enter, Esc) before the shell sees them.
// The menu stays open underneath, holding the pause, and has the pad back the moment the window closes.
//
// "Live" = the window was opened from here (App::tvMoreSettings_), is still open, and the in-game menu is open or TV mode is on with no
// game running.  When the menu closes under it (a Share hold) or a game starts (a drop), it is the plain desktop Settings window.  While
// live: ImGui gamepad + keyboard navigation are on (desktop_nav_popup.h), the window is centred at 80 % of the viewport with
// TV-sized text (ui.cpp drawSettings), and our own NavInput turns the pads into intents for ImGui's gamepad keys.
#pragma once

#include <SDL3/SDL.h>

#include <cstdint>

namespace vj {

class App;

bool tvMoreSettingsLive(App& app);   // app.tvMoreSettings_ && app.showSettings && (TV mode on or the in-game menu open)
void tvMoreSettingsOpen(App& app);   // AppHost::openMoreSettings(): showSettings = true, the flag, a fresh NavInput
// One SDL event, before the shell.  True = consumed: a pad press, the left stick, a bound trigger, and (the menu open) the keys the
// shell would take.  A release is fed but still reaches the app; Start and the intents that are not TV's stay the app's; nothing is
// consumed while a binding is being captured (the Input tab is in the window).  Esc closes the window when it may.
bool tvMoreSettingsHandleEvent(App& app, const SDL_Event& e, uint64_t ms);
// Once per UI frame (tvInputFrame): drops the flag when the window or the context went away, turns the queued intents into ImGui's
// gamepad keys, and closes the window on Back when it may.
void tvMoreSettingsFrame(App& app, uint64_t ms);
// Close the window.  False (nothing changes) when it is not live, an ImGui popup (a combo) is open or an item is active (a text box,
// a slider being tweaked): that Back / Esc belongs to the popup or the item, which leaves it.
bool tvMoreSettingsClose(App& app);
// What ui.cpp multiplies ImGuiStyle::FontSizeBase by while live so the window's text is TV-sized (30u at the viewport height); >= 1.
float tvMoreSettingsFontScale(App& app);

}  // namespace vj
