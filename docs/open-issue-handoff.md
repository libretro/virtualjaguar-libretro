# Open-issue handoff — verification, tooling and ticket plan

Written 2026-08-03 against `develop`. Audience: an agent or engineer who will
break this into work tickets and execute them. Every claim here is either
verified (marked so) or explicitly flagged as unconfirmed — do not promote an
unconfirmed item to fact without re-measuring.

**Read `CLAUDE.md` first.** It is not optional context; it carries the C89
rules, the branch policy, the private-ROM-tree warning, and the test-ABI
mechanics that every ticket below depends on.

---

## STATUS UPDATE — 2026-08-04 (post-merge)

Everything in the ticket table below has landed on `develop`, and two of the
four bug tickets moved substantially. Read this block before acting on the
sections underneath it; where they disagree, this block is newer.

| Ticket | State now |
|---|---|
| #266 | **Artefact confirmed present** headlessly on `888dfb6` in both BIOS modes. Earlier "no green found" was a blind spot, not an absence. No root cause yet. Open. |
| #267 | **Root-caused.** SRCSHADE off-by-one in the accurate blitter, blit `cmd=$49800601`. Repro fixture in PR #289. Open (fix not written). |
| #268 | Decision + inspector merged. Open, blocked on the reporter pasting `vjss_info` output for the two files. |
| #269 | **Closed.** Warn-and-refuse shipped; truncated-magic tolerance explicitly rejected. |

Merged tooling now available (do not rebuild): `test/tools/vjss_info.c`,
`VJFBDIG3` band digest in `test/tools/fb_row_digest.c` + `fb_row_diff.py`,
`test/fixtures/avp_reach_gameplay.press` (Alien) and, in PR #289,
`avp_reach_marine_shotgun.press` (Marine + shotgun).

**The headless caveat survived contact with reality, in both directions.** #266
and #267 both turned out to be reproducible headlessly once the right tooling
existed — so "not reproduced headlessly" really was a tooling gap, exactly as
the caveat warned. That does not weaken the caveat for the remaining unknowns:
neither ticket has a RetroArch confirmation of on-screen severity.

---

## 0. Cross-cutting rules that will bite you

These have each cost real debugging time in this repo. Internalise them before
opening any ticket.

1. **Headless ≠ what RetroArch shows.** The harness does not read the same
   composited framebuffer RetroArch presents. A visual artefact can be real and
   still be invisible headlessly (`CLAUDE.md`, "Headless framebuffer caveat").
   *"Not reproduced headlessly" is never proof a visual bug is fixed.* This
   directly governs #266.
2. **Verify the binary you are testing.** `make` can skip a rebuild when file
   mtimes are second-identical. Always:
   `VJ_EXPECT_BUILD=$(./scripts/build-id.sh) ./test/tools/<harness> ...`
3. **Test ABI switching.** `make` links the production-slim ABI; `make test`
   needs `TEST_EXPORTS=1` for harnesses to `dlsym` internals.
4. **Branch from `libretro/develop`**, never local `develop`, never `master`.
5. **Never relax a test threshold to make a PR green.** If a real fix
   legitimately changes a measured value, that is a deliberate, called-out
   baseline update — not a side effect.
6. **Never `git clean -xfd`.** `test/roms/private` is a symlink to an
   irreplaceable ROM tree outside the repo. Use `find -L` to traverse it.
7. **Set `DEVELOPER_DIR=/Library/Developer/CommandLineTools`** on macOS host
   builds, or every invocation raises an App Management prompt.
8. **A stale-object trap exists**: if another session cross-built for iOS in
   this tree, `make` fails with *"building for 'macOS', but linking in object
   file built for 'iOS'"*. Fix is `make clean`, not investigation.

### Tooling inventory (all already exist — do not rebuild these)

