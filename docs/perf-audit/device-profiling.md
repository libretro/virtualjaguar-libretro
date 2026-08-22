> Raw sub-agent audit output (2026-08-22, read-only, sonnet). Line numbers refer to
> `libretro/develop` @ 5f898da. Verified items are promoted to [`../perf-audit-2026-08.md`](../perf-audit-2026-08.md);
> treat anything here that is NOT in that file as unverified.

# Audit: is the existing device-perf tooling enough to find hot FUNCTIONS on an A10X iPad, or do we need Instruments?

Repo: `/Users/jmattiello/Workspace/Provenance/virtualjaguar-libretro/.claude/worktrees/alien-predator-save-state-ad621b`
RetroArch checkout: `/Users/jmattiello/Workspace/Provenance/RetroArch`
Both read-only for this audit; nothing was built, edited, or deployed.

---

## 1. What the perf-counter path (`RETRO_ENVIRONMENT_GET_PERF_INTERFACE`) actually delivers

**It is subsystem-level millisecond/tick totals only. It cannot attribute time to a function or a line, and it was never designed to.**

Evidence:

- `src/core/perf_iface.h` (lines 1-16, 89-99): 8 fixed slots (`VJP_M68K, VJP_GPU, VJP_DSP, VJP_GPU_SYNC, VJP_DSP_SYNC, VJP_OP, VJP_BLITTER, VJP_DAC`). Each slot brackets a whole *slice* — "one GPU execution span, every entry path", "one blit, whichever engine" — not a function or opcode. The header says outright: "every probe brackets a SLICE ... never an interpreter inner loop."
- `src/core/perf_iface.c`: `VJPerfEnter`/`VJPerfLeave` just call the frontend's `perf_start`/`perf_stop` on a `struct retro_perf_counter`, i.e. two `clock`-style timestamps per span. No call-stack, no PC, no line info is captured — the counter is literally a name string + accumulated ticks + run count (`struct retro_perf_counter` in libretro.h).
- `docs/profiling.md`, "The counters" table: 8 rows, one line each — `vj_gpu_exec` = "All GPU RISC execution, every entry path". You get "how much time GPU execution took in total", not "which opcode/branch in `GPUExec`/`executeOpcode` was expensive."

