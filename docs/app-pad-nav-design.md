# Controller navigation of the desktop library: design

Status: design for review (2026-10-17). Issue #1021, milestone v4.0.0. Scope: `app/` only.
Related: TV mode spec `docs/app-tv-mode-design.md` (#991), the TV-mode shortcut #1020.

## 1. Intent

A player holding a pad at the desktop library (TV mode off) should be able to do the basics without
reaching for the mouse: browse the games, start one, open a tile's menu, switch tabs, and switch to
TV mode. TV mode stays the full couch experience; this is "the desktop UI works with a pad as well as
it allows". Mouse and keyboard behaviour do not change.

Button names and confirm/back placement follow the controller family (Xbox, PlayStation, Switch,
MFi/generic), and a player can override the buttons for one controller model.

**Goals**
- Pad focus over the desktop library's cover grid, the Favorites / Recently played strips, the BIOS
  tab and the States tab, with a visible focus ring.
- The button map in section 3, from built-in family profiles detected through SDL.
- A user-editable custom profile per controller model (SDL GUID).
- The same profile drives TV mode's navigation, so a Switch pad confirms with its A in both UIs.

**Non-goals**
- Pad navigation of the rest of the desktop UI (menu bar, toolbar widgets, Settings window, BIOS
  settings, cheats). Start opens Settings only when phase 3's pad-driven "More settings" exists; until
  then Start is unbound in the desktop library (section 3).
- Keyboard navigation of the desktop library (arrows stay unbound there; "keyboard unchanged").
- An on-screen keyboard. Back/View focuses the search box for a physical keyboard; nothing more.

**Anti-goals (a failure even if it works)**
- A pad press reaching a game that starts or resumes (the A that launched it must not also press A).
- Any change to mouse or keyboard behaviour in the desktop UI, or to the game's own bindings.
- A second navigation system: the focus model and intents are TV mode's (`app/src/tv/tv_focus.h`,
  `tv_nav.h`), not ImGui's spatial navigation.
- A Switch player who learnt "A on the right confirms" in TV mode finding it backwards on the desktop,
  or the other way round.

## 2. When the desktop library owns the pad

Desktop pad navigation is **active** when all of these hold:
- TV mode is off and the TV in-game menu is not open;
- no game is loaded, or a game is loaded but paused and the Game library window (Ctrl+L) is open
  (my call: a running game always owns its pad);
- the home library is showing (`App::homeVisible()`) or the Game library window is open;
- no bind capture is in progress, and no other desktop window (Settings, States, Cheats, About, the
  controls overlay) is open. A popup opened by the library itself (the tile menu, the BIOS popup, the
  delete-state modal) does not count: it is driven as section 4 says.

**TV mode's Auto trigger wins (user decision).** With the TV setting Auto and its "when a controller
is used" trigger on (phase 4, not built yet), a pad press in the desktop library turns TV mode on, as
`docs/app-tv-mode-design.md` §8 says. Desktop pad navigation serves Off, Manual, and Auto with that
trigger unticked. Today nothing calls `TvMode::onPadPress`, so this PR only has to leave the press
available to it: the desktop consumer must not swallow a press before phase 4's hook would see it.
Write it as: the auto check runs first, and desktop nav sees the press only when it did not switch.

The existing app actions keep their meaning: the TV-menu action (Share hold, Guide hold, paddles, F4)
toggles TV mode with no game; C_HELP (touchpad) is untouched.

## 3. Button map

Positions are named Xbox-style here; the family profile maps physical buttons to these intents.

| Input | Intent | Desktop library |
|---|---|---|
| D-pad / left stick | Up/Down/Left/Right | Move focus. TV mode's repeat: 350 ms, then every 80 ms |
| Confirm, tap | Accept | Play the focused game; on a BIOS tile open its popup; on a state shot load it; inside a popup activate the row |
| Confirm, held 500 ms | AcceptLong | The focused tile's menu (the right-click menu) |
| X (west) | Alt | The tile's menu at once |
| Back | Back | Close a popup; else close the Game library window; on the home library nothing |
| Y (north) | Aux | Toggle favorite on the focused game |
| LB / RB | SectionPrev/Next | Previous / next visible library tab (Cartridges, CD, BIOS, Favorites, Recents, States) |
| LT / RT | PagePrev/Next | Jump to the previous / next initial letter in the grid (`tv::jumpInitial`) |
| Right stick | (scroll) | Scroll the grid; moves focus to the nearest visible tile when it scrolls out |
| Back / View (select) | Search | Focus the search box (`svc.focusSearch`) |
| Start | Menu | Unbound in the desktop library for now (see non-goals) |
| Share / Guide hold, paddles | C_TVMENU | Unchanged: toggles TV mode with no game |