| Tool | Use |
|---|---|
| `test/harness/harness.h` | Shared dlopen/init/run harness. Common CLI: `--json --frames N --option K=V --press FRAME:BUTTON[:HOLD]`. **Scripted input is how you reach in-game states headlessly.** |
| `test/tools/cd_visual_verify` | Per-second motion timeline, non-black coverage, audio RMS, periodic PPM screenshots. `sips -s format png` to read them. |
| `test/tools/test_blitter_compare` | Fast-vs-accurate per-blit diff. `--frame-window F L`, `--cmd-filter MASK VAL`, `--load-state`. Isolated the Iron Soldier wireframe bug. |
| `test/tools/cd_wedge_probe` | Frozen-framebuffer detector; dumps 68K regs, pcQueue, CD trace, RAM. Exits 42 when caught. |
| `test/regression_test.sh` | Screenshot regression vs `test/baselines/` (baselines are local, not committed). |
| `test/tools/cd_boot_matrix.sh` | Per-title CD boot-stage matrix, both modes. **22 rows, all `GAME_CODE` — this is the CD regression gate.** |
| `test/test_cd_hle_boot` + `VJ_TEST_CD_EXTS=cdi` | Corpus sweep across disc images. |
| `src/core/crash_detect.c` | Runtime watchdog: `gpu_pc_escape`, `dsp_pc_escape`, `gpu_wedge`, `dsp_wedge`, `video_stall`, `cd_seek_wedge`. Add new signatures here rather than sprinkling one-off logs. |
| `VJ_CD_TRACE=1` | CD trace ring (DSA TX/RX, seek, FIFO). `VJ_HARNESS_LOG_INFO=1` / `VJ_HARNESS_LOG_DEBUG=1` to surface it. |

---

## 1. #266 — AvP green dot / green right-edge (x ≥ 320 overscan)

> **2026-08-04:** Confirmed real. 2000-frame sweeps with the merged band digest
> find `#00FC38` at exactly `(324,32)` in HLE (1 px/frame, 1679/2000 frames) and
> at `x=323..325`, `y=1..239` under BIOS (366 px/frame, 1123/2000). That colour
> never appears at x<320. Sharpest lead: 9 HLE frames have a fully black active
> area while the green dot persists — the strip is not cleared in step with the
> active picture. Caveat found the hard way: `band_first_x/y` under-reports this,
> since it only surfaces green when nothing precedes it in row-major order — use
> a colour census instead.

**Status: reported, pixel-verified once, then twice NOT reproduced. Root cause open.**

### What is established

- BIOS off, both blitters, frame 1500: a single `#00FC38` pixel at exactly
  `(324, 32)`.
- BIOS on, both blitters: textured `#00FC38` at `x = 323..325`, `y ≈ 0..240`.
- Both hit ranges are at **x ≥ 320**. AvP's framebuffer is 326 columns; emulated
  active video is 320. Columns 320–325 are an overscan/border strip that a
  frontend may crop or show.
- Present in v2.1.0 (`48096c1`) — **not a regression**.
- The old "line buffer not cleared because AvP doesn't set BGEN" theory is
  **superseded**: BGEN *is* set for AvP with `BG=0x0000`.
- Two later full sweeps on develop found no green pixels anywhere in the border
  rows. In one of those passes the headless title screen also lacked the black
  letterbox the reporter sees — itself evidence of a capture/composition
  mismatch rather than of a fix.

### The decisive question

Is anything actually written into columns 320–325, or is the artefact produced
downstream in presentation/composition? The two sweeps point at the latter, but
the headless read path is exactly the thing that cannot answer it.

### Verification plan

1. **Get a current-nightly RetroArch capture with overscan cropping explicitly
   disabled.** This is the gating input; without it the ticket cannot progress.
   If the artefact is gone on a nightly, close it.
2. If still present: instrument the write side, not the read side. Determine
   whether TOM/OP/blitter ever writes `x ≥ 320` for AvP, by adding a temporary
   counter over the overscan columns rather than by screenshotting.
3. Compare what the core hands to `video_cb` against what RetroArch composites —
   the delta *is* the bug if step 2 shows clean writes.