So for the stated goal — hot **functions/lines** inside `GPUExec`/`executeOpcode` (`src/tom/gpu.c`), `DSPExec` (`src/jerry/dsp.c`), the blitter (`src/tom/blitter.c`), memory dispatch (`src/core/jaguar.c`) — the counter path answers "which of these ~8 buckets is expensive", not "which function/line inside the bucket". Getting finer would require adding new counter slots by hand for every candidate hot spot (the doc's "Adding a counter" section), which is a guess-and-recompile loop, not a profiler.

**Known traps** (both documented in `docs/profiling.md` and re-stated verbatim as comments in `test/tools/device_perf.sh` lines ~17-45):

1. **`perfcnt_enable` trap.** RetroArch's `runloop.c` always accepts registration (`vjPerfActive=1`), but gates `start`/`stop` and the final `perf_log()` behind its own `perfcnt_enable` setting. Off means every counter reads **zero**, indistinguishable from a broken core. `device_perf.sh cfg` exists solely to force `perfcnt_enable = "true"` into `retroarch.cfg` before capture.
2. **Avg-not-total trap.** RetroArch's log format is `"[PERF] Avg (%s): %llu ticks, %llu runs.\n"` — an average, not a total. Ranking by the printed average inverts the real comparison (blitter: few, expensive calls; GPU: many, cheap ones — printed averages get the ranking backwards). `device_perf.sh report`'s awk parser explicitly multiplies `avg × runs` back into a total before ranking (`test/tools/device_perf.sh`, `cmd_report`).

Two more caveats worth carrying forward: enabling counters costs ~2,000 extra indirect calls/frame (negligible per-slice but non-zero — don't compare counters-on vs counters-off for absolute speed), and the counters deliberately **overlap** (GPU cycles pulled inline into `vj_m68k_slice` via `GPUSyncToM68K`, and into `vj_op_halfline` via the Object Processor's inline GPU calls) — they are not a pie chart and `report`'s awk decomposes the nesting rather than summing it.

**Can `device_perf.sh` target an iPad (iOS), not just tvOS?** Yes, iOS is a first-class, symmetric destination, not an afterthought:

- `resolve_platform()` in `test/tools/device_perf.sh` (~line 96-113) has an explicit `ios|iOS` branch: `MK_PLATFORM=ios-arm64`, `DYLIB=virtualjaguar_libretro_ios.dylib`, `MODULES="$RA_DIR/pkg/apple/iOS/modules"`, `SCHEME="RetroArch iOS Release"`, `SDK=iphoneos`, `DEST_PLATFORM=iOS`.
- `cmd_install`'s `xcodebuild -destination "platform=$DEST_PLATFORM,name=$DEVICE"` uses `DEST_PLATFORM` (the destination namespace `iOS`/`tvOS`), not the SDK name — a bug class this script explicitly guards against (see the comment in `resolve_platform`, and the sibling memory note `project_540...` / commit "fix(test): use the destination namespace, not the SDK name, for tvOS" in recent git log — same fix class applies symmetrically to iOS since it shares the same `resolve_platform` function).
- `-d`/`--device` selection is device-name/UDID generic (`need_device()`), not platform-specific.
- **Nothing obviously broken for iOS in the script text.** It is exercised in the codebase primarily through tvOS in the docs and the recent PR #563 (see `project_509_device_perf_capture` memory: "two SILENT RetroArch traps ... GPU RISC is ~74%, blitter ~17%; A10X still unavailable") — i.e., the iOS path is implemented but **the A10X capture itself has not actually been run yet** per that memory note. That's a "never exercised on the goal device" gap, not a code defect visible from reading the script.

---

## 2. What Instruments Time Profiler needs on device

### (a) Core dylib

- `make platform=ios-arm64 RELEASE_DEBUG_INFO=1` — confirmed this keeps **both** optimization and debug info:
  - `Makefile` line ~91-97: for `platform=ios-arm64`, `OPT_LEVEL` defaults to `-O3` (it's in `OPT_O3_PLATFORMS`, Makefile line 70-71, "osx / ios-arm64 / ios9 / tvos-arm64 are that measurement plus the same clang and the same architecture", referencing the #515 macOS-arm64 -O3 measurement).
  - `Makefile` line ~826-828 (`else: FLAGS += $(OPT_LEVEL) -DNDEBUG`) applies `-O3 -DNDEBUG` for the ios-arm64 release path.
  - `Makefile` line ~833-841: `ifeq ($(RELEASE_DEBUG_INFO),1) ... FLAGS += -g` — appended on top, additively, with no `-O` override afterward. So the produced dylib is `-O3 -DNDEBUG -g`: optimized code with DWARF line/symbol info, i.e. exactly what Instruments needs for a representative-perf-but-symbolicated capture. (`-g` alone does not disable optimizations; it just keeps debug info describing the optimized code, with some inlining/variable-visibility loss that's normal for release-profiling and doesn't materially hurt function-level attribution.)
  - The `ios-arm64` platform block itself (`Makefile` lines 278-290) sets `CC = cc -arch arm64 -isysroot $(IOSSDK)`, `MINVERSION = -miphoneos-version-min=8.0` (it's in the `ios9|ios-arm64` filter) — nothing there strips or alters debug info.
  - **No `strip` or `dsymutil` call anywhere in `Makefile`** (grepped the whole file — the only hits are a comment about it and the unrelated `$(strip ...)` Make function). The core build produces a plain, unstripped `.dylib`; any stripping/dSYM generation is left entirely to downstream tooling.

- **Is the dylib stripped later?** No, by neither of the two candidate steps:
  - `pkg/apple/code-sign-cores.sh` (read in full) only runs `codesign --force --sign ...` over every `.dylib`/`.framework`/`.bundle` under `iOS/modules` or `tvOS/modules`. It does not strip symbols; codesigning a binary does not remove its symbol table or DWARF sections.
  - `device_perf.sh cmd_install` calls `xcodebuild ... build` (not `archive`/`install`) on the `RetroArch_iOS13.xcodeproj` "RetroArch iOS Release" scheme. Xcode's copy/strip phase (`STRIP_INSTALLED_PRODUCT`, `DEPLOYMENT_POSTPROCESSING`) is **not set anywhere** in `project.pbxproj` for the RetroArch app target or at the project level (grepped the whole file: zero hits for `STRIP_INSTALLED_PRODUCT`, zero for `DEPLOYMENT_POSTPROCESSING`). Xcode's default is that `DEPLOYMENT_POSTPROCESSING` (which gates `STRIP_INSTALLED_PRODUCT`) only fires on an **install/archive** action, not a plain `build`/Run action — so a normal `xcodebuild build` or Xcode ▶ Run leaves the app's embedded frameworks/dylibs (including the injected core) unstripped. Corroborating: the project's own top-level Release config explicitly sets `COPY_PHASE_STRIP = NO` (`project.pbxproj` line 1888, in the project-level `96AFAE5316C1D4EA009DE44C /* Release */` config), and the RetroArch app target's own Release block (`9204BE2A1D319EF300BD49DB`) adds no override.
  - The one `DEBUG_INFORMATION_FORMAT = "dwarf-with-dsym"` hit in the whole `project.pbxproj` (line 1671) belongs to an unrelated helper target ("Rebuild assets.zip"), not the RetroArch app or the core. The app target has no explicit `DEBUG_INFORMATION_FORMAT`, so Xcode's built-in default applies (`dwarf-with-dsym` for Release configs on any reasonably modern Xcode) — meaning a Release build of the **app itself** does get a dSYM by default; the **core dylib**, built by our own `make`, does not, unless you explicitly run `dsymutil` on it (see recipe below).

- **Can `dsymutil` produce a dSYM for the core, and will Instruments symbolicate it?** Yes to both, mechanically:
  - `dsymutil path/to/virtualjaguar_libretro_ios.dylib -o virtualjaguar_libretro_ios.dylib.dSYM` builds the dSYM bundle straight from the unstripped, `-g`-built dylib's DWARF.
  - Instruments/`xctrace symbolicate` matches purely by **Mach-O UUID** (`LC_UUID` load command), embedded at link time and identical between the shipped dylib and its dSYM as long as you don't rebuild/relink in between generating the two. As long as the dylib inside the `.app` bundle that's actually running on the iPad has the same UUID as the dSYM you hand Instruments (i.e., don't `make clean && make` again after `dsymutil` — that changes the UUID), Instruments resolves symbols for a `dlopen`'d dylib the same way it does for any other loaded image; there's nothing libretro/dlopen-specific that defeats this — Instruments/Time Profiler samples every loaded image in the target process, not just its main executable, and dSYM matching is opaque to how the image got mapped. Practical mechanism: put the dSYM in Xcode's "Symbols" search paths / next to the binary, or drag it onto the already-recorded trace.

### (b) RetroArch app build for the actual profiling run

- **Must it be Debug or Release?** Confirmed from the actual scheme file (`RetroArch_iOS13.xcodeproj/xcshareddata/xcschemes/RetroArch iOS Release.xcscheme`, read in full):
  - `<ProfileAction buildConfiguration = "Release" ... >` — Xcode's Product ▶ Profile action on this scheme **always builds Release**, regardless of what the Run/Launch action is set to. This is exactly what you want: representative perf, not `-O0` debug-build numbers.
  - `<LaunchAction buildConfiguration = "Release" ... selectedDebuggerIdentifier = "" ... runnableDebuggingMode = "0">` — even the normal Run action on this scheme launches Release with no debugger attached (`selectedDebuggerIdentifier=""`), so ordinary "Run" and "Profile" both give you Release-config, non-debugger-attached runs. Good: nothing about this scheme's config needs changing to get a valid profiling build.
- **Does Release strip?** As established in 2(a): no — `COPY_PHASE_STRIP=NO` project-wide, no `STRIP_INSTALLED_PRODUCT`/`DEPLOYMENT_POSTPROCESSING` overrides, and Product ▶ Profile does a `build`-class action (via Instruments' own launch), not an archive/install, so the app and its embedded core dylib keep symbols.
- **Does Instruments need the "Debug executable"/`get-task-allow` entitlement, i.e. a development-signed build on that device?** Yes, this is standard iOS/Xcode behavior (not something visible as a literal source line in this repo — none of the four `.entitlements` files under `pkg/apple/` (`RetroArch.entitlements`, `RetroArchAppStore.entitlements`, `RetroArchCg.entitlements`, `RetroArchiOS9.entitlements`) declare `get-task-allow` explicitly, because Xcode injects it automatically into the provisioning profile/signature at sign time whenever the app is signed with a **Development** (not Distribution/App Store/Ad Hoc/Enterprise) signing identity). Practically: the build must be run/installed to the iPad **from Xcode with your Apple Developer account's Development certificate** (which `device_perf.sh install` already does via `-allowProvisioningUpdates`, `DEVELOPMENT_TEAM = S32Z3HMYVQ` set on the tvOS extension target and `S32Z3HMYVQ`/`UK699V5ZS8` elsewhere in the project). A TestFlight/Ad-Hoc/enterprise-distributed build will refuse Instruments attach because it lacks `get-task-allow`.

---

## 3. Concrete step-by-step recipe

### Build the symbolized core

```bash
cd /path/to/virtualjaguar-libretro
# ios-arm64 SDK path resolves via xcodebuild, needs full Xcode (not CommandLineTools)
make platform=ios-arm64 RELEASE_DEBUG_INFO=1 -j"$(getconf _NPROCESSORS_ONLN)"
ls -la virtualjaguar_libretro_ios.dylib

# dSYM from the freshly-built, still-unstripped dylib -- do this BEFORE
# any further make/link touches the file, since dsymutil's match is by
# Mach-O UUID and a relink changes it.
dsymutil virtualjaguar_libretro_ios.dylib -o virtualjaguar_libretro_ios.dylib.dSYM
```

### Sign / inject into the RetroArch app

```bash
export VJ_RETROARCH_DIR=/Users/jmattiello/Workspace/Provenance/RetroArch
test/tools/device_perf.sh doctor                 # confirm the iPad shows "connected" (not "unavailable")
test/tools/device_perf.sh build   ios             # re-does the make above + build-identity assert
test/tools/device_perf.sh install ios -d "<iPad name or UDID>"
```
`install` copies the dylib into `pkg/apple/iOS/modules`, runs `code-sign-cores.sh` (signing only, no stripping — see §2a), then `xcodebuild -scheme "RetroArch iOS Release" -destination "platform=iOS,name=<device>" build` to build+install the app itself.

### Build + install RetroArch to the iPad from Xcode (equivalent, if doing it by hand instead of the script)

1. Open `pkg/apple/RetroArch_iOS13.xcodeproj` in Xcode.
2. Scheme selector (top bar): choose **"RetroArch iOS Release"**.
3. Destination device selector: choose the physical iPad Pro (must show as trusted/connected, not "unavailable").
4. **Product ▶ Profile** (⌘I) — this builds Release (per the scheme's `ProfileAction`, confirmed above), installs, and launches Instruments with a template picker.
5. Choose the **Time Profiler** template.

*Needs the user physically present*: unlocking/trusting the iPad the first time a Mac connects to it (an on-device "Trust This Computer?" tap), and starting/interacting with the actual gameplay in RetroArch once it launches under Instruments (loading the Jaguar ROM, navigating menus, playing). Everything else — build, sign, install, start/stop the trace — can be scripted or driven from Xcode without hands-on-device interaction.

### Capture

6. In Instruments' Time Profiler window, hit **Record** (red circle) if it didn't auto-start.
7. On the iPad: load Virtual Jaguar core, load a ROM, play real gameplay for 20-30s (per the project's own guidance in `test/tools/device_perf.sh cmd_capture`: "not a menu, not an attract loop" — representativeness matters).
8. Stop the recording (red square) in Instruments.

### Read the results

9. **Symbolication**: Instruments ▶ File ▶ Symbols (or the info panel) — point it at `virtualjaguar_libretro_ios.dylib.dSYM` if it isn't auto-found (Spotlight/dSYM search paths). Xcode auto-locates dSYMs it built itself (the app's own dSYM, via the default `dwarf-with-dsym` setting established in §2), but the **core's** dSYM was built by hand with the standalone `dsymutil` step, so may need manual pointing the first time.
10. **Call Tree pane** (bottom of the Time Profiler track): checkboxes for
    - **"Invert Call Tree"** — flips the tree root-first from leaves, surfacing the actual hot leaf functions (e.g. `GPUExec`, `BlitterMidsummer2`) at the top instead of burying them under `main`/event-loop frames.
    - **"Hide System Libraries"** — cuts kernel/libsystem noise, leaves core + RetroArch frames.
    - **"Flatten Recursion"** — merges recursive call frames of the same function so a deeply-recursive path doesn't fragment its time across many tree rows.
11. Right-click any frame ▶ **"Show in Heaviest Stack Trace"** (or select the sample and open the detail/heaviest-stack view) to see the single most-expensive call path start to finish — the fastest way to find e.g. "`GPUExec` → `executeOpcode` case X → `JaguarReadByte`" as the actual expensive path rather than an aggregate bucket.
12. Zoom into a time range in the track view to isolate one gameplay segment (e.g. a specific slow scene) instead of averaging the whole capture.

### `xctrace` CLI alternative (scriptable, no GUI)

Confirmed available on this Mac (`/usr/bin/xctrace`, part of Xcode command-line tools) and usable exactly as the user's spec suggests:

```bash
# Find the device UDID first
xcrun xctrace list devices

xcrun xctrace record --template "Time Profiler" \
  --device <UDID> \
  --attach RetroArch \
  --time-limit 30s \
  --output run.trace

# Symbolicate against the core's manually-built dSYM (app's own dSYM is
# usually auto-resolved from its build folder / Xcode's DerivedData index)
xcrun xctrace symbolicate --input run.trace \
  --dsym virtualjaguar_libretro_ios.dylib.dSYM

# Export the call tree / heaviest stack to XML for offline/scripted reading
xcrun xctrace export --input run.trace \
  --xpath '/trace-toc/run[@number="1"]/data/table[@schema="time-profile"]' \
  --output run.xml
```
`--attach RetroArch` requires the app already running on the device with Instruments/`devicectl` trusting the Mac; `--device` accepts the UDID from `xctrace list devices` (same UDIDs `device_perf.sh doctor`'s `xcrun devicectl list devices` prints). The exact `--xpath` schema name for the call-tree table needs a one-time `xctrace export --input run.trace --toc` dry run against a real capture to confirm on this Xcode version — this is unverified against an actual trace since no build/deploy was performed for this audit.

**Steps needing the user physically present**: initial device trust dialog; unlocking the iPad; interacting with RetroArch's UI (load core, load ROM, play). Build, sign, deploy, `xctrace record`/`export`, and reading the resulting file are all scriptable/headless once the device is trusted and awake.

---

## 4. Recommendation

**Run both — they answer different questions and neither substitutes for the other.**

- The perf-counter path (`device_perf.sh`) is the fast, low-friction first pass: no Xcode GUI interaction after initial setup, no dSYM juggling, gives the subsystem split (GPU RISC vs blitter vs 68K vs DSP vs OP vs DAC) on the actual A10X in a few minutes once the device is trusted. Its ceiling is exactly 8 fixed buckets at slice granularity — it will confirm *that* GPU execution dominates (matching the ~74% figure already recorded for #509 in project memory) but cannot say *where inside* `GPUExec`/`executeOpcode` the A10X specifically is slow (a particular opcode case, a branch misprediction, a specific memory-dispatch call pattern that only shows up on this chip's smaller cache/narrower OoO window).
- Instruments Time Profiler on-device is the tool that actually answers the stated goal — hot **functions and lines**. It requires more one-time setup (symbolized build, dSYM, Xcode device trust, GUI interaction to invert/flatten the call tree) but is the only path in scope that gets to function/line granularity, and its "Heaviest Stack Trace" + inverted-call-tree views directly map onto `GPUExec`/`executeOpcode`, `DSPExec`, `blitter_generic`/`BlitterMidsummer2`, and the `JaguarRead*/JaguarWrite*` dispatch functions named in the task and in `docs/profiling.md`'s own "Hot paths to know" table.

**Sequencing that costs the least device time**: run `device_perf.sh` first (cheap, confirms which of the 8 buckets to chase — almost certainly GPU per the #509 note), then go straight to Instruments and use "Invert Call Tree" + "Heaviest Stack Trace" filtered to just that subsystem's source files to find the actual hot functions/lines within it. This avoids wasting a profiling session broadly across all subsystems when the counters already narrow the target.

### Gaps in the repo's scripts worth filling (not implemented — description only)

`test/tools/rpi_perf.sh` has an automated `profile` subcommand (drives `perf_iface_witness`, a standalone one-file env-28 frontend, over ssh, with no GUI/config needed) that `device_perf.sh` has no counterpart for. That asymmetry is structural, not an oversight: iOS has no ad-hoc CLI execution outside an app bundle (no ssh-and-run-a-binary path the way Linux ARM has), so a `perf_iface_witness`-style standalone binary can't run there — `device_perf.sh capture` is necessarily "print manual steps for a human," and that's already what it does.

A `device_perf.sh profile` subcommand that wraps `xctrace` (per §3's CLI recipe) would still be valuable to add, though, to close the gap on the *function-level* side rather than the counter side. It would need to:
- Accept `-d/--device` consistent with the other subcommands, resolve to a UDID via `xcrun xctrace list devices` (parsed the same way `cmd_doctor` already parses `xcrun devicectl list devices`).
- Require/verify the app is already running (or launch it itself via `devicectl device process launch`) before `xctrace record --attach`.
- Take the core's dSYM path as a parameter (or auto-locate it next to the dylib built by `cmd_build`) and run `xctrace symbolicate` before `export`.
- Emit a flattened/inverted call-tree text or JSON summary (mirroring `cmd_report`'s existing "print a table, don't just say 'trace saved'" pattern) — the exact `--xpath` schema for that would need to be pinned against a real captured trace first, which this read-only audit could not do.
- Still need a human for the same two steps §3 lists (device trust, playing the game) — it would only remove the Xcode-GUI portion of the workflow, not the on-device interaction.

---

## Files read for this audit

- `docs/profiling.md` (whole file)
- `src/core/perf_iface.h`, `src/core/perf_iface.c`
- `src/core/perf_counters.h` (head)
- `test/tools/device_perf.sh` (whole file)
- `test/tools/rpi_perf.sh` (doctor/profile sections)
- `test/tools/perf_iface_witness.c` (header comment)
- `Makefile` lines 1-100, 260-340, 800-870 (OPT_LEVEL, RELEASE_DEBUG_INFO, ios-arm64/ios9/tvos-arm64 blocks; grepped whole file for `strip`/`dsymutil`)
- `scripts/build-id.sh` (whole file)
- `/Users/jmattiello/Workspace/Provenance/RetroArch/pkg/apple/code-sign-cores.sh` (whole file)
- `/Users/jmattiello/Workspace/Provenance/RetroArch/pkg/apple/RetroArch_iOS13.xcodeproj/project.pbxproj` (grepped for `DEBUG_INFORMATION_FORMAT`, `STRIP_INSTALLED_PRODUCT`, `COPY_PHASE_STRIP`, `DEPLOYMENT_POSTPROCESSING`; read app-target and project-level Release config blocks in full)
- `/Users/jmattiello/Workspace/Provenance/RetroArch/pkg/apple/RetroArch_iOS13.xcodeproj/xcshareddata/xcschemes/RetroArch iOS Release.xcscheme` (whole file)
- `/Users/jmattiello/Workspace/Provenance/RetroArch/pkg/apple/*.entitlements` (all four files)
- `find /Users/jmattiello/Workspace/Provenance -maxdepth 4 -iname 'build_ios*'` — no results, no such script exists anywhere under Provenance/.
