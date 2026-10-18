# Emulation thread, Phase 2 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (- [ ]) syntax for tracking.

**Goal:** With Settings > Interface > Emulation thread on, RetroAchievements and netplay work again, and a `call()` that blocks the UI for more than 250 ms shows a spinner instead of a frozen window. Underneath, `EmuSnapshot` moves to its own file and gains a core-event stream that carries the core's log lines, its `vj_event`s and the owner's option writes to the UI thread in order. The cheat batch becomes a regular command. The open items from Phase 1 are closed. Only the debugger tools and the GDB stub stay off while threaded (Phase 3).

**Architecture:** Each piece of state keeps one owner thread.
- **Core events.** The emulation thread queues what the core says (log lines, `vj_event`s) and the option writes it makes inside a `call()` into a `CoreEventQueue`. `CoreHost::pinSnapshot()` drains the queue on the UI thread, applies the option writes to the UI's mirror and `config.ini`, and hands the rest to `CoreHost::onUiEvent`.
- **Netplay.** Netplay stays a UI-thread object. It reads the core through that stream, drops events from before its own last start/stop (an epoch that rides the command queue), and completes its endpoint bounce by counting emulated frames.
- **RetroAchievements.**
  - Emulation thread: `rc_client_do_frame` (which also runs `rc_client_idle`) after each frame, and the rich-presence string, published every 60 frames.
  - UI thread: the HTTP pump, the queued rc_client events (copied into `AchEvent`s by the handler) and every UI-state field.
  - Core RAM is read only on the emulation thread. Elsewhere `read_memory` answers from the memory map without touching RAM.
- **Busy spinner.** `EmuThread::call()` runs a wait hook on the waiting thread once it has waited 250 ms. In the app the hook pumps OS events and swaps a spinner frame. It touches no ImGui and no `call()`-guarded field.

**Tech Stack:** C++17 (app/), SDL3, Dear ImGui, rcheevos v12.5.0 (rc_client), CMake/CTest, the statically linked C89 core (unchanged), ThreadSanitizer, GitHub Actions.

**Spec:** docs/app-emulation-thread-design.md (sections 3, 5, 6 and 7, and build-order step 2). Phase 1's plan, docs/app-emulation-thread-plan-phase1.md, has the boundary table this phase extends ("Phase 1 boundary") and the Phase 2 follow-up text this plan implements ("Follow-up plans").

## Global Constraints

- C++17 in `app/`. The core stays single-threaded C89 and is not touched: no file under `src/` or `libretro*.c` changes.
- Only the emulation thread calls into the core: every `retro_*` call, `vj_debug_iface`, and every read or write of core memory. This includes rc_client's `read_memory` callback and the `retro_cheat_*` calls in `achievements.cpp`.
- App CMake builds must NOT set `DEVELOPER_DIR`. The `DEVELOPER_DIR=/Library/Developer/CommandLineTools` prefix is for the core's Makefile build only.
- Never launch the app with a window on the maintainer's Mac. Windowed checks run on the Mac Studio only (`vj-eval/remote/vjr.sh`, ssh alias `studio-runner`; see the Studio notes in Task 11). Headless runs are fine anywhere.
- The setting stays Off by default in every build through Phase 2. Phase 2 adds no build-channel flag; nightlies default it On in Phase 3.
- Every existing test keeps passing unchanged in inline mode: headless runs stay single-threaded unless `--emu-thread on` is given. The new threaded twins are additions.
- Conventional commits (`feat(app):`, `fix(app):`, `test(app):`, `ci(app):`, `docs(app):`), each message ending with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- Commits are signed through 1Password. If signing fails with a 1Password error, wait and retry. Never disable signing.
- Branch off `libretro/develop`, never `master`. The PR targets `develop` with `Refs #967` in the body.
- Delete with `trash`, or `command rm -f`. Never use a bare `rm`, `cp` or `mv`: the shell aliases them to `-i`, and with no TTY they hang.

## Review Focus

These are the six failure modes most likely to slip past the per-task tests. Each one is pinned by the test named.

1. **A queued core event applied after the state it belonged to has gone.** For example, a LINK_DOWN from the previous netplay session arrives after `start()`, or a log line from the previous game arrives after a load. Pinned by `netplay_unit` "events from before the last start are dropped" (Task 3) and `core_host_thread` "a reload drops queued log lines but keeps the option writes" (Task 2).
2. **rc_client reading core RAM off the emulation thread.** Rich presence, the ping that `rc_client_idle` schedules, and the address validation of a game load completed in the UI's HTTP pump all read RAM. Pinned by the `ra_*_threaded` twins under the TSan job (Task 6) and by `ra_offthread_validation_threaded`, which requires the "answered from the memory map" log line (Task 6).
3. **The spinner's wait hook running UI code while a `call()` holds the core.** The hook must not read a field that only a `call()` writes, must not open an ImGui frame, and must not re-enter `call()`. Pinned by `emu_thread_unit` "the wait hook runs on the caller after 250 ms, never re-entered, never for a fast call" (Task 7), `core_host_thread` "call()-guarded readers count a violation while the UI waits" (Task 7), and `smoke_busy_threaded` (Task 7).
4. **A LINK_UP or an option write lost under a burst of log lines.** Pinned by `core_events_unit` "log lines are bounded, events and option writes never dropped" (Task 1) and by the `netplay_pair*_threaded` twins, which fail on "the core sent no LINK_UP event" (Task 3).
5. **RetroAchievements steps landing on a different frame threaded than inline.** Examples: a poke at frame 60, sign-in at 40, hardcore on at 31. Pinned by the `ra_*_threaded` twins, which run the same frame-numbered steps and expectations as their inline originals (Task 6).
6. **Quitting with link play reachable.** The core waits on its peer inside `retro_run`, so the 3 s join, then `_Exit(3)`, could fire. Pinned by `netplay_no_partner_threaded`: both children must exit with code 1, never 3 (Task 3). The jlink wait is bounded per frame (`src/jerry/jlink.c`: budget = period - emulation - margin), so a join is never held for more than about a frame.

## Phase 1 open items folded in

| Item | Where it stands | This plan |
|---|---|---|
| Frame-evenness steering (session "Explain and fix threaded frame-evenness regression", branch `claude/admiring-diffie-2a3550` / `fix/967-emu-thread-evenness`) | Became its own PR, #1018, merged into develop as `1b18d491` (`PacingClock::steer`, `--pace-steer off`) | Nothing to merge. Task 11 re-measures evenness with RetroAchievements active, because `rc_client_do_frame` adds to the cost of each frame on the emulation thread. Steering is driven by how widely frame end times spread, and that cost widens the spread. |
| MinGW fast-forward timer note (`emu_thread_unit`, commits `c7df9b34` + `3e00a86e`) | #1000 merged before the MinGW x64 job of its final commit ran; that job was still queued on 2026-10-17. ctest prints the test's `note:` lines only on failure, so a passing run shows no verdict either way. Before the fixes the runner measured fast-forward at 63.4 vs 96.6 frames/s, one frame per ~15.6 ms. | Task 9: high-resolution waitable timers for the emulation thread's short waits on Windows (`precise_sleep.h`), and a CI step that prints `emu_thread_unit`'s timer notes on every app job. |
| Final review Minor 2: `limitFeaturesForEmuThread()` runs before `startEmuThread()`, so a thread that fails to start leaves an inline run with the features off and no "Restart" hint | Open (`app.cpp` `run()` vs `startEmuThread()`) | Task 8: a failed start lifts the limits and falls back cleanly. |
| Final review Minor 3: threaded with vsync off naps 4 ms per iteration, presenting at ~250 Hz | Open (`app.cpp` threaded sleep block) | Task 8: nap to the display's next refresh. |
| Final review Minors 1, 4, 5, 6 (FAIL regex on the limit tests, the cap on late-start exemptions, the `kLockAfter` comment, the abandon comment) | Fixed on develop (`late_latch.h` `kMaxLateExempt`, `emu_thread.h:77-78`, `VJEmuThreadTests.cmake`) | Nothing |
| Final review "can wait" items: stale `hasRead_`; no test of concurrent `clear()`; `<utility>` include; `nextDue` non-const; stop()/`~EmuThread` semantics; no exception guard; `advance()` from two callers; `clamp16` with summed motion; test margins; `checkOverrides` read off the owner; the per-pin `DiscInfo` copy; `SDL_DelayPrecise` CPU cost; test nits | Each checked against the final code and ruled harmless | Out of scope. None of them blocks a Phase 2 task. The owner of the code they sit in may fix one in passing; this plan does not ask for it. |

## Resolved spec ambiguities

| Spec says | Phase 2 does | Why |
|---|---|---|
| "`Netplay::afterFrame` runs per emulated frame on the emulation thread; its UI-visible state goes through the snapshot" (section 6) | Netplay stays a UI-thread object. The core's link log lines and events reach it through the core-event stream. `afterFrame()` runs on the UI thread once per UI iteration, and completes the endpoint bounce by counting emulated frames (`CoreHost::frameCount()`), not by being called per frame. | `afterFrame()`'s only job is the bounce: after the mode was "disabled" for 2 frames, apply the new endpoint. That apply calls `CoreHost::overrideOption` / `setOptionValue`. On the emulation thread those take the owner branch, which writes the UI's option mirror and `config.ini` off the UI thread: the race spec section 5 forbids. Netplay's state is also written by `start()`/`stop()` from the window, so splitting it across threads would need a lock on every field. Counting frames keeps the old guarantee: the core reads "disabled" for at least 2 emulated frames (3 when threaded, one extra for a frame already running when the post was queued). |
| `onOptionChanged` and netplay's `onCoreLog`/`onEvent` "become snapshot events applied on the main thread" (section 5) | `CoreEventQueue` (in `emu_snapshot.h`) carries log lines, `vj_event`s and the owner's option writes. `pinSnapshot()` applies them on the UI thread. `CoreHost::onLog` / `onEvent` stay as they are: synchronous, on the core's thread. | `onLog` has synchronous users: the headless `--disc-insert` check swaps it inside a `call()` and reads the result as soon as the call returns. The UI-side channel is a separate callback, `onUiEvent`. |
| "Achievement events go to the main thread through the snapshot" (section 5) | `Achievements` keeps its own queue of `AchEvent`s (mutex), filled by the rc_client event handler on whichever thread raised the event, and drained in `pump()` on the UI thread. | rc_client raises events on the emulation thread (`do_frame`) and on the UI thread (completions of HTTP calls run in `pump()`, and `rc_client_set_hardcore_enabled`). One queue, drained in one place, serves both. |
| "`rc_client_do_frame` runs on the emulation thread after each frame; the HTTP worker is unchanged" (section 5) | `do_frame` runs on the emulation thread, and so does `rc_client_idle`: `do_frame` calls it at its end, and `pump()` posts it every second while the game is paused or not identified. The rich-presence message is computed there every 60 frames and published. Off the emulation thread, `read_memory` answers from the memory map with zeros and never touches RAM. | rc_client evaluates rich presence through `read_memory`, both in `rc_client_get_rich_presence_message` and in the ping that `rc_client_idle` schedules (rc_client.c 5724-5760). Its override hook is internal-only, not public API. A game load completed in the UI's HTTP pump validates every memref address with a 1-byte read (`rc_client_validate_addresses`, rc_client.c:1346); only the count of bytes returned matters there, so answering from the map keeps validation correct. |
| "Slow calls: after 250 ms the UI shows a spinner instead of freezing" (section 7) | `EmuThread::setWaitHook`. The app's hook pumps OS events (queued, not dispatched) and swaps a spinner frame drawn with scissored clears. No ImGui, no `call()`, no latch or perf accounting. While it runs, `CoreHost` counts any read of a `call()`-guarded field as a violation. Headless, it logs once per call. | Most slow calls start from a menu handler, in the middle of an ImGui frame, so the hook cannot open another. What it can safely do is keep the window from being marked "not responding" and show that the app is busy. The game picture is replaced by the spinner for those frames. |
| Content switch: "`FrameExchange` and `EmuSnapshot` are cleared on load" (section 7) | `CoreHost::startSession()` drops queued log lines and events (`CoreEventQueue::dropLogsAndEvents`) and keeps queued option writes. | An option write is the user's choice, made before the load. It must still reach `config.ini`. |
| Phase 1 plan: "the cheat batch from Phase 1 becomes a regular command" | `CheatManager` builds one `CheatBatch` (reset first, then every set) per `apply()` and hands it to `commit`. The app's `commit` is `emu.post(...)`. `resetSink` / `setSink` / `doneSink` and the file-scope `g_batch` go away. | `post()` already runs inline when not threaded or when on the emulation thread, so one path serves all three cases. |

## File Structure

| File | Status | Responsibility |
|---|---|---|
| `app/src/emu_snapshot.h` | new | `OptionValue`, `OptionDef`, `OptionCategory`, `OsdMessage`, `DiscInfo`, `OptionTable`, `EmuSnapshot` (moved from `core_host.h`); `CoreEvent`, `CoreEventQueue` (new) |
| `app/src/core_host.h`, `app/src/core_host.cpp` | modify | Include `emu_snapshot.h`. `onUiEvent`, `eventEpochBump()`, `eventEpoch()`, the queue, its drain in `pinSnapshot()`, owner-branch option writes as events, `setUiWaiting()` and the waiting-read guard |
| `app/src/netplay.h`, `app/src/netplay.cpp` | modify | `onUiEvent(const CoreEvent&)`, the epoch filter, the frame-counted bounce |
| `app/src/netplay_ui.cpp` | modify | Netplay back on while threaded: the taps through `onUiEvent`, a threaded headless check |
| `app/src/cheats.h`, `app/src/cheats.cpp`, `app/src/cheats_core.cpp` | modify | `CheatOp`, `CheatBatch`, `commit` |
| `app/src/ach_event.h`, `app/src/ach_event.cpp` | new | `AchEvent`, an owned copy of an `rc_client_event_t`; `achEventFrom()` |
| `app/src/achievements.h`, `app/src/achievements.cpp`, `app/src/achievements_ui.cpp` | modify | Event queue, `emuFrame()` / `emuIdle()`, published rich presence, the off-thread `read_memory` answer, core calls through `coreCall` / `corePost`, the mock lock, threaded `pump()` |
| `app/src/emu_thread.h`, `app/src/emu_thread.cpp` | modify | `setWaitHook`, `kBusyAfter` |
| `app/src/precise_sleep.h` | new | `preciseSleepNs()`: a high-resolution waitable timer on Windows, `sleep_for` elsewhere |
| `app/src/emu_commands.h` | modify | The MinGW short wait polls with `preciseSleepNs` |
| `app/src/late_latch.h` | modify | `LateLatch::idleNapNs()` |
| `app/src/video.h`, `app/src/video.cpp` | modify | `VideoOut::drawBusy()` |
| `app/src/app.h`, `app/src/app.cpp` | modify | Achievements on while threaded, `ach.emuFrame()` per frame, the threaded RA and netplay headless paths, `busyFrame()`, limits lifted after a failed start, the vsync-off nap, the slow-call test hook |
| `app/cmake/VJEmuThreadTests.cmake`, `app/cmake/VJAchTests.cmake`, `app/cmake/VJNetplayTests.cmake` | modify | New unit tests and threaded twins |
| `app/tests/core_events_test.cpp`, `app/tests/ach_event_test.cpp` | new | Unit tests |
| `app/tests/core_host_thread_test.cpp`, `app/tests/emu_thread_test.cpp`, `app/tests/netplay_test.cpp`, `app/tests/cheats_test.cpp`, `app/tests/late_latch_test.cpp` | modify | New checks |
| `.github/workflows/app.yml` | modify | Timer report step |
| `app/README.md`, `docs/agent/testing.md`, `docs/app-emulation-thread-design.md` | modify | Docs |

