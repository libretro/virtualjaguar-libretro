# TV mode and gamepad navigation for the desktop app: design

Status: draft for review (2026-10-15). Scope: `app/` only; the core is untouched.
Mockups (private canvas, shared on request): https://claude.ai/artifact/Gm3hMjVUwz33ULMan72SHi (A = Home, B = Library; C was not chosen).

## 1. Goal

A player who picks up a controller can browse the library, start a game, open an in-game menu,
change the common settings, and toggle the overlays without reaching for the mouse. TV mode is a
second look for the app, a 10-foot UI in the style of Steam Big Picture, that a player turns on by
hand or that turns itself on in fullscreen or when a controller is used.

**Audience:** desktop users who sometimes play from a couch with a pad. Setup stays mouse-driven:
adding ROM folders, the BIOS, RetroAchievements sign-in, remapping and every other text entry
happen in desktop mode. TV mode never needs an on-screen keyboard.

**Success:** with a pad alone a player can go from the library to a running game, into the menu,
save, load, change the shader or the overclock, show the FPS and a debug window, see their
achievements, and quit back to the library. TV mode adds no input latency to a game in progress
(the only change is in section 3.3: a tap on a dual-use button reaches the game when released).

## 2. Decisions taken

| Question | Decision |
|---|---|
| Audience | Desktop users who sometimes use a pad; setup stays in desktop mode |
| Settings in TV mode | A curated TV list, plus a "More settings…" row that opens the desktop Settings window with ImGui's own pad navigation turned on |
| Opening the menu in a game | A new rebindable app action, "TV menu", with several default bindings: a hold of Share, a hold of Guide, back paddles where the pad has them, and F4 |
| Auto-switching | Settings > Interface > TV mode: Off / Manual / Auto; Auto switches on in fullscreen and/or when a controller is used |
| Library layout | Steam-style shelves for Home (mockup A), and a rail plus cover grid for the Library tab (mockup B); Settings and Overlays reuse B's rail-and-list pattern |
| Renderer | Dear ImGui as the renderer (`ImDrawList`, 1.92 font sizes), with a focus model of our own; ImGui's built-in nav only inside "More settings" |
| Structure | A new `app/src/tv/` module that reuses the existing services; the desktop UI files are not changed beyond the hooks in section 7 |
| Code or themes from other front-ends | None. Layout ideas only; no RetroArch, ES-DE or Pegasus code, and no third-party assets until each licence is checked |

## 3. Input ownership and the TV menu action

### 3.1 Who owns the pad

| State | Pads and keyboard drive |
|---|---|
| Desktop mode, no game | The desktop UI (mouse first). Pads browse the desktop library (see `docs/app-pad-nav-design.md`), plus the TV-menu action and the auto-switch trigger, which wins under Auto (§2 of that design) |
| Desktop mode, game running | The game. The TV-menu action opens the TV in-game menu over the desktop UI |
| TV mode, no game | The TV UI, from every connected pad and from the keyboard (arrows, Enter, Esc, Backspace) |
| TV mode, game running | The game, except the TV-menu action |
| TV in-game menu open (either mode) | The TV UI. The game is paused while the menu is open, through the app's existing pause |

**RetroAchievements hardcore.** The menu goes through the same gates as the desktop UI. Opening it
asks `Achievements::allowPause(why)`; when that refuses (rcheevos rate-limits pausing in hardcore),
the menu does not open and `why` shows as a toast. Load state rows are hidden while
`allowStateLoad()` is false; Save state stays. Netplay: see section 12.

**Mouse in TV mode.** TV mode also works with a mouse, because the audience uses one: hovering a tile
focuses it, a click activates it, and the wheel scrolls a shelf, grid or list. The desktop menu bar
shows on hover at the top edge exactly as it does in fullscreen today. **Esc** is Back on every TV
screen; on the root screen it keeps its current meaning (leave fullscreen).

**Release guard.** When ownership moves from the UI to the game (closing the menu, starting a game),
every button held at that moment is masked from the game until it is released. Pressing A on
"Resume" must not also fire the game's A. The controls overlay already masks the pointer the same
way (`InputManager::handleEvent`); this applies the idea to pad buttons and keys.

### 3.2 The action

`C_TVMENU` is a new app action alongside `C_OVERLAY` and `C_HELP` in `remap.h`. It is appended after
the existing ids so saved profiles keep their meaning. It never reaches the core
(`isActionCtl` covers it). Like the others, it is event-driven, so it also works while paused.

Default bindings for the standard gamepad layout:

1. **Share (Misc1) held**: Xbox Series Share, Switch Pro Capture, DualSense mic. A tap still
   toggles the keypad overlay.
