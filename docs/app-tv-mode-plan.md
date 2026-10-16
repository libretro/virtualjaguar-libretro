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

## How this plan resolves the spec's open points and gaps

| Spec point | Resolution (found in the code) |
|---|---|
| §12 option keys | Shader: `video.shader.*` through `ShaderChain`. Integer: `present.integerScale` (`video.integer`). Aspect: `present.aspectOverride` (`video.aspect`), the 4-entry table now in `drawVideoTab`. Internal resolution: core option `virtualjaguar_internal_resolution`. RISC overclock: `virtualjaguar_risc_clock_scale`. Idle-loop skip: `virtualjaguar_risc_idle_skip`. Volume: `app.volume` + `applyAudioSettings()`. Show FPS: `app.showFps` (`ui.show_fps`). Vsync: `app.vsync` + `applyVsync()`. Theme: `setThemeChoice(key)`. |
| §12 "Recently added" | `LibraryService` does not expose it; `ScanFile.mtime` is the file's own date, not when the file joined the library. **The shelf is dropped**, as the spec's own fallback rule says. |
| §12 netplay | The menu opens without pausing when `netplay.configured()` (the test the auto-save already uses for "during netplay"), and shows the banner "Netplay: the game keeps running". |
| §4 Library rail "Homebrew" | `lib::Entry` has no homebrew flag. **Dropped**, by the same no-new-index rule. Rail: All games, Favorites, Recently played, Jaguar CD. |
| §4 Home hero "year and publisher", "achievement progress" | `lib::Entry` has neither year nor publisher, and RA progress is not keyed by library entry. The hero shows art, title, region, cartridge/CD, and the number of save states with the newest date. |
| §4 Home "Y: achievements" | Per-game RA rows exist only for the loaded game. With no game running, the Achievements screen shows the RA games list (`ach.games()`, read-only progress); with a game running, it shows that game's rows. |
| §4 "X on any tile" vs Home "X: resume" | Made consistent. **X (West) = resume the newest save state on any tile** (hidden when there is none, or in hardcore). **Start (Menu) = Game details on any tile**; a right-click does the same with a mouse. Y (North) = the letter picker in Library, Achievements on Home. |
| §4 Game details "Per-game settings" | Per-game settings exist only as control overrides for the *loaded* game, and Game details is reached only with no game running. **Row dropped**; the in-game menu's Settings > More settings covers it. |
| §8 "remembered for the next launch only in Manual" | New key `ui.tv_manual_on` (bool), written only while the setting is Manual. |
| §9 nightly = Auto, first release = Manual | CMake option `VJ_TV_DEFAULT` (`auto` by default) becomes the compile definition `VJ_TV_DEFAULT_SETTING`. `release.yml` passes `-DVJ_TV_DEFAULT=manual` on every run that is not the `develop` nightly. |
| §9 one-time hint | New key `ui.tv_hint_shown`. |
| Reset / Close game from a pad | Both ask first: a two-row Yes/No list with focus on No (one A press must not lose progress). |

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
  - `void tickHolds(uint64_t ms);`, called by `inputFrame` every UI frame (it also runs while paused).
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
  - `input.cpp` `handleEvent`:
    - Pad button and key down/up feed `holds_.press/release` with `SDL_GetTicks()`. Do this only when no
      capture is running, and for keys only when `!uiWantsKeyboard` and no Ctrl/Alt/Cmd is held.
    - A plain (non-hold) `C_TVMENU` binding sets `tvMenu_ = true`, exactly as `C_HELP` sets `helpToggle_`.
    - `GAMEPAD_REMOVED` calls `holds_.forget(id)`.
    - `WINDOW_FOCUS_LOST` calls `holds_.clear()`.
  - `reresolve()`/`openPad()` call `holds_.setHoldInputs(dev, <every hold binding in that profile>)`.
  - `poll()` calls `holds_.apply` and then `guard_.apply` on each `HostSource.snap` before `router_.route`, and
    `holds_.frame()` after it.
  - `tickHolds(ms)`: `if (holds_.takeFires(ms)) tvMenu_ = true;`.
  - `inputFrame()` calls `in.tickHolds(SDL_GetTicks())`.
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
```

  **Step 1b**: add CTest `tv_menu_headless`: `virtualjaguar --headless --frames 30 --tv-test-script <file>`, a dev
  flag that replays the script's lines `F:key|pad|focus EVENT` into `App::handleEvent` and asserts
  `menu-open=0/1`. Script cases:
  - F4 opens the menu.
  - F4 while `uiWantsKeyboard` does nothing.
  - F4 during `beginCapture` binds F4 and does not open the menu.
  - A `WINDOW_FOCUS_LOST` mid Share-hold leaves the menu closed after 600 ms.

  Implement the flag in `main.cpp`/`app.cpp` next to `--expect-controls-overlay`, and print `headless: FAIL …`
  on a mismatch.

- [ ] **Step 2: Run** `ctest --test-dir app/build -R "tv_ui_smoke|tv_menu_headless"`. Expected: it fails to build.
- [ ] **Step 3: Implement.**
  - `App::menuPause`/`menuResume`:
    - Pausing calls `ach.allowPause(why)` unless the game is already paused.
    - Under netplay, pausing returns true without pausing.
    - Neither direction shows the "Paused"/"Resumed" toast.
  - `inputFrame`: `if (in.takeTvMenuToggle() && app.host.isLoaded()) app.tvShell().toggleMenu();`.
  - `App::handleEvent`: give `tvShell().handleEvent` the event before `input.handleEvent` while
    `tvShell().ownsInput()`.
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

### Task 6: the mode state machine and input ownership (pure)

**Files:** Create `app/src/tv/tv_mode.h/.cpp`, `app/tests/tv_mode_test.cpp` (register `tv_mode_unit`).

**Interfaces:**

```cpp
namespace vj::tv {
enum class TvSetting { Off, Manual, Auto };
const char* settingKey(TvSetting s);                       // "off" | "manual" | "auto"
bool settingFromKey(const std::string& k, TvSetting& out);
TvSetting defaultSetting();                                // VJ_TV_DEFAULT_SETTING (Task 16), Auto when undefined
struct TvConfig { TvSetting setting = TvSetting::Manual; bool autoFullscreen = true, autoController = true; };
class TvMode {
public:
    static constexpr float kMouseOffPx = 32.0f;
    enum class Cause { None, Manual, Fullscreen, Controller };
    void configure(const TvConfig& c, bool fullscreenNow);   // Off -> off; Auto + autoFullscreen + fullscreen -> on
    bool active() const; Cause cause() const;
    void toggle();                           // View > TV mode, the in-game menu; no-op when Off
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

- [ ] **Step 1: Write the failing tests.** Every §8 trigger order:

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
CHECK(inputOwner(false, false, false) == Owner::DesktopUi && inputOwner(false, true, false) == Owner::Game);
CHECK(inputOwner(true, false, false) == Owner::TvUi && inputOwner(true, true, false) == Owner::Game);
CHECK(inputOwner(false, true, true) == Owner::TvUi && inputOwner(true, true, true) == Owner::TvUi);
TvSetting s; CHECK(settingFromKey("auto", s) && s == TvSetting::Auto && !settingFromKey("bogus", s));
```

- [ ] **Step 2: Run** `ctest --test-dir app/build -R tv_mode_unit`. Expected: it fails to build.
- [ ] **Step 3: Implement.** `Shell::ownsInput()` (Task 5) now delegates to `inputOwner(...) == Owner::TvUi`.
- [ ] **Step 4: Run** the same command. Expected: PASS.
- [ ] **Step 5: Commit** `feat(app): TV mode state machine and input ownership (#991)`.

### Task 7: shell chrome, TV mode on and off, `--tv`, View menu

**Files:**
- Modify:
  - `tv_shell.h/.cpp`: screen stack, top bar, hint bar, sections, mouse.
  - `tv_host.h` + `tv_app_host.cpp`: the members listed below.
  - `app.h/.cpp`: `tvMode` member; config load/save.
  - `main.cpp`: `--tv`.
  - `ui.cpp` `drawUi`: TV active → no menu bar except the hover band; no `drawLibraryHome`.
  - `ui.cpp` View menu: "TV mode".
  - `platform/commands.h/.cpp`, `desktop.cpp`: native twin.
  - `settings_reset.cpp` + `tests/settings_reset_test.cpp`: the `ui.tv_*` keys are Interface.
- Test: `tv_ui_smoke.cpp` (extend), `tests/platform_test.cpp` (extend).

**Interfaces:**
- Host gains:

```cpp
virtual bool tvActive() const = 0;
virtual bool tvAvailable() const = 0;   // setting != Off
virtual void setTvActive(bool on) = 0;
virtual bool fullscreen() const = 0;
virtual std::string clockText() const = 0;
virtual std::string raStatus() const = 0;
```

- `enum class Section { Home, Library, Achievements, Settings }`. LB/RB move between sections; the top bar shows
  them, plus the clock and the RA status.
- `CliArgs::tv` (bool). `Cmd::TvMode` goes after `Cmd::HomeLibrary`. `MenuState` gains `tvMode` and `tvAvailable`,
  and `operator==` compares both.
- Config: `ui.tv_mode` and `ui.tv_manual_on` are read in `loadConfig`/`applySettingsFromConfig` and written in
  `saveConfig`. `ui.tv_manual_on` is written only while the setting is Manual.

- [ ] **Step 1: Write the failing tests.**
  - Smoke:
    - `tv_shell_walks_sections`: TV active with no game; RB×3, LB×3; 120 frames with no ImGui asserts.
    - `esc_back_then_root`: Esc on Library → Home and consumed; on Home `backAtRoot() == true`.
    - `mouse_hover_focuses`: a synthetic `MOUSE_MOTION` over tile 2 sets focus to 2; a click launches.
  - `platform_test`:
    - the View menu model has "TV Mode", disabled when `tvAvailable == false`;
    - `Cmd::TvMode` dispatches to `toggleTvMode()`.
  - `settings_reset_test`: `classifyKey("ui.tv_mode") == KeyClass::Interface` for all five `ui.tv_*` keys.
- [ ] **Step 2: Run** `ctest --test-dir app/build -R "tv_ui_smoke|platform|settings_reset"`. Expected: FAIL.
- [ ] **Step 3: Implement.**
  - `drawUi`: when TV is active, the menu bar shows only at the top-edge hover band, as in fullscreen today.
    `Layer::Base` replaces `drawLibraryHome`.
  - The cursor hides while the last TV input came from a pad, and returns on mouse motion.
- [ ] **Step 4: Run** the full suite. Expected: all PASS.
- [ ] **Step 5: Commit** `feat(app): TV shell, Manual TV mode, --tv and the View menu entry (#991)`.

### Task 8: Home

**Files:** Create `app/src/tv/tv_home.cpp`. Extend `tv_host.h`/`tv_app_host.cpp`. Test: `tv_ui_smoke.cpp`.

**Interfaces:** Host gains:

```cpp
virtual const std::vector<lib::Entry>& games() = 0;
virtual const std::vector<lib::Resolved>& favorites() = 0;
virtual const std::vector<lib::Resolved>& recents() = 0;
virtual ArtView art(const lib::Entry& e) = 0;
virtual const lib::StateGame* statesFor(const lib::Entry& e) = 0;
virtual ArtView stateThumb(const lib::StateInfo& s) = 0;
virtual void launch(const lib::Entry& e) = 0;
virtual void launchState(const lib::Entry& e, int slot) = 0;
```

Shelves, in this order: Continue playing (`recents`), Favorites (`favorites`), Jaguar CD (`games()` with
`section == Disc`, by title). Empty shelves are hidden. With every shelf empty, a card reads: "Your library is
empty. Add game folders in desktop mode: View > TV mode turns TV mode off."

The hero shows the focused game: art, title, region, Cartridge/CD, and "N save states, newest <date>". Buttons:
- A: `launch`.
- X: `launchState(e, newest slot)`, only when states exist and `canLoadState()`.
- Y: the Achievements section.
- Start (or a right-click): Game details.

Focus is remembered per shelf (`FocusMemory` key `home/<shelf>`).

- [ ] **Step 1: Write the failing smoke tests.**
  - `home_hides_empty_shelves`
  - `home_resume_hidden_without_states_or_in_hardcore`
  - `home_focus_remembered_per_shelf`: Right×3 on Favorites, Down, Up → index 3.
  - `home_accept_launches_focused`
  - `home_empty_library_card`
  - `home_walk_120_frames_no_asserts`
- [ ] **Step 2: Run** `ctest --test-dir app/build -R tv_ui_smoke`. Expected: FAIL.
- [ ] **Step 3: Implement** `tv_home.cpp` with `LinearFocus` per shelf, `scrollToShow`, and tweens for focus
  and scroll.
- [ ] **Step 4: Run** the same command. Expected: PASS.
- [ ] **Step 5: Commit** `feat(app): TV Home screen with shelves and hero panel (#991)`.

### Task 9: Library

**Files:** Create `app/src/tv/tv_library.cpp`. Test: `tv_ui_smoke.cpp`.

**Interfaces:** consumes the Host members from Task 8, plus `GridFocus`, `jumpInitial` and `firstWithInitial`
from Task 4.

- Rail: All games, Favorites, Recently played, Jaguar CD.
- Grid: 6 columns, titles sorted by `sortByTitle`, and a caption under the focused cover.
- Rail and grid hand focus across with `GridFocus::Left`/Right on the rail.
- LT/RT call `jumpInitial`. Y opens the letter picker (A–Z, 0–9, a 6×6 `GridFocus`).
- Typed letters and digits (key-down scancodes mapped to ASCII; no IME) filter by case-insensitive substring.
  Backspace deletes the last filter character, and goes Back once the filter is empty.

- [ ] **Step 1: Write the failing smoke tests.**
  - `library_rail_grid_handoff`: Left at column 0 → rail; Right → grid at the remembered index.
  - `library_lt_rt_jump_initial`
  - `library_letter_picker_jumps`: Y, pick D → focus on the first D title.
  - `library_typing_filters_and_backspace`: type "do" → 1 tile; Backspace twice → full list; Backspace → Home.
  - `library_walk_120_frames_no_asserts`
- [ ] **Step 2: Run** `ctest --test-dir app/build -R tv_ui_smoke`. Expected: FAIL.
- [ ] **Step 3: Implement** `tv_library.cpp`.
- [ ] **Step 4: Run** the same command. Expected: PASS.
- [ ] **Step 5: Commit** `feat(app): TV Library screen with rail, cover grid and letter jump (#991)`.

### Task 10: Game details, and "Enter / Exit TV mode" in the in-game menu

**Files:** Create `app/src/tv/tv_details.cpp`. Modify `tv_ingame.cpp`. Extend the Host with
`isFavorite(e)` and `setFavorite(e, on)`.

Rows: Play; one "Resume" row per state, with its screenshot (hidden when `!canLoadState()`); Favorite (on/off);
Achievements (the Achievements section). The in-game menu gains "Enter TV mode"/"Exit TV mode", hidden when
`!tvAvailable()`.

- [ ] **Step 1: Write the failing smoke tests.**
  - `details_from_start_on_any_tile`
  - `details_resume_rows_match_states`
  - `details_favorite_toggles`
  - `ingame_tv_toggle_hidden_when_off`
- [ ] **Step 2: Run** `ctest --test-dir app/build -R tv_ui_smoke`. Expected: FAIL.
- [ ] **Step 3: Implement** `tv_details.cpp` and the new in-game menu rows.
- [ ] **Step 4: Run** the full suite. Expected: all PASS.
- [ ] **Step 5: Commit** `feat(app): TV Game details and the TV-mode toggle in the in-game menu (#991)`.

### Task 11: screenshots on the Studio, then open PR 2

- [ ] **Step 1:** On `studio-runner`, with a data dir that has a scanned library (copy one into
  `~/vj-remote/…`), run `virtualjaguar --tv --screenshot <s>.png --screenshot-frame 120` at 1280×720,
  1920×1080 and 3840×2160 (`--pixel-density 2` for the 4K one), for each of Home, Library, the letter picker and
  Game details. Use a `--tv-test-script` to navigate to each screen.
- [ ] **Step 2:** Pull the PNGs and compare them by eye with mockups A and B. Fix any drift before opening the PR.
- [ ] **Step 3: Open PR 2** with `Refs #991` and the screenshots attached.

---

## Phase 3 — Settings, Overlays, Achievements, "More settings" (PR 3)

### Task 12: the settings rows model and the TV Settings screen

**Files:**
- Create `app/src/tv/tv_settings.cpp`.
- Modify `tv_host.h`/`tv_app_host.cpp`.
- Modify `shaders_ui.cpp`: expose `std::vector<std::string> shaderPresetChoices(App&)` (builtin + user folder)
  and `void applyShaderPreset(App&, const std::string&)`, moving them out of the anonymous namespace.
- Modify `ui.cpp`: lift the aspect table into `const AspectChoice kAspectChoices[4]` in `ui_util.h`.
- Test: `tv_ui_smoke.cpp`, plus `tests/tv_settings_rows_test.cpp`, which links the adapter's pure row builder.

**Interfaces:**

```cpp
namespace vj::tv {
struct SettingRow { std::string id, label; std::vector<std::string> choices; int index = 0; bool readOnly = false; std::string note; };
// Ids: shader integer aspect resolution risc_clock idle_skip volume fps vsync theme tv_mode more
virtual std::vector<SettingRow> settingRows() = 0;   // Host
virtual void setSetting(const std::string& id, int index) = 0;
virtual void openMoreSettings() = 0;
// Pure, in tv_app_host.cpp, unit-tested: the row for one core option
SettingRow coreOptionRow(const std::string& id, const std::string& label, const OptionDef* def, const std::string& current,
                         bool visible, bool locked, bool overridden, bool* hidden);
}
```

Rules:
- A core-option row is hidden when `def == nullptr || !visible || locked`. It is `readOnly` with note "Set by
  netplay or --option for this session" when `overridden`.
- A change calls `host.setOptionValue(key, def->values[index].value)`, the same call `drawOption` makes, so
  hardcore's disallowed-option handling is identical.
- A current value outside the list (a typed RISC clock) shows as an extra choice "<value> (custom)".
- The shader row is hidden when `!ShaderChain::builtWithHeaders() || !video.shader.libraryLoaded()`. Its first
  choice is "Off" (`enabled = false`).
- App settings apply through the same members the desktop controls use. `app.saveConfig()` runs after each change.

- [ ] **Step 1: Write the failing tests.**

```cpp
// settings_rows_core_option_rules (Review focus 5)
OptionDef d; d.values = {{"1x", "1x"}, {"2x", "2x"}}; bool hidden = false;
SettingRow r = coreOptionRow("resolution", "Internal resolution", &d, "2x", true, false, false, &hidden);
CHECK(!hidden && r.index == 1 && r.choices.size() == 2 && !r.readOnly);
coreOptionRow("resolution", "x", &d, "2x", false, false, false, &hidden); CHECK(hidden);
coreOptionRow("resolution", "x", &d, "2x", true, true, false, &hidden); CHECK(hidden);
r = coreOptionRow("resolution", "x", &d, "2x", true, false, true, &hidden); CHECK(r.readOnly && !r.note.empty());
r = coreOptionRow("risc_clock", "x", &d, "137%", true, false, false, &hidden); CHECK(r.choices.back() == "137% (custom)" && r.index == 2);
coreOptionRow("resolution", "x", nullptr, "", true, false, false, &hidden); CHECK(hidden);
```

  Smoke tests:
  - `settings_left_right_changes`: Right on the fps row → `host.setSetting("fps", 1)`.
  - `settings_more_opens_desktop`
  - `settings_walk_no_asserts`
- [ ] **Step 2: Run** `ctest --test-dir app/build -R "tv_settings_rows_unit|tv_ui_smoke"`. Expected: FAIL.
- [ ] **Step 3: Implement.**
  - "More settings…" sets `showSettings = true`.
  - While `tvActive && showSettings`:
    - `io.ConfigFlags |= NavEnableGamepad | NavEnableKeyboard`, cleared otherwise;
    - `ImGui_ImplSDL3_SetGamepadMode(ImGui_ImplSDL3_GamepadMode_Manual, handles, n)` with
      `input.padHandles()`, so the filtered pad drives it; AutoFirst comes back when it closes;
    - the window is centred, with `ImGui::PushFont(nullptr, metrics.body)` around `drawSettings`.
  - B (East) closes it when no popup is open and no item is active.
- [ ] **Step 4: Run** the full suite. Expected: all PASS.
- [ ] **Step 5: Commit** `feat(app): TV Settings with curated rows and More settings (#991)`.

### Task 13: Overlays

**Files:** Create `app/src/tv/tv_overlays.cpp`. Extend the Host:

```cpp
struct ToggleRow { std::string id, label; bool on = false, enabled = true; };
virtual std::vector<ToggleRow> overlayRows() = 0;   // "fps", "keypad", "controls", "tool:<DebugTool::id>"
virtual void toggleOverlay(const std::string& id) = 0;
```

The tool rows come from `debug.tools()` that have a `toggle`. The label is the tool's label with the trailing
"..." dropped. Rows are disabled when `!debug.enabled()`, with the note "Debug windows are off (hardcore, or no
game)". Debug windows are not pad-interactive (spec §4).

- [ ] **Step 1: Write the failing smoke tests.**
  - `overlays_toggle_fps`
  - `overlays_tool_rows_from_registry`
  - `overlays_disabled_without_debug`
- [ ] **Step 2: Run** `ctest --test-dir app/build -R tv_ui_smoke`. Expected: FAIL.
- [ ] **Step 3: Implement** `tv_overlays.cpp`.
- [ ] **Step 4: Run** the same command. Expected: PASS.
- [ ] **Step 5: Commit** `feat(app): TV Overlays screen (#991)`.

### Task 14: Achievements

**Files:** Create `app/src/tv/tv_achievements.cpp`. Extend the Host:

```cpp
virtual bool achSignedIn() const = 0;
virtual bool achHardcore() const = 0;
virtual bool achRows(std::vector<ra::AchRow>& rows) = 0;      // the loaded game
virtual const std::vector<ra::GameEntry>& achGames() = 0;     // no game loaded: progress per game
virtual ra::ImageView achBadge(const std::string& url) = 0;   // the image cache achievements_ui.cpp uses
virtual std::vector<std::string> achTrackers() = 0;           // active leaderboards, read-only
```

When not signed in, the screen shows one line: "Sign in from Settings > Achievements in desktop mode".

- [ ] **Step 1: Write the failing smoke tests.**
  - `ach_not_signed_in_line`
  - `ach_rows_for_loaded_game`: locked/unlocked/progress drawn.
  - `ach_games_list_without_game`
  - `ach_walk_no_asserts`
- [ ] **Step 2: Run** `ctest --test-dir app/build -R tv_ui_smoke`. Expected: FAIL.
- [ ] **Step 3: Implement** `tv_achievements.cpp`.
- [ ] **Step 4: Run** the same command. Expected: PASS.
- [ ] **Step 5: Commit** `feat(app): TV Achievements screen (#991)`.

### Task 15: in-game menu rows, screenshots, PR 3

- [ ] **Step 1:** Add Achievements, Overlays and Settings to `tv_ingame.cpp`, in the spec §4 order. Extend the
  smoke test with `ingame_rows_full_order` and run it to PASS.
- [ ] **Step 2:** Take Studio screenshots of Settings, Overlays and Achievements, using the RA mock
  (`--ra-mock app/tests/ra/...`) at the three sizes. Compare them by eye.
- [ ] **Step 3: Commit** `feat(app): Achievements, Overlays and Settings in the TV in-game menu (#991)`, then
  open PR 3 with `Refs #991`.

---

## Phase 4 — auto-switching, the hint, tuning (PR 4)

### Task 16: Auto triggers, Interface settings, build default

**Files:**
- Modify:
  - `app.cpp`:
    - `SDL_EVENT_WINDOW_ENTER/LEAVE_FULLSCREEN` → `tvMode.onFullscreen`;
    - pad button down → `onPadPress(owner == DesktopUi && !io.WantTextInput && !input.capturing())`;
    - mouse motion → `onMouseMove`;
    - a key down that is neither a nav key (`intentForKey == None`) nor a modifier → `onNonNavKey`.
  - `ui.cpp` `drawInterfaceTab`: a "TV mode" section with an Off/Manual/Auto combo and the two Auto
    checkboxes (shown only for Auto).
  - The TV Settings rows for the Auto checkboxes: ids `tv_auto_fs`, `tv_auto_pad`.
  - `app/CMakeLists.txt`: `set(VJ_TV_DEFAULT "auto" CACHE STRING "...")` →
    `target_compile_definitions(virtualjaguar PRIVATE VJ_TV_DEFAULT_SETTING="${VJ_TV_DEFAULT}")`.
  - `.github/workflows/release.yml`: where the app is configured, add `-DVJ_TV_DEFAULT=manual` whenever
    `github.ref != 'refs/heads/develop'`.
- Test: `tv_mode_test.cpp` (`defaultSetting()` under both definitions; compile a second tiny TU with the macro
  set), plus `tv_menu_headless` script cases:
  - fullscreen enter → TV on;
  - pad press with a game running → stays off;
  - a 40 px mouse move after a controller-on → off.
- [ ] **Step 1:** Write the failing cases.
- [ ] **Step 2:** Run `ctest --test-dir app/build -R "tv_mode_unit|tv_menu_headless"`. Expected: FAIL.
- [ ] **Step 3:** Implement.
- [ ] **Step 4:** Run the full suite. Expected: all PASS.
- [ ] **Step 5: Commit** `feat(app): TV mode auto-switching on fullscreen and controller use (#991)`.

### Task 17: first-use hint, tuning, PR 4

- [ ] **Step 1:** The first pad press in the desktop library while the setting is Manual and
  `ui.tv_hint_shown` is false shows the toast "Press Share (hold) or F4 for TV mode" and sets the key. The
  glyph text comes from the pad's actual `C_TVMENU` binding. Add the smoke test `hint_once`.
- [ ] **Step 2:** By hand on a real TV (Xbox, DualSense, Switch Pro, an 8BitDo; one machine where Steam takes
  Guide): tune `kRepeatDelayMs`, `kRepeatMs`, `kTweenSeconds` and `focusScale` within the spec's ranges, and
  record the final values and the pads tried in the PR body.
- [ ] **Step 3:** Update `app/README.md`:
  - a "TV mode" section with the defaults, the bindings and the hold rule;
  - the keypad-chord limitation;
  - `--tv`.
- [ ] **Step 4: Commit** `feat(app): TV mode first-use hint and tuned motion (#991)`, then open PR 4 with
  `Closes #991`.

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
  - §9 → Tasks 16–17 and the phase order;
  - §10 → the tests in every task, plus screenshots in Tasks 5, 11 and 15;
  - §11 → nothing here builds anything it excludes.
- **Added beyond §7's hook list, because the code needs them:** `tv::Host` and its adapter (no test links
  `app.cpp`); the `remap_ui` row and its "Add hold" button; the cheat-sheet row; `--tv-test-script`; the
  `shaders_ui` exposure; the native `Cmd::TvMode`; `ui.tv_manual_on` and `ui.tv_hint_shown`; `VJ_TV_DEFAULT`.