Confirm with a long press means Accept fires on **release** (under 500 ms) in the desktop library, not
on press. That delay is only in the desktop library; TV mode keeps Accept on press (it has X for
details). The release guard (section 5) covers the button that launched a game either way.

**Families** (detected with `SDL_GetGamepadType`, SDL 3.4.16 types):

| Family | SDL types | Confirm / Back | Labels |
|---|---|---|---|
| Xbox | XBOX360, XBOXONE | South / East | A B X Y, LB RB, LT RT, View, Menu |
| PlayStation | PS3, PS4, PS5 | South / East | Cross Circle Square Triangle, L1 R1, L2 R2, Share/Create, Options |
| Switch | NINTENDO_SWITCH_PRO, JOYCON_LEFT/RIGHT/PAIR, GAMECUBE | **East / South** (A/B swapped) | A B X Y by SDL label, L R, ZL ZR, −, + |
| MFi / generic | STANDARD, UNKNOWN | South / East | A B X Y, LB RB, LT RT |

The fallback for anything unrecognised is the Xbox mapping. West and North stay positional in every
family (menu on the left face button, favorite on the top). Face-button glyphs come from
`SDL_GetGamepadButtonLabel` as TV mode already does (`tv_app_host.cpp` `glyph`), so an 8BitDo pad in
Switch mode that reports a Switch type and Switch labels gets both. A pad whose type and labels
disagree follows the type; the custom profile is the escape hatch.

## 4. Popups

The tile menu, the BIOS popup and the delete-state modal are ImGui popups. Inside them the pad drives
ImGui's own navigation, which is fine for a vertical list (the TV spec's objections were to spatial
nav across the grid): while a popup opened from pad focus is open, the app sets
`ImGuiConfigFlags_NavEnableGamepad`, puts the SDL3 backend in `ImGui_ImplSDL3_GamepadMode_Manual` with
no pads (so it submits no raw pad state), and feeds our intents as ImGui gamepad keys: directions to
`ImGuiKey_GamepadDpad*`, Accept to `GamepadFaceDown`, Back to `GamepadFaceRight`. That keeps the
Switch swap inside popups. The flag goes off when the popup closes. Gamepad nav alone does not make
`WantCaptureKeyboard` true (imgui.cpp 1.92.8, only `NavEnableKeyboard` does), so F4 and the #1020
shortcut keep working.

## 5. Ownership hand-off

Starting a game from the desktop library with the pad, or loading a state, calls the existing
`InputManager::armReleaseGuard()` before the game owns input, exactly as TV mode's launch does, so the
confirm press is masked from the game until it is released. Opening the library window over a paused
game and closing it again arms it too.

## 6. Focus and its look

- **Regions:** the Favorites strip and the Recently played strip (when shown on the tab, each a
  `tv::LinearFocus`), then the grid (`tv::GridFocus`, `cols` set from `drawGrid`'s per-frame column
  count). Up from the grid's first row goes to the strip above; Down from a strip goes to the strip
  or grid below; Up from the top region does nothing. The States tab is one `LinearFocus` over its
  shots in display order; the BIOS tab is the grid.
- **Memory:** `tv::FocusMemory` per tab and region, keyed by the game's path as TV mode does, so a
  list that reorders (a launch moves a game to the front of Recents) keeps the same game focused.
- **Scrolling:** the focused tile is scrolled into view (the grid uses an `ImGuiListClipper`, so the
  scroll target is computed from the row, not from a drawn item).
- **Look:** the same ring as TV mode: the theme's text colour, 2–3 px at desktop scale, round corners,
  plus the soft shadow, drawn in `drawCell` where the hover outline is drawn now. Not colour alone.
  (The approved description said "the theme's accent"; the palette has no accent colour and TV mode
  uses `text` on purpose, so this follows TV mode.)
- **Visibility:** the ring shows after a pad input and hides on mouse movement, like TV mode's
  `padDriven`. The mouse hover outline is unchanged.
- **Hint line:** while pad-driven, the library's status line shows the common buttons with the pad's
  own labels, e.g. "A Play · hold A Menu · Y Favorite · LB/RB Tabs" ("Cross Play …" on a PlayStation
  pad, "A" on the right-hand button on a Switch pad).

## 7. Profiles and the custom editor

- A pure `PadProfile` maps SDL gamepad buttons (and the trigger axes) to `tv::Intent`s. Built-in
  profiles per family; `familyFor(SDL_GamepadType)`.
- The TV shell and the desktop consumer both translate a pad event through the profile of the pad it
  came from (by joystick id), instead of the hard-coded `tv::intentForButton`. Xbox / generic pads map
  exactly as `intentForButton` does today, so existing TV tests stay valid.
