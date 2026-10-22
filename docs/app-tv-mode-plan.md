# TV mode and gamepad navigation: implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** a player can browse the library, start a game, open an in-game menu, change common settings, toggle
overlays and see achievements with a pad alone, in a 10-foot TV mode that turns on by hand or by itself.

**Architecture:** a new `app/src/tv/` module of pure units (focus, nav, mode, style) under an `ImDrawList`
shell. The screens reach the app only through a narrow `tv::Host` interface: `App` implements it in one adapter
file, and the tests use a fake. One new app action, `C_TVMENU`, opens the in-game menu. Hold bindings go
through a pure `HoldArbiter` that edits the device snapshot before the `Router` evaluates it.

**Tech stack:** C++17, SDL3, Dear ImGui 1.92.8 (docking branch), GL 3.3, CMake/CTest, and the repo's own
`CHECK` test harness (one `main` per test executable; failures print `FAIL file:line`).

**Spec:** [`docs/app-tv-mode-design.md`](app-tv-mode-design.md), approved 2026-10-15. Issue #991. Read the spec
first: this plan argues from it and does not repeat its rationale.

## Global constraints

- Scope is `app/` only; the core (`src/`, `libretro.c`) is untouched. The app is C++17; C89 rules do not apply here.
- Branch each phase off `libretro/develop` in a worktree under `~/Workspace/Provenance/vj-wt/`; PRs target
  `develop`. Each PR body carries `Refs #991` (the last phase: `Closes #991`).
- Build and test: `cmake -S app -B app/build -DCMAKE_BUILD_TYPE=Release && cmake --build app/build -j14 &&
  ctest --test-dir app/build -C Release --output-on-failure`. Do **not** set `DEVELOPER_DIR` for app CMake builds.
- Never open an app window on the developer's Mac. Run locally with `--headless` only. Windowed runs and
  screenshots go to the Mac Studio runner: `bash ~/Workspace/Provenance/vj-eval/remote/vjr.sh push|run|pull`
  over ssh alias `studio-runner`.
- No code, themes, icons or assets from RetroArch, ES-DE or Pegasus.
- Every size in TV drawing is in `u = viewport height / 1080`. Safe area 5% (96 × 54 at 1080p). Type: body 30,
  captions 26, minimum 24, titles 56–84. Focus: scale 1.08, a 5-unit ring in `ui::pal().text` plus a shadow.
- Hold threshold 500 ms. Tap pulse 4 emulated frames. Nav repeat: 350 ms, then every 80 ms. Tweens: ease-out over
  120 ms. Auto-off on mouse movement: over 32 px.
- Config keys: `ui.tv_mode` (`off`/`manual`/`auto`), `ui.tv_auto_fullscreen`, `ui.tv_auto_controller`, plus
  `ui.tv_manual_on` and `ui.tv_hint_shown` (added by this plan, see below).
- `C_TVMENU` is appended after `C_HELP`; existing `Ctl` ids never move. `C_COUNT` stays ≤ 64.
- Shell aliases `rm`/`cp`/`mv` are interactive: use `trash` or `command rm -f`.
- Commit messages are conventional: `feat(app): …`, `test(app): …`, `docs(app): …`. Commits are signed. On
  "failed to fill whole buffer", wait and retry; never disable signing.

## How this plan resolves the spec's open points

These follow from the code or from the spec's own fallback rules; they need no new decision.

| Spec point | Resolution (found in the code) |
|---|---|
| §12 option keys | Shader: `video.shader.*` through `ShaderChain`. Integer: `present.integerScale` (`video.integer`). Aspect: `present.aspectOverride` (`video.aspect`), the 4-entry table now in `drawVideoTab`. Internal resolution: core option `virtualjaguar_internal_resolution`. RISC overclock: `virtualjaguar_risc_clock_scale`. Idle-loop skip: `virtualjaguar_risc_idle_skip`. Volume: `app.volume` + `applyAudioSettings()`. Show FPS: `app.showFps` (`ui.show_fps`). Vsync: `app.vsync` + `applyVsync()`. Theme: `setThemeChoice(key)`. |
| §12 "Recently added" | `LibraryService` does not expose it; `ScanFile.mtime` is the file's own date, not when the file joined the library. **The shelf is dropped**, as the spec's own fallback rule says. |
| §12 netplay | The menu opens without pausing when `netplay.configured()` (the test the auto-save already uses for "during netplay"), and shows the banner "Netplay: the game keeps running". |
| §8 "remembered for the next launch only in Manual" | New key `ui.tv_manual_on` (bool), written only while the setting is Manual. |
| §9 nightly = Auto, first release = Manual | CMake option `VJ_TV_DEFAULT` (`auto` by default) becomes the compile definition `VJ_TV_DEFAULT_SETTING`, read only when `ui.tv_mode` is absent. The packages are built by `app-package.yml` (Phase 4 revision): it passes `-DVJ_TV_DEFAULT=manual` when its `release_tag` is a `vX.Y.Z` tag or `prerelease`; `nightly`, `prNNN` and `dev` builds stay Auto. |
| §9 one-time hint | New key `ui.tv_hint_shown`. |

## Changes to the approved spec (each needs the user's approval before its phase starts)

The spec was approved as written; the code made these unavoidable or better. Phase 1 needs only the
Reset / Close game row; the others apply from phase 2 on.

| Spec point | Proposed change |
|---|---|
| §4 Library rail "Homebrew" | `lib::Entry` has no homebrew flag. **Dropped**, by the same no-new-index rule. Rail: All games, Favorites, Recently played, Jaguar CD. |
| §4 Home hero "year and publisher", "achievement progress" | `lib::Entry` has neither year nor publisher, and RA progress is not keyed by library entry. The hero shows art, title, region, cartridge/CD, and the number of save states with the newest date. |
| §4 Home "Y: achievements" | Per-game RA rows exist only for the loaded game. With no game running, the Achievements screen shows the RA games list (`ach.games()`, read-only progress); with a game running, it shows that game's rows. |
| §4 "X on any tile" vs Home "X: resume" | Made consistent. **X (West) = resume the newest save state on any tile** (hidden when there is none, or in hardcore). **Start (Menu) = Game details on any tile**; a right-click does the same with a mouse. Y (North) = the letter picker in Library, Achievements on Home. |
| §4 Game details "Per-game settings" | Per-game settings exist only as control overrides for the *loaded* game, and Game details is reached only with no game running. **Row dropped**; the in-game menu's Settings > More settings covers it. |
| Reset / Close game from a pad | Both ask first: a two-row Yes/No list with focus on No (one A press must not lose progress). |
| §3.1 the TV-menu action with no game | With no game loaded and the setting not Off, `C_TVMENU` toggles TV mode (Task 7). The spec lists the action as live in that state without saying what it does; the §9 hint ("Press Share (hold) or F4 for TV mode") needs this meaning. |
| §9 the default per build | `app-package.yml` gains `-DVJ_TV_DEFAULT=manual` for tags and `prerelease` (Task 17; the packages moved out of `release.yml` after this plan was written). The v3.10.0 back-merge into develop carries the workflow files: that commit goes last and is rebased on it. |

## Review focus

The failure modes most likely to reach a player that no spec test names. Each has a test in the task listed.

1. **Opening the menu over a game the player already paused** must leave it paused when the menu closes
   (Task 5: `menu_keeps_user_pause`).
2. **A release that never arrives.** Steam or the OS takes Guide, or the window loses focus mid-hold. A stale
   hold or a queued pulse must not later fire into the game (Task 2: `clear_drops_pulse`; Task 5: the
   `tv_menu_headless` focus-lost case).
3. **Two pads.** One pad holding Share while another plays: the masking is per pad, so the other pad's Share
   and `#` pass through untouched (Task 2: `per_device_isolation`).
4. **`C_TVMENU` during a bind capture or while typing** in a text field must neither open the menu nor bind
   twice (Task 5: the `tv_menu_headless` capture and typing cases).
5. **A hardcore-disallowed core option changed from TV Settings** must behave exactly as from the desktop
   (same `CoreHost::setOptionValue` path), and an overridden option is read-only (Task 12:
   `settings_rows_core_option_rules`).

---

## Phase 1 — `C_TVMENU`, hold bindings, release guard, in-game menu over desktop mode (PR 1)

### Task 1: `C_TVMENU`, `Binding::hold`, paddles, pad capabilities

**Files:**
- Modify: `app/src/remap.h`, `app/src/remap.cpp`
- Test: `app/test/remap_test.cpp`

**Interfaces:**
- Produces:
  - `Ctl::C_TVMENU`, appended after `C_HELP`, with config id `"tvmenu"` and label `"TV menu"`.
  - `isActionCtl(C_TVMENU) == true`.
  - `Binding::hold` (`bool`, default false), part of `operator==`.
  - `Binding Binding::withHold() const`.
  - `struct PadCaps { bool hasMisc1 = true; };`
  - `static Profile Profile::builtin(DeviceClass cls, PadCaps caps = PadCaps())`.
  - `Profile BindingStore::resolve(DeviceClass, const std::string& guid, const std::string& gameId, PadCaps caps = PadCaps()) const`.