## Before Task 1: the worktree

- [ ] **Create the implementation worktree off `libretro/develop`** (or reuse the plan's worktree on a new branch)

```bash
cd ~/Workspace/Provenance/virtualjaguar-libretro
git fetch libretro
git worktree add ~/Workspace/Provenance/vj-wt/emu-thread-p2impl -b feat/967-emu-thread-phase2 libretro/develop
cd ~/Workspace/Provenance/vj-wt/emu-thread-p2impl
ln -sfn "${JAGUAR_ROMS_PRIVATE:-$HOME/Workspace/Provenance/jaguar-roms-private}" test/roms/private
cmake -S app -B app/build -DCMAKE_BUILD_TYPE=Release
cmake --build app/build -j8
ctest --test-dir app/build -C Release -E shaders_gl --output-on-failure -j4
```

Expected: the build succeeds and every test passes. This is the baseline. Record any test that already fails here, and leave it alone. Under heavy host load (other sessions), the `netplay_pair*` tests can fail with "no link within 30s". If they do, re-run them alone before counting them as failures.

All later commands run from the implementation worktree. #1012 (.m3u playlists) is open and touches `app.cpp`, `core_host.*` and `VJEmuThreadTests.cmake`. If it merges first, merge develop in before Task 2 and keep both sides.

---

### Task 1: `EmuSnapshot` in its own file, and the core-event stream

**Files:**
- Create: `app/src/emu_snapshot.h`, `app/tests/core_events_test.cpp`
- Modify: `app/src/core_host.h` (move `OptionValue`, `OptionDef`, `OptionCategory`, `OsdMessage`, `DiscInfo`, `OptionTable`, `EmuSnapshot` out; `#include "emu_snapshot.h"`), `app/cmake/VJEmuThreadTests.cmake` (header list + new test)

**Interfaces:**
- Produces: `struct vj::CoreEvent` and `class vj::CoreEventQueue`, exactly as below. Tasks 2 and 3 use `CoreEvent::Kind::{Log, Event, UserOption, SessionOption}`, the fields `epoch, level, type, a, b, c, d, text, value, erased`, and `push`, `take`, `dropLogsAndEvents`, `droppedLogs`.

- [ ] **Step 1: Write the failing test** `app/tests/core_events_test.cpp`:

```cpp
// CoreEventQueue (emu_snapshot.h): the core-event stream from the emulation thread to the UI thread (#967 phase 2).
// No core, no SDL.
//
//   vj_core_events_test
#include "emu_snapshot.h"

#include <atomic>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

using vj::CoreEvent;
using vj::CoreEventQueue;

namespace {

int g_fail = 0;
#define CHECK(c, ...)                                        \
    do {                                                     \
        if (!(c)) {                                          \
            std::printf("FAIL %s:%d: ", __FILE__, __LINE__); \
            std::printf(__VA_ARGS__);                        \
            std::printf("\n");                               \
            g_fail++;                                        \
        }                                                    \
    } while (0)

CoreEvent logLine(const std::string& t) {
    CoreEvent e;
    e.kind = CoreEvent::Kind::Log;
    e.text = t;
    return e;
}
CoreEvent event(unsigned type, unsigned a) {
    CoreEvent e;
    e.kind = CoreEvent::Kind::Event;
    e.type = type;
    e.a = a;
    return e;
}
CoreEvent option(const std::string& k, const std::string& v) {
    CoreEvent e;
    e.kind = CoreEvent::Kind::UserOption;
    e.text = k;
    e.value = v;
    return e;
}

void testOrder() {
    CoreEventQueue q;
    q.push(logLine("one"));
    q.push(event(1, 7));
    q.push(option("k", "v"));
    q.push(logLine("two"));
    std::vector<CoreEvent> got = q.take();
    CHECK(got.size() == 4, "all four come out (%zu)", got.size());
    if (got.size() == 4) {
        CHECK(got[0].text == "one" && got[1].type == 1 && got[1].a == 7 && got[2].text == "k" && got[2].value == "v" &&
                  got[3].text == "two",
              "in the order they were pushed");
    }
    CHECK(q.take().empty(), "take() empties the queue");
}

// A burst of log lines (a chatty core, a UI that stalls) must never cost an event or an option write.
void testBoundedLogs() {
    CoreEventQueue q;
    for (int i = 0; i < 300; i++) q.push(logLine("a" + std::to_string(i)));
    q.push(event(1, 1));   // LINK_UP in the middle of the burst
    q.push(option("k", "v"));
    for (int i = 0; i < 300; i++) q.push(logLine("b" + std::to_string(i)));
    std::vector<CoreEvent> got = q.take();
    size_t logs = 0, events = 0, options = 0;
    for (const CoreEvent& e : got) {
        if (e.kind == CoreEvent::Kind::Log) logs++;
        else if (e.kind == CoreEvent::Kind::Event) events++;
        else options++;
    }
    CHECK(logs == CoreEventQueue::kMaxLogs, "logs capped at %zu (%zu)", CoreEventQueue::kMaxLogs, logs);
    CHECK(events == 1 && options == 1, "the event and the option write survive (%zu, %zu)", events, options);
    CHECK(q.droppedLogs() == 600 - CoreEventQueue::kMaxLogs, "the oldest lines were dropped (%zu)", q.droppedLogs());
    CHECK(!got.empty() && got.back().text == "b299", "the newest line is kept");
    bool oldestGone = true;
    for (const CoreEvent& e : got)
        if (e.text == "a0") oldestGone = false;
    CHECK(oldestGone, "the oldest line is the one dropped");
}

void testContentSwitch() {
    CoreEventQueue q;
    q.push(logLine("old game"));
    q.push(event(2, 0));
    q.push(option("k", "v"));
    q.dropLogsAndEvents();
    std::vector<CoreEvent> got = q.take();
    CHECK(got.size() == 1 && got[0].kind == CoreEvent::Kind::UserOption, "a content switch keeps only option writes");
}

// One producer (the emulation thread), one consumer (the UI thread): nothing lost, nothing reordered.
void testTwoThreads() {
    CoreEventQueue q;
    const unsigned kN = 20000;
    std::thread producer([&] {
        for (unsigned i = 0; i < kN; i++) q.push(event(1, i));
    });
    unsigned next = 0;
    bool ordered = true;
    for (int spins = 0; next < kN && spins < 2000000; spins++) {
        for (const CoreEvent& e : q.take()) {
            if (e.a != next) ordered = false;
            next++;
        }
    }
    producer.join();
    for (const CoreEvent& e : q.take()) {
        if (e.a != next) ordered = false;
        next++;
    }
    CHECK(next == kN, "every event arrived (%u of %u)", next, kN);
    CHECK(ordered, "in order");
}

}  // namespace

int main() {
    testOrder();
    testBoundedLogs();
    testContentSwitch();
    testTwoThreads();
    std::printf("core_events_test: %s\n", g_fail ? "FAILED" : "OK");
    return g_fail ? 1 : 0;
}
```

In `app/cmake/VJEmuThreadTests.cmake`, add `#   core_events_unit      CoreEventQueue: order, bounded log lines, content switch, two threads` to the header list, and append the following block. Use the existing `vj_emu_thread_warnings` helper, the same way `vj_frame_exchange_test` uses it.

```cmake
add_executable(vj_core_events_test tests/core_events_test.cpp)
target_include_directories(vj_core_events_test PRIVATE src $<TARGET_PROPERTY:vj_core,INTERFACE_INCLUDE_DIRECTORIES>)
target_link_libraries(vj_core_events_test PRIVATE Threads::Threads)
vj_emu_thread_warnings(vj_core_events_test)
add_test(NAME core_events_unit COMMAND vj_core_events_test)
set_tests_properties(core_events_unit PROPERTIES TIMEOUT 60 LABELS emu_thread FAIL_REGULAR_EXPRESSION "FAIL ")
```

- [ ] **Step 2: Run it and watch it fail**

Run: `cmake -S app -B app/build >/dev/null && cmake --build app/build --target vj_core_events_test 2>&1 | tail -3`
Expected: the build fails because `emu_snapshot.h` does not exist.

- [ ] **Step 3: Create `app/src/emu_snapshot.h`.** Move the seven structs verbatim from `core_host.h`: `OptionValue` (the one above `OptionDef`), `OptionDef`, `OptionCategory`, `OsdMessage`, `DiscInfo`, `OptionTable` and `EmuSnapshot`, with their comments. Replace `EmuSnapshot`'s comment with the one below. `core_host.h` includes the new header in their place. Add `CoreEvent` and `CoreEventQueue`:

```cpp
// The state the emulation thread publishes for the UI thread (#967, docs/app-emulation-thread-design.md section 3):
// the per-frame snapshot CoreHost pins once per UI iteration, and the core-event stream -- what the core said inside
// retro_run() and the option writes made on the emulation thread -- which CoreHost::pinSnapshot() applies on the UI
// thread, in order.
#pragma once

#include "libretro.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace vj {

// ... OptionValue, OptionDef, OptionCategory, OsdMessage, DiscInfo, OptionTable (moved verbatim) ...

// The per-frame state the emulation thread publishes for the UI thread (CoreHost::publishSnapshot / pinSnapshot).
struct EmuSnapshot {
    // ... the fields, verbatim ...
};

// One thing the core said, or one option write made on the emulation thread, on its way to the UI thread.
struct CoreEvent {
    enum class Kind : uint8_t {
        Log,             // a line the core logged: level, text
        Event,           // a vj_event (vj_frontend_ext.h): type, a..d, text = its string (copied; the core's is call-scoped)
        UserOption,      // a saved option value written on the owner: text = key, value, erased = reset to default
        SessionOption,   // a session override set or cleared on the owner: text = key, value, erased = cleared
    };
    Kind kind = Kind::Log;
    uint32_t epoch = 0;              // CoreHost::eventEpochBump() in force on the emulation thread when it was queued
    int level = RETRO_LOG_INFO;      // Log
    unsigned type = 0, a = 0, b = 0, c = 0, d = 0;   // Event
    std::string text;
    std::string value;
    bool erased = false;
};

// Multi-producer, single-consumer, in order.  Log lines are bounded (the oldest go first); events and option writes
// are never dropped (a lost LINK_UP fails a netplay session, a lost option write loses the user's choice).
class CoreEventQueue {
public:
    static constexpr size_t kMaxLogs = 512;

    void push(CoreEvent e) {
        std::lock_guard<std::mutex> l(mu_);
        if (e.kind == CoreEvent::Kind::Log && logs_ >= kMaxLogs) {
            for (auto it = q_.begin(); it != q_.end(); ++it) {
                if (it->kind == CoreEvent::Kind::Log) {
                    q_.erase(it);
                    logs_--;
                    dropped_++;
                    break;
                }
            }
        }
        if (e.kind == CoreEvent::Kind::Log) logs_++;
        q_.push_back(std::move(e));
    }
    std::vector<CoreEvent> take() {
        std::lock_guard<std::mutex> l(mu_);
        std::vector<CoreEvent> out(std::make_move_iterator(q_.begin()), std::make_move_iterator(q_.end()));
        q_.clear();
        logs_ = 0;
        return out;
    }
    // A content switch: what the previous game said is stale; the user's option writes are not.
    void dropLogsAndEvents() {
        std::lock_guard<std::mutex> l(mu_);
        std::deque<CoreEvent> keep;
        for (CoreEvent& e : q_)
            if (e.kind == CoreEvent::Kind::UserOption || e.kind == CoreEvent::Kind::SessionOption) keep.push_back(std::move(e));
        q_.swap(keep);
        logs_ = 0;
    }
    size_t droppedLogs() const {
        std::lock_guard<std::mutex> l(mu_);
        return dropped_;
    }

private:
    mutable std::mutex mu_;
    std::deque<CoreEvent> q_;
    size_t logs_ = 0, dropped_ = 0;
};

}  // namespace vj
```

`core_host.h` keeps `ControllerType` and everything else, and drops the moved structs. Any file that used them through `core_host.h` still compiles, because it includes `emu_snapshot.h`. Add `#include <iterator>` for `std::make_move_iterator`.

- [ ] **Step 4: Run the tests**

Run: `cmake --build app/build -j8 2>&1 | tail -3 && ctest --test-dir app/build -C Release -R "core_events_unit|core_host_thread|emu_thread_e2e|smoke_yarc" --output-on-failure`
Expected: all pass. `core_events_test: OK`.

- [ ] **Step 5: Commit**

```bash
git add app/src/emu_snapshot.h app/src/core_host.h app/tests/core_events_test.cpp app/cmake/VJEmuThreadTests.cmake
git commit -F - <<'EOF'
feat(app): EmuSnapshot in its own file, with the core-event stream (#967)

CoreEventQueue carries log lines, vj_events and option writes from the emulation thread to the UI thread in order;
log lines are bounded, events and option writes never dropped.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
```

### Task 2: CoreHost delivers core events and owner option writes on the UI thread

**Files:**
- Modify: `app/src/core_host.h` (public: `onUiEvent`, `eventEpochBump`, `eventEpoch`; private: `uiEvents_`, `epoch_`, `uiEpoch_`, `emitUi`, `applyUiEvent`), `app/src/core_host.cpp` (`logCb`, `env` `VJ_ENV_EVENT`, `setOptionValue`, `resetOptionToDefault`, `overrideOption`, `clearOverride`, `startSession`, `pinSnapshot`), `app/tests/core_host_thread_test.cpp`

**Interfaces:**
- Consumes: `CoreEvent`, `CoreEventQueue` (Task 1).
- Produces:
  - `std::function<void(const CoreEvent&)> CoreHost::onUiEvent`: set before the first load, called only on the UI thread. Threaded, `pinSnapshot()` calls it in order. Inline, it is called synchronously where the core says the thing. Only `Log` and `Event` kinds reach it.
  - `uint32_t CoreHost::eventEpochBump()`, called on the UI thread. It returns a new epoch N. Every log line and event the core emits after the commands queued before this call have run carries epoch >= N.
  - `uint32_t CoreHost::eventEpoch() const`: the owner-side epoch.
  - Behaviour: threaded owner-branch writes in `setOptionValue` / `resetOptionToDefault` / `overrideOption` / `clearOverride` no longer touch `uiUser_` / `uiSession_` / `onOptionChanged` on the emulation thread. They queue an option event, which the next `pinSnapshot()` applies on the UI thread.

- [ ] **Step 1: Write the failing tests.** In `app/tests/core_host_thread_test.cpp`, keep the existing sections. Before the final print, add the sections below. `emu`, `client`, `host`, `kKey`, `changed`, `changedOn`, `uiThread` and `rom` are the test's existing names; reuse the existing threaded rig. If a name differs, adapt it to the file's own.

```cpp
    // ---- phase 2: the core-event stream ----
    {
        std::vector<std::thread::id> logOn;
        std::vector<std::string> lines;
        host.onUiEvent = [&](const CoreEvent& e) {
            if (e.kind != CoreEvent::Kind::Log) return;
            logOn.push_back(std::this_thread::get_id());
            lines.push_back(e.text);
        };
        std::string err;
        CHECK(emu.call([&] { return host.loadContent(rom, err); }));   // the core logs while it loads
        CHECK(lines.empty());                                          // nothing before the pin
        host.pinSnapshot();
        CHECK(!lines.empty());
        bool allUi = true;
        for (std::thread::id t : logOn) allUi = allUi && t == uiThread;
        CHECK(allUi);                                                  // never on the emulation thread
        host.onUiEvent = nullptr;
    }
    {
        // An option write made on the owner (inside a call) reaches the mirror and the config on the UI thread, at
        // the next pin -- never on the emulation thread.
        changed.clear();
        changedOn.clear();
        emu.call([&] { host.setOptionValue(kKey, "disabled"); });
        CHECK(changed.empty());
        host.pinSnapshot();
        CHECK(changed.size() == 1 && changed[0] == std::string(kKey) + "=disabled");
        CHECK(changedOn.size() == 1 && changedOn[0] == uiThread);
        CHECK(host.userOptions().count(kKey) && host.userOptions().at(kKey) == "disabled");
        emu.call([&] { host.resetOptionToDefault(kKey); });
        host.pinSnapshot();
        CHECK(changed.size() == 2 && changed[1] == std::string(kKey) + "=<erased>");
        CHECK(host.userOptions().count(kKey) == 0);
        emu.call([&] { host.overrideOption(kKey, "enabled"); });
        host.pinSnapshot();
        CHECK(host.optionOverridden(kKey));
        emu.call([&] { host.clearOverride(kKey); });
        host.pinSnapshot();
        CHECK(!host.optionOverridden(kKey));
    }
    {
        // The epoch rides the command queue: a call queued after the bump sees it.
        const uint32_t e1 = host.eventEpochBump();
        const uint32_t e2 = host.eventEpochBump();
        CHECK(e2 > e1);
        CHECK(emu.call([&] { return host.eventEpoch(); }) == e2);
    }
    {
        // A reload drops what the previous game said but keeps the user's option writes.
        changed.clear();
        std::string err;
        emu.call([&] {
            host.setOptionValue(kKey, "disabled");
            return host.loadContent(rom, err);
        });
        host.pinSnapshot();
        CHECK(changed.size() == 1 && changed[0] == std::string(kKey) + "=disabled");
    }
```

Add `using vj::CoreEvent;` near the top if the file has no `using namespace vj;`. Record `const std::thread::id uiThread = std::this_thread::get_id();` at the start of `main` if the test has no such variable yet. The first block relies on the core logging at least one line (any level) while it loads a cartridge. Check with `--verbose` on a headless yarc run. If a cart load logs nothing, use a call that does, such as a no-content boot (`host.loadNoContent(err)`), and say which in the test's comment.

- [ ] **Step 2: Run it and watch it fail**

Run: `cmake --build app/build --target vj_core_host_thread_test 2>&1 | tail -3`
Expected: compile errors on `onUiEvent`, `eventEpochBump` and `eventEpoch`.

- [ ] **Step 3: Implement in `app/src/core_host.h` / `core_host.cpp`.**

Public, next to `onEvent`:

```cpp
    // What the core says, for the UI thread (#967 phase 2): every log line and every event, in order.  Inline it is
    // called right where the core says it; threaded, pinSnapshot() calls it on the UI thread.  Set before the first
    // load.  onLog / onEvent stay synchronous on the core's thread (the --disc-insert check relies on that).
    std::function<void(const CoreEvent&)> onUiEvent;
    // A new epoch for what the core says from now on (UI thread).  The bump rides the command queue, so every line
    // and event the core emits after the commands queued before it carries an epoch >= the returned value: a
    // consumer that just changed the core's settings drops what came before (Netplay::onUiEvent).
    uint32_t eventEpochBump();
    uint32_t eventEpoch() const { return epoch_; }   // owner side
```

Private: `CoreEventQueue uiEvents_; uint32_t epoch_ = 0; uint32_t uiEpoch_ = 0; void emitUi(CoreEvent e); void applyUiEvent(CoreEvent& e);`.

`emitUi`, called on the core's thread:

```cpp
void CoreHost::emitUi(CoreEvent e) {
    e.epoch = epoch_;
    const bool option = e.kind == CoreEvent::Kind::UserOption || e.kind == CoreEvent::Kind::SessionOption;
    if (threaded() && onOwner()) {
        if (option || onUiEvent) uiEvents_.push(std::move(e));
        return;
    }
    if (!option && onUiEvent) onUiEvent(e);   // inline: the option writes were applied directly by the caller
}
```

`applyUiEvent` runs on the UI thread, from `pinSnapshot()`:

```cpp
void CoreHost::applyUiEvent(CoreEvent& e) {
    switch (e.kind) {
    case CoreEvent::Kind::UserOption:
        if (e.erased) uiUser_.erase(e.text);
        else uiUser_[e.text] = e.value;
        if (onOptionChanged) onOptionChanged(e.text, e.value, e.erased);
        break;
    case CoreEvent::Kind::SessionOption:
        if (e.erased) uiSession_.erase(e.text);
        else uiSession_[e.text] = e.value;
        break;
    default:
        if (onUiEvent) onUiEvent(e);
    }
}
```

`pinSnapshot()`: after the `ui_ = pub_` copy (and outside `snapMu_`), add `for (CoreEvent& e : uiEvents_.take()) applyUiEvent(e);`.

`eventEpochBump()`:

```cpp
uint32_t CoreHost::eventEpochBump() {
    const uint32_t n = ++uiEpoch_;
    if (onOwner()) epoch_ = n;
    else queue([this, n] { epoch_ = n; });
    return n;
}
```

`logCb`: change the early return to `if (!print && !h.onLog && !h.onUiEvent) return;`. After the `onLog` block, add the following. Both taps use the same stripped line, so build `line` once before them.

```cpp
    if (h.onUiEvent) {
        CoreEvent e;
        e.kind = CoreEvent::Kind::Log;
        e.level = (int)level;
        e.text = line;
        h.emitUi(std::move(e));
    }
```

`emitUi` is private and `logCb` lives in an anonymous namespace, so it cannot be made a friend. Add a public `void emitLogLine(int level, const std::string& line)` that builds this event and calls `emitUi`. `logCb` calls `h.emitLogLine((int)level, line)` in place of the block above.

`env` `VJ_ENV_EVENT`: after `if (onEvent) onEvent(*e);` add:

```cpp
        if (onUiEvent) {
            CoreEvent ce;
            ce.kind = CoreEvent::Kind::Event;
            ce.type = e->type;
            ce.a = e->a, ce.b = e->b, ce.c = e->c, ce.d = e->d;
            if (e->s) ce.text = e->s;
            emitUi(std::move(ce));
        }
```

Owner branches. Replace the mirror and callback lines as follows. The off-owner branches stay as they are.

```cpp
// setOptionValue, owner branch:
    if (inFrame_) return;
    if (threaded()) emitUi(optionEvent(CoreEvent::Kind::UserOption, key, value, false));   // the UI applies it at its next pin
    else if (onOptionChanged) onOptionChanged(key, value, false);
    applyUserValue(key, &value);
// resetOptionToDefault, owner branch: the same with (UserOption, key, "", true), then applyUserValue(key, nullptr).
// overrideOption, owner branch: if (threaded()) emitUi(optionEvent(CoreEvent::Kind::SessionOption, key, value, false));
// clearOverride, owner branch:  if (threaded()) emitUi(optionEvent(CoreEvent::Kind::SessionOption, key, "", true));
```

Here `static CoreEvent optionEvent(CoreEvent::Kind k, const std::string& key, const std::string& value, bool erased)` is a file-local helper. Keep each method's existing `applyOverride` / `applyUserValue` call. Update the threading comment block in `core_host.h`: the UI's copy is now written only on the UI thread, by the off-owner branches and by `pinSnapshot()`.

`startSession()`: next to `helloSeen_ = false;`, add `uiEvents_.dropLogsAndEvents();`.

- [ ] **Step 4: Run the tests**

Run: `cmake --build app/build -j8 2>&1 | tail -3 && ctest --test-dir app/build -C Release -L emu_thread --output-on-failure && ctest --test-dir app/build -C Release -R "netplay_unit|network_options|settings_reset|typed_options|smoke_" --output-on-failure`
Expected: all pass. Inline behaviour is unchanged: inline, `onUiEvent` is unset everywhere until Task 3.

- [ ] **Step 5: Commit**

```bash
git add app/src/core_host.h app/src/core_host.cpp app/tests/core_host_thread_test.cpp
git commit -F - <<'EOF'
feat(app): core log lines, events and owner option writes reach the UI thread through the stream (#967)

CoreHost::onUiEvent gets every log line and vj_event on the UI thread (pinSnapshot), with an epoch that rides the
command queue; option writes made on the emulation thread inside a call() are applied to the UI's mirror and the
config there too, never on the emulation thread.  A content switch drops queued lines and events.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
```

### Task 3: Netplay on the event stream, back on while threaded

**Files:**
- Modify: `app/src/netplay.h` (public `onUiEvent`; private `minEpoch_`, `bounceFrom_`, `bounceNeed_`; drop `bounceFrames_`), `app/src/netplay.cpp` (`start`, `stop`/`clearAll`, `applyNow`, `afterFrame`, `onUiEvent`), `app/src/netplay_ui.cpp` (`drawNetplayMenu`, `drawNetplay`, `netplayInit`, `netplayHeadlessCheck`), `app/src/app.cpp` (`threadedFrame`: `netplay.afterFrame()`; `runHeadless`: drop `threaded() ? 0 :`), `app/tests/netplay_test.cpp`, `app/cmake/VJNetplayTests.cmake`, `app/cmake/VJEmuThreadTests.cmake` (remove `netplay_refused_threaded`)

**Interfaces:**
- Consumes: `CoreHost::onUiEvent`, `eventEpochBump()`, `frameCount()`, `threaded()` (Task 2).
- Produces: `void Netplay::onUiEvent(const CoreEvent& e)`. `Netplay::afterFrame()` keeps its name and is now callable at any time on the UI thread: it completes a bounce once enough emulated frames have run.

- [ ] **Step 1: Write the failing tests.**

In `app/tests/netplay_test.cpp`, add the test below to the `unit` mode, before `testCore(argv[2])`. It needs no ROM and uses the inline `CoreHost` singleton the file already uses; take its include lines and `using` names.

```cpp
// #967 phase 2: the app's tap is CoreHost::onUiEvent.  What the core said before the last start()/stop() is stale:
// a LINK_UP from the previous session must not mark the new one up.
static void testEpochFilter() {
    vj::CoreHost& host = vj::CoreHost::instance();
    vj::Netplay np(host);
    np.setAddressViaOption(true);
    vj::NetSettings s;
    s.role = vj::NetRole::Host;
    std::string err;
    CHECK(np.start(s, err));   // nothing loaded: applied at once, with a new epoch
    const uint32_t now = host.eventEpoch();
    vj::CoreEvent up;
    up.kind = vj::CoreEvent::Kind::Event;
    up.type = VJ_EVT_LINK_UP;
    up.text = "tcp_server";
    up.epoch = now - 1;   // said before this start
    np.onUiEvent(up);
    CHECK(np.link() == vj::LinkState::Waiting);
    up.epoch = now;
    np.onUiEvent(up);
    CHECK(np.link() == vj::LinkState::Up);
    CHECK(np.linkUpEvents() == 1);
    vj::CoreEvent line;
    line.kind = vj::CoreEvent::Kind::Log;
    line.text = "[NETLINK] failed to open the port";   // a line with no event: read even with the event channel on
    line.epoch = now;
    np.onUiEvent(line);
    CHECK(!np.notices().empty());
    np.stop();
}
```

The `[NETLINK]` prefix and the "failed to open" text must match what `Netplay::onCoreLog` matches. Copy the exact substring from `netplay.cpp`'s `failed to open` branch.

In `testCore` (the in-process run with a loaded yarc), after the existing frames, add the bounce check. `kModeKey` is `"virtualjaguar_netlink"`. `np`, `host` and the start settings are the function's existing names.

```cpp
    // An endpoint change while content runs: "disabled" for at least 2 emulated frames, then the new endpoint --
    // counted in frames, whenever afterFrame() is called.
    {
        vj::NetSettings s2 = np.settings();
        s2.port = s2.port + 1;
        std::string err2;
        CHECK(np.start(s2, err2));
        CHECK(host.optionValueFor("virtualjaguar_netlink", "") == "disabled");
        np.afterFrame();   // no frame has run yet
        np.afterFrame();
        CHECK(host.optionValueFor("virtualjaguar_netlink", "") == "disabled");
        host.runFrame();
        np.afterFrame();
        CHECK(host.optionValueFor("virtualjaguar_netlink", "") == "disabled");
        host.runFrame();
        np.afterFrame();
        CHECK(host.optionValueFor("virtualjaguar_netlink", "") != "disabled");
    }
```

In `runPair` and `runControl`, take an optional thread mode for each child. The extra arguments are `pair EXE ROM DEVICE [HOST JOIN]` and `control EXE ROM [THREAD]`, where each mode is `on` or `off` and the default is `off`. A child whose mode is `on` gets `--emu-thread on` added to its `appArgs` extras. In `runControl`, when the thread mode is `on`, also require both exit codes to be exactly 1. Exit code 3 means the emulation thread did not stop within 3 s, which is the wedged-quit failure.

```cpp
    if (thread == "on") {
        CHECK(join.code == 1);   // 3 = the emulation thread did not stop within 3 s (a wedged quit)
        CHECK(host.code == 1);
    }
```

Update `main`'s argument parsing to match: `runPair(argv[2], argv[3], argc >= 5 ? argv[4] : "jaglink", argc >= 6 ? argv[5] : "off", argc >= 7 ? argv[6] : "off")` and `runControl(argv[2], argv[3], argc >= 5 ? argv[4] : "off")`.

In `app/cmake/VJNetplayTests.cmake`, after the existing tests, add:

```cmake
# #967 phase 2: the same sessions with the emulation thread on -- both sides, and a mixed pair (link play between a
# threaded and a single-thread copy).  netplay_no_partner_threaded: a copy quitting with no peer must exit 1, not 3.
add_test(NAME netplay_pair_threaded COMMAND vj_netplay_test pair $<TARGET_FILE:virtualjaguar> "${_yarc}" jaglink on on)
add_test(NAME netplay_pair_voicemodem_threaded
    COMMAND vj_netplay_test pair $<TARGET_FILE:virtualjaguar> "${_yarc}" voicemodem on on)
add_test(NAME netplay_pair_mixed COMMAND vj_netplay_test pair $<TARGET_FILE:virtualjaguar> "${_yarc}" jaglink on off)
add_test(NAME netplay_no_partner_threaded COMMAND vj_netplay_test control $<TARGET_FILE:virtualjaguar> "${_yarc}" on)
set_tests_properties(netplay_pair_threaded netplay_pair_voicemodem_threaded netplay_pair_mixed netplay_no_partner_threaded
    PROPERTIES TIMEOUT 300 RUN_SERIAL TRUE LABELS "emu_thread")
set_tests_properties(netplay_pair_threaded netplay_pair_voicemodem_threaded netplay_pair_mixed
    PROPERTIES FAIL_REGULAR_EXPRESSION "FAIL |owner violation|ThreadSanitizer")
set_tests_properties(netplay_no_partner_threaded PROPERTIES FAIL_REGULAR_EXPRESSION "owner violation|ThreadSanitizer")
```

In `app/cmake/VJEmuThreadTests.cmake`, delete `netplay_refused_threaded`, its properties and its header-list line.

- [ ] **Step 2: Run them and watch them fail**

Run: `cmake --build app/build -j8 2>&1 | tail -3; ctest --test-dir app/build -C Release -R "netplay_unit|netplay_pair_threaded|netplay_no_partner_threaded" --output-on-failure`
Expected: `vj_netplay_test` fails to compile (`onUiEvent`). Once it compiles, the threaded pairs exit 2 with "netplay: not available while the emulation thread is on".

- [ ] **Step 3: Implement.**

`netplay.h`: replace `int bounceFrames_` with `uint64_t bounceFrom_ = 0; unsigned bounceNeed_ = 2; uint32_t minEpoch_ = 0;`. Then add:

```cpp
    // The app's tap (CoreHost::onUiEvent, on the UI thread): drops what the core said before the last start() /
    // stop() / bounce (its epoch), then reads it like onCoreLog / onCoreEvent.
    void onUiEvent(const CoreEvent& e);
```

Rewrite `afterFrame`'s comment: "Call on the UI thread, once per emulated frame or once per UI iteration. It completes an endpoint change once the core has read 'disabled' for long enough, counted in emulated frames." Update the class comment ("Single-threaded: every method runs on the thread that drives CoreHost") to read "Every method runs on the UI thread".

`netplay.cpp`:
- In the bounce branch of `start()`, after `setOverride(kModeKey, "disabled", nullptr)`, add:

```cpp
        minEpoch_ = host_.eventEpochBump();
        bounceFrom_ = host_.frameCount();
        bounceNeed_ = host_.threaded() ? 3u : 2u;   // threaded: one more for a frame already running when the post was queued
```

  Remove `bounceFrames_ = 2`.
- At the end of `applyNow()` (after its option writes) and at the end of `clearAll()`, add `minEpoch_ = host_.eventEpochBump();`.
- `afterFrame()`:

```cpp
void Netplay::afterFrame() {
    if (!bouncing_) return;
    const uint64_t f = host_.frameCount();
    if (f >= bounceFrom_ && f - bounceFrom_ < bounceNeed_) return;   // (f < bounceFrom_: a reload restarted the count)
    bouncing_ = false;
    std::string err;
    if (!applyNow(bounceTarget_, err, nullptr)) {
        openError_ = err;
        state_ = LinkState::Off;
    }
}
```

- `onUiEvent`:

```cpp
void Netplay::onUiEvent(const CoreEvent& e) {
    if (e.epoch < minEpoch_) return;
    if (e.kind == CoreEvent::Kind::Log) {
        onCoreLog(e.level, e.text.c_str());
    } else if (e.kind == CoreEvent::Kind::Event) {
        vj_event v{};
        v.type = e.type;
        v.a = e.a, v.b = e.b, v.c = e.c, v.d = e.d;
        v.s = e.text.empty() ? nullptr : e.text.c_str();
        onCoreEvent(v);
    }
}
```

`netplay_ui.cpp`:
- `drawNetplayMenu` and `drawNetplay`: delete the `if (app.emuThreadThisRun()) { ... }` blocks.
- `netplayInit`: delete the threaded guard block, and replace the two taps with:

```cpp
    // Inline, called where the core says it; threaded, on this (the UI) thread at each pin (#967 phase 2).
    app.host.onUiEvent = [&app](const CoreEvent& e) { app.netplay.onUiEvent(e); };
```

- `netplayHeadlessCheck`: make `step` work in both modes:

```cpp
    auto step = [&]() {
        if (app.threaded()) {
            app.emu.advance(1);
            app.host.pinSnapshot();   // the core's link lines and events arrive here
            if ((++frame & 15) == 15) app.emu.call([&app] { app.audio.clear(); });
        } else {
            app.audio.update();
            app.host.runFrame();
            if ((++frame & 15) == 15) app.audio.clear();
        }
        np.afterFrame();
        app.host.takeMessages();
        SDL_Delay(2);   // real time, so the partner process gets to run and TCP gets to connect
    };
```

If `threaded()` / `emu` are private on `App`, use the accessors the file already has (`emuThreadThisRun()`); `emu` is public in Phase 1.

`app.cpp`:
- `threadedFrame()`: right after `host.pinSnapshot();`, add `netplay.afterFrame();`.
- `runHeadless()`: change `int nr = threaded() ? 0 : netplayHeadlessCheck(*this, a);` to `int nr = netplayHeadlessCheck(*this, a);`.
- `limitFeaturesForEmuThread()`: the comment line about Netplay goes. Task 6 changes the message.

- [ ] **Step 4: Run the tests**

Run: `cmake --build app/build -j8 2>&1 | tail -3; ctest --test-dir app/build -C Release -R "netplay|network_options|emu_thread_limits" --output-on-failure`
Expected: every netplay test passes, including the four new ones. Each threaded child prints `NETPLAY link=UP ... via=events`. `emu_thread_limits` still passes: its message changes in Task 6. Under heavy host load, re-run the pairs alone before treating a "no link within 30s" as real (see the baseline note).

- [ ] **Step 5: Commit**

```bash
git add app/src/netplay.h app/src/netplay.cpp app/src/netplay_ui.cpp app/src/app.cpp app/tests/netplay_test.cpp \
        app/cmake/VJNetplayTests.cmake app/cmake/VJEmuThreadTests.cmake
git commit -F - <<'EOF'
feat(app): netplay works with the emulation thread on (#967)

Netplay stays a UI-thread object: the core's link lines and events reach it through CoreHost::onUiEvent, filtered
by an epoch bumped at every start/stop/bounce, and the endpoint bounce completes after the core has read "disabled"
for 2 emulated frames (3 threaded), counted with frameCount().  Threaded twins of netplay_pair (both sides, voice
modem, a mixed threaded/inline pair) and of netplay_no_partner, which must exit 1, never 3.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
```

### Task 4: The cheat batch as a regular command

**Files:**
- Modify: `app/src/cheats.h` (`CheatOp`, `CheatBatch`, `commit`; remove `resetSink`, `setSink`, `doneSink`), `app/src/cheats.cpp` (`apply()`), `app/src/cheats_core.cpp` (remove the file-local `CheatOp` / `g_batch`; `cm.commit`; `applyCheatBatch`), `app/tests/cheats_test.cpp` (`Recorder`, `testDoneSink` -> `testCommitBatch`)

**Interfaces:**
- Produces:
  - `struct vj::CheatOp { bool reset = false; unsigned index = 0; bool enabled = false; std::string code; };`
  - `using vj::CheatBatch = std::vector<CheatOp>;`
  - `std::function<void(CheatBatch)> CheatManager::commit`: called once per `apply()`, with the reset first.
  - `void applyCheatBatch(const CheatBatch&)` in `cheats_core.cpp`, which runs it on the core.

- [ ] **Step 1: Write the failing test.** In `app/tests/cheats_test.cpp`, change `Recorder::attach` to:

```cpp
    void attach(CheatManager& m) {
        m.commit = [this](CheatBatch b) {
            batches++;
            for (const CheatOp& o : b) {
                if (o.reset) log.push_back("reset");
                else log.push_back("set " + std::to_string(o.index) + (o.enabled ? " on " : " off ") + o.code);
            }
        };
    }
    int batches = 0;
```

Every existing test that reads `r.log` keeps its expectations. Rename `testDoneSink` to `testCommitBatch` and rewrite its body. Keep its setup lines (the temp dir and the one cheat it enables); the new assertions are:

```cpp
    r.log.clear();
    r.batches = 0;
    m.apply();
    CHECK(r.batches == 1);                                  // one apply, one command
    CHECK(r.log.size() == 2 && r.log[0] == "reset" && r.log[1].compare(0, 3, "set") == 0);
    m.blocked = [] { return true; };
    r.log.clear();
    r.batches = 0;
    m.apply();
    CHECK(r.batches == 1 && r.log.size() == 1 && r.log[0] == "reset");   // blocked: the reset still goes
```

Update the call in `main` to `testCommitBatch();`.

- [ ] **Step 2: Run it and watch it fail**

Run: `cmake --build app/build --target vj_cheats_test 2>&1 | tail -3`
Expected: compile errors (`commit`, `CheatBatch`).

- [ ] **Step 3: Implement.**

`cheats.h`: replace the three sinks and their comments with:

```cpp
    // One cheat-list change: the core's list is reset, then each set applied, as ONE command (#967).  The app posts it
    // to the emulation thread (EmuThread::post, which runs it at once inline or when already there), so a frame never
    // sees half a list.
    std::function<void(CheatBatch)> commit;
```

Add `CheatOp` and `CheatBatch` above `class CheatManager`.

`cheats.cpp` `apply()`:

```cpp
void CheatManager::apply() {
    CheatBatch b;
    b.push_back(CheatOp{true, 0, false, std::string()});
    if (!blockedNow()) {
        // ... the existing loops, each `setSink(i, true, code)` becoming
        //     b.push_back(CheatOp{false, i, true, code});
    }
    if (commit) commit(std::move(b));
    // ... the existing over-limit warn, unchanged (only when not blocked, as before)
}
```

Keep the existing control flow around `blockedNow()`. The only difference is that the blocked path now also reaches the single `commit` at the end, carrying just the reset.

`cheats_core.cpp`: delete the file-local `CheatOp` / `g_batch` and the three sink lambdas. Add:

```cpp
// On the emulation thread (or inline): the cheat list as one command.
void applyCheatBatch(const CheatBatch& b) {
    for (const CheatOp& o : b) {
        if (o.reset) retro_cheat_reset();
        else retro_cheat_set(o.index, o.enabled, o.code.c_str());
    }
}
```

In `cheatsInit`:

```cpp
    cm.commit = [&app](CheatBatch b) {
        app.emu.post([b = std::move(b)] { applyCheatBatch(b); });   // at once inline / on the emulation thread
    };
```

`applyCheatBatch` can stay file-local. Task 5 calls the single reset through `corePost`, not through this function.

- [ ] **Step 4: Run the tests**

Run: `cmake --build app/build -j8 2>&1 | tail -3; ctest --test-dir app/build -C Release -R "cheats_unit|cheat_|ra_cheat" --output-on-failure`
Expected: all pass, including `cheat_apply_threaded` and every `ra_cheat_*`.

- [ ] **Step 5: Commit**

```bash
git add app/src/cheats.h app/src/cheats.cpp app/src/cheats_core.cpp app/tests/cheats_test.cpp
git commit -F - <<'EOF'
refactor(app): the cheat list reaches the core as one regular command (#967)

CheatManager::apply() builds one CheatBatch (reset, then every set) and hands it to commit, which the app posts to
the emulation thread; the three sinks and the UI-thread batch collector are gone.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
```

### Task 5: Achievements safe across two threads (inline behaviour unchanged)

**Files:**
- Create: `app/src/ach_event.h`, `app/src/ach_event.cpp`, `app/tests/ach_event_test.cpp`
- Modify: `app/src/achievements.h`, `app/src/achievements.cpp`, `app/src/achievements_ui.cpp` (rich presence), `app/src/discord_app.cpp` (only if it reads rich presence), `app/CMakeLists.txt` (`src/ach_event.cpp` in the app's sources), `app/cmake/VJAchTests.cmake` (`ach_event_unit`)

**Interfaces:**
- Consumes: `EmuThread::call/post/threaded/onEmuThread` (Phase 1).
- Produces:
  - `struct vj::AchEvent` and `AchEvent vj::achEventFrom(const rc_client_event_t& e, bool hardcore)`.
  - `void Achievements::emuFrame()`: what follows a frame on the emulation thread.
  - `void Achievements::emuIdle()`: the emulation thread's idle.
  - `std::string Achievements::richPresence() const`.
  - `unsigned Achievements::offThreadReads() const`.
  - Unchanged: `afterFrame()`, which inline does what it did and then calls `emuFrame()`, and `pump()`.

This task changes no behaviour inline. It makes every rc_client callback safe to run on either thread, so Task 6 can switch them on while threaded.

- [ ] **Step 1: Write the failing test** `app/tests/ach_event_test.cpp`:

```cpp
// AchEvent (ach_event.h): an owned copy of an rc_client event, so the handler can queue it on whichever thread raised
// it and the UI thread can apply it later (#967 phase 2).  rc_client's strings are valid for the call only.
//
//   vj_ach_event_test
#include "ach_event.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace {
int g_fail = 0;
#define CHECK(c)                                                    \
    do {                                                            \
        if (!(c)) {                                                 \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); \
            g_fail++;                                               \
        }                                                           \
    } while (0)
}  // namespace

int main() {
    char title[32], desc[32], badge[64], measured[16];
    std::strcpy(title, "First steps");
    std::strcpy(desc, "Do a thing");
    std::strcpy(badge, "https://example.invalid/b.png");
    std::strcpy(measured, "3/10");
    rc_client_achievement_t a;
    std::memset(&a, 0, sizeof a);
    a.id = 1001;
    a.title = title;
    a.description = desc;
    a.badge_url = badge;
    a.points = 5;
    std::strcpy(a.measured_progress, measured);   // a char array in rc_client_achievement_t
    rc_client_event_t e;
    std::memset(&e, 0, sizeof e);
    e.type = RC_CLIENT_EVENT_ACHIEVEMENT_TRIGGERED;
    e.achievement = &a;
    vj::AchEvent c = vj::achEventFrom(e, true);
    std::memset(title, 'x', sizeof title - 1);   // the source goes away after the handler returns
    std::memset(desc, 'x', sizeof desc - 1);
    CHECK(c.type == RC_CLIENT_EVENT_ACHIEVEMENT_TRIGGERED);
    CHECK(c.id == 1001 && c.points == 5 && c.hardcore);
    CHECK(c.title == "First steps" && c.description == "Do a thing" && c.badgeUrl == "https://example.invalid/b.png");
    CHECK(c.measured == "3/10");

    rc_client_leaderboard_tracker_t t;
    std::memset(&t, 0, sizeof t);
    t.id = 7;
    std::strcpy(t.display, "00:12.3");
    std::memset(&e, 0, sizeof e);
    e.type = RC_CLIENT_EVENT_LEADERBOARD_TRACKER_UPDATE;
    e.leaderboard_tracker = &t;
    c = vj::achEventFrom(e, false);
    CHECK(c.id == 7 && c.display == "00:12.3" && !c.hardcore);

    rc_client_server_error_t se;
    std::memset(&se, 0, sizeof se);
    se.api = "awardachievement";
    se.error_message = "nope";
    std::memset(&e, 0, sizeof e);
    e.type = RC_CLIENT_EVENT_SERVER_ERROR;
    e.server_error = &se;
    c = vj::achEventFrom(e, false);
    CHECK(c.api == "awardachievement" && c.error == "nope");

    std::memset(&e, 0, sizeof e);   // every pointer null: no crash, empty strings
    e.type = RC_CLIENT_EVENT_LEADERBOARD_STARTED;
    c = vj::achEventFrom(e, false);
    CHECK(c.title.empty());

    std::printf("ach_event_test: %s\n", g_fail ? "FAILED" : "OK");
    return g_fail ? 1 : 0;
}
```

Check the field types in rcheevos v12.5.0's `include/rc_client.h` before relying on the `strcpy` calls. `rc_client_achievement_t::measured_progress` and `rc_client_leaderboard_tracker_t::display` are fixed-size char arrays there; adjust if not. `rc_client_leaderboard_scoreboard_t` has `submitted_score` (char array), `new_rank` and `num_entries`.

In `app/cmake/VJAchTests.cmake`, add:

```cmake
add_executable(vj_ach_event_test tests/ach_event_test.cpp src/ach_event.cpp)
target_include_directories(vj_ach_event_test PRIVATE src)
target_link_libraries(vj_ach_event_test PRIVATE vj_rcheevos)
add_test(NAME ach_event_unit COMMAND vj_ach_event_test)
set_tests_properties(ach_event_unit PROPERTIES TIMEOUT 60 FAIL_REGULAR_EXPRESSION "FAIL ")
```

- [ ] **Step 2: Run it and watch it fail**

Run: `cmake -S app -B app/build >/dev/null; cmake --build app/build --target vj_ach_event_test 2>&1 | tail -3`
Expected: the build fails because `ach_event.h` does not exist.

- [ ] **Step 3: Create `ach_event.h` / `.cpp`.**

```cpp
// ach_event.h -- an rc_client event, copied (#967 phase 2).  The handler runs on whichever thread raised the event
// (the emulation thread inside rc_client_do_frame; the UI thread for completions pumped by AchHttp); it queues one of
// these and Achievements::pump() applies it on the UI thread.  Every string is owned.
#pragma once

#include "rc_client.h"

#include <cstdint>
#include <string>

namespace vj {

struct AchEvent {
    uint32_t type = 0;                 // RC_CLIENT_EVENT_*
    uint32_t id = 0;                   // achievement / leaderboard / tracker id
    uint32_t points = 0;
    bool hardcore = false;             // rc_client's hardcore flag when the event was raised
    std::string title, description, badgeUrl;
    std::string measured;              // achievement: measured_progress
    std::string display;               // leaderboard tracker
    std::string api, error;            // server error
    std::string submittedScore;        // scoreboard
    uint32_t newRank = 0, numEntries = 0;
    std::string subsetTitle;
};

AchEvent achEventFrom(const rc_client_event_t& e, bool hardcore);

}  // namespace vj
```

`achEventFrom` copies, with a null check on every pointer:
- from `e.achievement`: `id`, `title`, `description`, `badge_url`, `points`, `measured_progress`;
- from `e.leaderboard`: `id`, `title`;
- from `e.leaderboard_tracker`: `id`, `display`;
- from `e.leaderboard_scoreboard`: `submitted_score`, `new_rank`, `num_entries`;
- from `e.server_error`: `api`, `error_message`;
- from `e.subset`: `title`, into `subsetTitle`.

Add `src/ach_event.cpp` to the app target's source list next to `src/achievements.cpp`.

- [ ] **Step 4: Change `Achievements`.** Every change keeps the inline path doing exactly what it did.

1. **Event queue.** Add the private members `std::mutex eventMu_; std::vector<AchEvent> events_;` and `void drainEvents();`.
   - The handler (`AchCallbacks::event`) becomes `a->queueEvent(achEventFrom(*e, rc_client_get_hardcore_enabled(c) != 0));` (push under `eventMu_`).
   - `handleEvent(const rc_client_event_t*)` becomes `void handleEvent(const AchEvent& e)`. Its body is the same switch, reading the copied fields; `SessionUnlock::hardcore` comes from `e.hardcore`.
   - `drainEvents()` swaps `events_` out under the lock and applies each event.
   - `pump()` calls `drainEvents()` right after `mockDeliver()` and again just before the `resetPending_` block. The second call catches events raised by `rc_client_idle` and by completions delivered in the same pump.
2. **Mock server lock.** Add `std::mutex mockMu_;` guarding `mockQueue_`, `mockLog_`, `mockHashes_` and `mockUnhandled_`. In threaded mode an award reaches `serverCall` from inside `do_frame` on the emulation thread.
   - Take the lock in `mockRespond`.
   - Take it in `mockDeliver`, only around the swap: callbacks may queue more, so never call one while holding it.
   - Take it in `headlessCheck` and in `headlessSummary` (the code around lines 843-905) wherever they read these vectors.
3. **Memory flag.** `memoryReady_` becomes `std::atomic<bool>`, and `ranUnrestricted_` becomes `std::atomic<bool>`.
4. **Core calls go to the emulation thread.** Declare two private helpers in `achievements.h` and define them in `achievements.cpp`, where `App` is a complete type:

```cpp
// The core belongs to the emulation thread (#967): run there (inline / already there: at once).
void Achievements::coreCall(const std::function<void()>& fn) {
    if (app_) app_->emu.call([&fn] { fn(); });
    else fn();
}
void Achievements::corePost(std::function<void()> fn) {
    if (app_) app_->emu.post(std::move(fn));
    else fn();
}
```

   - Replace the four `retro_cheat_reset();` calls in `achievements.cpp` (in `onContentLoaded`, `pump()`'s `resetPending_` block, `checkHardcoreTransition` and `setHardcoreWanted`) with `corePost([] { retro_cheat_reset(); });`. In `pump()` the post precedes `app_->reset()`'s call, and the queue is FIFO, so the order is kept.
   - In `beginLoad()`, replace `rebuildMemory();` with `coreCall([this] { rebuildMemory(); });`.
   - In `headlessFrame()`, the poke's `retro_cheat_set(...)` becomes `coreCall([&] { retro_cheat_set(pokeIndex_, true, p.code.c_str()); }); pokeIndex_++;`.
   - In `runStep()`, wrap the `poke:` / `expect:` branch's memory access in `coreCall([&] { ... })`: the `memoryRegion` call and the read or write. The `fail(...)` / `printf` results are computed inside the lambda.
5. **`read_memory` off the emulation thread.** Replace `AchCallbacks::readMemory` with:

```cpp
    static uint32_t RC_CCONV readMemory(uint32_t address, uint8_t* buffer, uint32_t numBytes, rc_client_t* c) {
        Achievements* a = self(c);
        if (!a || !a->memoryReady_.load()) return 0;
        if (a->app_ && a->app_->emu.threaded() && !a->app_->emu.onEmuThread()) {
            // Core RAM belongs to the emulation thread (#967).  The one reader that runs here is a game load completed
            // in the UI's HTTP pump, validating its addresses (rc_client_validate_addresses: only the count matters):
            // answer from the memory map, without touching RAM.
            uint32_t avail = 0;
            if (!rc_libretro_memory_find_avail(&a->regions_, address, &avail) || avail == 0) return 0;
            const uint32_t n = avail < numBytes ? avail : numBytes;
            std::memset(buffer, 0, n);
            if (a->offThreadReads_.fetch_add(1) == 0)
                std::fprintf(stderr, "[ra] address check off the emulation thread answered from the memory map\n");
            return n;
        }
        return rc_libretro_memory_read(&a->regions_, address, buffer, numBytes);
    }
```

   Add the member `std::atomic<unsigned> offThreadReads_{0};` and the accessor `unsigned offThreadReads() const { return offThreadReads_.load(); }`.
6. **Rich presence, published.** Add the members `mutable std::mutex rpMu_; std::string richPresence_; unsigned rpFrames_ = 0;` and:

```cpp
// The rich-presence line, computed on the emulation thread (it evaluates game memory) every 60 frames.
std::string Achievements::richPresence() const {
    std::lock_guard<std::mutex> l(rpMu_);
    return richPresence_;
}
```

   In `achievements_ui.cpp` around line 192, replace the `rc_client_has_rich_presence` / `rc_client_get_rich_presence_message` pair with `const std::string rp = app.ach.richPresence(); if (!rp.empty()) { ... }`. Show the same text in the same place. Grep `discord_app.cpp` and `discord_presence.cpp` for `rich_presence` and switch any caller in the same way.
7. **The frame split.**

```cpp
// After each emulated frame, on the emulation thread (threaded) -- or inline from afterFrame().  Reads game memory.
void Achievements::emuFrame() {
    if (!client_) return;
    if (rc_client_is_game_loaded(client_) && memoryReady_.load()) {
        rc_client_do_frame(client_);   // ends with rc_client_idle(): the scheduled pings evaluate rich presence here
        if (++rpFrames_ >= 60) {
            rpFrames_ = 0;
            char rp[256] = "";
            if (rc_client_has_rich_presence(client_)) rc_client_get_rich_presence_message(client_, rp, sizeof rp);
            std::lock_guard<std::mutex> l(rpMu_);
            richPresence_ = rp;
        }
    } else {
        rc_client_idle(client_);
    }
}

// The emulation thread's idle while no frame runs (paused, or no game identified): pings and retries.
void Achievements::emuIdle() {
    if (client_) rc_client_idle(client_);
}
```

   `afterFrame()` keeps `checkHardcoreTransition()`, the `ranUnrestricted_` line and the windowed-mock `headlessFrame`. Its last two lines become `emuFrame();`. Clear `richPresence_` (under `rpMu_`) and `rpFrames_` in `onContentClosed()`.

- [ ] **Step 5: Run the tests**

Run: `cmake --build app/build -j8 2>&1 | tail -3; ctest --test-dir app/build -C Release -R "^ra_|ach_|cheat" --output-on-failure`
Expected: all pass. `ach_event_test: OK`. Every `ra_*` test passes unchanged (inline). No `[ra] address check off the emulation thread` line appears inline, because inline is never off the emulation thread.

- [ ] **Step 6: Commit**

```bash
git add app/src/ach_event.h app/src/ach_event.cpp app/src/achievements.h app/src/achievements.cpp \
        app/src/achievements_ui.cpp app/src/discord_app.cpp app/CMakeLists.txt app/cmake/VJAchTests.cmake \
        app/tests/ach_event_test.cpp
git commit -F - <<'EOF'
refactor(app): RetroAchievements safe on two threads (#967)

rc_client events are copied into AchEvents and applied in pump(); core RAM, the memory map and the cheat resets
are reached on the emulation thread (call/post); read_memory off it answers address checks from the map without
touching RAM; the rich-presence line is computed with the frame and published; the mock server is locked.  Inline
runs behave as before.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
```

### Task 6: RetroAchievements on with the emulation thread

**Files:**
- Modify: `app/src/app.cpp` (`run`: `ach.init` always; `limitFeaturesForEmuThread` message; `emuRunOneFrame`: `ach.emuFrame()`; `runHeadless`: the frame-stepped RA loop), `app/src/achievements.cpp` (`pump()` when threaded), `app/cmake/VJAchTests.cmake` (twins), `app/cmake/VJEmuThreadTests.cmake` (`emu_thread_limits` regex, header list)

**Interfaces:**
- Consumes: `Achievements::emuFrame/emuIdle/offThreadReads` (Task 5), `EmuThread::advance/framesRun/post` (Phase 1).
- Produces: the `vj_ra_test()` CMake function, which registers each RA run inline and as `<name>_threaded`. The `[emu]` limits line now reads `[emu] the debugger tools and the GDB stub are off while the emulation thread is on`.

- [ ] **Step 1: Write the failing tests.** In `app/cmake/VJAchTests.cmake`, add the function below near the top, after `VJ_RA_FIX` is set:

```cmake
# vj_ra_test(NAME <name> [PASS <regex>] [FIXTURES_SETUP <f>] [FIXTURES_REQUIRED <f;...>] ARGS <app arguments...>)
# Registers <name> (inline, as it always ran) and <name>_threaded: the same run with --emu-thread on (#967 phase 2),
# labelled emu_thread, so the TSan job runs it too.  Frame-numbered steps and pokes must land on the same frame in
# both.  A PASS regex makes it a control (it passes by failing for the stated reason); its twin then fails only on an
# owner violation or a TSan report.  In the twin, the saved-cheat data dir and the fixture names get a _threaded
# suffix, so the two chains never share state.
function(vj_ra_test)
    cmake_parse_arguments(RA "" "NAME;PASS;FIXTURES_SETUP" "FIXTURES_REQUIRED;ARGS" ${ARGN})
    add_test(NAME ${RA_NAME} COMMAND virtualjaguar --headless ${RA_ARGS})
    set(_twin)
    foreach(_a IN LISTS RA_ARGS)
        string(REPLACE "${_rcd}" "${_rcd}_threaded" _a "${_a}")
        list(APPEND _twin "${_a}")
    endforeach()
    add_test(NAME ${RA_NAME}_threaded COMMAND virtualjaguar --headless --emu-thread on ${_twin})
    set_tests_properties(${RA_NAME} ${RA_NAME}_threaded PROPERTIES TIMEOUT 300)
    set_tests_properties(${RA_NAME}_threaded PROPERTIES LABELS emu_thread)
    if(RA_PASS)
        set_tests_properties(${RA_NAME} ${RA_NAME}_threaded PROPERTIES PASS_REGULAR_EXPRESSION "${RA_PASS}")
        set_tests_properties(${RA_NAME}_threaded PROPERTIES FAIL_REGULAR_EXPRESSION "owner violation|ThreadSanitizer")
    else()
        set_tests_properties(${RA_NAME} PROPERTIES FAIL_REGULAR_EXPRESSION "headless: FAIL")
        set_tests_properties(${RA_NAME}_threaded PROPERTIES
            FAIL_REGULAR_EXPRESSION "headless: FAIL|owner violation|ThreadSanitizer")
    endif()
    if(RA_FIXTURES_SETUP)
        set_tests_properties(${RA_NAME} PROPERTIES FIXTURES_SETUP ${RA_FIXTURES_SETUP})
        set_tests_properties(${RA_NAME}_threaded PROPERTIES FIXTURES_SETUP ${RA_FIXTURES_SETUP}_threaded)
    endif()
    if(RA_FIXTURES_REQUIRED)
        set(_req)
        foreach(_f IN LISTS RA_FIXTURES_REQUIRED)
            list(APPEND _req ${_f}_threaded)
        endforeach()
        set_tests_properties(${RA_NAME} PROPERTIES FIXTURES_REQUIRED "${RA_FIXTURES_REQUIRED}")
        set_tests_properties(${RA_NAME}_threaded PROPERTIES FIXTURES_REQUIRED "${_req}")
    endif()
endfunction()
```

Convert every `add_test(NAME ra_... COMMAND virtualjaguar --headless ...)` in the file to `vj_ra_test(NAME ra_... ARGS ...)`, with the same arguments minus `--headless`. Delete the `set_tests_properties` calls the function now covers.
- **Controls.** `ra_dashboard_mock_control`, `ra_cheat_hardcore_blocked_control` and `ra_cheat_toggle_control` take `PASS "<their regex>"`.
- **Saved-cheat chain.** It moves `set(_rcd ...)` above the function. Its `vj_ra_test` calls pass `FIXTURES_SETUP` / `FIXTURES_REQUIRED` as today.
- **Directory tests.** The two `cmake -E remove_directory` tests (`ra_cheat_saved_clean` / `_cleanup`) stay `add_test`s. Add twins by hand on `"${_rcd}_threaded"`: `ra_cheat_saved_clean_threaded` with `FIXTURES_SETUP ra_cheat_fresh_threaded`, and `ra_cheat_saved_cleanup_threaded` with `FIXTURES_CLEANUP "ra_cheat_fresh_threaded;ra_cheat_saved_threaded;ra_cheat_hardcore_done_threaded"`.
- **ROM-gated test.** `ra_cd_cue` stays a plain `add_test`, with no twin.

Then add the off-thread validation pin:

```cmake
# The game load completes in the UI's HTTP pump and rc_client checks every address there: with the emulation thread
# on, that check must be answered from the memory map, never by reading RAM off the emulation thread.
add_test(NAME ra_offthread_validation_threaded
    COMMAND virtualjaguar --headless --emu-thread on --frames 60 --ra-mock "${VJ_RA_FIX}" "${VJ_ROMS}/yarc.j64")
set_tests_properties(ra_offthread_validation_threaded PROPERTIES TIMEOUT 300 LABELS emu_thread
    PASS_REGULAR_EXPRESSION "address check off the emulation thread answered from the memory map"
    FAIL_REGULAR_EXPRESSION "headless: FAIL|owner violation|ThreadSanitizer")
```

In `app/cmake/VJEmuThreadTests.cmake`, change `emu_thread_limits`' PASS regex to `"the debugger tools and the GDB stub are off while the emulation thread is on"`. Update its header-list line to say "the debugger and the GDB stub are off while threaded".

- [ ] **Step 2: Run them and watch them fail**

Run: `cmake -S app -B app/build >/dev/null && ctest --test-dir app/build -C Release -R "ra_unlock_softcore_threaded|ra_offthread_validation_threaded|emu_thread_limits" --output-on-failure`
Expected: the twins fail with `headless: FAIL ra: --ra-expect-*/--ra-poke given but the mock session is not running`, because `ach.init` is skipped while threaded. `emu_thread_limits` fails on the old message.

- [ ] **Step 3: Implement.**

`app.cpp` `run()`:

```cpp
    wireHost();
    if (emuThreadThisRun_) limitFeaturesForEmuThread();
    ach.init(*this, a);   // with the emulation thread on, rc_client_do_frame runs there (Achievements::emuFrame)
```

`limitFeaturesForEmuThread()`: update the comment (only the Tools windows and the GDB stub remain off, until Phase 3), and change the message to `"[emu] the debugger tools and the GDB stub are off while the emulation thread is on\n"`.

`emuRunOneFrame()`: after `runFrames_.fetch_add(1, ...)`, add `ach.emuFrame();   // rc_client_do_frame: reads game RAM, so here`.

`achievements.cpp` `pump()`, when threaded:
- Skip `rc_client_idle(client_)`. The emulation thread runs it, inside `do_frame` or through `emuIdle`.
- While the game is paused or not identified, post `emuIdle` once a second, so pings and retries keep going.
- Mark `ranUnrestricted_` from the frames that ran since the last pump.
- A windowed mock session runs its pokes per frame number seen.

```cpp
    const bool threaded = app_ && app_->emu.threaded();
    if (!threaded) {
        rc_client_idle(client_);
    } else {
        const uint64_t ran = app_->emu.framesRun();
        if (ran != framesSeen_) {
            if (!hardcoreActive()) ranUnrestricted_ = true;
            if (mockMode() && app_->window())
                for (uint64_t f = framesSeen_; f < ran; f++) headlessFrame(++mockFrames_);
            framesSeen_ = ran;
        } else if (app_->now() - lastIdlePost_ > 1.0) {
            lastIdlePost_ = app_->now();
            app_->emu.post([this] { emuIdle(); });
        }
    }
```

Add the members `uint64_t framesSeen_ = 0; double lastIdlePost_ = 0;`. Remove the `!window` condition only from this new path: `afterFrame()`'s mock line stays for inline.

`runHeadless()`, threaded branch: when the RA mock session is running, step one frame at a time in the inline loop's order. Otherwise keep the 16-frame batches.

```cpp
        if (threaded()) {
            retroTicks_.store(0);
            if (ach.mockMode()) {
                // RetroAchievements steps and pokes are frame-numbered: the same order as the inline loop below, one
                // frame at a time (the steps, then the frame -- rc_client_do_frame runs after it, on the emulation
                // thread -- then the UI's pump).
                for (unsigned i = 0; i < a.frames; i++) {
                    ach.headlessFrame(i);
                    emu.advance(1);
                    ach.pump();
                    if ((i & 15) == 15) emu.call([this] { audio.clear(); });
                    host.pinSnapshot();
                    for (const OsdMessage& m : host.takeMessages()) std::fprintf(stderr, "[osd] %s\n", m.text.c_str());
                }
            } else {
                // ... the existing 16-frame batches, unchanged
            }
            hlRun = retroTicks_.exchange(0);
            host.pinSnapshot();
        }
```

`headlessFrame` and `runStep` already reach the core through `coreCall` (Task 5). `setHardcoreWanted` and `logout` run here on the UI thread, as they do from the menu.

- [ ] **Step 4: Run the tests**

Run: `cmake --build app/build -j8 2>&1 | tail -3; ctest --test-dir app/build -C Release -R "^ra_|ach_|emu_thread_limits" --output-on-failure -j4`
Expected: every `ra_*` test and its `_threaded` twin passes, along with `ra_offthread_validation_threaded` and `emu_thread_limits`. If a twin fails while its inline original passes, find which frame the step or expectation landed on; a different frame is Review Focus 5. Fix the stepping, never the test.

Then, on a TSan build (as in Phase 1 Task 16), run:

`cmake -S app -B app/build-tsan -DCMAKE_BUILD_TYPE=RelWithDebInfo -DVJ_APP_TSAN=ON >/dev/null && cmake --build app/build-tsan -j8 2>&1 | tail -2 && TSAN_OPTIONS="halt_on_error=1 suppressions=$PWD/app/tests/tsan.supp" ctest --test-dir app/build-tsan -C RelWithDebInfo -R "^ra_.*_threaded$|netplay_.*threaded|netplay_pair_mixed" --output-on-failure`

Expected: no ThreadSanitizer report. If TSan names a race inside rcheevos between `rc_client_do_frame` (emulation thread) and a UI-thread `rc_client_*` reader, route that reader through a value published from `emuFrame()`, the way `richPresence()` works. Do not suppress it. Readers that may appear: `rc_client_get_game_info`, `rc_client_get_user_game_summary`, `rc_client_create_achievement_list`. Name the reader and the fix in the commit message.

- [ ] **Step 5: Commit**

```bash
git add app/src/app.cpp app/src/achievements.h app/src/achievements.cpp app/cmake/VJAchTests.cmake app/cmake/VJEmuThreadTests.cmake
git commit -F - <<'EOF'
feat(app): RetroAchievements works with the emulation thread on (#967)

rc_client_do_frame (and with it rc_client_idle) runs after each frame on the emulation thread; the UI pump posts the
idle while paused.  A threaded headless RA run steps one frame at a time in the inline order, so every ra_* test has
a _threaded twin that lands its steps on the same frames.  Only the debugger tools and the GDB stub stay off.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
```

### Task 7: The 250 ms wait hook and the busy spinner

**Files:**
- Modify: `app/src/emu_thread.h` (`setWaitHook`, `kBusyAfter`, `call()`'s wait loop), `app/src/core_host.h` / `.cpp` (`setUiWaiting`, the waiting-read guard on the `call()`-guarded readers), `app/src/video.h` / `.cpp` (`VideoOut::drawBusy`), `app/src/app.h` / `app.cpp` (`busyFrame`, the hook in `startEmuThread`, the `VJ_TEST_SLOW_CALL_MS` hook in `openContentOnCore`), `app/tests/emu_thread_test.cpp`, `app/tests/core_host_thread_test.cpp`, `app/cmake/VJEmuThreadTests.cmake`

**Interfaces:**
- Produces:
  - `static constexpr std::chrono::milliseconds EmuThread::kBusyAfter{250}`.
  - `void EmuThread::setWaitHook(std::function<void(double waitedSec, unsigned tick)> hook)`. The hook is called on the thread waiting in `call()`, about every 16 ms from 250 ms on. `tick` is 0 on the first call for each `call()`. It is never called inline, never on the emulation thread, and never re-entered.
  - `void CoreHost::setUiWaiting(bool)`.
  - `void VideoOut::drawBusy(int fbWidth, int fbHeight, double seconds)`.

**Contract of the hook** (document it at `setWaitHook`). It runs while a `call()` may be changing what `CoreHost` reads directly. It may pump OS events without dispatching them, draw with raw GL and swap. It must not:
- open or end an ImGui frame;
- call `call()` / `post()`;
- read `isLoaded`, `loadedPath`, `loadedName`, `stagedBios`, `controllerPorts`, `eventsActive` or `debugIface`;
- touch the late latch or perf swap accounting.

- [ ] **Step 1: Write the failing tests.** In `app/tests/emu_thread_test.cpp`, before `main`, add:

```cpp
// #967 phase 2: a call() slower than 250 ms runs the wait hook on the caller (the UI draws a spinner there); a fast
// one never does; the hook is never re-entered and never runs on the emulation thread.
void testWaitHook() {
    EmuThread e;
    FakeClient c;
    CHECK(e.start(EmuThread::Mode::Threaded, c), "start");
    std::atomic<int> calls{0};
    std::atomic<int> firstTicks{0};
    std::atomic<bool> onEmu{false};
    std::atomic<bool> nested{false};
    std::atomic<int> depth{0};
    e.setWaitHook([&](double waited, unsigned tick) {
        if (depth.fetch_add(1) != 0) nested = true;
        if (e.onEmuThread()) onEmu = true;
        if (tick == 0) firstTicks++;
        if (waited < 0.24) nested = true;   // (abuse the flag: never before 250 ms)
        calls++;
        depth--;
    });
    e.call([] { return 0; });
    CHECK(calls.load() == 0, "a fast call runs no hook (%d)", calls.load());
    e.call([] {
        std::this_thread::sleep_for(std::chrono::milliseconds(450));
        return 0;
    });
    CHECK(calls.load() >= 3, "a 450 ms call runs the hook repeatedly from 250 ms (%d)", calls.load());
    CHECK(firstTicks.load() == 1, "tick 0 once per call (%d)", firstTicks.load());
    CHECK(!onEmu.load(), "never on the emulation thread");
    CHECK(!nested.load(), "never re-entered, never before 250 ms");
    const int before = calls.load();
    e.post([] { std::this_thread::sleep_for(std::chrono::milliseconds(400)); });   // a slow post: nobody waits on it
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    CHECK(calls.load() == before, "posts never run the hook");
    CHECK(e.stop(), "stop");
}
```

Call it from `main` after the last existing test.

In `app/tests/core_host_thread_test.cpp`, add the section below to the threaded part:

```cpp
    {
        // While the UI waits in a call (the spinner's hook runs), the fields only a call() writes may be changing:
        // reading one there is a bug, counted like any other owner violation.
        const unsigned v0 = host.ownerViolations();
        host.setUiWaiting(true);
        (void)host.isLoaded();
        (void)host.stagedBios(2);
        host.setUiWaiting(false);
        CHECK(host.ownerViolations() == v0 + 2);
        (void)host.isLoaded();   // not waiting: a direct read is fine
        CHECK(host.ownerViolations() == v0 + 2);
    }
```

In `app/cmake/VJEmuThreadTests.cmake`, add the following, plus a header-list line `smoke_busy_threaded  a slow load call shows the busy state (log line headless)`:

```cmake
add_test(NAME smoke_busy_threaded
    COMMAND ${CMAKE_COMMAND} -E env VJ_TEST_SLOW_CALL_MS=400
            $<TARGET_FILE:virtualjaguar> --headless --emu-thread on --frames 30 "${VJ_ROMS}/yarc.j64")
set_tests_properties(smoke_busy_threaded PROPERTIES TIMEOUT 120 LABELS emu_thread
    PASS_REGULAR_EXPRESSION "\\[emu\\] busy: a call has taken over 250 ms"
    FAIL_REGULAR_EXPRESSION "headless: FAIL|owner violation|ThreadSanitizer")
```

- [ ] **Step 2: Run them and watch them fail**

Run: `cmake --build app/build -j8 2>&1 | tail -3`
Expected: compile errors (`setWaitHook`, `setUiWaiting`).

- [ ] **Step 3: Implement.**

`emu_thread.h`: add the public members:

```cpp
    static constexpr std::chrono::milliseconds kBusyAfter{250};
    // Called on the thread waiting in call() once it has waited kBusyAfter, then about every 16 ms until the call
    // returns (tick 0 first).  See the contract in docs/app-emulation-thread-plan-phase2.md (Task 7): it may pump OS
    // events and swap a frame drawn with raw GL; it must never open an ImGui frame, call()/post(), or read a field that
    // only a call() writes.  Never called inline, on the emulation thread, or re-entered.  Set before start().
    void setWaitHook(std::function<void(double waitedSec, unsigned tick)> hook) { waitHook_ = std::move(hook); }
```

Add the private member `std::function<void(double, unsigned)> waitHook_;`. Replace `call()`'s wait loop with:

```cpp
    // The task is in the queue.  An abandoned thread never answers it: poll for that instead of blocking in get().
    // Past kBusyAfter, the wait hook runs between slices (the UI's spinner).
    const auto t0 = std::chrono::steady_clock::now();
    unsigned tick = 0;
    for (;;) {
        const bool busy = waitHook_ && !g_inWaitHook && std::chrono::steady_clock::now() - t0 >= kBusyAfter;
        if (done.wait_for(busy ? std::chrono::milliseconds(16) : std::chrono::milliseconds(50)) ==
            std::future_status::ready)
            break;
        if (abandoned()) return R();
        if (waitHook_ && !g_inWaitHook && std::chrono::steady_clock::now() - t0 >= kBusyAfter) {
            g_inWaitHook = true;
            waitHook_(std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(), tick++);
            g_inWaitHook = false;
        }
    }
    return done.get();
```

Declare `g_inWaitHook` once, above the class in `emu_thread.h`: `inline thread_local bool g_inWaitHook = false;   // EmuThread::call: the wait hook is running on this thread`. A `thread_local` inside the template would give one flag per instantiation, and would not block re-entry from a `call()` with another result type.

`core_host.h`:
- Add `void setUiWaiting(bool w) { uiWaiting_.store(w); }` and the member `std::atomic<bool> uiWaiting_{false};`.
- Add `bool noteWaitingRead(const char* what) const;`. Implement it in the .cpp as `if (!uiWaiting_.load() || onOwner()) return false;`, then the same counting and log-once as `noteOffOwner`, with the label `"<what> while the UI waits in a call"`.
- Call `noteWaitingRead("<name>")` first in `isLoaded()`, `loadedPath()`, `loadedName()`, `stagedBios()`, `controllerPorts()`, `eventsActive()` and `debugIface()`. They are inline. Keep them inline, and make `noteWaitingRead` a const member defined in the .cpp.

`video.h` / `video.cpp`: add `drawBusy`, built from scissored clears with no shader state:

```cpp
// A "busy" frame while a call() holds the core (#967): the window cleared dark, eight blocks in a ring, one lit and
// turning.  Scissored clears only (no shader, no texture), so it is safe in the middle of an ImGui frame; the GL
// state it touches is saved and restored.
void VideoOut::drawBusy(int fbW, int fbH, double seconds) {
    GLint fb = 0, vp[4], sc[4];
    GLfloat cc[4];
    const GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &fb);
    glGetIntegerv(GL_VIEWPORT, vp);
    glGetIntegerv(GL_SCISSOR_BOX, sc);
    glGetFloatv(GL_COLOR_CLEAR_VALUE, cc);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glViewport(0, 0, fbW, fbH);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0.06f, 0.06f, 0.07f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_SCISSOR_TEST);
    const int d = std::max(4, std::min(fbW, fbH) / 40);
    const double r = d * 3.0;
    const int lit = (int)(seconds * 10.0) % 8;
    for (int i = 0; i < 8; i++) {
        const double a = i * 3.14159265358979 / 4.0;
        const int x = fbW / 2 + (int)(r * std::cos(a)) - d / 2;
        const int y = fbH / 2 + (int)(r * std::sin(a)) - d / 2;
        const float v = i == lit ? 0.9f : 0.3f;
        glScissor(x, y, d, d);
        glClearColor(v, v, v, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
    }
    if (!scissor) glDisable(GL_SCISSOR_TEST);
    glScissor(sc[0], sc[1], sc[2], sc[3]);
    glViewport(vp[0], vp[1], vp[2], vp[3]);
    glClearColor(cc[0], cc[1], cc[2], cc[3]);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)fb);
}
```

Use the GL headers and loader that `video.cpp` already includes.

`app.cpp`:
- `startEmuThread()`: before `emu.start(...)`, add `emu.setWaitHook([this](double waited, unsigned tick) { busyFrame(waited, tick); });`.
- `busyFrame`:

```cpp
// The emulation thread is busy with a call() the UI waits on (#967): keep the window alive and say so.  Called by
// EmuThread::call() on this thread -- possibly in the middle of an ImGui frame -- so: no ImGui, no call(), no event
// dispatch (SDL_PumpEvents only queues them), no read of a field only a call() writes (CoreHost counts those).
void App::busyFrame(double waited, unsigned tick) {
    if (tick == 0) std::fprintf(stderr, "[emu] busy: a call has taken over 250 ms\n");
    if (!window_ || !imguiUp_) return;
    host.setUiWaiting(true);
    SDL_PumpEvents();
    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(window_, &w, &h);
    if (w > 0 && h > 0) {
        video.drawBusy(w, h, waited);
        SDL_GL_SwapWindow(window_);
    }
    host.setUiWaiting(false);
}
```

- The slow-call test hook: in `startEmuThread()`, read `VJ_TEST_SLOW_CALL_MS` into a member `unsigned testSlowCallMs_` with `SDL_getenv`, clamped to 0..5000. At the start of `openContentOnCore`, the function that runs inside the load `call()`, add `if (testSlowCallMs_) SDL_Delay(testSlowCallMs_);   // VJ_TEST_SLOW_CALL_MS (smoke_busy_threaded)`.

- [ ] **Step 4: Run the tests**

Run: `cmake --build app/build -j8 2>&1 | tail -3; ctest --test-dir app/build -C Release -L emu_thread --output-on-failure -j4`
Expected: everything passes, including `smoke_busy_threaded` (`[emu] busy: a call has taken over 250 ms`), `emu_thread_unit` and `core_host_thread`. Re-run `emu_thread_unit` ten times (`ctest -R emu_thread_unit --repeat until-fail:10`): it must stay green.

- [ ] **Step 5: Commit**

```bash
git add app/src/emu_thread.h app/src/core_host.h app/src/core_host.cpp app/src/video.h app/src/video.cpp \
        app/src/app.h app/src/app.cpp app/tests/emu_thread_test.cpp app/tests/core_host_thread_test.cpp \
        app/cmake/VJEmuThreadTests.cmake
git commit -F - <<'EOF'
feat(app): a call() that takes over 250 ms shows a busy spinner (#967)

EmuThread::call() runs a wait hook on the waiting thread from 250 ms on; the app's hook pumps OS events and swaps a
spinner drawn with scissored clears -- no ImGui frame, no call(), no read of a field only a call() writes (CoreHost
counts those while the UI waits).  Headless, it logs once per call.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
```

### Task 8: Phase 1 minors: limits after a failed start, the vsync-off nap

**Files:**
- Modify: `app/src/app.h` / `app.cpp` (`liftFeatureLimits()`, `startEmuThread()` on failure, the `VJ_TEST_EMU_START_FAIL` hook, the threaded sleep block), `app/src/late_latch.h` (`idleNapNs`), `app/tests/late_latch_test.cpp`, `app/cmake/VJEmuThreadTests.cmake`

**Interfaces:**
- Produces:
  - `static uint64_t LateLatch::idleNapNs(uint64_t sinceSwapNs, uint64_t displayPeriodNs)`.
  - `void App::liftFeatureLimits()`.

- [ ] **Step 1: Write the failing tests.** In `app/tests/late_latch_test.cpp`, before `main`, add:

```cpp
// #967 final-review minor 3: with vsync off (or a swap that did not block), the threaded loop naps until the display's
// next refresh instead of a flat 4 ms (which presented at ~250 Hz for nothing).
static void testIdleNap() {
    const uint64_t ms = 1000000ull;
    const uint64_t p60 = 16666667ull;
    CHECK(vj::LateLatch::idleNapNs(0, p60) == p60);                  // just swapped: a whole period
    CHECK(vj::LateLatch::idleNapNs(10 * ms, p60) == p60 - 10 * ms);  // the rest of the period
    CHECK(vj::LateLatch::idleNapNs(16 * ms, p60) == 1 * ms);         // never under 1 ms (do not spin)
    CHECK(vj::LateLatch::idleNapNs(40 * ms, p60) == 1 * ms);         // late already: 1 ms
    CHECK(vj::LateLatch::idleNapNs(0, 0) == 4 * ms);                 // refresh unknown: the old 4 ms
}
```

Call it from `main`. Use the file's own `CHECK` macro, whatever form it has. In `app/cmake/VJEmuThreadTests.cmake`, add:

```cmake
add_test(NAME emu_thread_start_fallback
    COMMAND ${CMAKE_COMMAND} -E env VJ_TEST_EMU_START_FAIL=1
            $<TARGET_FILE:virtualjaguar> --headless --emu-thread on --frames 5 "${VJ_ROMS}/yarc.j64")
set_tests_properties(emu_thread_start_fallback PROPERTIES TIMEOUT 120 LABELS emu_thread
    PASS_REGULAR_EXPRESSION "the debugger tools and the GDB stub are back on .the emulation thread did not start."
    FAIL_REGULAR_EXPRESSION "headless: FAIL|owner violation|ThreadSanitizer")
```

- [ ] **Step 2: Run them and watch them fail**

Run: `cmake --build app/build --target vj_late_latch_test 2>&1 | tail -3; ctest --test-dir app/build -C Release -R emu_thread_start_fallback --output-on-failure`
Expected: `idleNapNs` is undefined, and the fallback test fails because the env var is ignored.

- [ ] **Step 3: Implement.**

`late_latch.h`, in `LateLatch`:

```cpp
    // The threaded loop's nap when vsync is off or the swap did not block: until the display's next refresh, at least
    // 1 ms; a flat 4 ms when the refresh rate is unknown (0).
    static uint64_t idleNapNs(uint64_t sinceSwapNs, uint64_t displayPeriodNs) {
        if (!displayPeriodNs) return 4000000ull;
        const uint64_t left = sinceSwapNs < displayPeriodNs ? displayPeriodNs - sinceSwapNs : 0;
        return left < 1000000ull ? 1000000ull : left;
    }
```

`app.cpp`, in the threaded sleep block, replace `SDL_DelayNS(4000000);` with `SDL_DelayNS(LateLatch::idleNapNs(SDL_GetTicksNS() - swap1, displayPeriodNs()));`. Here `swap1` is the swap end the loop already records. Add `uint64_t App::displayPeriodNs()`: `1e9 / refresh_rate` of `SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(window_))`, cached and refreshed when the window moves to another display (`SDL_EVENT_WINDOW_DISPLAY_CHANGED`), or 0 when unknown.

`liftFeatureLimits()` undoes `limitFeaturesForEmuThread()`. Nothing is loaded yet when it runs, because `startEmuThread()` comes before any content.

```cpp
void App::liftFeatureLimits() {
    debug.setThreadBlocked(false);
    host.announceDebug = true;
    host.optionLock = [this](const std::string& key, std::string& value) {   // wireHost()'s rule
        return lockedCoreOptionFor(key, value, {{proPorts[0].load(), proPorts[1].load()}});
    };
    std::fprintf(stderr, "[emu] the debugger tools and the GDB stub are back on (the emulation thread did not start)\n");
}
```

Use exactly the `optionLock` lambda `wireHost()` installs. If `wireHost()` has one named helper for it, call that instead of repeating the lambda.

In `startEmuThread()`'s failure branch, after `host.post = nullptr;`, add `emuThreadThisRun_ = false; liftFeatureLimits();`. With `emuThreadThisRun_` false, Settings shows "Restart the app to apply" again while the saved choice is On. Before `emu.start(...)`, add `if (SDL_getenv("VJ_TEST_EMU_START_FAIL")) { /* the start-failure path, for emu_thread_start_fallback */ }`, which jumps straight into the failure branch.

- [ ] **Step 4: Run the tests**

Run: `cmake --build app/build -j8 2>&1 | tail -3; ctest --test-dir app/build -C Release -L emu_thread --output-on-failure -j4`
Expected: everything passes. `emu_thread_start_fallback` prints the "back on" line and `headless: OK (1 load(s))` with no "emulation thread" suffix.

- [ ] **Step 5: Commit**

```bash
git add app/src/app.h app/src/app.cpp app/src/late_latch.h app/tests/late_latch_test.cpp app/cmake/VJEmuThreadTests.cmake
git commit -F - <<'EOF'
fix(app): a failed emulation-thread start lifts the feature limits; vsync off naps to the next refresh (#967)

Phase 1 final-review minors 2 and 3.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
```

### Task 9: MinGW: precise short waits, and the timer report in CI

**Files:**
- Create: `app/src/precise_sleep.h`
- Modify: `app/src/emu_commands.h` (the `__MINGW32__` branch of `waitUntil`), `app/tests/emu_thread_test.cpp` (`measureTimers`), `.github/workflows/app.yml`

**Interfaces:**
- Produces: `void vj::preciseSleepNs(uint64_t ns)`.

- [ ] **Step 1: Read the verdict that is available.**

Run: `gh run list --repo libretro/virtualjaguar-libretro --workflow app.yml --branch develop --limit 5 --json databaseId,headSha,conclusion` and, for the newest completed run, `gh run view <id> --repo libretro/virtualjaguar-libretro --json jobs --jq '.jobs[]|select(.name|test("MinGW"))|"\(.databaseId) \(.conclusion)"'`. Then fetch each MinGW job's log: `gh api --allow-escape-sequences repos/libretro/virtualjaguar-libretro/actions/jobs/<job>/logs | grep -a -E "emu_thread_unit|note:|fast-forward"`.

Expected: `emu_thread_unit ... Passed`, and probably no `note:` line, because ctest hides the output of a test that passes. Record what you see in the commit message: passed, failed with the ratio, or never ran.

- [ ] **Step 2: Write the failing test.** In `emu_thread_test.cpp`, change `measureTimers()` so it also measures `vj::preciseSleepNs(1000000)` 20 times. Add `double preciseMs` to `TimerCaps`, print it in the `note: timers here:` line, and include `#include "precise_sleep.h"`. Add the check:

```cpp
    CHECK(caps.preciseMs < 4.0, "preciseSleepNs(1 ms) takes %.2f ms on average", caps.preciseMs);
```

Leave the existing "not checked" logic for the fast-forward ratio in place. On a host where even the precise wait is coarse, the CHECK above is the honest failure.

- [ ] **Step 3: Run it and watch it fail**

Run: `cmake --build app/build --target vj_emu_thread_test 2>&1 | tail -3`
Expected: `precise_sleep.h` is not found.

- [ ] **Step 4: Implement `app/src/precise_sleep.h`.**

```cpp
// precise_sleep.h -- a short sleep that honours sub-tick durations (#967).  Windows: a high-resolution waitable
// timer (Windows 10 1803+), whatever the process's timer resolution -- MinGW's sleeps and timed waits otherwise
// round up to the ~15.6 ms scheduler tick, which held threaded fast-forward at normal speed on the MinGW runner.
// Falls back to a plain waitable timer, then Sleep(1).  Elsewhere: std::this_thread::sleep_for.
#pragma once

#include <chrono>
#include <cstdint>
#include <thread>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif
#endif

namespace vj {

inline void preciseSleepNs(uint64_t ns) {
#ifdef _WIN32
    struct Timer {
        HANDLE h = nullptr;
        Timer() {
            h = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
            if (!h) h = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS);
        }
        ~Timer() {
            if (h) CloseHandle(h);
        }
    };
    thread_local Timer t;
    if (t.h) {
        LARGE_INTEGER due;
        due.QuadPart = -(LONGLONG)(ns / 100);   // relative, in 100 ns units
        if (due.QuadPart == 0) due.QuadPart = -1;
        if (SetWaitableTimer(t.h, &due, 0, nullptr, nullptr, FALSE)) {
            WaitForSingleObject(t.h, INFINITE);
            return;
        }
    }
    Sleep(1);
#else
    std::this_thread::sleep_for(std::chrono::nanoseconds(ns));
#endif
}

}  // namespace vj
```

`emu_commands.h`: include `precise_sleep.h`. In the `__MINGW32__` branch, replace `std::this_thread::sleep_for(std::chrono::milliseconds(1));` with `vj::preciseSleepNs(1000000);`. Update the comment there: the poll now uses a high-resolution timer, because MinGW's `Sleep` and `sleep_for` honour the 1 ms timer resolution only on some hosts.

`.github/workflows/app.yml`: right after the "Smoke tests (headless, SDL dummy drivers)" step, add a step that every app job runs, in the job's default shell:

```yaml
    # Informational: emu_thread_unit's timer notes (sleep precision, fast-forward rate).  ctest shows a passing
    # test's output only with --verbose; this is where a coarse-timer runner (MinGW) is visible (#967).
    - name: Emulation-thread timer report
      if: always()
      run: ctest --test-dir app/build -C Release -R "^emu_thread_unit$" --verbose
```

- [ ] **Step 5: Run the tests**

Run: `cmake --build app/build -j8 2>&1 | tail -3; ctest --test-dir app/build -C Release -R "emu_thread_unit|emu_commands_unit" --verbose 2>&1 | grep -E "note:|Passed|Failed"`
Expected: both pass, and the note prints `preciseSleepNs(1 ms) takes ~1.1 ms` on macOS. The real verdict is the PR's CI: the MinGW x64 and ARM64 jobs print their timer report, `emu_thread_unit` passes, and the fast-forward ratio is checked there (no "not checked" note).

- [ ] **Step 6: Commit**

```bash
git add app/src/precise_sleep.h app/src/emu_commands.h app/tests/emu_thread_test.cpp .github/workflows/app.yml
git commit -F - <<'EOF'
fix(app): high-resolution short waits for the emulation thread on Windows (#967)

The MinGW runner paced threaded fast-forward at one frame per ~15.6 ms (63.4 vs 96.6 frames/s); <what Step 1 found
on develop's newest run>.  preciseSleepNs uses a high-resolution waitable timer, and every app job now prints
emu_thread_unit's timer report.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
```

### Task 10: Documentation

**Files:**
- Modify: `app/README.md` (the `## Emulation thread` section, the `## Layout` table), `docs/agent/testing.md` (the emu_thread test list, the twins, `VJ_TEST_SLOW_CALL_MS`, `VJ_TEST_EMU_START_FAIL`), `docs/app-emulation-thread-design.md` (dated amendments)

**Interfaces:** none.

- [ ] **Step 1: README.** In `## Emulation thread`, replace the "While it is on, this version switches off ..." paragraph with:

```markdown
While it is on, this version switches off the Tools (debugger) windows and the GDB stub option; they move to the
emulation thread in the next phase. RetroAchievements and netplay work as usual: achievements are checked on the
emulation thread after every frame, and link play's peer waits block only that thread. A load or a state save that
takes longer than a quarter of a second shows a spinner instead of freezing the window. A log line
`[core_host] owner violation: ...` means a UI path still called into the core directly: that is a bug, please report it.
```

In `## Layout`, add the following rows in the table's format:

```markdown
| `src/emu_snapshot.h` | what the emulation thread publishes for the UI: the per-frame snapshot and the core-event stream (log lines, events, option writes) |
| `src/ach_event.*` | an rc_client event copied, so it can be applied on the UI thread |
| `src/precise_sleep.h` | short sleeps that honour sub-tick durations (a high-resolution waitable timer on Windows) |
```

- [ ] **Step 2: testing.md.** In the emulation-thread section, list:
  - `core_events_unit` and `ach_event_unit`;
  - the `ra_*_threaded` twins, `ra_offthread_validation_threaded`, and how `vj_ra_test()` registers them;
  - `netplay_pair_threaded`, `netplay_pair_voicemodem_threaded`, `netplay_pair_mixed` and `netplay_no_partner_threaded`;
  - `smoke_busy_threaded` with `VJ_TEST_SLOW_CALL_MS`;
  - `emu_thread_start_fallback` with `VJ_TEST_EMU_START_FAIL`.

  Remove `netplay_refused_threaded`.

- [ ] **Step 3: Design doc amendments.** Each one is dated 2026-10-18 and in the doc's existing amendment style:
  - **Section 5**, after "Callbacks that write app state today change to:": the core-event stream (`emu_snapshot.h`) carries log lines, events and owner option writes; `onLog` / `onEvent` stay synchronous on the core's thread; `onUiEvent` is the UI-side channel.
  - **Section 5, Achievements:** idle and rich presence run on the emulation thread too; `read_memory` off it answers from the map; events are queued as `AchEvent`s and applied in `pump()`.
  - **Section 6, Link play:** Netplay stays a UI-thread object; the bounce is counted in emulated frames.
  - **Section 7, Slow calls:** the hook's contract, and that the picture is replaced by the spinner while it shows.

- [ ] **Step 4: Check**

Run: `grep -n "emu_snapshot\|ach_event\|precise_sleep\|spinner" app/README.md docs/agent/testing.md docs/app-emulation-thread-design.md`
Expected: each new name appears where it was added.

- [ ] **Step 5: Commit**

```bash
git add app/README.md docs/agent/testing.md docs/app-emulation-thread-design.md
git commit -F - <<'EOF'
docs(app): emulation thread phase 2 -- achievements, netplay, the busy spinner (#967)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
```

### Task 11: Studio checks and the PR

Spec section 9's Phase 2 checks:
- the windowed check with achievements and netplay on;
- presentation evenness with `rc_client_do_frame` on the emulation thread;
- link play between two threaded copies.

Windowed runs happen on the Studio only.

**Studio notes** (memory: reference_lan_build_runner; Phase 1 ledger rulings R20 and the evenness session):
- `vjr.sh run` `%q`-quotes each argument and exports `DEVELOPER_DIR=CommandLineTools`, which breaks app cmake. Run `bash $VJR run <tag> env -u DEVELOPER_DIR bash -lc '<script>'`.
- A sleeping display blocks the vsync swap forever. `vj-eval/remote/even-run.sh` wakes it, gates on load and kills hangs: use it for the evenness runs.
- CI runners on the Studio (ghrunner) can push the load to 20-100 without warning. Check `uptime` before each arm and wait while it is over 2.

**Files:** none in the repository. The results go into the PR description.

- [ ] **Step 1: Ship and build.**

```bash
VJR=~/Workspace/Provenance/vj-eval/remote/vjr.sh
bash $VJR push emu-p2 HEAD
bash $VJR run emu-p2 env -u DEVELOPER_DIR bash -lc 'uptime; cmake -S app -B app/build -DCMAKE_BUILD_TYPE=Release >/dev/null && cmake --build app/build -j14 >/dev/null && ctest --test-dir app/build -C Release -E shaders_gl -j6 2>&1 | tail -5'
```

Expected: the full suite passes on the Studio, including every new test.

- [ ] **Step 2: Windowed check, both features on.** Run `app/tests/emu_thread_windowed_check.sh` as in Phase 1, once with `--ra-mock app/tests/ra` added to the app's arguments. Then run the app windowed with `--emu-thread on --netplay host --netplay-port <free> yarc.j64` next to a headless `--emu-thread on --netplay join ...` partner for 20 s, and grep the log.
Expected: no `owner violation`, no `ThreadSanitizer`. The RA run logs the mock login and the game load. The netplay run logs `[NETLINK] link UP`. The Netplay window, opened through the TV-free desktop menu, shows "Connected".

Repeat the RA run on a ThreadSanitizer build of the app (`-DVJ_APP_TSAN=ON`, as Phase 1's windowed TSan pass). The headless twins pump RA while the emulation thread is parked between `advance()`s, so only a free-running window runs `rc_client_do_frame` concurrently with the UI's HTTP pump and its `rc_client_*` readers. Any report there is handled as in Task 6 Step 4: route the reader through a published value, never a suppression.

- [ ] **Step 3: Evenness with achievements active.** Run `even-run.sh` A/B/B/A on yarc at 60 Hz, 3600 UI frames per run:
  - A = threaded, no RA;
  - B = threaded with `--ra-mock app/tests/ra`.

  Summarise each run with `app/tests/perf_trace_summary.py` (shown repeats/skips per minute, exact event -> present) and the emulation thread's frame cost (`VJ_PERF_TRACE` end - start).
Expected: B within the run-to-run spread of A on repeats + skips per minute (the #1018 numbers: yarc 12 + 31.5/min), and a frame-cost increase under 0.2 ms. A larger cost, or worse evenness, goes into the PR as a finding with its numbers. It is not tuned away in this task.

- [ ] **Step 4: Link play stats.** On the Studio, run `vj_netplay_test pair` twice, inline (`off off`) and threaded (`on on`), with `--verbose` added to both children's `appArgs` extras. Leave that edit local, not committed. Collect the last `[NETLINK] link stats` line from each child: hops/s, slipped hops and timeouts.
Expected: threaded slipped hops and timeouts no worse than inline. Record both lines.

- [ ] **Step 5: Open the PR.** Write the body to `~/Workspace/Provenance/vj-eval/remote/emu-p2/pr-body.md`. Fill every cell from Steps 1-4.

```markdown
Refs #967 (phase 2 of 5: docs/app-emulation-thread-plan-phase2.md, design: docs/app-emulation-thread-design.md)

With Settings > Interface > Emulation thread on, RetroAchievements and netplay now work; only the debugger Tools and
the GDB stub stay off (phase 3). Still off by default in every build.

- Core-event stream (`emu_snapshot.h`): the core's log lines, its events and option writes made on the emulation
  thread reach the UI thread in order at each pin; the config is only ever written there.
- RetroAchievements: `rc_client_do_frame` (and idle, and rich presence) on the emulation thread; events copied and
  applied in the UI pump; core RAM never read off the emulation thread.
- Netplay: a UI-thread object fed by the stream, with an epoch per start/stop; the endpoint bounce counts emulated
  frames.
- A `call()` over 250 ms shows a spinner instead of freezing the window.
- Phase 1 leftovers: limits lifted after a failed thread start; vsync-off naps to the next refresh; high-resolution
  short waits on Windows (MinGW fast-forward).

## Studio (M4 Max, 60 Hz)

| check | result |
|---|---|
| full suite | |
| windowed, RA mock | |
| windowed, netplay host + headless join | |
| yarc repeats + skips / min, threaded [runs] | |
| yarc repeats + skips / min, threaded + RA [runs] | |
| emulation-thread frame cost, without / with RA (ms) | |
| link stats inline: hops/s, slipped, timeouts | |
| link stats threaded: hops/s, slipped, timeouts | |

MinGW timer report: <from this PR's CI: the MinGW jobs' `note: timers here:` lines>.

🤖 Generated with [Claude Code](https://claude.com/claude-code)
```

```bash
git push -u libretro feat/967-emu-thread-phase2
gh pr create --repo libretro/virtualjaguar-libretro --base develop --head feat/967-emu-thread-phase2 \
  --title "feat(app): emulation thread phase 2 -- achievements, netplay, busy spinner (#967)" \
  --body-file ~/Workspace/Provenance/vj-eval/remote/emu-p2/pr-body.md
```

Expected: CI goes green. The jobs to watch are every `App *` job (with the timer report on the Windows ones), `ThreadSanitizer (emulation thread)` and `pr-issue-link`. The PR is not merged by the agent.

## Follow-up plans

**Phase 3: the debugger halt, debug-view requests, the GDB stub, nightlies default On.** Unchanged from Phase 1's follow-up text.
- A `halt_pump` on the emulation thread.
- Per-frame debug-view copy requests.
- The GDB socket loop off the UI thread (#936).
- `call()`'s shutdown path detaches a halt first.
- `VJ_BUILD_CHANNEL`, so nightlies default the setting On.

Two additions from this phase:
- The wait hook's contract covers a halt: a `call()` behind a halted core waits until Continue, and the spinner shows meanwhile. Phase 3 replaces that with "applies after Continue" for the commands spec section 6 names.
- The busy frame should redraw the game picture behind the spinner. That needs a draw path that does not depend on ImGui's frame, which the halt redesign brings.

**Phase 4 (mode B) and Phase 5 (default On, retire the old loop):** unchanged from Phase 1's follow-up text.
