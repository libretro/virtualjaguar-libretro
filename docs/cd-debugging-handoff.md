# Jaguar CD — debugging handoff

**Branch:** `feature/jaguar-cd-support` (PR #201, base `develop`) · **HEAD at writing:** `0b88fa0` · synced with develop.

Read this first if you're picking up Jaguar CD boot work. It states what's fixed, what's broken, what the evidence is, and exactly which commands to run. Companion docs: [`cd-boot-matrix.md`](cd-boot-matrix.md) (per-title status + full diagnosis sections), `docs/jtrm-*.md` (hardware ground truth).

---

## 1. The one-paragraph story

Jaguar CD games boot the BIOS, load their boot stub, stream real disc data, and then **hang before gameplay**. Five root causes were found and fixed (interrupt routing, TOC layout, FIFO word phase, a GPU pipeline hazard, an HLE read heuristic). Each fix moved titles measurably further. **One diagnosed-but-unfixed bug remains as the current blocker** — a lost-wakeup race in the 68K/GPU scheduler — plus a short backlog of independent issues.

## 2. Current per-title state (real-BIOS mode, headless + one iOS device confirmation)

| Title | State |
|---|---|
| BrainDead 13 | Boots → splash clears → loads → **audio plays** → §4 lost-wakeup deadlock **FIXED** (`61aca48`); now runs further, then a NEW post-transfer-2 wait (video_stall ~1601 headless — needs diagnosis + device re-test). |
| Baldies (CUE) | Reaches GAME_CODE (`$060106`). Was an ILLEGAL-halt crash. |
| Primal Rage | Completes transfer #2, lands in the same wait state its working HLE mode reaches. |
| Highlander / Iron Soldier 2 | Stall earlier — a **different** pre-sentinel mechanism (drains freeze at 54/0). Undiagnosed. |
| Dragon's Lair / Space Ace | FMV titles sharing BrainDead's streaming engine — expect the §4 bug. |
| Battle Morph | Crashes **before any CD I/O** (§5.2). |
| baldies.cdi | Segfaults the CDI parser at load (§5.4). CUE variant is fine. |

HLE mode gets most titles into game code; it is not a substitute for the real-BIOS path.

## 3. What was fixed (all pushed, reviewed, tested)

| Commit | Fix |
|---|---|
| `73dbc18` | **BUTCH CD interrupt → GPU IRQ1, not IRQ0.** The CD BIOS installs its CD-data ISR at `$F03010` (IRQ1 vector; JTRM names JERRY as the IRQ1 source) and enables/acks only IRQ1. We asserted IRQ0 → ISR never ran → FIFO never filled. Reverts unverified April change `3279b30`. |
| `82eb0f4` | **`$2C00` TOC rewrite to track-indexed layout.** Entry for track N at `$2C00 + N*8` = `[track#][min][sec][frm][session-1][0][0][0]`. Verified against both game boot-stub scanners (Baldies `$4E18`, Primal Rage `$0803E2`). Old table had a zero-longword marker that terminated the scan, and never stamped the session key byte. |
| `09a62d6` | **FIFO word stream starts one 16-bit word into the sector** (`cdBufPtr = 2` at seek). BUTCH assembles 32-bit FIFO entries with a one-word capture skew; discs are mastered for it (sync marks sit at byte ≡ 2 mod 4). Grouping from word 0 split every longword across two disc longwords → loaders' sync scan never matched → **the "loading screen" hang**. Also delivers FIFO data while playing, not only inside `fifoDataReady` windows. |
| `7c98e16` | **GPU: don't clobber IRQ dispatch raised in a branch delay slot.** The CD ISR epilogue is `JUMP T,(Rret)` with `STORE Rflags,(G_FLAGS)` in the delay slot; that store clears IMASK, gpu.c dispatched the IRQ synchronously *inside* the delay slot, then `gpu_opcode_jump/jr` overwrote `gpu_pc` with the branch target — **clobbering the ISR vector, IMASK stuck set forever**. Fix pushes the branch target as the return address and suppresses the overwrite. Matches `dsp.c`'s proven deferred-dispatch semantics (that equivalence was the review's load-bearing proof). |
| `17f4145` | **HLE `CD_read` made idempotent** — deleted a fabricated `+3 sectors/call` continuation heuristic that made repeated identical reads drift and corrupt RAM. |

## 4. ~~THE CURRENT BLOCKER~~ — lost-wakeup race (**FIXED 2026-07-15**, commit `61aca48`, local/unpushed)

> **Status update:** fixed by delivering GPU-raised CPUINT through the event
> scheduler at `(slice budget + GPU cycles consumed)` µs when the 68K is not
> already stopped (`src/tom/gpu.c`, `GPUCPUINTCallback`). Contract test:
> `test/test_cd_lost_wakeup.c` (in `make test`; needs `VJ_FIFO_DISC`).
> Verified: full suite 56103/56103, cart library crash-signature A/B identical,
> matrix monotonic (see the 2026-07-15 notes in `cd-boot-matrix.md`). Known
> cost: Towers II −6.5 % FPS headless (hardware-accurate handshake latency).
> **Unpushed pending maintainer device testing of GPU-heavy carts.**
> BrainDead 13 now runs past the old wall and hits a NEW, undiagnosed wait
> (~frame 1601 video_stall, cd_seek_wedge at 2266 with drains frozen at 38916
> after transfer 2 completes) — that is the next investigation, plus the §5
> backlog. The mechanism analysis below is kept for reference.

**Not a CD bug.** A core 68K/GPU scheduler race that CD FMV engines expose.

### Mechanism

BrainDead's engine uses the standard Jaguar coprocessor handshake:

```
68K:  clr.w  ($0072CC)          ; clear done-flag
      move.l #1,($F039DC)       ; command the GPU  (a1 = $F039DC)
$00B2F6  stop  #$2000           ; halt, wait for GPU's interrupt
$00B2FA  tst.b ($0072CC)        ; on wake: done?
         beq.s $00B2F6          ; no -> halt again
```
The GPU, when finished, raises CPUINT; the 68K's level-2 handler at `$00A12A` sets `$0072CC = $FFFF`, acks, `rte`; the 68K resumes past `stop` and proceeds. On silicon this is airtight: `move.l`→`stop` is ~6 cycles, the GPU needs thousands.

**Our scheduler** ([`src/core/jaguar.c`](../src/core/jaguar.c), `JaguarExecuteNew`, ~line 983) runs coarse slices:
```c
m68k_execute(delta); GPUExec(delta); DSPExec(delta);
```
A slice boundary landed **between the `move.l` and the `stop`**. The GPU then ran a full slice, finished, and raised CPUINT while `regs.stopped == 0`, so `m68k_set_irq` took the *deferred* branch ([`m68kinterface.c:186-190`](../src/m68000/m68kinterface.c)). The 68K serviced the IRQ **before** reaching `$00B2F6`, the handler set the flag, `rte` — **and only then did the 68K execute `stop`**. `stop` does not re-test the flag, and nothing raises CPUINT again.

**Deadlock:** 68K halted in `stop` · flag `$0072CC` already `$FFFF` · GPU idle at `$F03270` · black screen, audio still running.

### Evidence (measured, not inferred)

```
CPUINT raised in m68k slice #1274819
68K first entered STOP in slice #1274821      (2 slices LATER)
=> CPUINT arrived BEFORE the 68K executed stop  => LOST WAKEUP
```
Also: `$0072CC` has exactly 3 references in main RAM — `$A12E` (the 68K IRQ handler that sets it), `$B2DE` (the wait loop), `$1FFF98`. **No GPU code writes it** — confirming the wakeup must come from the 68K IRQ path. The GPU's own poll loop at `$F03270-$F0327A` polls a **RAM mailbox (`$F039DC`), not a FIFO status word** — it is a healthy idle waiter, not starving for data.

Rejected hypotheses (don't re-litigate): **continuous-streaming starvation** — the ~1 MB transfer *completes*, with a byte-identical 38915 drain count on both headless and device; drains stopping is the game's ISR clearing BUTCH bit 0 = the *completion signal*. **Wrong-LBA / data corruption** — data lands byte-exact (`test_cd_fifo_stream` proves it).

### Blast radius — read before fixing

The `stop`-wait idiom is **not** CD-only. Static scan of the local cart library (`STOP #$2000` = `4E72 2000`): **10 of 30 ROMs** contain it — Zero 5 (80×), Ruiner Pinball (8), Native Demo (8), Iron Soldier 2 (2), Skyhammer (2), Towers II (2), Battle Sphere/Gold, Val d'Isere, White Men Can't Jump. Empirically Zero 5, Ruiner Pinball, Battle Sphere and Iron Soldier 2 all run 2000 frames today with **no hang**.

**Why carts survive and BrainDead doesn't** — this is the invariant any fix must preserve:
- A lost wakeup deadlocks *permanently* only when the awaited interrupt is **one-shot**. GPU CPUINT fires once per command: lose it once, halted forever.
- Carts predominantly `stop` waiting on **recurring** interrupts (VBlank, 60 Hz). A lost wakeup there self-heals within ≤16 ms — invisible.

**Consequences:** any change to GPU→68K CPUINT timing is a **global emulation change with no equivalence oracle** (unlike `7c98e16`, which had DSP parity as ground truth). Justify it by hardware reality — *the GPU cannot physically reach its CPUINT store within the ~6 cycles between the 68K's GPUGO write and its `stop`* — **not** by a constant tuned until BrainDead advances (the PR #170 anti-pattern). Candidate directions: defer a freshly-started GPU past the current quantum; route CPUINT through the event scheduler with a hardware-justified minimum decode latency; or detect-and-hold the lost wakeup. Whichever: verify it generalizes rather than special-casing this handshake.

**Do not fold this into the CD PR silently.** It deserves a standalone commit, evaluated on cart-game impact, gated on real-hardware testing across GPU-heavy titles.

## 5. Backlog (independent, each needs its own diagnosis)

1. **Highlander / IS2 (bios)** stall earlier than the sentinel scan — drains freeze at 54/0. Different mechanism from every fix above.
2. **Battle Morph** — 68K PC lands in data (`$AFDE`, Line-A) **before any CD I/O** (`seeks=0`, ~frame 437, right after the `$050176` stub injection). Its TOC link tested **negative**. Note: in CD-BIOS mode, vectors 2–255 are PRNG garbage (`jaguar.c` vector-stub block is gated `!vjs.useJaguarBIOS`). **Populating them would MASK real failures** — boot stubs execute deliberate ILLEGAL halts on error paths. Fix the underlying error, not the halt. (Same for `jagcd_hle.c`'s ADDQ skip-handler.)
3. **IS2 (HLE)** — `CD_poll` signals completion but the game re-reads forever. Downstream of the idempotency fix.
4. **`baldies.cdi`** — CDI parser segfaults at load (pre-injection). CUE variant unaffected.
5. **`cd_seek_wedge` false-positive** on *legitimate* transfer completion (BUTCH bit 0 cleared). Watchdog refinement; cosmetic log noise, not a hang.

## 6. Toolkit — everything runnable from the CLI

### Build
```bash
make TEST_EXPORTS=1 -j$(getconf _NPROCESSORS_ONLN)     # macOS dylib — ALWAYS use TEST_EXPORTS=1
make TEST_EXPORTS=1 platform=ios-arm64 -j$(sysctl -n hw.ncpu)   # iOS core
make TEST_EXPORTS=1 test                                # full suite (must exit 0)
bash scripts/c89-lint.sh src/cd/cdrom.c src/tom/gpu.c   # C89 gate — REQUIRED before pushing
```
> Plain `make` **strips the exports the CD harnesses dlsym** — they'll silently SKIP. Always `TEST_EXPORTS=1`.

### Runtime tracing (the highest-value tool)
```bash
VJ_CD_TRACE=1 <any harness>          # env toggle, for headless
# or core option: virtualjaguar_cd_trace = enabled   (for RetroArch / on-device)
```
Records a 256-entry ring: `DSA_TX/DSA_RX`, `SEEK_START/SEEK_DONE` (+LBA/block), `FIFO_FILL/FIFO_DRAIN`, `STOP`, `HLE_READ` (+LBA). **Dumps automatically to the log** when `cd_seek_wedge` fires. Watchdog signatures live in `src/core/crash_detect.c` (`gpu_pc_escape`, `dsp_pc_escape`, `gpu_wedge`, `dsp_wedge`, `video_stall`, `cd_seek_wedge`); core option `virtualjaguar_crash_detect = verbose` adds heartbeats.

### CD test harnesses
| File | What it asserts | Run |
|---|---|---|
| `test/test_cd_fifo_stream.c` | **Regression floor** — first boot-stub load lands byte-exact vs the BIN. Must stay GREEN. | `VJ_FIFO_DISC="<img.cue>" ./test/test_cd_fifo_stream` |
| `test/test_cd_second_transfer.c` | Liveness — never 120 consecutive frames of (IMASK set + GPU running + drains frozen). Catches the `7c98e16` regression. | `VJ_FIFO_DISC="<img.cue>" ./test/test_cd_second_transfer` |
| `test/test_cd_toc_contract.c` | `$2C00` table satisfies **both** game scanner algorithms against live RAM. | `VJ_TOC_DISC="<img.cue>" ./test/test_cd_toc_contract` |
| `test/test_cd_hle_idempotent.c` | Identical `CD_read` calls leave the destination byte-stable. In `make test`. | auto (skips without disc) |
| `test/test_butch_cd.c` | Register-level BUTCH/DSA, **ROM-free**. Includes the IRQ1-routing contract. In `make test`. | auto |

All skip cleanly without private discs, so CI stays green. Build one manually if needed:
```bash
cc -O2 -Wall -std=c99 -I. -I./src -I./src/core -I./src/cd -I./src/tom -I./src/jerry \
   -I./src/m68000 -I./libretro-common/include -o test/test_cd_fifo_stream test/test_cd_fifo_stream.c -ldl
```

### The regression gate — boot matrix
```bash
bash test/tools/cd_boot_matrix.sh                      # all titles × {hle, bios} -> docs/cd-boot-matrix.md
CD_MATRIX_MAX_RUNS=4 CD_MATRIX_LOGDIR=/tmp/cdmx bash test/tools/cd_boot_matrix.sh   # chunked (resumable)
```
Env: `CD_MATRIX_FRAMES`, `CD_MATRIX_TIMEOUT`, `CD_MATRIX_MAX_RUNS`, `CD_MATRIX_LOGDIR`, `CD_MATRIX_OUT`, `CD_MATRIX_ROMS_ROOT`.
**Resume guard:** delete a row from `docs/cd-boot-matrix.md` and re-run — only deleted rows re-run. That's how you re-measure a few titles after a fix. Every fix must leave the matrix monotonic (no title regresses).

### Analysis tools
```bash
python3 test/tools/analyze_cd_roms.py <image>     # track/session layout, MSF↔LBA mapping
python3 test/tools/disasm_gpu_isr.py              # GPU RISC disassembly (used for every ISR finding above)
python3 test/tools/bios_disasm.py                 # 68K BIOS ROM disassembly
./test/tools/test_benchmark <core.dylib> <rom> [frames]   # cart smoke-test; surfaces crash-detect lines
make cd-visual CD_VISUAL_DISC="<image.cue>" \
     CD_VISUAL_FLAGS="--bios --frames 3000 --outdir /tmp/cdshots"
# ^ automated visual+audio verdict: per-second motion timeline, audio RMS,
#   screenshots (PPM -> `sips -s format png *.ppm --out .`). An agent can Read
#   the PNGs to SEE what the game shows — used to confirm BrainDead 13 renders
#   the BIOS cube, plays its FMV, and freezes on real game artwork at the
#   post-transfer-2 wall.
```

### Build-identity guard
All harnesses print the core's embedded `vX.Y.Z <gitrev>[-dirty]` at load and
hard-fail if it doesn't match `VJ_EXPECT_BUILD` (`make test` and
`cd_boot_matrix.sh` set it automatically from `scripts/build-id.sh`). Set it
manually when invoking harnesses by hand: a stale binary once produced a false
PASS in this effort — see the SKIP≠PASS fix in `test/test_framework.h`.

### Test material (`test/roms/private/`, gitignored)
CUE/BIN: Baldies, Battle Morph, BrainDead 13, Dragon's Lair, Highlander, Hover Strike, Iron Soldier 2, Primal Rage, Space Ace. Also `baldies.cdi`, loose ISOs (non-bootable — documented), `Jaguar CD BIOS.rom`, `[BIOS] Atari Jaguar CD (World).j64`, `jagboot.rom`, and ~25 cart ROMs for regression/blast-radius testing.

### Local-only diagnosis writeups
`.superpowers/sdd/` is **gitignored** (present on this machine only): `task-7-streaming-wall-report.md` (FIFO phase), `task-8-second-wedge-report.md` (delay-slot IRQ + GPU engine disasm), `task-9-fmv-stall-report.md` (**the §4 lost-wakeup diagnosis, 261 lines**), `braindead-device-trace.txt` / `-2.txt` (real iOS traces). Everything load-bearing is distilled above and in `cd-boot-matrix.md`.

## 7. Working rules (learned the hard way)

- **C89 strict** in `src/` — vars at top of block, no `for (int i…)`. New CD files are **not** lint-exempt.
- **Diagnose before fixing.** Every fix above came from disassembly/trace/byte-compare evidence. "Probably X" without a trace line has burned this project before (PR #170, PR #154).
- **No tuned constants.** A magic number dialed until a title advances is a mask, not a fix.
- **No masking fixes.** Boot stubs *deliberately* execute ILLEGAL on error paths; RTE-stub vector population makes the symptom vanish while the bug stays.
- **Matrix-gate every change** and re-run the audio pair (`test_audio_clipping` + `test_audio_presence`) for anything touching DSP/audio — the DSP is now live in the CD path.
- **Headless can't render.** It cannot distinguish "video works" from "video frozen" (see CLAUDE.md's headless framebuffer caveat). **Confirm on device/RetroArch** before believing a title is fixed — the iOS device trace is what falsified a headless "this is just a harness artifact" conclusion for BrainDead 13.
