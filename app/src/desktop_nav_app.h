// Controller navigation of the desktop library (#1021): when the library owns the pad, and how pad events reach it.
// The glue between SDL's pad events, a pad's menu-button profile (pad_profile.h), a desktop-side tv::NavInput and the
// DesktopNav focus model (desktop_nav.h).  The library UI (library_ui.cpp) publishes its lists to `nav`, draws the ring
// and executes `pending`.  See docs/app-pad-nav-design.md sections 2 and 10.
#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <vector>

#include "desktop_nav.h"
#include "tv/tv_nav.h"

namespace vj {

class App;

struct DesktopPad {
    tv::NavInput in;
    DesktopNav nav;
    std::vector<DeskCmd> pending;   // commands deskPadFrame took from the nav; library_ui executes and clears them
    bool active = false;            // deskPadActive() as of the last frame
    bool padPopup = false;          // a popup the pad opened is driving ImGui's own nav (set by the popup glue)
    uint32_t seq = 0;               // bumped by every pad press / push this consumed (library_ui: a press while the search box has focus leaves it)
    float scrollY = 0;              // right stick Y, -1 (up) .. +1 (down), as the last event left it
    // The last frame's state, so the edge of any of them starts the nav over (see deskPadFrame).
    bool seenPopup = false, seenShow = false;
    int seenTab = -1;

    DesktopPad() { in.setLongAccept(true); }   // a held Accept is the tile menu: a tap fires on release, the hold at kLongMs
};

// Is the desktop library the owner of the pads right now?  TV mode off and its in-game menu shut, no binding capture,
// the home library or the Game library window showing, no game running (a paused one with the window open is fine),
// and none of Settings / States / Cheats / About / the controls overlay open.
bool deskPadActive(App& app);

// One SDL event.  True = consumed (a mapped press, the left stick, a mapped trigger); a release is fed but still
// reaches the app, as in the TV shell.  Keyboard events are never fed here.  `ms` is SDL_GetTicks().
bool deskPadHandleEvent(App& app, const SDL_Event& e, uint64_t ms);

// Once per UI frame (tvInputFrame): starts the nav over on an edge of active / the library window / the tab / the pad
// popup, then turns the queued intents into commands in `pending`.
void deskPadFrame(App& app, uint64_t ms);

// Between ImGui_ImplSDL3_NewFrame() and ImGui::NewFrame(): ImGui's gamepad navigation is on exactly while a popup the pad opened
// is open (the Manual-mode backend clears HasGamepad every frame, so it is set here), and off otherwise.
void deskPadBeforeNewFrame(App& app);

}  // namespace vj