- **Custom profile per controller model:** stored in the config next to the per-model game bindings,
  `input.padguid.<GUID>.nav.family` (auto | xbox | playstation | switch | generic) and
  `input.padguid.<GUID>.nav.<intent>` = one serialised button binding per intent that differs from
  the base family. Absent keys mean "the family default". Not new `Ctl` ids (they would eat the
  64-control headroom and reach the game bindings' code paths). Covered by `settings_reset`.
- **Editor:** in the controls screen, when the device picker is set to one connected controller
  model, a "Menu buttons" section: a Family combo (Auto shows the detected family) and one row per
  intent (Confirm, Back, Menu/Alt, Favorite/Aux, Tabs prev/next, Page prev/next, Search) with Set and
  Default, reusing `InputManager`'s bind capture for a pad button. "Reset menu buttons" clears the
  model's nav keys. With "Gamepads (every controller)" selected the section shows the family table
  read-only. Directions are not rebindable (D-pad and left stick always move).

## 8. Constraints

- C++17, SDL 3.4.16, Dear ImGui 1.92.8-docking. New logic lives in new files (the profile and the
  desktop consumer); hooks in `app.cpp`, `input.cpp`, `devices_ui.cpp`, `library_ui.cpp`,
  `tv_shell.cpp` and `remap_ui.cpp` stay small. The emulation-thread work (#967 phase 2) is editing
  `app.cpp` / input in parallel.
- `initImGui` keeps nav off by default; only section 4 turns gamepad nav on, and only while a
  pad-opened popup is open.
- Never open app windows on the developer's Mac. Windowed checks and screenshots run on the Mac Studio
  runner.

## 9. Testing

- Unit (pure): `PadProfile` family detection for every `SDL_GamepadType`, the Switch swap, custom
  overrides and their config round trip; NavInput's long-press Accept (tap fires on release under
  500 ms, a hold fires AcceptLong once); the desktop focus regions (grid ↔ strips, edges, memory by
  game key, the States list).
- Existing `tv_nav_test`, `tv_focus_test`, `tv_ui_smoke`, `tv_menu_headless` stay green unchanged for
  Xbox/generic pads.
- Headless script (`tv_menu_headless` style): with no game, a pad tap moves the desktop focus and A
  launches the focused game; a press while TV mode's Auto controller trigger would act is left for it.
  The virtual pad is a generic type, so family behaviour is covered by the unit tests, not the script.
- Screenshots of the focused grid, a strip, the tile menu and the hint line on the Mac Studio runner,
  attached to the PR.

## 10. Input rules settled after review (my calls)

- **Search box.** Pads never type, so pad intents are not blocked by a focused text field. While the
  search box has focus, Back, Down and Accept from a pad leave it and put focus back on the grid
  (`ImGui::SetWindowFocus` / clearing the active id); the keyboard keeps typing as before.
- **A release only counts if its press was seen here.** `NavInput` keeps a per-source "pressed here"
  latch: a release whose press it did not see, or that went down before the last `reset()`, produces
  nothing. Every ownership or state change (a popup opens or closes, the library window opens or
  closes, a tab switch, the Auto-TV check taking the press, a capture) calls `reset()`. So an A that
  activated a popup row and is released over the grid launches nothing.
- **AcceptLong fires at the 500 ms threshold while the button is still held**, once; its release is
  swallowed. The tile menu it opens takes no input from that button until it is released.
- **Inside popups Accept acts on press** (fed straight to ImGui as `GamepadFaceDown`); only the grid
  and strips delay Accept to release.
- **One shared `NavInput`, sources keyed by (joystick id, button).** Each event is translated through
  the profile of the pad it came from. The existing `padButton(int, bool, ms)` stays as an overload
  (joystick id 0, Xbox mapping) so the TV tests keep their signatures.
- **Custom profile rules.** Global only, not per game. One button per intent and one intent per button:
  Set on a button another intent uses swaps the two, so every intent stays bound. Reserved and refused
  with a note: the D-pad, the left stick, and any button that carries a C_TVMENU binding on that pad.
  Overrides are stored as a serialised `Binding` (button or axis), so Page prev/next can move from the
  triggers to buttons and back; Default restores the family's binding.
- **Pad-opened popups** open anchored at the focused tile (the right-click menu uses the mouse
  position); cell launch / open logic in `drawCell` moves into a helper shared by the click and the pad.
- `AppHost::glyph` and the hint line take Confirm/Back from the profile, not hard-coded South/East.

## 11. Left to the builder

The exact hint-line wording and layout, the ring thickness at desktop scale, how the right stick
scroll converts to pixels, the file names, and how the desktop consumer is split between pure state
and the ImGui-facing part.
