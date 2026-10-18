# Controller navigation of the desktop library: implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** a pad browses the desktop library (TV mode off): focus ring, play, tile menu, favorite, tabs, letter jump,
search focus, with per-family button profiles and a per-controller-model custom profile that TV mode shares.

**Architecture:** three new pure units (`tv::NavInput` extensions, `NavProfile`, `DesktopNav`) carry all logic and are
unit-tested. Thin ImGui/SDL glue (`desktop_nav_app.cpp`) routes pad events, drains intents per frame and executes
commands; `library_ui.cpp` publishes its display lists to `DesktopNav` and draws the ring. Popups use ImGui's own
gamepad nav, fed with our intents. The controls screen gets a "Menu buttons" editor.

**Tech Stack:** C++17, SDL 3.4.16, Dear ImGui 1.92.8-docking, CMake/CTest.

**Spec:** `docs/app-pad-nav-design.md` (sections cited as §N).

## Global Constraints

- Branch `feat/1021-pad-library-nav` off `libretro/develop`; worktree `~/Workspace/Provenance/vj-wt/pad-nav`. Rebase on
  `libretro/develop` before the PR (#1020's `feat/1020-tv-toggle` and #967 phase 2 touch `remap_ui.cpp`, `app.cpp`,
  `input.cpp`).
- Build: `env -u DEVELOPER_DIR CC=/usr/bin/clang CXX=/usr/bin/clang++ cmake -S app -B build -DCMAKE_BUILD_TYPE=Release`,
  `cmake --build build -j8`, `ctest --test-dir build --output-on-failure -j4`. Every existing test stays green.
- Never open app windows on the developer's Mac (only `--headless` and test binaries). Windowed runs and screenshots on
  the Mac Studio runner (`vj-eval/remote/vjr.sh`, `ssh studio-runner`).