2. **Guide held, only on pads without a Share button** (`SDL_GamepadHasButton(MISC1)` is false). On
   pads with Share, Guide keeps today's plain `#` with no change at all.
3. **Back paddles P1–P4**, when SDL reports them (Xbox Elite, DualSense Edge). These are plain
   presses, because the paddles have no other default use.
4. **F4** on the keyboard.

Every binding can be changed or removed in the controls screen, like any other action. Pads with a
custom SDL mapping get the same defaults wherever those buttons exist.

Separately from the action, **Ctrl+Shift+T** (**Cmd+Shift+T** on macOS) is a fixed, non-rebindable keyboard shortcut
that toggles TV mode with or without a game (the game keeps running; the in-game menu does not open). It sits on View >
TV mode like the other menu shortcuts and is ignored while a text field has the keyboard or a binding is being captured.
The controls screen and the cheat sheet name the action "TV menu / TV mode" (#1020).

### 3.3 Hold bindings

`Binding` gains a `hold` flag. It serialises as a `~hold` suffix (`bmisc1~hold`); the hold flag is
used only on `C_TVMENU` by default, a row older builds do not know and skip. `display()`
shows "Share (hold)".

A pure `HoldArbiter` decides what a physical input on a hold binding means:

- **While the input is down and under the threshold (500 ms):** its other mappings are withheld
  (the game does not see `#`, and the overlay does not toggle).
- **Released before the threshold:** a *tap*. The withheld mappings fire as a pulse that lasts
  4 emulated frames, so a game that polls the pad only every second or third frame still sees it.
  The cost is that a tap reaches the game when it is released rather than when it is pressed. This
  is the only latency change in the design, it affects only buttons that carry a hold binding, and
  the arbiter has to be told what they are. In full, for `#` on a Guide-hold pad: a press of any
  length under 500 ms reaches the game as a 4-frame `#` starting at release, and a press of 500 ms or
  more never reaches the game. A keypad chord that holds `#` (such as `*`+`#`) needs a pad with
  Share, the keypad overlay, or the Guide-hold binding removed; the controls screen says so next to
  the binding.
- **Held past the threshold:** the action fires once (the menu toggles), and the input is
  swallowed until it is released.

Saved profiles store one config key per control and parse each binding token on its own, dropping
only tokens they cannot read (`parseBindings`). So an older build ignores the `C_TVMENU` key it does
not know, and drops a `~hold` token on any other control without losing that control's other
bindings.

The arbiter keeps no SDL handles and no clock of its own. It takes press and release edges plus a
timestamp, so it can be unit-tested with synthetic input.

## 4. Screens

All screens share a top bar (logo, section tabs, clock, RetroAchievements status) and a
button-hint bar along the bottom that lists only what works on the current screen.

| Screen | Layout | Contents |
|---|---|---|
| **Home** | Mockup A | Hero panel for the focused game (art, year and publisher, save states, achievement progress). Shelves: Continue playing, Favorites, Recently added, Jaguar CD. Empty shelves are hidden. A: play. X: resume the newest save state. Y: achievements |
| **Library** | Mockup B | Rail: All games, Favorites, Recently played, Jaguar CD, Homebrew, plus the library's own lists. Six-column cover grid with a caption under the focused cover. LT/RT jump to the previous or next initial letter. Y opens a letter picker (A–Z, 0–9); typing on a keyboard also filters |
| **Game details** | Rail-and-list | X on any tile. Play, Resume a save state (with its screenshot), Favorite on/off, Achievements, Per-game settings (a link to desktop "More settings" filtered to this game) |
| **In-game menu** | Rail-and-list over the dimmed game | Resume, Save state (slot picker), Load state (with screenshots), Achievements, Overlays, Settings, Controls (the cheat sheet), Reset, Close game, Exit TV mode / Enter TV mode |
| **Settings** | Rail-and-list | Curated rows changed with left/right (section 5). The last row is "More settings…" |
| **Overlays** | Rail-and-list | Toggle rows: FPS, the keypad overlay, the controls cheat sheet, and each registered debug window by name (`openDebugWindow`). Debug windows appear on top of the game and are not interactive with a pad |
| **Achievements** | Rail-and-list | The current game's list (locked/unlocked, progress, artwork), leaderboards read-only, hardcore status. Not signed in: "Sign in from Settings > Achievements in desktop mode" |

Navigation: the D-pad and left stick move focus, A confirms, B goes back (from Home: nothing; from
the in-game menu: Resume), LB/RB switch top-bar sections. Focus is remembered per screen and per shelf, so coming back puts the player where they
were.

## 5. Curated settings

Each row is an existing typed option or config key, changed with left/right, and saved and applied
exactly as the desktop control would. No new options are introduced here. Rows:

1. Shader preset (the existing preset list, plus Off)
2. Integer scaling
3. Aspect ratio
4. Internal resolution (the hi-res enhancement)
5. RISC overclock
6. Idle-loop skip
7. Volume
8. Show FPS
9. Vsync
10. Theme
11. TV mode (Off / Manual / Auto) and its two Auto checkboxes
12. More settings…

The plan maps each row to its option key. A row whose option is missing in a build (for example no
shader runtime) is hidden, not greyed out.

**More settings…** opens the desktop Settings window centred, scaled to the TV font size, with
`ImGuiConfigFlags_NavEnableGamepad` turned on only while that window is open in TV mode. B closes it.

## 6. Visual system

- One unit `u = window height / 1080`; every size below is in `u`.
- Safe area: 5% (96 × 54 at 1080p). Backgrounds and scrolling rows may run past it; text and focus
  rings stay inside.
- Type: body 30, captions 26, minimum 24, screen titles 56–84. Fonts are the app's UI font at TV
  sizes (ImGui 1.92 renders any size), so the font picker carries over.
- Focus: scale 1.08 (range 1.05–1.10), a 5-unit ring in the theme's text colour plus a drop shadow.
  Never colour alone. Tiles reserve the padding the scaled size needs.
- Motion: focus and scroll ease out over 120 ms. Holding a direction repeats after 350 ms, then
  every 80 ms. These are starting values to be tuned on a TV; no source gives citable numbers.
- Colour: the current theme's palette and accent (`ui::pal()`), so a custom theme carries into TV
  mode. Text contrast at least 4.5:1, or 3:1 at 24 units and above.
- Art: the box art and screenshots `LibraryService` already loads. A placeholder tile shows the
  title when there is no art.
- The mouse cursor is hidden while TV mode is driven by a pad and comes back on mouse movement.

## 7. Architecture

New files under `app/src/tv/`:

| File | Responsibility | Depends on |
|---|---|---|
| `tv_focus.h/.cpp` | Pure focus model: a shelf (horizontal list), a grid, a vertical list; move, wrap rules, remembered positions, the visible window to scroll to | Nothing |
| `tv_nav.h/.cpp` | Pure: turns pad and keyboard edges into nav intents (Up, Down, Left, Right, Accept, Back, SectionPrev, SectionNext, PagePrev, PageNext, Menu, Search) with repeat timing | Nothing |
| `hold_arbiter.h/.cpp` (in `app/src/`) | Pure: the tap/hold rule in section 3.3 | Nothing |
| `tv_mode.h/.cpp` | Pure: the mode state machine (Off / Manual / Auto, the fullscreen and controller triggers, manual overrides) | Nothing |
| `tv_shell.h/.cpp` | The screen stack, ownership hand-off (3.1), the release guard, the hint bar, the top bar | App, the pure units |
| `tv_style.h/.cpp` | Metrics in `u`, palette lookup, tween helpers | `theme.h` |
| `tv_home.cpp`, `tv_library.cpp`, `tv_details.cpp`, `tv_ingame.cpp`, `tv_settings.cpp`, `tv_overlays.cpp`, `tv_achievements.cpp` | One screen each, drawn with `ImDrawList` | `tv_shell`, `LibraryService`, typed options, the RA client |

Hooks in existing files:

- `remap.h/.cpp`: `C_TVMENU`, `Binding::hold`, and the default bindings.
- `input.cpp`: feed the arbiter, raise the TV-menu toggle the same way `C_HELP` raises `helpToggle_`,
  and the release guard.
- `router.cpp`: clear `C_TVMENU` like the other actions, and apply the arbiter's withheld/pulse result.
- `ui.cpp` `drawUi`: when TV mode is active, draw `tv::draw(app)` in place of the menu bar and the home
  library. A loaded game's picture is still drawn by `video.draw` underneath. When the in-game menu
  is open in desktop mode, draw it on top.
- `app.cpp`: the ImGui nav flags follow section 5 (off except in "More settings" in TV mode); fullscreen
  edges and controller edges feed `tv_mode`; config keys `ui.tv_mode` (`off` / `manual` / `auto`),
  `ui.tv_auto_fullscreen`, `ui.tv_auto_controller`.
- The View menu, and its native twin in `platform/commands.cpp`: "TV mode" (toggle).

The TV code calls only App-level actions (load, pause, save and load state, reset, close, fullscreen,
open a debug window). It never calls the core directly. That keeps it independent of how the
emulation-thread work (#967) lands: when emulation moves to its own thread, those App actions become
commands, and the TV code does not change.

## 8. Auto-switching rules

- **Off:** TV mode never turns on; the TV-menu action still opens the in-game menu over desktop mode.
- **Manual:** View > TV mode, or the in-game menu's "Enter TV mode", toggles it. Nothing switches by itself.
- **Auto:**
  - *When fullscreen* (default on): entering fullscreen turns TV mode on; leaving turns it off. This
    covers F11, Alt+Enter and the macOS green button, which already reach `fullscreen`.
  - *When a controller is used* (default on): a pad button pressed while the desktop UI owns input
    (no game running, no text field focused, no bind capture in progress) turns TV mode on. Mouse
    movement over 32 px or a non-navigation key turns it off again, but only when this trigger was
    the one that turned it on and the window is not fullscreen under the fullscreen trigger.
  - A manual toggle wins until the next trigger edge.
- Switching mode never touches emulation: the game keeps running, and every window and setting is
  kept. The mode is remembered for the next launch only in Manual.
- `--tv` on the command line starts in TV mode, for screenshots and testing.

## 9. Rollout

- Nightlies: `ui.tv_mode = auto` with both triggers on, so it gets daily use.
- First release with it: `manual`, with a one-time hint the first time a controller is used in the
  library ("Press Share (hold) or F4 for TV mode"). Auto becomes the release default once nightly
  feedback is in.
- Order of delivery, each step shippable on its own:
  1. The `C_TVMENU` action, hold bindings, the arbiter, the release guard, and the in-game menu over
     desktop mode.
  2. The TV shell with Home, Library and Game details; Manual mode.
  3. Settings, Overlays, Achievements and "More settings".
  4. Auto-switching, the hint, and tuning of motion and repeat on a real TV.

## 10. Testing

- **Unit tests (CI, every platform)**, in `app/tests/`, synthetic input and no window:
  - `tv_focus`: grid moves and edges, shelf wrap rules, remembered focus per shelf, scroll window.
  - `tv_nav`: repeat timing (350/80 ms), stick dead zone, intents from keyboard and pad.
  - `hold_arbiter`: tap under the threshold fires a 4-frame pulse on release; a hold fires once and
    is swallowed; two hold buttons at once; a pad unplugged mid-hold; threshold exactly reached.
  - `tv_mode`: every trigger order in section 8, including manual overrides and fullscreen with the
    controller trigger.
  - `hold_arbiter`, more: a `*`+`#` chord on a Guide-hold pad (documents the limitation); Guide-hold
    is not bound on a pad that has Share.
  - The menu with hardcore on: refused pause shows the toast and keeps the game running; Load state
    rows are hidden.
  - `remap`: `~hold` serialise/parse round trip; a `~hold` token is the only one an older parser drops;
    `C_TVMENU` is appended without moving existing ids.
- **End-to-end (CI)**, extending `input_e2e.cpp` and `devices_e2e.cpp`: a synthetic Guide tap
  reaches the core as `#` for 4 frames after release; a Guide hold never reaches the core and opens
  the menu; the release guard keeps the closing A press from the game.
- **UI smoke (CI)**: each TV screen draws for 120 frames with a synthetic pad walking every focus
  path, as `debug_scan_ui_smoke.cpp` does for the scanner, with no asserts tripped and no ImGui ID
  conflicts.
- **Screenshots**: `--tv` screenshots of every screen at 1280×720, 1920×1080 and 3840×2160, taken on
  the Mac Studio runner, never on the developer's Mac (no foreground windows there). They are
  compared by eye against the mockups and attached to each PR.
- **By hand, before each release**: a real pad on a real TV (Xbox, DualSense, Switch Pro, an 8BitDo),
  including a machine where Steam or the OS takes the Guide button.

## 11. Out of scope

On-screen keyboard; first-run setup, adding folders, BIOS selection and RetroAchievements sign-in in
TV mode; remapping in TV mode (it opens the desktop controls screen through "More settings");
media and TV remotes (HDMI-CEC, Siri Remote) beyond what arrives as arrow keys; touch; theme packs or
icon sets from other front-ends; netplay lobby in TV mode.

## 12. Open points for the plan

- Exact option keys for each curated row (section 5), taken from `typed_options.h` and `config.cpp`.
- Whether `LibraryService` already exposes "Recently added"; if not, Home drops that shelf rather
  than adding a new index.
- How the pause used by the in-game menu interacts with netplay. Proposed: the menu does not pause
  during a netplay session; it shows "Netplay: the game keeps running" at the top.