### Automation worth building

Extend the A/B framebuffer digest tooling to emit a **separate digest for
columns 320–325**, corpus-wide. Overscan content is currently invisible to every
existing check; a per-column-band digest turns "did anything change in the
border strip" into a one-line regression assertion, and it generalises past AvP.

### Acceptance criteria

Either (a) a nightly capture shows the artefact gone → close with evidence, or
(b) the write-side instrumentation names the component that fills x ≥ 320, and a
fix is gated so that no other title's overscan digest changes.

### Traps

Do not "fix" this by blanking columns 320–325 unconditionally. That would hide
the symptom, and if some title legitimately renders there it becomes a new bug
with no signature.

---

## 2. #267 — AvP red background behind shotgun (accurate blitter)

> **2026-08-04:** Reproduced headlessly and root-caused. The artefact is behind
> the shotgun's **HUD slot-1 icon** (`x[249..298] y[62..79]`), not the in-view
> weapon sprite as the report reads. Blit `cmd=$49800601` =
> SRCEN|UPDA1|UPDA2|LFU_AN|LFU_A|DCOMPEN|**SRCSHADE** — `BKGWREN` and `DSTEN` are
> both clear, so #166's `!bkgwren` guard never applied. With an all-zero source
> the accurate path writes CLUT index N where fast writes N-1, leaving the
> backdrop at index 1 (dark red) instead of 0. Fix not written.
>
> Two gotchas: a save state taken after the pickup does **not** carry the
> artefact (so `test_blitter_compare --load-state` can't iterate on it — drive
> from boot), and the fixture's post-briefing route is a seeded wander that
> assumes the core stays deterministic.

**Status: never reproduced headlessly. May already be fixed.**

### What is established

- Symptom requires **Fast Blitter OFF** and a specific in-game weapon state.
- The reporter's guess that #166 is responsible was checked: #166 gated a
  phrase-mode `dstd` read on `!bkgwren` at `src/tom/blitter.c:2993`. An audit
  found the only other unconditional `dstd` read (`blitter.c:2872`, inside
  `if (dread)`) cannot fire under AvP's `!dsten && bkgwren` combination — so
  the guard *should* cover the case as understood. That makes **"it is a
  different blit type"** (SRCSHADE / Gouraud / other inhibit mask) the stronger
  lead.
- On current develop, fast vs accurate are **byte-identical across a full
  900-frame AvP timeline**. The paths agree far more than when this was filed.

### Verification plan (strictly ordered — step 1 is the whole unlock)

1. **Build an input script that reaches the shotgun.** Use
   `--press FRAME:BUTTON[:HOLD]` to navigate menus into gameplay and select the
   weapon. Until this exists, nothing else in this ticket can run. Treat this as
   its own ticket — it is reusable infrastructure, not overhead.
2. Re-confirm on a current nightly, accurate blitter, at that moment. If the
   byte-identical result holds through the shotgun draw, close as fixed by
   unrelated blitter work — with the frame window quoted as evidence.
3. If it reproduces: `test/tools/test_blitter_compare` with `--frame-window`
   around the draw and `--cmd-filter` to narrow to the offending B_CMD. This is
   the exact technique that isolated the Iron Soldier wireframe-tank bug.
4. From the isolated command, determine the blit type and which inhibit mask is
   wrong.

### Automation worth building