- [ ] **Step 1: Write the failing tests** in `app/test/remap_test.cpp` (the file's existing `CHECK` style):

```cpp
// tvmenu_is_appended
CHECK(C_TVMENU == C_HELP + 1 && C_COUNT == C_TVMENU + 1 && isActionCtl(C_TVMENU));
CHECK(std::string(ctlId(C_TVMENU)) == "tvmenu");
CHECK(std::string(ctlId(C_HELP)) == "help" && std::string(ctlId(C_OVERLAY)) == "overlay");   // ids unmoved
// hold_round_trip
Binding g = Binding::button(SDL_GAMEPAD_BUTTON_MISC1).withHold();
CHECK(g.serialize() == "bmisc1~hold");
Binding p; CHECK(Binding::parse("bmisc1~hold", p) && p == g && p.hold);
CHECK(Binding::parse("k61~hold", p) && p.kind == Binding::Key && p.hold);
CHECK(!Binding::parse("a+lefty~hold", p));                        // hold is for keys and buttons only
CHECK(g.display() == "Share / Capture (hold)");
CHECK(Binding::button(SDL_GAMEPAD_BUTTON_GUIDE) != Binding::button(SDL_GAMEPAD_BUTTON_GUIDE).withHold());
// older_parser_drops_only_hold_tokens: what a pre-TV build does with the new form
CHECK(legacyParse("bguide,bmisc1~hold").size() == 1);              // the old rule rejects only the ~hold token
CHECK(parseBindings("bguide,bmisc1~hold").size() == 2);            // the new rule keeps both
// builtin_tvmenu_defaults
Profile withShare = Profile::builtin(DeviceClass::Gamepad, PadCaps{true});
Profile noShare = Profile::builtin(DeviceClass::Gamepad, PadCaps{false});
auto has = [](const Profile& pr, Binding b) { auto& v = pr.binds[C_TVMENU]; return std::find(v.begin(), v.end(), b) != v.end(); };
CHECK(has(withShare, Binding::button(SDL_GAMEPAD_BUTTON_MISC1).withHold()));
CHECK(!has(withShare, Binding::button(SDL_GAMEPAD_BUTTON_GUIDE).withHold()));   // Guide keeps plain #
CHECK(has(noShare, Binding::button(SDL_GAMEPAD_BUTTON_GUIDE).withHold()));
for (int b : {SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1, SDL_GAMEPAD_BUTTON_LEFT_PADDLE1, SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2, SDL_GAMEPAD_BUTTON_LEFT_PADDLE2})
    CHECK(has(withShare, Binding::button(b)));                                   // plain presses
CHECK(has(Profile::builtin(DeviceClass::Keyboard), Binding::key(SDL_SCANCODE_F4)));
CHECK(has(withShare, Binding::button(SDL_GAMEPAD_BUTTON_GUIDE)) == false);       // C_TVMENU never gets plain Guide
CHECK(std::find(withShare.binds[C_HASH].begin(), withShare.binds[C_HASH].end(), Binding::button(SDL_GAMEPAD_BUTTON_GUIDE)) != withShare.binds[C_HASH].end());
// hold_binding_is_never_down_in_evaluate
DeviceSnapshot s; s.btn[SDL_GAMEPAD_BUTTON_MISC1] = true;
CHECK(!evaluateExtra(withShare, s).test(C_TVMENU));                             // only the arbiter fires a hold
CHECK(evaluate(withShare, s).test(C_OVERLAY));                                  // the plain mapping still sees the bit
// paddle_labels
CHECK(Binding::button(SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1).display() == "Paddle P1");
CHECK(Binding::button(SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2).display() == "Paddle P2");
CHECK(Binding::button(SDL_GAMEPAD_BUTTON_LEFT_PADDLE1).display() == "Paddle P3");
CHECK(Binding::button(SDL_GAMEPAD_BUTTON_LEFT_PADDLE2).display() == "Paddle P4");
```

`legacyParse` is a ten-line copy, inside the test, of today's `parseBindings` without the `~hold` branch. It
documents what an older build does with a new profile.

- [ ] **Step 2: Run** `cmake --build app/build --target vj_remap_test && ctest --test-dir app/build -R remap_unit`.
  Expected: it fails to compile (`C_TVMENU` and `withHold` are not defined).
- [ ] **Step 3: Implement** in `remap.h`/`remap.cpp`:
  - Append `"tvmenu"`/`"TV menu"` to `kCtlId`/`kCtlLabel`; the existing `static_assert`s check the sizes.
  - `serialize()` appends `"~hold"` when `hold` is set.
  - `parse()` strips a trailing `"~hold"` before the existing rules, and accepts it only on `k`/`b` tokens.
  - `display()` appends `" (hold)"`.
  - `buttonLabel()` gains the four paddle labels.
  - `bindingDown()` returns false for a `hold` binding.
  - `builtin()`: keyboard `C_TVMENU` = F4. Gamepad `C_TVMENU` = `MISC1.withHold()` and the four paddles, plus
    `GUIDE.withHold()` only when `!caps.hasMisc1`.
  - `resolve()` passes `caps` to `builtin()`.
- [ ] **Step 4: Run** `ctest --test-dir app/build -R remap_unit`. Expected: PASS.
- [ ] **Step 5: Commit** `feat(app): C_TVMENU action, hold bindings and pad capabilities in remap (#991)`.

### Task 2: `HoldArbiter` and `ReleaseGuard` (pure)

**Files:**
- Create: `app/src/hold_arbiter.h`, `app/src/hold_arbiter.cpp`
- Create: `app/tests/hold_arbiter_test.cpp`, `app/cmake/VJTvTests.cmake` (included from `app/CMakeLists.txt`
  next to `VJThemeTests`)

**Interfaces:**
- Consumes: `Binding`, `DeviceSnapshot`, `kStickThreshold`, `kTriggerThreshold` (Task 1 / `remap.h`).
- Produces:

```cpp
namespace vj {
constexpr uint32_t kKeyboardDev = 0;   // SDL never hands out joystick id 0
class HoldArbiter {
public:
    static constexpr uint64_t kHoldMs = 500;
    static constexpr int kPulseFrames = 4;
    void setHoldInputs(uint32_t dev, const std::vector<Binding>& inputs);   // hold flag ignored; keys / buttons
    bool isHoldInput(uint32_t dev, const Binding& in) const;
    void press(uint32_t dev, const Binding& in, uint64_t ms);     // no-op unless isHoldInput
    void release(uint32_t dev, const Binding& in, uint64_t ms);
    int takeFires(uint64_t ms);       // UI frame: holds that reached kHoldMs since the last call (each fires once)
    void frame();                     // one emulated frame: pulses count down
    // A tap queues a pulse only while live (a game loaded, not paused, the game owns input).  A tap in the library
    // or the paused menu means nothing to a game and must not reach the next one.  Going not-live drops queued pulses.
    void setLive(bool live);
    void apply(uint32_t dev, DeviceSnapshot& s) const;   // held-under-threshold or swallowed -> bit cleared; pulsing -> set
    void forget(uint32_t dev);        // pad unplugged: its holds and pulses go, nothing fires
    void clear();                     // focus lost / ownership change: everything goes, nothing fires
};
class ReleaseGuard {
public:
    void arm(uint32_t dev, const DeviceSnapshot& now);   // every key / button / axis past its threshold is masked
    void apply(uint32_t dev, DeviceSnapshot& s);         // masks what is still down; forgets what was released
    bool armed() const;
    void clear();
};
}
```

- [ ] **Step 1: Write the failing tests** (`app/tests/hold_arbiter_test.cpp`). `G` is
  `Binding::button(SDL_GAMEPAD_BUTTON_GUIDE)`, `dev` is 7, and `snapG()` is a snapshot with Guide down.

```cpp
// tap_pulses_on_release
a.setHoldInputs(7, {G}); a.press(7, G, 1000);
DeviceSnapshot s = snapG(); a.apply(7, s); CHECK(!s.btn[GUIDE]);          // withheld while down
CHECK(a.takeFires(1200) == 0);
a.release(7, G, 1300);
for (int f = 0; f < 4; f++) { DeviceSnapshot u{}; a.apply(7, u); CHECK(u.btn[GUIDE]); a.frame(); }
DeviceSnapshot after{}; a.apply(7, after); CHECK(!after.btn[GUIDE]);       // exactly 4 frames
// hold_fires_once_and_swallows
a.press(7, G, 0); CHECK(a.takeFires(499) == 0); CHECK(a.takeFires(500) == 1); CHECK(a.takeFires(900) == 0);
s = snapG(); a.apply(7, s); CHECK(!s.btn[GUIDE]);
a.release(7, G, 1000); DeviceSnapshot r{}; a.apply(7, r); CHECK(!r.btn[GUIDE]);   // no pulse after a hold
// threshold_reached_at_release_without_tick: press 0, release 500, takeFires(501) == 1, no pulse
// two_hold_buttons: Share and Guide both hold inputs on dev 7; Share held 600 ms, Guide tapped -> 1 fire, Guide pulses
// per_device_isolation: dev 7 holds Guide; apply(8, snap with Guide down) leaves dev 8's bit set
// unplug_mid_hold: press(7,G,0); forget(7); takeFires(600) == 0; apply(7, snapG()) leaves the bit alone (no longer a hold input)
// clear_drops_pulse: tap, clear() before frame(); apply shows no Guide bit
// no_pulse_when_not_live: setLive(false); tap; setLive(true); apply shows no Guide bit for 4 frames
//   (a Share tap in the library must not open the keypad overlay in the next game; a Guide tap while paused
//   must not press # on resume)
// going_not_live_drops_queued_pulse: setLive(true); tap; setLive(false); setLive(true); no Guide bit
// holds_still_fire_when_not_live: setLive(false); press 0; takeFires(500) == 1   // the menu works paused / in the library
// non_hold_input_untouched: Start on dev 7 passes through apply() unchanged
// chord_star_hash_on_guide_hold_pad (documents the spec limitation): with right-stick-down (*) held and Guide
//   pressed, the core-facing snapshot never has both until Guide is released, then Guide pulses 4 frames
// guard_masks_until_release
ReleaseGuard g; DeviceSnapshot down{}; down.btn[SOUTH] = true; down.axis[LEFTX] = 30000; g.arm(7, down);
DeviceSnapshot still = down; g.apply(7, still); CHECK(!still.btn[SOUTH] && still.axis[LEFTX] == 0);
DeviceSnapshot up{}; g.apply(7, up); DeviceSnapshot again = down; g.apply(7, again); CHECK(again.btn[SOUTH]);   // re-press reaches the game
CHECK(!g.armed());
```

- [ ] **Step 2:** Add `vj_hold_arbiter_test` (sources: the test, `src/hold_arbiter.cpp`, `src/remap.cpp`,
  `src/config.cpp`, `src/paths.cpp`; link `${VJ_SDL3_TARGET}`) and `add_test(NAME hold_arbiter_unit …)` to
  `VJTvTests.cmake`, copying `VJThemeTests.cmake`'s warning flags and `FAIL ` regex. Run
  `ctest --test-dir app/build -R hold_arbiter_unit`. Expected: it fails to link (no implementation yet).
- [ ] **Step 3: Implement** `hold_arbiter.cpp`. Keep per-`(dev, Binding)` state `{downAt, swallowed, pulse}`
  in a small vector, plus a pending-fires counter: `release()` at `ms - downAt >= kHoldMs` adds a fire and
  starts no pulse.
- [ ] **Step 4: Run** `ctest --test-dir app/build -R hold_arbiter_unit`. Expected: PASS.
- [ ] **Step 5: Commit** `feat(app): hold arbiter and release guard for dual-use buttons (#991)`.

### Task 3: wire holds, the TV-menu toggle and the guard into input

**Files:**
- Modify: `app/src/input.h`, `app/src/input.cpp`, `app/src/router.cpp`, `app/src/devices_ui.cpp` (`inputFrame`)
- Test: `app/test/devices_e2e.cpp` (sources gain `src/hold_arbiter.cpp` in `VJSmokeTests.cmake`)

**Interfaces:**
- Consumes: Tasks 1–2.
- Produces (on `InputManager`):
  - `bool takeTvMenuToggle();`
  - `void tickHolds(uint64_t ms, bool live);`, called every UI frame (it also runs while paused). It passes `live`
    to `HoldArbiter::setLive`.
  - `void feedHoldEdges(const SDL_Event& e);`, the press/release half of `handleEvent`. `App::handleEvent` calls it
    first, before the TV shell can consume the event, so a release is never lost and a Share hold can also
    close the menu.
  - `void armReleaseGuard();`, which snapshots the keyboard and every open pad now.
  - `void clearHolds();`
  - `std::vector<SDL_Gamepad*> padHandles() const;`
  - `void beginCapture(const LayerId&, Ctl, bool add, bool hold = false);`, where `hold` sets the flag on the captured binding.
  - `Pad` gains `PadCaps caps`, filled in `openPad` from `SDL_GamepadHasButton(g, SDL_GAMEPAD_BUTTON_MISC1)` and
    passed to every `resolve` in `reresolve`/`openPad`.

- [ ] **Step 1: Write the failing e2e tests** in `app/test/devices_e2e.cpp`.
  - Build `g_padp` with `Profile::builtin(Gamepad, PadCaps{false})`.
  - Per frame, run the same order `poll()` uses: `arb.apply(dev, snap)` → `guard.apply(dev, snap)` →
    `Router::route` → `arb.frame()`.
  - Read the core's `#` from the joypad matrix: copy the `joypad0Buttons` extern and `slotFor(C_HASH)` from
    `test/input_e2e.cpp`, and make sure the keypad has been announced (`host.sendKeyEvent`, as `input_e2e` does).

```cpp
// guide_tap_reaches_core_as_hash_for_4_frames_after_release
arb.press(dev, G, 0); frames(10, guideDown); CHECK(!hashPressed());
arb.release(dev, G, 200); int on = 0; for (int f = 0; f < 8; f++) { frame(nothingDown); on += hashPressed(); }
CHECK(on == 4);
// guide_hold_never_reaches_core: press 0, takeFires(600)==1, 40 frames with Guide down: hashPressed() never true
// release_guard_keeps_closing_A_from_game: guard.arm(dev, eastDown); frames with East down: C_A never set; after an
//   all-up frame and a new East press: C_A set
```

  These cases prove that Tasks 1–2 plus the `Router` reach the real core. The `InputManager` glue below is
  SDL-bound and no unit test reaches it. Task 5's headless script (`tv_menu_headless`) covers it: F4 opens the
  menu, nothing fires while capturing or typing (Review focus 4), and focus loss clears the holds (Review focus 2).

- [ ] **Step 2: Run** `ctest --test-dir app/build -R devices_e2e`. Expected: it fails to link until
  `src/hold_arbiter.cpp` joins `vj_devices_e2e`'s sources; after that the three cases PASS. If one fails, the
  defect is in Task 1 or 2: fix it there before going on.
- [ ] **Step 3: Implement the glue.**
  - `router.cpp`: `ex.clear(C_TVMENU)` next to `ex.clear(C_HELP)`.
  - `input.cpp` `feedHoldEdges` (only `App::handleEvent` calls it, once per event; `InputManager::handleEvent`
    does not feed the arbiter):
    - Pad button and key down/up feed `holds_.press/release` with `SDL_GetTicks()`. Do this only when no
      capture is running, and for keys only when `!uiWantsKeyboard` and no Ctrl/Alt/Cmd is held.
  - `input.cpp` `handleEvent`:
    - A plain (non-hold) `C_TVMENU` binding sets `tvMenu_ = true`, exactly as `C_HELP` sets `helpToggle_`.
    - `GAMEPAD_REMOVED` calls `holds_.forget(id)`.
    - `WINDOW_FOCUS_LOST` calls `holds_.clear()`.
  - `reresolve()`/`openPad()` call `holds_.setHoldInputs(dev, <every hold binding in that profile>)`.
  - `poll()` calls `holds_.apply` and then `guard_.apply` on each `HostSource.snap` before `router_.route`, and
    `holds_.frame()` after it.
  - `tickHolds(ms, live)`: `holds_.setLive(live); if (holds_.takeFires(ms)) tvMenu_ = true;`.
  - New `void tvInputFrame(App& app, uint64_t ms)` in `devices_ui.cpp`, free of ImGui:
    - it calls `in.tickHolds(ms, app.host.isLoaded() && !app.paused && <the game owns input>)`;
    - it consumes `takeTvMenuToggle()` (Task 5).
    `inputFrame()` calls it, and the headless script runner (Task 5) calls it too, because `runHeadless` never
    calls `inputFrame`.
  - `App::openContent` and `App::closeContent` call `input.clearHolds()`.
- [ ] **Step 4: Run** `ctest --test-dir app/build -R "devices_e2e|input_e2e|remap_unit|hold_arbiter_unit"`.
  Expected: all PASS.
- [ ] **Step 5: Commit** `feat(app): route hold bindings, the TV-menu toggle and the release guard through input (#991)`.

### Task 4: focus model, nav intents, style metrics (pure)

**Files:**
- Create: `app/src/tv/tv_focus.h/.cpp`, `app/src/tv/tv_nav.h/.cpp`, `app/src/tv/tv_style.h/.cpp`
- Create: `app/tests/tv_focus_test.cpp`, `app/tests/tv_nav_test.cpp`; register both in `VJTvTests.cmake`
  (`tv_style.cpp` is tested in `tv_focus_test`, which links `vj_imgui` for `ImVec2`)

**Interfaces:**

```cpp
namespace vj::tv {
// tv_focus.h
struct LinearFocus {                     // a shelf (horizontal) or a list (vertical); never wraps
    int count = 0, index = 0, first = 0;
    bool move(int delta);                // false = at an end, nothing moved (focus may leave)
    void clampTo(int newCount);
    void scrollToShow(int visible);      // first..first+visible-1 contains index
};
struct GridFocus {
    int count = 0, cols = 6, index = 0, firstRow = 0;
    enum Edge { None, Left, Right, Top, Bottom };
    Edge move(int dx, int dy);           // down onto a short last row lands on the last item
    int row() const; int col() const; int rows() const;
    void scrollToShow(int visibleRows);
};
class FocusMemory { public: int get(const std::string& key, int def = 0) const; void put(const std::string& key, int index); };
char initialOf(const std::string& title);                                     // 'A'..'Z', '0'..'9', else '#'
int jumpInitial(const std::vector<std::string>& titles, int from, int dir);   // LT/RT: first item of the prev/next initial
int firstWithInitial(const std::vector<std::string>& titles, char initial);   // letter picker; -1 = none
// tv_nav.h
enum class Intent { None, Up, Down, Left, Right, Accept, Back, Alt, Aux, Menu, SectionPrev, SectionNext, PagePrev, PageNext };
class NavInput {
public:
    static constexpr uint64_t kRepeatDelayMs = 350, kRepeatMs = 80;
    static constexpr int kStickOn = 16000, kStickOff = 8000;   // hysteresis
    void padButton(int sdlButton, bool down, uint64_t ms);
    void padAxis(int sdlAxis, int16_t v, uint64_t ms);
    void key(int scancode, bool down, bool shift, uint64_t ms);
    std::vector<Intent> take(uint64_t ms);   // edges since the last call, plus repeats (directions only)
    void reset();
};
Intent intentForButton(int sdlButton);     // DPAD -> dirs, SOUTH Accept, EAST Back, WEST Alt, NORTH Aux, START Menu, shoulders Section*
Intent intentForKey(int scancode, bool shift);   // arrows, Return/KP_Enter Accept, Escape/Backspace Back, Tab/Shift+Tab Section*, PageUp/Down Page*
// tv_style.h
struct Metrics { float u, safeX, safeY, body, caption, minText, titleSmall, titleLarge, ring, focusScale; };
Metrics metrics(float viewportH);
float easeOut(float t);                                  // 0..1 -> 0..1, cubic
struct Tween { float from = 0, to = 0; double start = 0; float at(double now) const; void retarget(float to, double now); };
constexpr double kTweenSeconds = 0.120;
}
```

- [ ] **Step 1: Write the failing tests.**

```cpp
// tv_focus_test.cpp
GridFocus g{14, 6, 0}; CHECK(g.move(-1, 0) == GridFocus::Left && g.index == 0);
CHECK(g.move(0, -1) == GridFocus::Top);
g.index = 10; CHECK(g.move(0, 1) == GridFocus::None && g.index == 13);   // short last row: last item
g.index = 13; CHECK(g.move(0, 1) == GridFocus::Bottom);
g.index = 5;  CHECK(g.move(1, 0) == GridFocus::Right && g.index == 5);
LinearFocus l{5, 4}; CHECK(!l.move(1) && l.index == 4);                  // no wrap
l.index = 0; CHECK(!l.move(-1));
LinearFocus sc{20, 12}; sc.scrollToShow(6); CHECK(sc.first == 7);
FocusMemory m; m.put("home/favorites", 3); CHECK(m.get("home/favorites") == 3 && m.get("home/recents", 0) == 0);
std::vector<std::string> t = {"1942", "Alien vs Predator", "Atari Karts", "Cybermorph", "Doom"};
CHECK(initialOf("alien") == 'A' && initialOf("1942") == '1' && initialOf("[x]") == '#');
CHECK(jumpInitial(t, 1, +1) == 3 && jumpInitial(t, 3, -1) == 1 && jumpInitial(t, 4, +1) == 4);
CHECK(firstWithInitial(t, 'D') == 4 && firstWithInitial(t, 'Z') == -1);
Metrics m1 = metrics(1080), m2 = metrics(2160);
CHECK(m1.u == 1 && m1.safeX == 96 && m1.safeY == 54 && m1.body == 30 && m1.minText == 24 && m1.ring == 5);
CHECK(m2.body == 60 && m1.focusScale > 1.05f && m1.focusScale < 1.10f);
Tween tw{0, 1, 0}; CHECK(tw.at(0) == 0 && tw.at(0.120) == 1 && tw.at(0.060) > 0.5f);   // ease-out: past half at half time
// tv_nav_test.cpp
NavInput n; n.padButton(SDL_GAMEPAD_BUTTON_DPAD_DOWN, true, 0);
CHECK(n.take(0) == std::vector<Intent>{Intent::Down});
CHECK(n.take(349).empty()); CHECK(n.take(350).size() == 1); CHECK(n.take(430).size() == 1); CHECK(n.take(509).empty());
n.padButton(SDL_GAMEPAD_BUTTON_DPAD_DOWN, false, 520); CHECK(n.take(2000).empty());
n.padButton(SDL_GAMEPAD_BUTTON_SOUTH, true, 0); CHECK(n.take(0) == std::vector<Intent>{Intent::Accept}); CHECK(n.take(1000).empty());   // no repeat
n.padAxis(SDL_GAMEPAD_AXIS_LEFTY, 15000, 0); CHECK(n.take(0).empty());                 // inside the dead zone
n.padAxis(SDL_GAMEPAD_AXIS_LEFTY, 20000, 10); CHECK(n.take(10) == std::vector<Intent>{Intent::Down});
n.padAxis(SDL_GAMEPAD_AXIS_LEFTY, 10000, 20); CHECK(n.take(400).size() == 1);          // hysteresis: still held
n.padAxis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 30000, 0); CHECK(n.take(0) == std::vector<Intent>{Intent::PageNext});
CHECK(intentForKey(SDL_SCANCODE_TAB, true) == Intent::SectionPrev && intentForKey(SDL_SCANCODE_BACKSPACE, false) == Intent::Back);
CHECK(intentForButton(SDL_GAMEPAD_BUTTON_EAST) == Intent::Back && intentForButton(SDL_GAMEPAD_BUTTON_START) == Intent::Menu);
```

- [ ] **Step 2: Run** `ctest --test-dir app/build -R "tv_focus_unit|tv_nav_unit"`. Expected: it fails to build.
- [ ] **Step 3: Implement** the three units. `NavInput` holds one held direction at a time (the newest wins)
  and repeats only that one.
- [ ] **Step 4: Run** the same command. Expected: PASS.
- [ ] **Step 5: Commit** `feat(app): TV focus model, nav intents and style metrics (#991)`.

### Task 5: `tv::Host`, the shell and the in-game menu over desktop mode

**Files:**
- Create: `app/src/tv/tv_host.h` (the interface), `app/src/tv/tv_app_host.h/.cpp` (App's adapter)
- Create: `app/src/tv/tv_shell.h/.cpp`, `app/src/tv/tv_ingame.cpp`, `app/src/tv/tv_widgets.h/.cpp`
  (row, tile, hint bar, banner; all `ImDrawList`)
- Modify:
  - `app/src/app.h/.cpp`: owns the host and shell; `menuPause`/`menuResume`; Esc routing.
  - `app/src/ui.cpp` `drawUi`: two draw calls.
  - `app/src/devices_ui.cpp` `inputFrame`: consumes `takeTvMenuToggle()`.
  - `app/src/remap_ui.cpp`: a `C_TVMENU` row, "Add hold", the chord note.
  - `app/src/cheatsheet.cpp` `app()`: `ctl(C_TVMENU)`.
  - `app/CMakeLists.txt`: the tv sources in `VJ_APP_SOURCES`.
- Test: `app/tests/tv_ui_smoke.cpp` (a fake `Host`, an ImGui context with no renderer, as
  `debug_scan_ui_smoke.cpp` does); register `tv_ui_smoke` in `VJTvTests.cmake`.

**Interfaces:**

```cpp
namespace vj::tv {
struct SlotInfo { int slot; bool exists; int64_t time; unsigned tex; int w, h; };
class Host {                                   // everything a TV screen may touch; App implements it (tv_app_host.cpp)
public:
    virtual ~Host() = default;
    virtual double now() const = 0;
    virtual bool gameLoaded() const = 0;
    virtual std::string gameTitle() const = 0;
    virtual bool menuPause(std::string& why) = 0;    // false = refused (hardcore rationing); netplay: true, not paused
    virtual void menuResume() = 0;                   // restores the pause state from before the menu
    virtual bool netplayActive() const = 0;          // netplay.configured()
    virtual bool canLoadState() const = 0;           // ach.allowStateLoad()
    virtual std::vector<SlotInfo> slots() = 0;       // 0..9, thumbnails from App::slotThumb
    virtual bool saveState(int slot) = 0;
    virtual bool loadState(int slot) = 0;
    virtual void reset() = 0;
    virtual void closeGame() = 0;
    virtual void toggleControlsOverlay() = 0;
    virtual void armReleaseGuard() = 0;
    virtual void clearHolds() = 0;
    virtual void toast(const std::string& text) = 0;
    virtual std::string glyph(Intent i) const = 0;   // "A", "Cross", "Enter": SDL_GetGamepadButtonLabel on the last-used pad
};
enum class Layer { Base, Menu };   // Base: TV screens, under desktop windows; Menu: the in-game menu, over them
class Shell {
public:
    explicit Shell(Host& h);
    bool menuOpen() const;
    bool openMenu();                 // menuPause(); refused -> toast(why), false
    void closeMenu();                // menuResume(), armReleaseGuard(), clearHolds()
    void toggleMenu();
    bool ownsInput() const;          // §3.1: menu open, or TV active with no game running
    bool handleEvent(const SDL_Event& e, uint64_t ms);   // nav input while ownsInput(); true = consumed
    void frame(uint64_t ms);         // NavInput::take -> the top screen
    void draw(Layer layer, ImVec2 pos, ImVec2 size);
    bool backAtRoot() const;         // Esc keeps its desktop meaning (leave fullscreen) only here
};
}
// App additions
bool App::menuPause(std::string& why);   // sets pausedBeforeMenu_; no "Paused" toast
void App::menuResume();
tv::Shell& App::tvShell();
```

The draw surface:
- **`Layer::Base`** is a full-viewport ImGui window drawn in `drawUi` right after the picture and before
  `drawSettings`. Its flags are `NoDecoration | NoBackground | NoBringToFrontOnFocus | NoNav | NoSavedSettings`,
  so desktop windows draw above it and take their own clicks. Hover and click on tiles require
  `ImGui::IsWindowHovered()`.
- **`Layer::Menu`** is drawn after `drawDebugWindows` and takes focus when the menu opens.

In-game menu rows in this phase: Resume, Save state (slot picker), Load state (hidden when `!canLoadState()`;
each row shows its thumbnail), Controls, Reset (Yes/No), Close game (Yes/No). Phase 2 adds Enter/Exit TV mode;
phase 3 adds Achievements, Overlays and Settings. With `netplayActive()`, a banner at the top reads
"Netplay: the game keeps running".

- [ ] **Step 1: Write the failing smoke test** (`tv_ui_smoke.cpp`). `FakeHost` records calls; `frames(n)` runs
  `NewFrame` / `shell.frame` / both `draw` layers / `Render`; pad and key events are synthetic `SDL_Event`s fed
  to `shell.handleEvent`.

```cpp
// menu_opens_and_walks: toggleMenu(); press Down x8, Up x8, Accept on Save state, Down to slot 3, Accept
CHECK(host.savedSlot == 3); frames(120); CHECK(g_imguiAsserts == 0);
// menu_refused_in_hardcore: host.refusePause = "Hardcore: wait 20 s"; CHECK(!shell.openMenu() && host.lastToast == "Hardcore: wait 20 s" && !shell.menuOpen());
// load_rows_hidden_in_hardcore: host.canLoad = false; open; walk every row; CHECK(no row label begins with "Load")
// back_resumes: open; Back; CHECK(!shell.menuOpen() && host.resumed == 1 && host.guardArmed == 1 && host.holdsCleared == 1)
// menu_keeps_user_pause (Review focus 1): host.paused = true before open; open; Back; CHECK(host.paused)
// close_game_asks_first: Accept on Close game, Accept (focus starts on No) -> CHECK(host.closed == 0); Down, Accept -> CHECK(host.closed == 1)
// netplay_banner: host.netplay = true; open; CHECK(host.paused == false && drawnTexts contains "Netplay: the game keeps running")
// pointer_free_while_playing: host.loaded = true, menu closed, io.MousePos at the viewport centre; frames(3)
CHECK(!ImGui::GetIO().WantCaptureMouse);   // the light gun / mouse keep the picture
```

  **Step 1b**: add CTest `tv_menu_headless`: `virtualjaguar --headless --frames 30 --tv-test-script <file>`.

  - The dev flag replays the script's lines (`<ms> key|pad|focus|load|expect …`) at those times, in its own loop
    in place of `runHeadless`'s positional-content loop. Each event goes through `App::handleEvent`, then
    `tvInputFrame(app, ms)` runs, and a loaded game steps one frame per 16 ms. `load <rom>` drops content
    through the same event path a drop uses, so lines before it run with no game. `expect menu-open=0|1` checks
    `tvShell().menuOpen()`; `expect tv=0|1` (Task 7) checks `tvMode.active()`.
  - The shell and its `AppHost` exist in headless runs too: they need no ImGui to open, close or pause.
  - Pads: the runner attaches one with `SDL_AttachVirtualJoystick` (type `SDL_JOYSTICK_TYPE_GAMEPAD`, with a
    MISC1 button; it works under the dummy video driver). It reaches `InputManager` through the normal
    `GAMEPAD_ADDED` hot-plug path, and the script presses buttons with `SDL_SetJoystickVirtualButton` +
    `SDL_UpdateJoysticks`. **Check this first:** if the virtual pad does not appear headless on all three CI
    OSes, stop and report it. Don't fake `GAMEPAD_*` events for an unopened id: `InputManager` ignores them, so
    the test would pass for the wrong reason.

  Script cases:
  - F4 opens the menu; F4 again closes it.
  - F4 while `uiWantsKeyboard` does nothing.
  - F4 during `beginCapture` binds F4 and does not open the menu.
  - A Share hold of 600 ms opens it; a Share hold while it is open closes it (the edge reaches the arbiter
    before the shell).
  - `WINDOW_FOCUS_LOST` 200 ms into a Share hold, then 600 ms: the menu stays closed.

  Implement the flag in `main.cpp`/`app.cpp` next to `--expect-controls-overlay`, and print `headless: FAIL …`
  on a mismatch.

- [ ] **Step 2: Run** `ctest --test-dir app/build -R "tv_ui_smoke|tv_menu_headless"`. Expected: it fails to build.
- [ ] **Step 3: Implement.**
  - `App::menuPause`/`menuResume`:
    - Pausing calls `ach.allowPause(why)` unless the game is already paused.
    - Under netplay, pausing returns true without pausing.
    - Neither direction shows the "Paused"/"Resumed" toast.
  - `tvInputFrame`: `if (in.takeTvMenuToggle() && app.host.isLoaded()) app.tvShell().toggleMenu();`. With no
    game this phase does nothing; Task 7 adds the no-game meaning.
  - `App::handleEvent`, in this order:
    1. `input.feedHoldEdges(e)`;
    2. `tvShell().handleEvent` while `tvShell().ownsInput()`;
    3. `input.handleEvent`.
  - Layers:
    - `Layer::Base` is drawn only while no game is loaded and TV is active.
    - `Layer::Menu` is drawn only while the menu is open.
    - With a game running and the menu closed, no shell window exists, so `io.WantCaptureMouse` stays false
      over the picture and mouse, rotary and light-gun games keep the pointer.
  - `handleHotkey` Esc: `if (tvShell().ownsInput() && !tvShell().backAtRoot()) break;` before leaving fullscreen.
  - `remap_ui.cpp`:
    - Add `{"App", C_TVMENU, C_TVMENU}` to `kSections`.
    - The `C_TVMENU` row gets an extra "Add hold" button that calls `beginCapture(layer, c, true, true)`.
    - When the resolved profile has `GUIDE.withHold()`, show this hint under the row: "Guide (hold) opens the TV
      menu, so a keypad chord that holds # (such as * + #) needs the keypad overlay, a pad with Share, or this
      binding removed."
  - `cheatsheet.cpp`: add the `C_TVMENU` row.
  - Re-run `ctest -R controls_overlay` and update any printed-sheet regex in `VJSmokeTests.cmake` that the new
    row breaks.
- [ ] **Step 4: Run** the full suite: `ctest --test-dir app/build -C Release --output-on-failure`. Expected: all
  PASS, including `controls_overlay_*`, `remap_unit` and `devices_e2e`.
- [ ] **Step 5: Screenshot on the Studio.** Push the branch with `vjr.sh push`, then `vjr.sh run`:
  `virtualjaguar --data-dir <tmp> --screenshot menu-1080.png --screenshot-frame 90 --tv-test-script open-menu.txt yarc.j64`
  at 1280×720 and 1920×1080. Pull the PNGs and attach them to the PR. Never run this locally.
- [ ] **Step 6: Commit** `feat(app): TV in-game menu over desktop mode, opened by the TV menu action (#991)`.
- [ ] **Step 7: Open PR 1** into `develop` with `Refs #991`; title `feat(app): TV menu action and in-game menu (TV mode phase 1)`.

---

## Phase 2 — the TV shell, Home, Library, Game details; Manual mode (PR 2)

Revised 2026-10-16 against the code Phase 1 merged (5f21ab75). Branch `feat/991-tv-phase2`, worktree
`~/Workspace/Provenance/vj-wt/tv-phase2`.

### What Phase 1 left (read before any task)

- `tv::Host` (`tv_host.h`) has `showControlsOverlay()` (not `toggleControlsOverlay`), plus the members listed in Task 5.
- `Shell::ownsInput()` is `open_`, and `backAtRoot()` is `!open_`. `draw(Layer::Base, …)` draws nothing, and `drawUi`
  calls only `draw(Layer::Menu, …)`.
- The menu model (pages, rows, `accept`/`back`) lives inline in `Shell`. **It stays there.** Phase 2 adds a stack of
  Base-layer screens beside it and does not fold the menu into the stack.
- `tvInputFrame(App&, uint64_t)` is in `devices_ui.cpp`. Today it opens the menu only with a game loaded.
- `tv_script.cpp` runs `tests/tv_menu_headless.txt.in`. The verbs are listed in that file's header.
- Smoke harness: `tests/tv_ui_smoke.cpp` provides `FakeHost`, `frames()`, `tapButton()`, a `drew()` text probe and
  `g_imguiErrors`. `DisplaySize` is 1920×1080.
- `theme::Palette` has no accent field. TV chrome uses `hoverEdge` as its accent (it is a tile's hover outline).

### Phase 2 decisions (the approval list at the end of this phase repeats the ones that change the spec)

1. **Sections in PR 2 are Home and Library only**: `enum class Section { Home, Library }`. Phase 3 appends
   Achievements and Settings. Until then, Y on Home does nothing and is not hinted, and Game details has no
   Achievements row.
2. **`auto` acts as Manual until Task 16.** `effectiveSetting(TvSetting)` maps Auto to Manual, and App passes its
   result to `TvMode::configure`. The stored value is kept as written. A nightly started fullscreen must not enter TV
   mode with no trigger wired to leave it. `defaultSetting()` moves to Task 16; until then the config default is
   `manual`.
3. **`tv_host.h` stays pure.** It holds no `lib::` types and no `LibraryService`. The TV module gets its own value types
   (`GameRef`, `Art`, `StateRef`), fetched by value each frame. A `GameRef` is keyed by file path, so a rescan cannot
   leave a screen holding a dead `const Entry*`.
4. **Back belongs to the shell.** Screens never see `Intent::Back`. The shell pops a pushed screen; on Library's root
   it goes to Home; on Home it does nothing.
5. **A launch arms the release guard.** Every launch from a TV screen calls `Shell::launched()`
   (`armReleaseGuard()` + `clearHolds()`), so the A that started the game does not also reach it.
6. **Start (Intent::Menu) opens Game details** while the Base layer owns input; a right-click does the same with a
   mouse. If a player binds `C_TVMENU` to Start, Start still opens Details on these screens; F4 and the Share hold
   still toggle TV mode.
7. **Keep `app.cpp` edits small** (#967 edits it too): config read/write, `--tv`, `toggleTvMode()`. Everything else
   goes in `tv_app_host.cpp`, `devices_ui.cpp` and `ui.cpp`.

### Screen geometry (design units, 1080 high; `x` runs to `W = viewport width / u`)

Shared chrome, every Base screen:
- **Top bar** at `(safeX, safeY)`, 56 high:
  - an accent square 22×22 (`hoverEdge`), gap 16, then "VIRTUAL JAGUAR" at 34;
  - gap 56, then the section tabs at body 30, gap 40, flanked by LB / RB key pills at 24 (2u outline, radius 8). The
    active tab has a 4u `hoverEdge` underline 6u below its text, and the others are `textMuted`;
  - right-aligned to `W − safeX`: `raStatus()` at caption 26 `textMuted`, gap 24, `clockText()` at 26 in `text`.
- **Content area**: from y = 150 (54 + 56 + 40) to y = 968.
- **Hint bar**: `widgets::hintBar` at y = 968..1026, x = safeX..W − safeX. The shell appends {"LB RB", "Sections"}
  before the screen's last hint.

**Home** (mockup A):
- **Hero** at y = 150, 380 high:
  - the box art 266×372 at x = safeX;
  - a text column from x = safeX + 266 + 56, bottom-aligned with the art:
    - the shelf name in upper case, at 26 `textMuted`;
    - the title at `titleLarge` (84), cut with an ellipsis at the column width;
    - "<region> · Cartridge" or "<region> · Jaguar CD", at 28 `textSecondary` (the region and its dot are dropped
      when the region is empty);
    - "N save states · newest <d Mon YYYY>", or "No save states", at 28;
    - the button row at 28: glyph(Accept) "Play"; glyph(Alt) "Resume last state" (only when allowed, per Task 8);
      glyph(Menu) "Details".
- **Shelves** from y = 570:
  - each shelf is a title at body 30, a gap of 22, then tiles 210×294 with a gap of 32, starting at x = safeX + 12;
  - shelf pitch 386. The focused shelf is drawn in the first slot; the shelves above it are scrolled off. The next
    shelf shows in part, clipped at y = 968. Vertical scroll eases with a `Tween`;
  - tiles may run past the right safe edge (spec §6).

**Tile** (Home and Library):
- the art (`Art::tex`) fitted inside the tile; otherwise a placeholder: `cardBg`, with the title in `placeholderText`
  at 26, wrapped inside 18u padding and bottom-aligned;
- a "CD" badge at 24 (`badgeBg`/`badgeText`) top-right for a disc;
- focus as in `widgets::row`: scale to `focusScale` about the centre, a `ring` of 5u in `text`, a drop shadow, all
  driven by `FocusTweens`.

**Library** (mockup B under the shared top bar; the mockup's logo is in the top bar instead):
- **Rail** at x = safeX, 344 wide, from y = 150:
  - four items (All games, Favorites, Recently played, Jaguar CD), each at body 30 with 12/20 padding, 54 high,
    8 apart, radius 10;
  - the selected list gets a `cardBgHover` fill and `text`; the others are `textMuted`;
  - while the rail has focus, its focused item also gets the ring.
- **Grid** from x = safeX + 344 + 64 to W − safeX:
  - a caption at y = 150, at 26 `textMuted`: "<list name> · N games" ("1 game" in the singular), then
    "   Filter: <text>" while filtering;
  - tiles from y = 196 in 6 columns with a column gap of 32: `tileW = (gridW − 160) / 6`, `tileH = tileW × 7 / 5`;
  - row pitch `tileH + 72`. The 72 keeps room for the focused tile's caption: "<region> · Cartridge|Jaguar CD · N
    states" (the last part dropped at 0), at 24 `textSecondary`, at most 2 × tileW wide;
  - `visibleRows = max(1, floor((968 − 196 + 72) / (tileH + 72)))`. A partial next row is drawn and clipped at 968.
- **Letter picker** (modal over Library):
  - a 0.6 scrim, then a centred `panel`: the title "Jump to letter" at 30, then a 6×6 grid of 96u cells 16 apart over
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
  - a letter with no title (`firstWithInitial == -1`) is drawn `textMuted`, and Accept on it does nothing.

**Game details**:
- the art 266×372 at (safeX, 150);
- right of it at x = safeX + 266 + 56: the title at `titleSmall` (56), then the region/kind line at 28;
- rows from y = 150 + 56 + 28 + 40, 900 wide, drawn with `widgets::row`: 72 high, or 112 for Resume rows, which carry a
  128×96 `thumbBox`.

---

### Task 6: the mode state machine and input ownership (pure)

**Files:** Create `app/src/tv/tv_mode.h/.cpp` and `app/tests/tv_mode_test.cpp`. Register `tv_mode_unit` in
`cmake/VJTvTests.cmake`, next to `tv_focus_unit` (pure, no SDL).

**Interfaces (produces):**

```cpp
namespace vj::tv {
enum class TvSetting { Off, Manual, Auto };
const char* settingKey(TvSetting s);                       // "off" | "manual" | "auto"
bool settingFromKey(const std::string& k, TvSetting& out);
TvSetting effectiveSetting(TvSetting stored);              // Phase 2: Auto -> Manual (no Auto triggers yet); Task 16 makes it the identity
struct TvConfig { TvSetting setting = TvSetting::Manual; bool autoFullscreen = true, autoController = true; };
class TvMode {
public:
    static constexpr float kMouseOffPx = 32.0f;
    enum class Cause { None, Manual, Fullscreen, Controller };
    void configure(const TvConfig& c, bool fullscreenNow);   // Off -> off; Auto + autoFullscreen + fullscreen -> on
    bool active() const; Cause cause() const;
    void toggle();                           // View > TV mode, the in-game menu, C_TVMENU with no game; no-op when Off
    void setOn(bool on);                     // --tv, the remembered Manual state; no-op when Off
    void onFullscreen(bool fs);              // Auto + autoFullscreen: enter -> on, leave -> off
    void onPadPress(bool desktopOwnsInput);  // Auto + autoController + desktopOwnsInput -> on (Cause::Controller)
    void onMouseMove(float dx, float dy);    // accumulates; > 32 px -> off, only when cause == Controller and not held by the fullscreen trigger
    void onNonNavKey();                      // same rule as the mouse
};
enum class Owner { DesktopUi, Game, TvUi };
Owner inputOwner(bool tvActive, bool gameRunning, bool menuOpen);   // the §3.1 table
}
```

- [ ] **Step 1: Write the failing tests** (every §8 trigger order; the Auto cases test the pure machine, which Phase 4
  wires up):

```cpp
TvMode m; m.configure({TvSetting::Off}, false); m.toggle(); m.onFullscreen(true); m.onPadPress(true); CHECK(!m.active());
m.configure({TvSetting::Manual}, false); m.onFullscreen(true); m.onPadPress(true); CHECK(!m.active()); m.toggle(); CHECK(m.active());
m.configure({TvSetting::Auto}, false); m.onFullscreen(true); CHECK(m.active() && m.cause() == TvMode::Cause::Fullscreen);
m.onMouseMove(100, 0); CHECK(m.active());                                    // fullscreen holds it
m.onFullscreen(false); CHECK(!m.active());
m.onPadPress(false); CHECK(!m.active());                                     // the game owned the pad
m.onPadPress(true); CHECK(m.active() && m.cause() == TvMode::Cause::Controller);
m.onMouseMove(20, 0); CHECK(m.active()); m.onMouseMove(0, 20); CHECK(!m.active());   // 28.3 then > 32 px total
m.onPadPress(true); m.onNonNavKey(); CHECK(!m.active());
m.onPadPress(true); m.onFullscreen(true); m.onMouseMove(100, 0); CHECK(m.active());   // controller-on, then fullscreen holds it
m.toggle(); CHECK(!m.active()); m.onMouseMove(100, 0); m.onFullscreen(true); CHECK(m.active());   // manual wins until the next edge
TvConfig noFs{TvSetting::Auto, false, true}; m.configure(noFs, true); m.onFullscreen(false); m.onFullscreen(true); CHECK(!m.active());
m.configure({TvSetting::Off}, false); m.setOn(true); CHECK(!m.active());
m.configure({TvSetting::Manual}, false); m.setOn(true); CHECK(m.active() && m.cause() == TvMode::Cause::Manual);
CHECK(effectiveSetting(TvSetting::Auto) == TvSetting::Manual && effectiveSetting(TvSetting::Off) == TvSetting::Off);
CHECK(inputOwner(false, false, false) == Owner::DesktopUi && inputOwner(false, true, false) == Owner::Game);
CHECK(inputOwner(true, false, false) == Owner::TvUi && inputOwner(true, true, false) == Owner::Game);
CHECK(inputOwner(false, true, true) == Owner::TvUi && inputOwner(true, true, true) == Owner::TvUi);
TvSetting s; CHECK(settingFromKey("auto", s) && s == TvSetting::Auto && !settingFromKey("bogus", s));
```

- [ ] **Step 2: Run** `ctest --test-dir app/build -R tv_mode_unit`. Expected: it fails to build.
- [ ] **Step 3: Implement** `tv_mode.cpp`. The `toggle()` cause is `Manual`. A manual toggle wins until the next
  fullscreen or pad edge.
- [ ] **Step 4: Run** the same command. Expected: PASS.
- [ ] **Step 5: Commit** `feat(app): TV mode state machine and input ownership (#991)`.

### Task 7: the screen stack, the chrome, Manual TV mode, `--tv`, the View menu

**Files:**
- Create:
  - `app/src/tv/tv_screen.h` (`Section`, `DrawCtx`, `Screen`);
  - `app/src/tv/tv_chrome.cpp` (the Base window, the top bar, the hint bar);
  - `app/src/tv/tv_home.cpp` with a stub `HomeScreen` (one text line, "Home"), which Task 8 fills;
  - `app/src/tv/tv_library.cpp` with a stub `LibraryScreen` ("Library"), which Task 9 fills.
- Modify:
  - `tv_host.h`, `tv_app_host.h/.cpp`: the Host members below;
  - `tv_shell.h/.cpp`: the stack, sections, ownership and the Base-input rules;
  - `devices_ui.cpp` `tvInputFrame`: `C_TVMENU` with no game;
  - `app.h/.cpp`: the `tvMode`/`tvSetting` members, `toggleTvMode()`, config read/write and `--tv`;
  - `main.cpp`: `--tv` and its usage line;
  - `ui.cpp`: `drawUi` and the in-window View menu item "TV mode";
  - `platform/commands.h/.cpp`, `desktop.cpp`: `Cmd::TvMode`;
  - `settings_reset.cpp`: the five `ui.tv_*` keys are Interface;
  - `tv_script.cpp` and `tests/tv_menu_headless.txt.in`: new verbs and cases;
  - `cmake/VJTvTests.cmake`, `app/CMakeLists.txt`: the new sources.
- Test: `tv_ui_smoke.cpp`, `platform_test.cpp`, `settings_reset_test.cpp`, `tv_menu_headless`.

**Interfaces (produces):**

```cpp
// tv_host.h, added
virtual bool tvActive() const = 0;
virtual bool tvAvailable() const = 0;          // the setting is not Off
virtual void setTvActive(bool on) = 0;         // through App::toggleTvMode's path; no-op when Off
virtual std::string clockText() const = 0;     // "8:42 PM" or "20:42" (SDL_GetDateTimeLocalePreferences)
virtual std::string raStatus() const = 0;      // "" unless signed in; "RetroAchievements: <user>", + " · Hardcore" when wanted

// tv_screen.h
enum class Section { Home, Library };          // Phase 3 appends Achievements, Settings
struct DrawCtx {
    ImDrawList* dl; Metrics m; ImVec2 pos, size;   // the content area (between the top bar and the hint bar), pixels
    bool hovered;                                  // the Base window is hovered (not a desktop window above it)
    ImVec2 mouse; bool clicked, rightClicked; float wheel; double now;
};
class Screen {
public:
    virtual ~Screen() = default;
    virtual const char* id() const = 0;            // "home" | "library" | "letters" | "details": FocusMemory keys, tests
    virtual void intent(Intent in) = 0;            // never Back, SectionPrev or SectionNext (the shell's)
    virtual bool typed(char c) { return false; }   // 'a'..'z', '0'..'9', '\b'; true = used
    virtual void draw(const DrawCtx& c) = 0;       // draws, and does hover-focus / click / wheel from c
    virtual std::vector<widgets::Hint> hints() const = 0;   // last = the way out
    virtual bool modal() const { return false; }   // drawn over the screen below it, which is drawn first
};

// tv_shell.h, added
constexpr const char* kBaseWindowName = "TV##vjtvbase";
bool baseActive() const;                    // host.tvActive() && !host.gameLoaded()
Section section() const;
void setSection(Section s);                 // replaces the stack with that section's root screen (created fresh; focus via memory())
void push(std::unique_ptr<Screen> s);
void pop();                                 // never pops the root
std::string screenId() const;               // the top screen's id(), "" when the stack is empty
FocusMemory& memory();
void launched();                            // armReleaseGuard() + clearHolds()
Host& host();

// App, added
tv::TvMode tvMode; tv::TvSetting tvSetting = tv::TvSetting::Manual;
void toggleTvMode();                        // tvMode.toggle()
// CliArgs: bool tv = false;   Cmd::TvMode after Cmd::HomeLibrary;   MenuState: bool tvMode = false, tvAvailable = false (in operator==)
// DesktopActions: virtual void toggleTvMode() = 0;
```

Shell behaviour:
- `ownsInput()` is `inputOwner(host.tvActive(), host.gameLoaded(), open_) == Owner::TvUi`.
- `backAtRoot()`:
  - menu open: false;
  - `baseActive()`: the stack holds only the root and the section is Home;
  - otherwise: true.
- The first `frame()` with `baseActive()` and an empty stack calls `setSection(Home)`. The stack is kept while a game
  runs, so closing the game lands back where the player was.
- Base-layer input in `handleEvent`, on top of the menu's rules:
  - **Escape key-down with `backAtRoot()`:** not consumed and not fed, so it keeps its desktop meaning.
  - **Start:** fed as `Intent::Menu` and consumed, but only while `baseActive()`. With the menu open it stays the
    app's, as today.
  - **Typing:** a key-down with no Ctrl/Alt/Gui whose `e.key.key` (the layout-aware keycode, not the scancode) is
    a–z or 0–9 goes to `top()->typed(lowercase)`. Backspace goes to `top()->typed('\b')`. A key the screen uses is
    consumed and not fed to the nav. A Backspace it does not use falls through to Back.
  - **Cursor:** a pad event sets `padDriven_`, and mouse motion clears it. While it is set, the Base window calls
    `ImGui::SetMouseCursor(ImGuiMouseCursor_None)`.
- `frame()` with the menu closed and `baseActive()` routes each intent:
  - SectionPrev / SectionNext: `setSection` ±1, with no wrap;
  - Back: rule 4;
  - anything else: `top()->intent`.
- `draw(Layer::Base)` exists only while `baseActive()`:
  - a full-viewport window `kBaseWindowName` with `NoDecoration | NoBackground | NoBringToFrontOnFocus | NoNav |
    NoSavedSettings`;
  - the chrome, then the stack: the topmost non-modal screen and every modal above it, in order;
  - the hints of `top()`.

App and UI:
- **Config** (`applySettingsFromConfig` / `saveConfig`):
  - read `ui.tv_mode` (default `manual`) into `tvSetting`;
  - `tvMode.configure({effectiveSetting(tvSetting), cfg ui.tv_auto_fullscreen, cfg ui.tv_auto_controller}, fullscreen)`;
  - when that is Manual and `ui.tv_manual_on` is set, `tvMode.setOn(true)`;
  - save `ui.tv_mode`, and `ui.tv_manual_on = tvMode.active()` only while the setting is Manual and the run was not
    started with `--tv`.
- **`--tv`:** `tvMode.setOn(true)` after the config is applied (a no-op when Off).
- **`tvInputFrame`:** `if (menuToggle && !in.capturing()) { if (loaded) toggleMenu(); else app.toggleTvMode(); }`.
  `toggleTvMode` is already a no-op when Off.
- **`drawUi`:**
  - `const bool tvBase = app.tvShell().baseActive();`
  - the in-window menu bar shows as in fullscreen when `tvBase`:
    `showMenu = native ? showInWindowMenu : (!(app.fullscreen || tvBase) || app.menuPinned || menuHot)`;
  - in the no-game branch, `tvBase` draws `Layer::Base` in place of `drawLibraryHome` and the welcome text.
- **Native menu:** `commands.cpp` adds `item(Cmd::TvMode, "TV Mode", "", s.tvAvailable, s.tvMode)` after the
  HomeLibrary item. `desktop.cpp` fills `tvMode`/`tvAvailable` and implements `toggleTvMode()`.
- **AppHost:**
  - `clockText` uses `SDL_GetCurrentTime` → `SDL_TimeToDateTime(…, true)`, in 12- or 24-hour form per
    `SDL_GetDateTimeLocalePreferences`;
  - `raStatus` uses `ach.signedIn()`, `ach.userName()` and `ach.hardcoreWanted()`.
- **Script verbs** (add them to the header of `tv_menu_headless.txt.in`):
  - `tvsetting off|manual|auto`: sets `tvSetting` and reconfigures, as Settings will;
  - `expect tv=0|1`;
  - `expect tv-screen=<id>`.

- [ ] **Step 1: Write the failing tests.**
  - `FakeHost` gains `tv = false, tvOn = true` (tvAvailable), `ra`, and `clock = "8:42 PM"`. `frames()` draws
    `Layer::Base` before `Layer::Menu`.
  - Smoke:
    - `tv_shell_walks_sections`: `tv = true, loaded = false`; RB, RB, LB, LB with a frame after each.
      `screenId()` goes "home", "library", "library", "home", "home". Then `frames(120)`;
      `CHECK(g_imguiErrors == 0 && drew("Home") && drew("Library") && drew("8:42 PM"))`.
    - `esc_back_then_root`: on Library, an Escape key-down → `handleEvent` returns true, and after a frame
      `screenId() == "home"`. A second Escape → `handleEvent` returns false, and `backAtRoot()`.
    - `base_only_without_game`:
      - `tv = true, loaded = true`, frames → `ImGui::FindWindowByName(kBaseWindowName) == nullptr` and
        `!shell.ownsInput()`;
      - `tv = false, loaded = false` → no window and `!ownsInput()`.
    - `base_never_captures_keyboard`: on Home after a pad walk, `!ImGui::GetIO().WantCaptureKeyboard`.
    - `pad_hides_cursor`: a pad tap, then a frame → `ImGui::GetMouseCursor() == ImGuiMouseCursor_None`. A
      `SDL_EVENT_MOUSE_MOTION` to `handleEvent`, then a frame → `!= ImGuiMouseCursor_None`.
    - `base_returns_after_game`: on Library, `loaded = true` for 5 frames, `loaded = false` → `screenId() == "library"`.
    - `ra_status_hidden_when_empty`: `ra = ""` → `!drew("RetroAchievements")` for any text that starts so.
  - `platform_test`:
    - the View menu has `Cmd::TvMode`, enabled iff `tvAvailable` and checked iff `tvMode`;
    - dispatching it records `"toggleTvMode"`.
  - `settings_reset_test`: `classifyKey(k) == KeyClass::Interface` for `ui.tv_mode`, `ui.tv_auto_fullscreen`,
    `ui.tv_auto_controller`, `ui.tv_manual_on` and `ui.tv_hint_shown`.
  - `tv_menu_headless`: replace the first block ("No game: the TV-menu action does nothing") with:
    ```
    0     expect loaded=0
    0     expect tv=0
    0     key tap F4
    40    expect tv=1
    40    expect tv-screen=home
    60    key tap Escape          # at root: not the shell's
    100   expect tv=1
    100   key tap F4
    140   expect tv=0
    140   tvsetting off
    160   key tap F4
    200   expect tv=0
    200   tvsetting auto           # acts as Manual in phase 2
    220   key tap F4
    260   expect tv=1
    260   key tap F4
    300   expect tv=0
    300   tvsetting manual
    320   pad down misc1
    920   pad up misc1
    960   expect tv=1
    960   key tap F4
    1000  expect tv=0
    ```
    Shift the later lines' times so they still increase.
- [ ] **Step 2: Run** `ctest --test-dir app/build -R "tv_ui_smoke|platform|settings_reset|tv_menu_headless"`.
  Expected: FAIL.
- [ ] **Step 3: Implement** the interfaces and rules above.
- [ ] **Step 4: Run** the full suite: `ctest --test-dir app/build -C Release --output-on-failure`. Expected: all PASS.
- [ ] **Step 5: Commit** `feat(app): TV shell, Manual TV mode, --tv and the View menu entry (#991)`.

### Task 8: Home

**Files:** Fill `app/src/tv/tv_home.cpp` (`HomeScreen`). Add `formatDate` to `tv_widgets.h/.cpp`. Extend `tv_host.h`
and `tv_app_host.cpp`, plus `achievements.h/.cpp` (one accessor). Test: `tv_ui_smoke.cpp`.

**Interfaces:**
- Consumes: `Screen`, `DrawCtx`, `Shell::launched/push/memory` (Task 7); `LinearFocus`, `FocusMemory` (Task 4);
  `widgets::*`.
- Produces (`tv_host.h`):

```cpp
struct Art { unsigned tex = 0; int w = 0, h = 0; bool loading = false; };   // tex 0: draw the placeholder
struct GameRef {
    std::string key;            // the file path: stable across rescans; what every action is keyed by
    std::string title, region;
    bool disc = false;
    int index = -1;             // AppHost's position in library.entries(), valid for the frame it was fetched in only
};
struct StateRef { int slot; int64_t time; };   // slot -1 = the auto-save; time = seconds since the epoch; newest first
enum class List { All, Favorites, Recent, Disc };
virtual std::vector<GameRef> games(List l) = 0;   // All / Disc / Favorites: by title, case-insensitive; Recent: newest first; never BIOS
virtual Art art(const GameRef& g) = 0;
virtual std::vector<StateRef> states(const GameRef& g) = 0;
virtual Art stateThumb(const GameRef& g, int slot) = 0;
virtual void launch(const GameRef& g) = 0;
virtual void launchState(const GameRef& g, int slot) = 0;
```

- `std::string widgets::formatDate(int64_t secs)` returns "16 Oct 2026" (local time).
- `bool Achievements::hardcoreNextGame() const` is `client_ && enabled_ && hardcoreWanted_ && settingsOk_ &&
  (signedIn() || loginPending_)`, and `hardcoreActive()` becomes `hardcoreNextGame() && (loading_ || gameKnown_)`.
- `AppHost::canLoadState()` becomes `host.isLoaded() ? ach.allowStateLoad() : !ach.hardcoreNextGame()`. With no game,
  `hardcoreActive()` is always false, so X would otherwise offer a load that hardcore refuses.

HomeScreen:
- Shelves, in order: Continue playing (`Recent`, id `recent`), Favorites (`Favorites`), Jaguar CD (`Disc`, id `disc`).
  Empty shelves are hidden.
- Lists are fetched once per `draw` and the focus is clamped to them (`LinearFocus::clampTo`). The shelf index and
  each shelf's tile index are stored in `memory()` under `home/shelf` and `home/<id>`.
- Intents:
  - Left / Right: within the shelf;
  - Up / Down: to the neighbouring visible shelf, at that shelf's remembered index;
  - Accept: `launch` + `launched()`;
  - Alt (X): only when `states(g)` is non-empty and `canLoadState()`, `launchState(g, states[0].slot)` +
    `launched()`;
  - Menu: nothing until Task 10.
- Mouse: hovering a tile focuses its shelf and index; a click is Accept; a right-click is Menu; each wheel notch is one
  shelf down or up.
- With every shelf empty, a centred `panel` reads "Your library is empty. Add game folders in desktop mode: View > TV
  mode turns TV mode off.", at 30 and wrapped.
- Hints: glyph(Accept) "Play"; glyph(Alt) "Resume state" (only when allowed); glyph(Menu) "Details".

AppHost:
- `games()` maps `library.entries()` (skipping `Section::Bios`), `favoriteEntries()` and `recentEntries()` to
  `GameRef`s. `index` is the entry's position in `entries()`.
- Actions find the entry from `index` when `entries()[index].file.path == key`, and otherwise by a scan by path. No
  entry → nothing happens.
- The rest:
  - `art`: `library.art(e)` (`tex` only when `Ready`; `loading` when `Loading`);
  - `states`: from `statesFor(e)`;
  - `stateThumb`: `library.stateThumb` of the matching `StateInfo`;
  - `launch` / `launchState`: the library's own.

- [ ] **Step 1: Write the failing smoke tests.**
  - `FakeHost` gains:
    - lists: `all`, `favs`, `recent`, `disc` (`std::vector<GameRef>`);
    - `std::map<std::string, std::vector<StateRef>> st`;
    - `launchedKey`, plus `launchedState` as `{key, slot}`.
  - `games()` returns copies; `art()` returns `{}`.
  - Tests:
    - `home_hides_empty_shelves`: `recent` 3, `favs` empty, `disc` 2 → `drew("Continue playing") &&
      drew("Jaguar CD") && !drew("Favorites")`.
    - `home_accept_launches_focused`: Right, Accept → `launchedKey == recent[1].key && guardArmed == 1 &&
      holdsCleared == 1`.
    - `home_x_resumes_newest`: `st[recent[0].key] = {{3, 200}, {-1, 100}}`; Alt → `launchedState == {recent[0].key, 3}`.
    - `home_resume_hidden_without_states_or_in_hardcore`:
      - focused game with no states → `!drew("Resume last state")`, and Alt launches nothing;
      - with states → `drew("Resume last state")`;
      - `canLoad = false` → not drawn, and Alt launches nothing.
    - `home_focus_remembered_per_shelf`: `recent` 5, `favs` 5; Down, Right×3, Up, Down, Accept →
      `launchedKey == favs[3].key`.
    - `home_mouse_hover_focuses_and_clicks`: at 1920×1080, move the mouse (`io.AddMousePosEvent`) to tile 2 of the
      first shelf, `(96 + 12 + 2 × 242 + 105, 622 + 147)`; frames(2); then a left click (`AddMouseButtonEvent`, down,
      frame, up, frame) → `launchedKey == recent[2].key`.
    - `home_survives_rescan` (Review focus 7): focus `recent[3]`; replace `recent` with a reordered copy of 2 items
      whose `index` is -1; frames(3) → no ImGui error, and Accept launches one of the 2 items.
    - `home_empty_library_card`.
    - `home_walk_120_frames_no_asserts`: every direction ×10 at 720p, 1080p and 2160p, with `frames(120)` each.
- [ ] **Step 2: Run** `ctest --test-dir app/build -R tv_ui_smoke`. Expected: FAIL.
- [ ] **Step 3: Implement** `HomeScreen`, the Host members, `AppHost`'s members and `hardcoreNextGame`.
- [ ] **Step 4: Run** the full suite (an achievements test covers `hardcoreActive`). Expected: all PASS.
- [ ] **Step 5: Commit** `feat(app): TV Home screen with shelves and hero panel (#991)`.

### Task 9: Library and the letter picker

**Files:** Fill `app/src/tv/tv_library.cpp` (`LibraryScreen`, `LettersScreen`). Test: `tv_ui_smoke.cpp`.

**Interfaces:**
- Consumes: the Host members from Task 8; `GridFocus`, `LinearFocus`, `jumpInitial`, `firstWithInitial` and
  `initialOf` from Task 4; `Shell::push/pop/launched/memory`.
- Produces: `LettersScreen(Host&, Shell&, LibraryScreen& owner)`, which calls `owner.focusTitle(index)` and then
  `shell.pop()`.

LibraryScreen:
- **Rail** (`LinearFocus`, 4 items): moving on the rail switches the list at once. Each list's grid index is
  remembered under `library/<list>`, and the rail under `library/rail`.
- **Grid**: a `GridFocus` with 6 columns over the filtered list.
- **Intents on the rail:** Right or Accept goes to the grid, at the remembered index.
- **Intents on the grid:**
  - a Left edge goes to the rail;
  - Accept: launch + `launched()`;
  - Alt: the Home resume rule;
  - Aux (Y): push `LettersScreen`, focused on the focused title's initial;
  - PagePrev / PageNext: `jumpInitial(titles, index, -1 / +1)`;
  - Menu: nothing until Task 10.
- **Filter:** `typed()` appends a letter or digit and filters by a case-insensitive substring of the title.
  - `'\b'` with a non-empty filter drops the last character and returns true; with an empty filter it returns false,
    which makes it Back (to Home).
  - Switching lists clears the filter.
  - The grid index is clamped to the filtered list.
- **Mouse:**
  - hovering a rail item selects it;
  - hovering a tile focuses it; a click launches it; a right-click is Menu;
  - each wheel notch over the grid moves one row.
- **Hints:**
  - on the rail: glyph(Accept) "Open", Back "Home";
  - on the grid: glyph(Accept) "Play", glyph(Alt) "Resume state" (only when allowed), glyph(Menu) "Details",
    glyph(Aux) "Jump to letter", glyph(PagePrev) + " " + glyph(PageNext) "A–Z", glyph(Back) "Home".

LettersScreen:
- a modal `GridFocus` (6 columns, 36 cells);
- Accept on a letter that has titles: `owner.focusTitle(firstWithInitial(...))`, then `pop()`. On a muted letter it
  does nothing;
- Back pops it (the shell's rule);
- hints: "Jump", "Back".

- [ ] **Step 1: Write the failing smoke tests.**
  - The fixture `all` is six titles in order: "Alien vs Predator", "Atari Karts", "Battlemorph", "Blue Lightning",
    "Cybermorph", "Doom". Every test starts with RB (to Library).
  - Tests:
    - `library_rail_grid_handoff`: Right, Right (grid index 2), Left ×3 (to the rail), Down (Favorites), Up (All),
      Right, Accept → `launchedKey == all[2].key`.
    - `library_lt_rt_jump_initial`: a right-trigger axis event to 32767, then back to 0 → focus on "Battlemorph". The
      same again → "Cybermorph". A left trigger → "Battlemorph". Accept confirms each.
    - `library_letter_picker_jumps`:
      - Y → `screenId() == "letters"`;
      - Right ×3 (to D), Accept → `screenId() == "library"`, and Accept → `launchedKey` is Doom's;
      - Y again, Down ×4, Right (to Z: no title), Accept → still "letters".
    - `library_typing_filters_and_backspace`: key-downs with `key.key = SDLK_D`, then `SDLK_O`, each consumed →
      `drew("All games · 1 game")`. Backspace ×2 → `drew("All games · 6 games")`. Backspace → `screenId() == "home"`.
    - `library_typing_leaves_hotkeys` (Review focus 8): a D key-down with `SDL_KMOD_CTRL`, and an F4 key-down, each
      → `handleEvent` returns false.
    - `library_walk_120_frames_no_asserts`: the rail, the grid, the picker and the filter, at three sizes.
- [ ] **Step 2: Run** `ctest --test-dir app/build -R tv_ui_smoke`. Expected: FAIL.
- [ ] **Step 3: Implement** `LibraryScreen` and `LettersScreen`.
- [ ] **Step 4: Run** the same command. Expected: PASS.
- [ ] **Step 5: Commit** `feat(app): TV Library screen with rail, cover grid and letter jump (#991)`.

### Task 10: Game details, and "Enter / Exit TV mode" in the in-game menu

**Files:**
- Create `app/src/tv/tv_details.cpp` (`DetailsScreen`).
- Modify:
  - `tv_home.cpp`, `tv_library.cpp`: Menu and right-click push Details;
  - `tv_shell.h/.cpp`: a new `Row::TvMode` in the root page;
  - `tv_host.h`, `tv_app_host.cpp`: favourites.
- Test: `tv_ui_smoke.cpp`.

**Interfaces:**

```cpp
virtual bool isFavorite(const GameRef& g) = 0;          // library.isFavorite(entry)
virtual void setFavorite(const GameRef& g, bool on) = 0;
class DetailsScreen : public Screen { public: DetailsScreen(Host&, Shell&, GameRef g); /* id() "details" */ };
```

- **Rows** (refetched each draw, keyed by `g.key`):
  - "Play";
  - one row per state, newest first: "Resume: Slot N" or "Resume: Auto-save", with `sub` = `formatDate(time)` and
    the state's thumbnail. These rows are hidden when `!canLoadState()`;
  - "Favorite: On" or "Favorite: Off".
- **Accept:**
  - Play: `launch` + `launched()`;
  - Resume: `launchState(g, slot)` + `launched()`;
  - Favorite: `setFavorite(g, !isFavorite(g))`.
- **Hints:** glyph(Accept) "Select", glyph(Back) "Back".
- **In-game menu:** a root row after Controls:
  - its label is "Exit TV mode" while `tvActive()`, otherwise "Enter TV mode";
  - it is hidden when `!tvAvailable()`;
  - Accept calls `setTvActive(!tvActive())`, rebuilds the root and keeps the focus on the row. The menu stays open.

- [ ] **Step 1: Write the failing smoke tests.**
  - `FakeHost` gains `std::set<std::string> favSet`.
  - Tests:
    - `details_from_start_on_any_tile`:
      - Home: Right, Start → `screenId() == "details"`; Back → "home"; Accept → `launchedKey == recent[1].key` (the
        focus survived);
      - Library: a right-click on a tile → "details".
    - `details_resume_rows_match_states`:
      - 2 states → exactly 2 drawn texts start with "Resume: ";
      - `canLoad = false` → none.
    - `details_favorite_toggles`: `drew("Favorite: Off")`; Down to the row, Accept → `favSet.count(key) &&
      drew("Favorite: On")`.
    - `details_play_launches`: Accept → `launchedKey`, `guardArmed == 1`.
    - `ingame_tv_toggle_hidden_when_off`:
      - `loaded = true, tvOn = false`, open the menu → neither label is drawn;
      - `tvOn = true, tv = false` → `drew("Enter TV mode")`; Accept on it → `tv && drew("Exit TV mode") &&
        shell.menuOpen()`.
- [ ] **Step 2: Run** `ctest --test-dir app/build -R tv_ui_smoke`. Expected: FAIL.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run** the full suite. Expected: all PASS.
- [ ] **Step 5: Commit** `feat(app): TV Game details and the TV-mode toggle in the in-game menu (#991)`.

### Task 11: screenshots on the Studio, then open PR 2

**Files:** Modify `tv_script.cpp` and the header of `tv_menu_headless.txt.in` (the windowed-only verbs). Do not commit
the screenshot scripts or data dirs: they live in `~/Workspace/Provenance/vj-eval/tvshots/`.

- New verbs, windowed only (headless fails the line with "windowed only"):
  - `wait library`: the script blocks until `!library.scanning() && !library.entries().empty()`, and FAILs after
    120 s;
  - `shot PATH`: `saveWindowScreenshot(PATH)` at the end of the next UI frame;
  - `quit`: ends the run through the normal teardown.
- [ ] **Step 1: Studio data dirs.** Make one per size, with the library folder `~/vj-remote/roms` and
  `window.w`/`window.h` set to 1280×720 and 1920×1080. The 4K shot is the 1920×1080 window with
  `--pixel-density 2`. Write a script `tv-shots.txt`:
  1. `wait library`, then a 5000 ms gap (art);
  2. `shot home.png`;
  3. `key tap Tab`, `shot library.png`;
  4. `pad tap y`, `shot letters.png`;
  5. `key tap Escape`, `pad tap start`, `shot details.png`;
  6. `quit`.
- [ ] **Step 2: Run** on `studio-runner` with `vjr.sh push` and `vjr.sh run`:
  `virtualjaguar --data-dir <dir> --tv --tv-test-script tv-shots.txt` for each size. Pull the PNGs. Never run this
  locally.
- [ ] **Step 3: Compare by eye** with mockups A and B, and fix any drift. If the Studio cannot fetch art, the tiles
  show placeholders; the PR says so.
- [ ] **Step 4: Open PR 2** into `develop`:
  - title `feat(app): TV shell with Home, Library and Game details (TV mode phase 2)`;
  - body with `Refs #991`, the test summary and the screenshots;
  - then bind it (`ccd_pr`) and report CI.

### Phase 2 review focus (added to the list above)

6. **The A that launches a game reaches the game.** `launched()` arms the guard on every launch path (Task 8
   `home_accept_launches_focused`, Task 10 `details_play_launches`).
7. **A rescan while TV mode is showing** (entries rebuilt; a favourite's drive unplugged): screens hold `GameRef`s
   keyed by path and refetch every frame (Task 8 `home_survives_rescan`).
8. **Typing in Library eats a hotkey** (F4, Ctrl/Cmd shortcuts, Esc at root) (Task 9
   `library_typing_leaves_hotkeys`; Task 7 `esc_back_then_root`).
9. **TV mode turns on at startup with nothing to turn it off** (a stored `auto`, a fullscreen launch): `auto` acts as
   Manual until Task 16 (Task 6 `effectiveSetting`; Task 7 `tvsetting auto` script case).
10. **Closing a game from the in-game menu while TV mode is on** must land on the TV screen the player left (Task 7
    `base_returns_after_game`).

### Approval needed before Task 7 starts (spec changes for phase 2)

The rows already in "Changes to the approved spec" that phase 2 uses:
- Homebrew is dropped from the rail.
- The hero shows no year, publisher or achievements.
- X resumes the newest state; Start opens Game details.
- The per-game settings row is dropped.
- `C_TVMENU` with no game toggles TV mode.

New in this revision:
- **A.** Phase 2 has two sections (Home, Library). Y on Home and the Achievements row in Game details come in Phase 3.
- **B.** A stored `auto` acts as Manual until Phase 4.
- **C.** Library keeps the shared top bar. Mockup B's logo moves into it, and the rail holds only the four lists:
  Achievements, Overlays and Settings are sections, not rail rows. The 64u "All games" title becomes the 26u caption
  "All games · N games", because the top bar costs that height.
- **D.** Where the mockups and the plan differ, the plan wins:
  - Library's "X Details" becomes X = resume, Start = Details;
  - Home's "Y Search" becomes Y = Achievements, in Phase 3;
  - mockup A's "Jaguar CD" tab is a shelf on Home and a rail row in Library, not a section.
- **E.** Start opens Game details in TV Home and Library even when the player bound the TV-menu action to Start.

---

## Phase 3 — Settings, Overlays, Achievements, "More settings" (PR 3)

Revised 2026-10-20 against the code Phase 2 merged (#999) and what landed after it: the TV-mode hotkey (#1024), the
desktop library's pad navigation (#1030), the emulation thread phases 1–2 (#1000, #1031), the Pro Controller (#1019),
Neuro (#1001/#1027), disc control (#1006–#1012) and the community cheat import (#1052). Branch `feat/991-tv-phase3`,
worktree `~/Workspace/Provenance/vj-wt/tv-phase3`, off `libretro/develop` abc9054e.

### Phase 3 constraints (overlap with other work)

- The v3.10.0 release PR (#1042, `release/v3.10.0` → `master`) is in flight and carries `app/src/app.cpp`,
  `devices_ui.cpp`, `desktop_nav*`, `achievements*.cpp` and every workflow file. PR 3 targets `develop` as usual; expect a
  back-merge of master into develop while PR 3 is open, and rebase on it before marking ready. Touch `app.cpp` and
  `devices_ui.cpp` only where this phase says (a flag, two one-line hooks), so the rebase is trivial.
- An #967 Phase 3 session edits `app.cpp`, `input.*`, `devices_ui.cpp` and `ui.cpp` too. Nothing in this phase changes
  how input reaches the core or how the UI reads the core: every core-facing call stays inside `App` members that
  already exist (see "Threading" below). Do not add `emu.call` / `emu.post` anywhere in `tv/`.
- No `app/src/ui.cpp` edits beyond the two named in Task 15b (the aspect table lift and the "More settings" window
  placement); no `shaders_ui.cpp` edits beyond the two functions moved out of its anonymous namespace (Task 12).

### What Phase 2 left (read before any task)

- **Sections.** `tv_screen.h`: `enum class Section { Home, Library }`; `Shell::setSection` builds the root screen with a
  two-way conditional (`makeHomeScreen` / `makeLibraryScreen`); `Shell::stepSection` clamps at `Section::Library`;
  `tv_chrome.cpp` draws `const Section sections[] = {Home, Library}` with `sectionLabel()`; `backAtRoot()` is "stack of one
  and section Home". All five places change when sections are added (Task 14).
- **`tv_host.h` is pure** (Phase 2 decision 3): no `lib::`, no `ra::`, no `OptionDef`, no App type. Phase 3 keeps it so.
  The TV module gets its own value types (`SettingRow`, `ToggleRow`, `AchItem`, `AchGame`) filled by `AppHost`.
- **The in-game menu** is Shell's row model (`Shell::Row::Kind {Resume, Save, Load, Controls, TvMode, Reset, Close, Slot,
  No, Yes}`, `Shell::Page`, `buildRoot()`, `accept()`, `back()` in `tv_shell.cpp`); `tv_ingame.cpp` only draws it (rail = title
  + page name, list = rows, hint bar). Root order today: Resume, Save state, Load state, Controls, Enter/Exit TV mode,
  Reset, Close game. The TV-mode row sits before Reset, not last as spec §4 lists it.
- **Input hand-off is already in place for "More settings".** `Shell::ownsInput()` is false on the Base layer whenever
  `Host::desktopWindowOpen()` (= `App::desktopWindowOpen()`: Settings, States, Cheats, About, the library window, the keypad
  overlay, any ImGui popup) is true, and `Shell::frame` resets the nav on that edge. With the in-game menu open the shell keeps
  the input regardless (`open_`), so a window opened from the menu needs its own hand-off (Task 13).
- **ImGui gamepad navigation already exists, fed by us.** `App::initImGui` clears `NavEnableKeyboard | NavEnableGamepad` and
  sets `ImGui_ImplSDL3_SetGamepadMode(Manual, nullptr, 0)`: the backend submits no pad state, on purpose. The desktop
  library's popups (#1030) turn nav on per frame with `deskSetPopupNav(bool)` (called from `deskPadBeforeNewFrame`, between
  `ImGui_ImplSDL3_NewFrame` and `ImGui::NewFrame`) and press ImGui's gamepad keys from our intents with
  `deskFeedPopupIntents(intents)` (`desktop_nav_popup.h`, header-only, smoke-tested in `tv_ui_smoke.cpp` `padPopupNav`).
  "More settings" reuses both; it does not switch the backend's gamepad mode and does not use `input.padHandles()`.
- **Pad ownership while a desktop window is open:** `deskPadActive()` is false with Settings open, so the desktop nav never
  competes with "More settings". The shell's `handleEvent` runs before `deskPadHandleEvent` in `App::handleEvent`; a
  "More settings" glue goes before both.
- **Smoke harness** (`tests/tv_ui_smoke.cpp`): `FakeHost` (lists `all/favs/recent/disc`, `st`, `favSet`, `tv`, `tvOn`, `ra`,
  `desktopOpen`, `switchPad`, `keyboardLast`), `frames(host, shell, n)`, `drew(text)`, `g_texts`, `g_imguiErrors` (not
  `g_imguiAsserts`), `tapButton`, `down/up/accept/back/rb/lb/rightN/leftN`, `keyEvent(shell, scancode, key, down, mod)`,
  `rightClick`, `pullTrigger`, `homeSetup/librarySetup/detailsSetup`, `focusedLabel(shell)`. It links no App file
  (`VJTvTests.cmake` `vj_tv_ui_smoke`): new screen sources join its source list; `tv_app_host.cpp` never does.
- **Script runner** (`tv_script.cpp`, `tests/tv_menu_headless.txt.in`): verbs `load, key, pad, axis, mouse, focus, typing,
  capture, tvsetting, navdefault, saveconfig, wait, shot, quit`; expects `fullscreen, menu-visible, menu-pinned, menu-open,
  tv, tv-screen, tv-owns-input, home-visible, library-window, desk-pad, desk-scroll, desk-pending, desk-popup, cfg, paused,
  loaded, capturing, fast-forward, core-input, kb-bind`. `tvsetting` calls `App::configureTvMode()`. Headless runs have no
  ImGui context: anything that reads `ImGui::GetIO()` must guard on `ImGui::GetCurrentContext()`.
- **Config:** `App::saveConfig()` never writes `ui.tv_mode` (comment in `app.cpp`: a stored value would pin the default Task 16
  moves). The TV Settings row that changes it writes the key itself, on change only, then `configureTvMode()`.
- **Core options before the first load:** `host.optionDefs()` is empty until the core pushes its definitions
  (`retro_load_game`); with no game run this session the three core rows have no `OptionDef` and are hidden by the
  `def == nullptr` rule.
- **Threading (#967, `docs/app-emulation-thread-design.md` §5).** What a TV screen needs is UI-thread safe through these
  App-level entry points, all of which `AppHost` may call:
  - actions that already wrap `emu.call()`: `App::saveState`, `loadState`, `reset`, `closeContent`, `menuPause`, `menuResume`,
    `library.launch/launchState`;
  - the CoreHost option table, which is the UI thread's channel by design (option writes are mirrored and `post`ed; readers
    answer from the pinned snapshot): `host.optionDefs()`, `optionVisible(key)`, `optionLocked(key)`,
    `optionOverridden(key)`, `optionValue(def)`, `setOptionValue(key, value)` — the same calls `drawOption` (`ui.cpp`) makes;
  - app settings: `app.present`, `app.vsync` + `applyVsync()`, `app.volume` + `applyAudioSettings()`, `app.showFps`,
    `setThemeChoice(key)`, `theme::choiceKey()`, `app.themeStore().list()`, `video.shader` (`ShaderChain`: main thread's);
  - overlays: `input.overlay.open` (`KeypadOverlay`), `overlayEvent(input.controls, OverlayEvent::Toggle)` and
    `input.controls.show` (`cheatsheet.h`), `app.debug.tools()` / `DebugTool::isOpen/toggle` / `debug.enabled()` (UI-side
    registry; `enabled()` is also false while the emulation thread is on: `DebugSession::threadBlocked()`);
  - achievements (UI thread, under `clientMu_`): `ach.signedIn()`, `hardcoreActive()`, `hardcoreBlocker()`,
    `clientSnapshot()` (`gameId`, `gameTitle`, `summary`), `achievementRows(rows)`, `games()`, `gamesTabShown()`
    (must run every frame the list shows: it loads the cache and fetches progress), `progressKnown()`, `playGame(entry)`,
    `trackers` (a `std::map<unsigned, std::string>`), `images.get(url)` (`ra::ImageView {state, tex, w, h}`).
  Nothing else of `app.host` is a TV call: `runFrame`, `frame()`, `inFrame()`, states, SRAM and memory are owner-thread only.

### Phase 3 decisions (the approval list at the end repeats the ones that change the spec)

1. **Sections become Home, Library, Achievements, Settings**, in that order (LB/RB). Overlays is not a section: with no game
   running only "Show FPS" means anything. It is a screen reached from the in-game menu.
2. **One screen class serves both layers.** `SettingsScreen`, `OverlaysScreen` and `AchievementsScreen` are `tv::Screen`s.
   On the Base layer they are section roots (Settings, Achievements). In the in-game menu, a new `Row::Kind` per screen pushes
   the screen into a new menu slot, `Shell::menuScreen_` (one `std::unique_ptr<Screen>`): while it is set, `drawMenu` draws the
   screen in the panel's list area (a `DrawCtx` over that rectangle) and `Shell::intent` forwards everything but Back to it;
   Back clears the slot and returns to the root row it came from. The menu's page model is untouched.
3. **Settings is a rail-and-list** (the Library rail metrics): rail groups Picture, Speed, Sound, Interface; the list holds
   that group's rows, plus "More settings…" as the last row of Interface. Left/Right change a row; Accept steps it forward
   (wrapping) so a one-handed player can still change it; Accept on "More settings…" opens the desktop window.
4. **The curated rows and their keys** (spec §5): `shader` (`video.shader`: "Off" + every preset the desktop list offers,
   skipping presets `presetNeedsArraysOfArrays()` when `!glHasArraysOfArrays()`), `integer` (`present.integerScale`), `aspect`
   (`present.aspectOverride`, the 4-entry table), `resolution` (`virtualjaguar_internal_resolution`), `risc_clock`
   (`virtualjaguar_risc_clock_scale`), `idle_skip` (`virtualjaguar_risc_idle_skip`), `volume` (`app.volume` in 10 % steps),
   `fps` (`app.showFps`), `vsync` (`app.vsync`), `theme` (System / Dark / Light / "Custom: <name>"), `tv_mode` (Off / Manual
   / Auto; Auto still acts as Manual until Phase 4, which also adds the two Auto rows), `more`.
5. **Overlays rows:** `fps`, `keypad`, `controls`, then one `tool:<DebugTool::id>` per registered tool with a `toggle`.
   `fps` and the tool rows toggle in place and the menu stays open; `keypad` and `controls` show a mouse-driven window over
   the game, so Accept on them closes the menu first (as the Controls row does today).
6. **Achievements screen.** With a tracked game: a header (icon, title, "N of M achievements", the hardcore chip), the active
   leaderboard trackers as read-only rows at the top, then one row per achievement in rc_client's bucket order (badge,
   title + points, the status line `drawThisGame` builds). With no game: the RetroAchievements Jaguar list (`ach.games()` through
   `ra::viewGames(All, Title, "")`, so games without a set are skipped), each row "title · unlocked/total" when progress is
   known, and Accept on a game the library has (`libIndex >= 0`) launches it (`playGame`, then `Shell::launched()`). Not signed
   in: the one line "Sign in from Settings > Achievements in desktop mode". Not available (no rc_client in this build):
   the same line with "not available in this build".
7. **Y (Aux) on Home opens the Achievements section** with its hint; the row "Achievements" in Game details is shown only when
   `Host::achGameId(g) != 0` (the RA list has an entry whose `libIndex` resolves to this file) and opens the section with that
   game focused.
8. **"More settings…" in TV mode** (spec §5): `App::showSettings = true` plus a flag `App::tvMoreSettings_`. While the flag
   and `showSettings` are both true: ImGui gamepad *and* keyboard navigation are on (through `deskSetPopupNav`, extended to
   take a second reason), the pads drive ImGui through a `tv::NavInput` of its own fed to `deskFeedPopupIntents`, the Settings
   window is centred at 80 % of the viewport with `ImGui::PushFont(nullptr, FontSizeBase × s)` where `s` makes the body text
   `30u` tall, and B (`Intent::Back`) or Esc closes it when no popup is open and no item is active. The shell steps aside on
   its own on the Base layer (`desktopWindowOpen()`); with the in-game menu open, the glue consumes the pad events before the
   shell sees them, and the menu is still there when the window closes.
9. **No new App members for settings beyond the flag.** Every write goes through the member or method the desktop control
   uses, followed by `app.saveConfig()`; `ui.tv_mode` is written on change only (`cfg.set("ui.tv_mode", settingKey(...))`).
10. **The pure parts get their own file** (`tv_settings_model.h/.cpp`): row building from plain inputs, unit-tested without
    App or ImGui. `tv_app_host.cpp` only gathers the inputs.
11. **Keep `app.cpp` edits to three lines** (#967 and the release both touch it): the flag, the hand-off call in
    `handleEvent`, and the Esc rule in `handleHotkey`. The glue lives in `app/src/tv_more_settings.cpp`.

### Screen geometry (design units, 1080 high)

- **Settings / Achievements (Base layer):** the Library layout — rail at `x = safeX`, 344 wide, items 54 high, 8 apart; the
  list from `x = safeX + 344 + 64` to `W − safeX`, `widgets::row` 72 high (112 for achievement rows with a badge
  `thumbBox` 96×96), gap 14, scrolled with a `Tween` as Game details does; a caption line at `y = 150` at 26 `textMuted`.
- **Inside the in-game menu:** the same list drawn in the menu panel's list area (`listX`, `listY`, `listW`, `listH` as
  `drawMenu` computes them); the rail is the menu's own (title + page name), so the Settings groups become a first list level:
  Accept on a group opens its rows, Back returns to the groups.
- **A setting row:** the label at 30 `text` left; the value right-aligned at 30 with "‹" and "›" glyphs 24 apart when the row
  can step that way; `note` under the value at 24 `textMuted` (read-only rows); a disabled row drawn muted (`RowArgs::enabled`).
- **An overlay row:** the label left and "On" / "Off" right; disabled rows carry the note.
- **The Achievements header** (game loaded): the icon 96×96 at `(listX, 150)`, the title at `titleSmall`, the count line at
  28, the chip "Hardcore" / "Softcore" / "Softcore (hardcore is paused: <blocker>)" at 24; the list starts under it.

---

### Task 12: the settings model (pure) and the TV Settings screen

**Files:**
- Create `app/src/tv/tv_settings_model.h/.cpp` (pure; includes `emu_snapshot.h` for `OptionDef`), `app/src/tv/tv_settings.cpp`
  (`SettingsScreen`), `app/tests/tv_settings_model_test.cpp`.
- Modify `tv_host.h`, `tv_app_host.h/.cpp`, `tv_screen.h` (the factory), `cmake/VJTvTests.cmake`, `app/CMakeLists.txt`.
- Modify `shaders_ui.cpp`: move `applyPreset` and the two `Listing`s out of the anonymous namespace as
  `std::vector<std::string> shaderPresetPaths(App&)` (built-in then user folder, full paths, `rescan` on first use and when the
  folder changes, skipping presets that need arrays of arrays on a GL that lacks them) and `void applyShaderPreset(App&, const
  std::string& path)`; declare both in `shaders_ui.h` (new, two lines) next to `drawShaderSettings`.
- Modify `video.h`: `struct AspectChoice { const char* label; float value; }; extern const AspectChoice kAspectChoices[4];`
  (defined in `video.cpp`), and `ui.cpp` `drawVideoTab` reads it instead of its two static arrays.
- Test: `tv_settings_model_unit` (new), `tv_ui_smoke`.

**Interfaces:**

```cpp
// tv_host.h, added
struct SettingRow {
    std::string id, label;             // ids: shader integer aspect resolution risc_clock idle_skip volume fps vsync theme tv_mode more
    std::vector<std::string> choices;  // empty for "more"
    int index = 0;
    bool readOnly = false;
    std::string note;                  // shown under the value: why it is read-only
    int group = 0;                     // 0 Picture, 1 Speed, 2 Sound, 3 Interface
};
virtual std::vector<SettingRow> settingRows() = 0;             // only the rows that exist in this build / session
virtual void setSetting(const std::string& id, int index) = 0; // applies and saves, as the desktop control would
virtual void openMoreSettings() = 0;                           // showSettings = true (+ the TV hand-off flag)

// tv_settings_model.h (pure)
namespace vj::tv {
const char* settingGroupLabel(int group);   // "Picture" "Speed" "Sound" "Interface"
// One core option as a row.  hidden: no definition, not visible, or locked.  readOnly + note when overridden.  A current value
// outside the list (a typed RISC clock) is appended as "<value> (custom)" and selected.
SettingRow coreOptionRow(const std::string& id, const std::string& label, int group, const OptionDef* def,
                         const std::string& current, bool visible, bool locked, bool overridden, bool* hidden);
SettingRow onOffRow(const std::string& id, const std::string& label, int group, bool on);
SettingRow volumeRow(float volume);                        // "0 %" .. "100 %", index = round(volume * 10)
SettingRow aspectRow(float aspectOverride, const AspectChoice* table, int n);
SettingRow themeRow(const std::string& choiceKey, const std::vector<std::string>& customNames);   // keys: system dark light custom:<name>
std::string themeKeyAt(int index, const std::vector<std::string>& customNames);
SettingRow tvModeRow(TvSetting stored);                     // Off / Manual / Auto
SettingRow shaderRow(const std::string& currentPreset, bool enabled, const std::vector<std::string>& presetPaths);   // "Off" + file stems
int stepIndex(const SettingRow& r, int delta, bool wrap);   // Left/Right: clamped; Accept: wraps
}
```

Rules (`AppHost::settingRows`):
- Order and group: shader, integer, aspect, resolution (Picture); risc_clock, idle_skip (Speed); volume (Sound); fps, vsync,
  theme, tv_mode, more (Interface).
- The shader row exists only when `ShaderChain::builtWithHeaders() && video.shader.libraryLoaded()`; `index 0` = "Off"
  (`enabled = false`); any other index: `applyShaderPreset(app, path)`.
- Core rows: `coreOptionRow(id, label, group, defFor(key), host.optionValue(*def), host.optionVisible(key),
  host.optionLocked(key), host.optionOverridden(key), &hidden)` where `defFor` scans `host.optionDefs()`; `setSetting` calls
  `host.setOptionValue(key, def->values[index].value)` (never for the "(custom)" index), the call `drawOption` makes, so the
  hardcore disallowed-option handling (`Achievements::checkSettings`) is identical.
- `setSetting("tv_mode", i)`: `tvSetting = (TvSetting)i; cfg.set("ui.tv_mode", settingKey(tvSetting)); configureTvMode();`
  — choosing Off while TV mode is on turns it off (`configure` does); the screen is then gone, which is what the player asked.
- Every `setSetting` ends with `app.saveConfig()`.

`SettingsScreen` (`id() "settings"`, `makeSettingsScreen(Host&, Shell&)`):
- Rows are fetched every draw (`settingRows()`), grouped by `group`; the rail is the groups that have rows; the list is the
  focused group's rows. Rail focus and each group's row index are remembered under `settings/rail` and `settings/<group>`.
- Intents: on the rail Up/Down pick a group, Right or Accept enter the list; in the list Up/Down move, a Left edge returns to
  the rail, Left/Right `stepIndex(r, ∓1, false)` → `setSetting`, Accept `stepIndex(r, +1, true)` → `setSetting` (on `more`:
  `openMoreSettings()`); read-only rows ignore Left/Right/Accept.
- Mouse: hovering focuses; a click on the value's left/right half steps that way; the wheel moves the focus.
- Hints: rail: glyph(Accept) "Open", glyph(Back) "Home"; list: glyph(Left)+" "+glyph(Right) "Change", glyph(Accept) "Next"
  ("Open" on `more`), glyph(Back) "Groups".
- In the menu slot (decision 2) the rail is the first list level: Accept on a group enters it, Back returns to the groups,
  Back on the groups clears the slot.

- [ ] **Step 1: Write the failing tests.**

```cpp
// tv_settings_model_test.cpp: settings_rows_core_option_rules (Review focus 5)
OptionDef d; d.key = "virtualjaguar_internal_resolution"; d.values = {{"1x", "1x"}, {"2x", "2x"}}; bool hidden = false;
SettingRow r = coreOptionRow("resolution", "Internal resolution", 0, &d, "2x", true, false, false, &hidden);
CHECK(!hidden && r.index == 1 && r.choices.size() == 2 && !r.readOnly && r.group == 0);
coreOptionRow("resolution", "x", 0, &d, "2x", false, false, false, &hidden); CHECK(hidden);          // not visible
coreOptionRow("resolution", "x", 0, &d, "2x", true, true, false, &hidden); CHECK(hidden);           // locked by the app
r = coreOptionRow("resolution", "x", 0, &d, "2x", true, false, true, &hidden); CHECK(!hidden && r.readOnly && !r.note.empty());
r = coreOptionRow("risc_clock", "x", 1, &d, "137%", true, false, false, &hidden); CHECK(r.choices.back() == "137% (custom)" && r.index == 2);
coreOptionRow("resolution", "x", 0, nullptr, "", true, false, false, &hidden); CHECK(hidden);       // no definition yet
// the app rows
CHECK(volumeRow(0.75f).index == 8 && volumeRow(0.75f).choices.size() == 11 && volumeRow(1.0f).choices[10] == "100 %");
CHECK(onOffRow("fps", "Show FPS", 3, true).index == 1 && onOffRow("fps", "x", 3, false).choices == std::vector<std::string>{"Off", "On"});
SettingRow t = themeRow("custom:Amber", {"Amber", "Zed"}); CHECK(t.index == 4 && t.choices[3] == "Custom: Amber" && themeKeyAt(1, {}) == "dark");
CHECK(tvModeRow(TvSetting::Auto).index == 2 && tvModeRow(TvSetting::Off).choices[1] == "Manual");
SettingRow s = shaderRow("/p/crt-royale.slangp", true, {"/p/crt-royale.slangp", "/u/x/lcd.slangp"});
CHECK(s.index == 1 && s.choices[0] == "Off" && s.choices[2] == "lcd"); CHECK(shaderRow("/p/crt-royale.slangp", false, {"/p/crt-royale.slangp"}).index == 0);
CHECK(stepIndex(s, +1, false) == 2 && stepIndex(s, +1, true) == 2); s.index = 2; CHECK(stepIndex(s, +1, false) == 2 && stepIndex(s, +1, true) == 0 && stepIndex(s, -1, false) == 1);
```

  Smoke tests (`FakeHost` gains `std::vector<SettingRow> rows`, `std::vector<std::pair<std::string,int>> set`, `int moreOpened`;
  `settingsSetup` puts the fixture rows `fps` (Off/On, 0), `volume`, `aspect` and `more` in place and walks RB×3):
  - `settings_rail_and_list`: `screenId() == "settings"`; `drew("Picture") && drew("Interface")` and `!drew("Speed")` (no
    row has group 1); Down ×2 (Interface), Right (the list: "Show FPS" is its first row), Right → `set.back() == {"fps", 1}`;
    Left → `{"fps", 0}`; Accept → `{"fps", 1}`; Accept → `{"fps", 0}` (wrap).
  - `settings_tv_mode_off_leaves_cleanly` (Review focus 13): a `tv_mode` row at index 1; Accept-step it to Off → the fake's
    `setSetting` sets `tv = false`; after a frame `ImGui::FindWindowByName(kBaseWindowName)` is gone,
    `ImGui::GetMouseCursor() != ImGuiMouseCursor_None`, and a pad Down → `handleEvent` returns false.
  - `settings_readonly_row_ignores_input`: a row with `readOnly = true` and a note → Right/Left/Accept leave `set` empty and
    `drew(note)`.
  - `settings_more_opens_desktop`: Down to "More settings…", Accept → `moreOpened == 1`; then `desktopOpen = true`, a pad Down
    → `handleEvent` returns false (the shell stepped aside).
  - `settings_hidden_rows_absent`: a fixture with no `shader` row → `!drew("Shader")`.
  - `settings_focus_remembered`: list row 2 of Interface, LB (Library), RB (Settings) → the same row is focused (Accept hits it).
  - `settings_mouse`: hover a row, click its right half → `set.back().second == 1`.
  - `settings_walk_120_frames_no_asserts` at 720p, 1080p, 2160p.
- [ ] **Step 2: Run** `ctest --test-dir app/build -R "tv_settings_model_unit|tv_ui_smoke"`. Expected: FAIL (does not build).
- [ ] **Step 3: Implement** the model, the screen, the Host members, `AppHost`, the `shaders_ui` exposure and the aspect table.
  Register `vj_tv_settings_model_test` in `VJTvTests.cmake` as `vj_hold_arbiter_test` is (it needs `vj_core`'s include dirs for
  `emu_snapshot.h`); add `src/tv/tv_settings_model.cpp` and `src/tv/tv_settings.cpp` to `vj_tv_ui_smoke` and
  `VJ_APP_SOURCES`.
- [ ] **Step 4: Run** the full suite: `ctest --test-dir app/build -C Release --output-on-failure`. Expected: all PASS
  (`shader_tests` and the video smoke cover the moved code).
- [ ] **Step 5: Commit** `feat(app): TV Settings screen with the curated rows (#991)`.

### Task 12a: layout fixes from the user's TV testing

TBD: filled in from the user's report.

### Task 13: "More settings" with ImGui navigation

**Files:**
- Create `app/src/tv_more_settings.h/.cpp` (App-side glue, next to `desktop_nav_app.cpp`).
- Modify `desktop_nav_popup.h`: `deskSetPopupNav(bool on)` is unchanged; `deskPadBeforeNewFrame` (`desktop_nav_app.cpp`) passes
  `popupLive(app) || tvMoreSettingsLive(app)`.
- Modify `app.h/.cpp`: `bool tvMoreSettings_ = false;` (`bool tvMoreSettingsLive() const`), one call in `handleEvent`, one
  rule in `handleHotkey`.
- Modify `devices_ui.cpp` `tvInputFrame`: one call, `tvMoreSettingsFrame(app, ms)`.
- Modify `ui.cpp` `drawSettings`: the placement and font push while live.
- Modify `tv_app_host.cpp`: `openMoreSettings()`.
- Test: `tv_menu_headless` (script), `tv_ui_smoke` (the nav-flag probe).

**Interfaces:**

```cpp
// tv_more_settings.h
namespace vj {
bool tvMoreSettingsLive(App& app);                         // app.tvMoreSettings_ && app.showSettings && (tv on or the menu open)
void tvMoreSettingsOpen(App& app);                         // showSettings = true; the flag; resets the glue's NavInput
bool tvMoreSettingsHandleEvent(App& app, const SDL_Event& e, uint64_t ms);   // pad buttons / left stick / triggers while live; true = consumed
void tvMoreSettingsFrame(App& app, uint64_t ms);           // intents -> deskFeedPopupIntents; Back closes when it may
bool tvMoreSettingsClose(App& app);                        // false when a popup is open or an item is active (text box, slider)
float tvMoreSettingsFontScale(App& app);                   // FontSizeBase x this = 30u at the viewport height; >= 1
}
```

Rules:
- `tvMoreSettingsOpen` is what `AppHost::openMoreSettings` calls. The flag is cleared whenever `showSettings` turns false
  (the window's close box, Ctrl/Cmd+Comma, Settings > Reset), checked in `tvMoreSettingsFrame`.
- `handleEvent` order: `feedHoldEdges` → `noteInput` → **`tvMoreSettingsHandleEvent` while live** → the shell → desktop nav →
  `input.handleEvent`. The glue takes the pad's profile intents through `input.navProfile(which)` as the shell does; Start and
  intents that are not TV's stay the app's; a release is fed but not consumed.
- `tvMoreSettingsFrame`: `take(ms)`; `Back` → `tvMoreSettingsClose(app)` (consumed either way); the rest →
  `deskFeedPopupIntents` (Up/Down/Left/Right/Accept; `Alt`/`Aux`/pages are dropped).
- `handleHotkey` Esc: `if (tvMoreSettingsLive(*this) && tvMoreSettingsClose(*this)) break;` before the TV-shell check.
- `drawSettings` while live: `SetNextWindowPos(centre, ImGuiCond_Always, {0.5, 0.5})`, `SetNextWindowSize(0.8 × WorkSize,
  ImGuiCond_Always)`, `PushFont(nullptr, FontSizeBase × tvMoreSettingsFontScale(app))` around the tab bar, `PopFont` after;
  `ui::nextWindow` is skipped so imgui.ini never learns the TV size.
- Keyboard navigation is on too while live (approval item B): the arrow keys, Enter and Esc work in the window, and
  `io.WantCaptureKeyboard` is true while it has focus, which gates F4 and Ctrl/Cmd+Shift+T as any text box does.
- With a game loaded: the menu stays open underneath (it holds the pause); the game's picture is still dimmed by the menu.

- [ ] **Step 1: Write the failing tests.**
  - `tv_menu_headless`, a new block after the TV-mode block (no game):
    ```
    1100  key tap F4
    1140  expect tv=1
    1140  pad tap rightshoulder
    1160  pad tap rightshoulder
    1180  pad tap rightshoulder
    1200  expect tv-screen=settings
    1200  pad tap dpdown              # Interface is the last group: Down x3 from Picture
    1220  pad tap dpdown
    1240  pad tap dpdown
    1260  pad tap dpright             # the list: Show FPS, VSync, Theme, TV mode, More settings... (no wrap)
    1270  pad tap dpdown
    1280  pad tap dpdown
    1290  pad tap dpdown
    1295  pad tap dpdown
    1300  pad tap a
    1340  expect settings-open=1
    1340  expect tv-more=1
    1340  expect tv-owns-input=0
    1340  pad tap dpdown              # goes to ImGui, not the shell
    1360  expect tv-screen=settings
    1360  pad tap b
    1400  expect settings-open=0
    1400  expect tv-more=0
    1400  expect tv-owns-input=1
    1400  key tap F4
    1440  expect tv=0
    ```
    New expects in `tv_script.cpp`: `settings-open` (`showSettings`), `tv-more` (`tvMoreSettingsLive(*this)`). Shift the
    later blocks' times.
  - `tv_ui_smoke` `more_settings_nav_flags`: with the existing `PopupProbe`, `deskSetPopupNav(true)` then `deskFeedPopupIntents({Down,
    Accept})` → the probe's selectable activates (this exists as `padPopupNav`; extend it with a second reason value: the
    probe is the same code path, so one added CHECK that `deskSetPopupNav(false)` after a TV-reason frame clears
    `ConfigNavCursorVisibleAlways`).
  - `tv_ui_smoke` `menu_more_settings_keeps_menu`: with `loaded = true`, open the menu, Down to Settings, Accept, Down to
    Interface, Accept, walk to "More settings…", Accept → `moreOpened == 1 && shell.menuOpen()`.
- [ ] **Step 2: Run** `ctest --test-dir app/build -R "tv_menu_headless|tv_ui_smoke"`. Expected: FAIL.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run** the full suite. Expected: all PASS.
- [ ] **Step 5: Commit** `feat(app): More settings from TV mode with ImGui pad navigation (#991)`.

### Task 14: sections Achievements and Settings, Y on Home, the Achievements screen

**Files:**
- Create `app/src/tv/tv_achievements.cpp` (`AchievementsScreen`).
- Modify `tv_screen.h` (`Section`, the factories), `tv_shell.cpp` (`setSection`, `stepSection`), `tv_chrome.cpp`
  (`sections[]`, `sectionLabel`), `tv_home.cpp` (Aux), `tv_details.cpp` (the row), `tv_host.h`, `tv_app_host.cpp`.
- Test: `tv_ui_smoke`.

**Interfaces:**

```cpp
// tv_screen.h
enum class Section { Home, Library, Achievements, Settings };
std::unique_ptr<Screen> makeSettingsScreen(Host& h, Shell& s);        // Task 12
std::unique_ptr<Screen> makeAchievementsScreen(Host& h, Shell& s, uint32_t focusGameId = 0);
// tv_shell.h
void openSection(Section s, std::unique_ptr<Screen> root);   // setSection with a prepared root (Details -> Achievements focused on a game)

// tv_host.h, added
struct AchItem {                 // one achievement of the loaded game, or one leaderboard tracker (kind Tracker)
    enum Kind { Achievement, Tracker } kind = Achievement;
    uint32_t id = 0;
    std::string title, status;   // status: the desktop's line ("Unlocked in hardcore  16 Oct 2026", "Locked  Progress: 5/10 ...")
    unsigned points = 0;
    bool unlocked = false;
    std::string badgeUrl;        // the one to show (earned or locked)
};
struct AchGame {                 // one game of the RetroAchievements Jaguar list
    uint32_t id = 0;
    std::string title, imageUrl;
    unsigned achievements = 0, unlocked = 0;
    bool haveProgress = false, inLibrary = false;
};
struct AchHeader { bool signedIn = false, available = true, hasGame = false; std::string title, mode, iconUrl; unsigned unlocked = 0, total = 0; };
virtual AchHeader achHeader() = 0;                      // mode: "Hardcore" | "Softcore" | "Softcore (hardcore is paused: <blocker>)"
virtual std::vector<AchItem> achItems() = 0;            // trackers first, then the rows in bucket order; empty with no tracked game
virtual std::vector<AchGame> achGames() = 0;            // calls ach.gamesTabShown() first; games with a set only (ra::viewGames)
virtual Art achImage(const std::string& url) = 0;       // ach.images.get(url): tex only when Ready
virtual void achPlay(uint32_t gameId) = 0;              // ach.playGame(entry): a launch (the caller guards)
virtual uint32_t achGameId(const GameRef& g) = 0;       // the list entry whose libIndex is this game, else 0
```

`AchievementsScreen` (`id() "achievements"`):
- Draws `achHeader()` each frame: `!available` → the line "RetroAchievements is not available in this build"; `!signedIn` →
  "Sign in from Settings > Achievements in desktop mode"; `hasGame` → the header and `achItems()` as rows (Tracker rows
  are drawn muted and skipped by the focus); otherwise `achGames()` as rows ("<title> · <unlocked>/<achievements>" when
  `haveProgress`, "<title> · <achievements> achievements" otherwise; rows with `!inLibrary` are drawn muted).
- Intents: Up/Down; Accept on a game row with `inLibrary` → `achPlay(id)` + `shell.launched()`; nothing else.
- `focusGameId`: the first draw focuses that game's row.
- Focus remembered under `achievements/games`.
- Hints: games list: glyph(Accept) "Play" (only when the focused row is in the library), glyph(Back) "Home"; rows: glyph(Back).
- Badge pictures through `achImage(url)` with a placeholder box while loading.

Wiring:
- `stepSection` clamps at `Section::Settings`; `setSection` builds all four roots; `tv_chrome.cpp` lists four tabs
  (the tab bar must still fit at 1280×720: section labels at body 30, 40 apart, measured in the walk test).
- Home: `case Intent::Aux: shell_.setSection(Section::Achievements)`, hint glyph(Aux) "Achievements" (dropped on the keyboard
  by `dropUnbound`).
- Details: an item `Item::Achievements` ("Achievements") after Favorite when `achGameId(g_) != 0`; Accept →
  `shell_.openSection(Section::Achievements, makeAchievementsScreen(host_, shell_, id))`.
- `backAtRoot()` is unchanged (Home root only); Back on the Achievements or Settings root goes to Home, as Library does.

- [ ] **Step 1: Write the failing tests** (`FakeHost` gains `AchHeader hdr`, `std::vector<AchItem> items`, `std::vector<AchGame>
  raGames`, `uint32_t played = 0`, `std::map<std::string, uint32_t> achIds`):
  - `sections_are_four`: from Home, RB ×3 → `screenId()` "library", "achievements", "settings"; RB again stays "settings";
    LB ×3 → "home"; `drew("Achievements") && drew("Settings")`.
  - `ach_not_signed_in_line`: `hdr.signedIn = false` → `drew("Sign in from Settings > Achievements in desktop mode")`.
  - `ach_not_available_line`.
  - `ach_rows_for_loaded_game`: `hdr.hasGame = true`, 2 items unlocked/locked + 1 tracker → 3 rows drawn, `drew("Unlocked in
    hardcore")`, `drew(hdr.mode)`, and Down from row 0 skips the tracker.
  - `ach_games_list_without_game`: 3 games, one `inLibrary` → Accept on it → `played == its id && guardArmed == 1`; Accept on
    another → `played` unchanged.
  - `home_y_opens_achievements`: on Home, Y → `screenId() == "achievements"`; Back → "home". The hint is glyph(Aux)
    "Achievements", dropped on the keyboard by `dropUnbound` as the existing `hintsListOnlyWhatWorks` already proves for
    Alt / Menu (count the "Achievements" texts: 2 with a pad, the tab and the hint; 1 with `keyboardLast = true`).
  - `details_achievements_row_when_known`: `achIds[recent[0].key] = 77` → Details shows "Achievements"; Accept →
    `screenId() == "achievements"` and the focused row is game 77; with no id → the row is absent.
  - `ach_walk_no_asserts` at three sizes, both branches.
  - `ingame_settings_slot` (decision 2, drawn by Task 15): see Task 15.
- [ ] **Step 2: Run** `ctest --test-dir app/build -R tv_ui_smoke`. Expected: FAIL.
- [ ] **Step 3: Implement.** `AppHost::achItems` builds the status line exactly as `drawThisGame` does (lift that block into
  `std::string ra::achStatusLine(const AchRow&)` in `ra_dash.h/.cpp` so both use it; `achievements_ui.cpp` calls it).
- [ ] **Step 4: Run** the full suite (the `ach_*` CTest targets cover `ra_dash`). Expected: all PASS.
- [ ] **Step 5: Commit** `feat(app): TV Achievements screen and the Achievements and Settings sections (#991)`.

### Task 15: Overlays, the in-game menu rows, screenshots, PR 3

**Files:**
- Create `app/src/tv/tv_overlays.cpp` (`OverlaysScreen`, `makeOverlaysScreen(Host&, Shell&)`).
- Modify `tv_shell.h/.cpp` (`Row::Kind` gains `Achievements, Overlays, Settings`; `menuScreen_`; `buildRoot`; `accept`;
  `back`; `intent`), `tv_ingame.cpp` (draw the slot), `tv_host.h`, `tv_app_host.cpp`.
- Test: `tv_ui_smoke`.

**Interfaces:**

```cpp
// tv_host.h, added
struct ToggleRow { std::string id, label; bool on = false, enabled = true; std::string note; };
virtual std::vector<ToggleRow> overlayRows() = 0;   // "fps", "keypad", "controls", "tool:<DebugTool::id>"
virtual void toggleOverlay(const std::string& id) = 0;
// tv_shell.h, added
bool menuHasScreen() const;                 // the slot is set
void menuPushScreen(std::unique_ptr<Screen> s, Row::Kind from);   // the slot; Back returns the focus to `from`
```

Rules:
- `overlayRows`: `fps` (`app.showFps`), `keypad` (`input.overlay.open`), `controls` (`input.controls.show`), then one row per
  `debug.tools()` entry with a `toggle`, label = the tool's label without a trailing "...", `on` = `isOpen && isOpen()`,
  `enabled = debug.toolAvailable(t)`, `note` when disabled: "Debug windows are off (hardcore, the emulation thread, or no
  game)". `toggleOverlay("tool:<id>")` calls `t.toggle()` (as the Tools menu does); `keypad` flips `overlay.open`; `controls`
  is `overlayEvent(controls, Toggle)`; `fps` flips `showFps` + `saveConfig()`.
- `OverlaysScreen` (`id() "overlays"`): rows refetched each draw; Accept toggles; after `keypad` or `controls` the screen calls
  `shell.closeMenu()` (decision 5). Hints: glyph(Accept) "Toggle", glyph(Back) "Menu".
- Root rows in spec §4 order: Resume, Save state, Load state, **Achievements, Overlays, Settings**, Controls, Enter/Exit TV
  mode, Reset, Close game. Accept on the three pushes the screen into the slot (`makeAchievementsScreen`, `makeOverlaysScreen`,
  `makeSettingsScreen`); the slot screen gets every intent but Back; Back clears it and `enter(Page::Root, from)`.
- `drawMenu` with the slot set: the rail's page name is the screen's id capitalised; the list area is handed to
  `screen->draw(DrawCtx{…})` with `hovered = ImGui::IsWindowHovered()`; the hint bar is the screen's hints with Back "Menu"
  appended by the shell.

- [ ] **Step 1: Write the failing smoke tests** (`FakeHost` gains `std::vector<ToggleRow> overlays`, `std::vector<std::string>
  toggled`):
  - `ingame_rows_full_order`: open the menu with `canLoad = true, tvOn = true` → the labels in order are exactly
    `{"Resume", "Save state", "Load state", "Achievements", "Overlays", "Settings", "Controls", "Enter TV mode", "Reset",
    "Close game"}`.
  - `overlays_toggle_fps`: rows `fps` off, `keypad` → Down to Overlays, Accept, Accept on "Show FPS" → `toggled == {"fps"}`
    and `shell.menuOpen()`.
  - `overlays_keypad_closes_menu`: Accept on "Keypad overlay" → `toggled == {"keypad"} && !shell.menuOpen() && resumed == 1`.
  - `overlays_tool_rows_from_registry`: rows `tool:memory` ("Memory viewer", on) and `tool:cpu` (disabled, note) →
    `drew("Memory viewer") && drew(note)`; Accept on the disabled row toggles nothing.
  - `ingame_slot_back_returns_to_row`: Settings, Accept, Back → the slot is gone and `focusedLabel(shell) == "Settings"`.
  - `ingame_achievements_slot_draws`: `hdr.hasGame = true`, 2 items → Accept on Achievements → both titles drawn; Back.
  - `menu_walk_with_slots_no_asserts`: every slot at three sizes.
  - `tv_menu_headless` (Review focus 14), after the existing game block: `load`, F4, Down ×5 (Settings), `pad tap a`,
    `pad tap a` (Picture), Down to "Internal resolution", `pad tap dpright`, `pad tap b` ×3, F4,
    `expect core-option=virtualjaguar_internal_resolution:2x` (new expect: `host.optionValueFor(KEY, "")`; the value
    after the first Right from the core's default — read it from `libretro_core_options.h` when writing the line).
- [ ] **Step 2: Run** `ctest --test-dir app/build -R "tv_ui_smoke|tv_menu_headless"`. Expected: FAIL.
- [ ] **Step 3: Implement.** Add `src/tv/tv_overlays.cpp`, `tv_achievements.cpp`, `tv_settings.cpp`, `tv_settings_model.cpp`
  to `vj_tv_ui_smoke` and `VJ_APP_SOURCES` (if not done in Tasks 12–14).
- [ ] **Step 4: Run** the full suite. Expected: all PASS.
- [ ] **Step 5: Commit** `feat(app): TV Overlays screen and Achievements, Overlays and Settings in the in-game menu (#991)`.
- [ ] **Step 6: Screenshots on the Studio** (`vjr.sh push` / `run` / `pull`; never locally). Extend
  `~/Workspace/Provenance/vj-eval/tvshots/tv-shots.txt` with: `key tap Tab` ×2 → `shot achievements-nogame.png`; `key tap Tab`
  → `shot settings.png`; `pad tap dpdown` ×3, `pad tap dpright`, `pad tap dpup`, `pad tap a` → `shot more-settings.png`,
  `pad tap b`; then `load <a RA-known rom>`, a 3000 ms gap, `key tap F4` → `shot menu.png`; Down ×3, `pad tap a` →
  `shot achievements-game.png`; `pad tap b`, Down, `pad tap a` → `shot overlays.png`; `pad tap b`, Down, `pad tap a` →
  `shot menu-settings.png`; `quit`. Run with `--ra-mock app/tests/ra` (sign-in and a tracked game come from the canned
  JSON) at 1280×720 and 1920×1080, and 1920×1080 with `--pixel-density 2`. Compare by eye with mockup B and attach.
- [ ] **Step 7: Open PR 3** into `develop`: title `feat(app): TV Settings, Overlays, Achievements and More settings (TV mode
  phase 3)`; body with `Refs #991`, the test summary, the screenshots and the approval items below as applied; bind it
  (`ccd_pr`) and report CI. Rebase onto develop once the v3.10.0 back-merge lands.

### Phase 3 review focus (added to the list above)

11. **A pad press while "More settings" is open must never reach a TV tile or a game** (Task 13: the `tv-owns-input=0`
    script case; `settings_more_opens_desktop`).
12. **Closing "More settings" with B while a combo or a text box is active** must close the combo / leave the box, not the
    window (Task 13 `tvMoreSettingsClose` refuses then; by hand on the Studio: Theme combo open, B, B).
13. **Choosing Off in the TV-mode row while in TV mode** lands on the desktop with nothing stuck: no hidden cursor, no held
    nav direction (Task 12: `settings_tv_mode_off_leaves_cleanly` — `tv` goes false, `ImGui::GetMouseCursor() != None` after a
    frame, a later pad Down reaches nothing).
14. **A core-option row changed with the menu open** reaches the core after the menu closes (the write is a `post`ed
    command; the paused thread applies it at the next boundary): script case `load`, F4, Settings, Picture, Right on
    Internal resolution, B ×3, `expect core-option=virtualjaguar_internal_resolution:2x` (new expect:
    `host.optionValueFor(key, "")`).
15. **The Achievements games list with no network** (cache only, `gamesFromCache()`): the screen draws the cached list and no
    progress, without a spinner that never ends (Task 14: `raGames` with `haveProgress = false`).
16. **Debug tool rows under the emulation thread** are disabled with the note, never a dead toggle (Task 15
    `overlays_tool_rows_from_registry`; by hand with `--emu-thread on`).

### Approval needed before Phase 3 starts (spec changes for phase 3)

The rows already in "Changes to the approved spec" that this phase uses: Y on Home opens Achievements (with no game the
games list, with a game its rows); the Achievements row in Game details; Overlays and Settings reached from the in-game menu.

New in this revision:
- **A.** Sections are Home, Library, Achievements, Settings. Overlays is not a section (with no game only "Show FPS" would do
  anything): it is a screen of the in-game menu. This narrows Phase 2's approval C.
- **B.** "More settings…" turns on ImGui's keyboard navigation as well as its gamepad navigation while the window is open in
  TV mode (spec §5 names the gamepad flag only). A keyboard-only couch player can then use it; the cost is that F4 and
  Ctrl/Cmd+Shift+T are gated while the window has focus, as they are over any text box.
- **C.** The Achievements screen shows leaderboards as the active trackers only (the same text as the desktop's bottom-right
  HUD), not a leaderboard list: rc_client exposes no per-game leaderboard list to the app today, and adding one is a
  desktop feature first.
- **D.** The Achievements row in Game details appears only for a game the RetroAchievements list knows (`libIndex`
  resolves to it); other games get no row rather than a row that opens an unrelated list.
- **E.** The "TV mode" row offers Off / Manual / Auto now, with Auto acting as Manual until Phase 4 (Phase 2 approval B
  carries over); the two Auto checkbox rows come with Phase 4. Choosing Off from inside TV mode leaves TV mode at once.
- **F.** The in-game menu keeps its Phase 1 placement of Enter/Exit TV mode (before Reset), while Achievements, Overlays and
  Settings go where spec §4 puts them (after Load state). Spec §4 lists the TV-mode row last; moving it would put a
  mode toggle under two destructive rows.
- **G.** The "Internal resolution", "RISC overclock" and "Idle-loop skip" rows are hidden until the core has pushed its
  option definitions, that is until a game has been loaded in this session; the Speed group then shows a one-line note
  "Available once a game has run".

---

## Phase 4 — auto-switching, the hint, the build default, tuning (PR 4)

Revised 2026-10-21 against the code Phase 3 merged (#1060) and what landed after it: the emulation thread phase 3a (#1061),
the late-latch waker (#1058, #1062), the installer (#1059). Branch `feat/991-tv-phase4`, worktree
`~/Workspace/Provenance/vj-wt/tv-phase4`, off `libretro/develop` cf036d74. **Build and test on the Mac Studio only:**
`~/Workspace/Provenance/vj-eval/remote/tvb.sh "<ctest regex>"` (rsyncs the worktree, builds, runs the matching tests;
`--build-only` to just build; the log is on the Studio at `~/vj-remote/out/tvp4.log`). Never build the app on the developer's
Mac for this phase.

### Phase 4 constraints (overlap with other work)

- v3.10.0 is on `master` (#1042 merged) and **not yet merged back into `develop`**; the back-merge carries every workflow file.
  The `app-package.yml` change (Task 17) is its own commit, last on the branch, so the rebase after the back-merge is one
  cherry-pick; check `git log libretro/master..libretro/develop -- .github/workflows/app-package.yml` is empty before editing.
- An #967 phase 3b session edits `app.cpp` too. The Auto hooks go in **one** block of `handleEvent` plus one line in
  `setFullscreen`, and no edit to how events reach `input.handleEvent` or the core.
- No `tv/` file calls `emu.call` / `emu.post` (as before); the only new App members are `setTvSetting`, `setTvAuto`,
  `tvAutoPadPress`, `tvFullscreenEdge`, `tvNoteFsSeen_`, `tvHintShownThisRun_`, `toastCount_`.

### What Phase 3 left (read before any task)

- **`TvMode` is complete and unit-tested** (`tv_mode.h/.cpp`, `tv_mode_test.cpp`): `configure(TvConfig, fullscreenNow)`,
  `toggle()`, `setOn(bool)`, `onFullscreen(bool)`, `onPadPress(bool desktopOwnsInput)`, `onMouseMove(dx, dy)` (summed path
  length over `kMouseOffPx = 32`), `onNonNavKey()`, `cause()` (`None / Manual / Fullscreen / Controller`). **Nothing calls the
  last four.** Two behaviours matter for the wiring: a repeated `onFullscreen(true)` re-applies (it turns TV back on after a
  manual off; `testFullscreenHoldIsState` relies on it), so App must feed it *edges only*; and `configure()` always starts from
  off, so every caller that reconfigures while TV is on must restore it (`AppHost::setSetting("tv_mode")` already does:
  `wasOn → setOn(true)`, which makes the cause Manual).
- **`effectiveSetting(stored)` is the Phase 2 mapping** (Auto → Manual). Five call sites: `app.cpp` `loadConfig` (×1 after
  `configureTvMode`), `configureTvMode`, `saveConfig` (the `ui.tv_manual_on` rule); `tv_app_host.cpp` `tvAvailable()`;
  `ui.cpp` the View > TV mode `MenuItem` enable. `tv_mode_test.cpp:190` checks `effectiveSetting(Auto) == Manual`.
- **The stored setting:** `App::tvSetting` (`TvSetting`), read in `loadConfig` as `cfg.get("ui.tv_mode", "manual")`;
  `saveConfig` never writes `ui.tv_mode` (comment there); `AppHost::setSetting("tv_mode", i)` writes it on change, clears
  `ui.tv_manual_on` on Off, `configureTvMode()`, then `setOn(true)` if it was on. The script verb `tvsetting` sets
  `tvSetting` and calls `configureTvMode()` without writing the key (the script's `expect cfg=ui.tv_mode:absent` lines rely on
  that). `settings_reset.cpp` already classifies all five `ui.tv_*` keys as Interface, and `settings_reset_test` covers them.
- **`App::handleEvent` order** (`app.cpp` ~1744): `LatchWaker::isWake` → `ImGui_ImplSDL3_ProcessEvent` → `fromExtraWindow` →
  F10 step → `input.feedHoldEdges(e)` → `tvHost_->noteInput(e)` → `tvMoreSettingsHandleEvent` → `tvShell().handleEvent` (when
  `ownsInput() && !input.capturing()`) → **the comment "phase 4's Auto controller trigger, TvMode::onPadPress, goes before this
  call"** → `deskPadHandleEvent` → `input.handleEvent` → the `switch` (fullscreen events at `SDL_EVENT_WINDOW_ENTER/LEAVE_FULLSCREEN`
  set `fullscreen` and call `fullscreenMenu.update(...)`; `SDL_EVENT_KEY_DOWN` calls `handleHotkey` unless ImGui wants the
  keyboard). `App::setFullscreen(bool)` (~1093) sets the flag, updates `fullscreenMenu`, calls `SDL_SetWindowFullscreen` when
  there is a window; the SDL event then arrives too (windowed), or never (headless). `main` runs `loadConfig()` before the window
  exists (`fullscreen` is false then) and `setFullscreen(true)` for `--fullscreen` after it.
- **Desktop pad navigation (#1030)**, `desktop_nav_app.cpp`: `deskPadActive(app)` is false when `tvMode.active()`, the menu is
  open, a capture runs, no library shows, a game runs, or a desktop window / foreign popup is open. `deskPadHandleEvent` consumes a
  mapped button *down* (`navProfile(which).intentForButton(b)` not `None` and not `Menu`), the left stick and mapped triggers;
  releases are fed but not consumed. `deskPadFrame` resets the nav (`d.in.reset()`, `pending.clear()`) on any edge of
  `deskPadActive`, so a press that turned TV on leaves nothing queued, and its release is a "press never seen" release
  (`tv::NavInput`'s per-source latch, pad-nav design §10). The TV shell's `nav_` is the same class, so the release reaching the
  shell after a switch does nothing either.
- **The shell and keys** (`tv_shell.cpp` `handleEvent`): Esc at the root is not consumed; a typed character (`typedChar`:
  letters, digits, Backspace) is offered to the top screen (`Screen::typed`; only `LibraryScreen` uses it) and consumed when used;
  nav keys (`intentForKey`: arrows, Enter, Esc, Backspace, Tab, PageUp/Down) are consumed on down; **everything else returns
  false** ("F4 and friends are the app's"). The hotkeys (`handleHotkey`, ~1671) are F1–F11, Esc, Tab, Ctrl/Cmd+letter.
- **Script runner** (`tv_script.cpp`): verbs `load, key, pad, axis, mouse (windowed only), focus, typing, capture, capture nav:,
  navdefault, tvsetting, saveconfig, wait, shot, quit`; expects as the header of `tests/tv_menu_headless.txt.in` lists them, plus
  `cfg=KEY:VALUE|absent`, `core-option`, `desk-pad`, `desk-pending`. `expect` ticks two frames first. The runner is `App`
  member code, so `osd_` is in reach. No `fullscreen` verb exists (`fullscreen` is an expect only). Headless: no ImGui context,
  no window (`window_` is null: `setFullscreen` only flips the flag), the config lives in a private temp dir, the virtual pad is a
  generic Xbox-layout gamepad (A/B/X/Y, D-pad, shoulders, `misc1`, `start`, `touchpad`).
- **Toasts:** `App::toast(text, seconds = 3.0, level = 1)` pushes an `Osd {text, born, life, level}` onto `osd_` (last 6;
  headless also prints `[osd] text`); `App::osd()` exposes the vector.
- **Binding text:** `remap_ui.cpp` has a file-local `tvMenuText(const std::vector<Binding>&)` ("Share (hold); Guide (hold) on
  pads without Share", else `Binding::display()`); `InputManager` exposes `keyboardProfile()`, `padProfile(size_t i)` (by index,
  matching `pads()`), `navProfile(joystickId)`, `lastPadId()`, `pads()` (`PadInfo {name, guid, hasMisc1, …}`), but **no per-pad
  profile lookup by joystick id** (`Pad` is private; `input.cpp` ~464 reads `pad->profile.binds[C_TVMENU]` internally).
  `padButtonLabel(SDL_Gamepad*, const NavProfile&, Intent)` names *nav* intents, not app actions; the hint needs the action's
  binding, so it goes through `Binding::display()` ("Share (hold)", "F4"). A pad's default `C_TVMENU` list can hold several
  bindings (the Share or Guide hold plus any paddles SDL reports; the virtual pad reports them all).
- **Settings UI:** `ui.cpp` `drawInterfaceTab` is `SeparatorText` sections: Overlays, (Appearance, Font via helpers), Performance,
  Save states, Game library, then `drawDesktopSettings`, `drawLibraryListOptions`. `drawAppGroupReset(app, AppGroup::Interface)`
  heads it. `tv_settings_model.cpp` has `tvModeRow(TvSetting)` (group 3, "Off / Manual / Auto") and `onOffRow(id, label, group,
  on)`; `AppHost::settingRows` pushes `tvModeRow` right before `more`.
- **Build default:** `app/CMakeLists.txt` has no `VJ_TV_DEFAULT`; `virtualjaguar`'s compile definitions are at ~467. The app
  packages are built by `.github/workflows/app-package.yml` (a `workflow_call` with `inputs.release_tag`: `vX.Y.Z` from a tag,
  `nightly` from a `develop` push, `prerelease` from `release/**`, `prNNN` / `dev` from `app.yml`), with three `cmake -S app`
  invocations (macOS ~193, Windows ~370, Linux ~638) and a Flatpak built by `flatpak-builder` straight from
  `app/packaging/flatpak/io.github.libretro.VirtualJaguar.yml` (`config-opts`, ~80). `release.yml` only chooses the tag.
- **README:** `app/README.md` "Running" has the Ctrl/Cmd+Shift+T and F4 rows (113–114); "Controller in the desktop library"
  (160) ends with "Share or Guide, held | toggle TV mode". There is no TV-mode section, no `--tv`, no keypad-chord note.

### Phase 4 decisions (the approval list at the end repeats the ones that change the spec)

1. **The controller trigger beats desktop pad navigation, and the switching press is consumed.** Under Auto with "when a
   controller is used" on, a mapped button down (`navProfile(which).intentForButton(b) != Intent::None`, Start included) while
   the desktop library owns the pad (`deskPadActive(app)`: TV off, no capture, the home library or the library window showing,
   no desktop window or foreign popup) **and no game is loaded** (a paused game under the library window stays the desktop's)
   and `!io.WantTextInput` turns TV mode on and **returns**: it neither moves the desktop focus nor reaches the TV shell (A would
   otherwise play the Home hero game blind). Its release goes through as usual (both navs' press-seen latches ignore it).
   Desktop pad navigation therefore serves Off, Manual, and Auto with the trigger unticked, exactly as the pad-nav design §2
   recorded the user's call. Buttons with no nav intent (Guide, Share, touchpad, paddles) are not triggers: they carry app
   actions (`C_TVMENU` hold, keypad `#`, the keypad overlay on a Share tap, `C_HELP`), and a Share tap that also switched TV on
   would be two actions on one press; sticks and triggers are not triggers either (a resting stick drifts).
2. **"Mouse movement over 32 px" is the summed path of `SDL_EVENT_MOUSE_MOTION` `xrel/yrel` since TV mode went on** (or since
   the last pad press while on), as `TvMode` already implements; it is fed only while `!input.mouseCaptured()` and from the main
   window (`fromExtraWindow` already filters). No new threshold.
3. **"A non-navigation key" is a printable key the TV UI did not use**: a `SDL_EVENT_KEY_DOWN` that reached `handleEvent` past
   the shell (so a letter the Library's filter consumed does not count), with no Ctrl/Cmd/Alt held, not a repeat, not wanted
   by ImGui (`io.WantCaptureKeyboard`: a text box, or "More settings" with its keyboard nav), and `SDL_GetKeyFromScancode` in
   `0x20..0x7E`. F-keys, Esc, Tab, Enter, arrows and every Ctrl/Cmd shortcut are excluded: F4 and Ctrl/Cmd+Shift+T toggle TV
   mode themselves, and "off by the key, then on again as Manual" would invert the toggle.
4. **The fullscreen trigger applies with a game running** (spec §8 as written; `TvMode` already does it). The visible effect
   is deferred: `inputOwner` keeps the game, nothing is drawn over it, and when the game closes the TV Home shows instead of the
   desktop library. App feeds `onFullscreen` only on an **edge** of `fullscreen` (`tvFullscreenEdge()` compares with
   `tvNoteFsSeen_`), from `setFullscreen` and from the two SDL events, so a repeated enter event cannot undo a manual off.
5. **`effectiveSetting()` is removed**, not made the identity: its five call sites read `tvSetting` directly. Phase 3's
   approval E ("Auto acts as Manual") ends.
6. **One App entry point for every setting write:** `App::setTvSetting(TvSetting)` (writes `ui.tv_mode` on change only, clears
   `ui.tv_manual_on` on Off, reconfigures, restores on) and `App::setTvAuto(bool fullscreen, bool on)` (writes
   `ui.tv_auto_fullscreen` / `ui.tv_auto_controller`, reconfigures, restores on). `AppHost::setSetting`, the Interface tab and the
   `tvsetting` verb all call them (the verb keeps writing nothing: it calls `configureTvMode()` only).
7. **Build default:** `tv::defaultSetting(const char* buildDefault)` is pure (`"off" | "manual" | "auto"`, anything else and
   null → Auto); `loadConfig` uses it when `!cfg.has("ui.tv_mode")`, else the stored key. CMake: `set(VJ_TV_DEFAULT "auto" CACHE
   STRING ...)`, validated, passed as `VJ_TV_DEFAULT_SETTING="…"` to `virtualjaguar` only (the tests never see it). The packages
   pass `-DVJ_TV_DEFAULT=manual` when `inputs.release_tag` starts with `v` or is `prerelease`; `nightly`, `prNNN` and `dev` stay
   Auto. The Flatpak gets the same through one `sed` on the manifest's `config-opts` in the workflow before `flatpak-builder`
   (the manifest itself stays Auto, like the CMake default).
8. **The hint** fires at the first desktop-nav press this *install* while the setting is Manual: in `deskPadHandleEvent`, on the
   consumed button down, when `app.tvSetting == Manual && !cfg.getBool("ui.tv_hint_shown") && !tvHintShownThisRun_`. Text:
   `"Press " + <pad binding> + " or " + <keyboard binding> + " for TV mode"`, where the pad part is the `display()` of that
   pad's **first `C_TVMENU` binding, hold bindings first** ("Share (hold)" on a pad with Share, "Guide (hold)" without, a
   paddle's name when only a paddle is bound; never the whole list, which would name four paddles) and the keyboard part the
   keyboard profile's first `C_TVMENU` key ("F4"); with no pad binding the text is "Press F4 or Ctrl+Shift+T for TV mode" (Cmd on
   macOS), with no keyboard binding either "Press Ctrl+Shift+T for TV mode". Shown with `toast(text, 6.0)`; `ui.tv_hint_shown`
   is set and `saveConfig()` called before the toast. Never under Auto (the press switched instead) or Off (nothing to hint). A
   Settings > Reset of Interface clears the key and the hint may show once more; that is the documented meaning of a reset.
9. **Interface tab:** a `SeparatorText("TV mode")` block after Overlays: a combo `Off / Manual / Auto` (→ `setTvSetting`), the
   two checkboxes "Turn on in fullscreen" and "Turn on when a controller is used" shown only under Auto (→ `setTvAuto`), one
   `ui::hint` naming the shortcuts and the build default. The TV Settings rows `tv_auto_fs` ("Auto: in fullscreen") and
   `tv_auto_pad` ("Auto: when a controller is used") follow `tv_mode` in group 3 and exist only while the stored setting is Auto
   (`tvAutoRows(stored, fs, pad)` in the model).
10. **Motion and repeat stay at the spec's starting values** (`kRepeatDelayMs 350`, `kRepeatMs 80`, `kTweenSeconds 0.12`,
    `focusScale 1.08`, all unchanged since Phase 2): agents cannot test on a TV. Tuning on a real TV with real pads is owed
    to the user and listed in the PR body as open; a follow-up PR carries whatever values come back.
11. **`--tv` under Auto** is unchanged (`setOn(true)` = Manual cause): a screenshot run stays in TV mode whatever the mouse does.
12. **Phase 3's `ui.tv_manual_on`** stays Manual-only: under Auto the triggers decide at every launch.

---

### Task 16: the Auto triggers in App

**Files:**
- Modify `tv_mode.h/.cpp`: remove `effectiveSetting`; add `TvSetting defaultSetting(const char* buildDefault);`.
- Modify `app.h/.cpp`: `loadConfig`, `configureTvMode`, `saveConfig`, `handleEvent`, `setFullscreen`; new members.
- Modify `tv_app_host.cpp` (`tvAvailable`, `setSetting("tv_mode")` → `setTvSetting`), `ui.cpp` (the View menu enable),
  `tv_script.cpp` (verb `fullscreen on|off`), `tests/tv_menu_headless.txt.in`, `tests/tv_mode_test.cpp`.
- Test: `tv_mode_unit`, `tv_menu_headless`.

**Interfaces:**

```cpp
// tv_mode.h
TvSetting defaultSetting(const char* buildDefault);   // "off" | "manual" | "auto" (VJ_TV_DEFAULT_SETTING); null or unknown -> Auto
// (effectiveSetting is gone)

// app.h
void setTvSetting(tv::TvSetting s);          // ui.tv_mode written on change only; Off clears ui.tv_manual_on; reconfigure; stays on if it was
void setTvAuto(bool fullscreenTrigger, bool on);   // ui.tv_auto_fullscreen / ui.tv_auto_controller; reconfigure; stays on if it was
bool tvAutoPadPress(const SDL_Event& e);     // decision 1: true = the press turned TV mode on and is consumed
void tvFullscreenEdge();                     // decision 4: tvMode.onFullscreen(fullscreen) when it differs from tvNoteFsSeen_
bool tvNoteFsSeen_ = false;                  // the fullscreen state TvMode was last told
```

Rules:
- `loadConfig`: `tvSetting = cfg.has("ui.tv_mode") ? parsed (unknown value → defaultSetting) : tv::defaultSetting(VJ_TV_DEFAULT_SETTING)`;
  `configureTvMode()` passes `tvSetting` as is; `tvNoteFsSeen_ = fullscreen` after it. The `ui.tv_manual_on` restore and the
  `saveConfig` write test `tvSetting == Manual`.
- `handleEvent`, one block where the comment sits (after the shell, before `deskPadHandleEvent`):
  ```cpp
  if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && tvAutoPadPress(e)) return;
  if (e.type == SDL_EVENT_MOUSE_MOTION && !input.mouseCaptured()) tvMode.onMouseMove(e.motion.xrel, e.motion.yrel);
  if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat && !(e.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI)) &&
      !(imguiUp_ && ImGui::GetIO().WantCaptureKeyboard)) {
      const SDL_Keycode k = SDL_GetKeyFromScancode(e.key.scancode, SDL_KMOD_NONE, false);
      if (k >= 0x20 && k <= 0x7E) tvMode.onNonNavKey();   // decision 3: a printable key the TV UI did not use
  }
  ```
  `tvAutoPadPress(e)`: `const Intent in = input.navProfile(e.gbutton.which).intentForButton(e.gbutton.button); if (in == None) return false;`
  then, while TV is on, `tvMode.onPadPress(true)` (a fresh mouse budget under `Cause::Controller`) and `return false`; while off:
  `if (deskPadActive(*this) && !host.isLoaded() && !(imguiUp_ && ImGui::GetIO().WantTextInput)) tvMode.onPadPress(true); return
  tvMode.active();` (`TvMode` itself checks Auto and the trigger box; `deskPadActive` covers TV off, no capture, the library showing,
  no desktop window). On a switch `input.armReleaseGuard()` is **not** needed: no game owns the input.
- `setFullscreen` and the two SDL fullscreen cases end with `tvFullscreenEdge();`.
- `tv_script.cpp`: verb `fullscreen on|off` → `setFullscreen(arg1 == "on")` (headless: the flag and the edge only); comment it in
  the script header.

- [ ] **Step 1: Write the failing tests.**
  - `tv_mode_test.cpp`: replace the `effectiveSetting` line with
    `CHECK(defaultSetting("manual") == TvSetting::Manual && defaultSetting("off") == TvSetting::Off && defaultSetting("auto") == TvSetting::Auto && defaultSetting("bogus") == TvSetting::Auto && defaultSetting(nullptr) == TvSetting::Auto);`
    and add `testTriggerOrders()` with every §8 order not yet covered (assert `active()` and `cause()` after each line):
    pad → fullscreen enter → fullscreen leave (off, whatever the cause); fullscreen enter → pad (stays Fullscreen cause) → mouse
    100 px (on) → leave (off); pad → manual toggle (off) → pad (on again, Controller) → key (off); fullscreen enter → toggle
    (off) → pad (on, Controller: a pad edge ends the manual override) → mouse 40 px (stays on: fullscreen holds it) → leave
    (off); `configure(Auto, fullscreenNow = true)` → toggle (off) → `onFullscreen(true)` repeated (on again: documents why App
    feeds edges only); Off: every trigger is a no-op; Manual: every trigger is a no-op and `toggle` works.
  - `tv_menu_headless.txt.in`, a new block before "A game to play with" (the setting is Manual there; shift later times):
    ```
    # Auto (#991 phase 4): a mapped pad press in the desktop library turns TV mode on and is consumed (nothing queued, nothing
    # launched); a printable key turns a controller-started TV mode off; fullscreen holds it; leaving fullscreen ends it.
    2760  tvsetting auto
    2760  expect tv=0
    2760  expect desk-pad=1
    2760  pad tap a
    2800  expect tv=1
    2800  expect tv-screen=home
    2800  expect loaded=0
    2800  expect desk-pending=0
    2800  key tap H
    2840  expect tv=0
    2840  pad tap dpdown
    2880  expect tv=1
    2880  fullscreen on
    2900  expect fullscreen=1
    2900  key tap H
    2940  expect tv=1                 # fullscreen holds it
    2940  fullscreen off
    2980  expect tv=0
    2980  fullscreen on
    3020  expect tv=1                 # the fullscreen trigger on its own
    3020  key tap ctrl+shift+T
    3060  expect tv=0                 # manual wins ...
    3060  key tap H
    3100  expect tv=0
    3100  fullscreen off
    3100  fullscreen on
    3140  expect tv=1                 # ... until the next edge
    3140  fullscreen off
    3180  expect tv=0
    3180  pad tap touchpad            # no nav intent (C_HELP's button): not a trigger.  (Not misc1: a Share tap opens the keypad overlay)
    3220  expect tv=0
    3220  pad tap start
    3260  expect tv=1                 # Start is a mapped intent: it switches
    3260  key tap H
    3300  expect tv=0
    3300  key tap ctrl+,              # a desktop window takes the pad before the trigger sees it
    3340  pad tap dpdown
    3380  expect tv=0
    3380  key tap ctrl+,
    3420  tvsetting manual
    3420  saveconfig
    3420  expect cfg=ui.tv_mode:absent
    ```
    and, after the first `load` (the game runs): `tvsetting auto`, `pad tap dpdown` → `expect tv=0` (the game owns the pad),
    `fullscreen on` → `expect tv=1`, `expect menu-open=0`, `expect paused=0`, `pad down dpright` → `expect core-input=1`,
    `pad up dpright`, `fullscreen off` → `expect tv=0`, `tvsetting manual`. Also: the script's **first line becomes
    `0 tvsetting manual`** (a fresh config now takes the build default, Auto on a CTest build), and the comment "acts as Manual
    in phase 2" at line ~200 becomes "the toggle works under Auto too" (the `expect` values there are unchanged). New expect
    `tv-setting=off|manual|auto` (`settingKey(tvSetting)`), used by hand in Step 3 of Task 17, not by the CTest script.
- [ ] **Step 2: Run** `~/Workspace/Provenance/vj-eval/remote/tvb.sh "tv_mode_unit|tv_menu_headless"`. Expected: FAIL (does not build:
  `effectiveSetting` gone, no `fullscreen` verb).
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run** `~/Workspace/Provenance/vj-eval/remote/tvb.sh ""` (the full suite). Expected: all PASS; `tv_ui_smoke` and
  `settings_reset` unchanged.
- [ ] **Step 5: Commit** `feat(app): TV mode switches on in fullscreen and on a controller press (#991)`.

### Task 16a: layout fixes from the user's TV testing

Closed with no work: on 2026-10-22 the owner reported that no layout fixes are needed, so PR 4 closes #991.

### Task 17: the settings UI and the build default

**Files:**
- Modify `ui.cpp` `drawInterfaceTab`, `tv_settings_model.h/.cpp`, `tv_app_host.cpp` (`settingRows`, `setSetting`),
  `app/CMakeLists.txt`, `.github/workflows/app-package.yml` (own commit, see the constraints), `tests/tv_settings_model_test.cpp`,
  `tests/tv_ui_smoke.cpp`, `tests/tv_menu_headless.txt.in`.
- Test: `tv_settings_model_unit`, `tv_ui_smoke`, `tv_menu_headless`.

**Interfaces:**

```cpp
// tv_settings_model.h
std::vector<SettingRow> tvAutoRows(TvSetting stored, bool autoFullscreen, bool autoController);   // ids tv_auto_fs, tv_auto_pad; empty unless Auto
```
```cmake
# app/CMakeLists.txt, next to VJ_DISCORD_APP_ID
set(VJ_TV_DEFAULT "auto" CACHE STRING "TV mode setting for a fresh config: auto (nightlies, PR builds) | manual (releases) | off")
if(NOT VJ_TV_DEFAULT MATCHES "^(auto|manual|off)$")
    message(FATAL_ERROR "VJ_TV_DEFAULT must be auto, manual or off (got '${VJ_TV_DEFAULT}')")
endif()
# with the other virtualjaguar definitions (~467):  VJ_TV_DEFAULT_SETTING="${VJ_TV_DEFAULT}"
```
```yaml
# app-package.yml, env: (top level)
TV_DEFAULT: ${{ (startsWith(inputs.release_tag, 'v') || inputs.release_tag == 'prerelease') && 'manual' || 'auto' }}
# each of the three cmake -S app lines:  -DVJ_TV_DEFAULT="$TV_DEFAULT"
# before the flatpak-builder step:  sed -i "s|- -DVJ_APP_BUILD_TESTS=OFF|&\n      - -DVJ_TV_DEFAULT=$TV_DEFAULT|" app/packaging/flatpak/io.github.libretro.VirtualJaguar.yml
```

Rules:
- `drawInterfaceTab`, after the Overlays section: `SeparatorText("TV mode")`; combo `##tv_mode` over `settingKey`-ordered labels
  "Off / Manual / Auto" → `app.setTvSetting((TvSetting)i)` on change; `if (app.tvSetting == Auto)` two `Checkbox`es bound to
  `cfg.getBool(...)` copies → `app.setTvAuto(true|false, v)`; `ui::hint("A 10-foot look for a pad and a TV. Manual: View > TV mode,
  Ctrl/Cmd+Shift+T, or F4 / a Share hold with no game. Auto also turns it on in fullscreen and when a controller is used in the
  library (mouse movement or typing turns it off again). Nightly builds start on Auto, releases on Manual.")`.
- `AppHost::settingRows`: after `tvModeRow`, append `tvAutoRows(app_.tvSetting, cfg.getBool("ui.tv_auto_fullscreen", true),
  cfg.getBool("ui.tv_auto_controller", true))`. `setSetting("tv_auto_fs", i)` → `setTvAuto(true, i == 1)`; `"tv_auto_pad"` →
  `setTvAuto(false, i == 1)`; `"tv_mode"` → `setTvSetting((TvSetting)index)` (the existing body moves into App).
- `setTvAuto` while TV is on under Auto: `configure` restarts from off, so restore with `setOn(true)` (Manual cause) as
  `setSetting("tv_mode")` does; the next trigger edge takes over.

- [ ] **Step 1: Write the failing tests.**
  - `tv_settings_model_test.cpp`: `CHECK(tvAutoRows(TvSetting::Manual, true, true).empty() && tvAutoRows(TvSetting::Off, true, true).empty());`
    `auto a = tvAutoRows(TvSetting::Auto, true, false); CHECK(a.size() == 2 && a[0].id == "tv_auto_fs" && a[0].index == 1 && a[1].id == "tv_auto_pad" && a[1].index == 0 && a[0].group == 3 && a[1].choices == std::vector<std::string>{"Off", "On"});`
  - `tv_ui_smoke` `settingsAutoRowsStep`: fixture rows `tv_mode` (index 2) + the two auto rows; Right on "Auto: in fullscreen" →
    `set.back() == {"tv_auto_fs", 1}`; Accept on it → `{"tv_auto_fs", 0}` (wrap over two choices); `drew("Auto: when a controller is used")`.
  - `tv_menu_headless`, a block right after Task 16's Auto block (still before "A game to play with"; the only
    `cfg=ui.tv_mode:absent` expects are earlier in the script, so the key this block writes disturbs nothing): `tvsetting auto`,
    `key tap F4` → `expect tv=1`; RB ×3 → `expect tv-screen=settings`; Down ×3 (Interface), Right (the list at Show FPS), Down ×3
    (VSync, Theme, TV mode), Down (Auto: in fullscreen; the Interface list is now Show FPS, VSync, Theme, TV mode, Auto: in
    fullscreen, Auto: when a controller is used, More settings...); `pad tap dpleft` → `expect cfg=ui.tv_auto_fullscreen:0`,
    `expect tv=1` (still on: Manual cause after the reconfigure); `pad tap dpright` → `expect cfg=ui.tv_auto_fullscreen:1`; Up,
    `pad tap dpleft` (TV mode: Auto → Manual) → `expect cfg=ui.tv_mode:manual`, `expect tv=1`; Down (the row under it is now
    More settings..., the Auto rows are gone), `pad tap a` → `expect tv-more=1`, `pad tap b` → `expect tv-more=0`; `key tap F4`
    → `expect tv=0`. The config holds `ui.tv_mode=manual` from here on, which is what the rest of the script assumes anyway.
- [ ] **Step 2: Run** `~/Workspace/Provenance/vj-eval/remote/tvb.sh "tv_settings_model_unit|tv_ui_smoke|tv_menu_headless"`. Expected: FAIL.
- [ ] **Step 3: Implement** the model rows, `AppHost`, the Interface block, CMake. Check by hand on the Studio that the macro
  reaches the binary: a one-line script `0 expect tv-setting=auto` run with `--headless --frames 1 --tv-test-script` (a headless
  run has a fresh config) passes on the default configure and `0 expect tv-setting=manual` passes after
  `cmake -S app -B app/build -DVJ_TV_DEFAULT=manual`; reconfigure back to `auto` afterwards. (The CTest script itself starts with
  `tvsetting manual`, so it is the same on either build: release packages run the smoke tests too.)
- [ ] **Step 4: Run** the full suite on the Studio. Expected: all PASS.
- [ ] **Step 5: Commit** `feat(app): TV mode in Settings > Interface, the Auto rows in TV Settings, VJ_TV_DEFAULT (#991)`.
- [ ] **Step 6: The workflow**, its own commit: `ci(app): release packages default TV mode to Manual, nightlies to Auto (#991)`.
  Verify with `gh workflow run app-package.yml -f release_tag=v0.0.0-test` only if the user asks (it signs); otherwise rely on
  the PR's own `app.yml` package job (`release_tag = prNNN` → Auto) and read the configure log for `VJ_TV_DEFAULT`.

### Task 18: the first-use hint, README, Studio check, PR 4

**Files:**
- Modify `input.h/.cpp` (`tvMenuBindingText(SDL_JoystickID) const`, `tvMenuKeyText() const`), `desktop_nav_app.cpp`
  (`deskPadHandleEvent` + a static `tvMaybeShowHint`), `app.h/.cpp` (`bool tvHintShownThisRun_ = false; unsigned toastCount_ = 0;`),
  `tv_script.cpp` (expects `toast=TEXT`, `toast-count=N`), `app/README.md`, `tests/tv_menu_headless.txt.in`.
- Test: `tv_menu_headless`.

**Interfaces:**

```cpp
// input.h
std::string tvMenuBindingText(SDL_JoystickID id) const;   // display() of that pad's first C_TVMENU binding, hold bindings first; "" when none / unknown pad
std::string tvMenuKeyText() const;                        // display() of the keyboard profile's first plain C_TVMENU key ("F4"); "" when none
// tv_mode.h (pure, so tv_mode_unit can pin the wording)
std::string firstUseHint(const std::string& padText, const std::string& keyText, bool macOS);   // decision 8's wording
```

Rules:
- In `deskPadHandleEvent`, at the `return down;` of a consumed mapped button: `if (down) tvMaybeShowHint(app, e.gbutton.which)`,
  a static in `desktop_nav_app.cpp` that applies decision 8 (`tvSetting == Manual`, key unset, not shown this run → set the key,
  `saveConfig()`, `toast(tv::firstUseHint(input.tvMenuBindingText(which), input.tvMenuKeyText(), <macOS>), 6.0)`,
  `tvHintShownThisRun_ = true`).
- README `## Running`: a new `### TV mode` after "Controller in the desktop library": what it is, the three settings and where
  (Settings > Interface, TV Settings > Interface), the Auto rules in one paragraph (fullscreen; a controller press in the library;
  32 px of mouse or typing ends a controller-started TV mode; a manual toggle wins until the next edge), the build default
  (nightlies Auto, releases Manual, `-DVJ_TV_DEFAULT`), the hint, `--tv`, the hold rule (500 ms) and the keypad-chord limitation
  (from the controls screen's hint text). Keep the existing rows at 113–114 and 177 and link them here.

- [ ] **Step 1: Write the failing tests.** `tv_mode_test.cpp`: `CHECK(firstUseHint("Share (hold)", "F4", false) == "Press Share
  (hold) or F4 for TV mode" && firstUseHint("", "F4", false) == "Press F4 or Ctrl+Shift+T for TV mode" && firstUseHint("", "", true)
  == "Press Cmd+Shift+T for TV mode");`. In the script's existing desktop-pad block (the first consumed press under Manual, today
  `2080 pad tap rightshoulder`): before it `expect cfg=ui.tv_hint_shown:absent`, `expect toast-count=0`; after it
  `expect toast=Press Share (hold) or F4 for TV mode` (the virtual pad has `misc1`, so its default is the Share hold),
  `expect cfg=ui.tv_hint_shown:1`, `expect toast-count=1`; after the next press (`pad tap leftshoulder`) `expect toast-count=1`.
  In the Auto block: `expect toast-count=1` after the switching press (no hint under Auto). `toast-count` counts `App::toast`
  calls since start (`toastCount_`, incremented in `toast`; `osd_` is capped at 6 so it cannot be the counter); `toast` is the
  last text.
- [ ] **Step 2: Run** `~/Workspace/Provenance/vj-eval/remote/tvb.sh tv_menu_headless`. Expected: FAIL.
- [ ] **Step 3: Implement**, then the README.
- [ ] **Step 4: Run** the full suite on the Studio. Expected: all PASS.
- [ ] **Step 5: Commit** `feat(app): one-time TV mode hint on the first controller press in the library (#991)` and
  `docs(app): TV mode section in the README (#991)`.
- [ ] **Step 6: Windowed check on the Studio** (`vjr.sh push` / `run` / `pull`, never locally), with the windowed script: Auto,
  `mouse` moves of 10 px ×4 after a pad press → `expect tv=0` (the only place the real motion path is exercised); F11 with
  `expect tv=1`; the hint toast in a `shot hint.png` at 1280×720. Attach the shot.
- [ ] **Step 7: Open PR 4** into `develop`: title `feat(app): TV mode auto-switching, first-use hint and build default (TV mode
  phase 4)`; body with the test summary, the shot, the approval items below as applied, the "owed to the user" list (below), and
  `Closes #991` **only if Task 16a is done or the user has said the layout list is empty**; otherwise `Refs #991` and a comment on
  #991 listing what remains. Bind it (`ccd_pr`), report CI, rebase after the v3.10.0 back-merge lands on develop.

### What "complete" means for #991

PR 4 closes #991 when every spec section has shipped or has a recorded decision: §3 (PR 1), §4–§5 (PRs 2–3, with the approved
changes), §6 at the spec's starting values, §7, §8 and §9 (this PR), §10's CI tests, and Task 16a's layout list. It leaves
these **owed to the user**, listed in the PR body and in a final comment on #991 rather than blocking the close:
- tuning of `kRepeatDelayMs` / `kRepeatMs` / `kTweenSeconds` / `focusScale` (and whether 32 px is the right mouse budget) on a real
  TV with Xbox, DualSense, Switch Pro and an 8BitDo pad, one machine where Steam or the OS takes Guide (spec §10 "by hand");
- the §9 decision to make Auto the release default once nightly feedback is in (a one-line `app-package.yml` change);
- the layout list for Task 16a, if it arrives after PR 4 (then a follow-up PR, `Refs #991`).

### Phase 4 review focus (added to the list above)

17. **TV mode flapping on and off.** A repeated `SDL_EVENT_WINDOW_ENTER_FULLSCREEN` (macOS sends them on space and display
    changes) must not re-enable TV mode after a manual off (Task 16 `tvFullscreenEdge`, the `testTriggerOrders` repeated-enter
    case documents why); a Share hold that toggles TV mode must not also count as the controller trigger (decision 1: no nav
    intent; script `pad tap misc1 → tv=0`); F4 / Ctrl+Shift+T must not first count as a "non-navigation key" (decision 3; script
    `ctrl+shift+T → tv=0` while controller-started).
18. **A pad press that both switches and acts.** The switching press reaches neither the desktop library nor the TV shell
    (script: `pad tap a → tv=1, loaded=0, desk-pending=0`); its release is ignored by both navs' press-seen latches (by hand on
    the Studio: hold A, watch nothing launch on release).
19. **Auto turning on mid-game.** The controller trigger never fires with a game loaded (script: `pad tap dpdown → tv=0` after
    `load`); the fullscreen trigger does, and must leave the game running, unpaused, with its input (script: `menu-open=0,
    paused=0, core-input=0`… then a held D-pad reads `core-input=1`).
20. **A stored setting pinning the default.** `ui.tv_mode` is written only by `setTvSetting` on change (script:
    `expect cfg=ui.tv_mode:absent` after `tvsetting` + `saveconfig`; `expect tv-setting=auto` first line proves the CMake default
    reached the binary); a config from Phase 2/3 with no key picks up the nightly's Auto; Settings > Reset Interface returns to
    the build default (`settings_reset_test`, already green).
21. **The hint showing twice, or under Auto.** The key is set and saved before the toast, a per-run flag backs it, and Auto's
    switching press takes the `return` before the hint site (script `toast-count` lines in both blocks).

### Approval needed before Phase 4 starts (spec changes for phase 4)

The rows already in "Changes to the approved spec" that this phase uses: `C_TVMENU` toggles TV mode with no game (the hint's
wording relies on it); the build default through `VJ_TV_DEFAULT`, now in `app-package.yml` rather than `release.yml` (the
packages moved there after the plan was written; the rule is the same: tags and `prerelease` Manual, everything else Auto).

New in this revision:
- **A.** The pad press that turns TV mode on under Auto is **consumed**: it does not also act in the desktop library or on the
  TV screen it reveals. Spec §8 says the press "turns TV mode on" and no more; the pad-nav design §2 recorded that the trigger
  wins, this fixes that the press does nothing else. Desktop pad navigation is therefore unreachable under Auto with the
  controller trigger on (it serves Off, Manual, and Auto with that box unticked).
- **B.** Only buttons with a navigation intent in the pad's profile (D-pad, the four face buttons, LB/RB, Start) are the
  controller trigger. Guide, Share, touchpad, paddles, the sticks and the triggers are not (they carry app actions or drift).
  Spec §8 says "a pad button".
- **C.** "A non-navigation key" (§8) is read as a printable key the TV UI did not use, with no Ctrl/Cmd/Alt held. F-keys,
  Esc, Tab, Enter and shortcuts never end TV mode by themselves (F4 and Ctrl/Cmd+Shift+T are toggles and would otherwise
  invert).
- **D.** The fullscreen trigger stays live with a game running (spec §8 as written, confirmed rather than changed): the only
  visible effect is which home shows when the game closes. Flagged because the plan's old Task 16 did not test it.
- **E.** The one-time hint's wording is "Press <this pad's TV-menu binding> or <the keyboard's> for TV mode" ("Press Share
  (hold) or F4 for TV mode" on a pad with Share, "Guide (hold)" without, the paddle's name when only a paddle is bound), shown
  as a 6 s toast at the first pad press in the desktop library, Manual only, once per install (a Settings > Reset of Interface
  can show it once more). Spec §9 fixes the text to "Press Share (hold) or F4".
- **F.** No motion or repeat tuning ships in PR 4; the spec's starting values stay and the real-TV pass is owed to the user
  (spec §9 step 4 bundles "tuning … on a real TV" into this delivery step).
- **G.** PR 4 may close #991 with Task 16a's layout list still outstanding only if the user says so; otherwise it stays
  `Refs #991` and the layout PR closes it.

---

## Phase 5 — TV mode owns Settings and Library, the full settings catalogue (more commits on PR 4)

Written 2026-10-22 from the owner's test of PR 4 ("once you open a game, the tv mode isn't the mode for settings or library
until you close the game"; "you can't even see the game core settings in tv mode"; "finish the settings at least in tv mode").
Same branch and worktree as Phase 4 (`feat/991-tv-phase4`, `~/Workspace/Provenance/vj-wt/tv-phase4`); the work lands as more
commits on PR #1067. Build and test on the Studio only: `~/Workspace/Provenance/vj-eval/remote/tvb.sh "<ctest regex>"`.

### Task 18b (done separately, do not redo): Settings entry points

`App::openSettings()` (`app.h:291`, `app.cpp:1429`) is what Ctrl/Cmd+, (`app.cpp:1788`), the Settings menu (`ui.cpp:671`),
the native `Cmd::Settings` (`platform/commands.cpp:306` → `desktop.cpp:127`), the BIOS openers (`ui.cpp:602`, `desktop.cpp:84`,
`library_ui.cpp:1102`) and `--show-window settings` call. While TV mode is on it calls `Shell::openSettings()`
(`tv_shell.cpp:67`: the Settings section with no game; with a game `openMenu()` + the Settings screen in the slot, so the hardcore
pause gate and its toast apply); otherwise the desktop window. New script expect `menu-slot`. Phase 5 builds on it.

### What Phase 4 left (read before any task)

- **Core options reach the app only through `CoreHost::startSession`** (`core_host.cpp:794` `retro_set_environment(envCb)` →
  `libretro.c:1521` `libretro_set_core_options`), which `loadContent` / `loadNoContent` run for the first game. At startup `defs_`
  is empty: `ui.cpp:126` prints "The core's options appear here once a game is loaded", `AppHost::settingRows`
  (`tv_app_host.cpp:238`) hides the three curated core rows and shows `speedNoteRow()`. `captureOptions` (`core_host.cpp:196`)
  reads a `retro_core_options_v2` into `OptionCategory` / `OptionDef` (`emu_snapshot.h:26-41`: `desc`, `descCategorized`,
  `categoryKey`, `values[]` with labels), applies `optionLock` (`app.cpp:534`, `lockedCoreOptionFor`) and `optSerial_++`. The
  table has 10 categories and 118 options (`libretro_core_options.h`: video, bios_boot, cdrom, network, input, input_p1,
  input_p2, diagnostics, speed, accuracy); `update_option_visibility` (`libretro.c:1522`, installed through
  `SET_CORE_OPTIONS_UPDATE_DISPLAY_CALLBACK`, `core_host.cpp:522`) hides gated rows by `SET_CORE_OPTIONS_DISPLAY`.
- **Core options are readable without a game.** `retro_set_environment` (`libretro.c:1502-1535`) only stores the callback, asks
  for the log interface, pushes the options and the display callback, and asks for VFS and the microphone; libretro allows it
  before `retro_init`, and `startSession` repeats it for every game anyway (`stopSession` then `retro_set_environment` again).
  So one call at startup, with `CoreHost::env` answering only the option commands (`GET_CORE_OPTIONS_VERSION`,
  `SET_CORE_OPTIONS_V2[_INTL]`, `SET_CORE_OPTIONS_UPDATE_DISPLAY_CALLBACK`, `SET_CORE_OPTIONS_DISPLAY`, `GET_VARIABLE`) and
  refusing the rest, fills `defs_` / `categories_` / `visibility_` exactly as a load does. It must run on the UI thread before
  `startEmuThread()` (`app.cpp:2135`; the thread publishes the table at once, `core_host.cpp:1213`) and after
  `loadUserOptions` (`app.cpp:189`) and `wireHost()` (the lock). A write with no game is `setOptionValue` → `userValues_` →
  `onOptionChanged` → `opt.<key>` in the config (`app.cpp:544`), read by the core at the next load through `GET_VARIABLE`.
  Rejected alternative: including `libretro_core_options.h` in a C++ TU (it has `extern "C"` guards, lines 60 and 2103) — a
  second copy of the table that bypasses `captureOptions`' lock and visibility rules.
- **Library entry points that open the desktop window today:** Ctrl/Cmd+L `app.cpp:1792` → `App::toggleLibrary` `app.cpp:1137`
  (`library.show`, or the home search box); File > "Game library..." `ui.cpp:585`; native `Cmd::Library` `platform/commands.cpp:275`
  → `desktop.cpp:88`; `--show-window library` `app.cpp:2188` (dev flag, stays). The native checkmark reads `s.libraryOpen`
  (`desktop.cpp:35`). The script documents the current behaviour at `tv_menu_headless.txt.in:100-121` ("Ctrl+L opens the
  desktop library WINDOW" with TV on): Phase 5 flips that block. `App::openContent` (`app.cpp:1160`) already closes the menu
  (`:1166`) and auto-saves the running game (`:1171`), so a launch from the in-game Library keeps the running game's state.
- **The in-game menu slot** (`Shell::menuPushScreen`, `tv_shell.cpp:332`) hands the slot screen the menu's list area only
  (`tv_ingame.cpp:89-94`: a 1500u panel minus a 470u rail); `Shell::intent` (`:349-352`) forwards every intent but Back, and
  `typed()` goes to the Base stack's top only (`tv_shell.cpp:129`), never to the slot. `LibraryScreen` launches with
  `host_.launch(*g)` + `shell_.launched()` (`tv_library.cpp:342`), `launchState` (`:359`), pushes `DetailsScreen` on Start (`:350`)
  and `LettersScreen` on Y (`:482`) onto the Base stack — none of which can show while a game runs.
- **`NavInput` repeats held directions** (`tv_nav.h:30`, 350 ms then 80 ms, Left and Right included), so a held Right on a
  settings row already steps every 80 ms; each step is a `setSetting` + `saveConfig()`.
- **Settings groups are the four ints of `tv_settings_model.h`** (`kSettingGroups`, `settingGroupLabel`, `SettingRow::group`);
  the rail is not scrollable (`tv_settings.cpp:88-96` draws every group at `kItemH + kItemGap`).
- **App settings that are a choice, toggle or slider in the desktop tabs** (the catalogue Task 19 lists; everything else — folders,
  BIOS files, the font file, RA sign-in, remapping, the per-device axis sliders `devices_ui.cpp:323`, the theme editor
  `theme_ui.cpp:161`, the Discord app id, netplay host/port, cheats — stays desktop-only): Video `ui.cpp:184-196` (integer, smooth,
  stretch, aspect, vsync, fullscreen), shader `shaders_ui.cpp:189`; Audio `ui.cpp:204-206` (volume, mute, buffer target);
  Input `devices_ui.cpp:335-347` (stick dead zone, mouse drives, light gun aims with), `:362` (controls overlay at start);
  Storage `ui.cpp:244` (flush saves); Interface `ui.cpp:285` (FPS), `:292` (emulation thread), `:304` (resume mode), `:315` (home
  library), `desktop.cpp:201-230` (tray icon, in-window menu bar, auto-hide menu in fullscreen, windows outside, keep awake),
  `theme_ui.cpp:111` (theme), `library_ui.cpp:237` (show as), `:284` (tile size), `:299` (download box art), `:312` (close on
  launch); Achievements `achievements_ui.cpp:345` (download pictures); Discord `ui.cpp:332-337`; Neuro `neuro_app.cpp:178-197`.

### Phase 5 decisions (lead's calls; told to the owner, no approval round)

1. **While TV mode is on, Settings and Library are TV screens, never desktop windows** — with or without a game. Every opener
   goes through `App::openSettings()` (Task 18b) or the new `App::openLibrary()`. The only desktop window TV mode still opens is
   the one behind the single "Desktop settings…" row (folders, BIOS files, sign-in, remapping, text entry), which keeps Phase 3's
   hand-off (`tv_more_settings.h`).
2. **Core options are shown always**, read at startup by `CoreHost::probeOptions()` (the environment probe above). Phase 3
   approval G (hidden until a game has run) and `speedNoteRow()` go. The three curated core rows (`resolution`, `risc_clock`,
   `idle_skip`) go too: they are ordinary rows of the core's own Video and Speed categories.
3. **Groups are the core's categories plus the app's tabs**, in this rail order: Picture, Sound, Interface, Library, Achievements,
   Discord, Neuro (build-gated), then the core's categories in definition order (`General` for options with no category, as
   `drawCoreOptions` does). Groups with no visible row are absent. `SettingRow::group` becomes a string key; the rail scrolls.
4. **Every visible core option is a row**: label `descCategorized` (else `desc`), value `label` (else `value`), `info` is not shown
   (no room; the desktop tooltip stays there). Hidden: `!optionVisible(key)` (covers the app's locks). Read-only with the note
   "Set by Netplay or --option for this session": `optionOverridden(key)`. Typed keys (`typed_options.h`): the presets plus a
   selected "<value> (custom)" when the current value is not a preset (Phase 3's rule, now for every typed key); `OptKind::Host`
   (`virtualjaguar_netlink_host`) is read-only with the note "Type it in Desktop settings". Options whose `info` says "restart"
   carry the note "Applies when the game next starts". No "reset" and no per-group reset in TV. Writes stay
   `CoreHost::setOptionValue(key, def->values[i].value)` — the call `drawOption` makes, so `Achievements::checkSettings` and the
   netplay / `--option` rules are unchanged.
5. **Sliders become stepped lists**: volume 0–100 in 10 %, audio buffer 20–200 ms in 10 ms, stick dead zone 0–40 % in 5 %, tile
   size `kTileScaleMin..kTileScaleMax` in 0.1, Neuro pause 0–10 s in 0.5. The desktop slider keeps its finer value: a current value
   off the grid shows as "<value> (custom)" and is selected; stepping lands on the grid.
6. **Long lists on a pad**: Up/Down clamp (no wrap) in both the rail and the list, as Phase 3; Left/Right clamp, Accept wraps; the
   held-direction repeat of `NavInput` is the "many steps" path; the focus is remembered per group under
   `settings/<groupKey>` (index + row id) and the rail under `settings/rail` (group key); the rail and the list both scroll to
   show the focus with the Phase 3 tween; LT/RT (PagePrev/PageNext) jump the list a page. Each repeated step still saves the
   config (12.5 Hz while held; `config.ini` is small — measured on the Studio in Task 20, step 4).
7. **The in-game menu gains a "Library" row** after Load state (root order: Resume, Save state, Load state, Library,
   Achievements, Overlays, Settings, Controls, Enter/Exit TV mode, Reset, Close game). It puts `LibraryScreen` in the slot;
   a slot screen may ask for the whole panel (`Screen::wide()`), which the Library does, so the rail + cover grid draw unchanged
   over the dimmed game. In the slot: A = launch (with the confirm below), LT/RT and typing work; Start (Details) and Y (letter
   picker) do nothing (they push onto the Base stack, which does not show under a game); X (resume a state) asks the same confirm.
8. **Launching from the in-game Library asks first**: a page `ConfirmLaunch` ("Close <running title> and start <title>?", rows
   No / Yes, focus on No, as Reset and Close do). Yes → `host.launch(g)` / `launchState(g, st)` — `App::openContent`, which
   auto-saves the running game and closes the menu — then `launched()`. The same confirm is used by `Shell::requestLaunch`
   whenever a game is loaded; with no game it launches at once (Home, Library, Details, Achievements are unchanged).
9. **`App::openLibrary()`** replaces every call of `toggleLibrary()` that is a user action: TV on and no game → the Library
   section; TV on and a game → the menu (through `openMenu()`, hardcore gate and toast as Settings) with the Library in the slot;
   TV off → `toggleLibrary()` as today. `desktop.cpp:35` `libraryOpen` is also true while the TV Library section or slot shows.
10. **No `app.cpp` edits beyond**: `probeOptions()` in `run()` before `startEmuThread()`, `openLibrary()`, the Ctrl/Cmd+L line.
    No `tv/` file calls `emu.call` / `emu.post`; the probe is a `CoreHost` member called from `App`.

### Task 19: core options at startup, and the settings catalogue (model)

**Files:**
- Modify `core_host.h/.cpp`: `void probeOptions();` (UI thread, before the emulation thread exists) and `bool probing_ = false;`
  checked at the top of `env()`: while set, only the option commands listed above are handled, everything else returns false.
  After `retro_set_environment(envCb)` call `updateDisplayCb_()` once (the core's `update_option_visibility` reads its gates
  through `GET_VARIABLE` and hides rows through `SET_CORE_OPTIONS_DISPLAY`, both allowed), then `probing_ = false`.
  `startSession` is unchanged (it clears and re-pushes). `probeOptions` is a no-op when `initialized_`.
- Modify `app.cpp` `run()`: `host.probeOptions();` after `netplayInit` and before the `runHeadless / runTvScript / runInteractive`
  line (the thread starts inside `runInteractive`, `app.cpp:2135`; headless runs stay inline).
- Modify `tv_host.h` `SettingRow`: `int group` → `std::string group` (key) + `std::string groupLabel`; `tv_settings_model.h/.cpp`:
  drop `kSettingGroups`, `settingGroupLabel`, `speedNoteRow`; add the builders below; `tv_app_host.h/.cpp`: the catalogue.
- Modify `ui.cpp` `drawCoreOptions`: drop the "appear here once a game is loaded" branch (the table is there); keep the rest.
- Test: `tv_settings_model_unit`, new `vj_core_options_probe_test` (`app/tests/core_options_probe_test.cpp`, links `vj_core`
  and `core_host.cpp` as `core_host_thread_test` does), `tv_ui_smoke`.

**Interfaces:**

```cpp
// tv_settings_model.h (pure)
struct SettingGroup { std::string key, label; };
// Every visible core option as a row, in category order: uncategorised first under {"core:", "General"}, then each category
// under {"core:" + cat.key, cat.desc}.  id = "core:" + key.  Rules of decision 4.  `visible`, `locked`, `overridden`, `current`
// are per key (std::function), so the model never sees CoreHost.
std::vector<SettingRow> coreOptionRows(const std::vector<OptionCategory>& cats, const std::vector<OptionDef>& defs,
                                       const std::function<std::string(const OptionDef&)>& current,
                                       const std::function<bool(const std::string&)>& visible,
                                       const std::function<bool(const std::string&)>& overridden,
                                       const std::function<const TypedOption*(const std::string&)>& typed);
SettingRow choiceRow(const std::string& id, const std::string& label, const std::string& group, const std::string& groupLabel,
                     std::vector<std::string> choices, int index);
// A slider as a stepped list: "lo", "lo+step", ... "hi" printed with fmt ("%.0f ms"); a value off the grid is appended as
// "<value> (custom)" and selected.  stepValue() maps an index back (the "(custom)" index gives `current` unchanged).
SettingRow sliderRow(const std::string& id, const std::string& label, const std::string& group, const std::string& groupLabel,
                     float current, float lo, float hi, float step, const char* fmt);
float stepValue(const SettingRow& r, int index, float lo, float step, float current);
std::vector<SettingGroup> groupsOf(const std::vector<SettingRow>& rows);   // first-seen order, keys unique
// Existing builders keep their names; their `int group` parameter becomes (key, label); onOffRow(id, label, group, groupLabel, on).
```

Catalogue (`AppHost::settingRows`, in this order; ids in parentheses; every setter is the desktop control's member or method
followed by `app.saveConfig()`, as Phase 3 decision 9):
- **Picture** `picture`: shader (`shader`), integer (`integer`), smooth (`smooth`: `present.linearFilter`), stretch (`stretch`:
  `present.stretch`), aspect (`aspect`), vsync (`vsync`, moved here from Interface), fullscreen (`fullscreen`: `setFullscreen`).
- **Sound** `sound`: volume (`volume`), mute (`mute`: `app.mute` + `applyAudioSettings`), buffer (`audio_buffer`: `audioTargetMs`,
  20–200 by 10, `"%.0f ms"` + `applyAudioSettings`).
- **Interface** `interface`: fps, controls overlay at start (`controls_at_start`: what `devices_ui.cpp:362` writes), theme,
  tv_mode, tv_auto_fs, tv_auto_pad, in-window menu bar (`menu_in_window`: `showInWindowMenu`; absent where `desktop.cpp` hides the
  box), auto-hide menu in fullscreen (`menu_autohide`), windows outside (`windows_outside`), keep awake (`keep_awake`), tray icon
  (`tray`, platform-gated as `desktop.cpp:210`), emulation thread (`emu_thread`: `emuThreadPref`; note "Restart the app to apply"
  while `emuThreadWanted() != emuThreadThisRun()`), stick dead zone (`dead_zone`: 0–40 by 5, `"%.0f %%"`), mouse drives
  (`mouse_port`), light gun aims with (`gun_source`), flush saves (`flush_saves`: `autosaveSram`).
- **Library** `library`: resume mode (`resume`: `lib::resumeModeLabel` × `kResumeModeCount`), home library (`home_library`), show
  as (`show_as`: `lib::showAsLabel`), tile size (`tile_size`: `cellScale`, `kTileScaleMin..Max` by 0.1, `"%.1fx"`), download box
  art (`download_art`: `setDownloadArt`), close on launch (`close_on_launch`: what `library_ui.cpp:312` writes).
- **Achievements** `achievements`: download pictures (`ra_images`: `ach.setImagesWanted`); row absent when `!achHeader().available`.
- **Discord** `discord`: `discord_enabled`, `discord_art`, `discord_buttons` (`cfg.setBool` as `ui.cpp:332-337`).
- **Neuro** `neuro` (`VJ_HAVE_NEURO`): `neuro_enabled`, `neuro_auto_force`, `neuro_interval` (0–10 by 0.5, `"%.1f s"`).
- **The core's groups**: `coreOptionRows(host.optionCategories(), host.optionDefs(), …)`.
- Last row of Interface: `moreRow()` relabelled **"Desktop settings…"** (id stays `more`).
`setSetting("core:<key>", i)`: `defFor(key)`; not overridden, `i` within `def->values` (never the "(custom)" index) →
`host.setOptionValue(key, def->values[i].value)`.

- [ ] **Step 1: Write the failing tests.**
  - `core_options_probe_test.cpp`: `CoreHost h; h.probeOptions(); CHECK(h.optionDefs().size() >= 100 && h.optionCategories().size() == 10);`
    a def for `virtualjaguar_internal_resolution` exists with `categoryKey == "video"`; `h.optionVisible("virtualjaguar_netlink_host")`
    is false (netlink off by default hides its host; read the gate in `libretro.c update_option_visibility` when writing the line
    and pick a key it hides); `h.setOptionValue("virtualjaguar_internal_resolution", "2x")` with no game →
    `h.optionValueFor(key, "") == "2x"` and `onOptionChanged` was called with (`key`, `"2x"`); `probeOptions()` twice is one push
    (`optionPushes()` unchanged); `h.isLoaded()` stays false.
  - `tv_settings_model_test.cpp`: `coreOptionRows` with two categories and three defs (one uncategorised, one typed with a custom
    current, one hidden) → 2 rows in order General, then the category; `ids == {"core:a", "core:b"}`; the typed row's last choice
    is `"137% (custom)"` and selected; the hidden one absent; an overridden one `readOnly` with the note; a `Host`-kind typed key
    read-only with "Type it in Desktop settings"; a def whose `info` contains "restart" carries the restart note.
    `sliderRow("audio_buffer", …, 80, 20, 200, 10, "%.0f ms")` → 19 choices, `index == 6`, `choices[0] == "20 ms"`;
    `sliderRow(…, 85, …)` → 20 choices, last `"85 (custom)"` selected; `stepValue(r, 7, 20, 10, 85) == 90`; `stepValue(r, 19, …, 85) == 85`.
    `groupsOf` keeps first-seen order and drops duplicates.
  - `tv_ui_smoke` fixtures: `mkRow` takes a group key + label; the Phase 3 tests that walked `Down ×3` to Interface are rewritten
    against the new fixture (`settingsFixture()`: picture, sound, interface, "core:video", "core:speed").
- [ ] **Step 2: Run** `~/Workspace/Provenance/vj-eval/remote/tvb.sh "core_options_probe|tv_settings_model_unit|tv_ui_smoke"`. Expected: FAIL.
- [ ] **Step 3: Implement** the probe, the model, the catalogue. Register `vj_core_options_probe_test` in `VJTvTests.cmake` the
  way `core_host_thread_test` is registered.
- [ ] **Step 4: Run** the full suite on the Studio. Expected: all PASS (`settings_reset_test` and `core_host_test` unchanged).
- [ ] **Step 5: Commit** `feat(app): core options readable at startup; TV Settings catalogue of every core option and app choice (#991)`.

### Task 20: the TV Settings screen for long grouped lists

**Files:**
- Modify `tv_settings.cpp` (`SettingsScreen`), `tv_widgets.h/.cpp` only if a helper is missing, `tests/tv_ui_smoke.cpp`,
  `tests/tv_menu_headless.txt.in`, `tv_script.cpp` (expect `tv-group=KEY`: the focused group's key).
- Test: `tv_ui_smoke`, `tv_menu_headless`.

Rules:
- `refresh()` builds `groups_` with `groupsOf(all)`; the rail is a `LinearFocus` over them with its own `Tween` scroll and
  `moreArrow`s, clipped to the content height (the list's pattern at `tv_settings.cpp:110-150`); the focused group's item is kept
  in view. Memory: `settings/rail` → `putItem(groupKey)`; `settings/<groupKey>` → index + row id; `settings/list` as before.
- The list: as today, plus `PagePrev` / `PageNext` = `list_.move(∓visibleRows)`; the caption line is "<group> · N settings".
- Left on the first choice of a row still returns to the rail (Phase 3); a read-only row ignores Left/Right/Accept and shows its note.
- In the slot (embedded): the groups are the first level (as Phase 3) and that list scrolls the same way (17+ rows).
- The "Desktop settings…" row hint is glyph(Accept) "Open".

- [ ] **Step 1: Write the failing tests.**
  - `settings_groups_in_row_order`: fixture groups picture, sound, interface, core:video, core:speed → the rail labels in that
    order; a fixture with no `sound` row → `!drew("Sound")`.
  - `settings_rail_scrolls_720p`: 20 fixture groups at 1280×720, Down ×19 → `focusLabel()` is the last group and its item is
    drawn inside the content area (track the rail items' y in the test through `g_texts`).
  - `settings_held_right_repeats`: a `core:x` row with 30 choices; press Right and hold through `frames(host, shell, 90)` (≈1.5 s
    at the smoke clock) → `host.set.size() >= 10` and the last index is monotone; release → no further sets.
  - `settings_page_jump`: 40 rows, RT → the focus moved by the visible count, LT back to 0.
  - `settings_focus_memory_per_group_key`: row 3 of `core:video`, Up to the rail, Down to `core:speed`, Right, Back, Up, Right →
    row 3 of `core:video` again.
  - `settings_custom_value_row`: a slider row with "(custom)" selected: Right steps to the next grid value (`set.back().second`
    = the grid index), Left to the previous; Accept wraps to 0.
  - `settings_embedded_groups_scroll`: in the menu slot with 20 groups, Down ×19 keeps the focused group row inside the list area.
  - `tv_menu_headless`: in the existing Settings block (no game), after `expect tv-screen=settings`: `expect tv-group=picture`;
    Down ×7 → `expect tv-group=core:video` (Picture, Sound, Interface, Library, Achievements, Discord, [Neuro], General?, Video —
    count the groups the headless build has when writing the line: Neuro is build-gated and General exists only if an option has
    no category); Right; `expect tv-focus=Internal resolution` after the Downs that reach it; `pad tap dpright` →
    `expect core-option=virtualjaguar_internal_resolution:2x` **with no game loaded** (decision 2); `saveconfig` →
    `expect cfg=opt.virtualjaguar_internal_resolution:2x`; `pad tap dpleft` to restore. Shift the later blocks' times.
- [ ] **Step 2: Run** `~/Workspace/Provenance/vj-eval/remote/tvb.sh "tv_ui_smoke|tv_menu_headless"`. Expected: FAIL.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run** the full suite on the Studio; then by hand on the Studio (`vjr.sh run`, windowed) hold Right on the RISC
  overclock row for 3 s and read the frame time in the FPS overlay: no visible hitch from the per-step `saveConfig()`. If there
  is one, coalesce: `AppHost::setSetting` sets `saveDue_ = now + 0.5 s` and `AppHost` saves at the first frame past it.
- [ ] **Step 5: Commit** `feat(app): TV Settings scrolls its rail, pages its list, and shows every group (#991)`.

### Task 21: Library in TV mode (every entry point, the in-game row, the launch confirm)

**Files:**
- Modify `app.h/.cpp` (`void openLibrary();`, the Ctrl/Cmd+L line `app.cpp:1792`), `ui.cpp:585`, `desktop.cpp:35` and `:88`,
  `tv_shell.h/.cpp` (`openLibrary()`, `Row::Library`, `Page::ConfirmLaunch`, `requestLaunch`, `typed()` to the slot, `wide()`),
  `tv_screen.h` (`virtual bool wide() const { return false; }`), `tv_ingame.cpp` (a wide slot takes the panel: no rail, the
  hint bar still the shell's), `tv_library.cpp` (slot rules of decision 7, `shell_.requestLaunch`), `tv_home.cpp`,
  `tv_details.cpp`, `tv_achievements.cpp` (`requestLaunch` instead of `host_.launch` — a no-op change with no game),
  `tests/tv_ui_smoke.cpp`, `tests/tv_menu_headless.txt.in`, `tv_script.cpp` (expect `menu-page=root|save|load|confirm-reset|confirm-close|confirm-launch`).
- Test: `tv_ui_smoke`, `tv_menu_headless`.

**Interfaces:**

```cpp
// app.h
void openLibrary();   // Ctrl/Cmd+L, File > Game library, Cmd::Library: the TV Library while TV mode is on (the in-game menu with a game), else toggleLibrary()
// tv_shell.h
bool openLibrary();                                            // as openSettings(), with makeLibraryScreen in the slot
void requestLaunch(const GameRef& g, const StateRef* state);   // no game: launch now; a game running: Page::ConfirmLaunch first
```

Rules:
- `Shell::requestLaunch`: `!host_.gameLoaded()` → `launch` / `launchState` + `launched()`. Otherwise `pendingLaunch_ = {g, state}`
  and `enter(Page::ConfirmLaunch, Row::No)` with `title_ = "Close " + host_.gameTitle() + " and start " + g.title + "?"`; `Yes` →
  the launch (the app closes the menu and arms the guard) and `launched()`; `No` → back to the Library in the slot, focus kept
  (the slot screen is retired by `enter`, so `requestLaunch` keeps it aside and `No` puts it back: `menuPushScreen(std::move(kept), Row::Library)`).
- `Shell::handleEvent` key path: `typed(c)` goes to `menuScreen_` when set, else the Base top.
- `LibraryScreen` when `embedded()`: `Intent::Menu` and `Intent::Aux` ignored; `hints()` drop "Details" and "Jump to letter";
  Accept → `shell_.requestLaunch(*g, nullptr)`; X → `requestLaunch(*g, &st[0])`; `wide()` true.
- `drawMenu` with a wide slot: `DrawCtx::pos/size` = the panel inset (`pp + inset`, `ps − 2·inset`), the rail is not drawn, the
  hint bar is the screen's hints + Back "Menu".
- `App::openLibrary()`: `if (tvMode.active()) tvShell().openLibrary(); else toggleLibrary();` — a refused `openMenu()` toasts
  (hardcore) and nothing opens. `ui.cpp:585` and `desktop.cpp:88` call it; the menu checkmark at `ui.cpp:585` is
  `(library.show && !homeVisible()) || (tvMode.active() && tvShell().showingLibrary())` where `showingLibrary()` is the
  Library section on the Base layer or `menuScreenId() == "library"`.

- [ ] **Step 1: Write the failing tests.**
  - `ingame_rows_full_order`: now `{"Resume", "Save state", "Load state", "Library", "Achievements", "Overlays", "Settings",
    "Controls", "Enter TV mode", "Reset", "Close game"}`.
  - `ingame_library_slot_wide`: `loaded = true`, open the menu, Down ×3, Accept → `menuScreenId() == "library"`, a tile title is
    drawn, and the menu's rail text (the game title at `tv_ingame.cpp:86`) is not.
  - `ingame_library_launch_asks`: Accept on a tile → `menuPage() == Page::ConfirmLaunch`, `focusedLabel(shell) == "No"`,
    `launched.empty()`; Accept (No) → `menuScreenId() == "library"` and the same tile focused; Accept, Down, Accept (Yes) →
    `launched == {tile.key}` and `guardArmed == 1` (the fake's `launch` sets `loaded = false` as the app's close does).
  - `ingame_library_resume_state_asks`: X on a tile with a state → the confirm; Yes → `launchedState == {key, slot}`.
  - `ingame_library_typed_filters`: `keyEvent` 'd' → the grid shows only titles with a 'd' initial (the Base Library's own test
    pattern); `Intent::Aux` and `Intent::Menu` leave `screenId()`/`menuScreenId()` unchanged.
  - `ingame_library_back_returns_to_row`: Back → the slot is gone and `focusedLabel(shell) == "Library"`.
  - `base_library_opens_from_app`: `tvOn = true`, no game: `shell.openLibrary()` → `screenId() == "library"`; again → unchanged.
  - `tv_menu_headless`: flip the Ctrl+L block (`:100-121`): with TV on, `key tap ctrl+L` → `expect library-window=0`,
    `expect tv-screen=library`, `expect tv-owns-input=1`; again → unchanged; `pad tap b` → `tv-screen=home`. With TV off the block
    keeps its first lines (the home search box). In the game block: `key tap ctrl+L` → `expect menu-open=1`, `expect menu-slot=library`,
    `expect paused=1`; `pad tap a` → `expect menu-page=confirm-launch`, `expect tv-focus=No`, `expect loaded=1`; `pad tap b` →
    `expect menu-slot=library`; `pad tap b` → `expect menu-slot=none`; `pad tap b` → `expect menu-open=0`. Hardcore: not scriptable
    headless (no RA mock in this script); covered by `openMenu()`'s existing refusal test.
- [ ] **Step 2: Run** `~/Workspace/Provenance/vj-eval/remote/tvb.sh "tv_ui_smoke|tv_menu_headless"`. Expected: FAIL.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run** the full suite on the Studio. Expected: all PASS.
- [ ] **Step 5: Commit** `feat(app): Library is a TV screen while TV mode is on; in-game Library row with a launch confirm (#991)`.

### Task 22: Studio screenshots, README, the PR

- [ ] Extend `~/Workspace/Provenance/vj-eval/tvshots/tv-shots.txt`: Settings → `shot settings-rail.png`; Down to `core:video`,
  Right → `shot settings-core-video.png`; Down to `core:speed`, Right, Down to the last row → `shot settings-core-speed-end.png`
  (the list scrolled); `load <rom>`, `key tap ctrl+L` → `shot menu-library.png`; `pad tap a` → `shot confirm-launch.png`;
  `pad tap b` ×3, `key tap ctrl+,` → `shot menu-settings-core.png`; `quit`. 1280×720 and 1920×1080, never locally.
- [ ] README `### TV mode`: one paragraph — Settings and Library are TV screens whenever TV mode is on (Ctrl/Cmd+, and Ctrl/Cmd+L
  included), every core option is there by the core's categories, the "Desktop settings…" row is for folders, BIOS files,
  sign-in and remapping.
- [ ] Commit `docs(app): TV mode screenshots and README for the full TV Settings and Library (#991)`; update the PR #1067 body
  (the Phase 5 decisions as applied, the shots); report CI.

### Phase 5 review focus (added to the list above)

22. **No path still opens a desktop Settings or Library window while TV mode is on**: `grep -n 'showSettings = true\|library.show = \|toggleLibrary()' app/src`
    must list only `App::openSettings` / `openLibrary`, `--show-window`, and `tvMoreSettingsOpen` (Task 21 script lines; by hand
    on the Studio: the native File and Settings menus with TV on, with and without a game).
23. **A hardcore-disallowed core option changed from TV Settings** (`Achievements::checkSettings`) behaves exactly as from the
    desktop, with and without a game (Task 19 `setOptionValue` path unchanged; by hand with `--ra-mock app/tests/ra`, hardcore on,
    change the RISC overclock from the TV Settings: the same refusal / downgrade as the desktop combo).
24. **A core option changed mid-game reaches the core** after the menu closes (Phase 3 review focus 14, re-run against the new
    row location in the script) and **one changed with no game reaches the next game** (Task 20 script: `opt.*` in the config,
    then `load` → `expect core-option=…:2x`).
25. **Launching from the in-game Library never loses the running game's state without the confirm**: `expect loaded=1` after the
    first A; `openContent`'s auto-save runs on Yes (`app.cpp:1171`); the Phase 1 release guard masks the Yes press from the new
    game (`guardArmed == 1`).
26. **A typed / numeric option is usable on a pad**: a held Right steps every 80 ms and stops at the end (clamp); a "(custom)"
    value is shown, kept until stepped, and never written back as a non-preset (`settings_custom_value_row`,
    `settings_held_right_repeats`).
27. **The startup probe must not change what a game sees**: `startSession` re-pushes the table (`core_host.cpp:775-795`), the
    probe's `environ_cb` is replaced by the real one before `retro_init`, and no non-option environment command is answered while
    probing (`core_options_probe_test`: `isLoaded()` false, `optionPushes()` counts one). With `--emu-thread on`, the UI reads the
    published table from the first frame (`core_host.cpp:1213`): `tv_menu_headless` runs inline only (`VJTvTests.cmake:89`), so
    add a `tv_menu_headless_threaded` twin with `--emu-thread on` the way `VJAchTests.cmake:41-56` registers `<name>_threaded`,
    or at least run the no-game `core-option` block by hand on the Studio with the thread on.
28. **The rail with 17+ groups on a 720p window**: every group reachable and visible when focused (`settings_rail_scrolls_720p`,
    `settings-core-speed-end.png`).

---

## Self-review notes

- **Spec coverage:**
  - §3.1 → Tasks 3, 5, 6;
  - §3.2 → Task 1;
  - §3.3 → Tasks 1–3;
  - §4 → Tasks 5, 8–10, 12–14;
  - §5 → Task 12;
  - §6 → Task 4, plus every screen task;
  - §7 → the file lists;
  - §8 → Tasks 6, 7, 16;
  - §9 → Tasks 17–18 and the phase order;
  - §10 → the tests in every task, plus screenshots in Tasks 5, 11 and 15;
  - §11 → nothing here builds anything it excludes.
- **Added beyond §7's hook list, because the code needs them:** `tv::Host` and its adapter (no test links
  `app.cpp`); the `remap_ui` row and its "Add hold" button; the cheat-sheet row; `--tv-test-script`; the
  `shaders_ui` exposure; the native `Cmd::TvMode`; `ui.tv_manual_on` and `ui.tv_hint_shown`; `VJ_TV_DEFAULT`.
