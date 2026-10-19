// Controller navigation of the desktop library (#1021): the ImGui-facing helpers library_ui.cpp uses -- the focus ring,
// "bring into view" scrolling, the right-stick scroll and the hint line.  The focus model is desktop_nav.h and the pad
// glue desktop_nav_app.h; this file only draws and scrolls.  See docs/app-pad-nav-design.md section 6.
#pragma once

#include <string>

#include "imgui.h"

namespace vj {

class App;

// The pad is driving the library right now (it owns the pads and the player used one since the pointer last moved): the
// ring and the hint line show only then.
bool deskPadShown(App& app);

// The focus ring around a tile's art box, in the theme's text colour: round corners, `2.5 * DPI scale` thick, and a
// soft shadow behind it.  Call deskDrawFocusShadow() before the tile's art (it darkens what is under it) and
// deskDrawFocusRing() after the hover outline.
void deskDrawFocusShadow(ImDrawList* dl, ImVec2 a0, ImVec2 a1, float round);
void deskDrawFocusRing(ImDrawList* dl, ImVec2 a0, ImVec2 a1, float round);

// Scroll the current window just enough that [min, max] (screen coordinates) is fully inside it: nothing when it already
// is.  The glue flags a scroll on every focus change, and also when the item under a static index changes, so a plain
// "scroll to the item" would make the view jitter.
void deskEnsureVisibleY(float itemMin, float itemMax);
void deskEnsureVisibleX(float itemMin, float itemMax);

// Take keyboard focus off the search box (a pad press leaves it; pads do not type).
void deskLeaveTextInput();

// The right stick scrolls the current window (the grid): a 0.25 deadzone, then fs * 40 px/s per full deflection.
void deskStickScroll(App& app);

// "A Play  |  hold A Menu  |  Y Favorite  |  LB/RB Tabs", the buttons named as the last-used pad's family labels them
// (SDL's face labels where that pad is open).  A part whose button the profile leaves unbound is dropped.  `states`: the
// States tab, where a shot only loads ("A Load  |  LB/RB Tabs").
std::string deskHintLine(App& app, bool states);

// While the right stick scrolls, the focused grid tile that left the view hands focus to the nearest fully visible one
// (deskNearestVisibleIndex, in the grid window's visible range): returns the index to focus, `index` itself when the stick is
// at rest or the tile is still in view.  The arguments describe the grid's layout in screen coordinates.
int deskFollowStick(App& app, int index, int cols, int count, float row0Top, float pitch, float tileH);

}  // namespace vj