The step-1 input fixture. Bank it as a named scripted-input fixture other AvP
tickets (#266) can reuse, and add the resulting frame to the screenshot
regression corpus so a future regression is caught without manual play.

### Acceptance criteria

Either a quoted byte-identical comparison through the shotgun draw (close), or
an identified B_CMD plus a fix that leaves `test_blitter_compare` at zero
divergent blits corpus-wide.

---

## 3. #268 — AvP savestates rejected (below STATE_MIN_VERSION)

**Status: fixed. This section is kept for the history; the earlier "not a
bug, needs a decision" reading was wrong.** See `docs/savestate-compat.md`.

### What the investigation actually found

Two defects, and the second is the bigger one:

- `STATE_MIN_VERSION` was `2`, so v1 states (release v2.2.0) were refused.
  That is the reported symptom.
- **v1, v2 and v3 states all mis-parsed from the CDROM chunk onward.** v2
  (v2.3.0/v2.3.1) and v3 (v2.3.2) were *inside* the accepted window:
  `retro_unserialize()` returned true, the core kept running, and every chunk
  after the CD block was read at the wrong offset. The CD-support work
  restructured the CDROM chunk (−2627 bytes) with only its trailing 28 bytes
  version-gated. Nothing user-visible flagged it.

So the release-notes claim that states "from the v2.x line load normally" was
false as shipped in v3.0.0.

### What was done

`STATE_MIN_VERSION` is `1`, `DACStateLoad` gates the I2S resampler fields, and
`CDROMStateLoad` forks on `STATE_VERSION_CDROM_RESTRUCTURE` to consume the
legacy 8004-byte block. Both fixes are exact, not best-effort — the defaulted
fields are all re-derived before use (DAC) or belong to a CD path those cores
never had (CDROM). This deliberately reverses the "not adding a legacy loader
in this pass" call recorded on the issue, on the grounds that the loader turned
out to be exact.

Verified with genuine states written by v2.2.0, v2.3.0 and v2.3.2 cores built
from their own tags against the real AvP ROM; `test/test_state_compat` guards
all four released layouts in CI.

### Still open on the ticket

The reporter's two files were never available, so *their* header versions
remain inferred rather than measured. `test/tools/vjss_info` answers it in one
line if they turn up.

---

## 4. #269 — CDI V2 rips with truncated/absent boot headers

> **2026-08-04 — CLOSED.** Warn-and-refuse shipped; truncated-magic tolerance
> rejected. Note `worldtourracing` reports `matched 0/32` at `+0x42`, not the
> 22/32 this section claims below — the "partial magic" premise doesn't hold at
> the offset the boot stub actually checks.

**Status: root-caused. Needs a product decision, not more investigation.**

### What is established

Of 14 local CDI images, 4 fail `retro_load_game` — all CDI V2 rips:

- **ironsoldier2, mystdemo, vidgrid** — the boot-header region is **zero-filled
  in the file itself**. No offset math can recover data that is not present.
  These are bad dumps.
- **worldtourracing** — carries a **partial 22-of-32-byte** boot magic. Making
  it boot requires the boot stub to tolerate a truncated match.

The CDI walk itself is correct; this is not an offset bug. (Verified previously:
the V2 rips are `zeros(N) + content`, N = 112/76.)

### Decision required

- **Recommended: warn and refuse**, with a message that names the dump problem
  explicitly ("boot header is zero-filled — this image is a bad dump, not an
  unsupported format") and a known-bad-dump table so users stop re-filing it.
- Alternative: tolerate truncated magic for the worldtourracing class. **This
  carries real false-positive risk** — a 22-byte prefix match against
  decoy-prone data is a weak predicate, and a wrong accept boots garbage rather
  than failing cleanly.

### Verification plan

`VJ_TEST_CD_EXTS=cdi test/test_cd_hle_boot` over the corpus (this is how the
four were found). Whatever is implemented, assert the *message* in that sweep,
not just the pass/fail.

### Acceptance criteria

All 14 CDI images produce either a successful boot or a specific, actionable
diagnostic naming the dump defect. Zero regressions in the 22-row boot matrix.

---

## 5. VLM / audio-CD support — **no ticket exists yet; open one**

**Status: investigated this session. Verified findings below.**

### What is established (all verified)

- The **Virtual Light Machine ships inside the CD BIOS we already compile in**.
  `src/bios/jagcdbios.c` and `jagdevcdbios.c` are both 262144 bytes and contain
  `Virtual Light Machine v0.9 / (c) 1994 Virtual Light Company Ltd. / Jaguar
  CD-ROM version / FFT code by ib2 / Grafix code by Yak`. No BIOS file needed.
- Booting a synthetic single-session audio CD with
  `virtualjaguar_cd_boot_mode=bios` runs the real BIOS — logo + starfield
  animate for 90 s — but **never hands off to the VLM**.
- Trace shows only **two** DSA commands ever sent: `$7001` (Set DAC Mode) and
  `$150A` (Set Mode = double speed + **data**), then nothing. No TOC read
  (`$03xx`), no seek (`$10/$11/$12`). Button presses change nothing.
- So the BIOS stalls **before** its "is this a data disc?" decision.
- HLE rejects the disc outright: `[CD-BOOTSTUB] Early exit: loaded=1
  numSessions=1`, then "unsupported or invalid content format".
- Root cause is **disc shape**: the CD stack is built for 2-session game discs;
  `cdintf.c` gates on `numSessions >= 2` (≈ lines 427, 445, 1164).

### Two corrections to natural assumptions

- **"Add an option to always boot the BIOS" is not the fix** — that option
  already exists (`CD Boot Mode = Real BIOS`) and is exactly what was tested.
- **Every Jaguar CD track is `TRACK AUDIO / FLAGS DCP`, including data tracks.**
  Game discs and music CDs are indistinguishable by track type; the BIOS
  separates them by the `ATARI APPROVED DATA HEADER` magic in session 2.

### Verification plan

1. Find what the BIOS polls after `$150A` and why it never advances. Trace the
   68K PC in the stall loop and identify the register/response it waits on.
2. Serve a single-session all-audio disc convincingly: TOC plus CDDA.
3. Success = the VLM appears and reacts to the audio.

### Repro

```bash
# synthesize a music CD: two audio tone tracks, one session, no ATRI header
./test/tools/cd_visual_verify <core> musiccd.cue \
  --option virtualjaguar_cd_boot_mode=bios \
  --frames 5400 --outdir DIR --shot-every 1800
# add VJ_CD_TRACE=1 VJ_HARNESS_LOG_DEBUG=1 to see the DSA traffic
```

### Traps

- The VLM is BIOS code — it can **only** work in real-BIOS mode. HLE will never
  produce it. Do not scope an HLE path.
- Gate any change strictly on **1 session AND no ATRI header** so game discs
  take byte-identical paths. `cdintf.c`/`cdrom.c` are the code that took the
  longest to stabilise for game boot.
- **Regression gate: the 22-row CD boot matrix must stay all `GAME_CODE`.**

### Follow-on this unlocks

`supports_no_game` (currently `"false"`; `libretro.c` has `if (!info) return
false;`) would let the CD BIOS boot with no disc. That is also the only context
in which libretro **disk control** becomes meaningful — swapping audio CDs while
the VLM runs. Note `disk_control = "false"` in the `.info` file is *correct
today*: the CD BIOS jump table (18 entries, `jagcd_hle.c:60-77`) has no
disc-status call and the reverse-engineered DSA set has no lid/status opcode, so
a game cannot be told a disc changed. Enabling disk control without that would
let a frontend swap the image under a running game — a corruption path, not a
feature.

---

## 6. #252 — Crowdin: in-repo work merged, org-side work remains

The pipeline landed (PR #264). **Nothing further is a code ticket**; four steps
need libretro org access:

1. Obtain a Crowdin access token for this repo (Crowdin project managers, or the
   `retroarch-translations` Discord channel).
2. Add it as the Actions secret **`CROWDIN_API_KEY`**.
3. Run **Crowdin Translations Initial Setup** manually, once, then delete or
   disable it — re-running can overwrite newer Crowdin-side translations.
4. Run **Crowdin Translation Sync** manually once to confirm it can push to
   `develop`.

Until step 2, both workflows fail fast on the empty-key guard by design.

**Known residual gap** (documented in `docs/core-option-translations.md`): the
vendored `intl/` scripts call `subprocess.run()` without `check=True`, so a
token that is present but *rejected*, or a Crowdin outage, still yields a green
run that changed nothing. Belongs upstream.

**Per-language enablement is manual and expected:** the generator writes only
`libretro_core_options_intl.h`; each language stays inert until its `NULL` in
`options_intl[]` is replaced with `&options_xx`. That is upstream behaviour, and
referencing the symbol before the header defines it would not compile.

**Risk note (dependabot bumps already on `develop`):** PRs #278–#281 all merged
before any live Crowdin run. #278 is the non-routine one —
`ad-m/github-push-action` **0.8.0 → 1.3.0** (major), the action that pushes
regenerated translations in
`.github/workflows/crowdin_translation_sync.yml`. The other three
(`setup-python` 4→7, `checkout` 4→7, `setup-java` 4→5) land in the same
workflows. Until step 4 above succeeds once, a sync failure is ambiguous
between "bump broke it" and "was never set up". Check the push-action
changelog for breaking input changes (`github_token`, `branch`) when
triaging the first live failure.

---

## 7. #254 — Standalone SDL frontend

Parked feature, no investigation needed. The useful framing for whoever scopes
it: **`test/harness/` is already most of a frontend** — it implements the
libretro callbacks headlessly. Phase 1 (CLI: `vjag game.j64 [--fullscreen]
[--bios real] [--netlink client:HOST]`, SDL window, audio, gamepad) is mostly
wiring that harness to SDL rather than new emulator work. Evaluate SDL2 vs SDL3
at implementation time.

---

## 8. #236 — Nightly builds

Informational, auto-updated by CI on every nightly. **No work.** Edits to the
body are overwritten. Do not open tickets against it.

---

## 9. Dependabot bumps — merged (history)

PRs #278, #279, #280, and #281 all merged to `develop` (#278 as `6249339`;
#279–#281 as `c4edff9` / `791d292` / `f1e84d8`). There is no open dependabot
work left. The untested major bump of `ad-m/github-push-action` is now a risk
note under §6 (#252), not a ticket.

---

## Suggested ticket breakdown

Ordered by value-per-effort, not by issue number. Cheap-model task files for
items 1–4 and 6 live under `docs/tasks/`.

| # | Ticket | Depends on | Size |
|---|---|---|---|
| 1 | Savestate header inspector CLI (#268) | — | XS |
| 2 | #268 maintainer decision + document | 1 | XS |
| 3 | #269 warn-and-refuse + known-bad-dump table + corpus message assertion | — | S |
| 4 | Scripted-input fixture reaching AvP in-game (#267 unlock) | — | S–M |
| 5 | #267 re-confirm on nightly; isolate B_CMD if it reproduces | 4 | M |
| 6 | Overscan column-band digest in A/B sweep tooling (#266) | — | S |
| 7 | #266: request nightly capture; write-side instrumentation if confirmed | 6 | M |
| 8 | VLM: trace the post-`$150A` stall (investigation only, no code) | — | M |
| 9 | VLM: single-session audio-disc support, gated | 8 | L |
| 10 | `supports_no_game` + CD BIOS standalone boot | 9 | M |
| 11 | Disk control (audio-CD swap in VLM only) | 10 | M |
| 12 | #254 SDL frontend phase 1 | — | L |

Items 1–3 are closeable quickly and clear the tracker. Items 4 and 6 are
infrastructure that make two stuck visual bugs tractable and stay useful
afterwards — do them before the bugs they unlock. Items 8–11 are the VLM chain
and are strictly sequential. Item 12 is independent of everything else.

### What NOT to do

- Do not close #266 or #267 on headless evidence alone. Both are visual bugs in
  a code path where the harness is known not to see what RetroArch shows.
- Do not implement disk control before the VLM chain — there is nothing for it
  to control, and it would be actively harmful on game discs.
- Do not touch `cdintf.c` session logic without running the 22-row boot matrix.
- Do not edit vendored `intl/` scripts for style; they are a clean upstream copy
  and the two existing divergences are documented deliberately.