- Hooks in existing files stay a few lines each; logic lives in new files. Match the surrounding comment style.
- Mouse and keyboard behaviour of the desktop UI unchanged (§1 anti-goals). Keyboard is never fed to `DesktopNav`.
- Xbox / generic pads map exactly as `tv::intentForButton` does today (§7).
- Shell aliases `rm`/`cp`/`mv` are interactive: `trash` or `command rm -f`. Commits: conventional, 1Password-signed
  (retry on "failed to fill whole buffer"), ending `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.

## Review Focus

1. A pad hot-plugged or unplugged mid-hold (Confirm held, then `SDL_EVENT_GAMEPAD_REMOVED`): no AcceptLong, no stuck
   repeat — Task 1 test `unplug_mid_hold`.
2. The library list changes under focus (scan finishes, search typed, a game launched moves Recents): focus follows the
   same game by key, else clamps — Task 3 test `list_changes_follow_key`.
3. Two pads at once with different families (Switch + Xbox): each confirms with its own button — Task 1 test
   `two_pads_same_button` and Task 4 smoke `switch_pad_accepts_east`.
4. An empty tab / "Nothing matches" (zero items in every region): every intent is a no-op, no crash — Task 3 test
   `empty_lists`.
5. The custom profile edited for a pad that is then unplugged / a GUID never seen: config keys are inert, defaults apply
   — Task 2 test `unknown_guid_defaults`.

---

### Task 1: NavInput — per-pad sources, supplied intents, long Accept, Search

**Files:**
- Modify: `app/src/tv/tv_nav.h`, `app/src/tv/tv_nav.cpp`
- Test: `app/tests/tv_nav_test.cpp`

**Interfaces:**
- Produces:
  - `enum class Intent { …existing…, PageNext, Search, AcceptLong };` (appended; nothing reorders)
  - `void NavInput::padIntent(uint32_t pad, int button, Intent in, bool down, uint64_t ms);` source = (pad, button)
  - `void NavInput::padTrigger(uint32_t pad, int axis, Intent in, int16_t v, uint64_t ms);` edge only, `kTriggerOn`/
    `kTriggerOff` hysteresis per (pad, axis), no repeat; `in == None` ignores the axis
  - `void NavInput::setLongAccept(bool on);` and `static constexpr uint64_t kLongMs = 500;`
  - `void NavInput::forgetPad(uint32_t pad);` drops that pad's held sources, emits nothing
  - Existing `padButton(b, down, ms)` == `padIntent(0, b, intentForButton(b), down, ms)`; `padAxis` unchanged.

- [ ] **Step 1: Write failing tests** in `tv_nav_test.cpp` (style of the file):
  - `long_accept_tap`: `setLongAccept(true)`; `padIntent(7, SOUTH, Accept, true, 0)`; `take(100)` empty;
    `padIntent(7, SOUTH, Accept, false, 200)`; `take(200) == {Accept}`.
  - `long_accept_hold`: down at 0; `take(499)` empty; `take(500) == {AcceptLong}`; `take(600)` empty; up at 900;
    `take(900)` empty.
  - `long_accept_unseen_release`: down at 0; `reset()`; up at 100; `take(100)` empty.
  - `long_accept_off_unchanged`: without `setLongAccept`, down at 0 → `take(0) == {Accept}`, up → nothing.
  - `two_pads_same_button`: `setLongAccept(true)`; pad 1 SOUTH Accept down at 0; pad 2 SOUTH Back down at 10
    (`take(10) == {Back}`); pad 2 up at 20; pad 1 up at 100 → `take(100) == {Accept}`.
  - `unplug_mid_hold`: long accept on; pad 3 SOUTH down at 0, pad 3 DPAD_DOWN Down down at 0; `forgetPad(3)`;
    `take(1000)` contains neither AcceptLong nor a Down repeat (only the initial Down edge from before).
  - `pad_trigger_edges`: `padTrigger(1, RIGHT_TRIGGER, PageNext, 9000, 0)` → `{PageNext}`; 5000 → nothing; 3000 then
    9000 → `{PageNext}` again; pad 2's trigger independent of pad 1's.
- [ ] **Step 2:** `cmake --build build -j8 --target vj_tv_nav_test && ctest --test-dir build -R tv_nav_unit` → FAIL
  (does not compile).
- [ ] **Step 3: Implement.** Source key `int src = (int)((pad & 0x7FFF) << 16) | (kSrcButton + button)`; triggers keyed
  the same way with `kSrcAxis`. Long Accept: an Accept press records `downAt` in a held-accept list instead of
  pushing; `take(ms)` emits `AcceptLong` once when `ms - downAt >= kLongMs` and marks it swallowed; `release` of a
  source still in the list and not swallowed pushes `Accept`; a release whose source is not in `down_` emits nothing.
  `reset()` clears the list.
- [ ] **Step 4:** run `tv_nav_unit`, `tv_ui_smoke`, `tv_menu_headless` → PASS.
- [ ] **Step 5: Commit** `feat(app): NavInput per-pad sources, supplied intents and long-press Accept (#1021)`.

### Task 2: NavProfile — families, defaults, custom per-model layer

**Files:**
- Create: `app/src/pad_profile.h`, `app/src/pad_profile.cpp`
- Test: `app/tests/pad_profile_test.cpp`; register `vj_pad_profile_test` / `pad_profile_unit` in
  `app/cmake/VJTvTests.cmake` like `vj_hold_arbiter_test` (links `src/remap.cpp src/config.cpp src/paths.cpp`, SDL)
- Modify: `app/src/settings_reset.cpp` (the `input.padguid.*.nav.*` keys reset with the controls, same class as the
  other `input.` keys)

**Interfaces:**
- Consumes: `tv::Intent` (Task 1), `Binding` (`remap.h`), `Config` (`config.h`).
- Produces (namespace `vj`):
  - `enum class PadFamily { Xbox, PlayStation, Switch, Generic };`
  - `PadFamily familyForType(int sdlGamepadType);` — XBOX360/XBOXONE→Xbox; PS3/PS4/PS5→PlayStation;
    NINTENDO_SWITCH_PRO/JOYCON_LEFT/JOYCON_RIGHT/JOYCON_PAIR/GAMECUBE→Switch; STANDARD/UNKNOWN/other→Generic
  - `const char* familyKey(PadFamily)` ("xbox" "playstation" "switch" "generic"), `bool familyFromKey(const
    std::string&, PadFamily&)`, `const char* familyName(PadFamily)` ("Xbox", "PlayStation", "Nintendo Switch",
    "MFi / generic")
  - `constexpr tv::Intent kNavIntents[10] = {Accept, Back, Alt, Aux, Menu, SectionPrev, SectionNext, PagePrev,
    PageNext, Search};` with `const char* navIntentKey(tv::Intent)` ("confirm" "back" "menu" "favorite" "start"
    "tabprev" "tabnext" "pageprev" "pagenext" "search") and `const char* navIntentLabel(tv::Intent)` ("Confirm",
    "Back", "Tile menu", "Favorite", "Start", "Previous tab", "Next tab", "Previous letter", "Next letter", "Search")
  - `struct NavProfile { PadFamily family; bool familyOverridden = false; std::array<Binding, 10> binds;
    tv::Intent intentForButton(int sdlButton) const; tv::Intent intentForTrigger(int sdlAxis) const;
    Binding binding(tv::Intent) const; };` — D-pad buttons always map to directions; a trigger maps only through a
    `PadAxis` binding with `dir == +1` on LEFT/RIGHT_TRIGGER.
  - `NavProfile familyNavProfile(PadFamily);` Xbox/PlayStation/Generic: Accept SOUTH, Back EAST, Alt WEST, Aux NORTH,
    Menu START, SectionPrev LEFT_SHOULDER, SectionNext RIGHT_SHOULDER, PagePrev axis(LEFT_TRIGGER,+1), PageNext
    axis(RIGHT_TRIGGER,+1), Search BACK. Switch: same with **Accept EAST, Back SOUTH**.
  - `std::string navKeyPrefix(const std::string& guid)` = `"input.padguid." + guid + ".nav."`
  - `NavProfile resolveNavProfile(const Config&, const std::string& guid, int sdlGamepadType);` — family from
    `<prefix>family` when set, else `familyForType`; then each `<prefix><intentKey>` that parses as one Binding
    overrides. Empty guid → family default.
  - `enum class NavSet { Ok, Swapped, Reserved };`
    `NavSet setNavBinding(Config&, const std::string& guid, int sdlGamepadType, tv::Intent, const Binding&,
    const std::vector<Binding>& reserved);` — refuses D-pad buttons, LEFTX/LEFTY axes and anything in `reserved`
    (the pad's C_TVMENU bindings, hold flag ignored); if another intent holds the binding the two swap; a key equal to
    the family default is erased, not written.
  - `void setNavFamily(Config&, const std::string& guid, const std::string& keyOrAuto);` ("auto" erases the key)
  - `void clearNavProfile(Config&, const std::string& guid);` erases every key under the prefix
  - `std::string navButtonName(PadFamily, const Binding&);` face buttons by family (Xbox A/B/X/Y; PlayStation
    Cross/Circle/Square/Triangle; Switch SOUTH=B EAST=A WEST=Y NORTH=X), shoulders LB/RB · L1/R1 · L/R, triggers
    LT/RT · L2/R2 · ZL/ZR, START Menu/Options/+, BACK View/Create/−; else `Binding::display()`.

- [ ] **Step 1: Write failing tests** (`pad_profile_test.cpp`, FAIL-regex style of `hold_arbiter_test.cpp`):
  `family_for_every_type` (all 12 `SDL_GamepadType` values incl. COUNT-1), `xbox_matches_intentForButton` (for every
  `SDL_GamepadButton`, `familyNavProfile(Xbox).intentForButton(b) == tv::intentForButton(b)` except BACK→Search),
  `switch_swaps_confirm_back` (EAST→Accept, SOUTH→Back, WEST→Alt), `triggers_page`, `resolve_family_override`
  (`<prefix>family=switch` on an Xbox type → Switch map), `set_swaps` (set Accept=NORTH on Xbox → Accept NORTH, Aux
  SOUTH, both keys written), `set_default_erases`, `set_reserved_refused` (DPAD_UP; LEFTX; MISC1 when passed as
  reserved), `trigger_to_button_and_back`, `clear_removes_all`, `unknown_guid_defaults` (keys for guid A, resolve
  guid B → family default), `round_trip` (save/load a temp Config file), `button_names` (Switch EAST "A",
  PlayStation SOUTH "Cross", Switch LEFT_TRIGGER axis "ZL").
- [ ] **Step 2:** build `vj_pad_profile_test`, run `ctest -R pad_profile_unit` → FAIL.
- [ ] **Step 3:** implement `pad_profile.cpp`.
- [ ] **Step 4:** `ctest -R "pad_profile_unit|settings_reset"` → PASS.
- [ ] **Step 5: Commit** `feat(app): controller-family menu-button profiles with a per-model custom layer (#1021)`.

### Task 3: DesktopNav — pure focus over the library's regions

**Files:**
- Create: `app/src/desktop_nav.h`, `app/src/desktop_nav.cpp`
- Test: `app/tests/desktop_nav_test.cpp`; target `vj_desktop_nav_test` / test `desktop_nav_unit` in
  `app/cmake/VJTvTests.cmake` (links `src/desktop_nav.cpp src/tv/tv_focus.cpp`)

**Interfaces:**
- Consumes: `tv::Intent`, `tv::LinearFocus`, `tv::GridFocus`, `tv::FocusMemory`, `tv::jumpInitial` (`tv_focus.h`).
- Produces (namespace `vj`):
  - `struct DeskItem { std::string key, title; };` (key = the entry's path; a state shot = `path + "#" + slot`)
  - `enum class DeskRegion { FavStrip, RecentStrip, Grid, States };`
  - `struct DeskLists { std::vector<DeskItem> fav, recent, grid, states; int gridCols = 1; };`
  - `struct DeskCmd { enum Kind { Activate, OpenMenu, ToggleFavorite, TabPrev, TabNext, FocusSearch, Back } kind;
    std::string key; };`
  - `class DesktopNav { public: void setLists(const std::string& tab, const DeskLists&);
    std::vector<DeskCmd> apply(const std::vector<tv::Intent>&); DeskRegion region() const; int index() const;
    std::string focusedKey() const; bool focused(DeskRegion, int index) const; bool takeScroll();
    void setVisible(bool); bool visible() const; void reset(); };`
- Rules (§6, §3): regions in order FavStrip, RecentStrip, Grid (empty ones skipped) or States alone. Left/Right move in
  a strip; the grid moves with `GridFocus`; Up from grid row 0 → the region above at its remembered index; Down from a
  strip → the next region (grid index = min(strip index, cols−1)). States: Up/Left −1, Down/Right +1. PagePrev/PageNext
  jump initials in the grid (`jumpInitial` on grid titles). Accept → Activate(key); AcceptLong/Alt → OpenMenu(key); Aux
  → ToggleFavorite(key, not in States); SectionPrev/Next → TabPrev/TabNext (and the new tab's remembered focus applies
  on the next `setLists`); Search → FocusSearch; Back → Back; Menu, None → nothing. `setLists` keeps the focused key
  if it is still listed (FocusMemory item), else clamps the index; memory is per `tab + region`. Every move sets the
  scroll flag `takeScroll()` returns once. `apply` with no items anywhere returns only tab/search/back commands.

- [ ] **Step 1: Write failing tests:** `grid_moves_and_edges`, `strip_to_grid_and_back` (remembered strip index),
  `states_linear`, `page_initials`, `commands_from_intents` (each intent → expected `DeskCmd`), `list_changes_follow_key`
  (focused "b" stays focused after "a" is inserted ahead; removed → clamps), `per_tab_memory`, `empty_lists` (every
  intent on empty lists: no crash, no Activate), `scroll_flag_once`.
- [ ] **Step 2:** run `desktop_nav_unit` → FAIL.
- [ ] **Step 3:** implement.
- [ ] **Step 4:** run → PASS.
- [ ] **Step 5: Commit** `feat(app): DesktopNav focus model for the desktop library (#1021)`.

### Task 4: Pads translate through their profile in TV mode too

**Files:**
- Modify: `app/src/input.h`, `app/src/input.cpp` (Pad gets `NavProfile nav`; resolved where `p.profile` is resolved, with
  `SDL_GetGamepadType(p.handle)`; `const NavProfile& navProfile(uint32_t joystickId) const` — a static Xbox default for an
  unknown id; `PadFamily lastPadFamily() const`)
- Modify: `app/src/tv/tv_host.h` (`virtual Intent padIntent(uint32_t pad, int button) const { return
  intentForButton(button); }`, `virtual Intent padTriggerIntent(uint32_t pad, int axis) const` defaulting to
  LEFT_TRIGGER→PagePrev, RIGHT_TRIGGER→PageNext), `app/src/tv/tv_app_host.cpp` (override both from
  `app.input.navProfile`; `glyph(Accept/Back/Alt/Aux/Section*/Page*)` labels the profile's binding: the SDL face label
  for a face button as now, `navButtonName` otherwise)
- Modify: `app/src/tv/tv_shell.cpp` `handleEvent`: buttons via `nav_.padIntent(e.gbutton.which, b,
  host_.padIntent(which, b), …)`; triggers via `nav_.padTrigger`; `Search` and `AcceptLong` are not TV's (not fed, not
  consumed); `SDL_EVENT_GAMEPAD_REMOVED` calls `nav_.forgetPad`.
- Test: `app/tests/tv_ui_smoke.cpp` (FakeHost gets a `switchPad` flag)
- Modify: `app/CMakeLists.txt` / `app/cmake/VJTvTests.cmake` source lists for `pad_profile.cpp` where `input.cpp` and
  `tv_app_host.cpp` are built.

- [ ] **Step 1: Write failing smoke case** `switch_pad_accepts_east`: FakeHost with `switchPad = true` maps EAST→Accept,
  SOUTH→Back; on Home, tapping EAST launches the focused game (FakeHost records `launch`), tapping SOUTH at the root does
  not.
- [ ] **Step 2:** run `tv_ui_smoke` → FAIL.
- [ ] **Step 3:** implement the hooks above.
- [ ] **Step 4:** full `ctest` → PASS (`tv_menu_headless` unchanged: its virtual pad is a generic type → Xbox map).
- [ ] **Step 5: Commit** `feat(app): TV mode reads each pad's menu-button profile (#1021)`.

### Task 5: Routing — when the desktop library owns the pad

**Files:**
- Create: `app/src/desktop_nav_app.h`, `app/src/desktop_nav_app.cpp` (`struct DesktopPad { tv::NavInput in;
  DesktopNav nav; bool active = false; bool padPopup = false; float scrollY = 0; };`,
  `bool deskPadActive(const App&)`, `bool deskPadHandleEvent(App&, const SDL_Event&, uint64_t ms)`,
  `void deskPadFrame(App&, uint64_t ms)`)
- Modify: `app/src/app.h` (one member `DesktopPad deskPad;`), `app/src/app.cpp` `handleEvent` (one line after the TV
  shell's: `if (deskPadHandleEvent(*this, e, SDL_GetTicks())) return;` with a comment that phase 4's Auto controller
  trigger goes before it, §2), `app/src/devices_ui.cpp` `tvInputFrame` (call `deskPadFrame` after `tvShell().frame`)
- Modify: `app/src/tv_script.cpp`, `app/tests/tv_menu_headless.txt.in` (new expect `desk-pad=0|1`)

**Interfaces:**
- Consumes: Tasks 1–4.
- Produces: `deskPadActive(app)` exactly per §2: `!tvMode.active() && !tvShell().menuOpen() && !input.capturing() &&
  (homeVisible() || library.show) && (!host.isLoaded() || (paused && library.show)) && !(showSettings || showStates ||
  showCheats || showAbout || input.overlay.open)`. Events: pad buttons/triggers through `input.navProfile(which)` into
  `deskPad.in` (`setLongAccept(true)`), left stick via `padAxis`, right stick Y stored in `scrollY`; a mapped press is
  consumed (returns true on down, false on up, as the TV shell does); unmapped buttons (Guide, Share, paddles, Start
  is mapped but `DesktopNav` ignores Menu) fall through. Pads never blocked by `WantTextInput`. Mouse motion →
  `nav.setVisible(false)`; a pad press → `true`. `deskPadFrame` calls `in.reset()` on every edge of `active`,
  `padPopup`, `library.show` and tab change, then drains `in.take(ms)` into `nav.apply` (or into ImGui, Task 7).
  `DeskCmd::Activate` on a game calls `input.armReleaseGuard()` before `svc.launch` (§5); a launch of a state too.

- [ ] **Step 1: Write failing script lines** in `tv_menu_headless.txt.in`: no game, TV off → `expect desk-pad=1`;
  F4 (TV on) → `desk-pad=0`; F4 → `desk-pad=1`; `load` a game → `desk-pad=0`; `capture` start → `desk-pad=0`.
- [ ] **Step 2:** build, run `tv_menu_headless` → FAIL (unknown expect).
- [ ] **Step 3:** implement routing and the expect verb.
- [ ] **Step 4:** full `ctest` → PASS.
- [ ] **Step 5: Commit** `feat(app): route pads to the desktop library when it owns the input (#1021)`.

### Task 6: library_ui — lists, focus ring, scroll, commands, hint line

**Files:**
- Modify: `app/src/library_ui.cpp` (publish `DeskLists` from `drawGrid` / `drawStrips` / `drawStatesTab` each frame via
  `app.deskPad.nav.setLists(tabKey, lists)`; `drawCell` takes `bool focus` and draws the ring after the hover outline:
  `AddRect` in `ui::pal().text`, rounding 4, thickness `2.5f * dpiScale`, plus a 3-layer soft shadow like
  `tv_widgets.cpp`, only when `nav.visible()`; when `takeScroll()` the focused grid row's y is computed from
  `row * (artH + textH + gap)` and `ImGui::SetScrollY` brings it into view (strips: `SetScrollX` in their child);
  execute `DeskCmd`s: Activate = the tile's click action (extract a helper `activateEntry(app, svc, e)` used by the
  click and the pad; BIOS tiles open `##bios`), OpenMenu = `ImGui::OpenPopup("##cellmenu")` inside that tile's ID
  scope, anchored with `SetNextWindowPos` at the tile's bottom-left, and `deskPad.padPopup = true`; ToggleFavorite =
  `svc.setFavorite`; TabPrev/TabNext over `lib::visibleTabs` → `svc.section = t; svc.saveSettings()`; FocusSearch =
  `svc.focusSearch = true`; Back = close the Game library window (`svc.show = false`) unless on the home library; while
  the search box is active a pad Back/Down/Accept clears it (`ImGui::ClearActiveID()` / `SetWindowFocus(nullptr)`) and
  returns focus to the grid; right stick `scrollY` scrolls the grid child at `fs * 40 px/s` per full deflection.
- Hint line in `drawStatus` while `nav.visible()`: "<Confirm> Play · hold <Confirm> Menu · <Aux> Favorite ·
  <SectionPrev>/<SectionNext> Tabs" with `navButtonName(lastPadFamily(), binding)` (SDL face labels where a handle is
  known).

- [ ] **Step 1:** build; full `ctest` → PASS (no behaviour change for the mouse; the ring only draws when pad-driven).
- [ ] **Step 2: Windowed script on the Studio runner** (`tv_script` windowed: `wait library`, `pad tap dpad_right`,
  `pad tap dpad_down`, `shot grid.png`, `pad down south` 600 ms `pad up south`, `shot menu.png`, `pad tap east`,
  `pad tap rightshoulder`, `shot tab.png`, `quit`), at 1280×720; check by eye: ring on the focused tile, menu at the
  tile, tab switched, mouse hover unchanged.
- [ ] **Step 3: Commit** `feat(app): pad focus, ring and actions in the desktop library (#1021)`.

### Task 7: Popups — ImGui gamepad nav fed with our intents

**Files:**
- Modify: `app/src/app.cpp` `initImGui` (`ImGui_ImplSDL3_SetGamepadMode(ImGui_ImplSDL3_GamepadMode_Manual, nullptr, 0)`
  — the backend then submits no pad state; nav was already off) and the two `ImGui_ImplSDL3_NewFrame(); …
  ImGui::NewFrame();` sites (one call `deskPadBeforeNewFrame(*this)` between them)
- Modify: `app/src/desktop_nav_app.cpp` (`void deskPadBeforeNewFrame(App&)`: while `deskPad.padPopup` and an ImGui popup
  is open, set `ImGuiConfigFlags_NavEnableGamepad` and `io.BackendFlags |= ImGuiBackendFlags_HasGamepad`; otherwise
  clear both and `padPopup = false`; `deskPadFrame` in popup mode turns intents into `io.AddKeyEvent` down+up pairs:
  Up/Down/Left/Right → `ImGuiKey_GamepadDpad*`, Accept (on press: `setLongAccept(false)` in popup mode) →
  `ImGuiKey_GamepadFaceDown`, Back → `ImGuiKey_GamepadFaceRight`; the Confirm still held from the long press is
  ignored until released (the reset on the `padPopup` edge does it))
- Modify: `app/src/library_ui.cpp` (the `##bios` popup and the "Delete save state?" modal set `deskPad.padPopup` when
  opened from the pad)
- Modify: `app/tests/tv_ui_smoke.cpp` or a new case in it: with `NavEnableGamepad` + `HasGamepad` and only gamepad nav,
  `io.WantCaptureKeyboard` stays false over an open popup (§4).

- [ ] **Step 1: Write the failing smoke assertion** above.
- [ ] **Step 2:** run → FAIL / PASS as written (the assertion documents the ImGui guarantee; if it passes immediately,
  keep it as the guard).
- [ ] **Step 3:** implement; full `ctest` → PASS.
- [ ] **Step 4: Windowed script on the Studio:** long-press opens the tile menu, `pad tap dpad_down` ×2, `shot`, `pad tap
  south` toggles "Add to Favorites"; a Switch-type pad is not available there, so the swap inside popups is covered by
  reading the code path (intents, not raw buttons, reach ImGui).
- [ ] **Step 5: Commit** `feat(app): drive library popups with the pad through ImGui's gamepad nav (#1021)`.

### Task 8: The "Menu buttons" editor in the controls screen

**Files:**
- Modify: `app/src/remap_ui.cpp` (`drawControlsTab`: when `device >= 2` (one controller model) a collapsing section
  "Menu buttons": Family combo — "Auto (<detected>)", then the four families — via `setNavFamily`; a table row per
  `kNavIntents` entry: label, `navButtonName` of the current binding, **Set** (capture), **Default** (erase that key);
  a "Reset menu buttons" button → `clearNavProfile`; a refused or swapped Set shows its note under the table; with
  `device == 1` the family table of the last-used pad is shown read-only. After any change `app.cfg.save();
  in.reresolve();`)
- Modify: `app/src/input.h/.cpp` (`void beginNavCapture(const std::string& guid, tv::Intent)`: the existing
  `BindCapture` for `DeviceClass::Gamepad`; on capture call `setNavBinding` with the pad's C_TVMENU bindings as
  `reserved`, store the `NavSet` result for the UI (`NavSet lastNavSet() const`), save, reresolve)
- Test: extend `tv_menu_headless.txt.in` if the script's `capture` verb can target a nav intent (add
  `capture nav:confirm` → press `pad tap north` → `expect cfg=input.padguid.<virtual guid>.nav.confirm:bnorth`), else
  rely on Task 2's unit tests and note it in the PR.

- [ ] **Step 1:** write the script lines (or record why not).
- [ ] **Step 2:** implement; full `ctest` → PASS.
- [ ] **Step 3: Windowed screenshot on the Studio** of Settings > Controls with the virtual pad selected.
- [ ] **Step 4: Commit** `feat(app): edit a controller model's menu buttons in the controls screen (#1021)`.

### Task 9: Docs, rebase, PR

- [ ] Update `app/README.md` (controls section: the desktop library with a pad, the families, the editor), the cheat
  sheet if it lists library controls, and `docs/app-tv-mode-design.md` §3.1's first row ("pads do nothing except…" →
  "pads browse the library, see docs/app-pad-nav-design.md").
- [ ] `git fetch libretro develop && git rebase libretro/develop`; rebuild; full `ctest`.
- [ ] Push `feat/1021-pad-library-nav` to `libretro`, open the PR into `develop` with `Closes #1021`, the screenshots
  from Tasks 6–8, and the list of design calls from `docs/app-pad-nav-design.md` §10.
