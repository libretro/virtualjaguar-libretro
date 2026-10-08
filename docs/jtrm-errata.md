# JTRM Errata: where the manual is wrong, silent, or hardware differs

Curated, durable reference. Shorthand, LLM-oriented. Read before any hardware-accuracy decision in an
area listed here. Companion to the `docs/jtrm-*.md` distillations (which are a mix of manual-derived and
source-derived text; see `docs/agent/hardware.md`). Built from the 2026-10 JTRM audit (tracking issue #820);
code locations re-located on `libretro/develop` @285a642. Line numbers rot -- prefer the symbol or comment
anchor given with each.

Evidence ranking the repo uses (`docs/timing-campaign-plan.md`): **jag_sim netlists > JTRM > MAME**. Netlists
break JTRM ties (#354). A reimplementation's label (MAME enum, MiSTer RTL name) is not hardware evidence.

Source key:
- **v8** = Jaguar Technical Reference Manual Rev 8 (28 Feb 2001), `docs/atari-jaguar-1999/Technical Reference v8.pdf`.
  Printed page = PDF page. Cited `v8 p.N`.
- **v10** = Technical Reference V10.0 (SgM Electrosoft, 2010), cited `v10 p.N`.
- **SWR** = Software Reference Manual v2.4 (7 Jun 1995), `03 - Software Reference.pdf` (image-only). Cited by
  printed page with PDF page in parens where known, e.g. `SWR p.45 (PDF p.47)`. Printed = PDF minus 2 or 3.
- **HBW** = `05 - Hardware Bugs & Warnings.pdf` (26 Apr 1995, 6 pp, image-only), cited `HBW p.N #K`.
- **Netlist** = `jag_sim/netlists/*.NET` (original Flare/Atari ASIC netlists; not in this repo, cited by name).
- Manual text is paraphrased here, never quoted; the PDFs are copyrighted and gitignored.

`modeled?` = does the emulator reproduce the hardware behaviour (not the manual's idealised one).

---

## A. Manual wrong, internally inconsistent, or hardware differs

**A1. GPU object (type 2) activation by YPOS.**
- Manual: v8 p.20 -- active when VC matches YPOS, `$7FF` = all lines.
- True: silicon fires whenever the OP reaches it; games gate it with BRANCH objects and leave stale YPOS
  (yarc, Primal Rage). Evidence: game behaviour + MAME resume-at-+8. NOT netlist-verified (OP netlist unread).
- Emulator: `src/tom/op.c` `OBJECT_TYPE_GPU` case (~688-712), `test/tools/test_op_gpu_object.c`, commit `97589a3`.

**A2. BRANCH object CC width.**
- Manual: v8 p.20 -- CC printed as bits 14-15, but five codes (0-4) listed; cannot fit in 2 bits.
- True: 3 bits (14-16). Evidence: the manual's own table is impossible.
- Emulator: `src/tom/op.c` `cc = (p0 >> 14) & 0x07` (~738); `docs/jtrm-object-processor.md` BRANCH; commit `d6e6e20`.

**A3. DMAEN / BUSHI / BUS_HOG / VDE are documented as features; Atari later said do not use them.**
- Manual: v8 register chapter treats them as normal -- DMAEN p.59 (GPU) / p.109 (DSP), BUS_HOG p.61 / p.111, VDE
  p.15, BUSHI p.75 (mild caveat: long blits may disturb the screen).
- True: v8's own bug list says no master may outrank the OP (TOM #24, p.138). SWR: VDE should be `$FFFF` "due
  to a bug" (p.13, PDF p.16); DMAEN must not be set (GPU p.45 / PDF p.47; DSP p.81 / PDF p.83); BUS_HOG must not
  be set (GPU p.47 / PDF p.49; DSP PDF p.85); BUSHI should not be used (PDF p.63). HBW p.2: #4 DSP DMAEN set
  makes an external load/store hang the DSP (reset needed); #5 GPU DMAEN / blitter BUSHI while the OP runs
  corrupts the line-buffer address -- horizontal black stripes (a higher-priority master between 2nd and 3rd
  phrase of an object header).
- Emulator: priority escalation NOT modeled (`src/tom/blitter.c` "Missing: BUSHI" ~2251; no DMAEN/BUS_HOG logic in
  `gpu.c`/`dsp.c`/`bus_arbiter.c`). Games that set BUSHI render clean in the emulator, stripe on hardware.
- Docs disagreed with each other (fixed in #820 docs pass): `jtrm-gpu-dsp.md`/`jtrm-blitter.md` used to call
  BUS_HOG/BUSHI "used deliberately by games"; unverified.

**A4. Flags-store pipeline warning differs by revision.**
- v8 p.59/p.109: at least 1 intervening instruction. SWR p.46 (PDF p.48): 2, or 4 for an indexed STORE. HBW p.3
  #11: two NOPs. Later documents are stricter. Used: `jtrm-register-map.md` (2/4).
- Emulator models none of these directly; see B10 for the DSP behavioural model.

**A5. B_COUNT inner count 0.**
- Manual: v8 p.75 -- counters take 1..65536, 0 = 65536.
- True (accurate engine): outer 0 = 65536 lines; inner 0 = ONE step (<=64 px at 1bpp), because
  `BlitterMidsummer2` ends the inner loop on the bit-15 crossing. Troy Aikman, Double Dragon V, Brutal Sports
  Football write `B_COUNT=0` at boot and run fine. The engine is a netlist port, so this may be hardware-true;
  not confirmed.
- Emulator: `src/core/crash_detect.c` `inframe_blit_inner()` (~112), commit `9bcaebd` (#740).

**A6. LPH is the latched HC, not a pixel position.**
- Manual: SWR/v8 p.12 -- horizontal light-pen position in pixels.
- True: LPH latches the HC counter value (field finding in a third-party lightgun routine, 2020).
- Emulator: `src/tom/tom.c` "light gun: LPH / LPV synthesis" (~1484), `docs/lightgun-design.md`; issue #438.

**A7. DSP slave-read cycle count (self-admitted).** v8 JERRY bug 2 (p.141): every manual up to Rev 5 says 2 clocks;
correct IOSPEED is 3 (6 clocks). Irrelevant to emulation (reads always valid).

**A8. Blitter pointer registers read from the wrong addresses (self-admitted).** v8 TOM #10 (p.135). Modeled (see C).

**A9. B_I3..B_I0 / B_Z3..B_Z0 naming swapped between revisions.**
- SWR v2.2 and earlier swapped the descriptions; equates unchanged. v10 p.4 footnote `***` marks ONLY those eight
  registers (not B_SRCZ1/2). v8 p.77 still uses the old naming (Intensity 0 at `$F0227C`, Z0 at `$F0228C`);
  v10/JAGUAR.INC: B_I3 at `$F0227C` ... B_I0 at `$F02288`, B_Z3 `$F0228C` ... B_Z0 `$F02298`. Docs follow v10.
- v8 p.76: SRCZ1 = integer parts, SRCZ2 = fractional parts of computed Z (docs had them swapped, fixed in #820).

**A10. v10 transcription typos.**
- v10 p.6: PAL 68000 clock printed 13.296695 MHz; half of 26.5939 is 13.29695. Pixel-divisor table: divisor-1
  non-overscanned printed 1046 (should be 1064: every other row is 1064/divisor), divisor-2 overscanned printed
  655 (should be 665 = 1330/2).
- v10 p.5: ASIDATA listed at `F10039`; v8 p.94 and `src/jerry/jerry.c` (~37) have `F10030`.
- Evidence: arithmetic and v8. Our docs carry the right values.

**A11. 6D controller X sign (self-contradicting).** TR10 p.23 prose says X is positive right-to-left; the figure
on the same page draws the arrow to the right. Unresolved; no hardware. Code follows the prose and flags it
(`src/jerry/inputdev.h` ~174-190; `docs/input-devices-user-guide.md`). The three torque signs are flagged in the code as a named guess (`inputdev.h` ~199).

**A12. Advanced-controller socket default differs by revision.** TR04 (1995): responds on socket 1 unless it sees
+5V on pin 8. TR10 (2010): no longer required to check. Unresolved; irrelevant to pads-only Team Tap
(`docs/teamtap-procontroller-spike.md` §3.6).

**A13. Wave table size wording.** SWR says "2K bytes of wave tables" while the window is `$F1D000-$F1DFFF`
("1K 32-bit locations"; v8 p.97-98). Both agree on eight 128-entry tables, 0x200 apart. Harmless.

**A14. DSP DIV_OFFSET bit position (found in the #820 docs pass).**
- Manual: GPU `G_DIVCTRL` bit 0 (v8 p.61); DSP `D_DIVCTRL` bit **1** (v8 p.111).
- Code: both test bit 0 (`src/jerry/dsp.c` `dsp_div_control & 0x01`, `src/tom/gpu.c`). Probably a manual typo (same
  divider). Unresolved -- no hardware/netlist check. Follow-up on #820.

**A15. STOP object interrupt condition (found in the #820 docs pass).**
- Manual: v8 p.20 -- a stop object stops the OP and interrupts the host; bits 3-63 are free data.
- Code: `src/tom/op.c` `OBJECT_TYPE_STOP` raises the interrupt only if bit 3 (`p0 & 0x08`) is set. Docs used to
  say "bit 3 may optionally trigger an interrupt". Unresolved; follow-up on #820.

---

## B. Manual silent; settled from netlist, Verilator, MAME, MiSTer, BigPEmu, or game behaviour

Format: what is settled -- evidence -- where in the emulator.

**B1. OB register phrase mapping.** OB0 `$F00010` = phrase[15:0], LSW first (manual gives only the address row).
Netlist `OB.NET:55-67`, `IODEC.NET:85-88`. `src/tom/op.c` `OPSetCurrentObject` (~465, comment ~438),
`docs/jtrm-object-processor.md`; commits `f810ef7`, `4db591f` (revert), `b4556e6` (wrong MAME corroboration, see
D6); issue #354 (Val d'Isere). Residual: real OB mixes latched fields with live counters (unmodeled).

**B2. GPU/DSP cycle costs.** External load latency L = 7 + D; register-writing ALU ops retire at 2.00 ticks
back-to-back (write-back port conflict); indexed load +2, indexed store +1, MOVEI 3 ticks, taken JUMP +2, taken
JR +3; `g-1` extra clocks for `g` consecutive local loads. Netlists `SBOARD`, `INS_EXEC`, `EXECON`, `ARB`, `MEM`
(.NET), pinned by Verilator on the jag_sim GPU island. `src/tom/gpu.c` (~75-160, `GPU_PIPE_*`),
`docs/gpu-timing-spec.md`, `docs/gpu-timing-verilator-results.md`; commits `c62b97c`, `ba4dc79`, `92a27a8`;
#401, #313. Ambiguous: local-load latency is 3 ticks per the spec (`GPU_PIPE_LOCAL_LOAD 3u`) but 2 sysclks per the
Verilator side result. DSP assumed identical to GPU; never Verilated.

**B3. Bus arbitration and DRAM.** Priorities from `ARB.NET:85-95` (GPU normal beats blitter normal; OP beats GPU
normal; BUSHI beats GPU unless DMAEN) and `MEM.NET`. MiSTer `mem.v` deliberately not used for DRAM counts (FPGA
`ram_rdy` edits). Spot-verified against v8 p.10 MEMCON1: row-miss `{7,7,5,3}`, refresh `{5,4,4,3}`, ROMSPEED
10/8/6/5 are correct (`src/core/bus_arbiter.c`, `bus_arbiter.h`). Not sourced: `IO_BUS_CLOCKS_ESTIMATE 2`
(`bus_arbiter.c` ~44; its comment says no JTRM figure was found).

**B4. Accurate blitter = port of the Oberon ASIC nets.** `src/tom/blitter.c` header. Netlist-not-manual behaviours:
computed-Z write order (seed first, then ZINC; `INNER.NET`, `DCONTROL.NET`, `DATA.NET`; ~3627; commit `fe778f4`,
#791/#789; consistent with v8 p.81-82 example); the "Jaguar I bugs" (A2 Y-add tied to A1's ~2343, A1 window X used
for the destination write mask ~3525, Jaguar-II "bug fix bit" absent ~3290); `MCONTROL.NET` font-read decode
(~3238, flagged in a code comment as possibly misplaced).

**B5. Phrase-mode Gouraud lane order.** Lane 3 is the left-most pixel. `BLITGPU.NET` Dec4. `blitter.c` (~645),
commit `52910da` (#793). Manual: v8 p.82 worked example + pp.130-131 (big-endian).

**B6. DCOMPEN / PATD colour-key comparator, A2 mask wrap.** From MiSTer RTL `comp_ctrl.v` (`blitter.c` ~727) and
`address.v` (~4123). RTL names are corroboration only.

**B7. Z beyond 16bpp.** v8 pp.68, 81 define Z only for 16bpp. 32bpp Z phrase addressing follows 42Bastian's
PolyEngine (`blitter.c` ~311, commit `dcdb0b7`, #786). DSTENZ is its own memory-cycle enable (v8 p.73).

**B8. Pixel-mode data channel 0** lives in the low longword (`blitter.c` ~338-350, commit `2aaf5b3`), inferred from
the v8 p.77 alias text and the p.82 example.

**B9. Interrupt latching while the RISC core is stopped.** Manual silent about latches with GPUGO/DSPGO clear.
Game behaviour only: Hover Strike (`e92b675`, GPU; `test/acid/tests/op/op_gpu_int_object_halted.s`); White Men
Can't Jump (`646d82f`, DSP CPUINT gated on DSPGO; `src/jerry/dsp.c` ~825-845, whose comment says it is
worth confirming against hardware; #635).

**B10. D_FLAGS store retires a pipeline stage late.** Jump reads pre-store bank; IMASK clear delayed. Inferred
from Wolfenstein 3D / Doom epilogue shapes (`dsp.c` ~707-757 "inferred from behaviour", commit `d220aa2`). The GPU
has no analog (`gpu.c` ~1488) though the manual's warning covers both.

**B11. Misaligned LOAD/STORE.** v8 pp.48-56 say "must be long-word aligned"; unaligned behaviour unstated.
"Preliminary testing on real hardware" for main-memory unaligned reads (`gpu.c` ~3057; Power Drive Rally
contradicts an earlier theory; `GPU_CORRECT_ALIGNMENT` ~46 undecided). DSP forced alignment "per the JTRM"
(commit `d39f241`) -- the JTRM only says must-be-aligned.

**B12. 68K writes to RISC local RAM.** v8 p.101 gives the ordering rule; HBW Misc #2 names the failing
instructions. The commit-on-partner latch is inferred from that + Power Drive Rally (`src/core/jaguar.c` ~733-770,
commit `8648a5b`, #355). The code comment says it is more permissive than the hardware (`jaguar.c` ~747).

**B13. GPU single-step as a coprocessor barrier.** Manual: bit 3 enables single-step. "Stops free-running"
semantics from Iron Soldier 2 (`gpu.c` ~2312-2327, commit `bf4b7e8`).

**B14. Unpopulated `$200000-$7FFFFF`: writes vanish, reads still mirror.** JTRM map + MiSTer address decode
(`jaguar.c` ~1076-1090, 1130, 1224; commit `1d4a311`). Floating reads unmodeled.

**B15. OP window and start behaviour (empirical).** VDE past end of field clamps to the visible bottom, not VBB
(`tom.c` ~1640-1680; commits `f8116f0`, `ad6f381`; #641/#632). "OP start bug ~507" is a guess (`tom.c` ~1645).
YPOS==0 bumped to VDB (`op.c` ~545-560, "no idea why"). vscale 0 treated as `$20` (`op.c` ~645, "Or is it?").
OP line-buffer limit 720 (`op.c` ~820, ~1345) matches v8 TOM #5 text; the code comment says LIMIT is Jaguar II only.

**B16. HC reads** are synthetic (bit 0x400 per halfline plus an incrementing phase;
`test/acid/tests/timing/hc_advance.s`). No manual or netlist source cited.

**B17. IRQ routing.** All IRQs reach the 68K via TOM at level 2, vector 64. Derived by tracing the IPL lines on the
schematic (`jaguar.c` ~447-466). This is the model for TOM #7.

**B18. BUTCH register semantics and DSA protocol.** Distilled JTRM has nothing on BUTCH; CD-ROM manual (PDF 06)
image-only, read selectively. Settled from MiSTer `butch.v` (`src/cd/cdrom.c`, `test/mister_ground_truth.h`,
`test/test_butch_cd.c`). `$10xx/$11xx` Goto commands give no serial response.

**B19. BUTCH interrupt goes to GPU IRQ1, not IRQ0.** JTRM names int 1 as the DSP/JERRY source; BIOS ISR
fingerprints + `butch.v:83` (`cdrom.c` ~1068-1108, commit `73dbc18`).

**B20. CD timing and mode details.** Seek time = "reference parity" with BigPEmu, not silicon (`cdrom.c` ~59);
no manual seek time. Power-on drive speed defaults to double (manual silent). SBCNTRL Q-subcode bits
"undocumented" (`cdrom.c` ~1732). Set Mode `$15nn` payload format from BIOS disassembly.

**B21. Memory Track.** Map and detection corrected against MiSTer (`src/core/memtrack.c`, `nvmbios.c`).

**B22. JagGD.** Matched to BigPEmu + GDBIOS v1.11, not a hardware document (`src/core/jaggd.h`, `jaggd.c`).

**B23. Input devices.** Pro Controller: Atari SDK `JAGUAR.INC` + Developer Weekly 11 Aug 1995
(`docs/teamtap-procontroller-spike.md` §9). ST/Amiga mouse adapter pin map: a doc, #443. Team Tap/6D: TR04/TR10
only (v8 has no controller chapter).

**B24. I2S slave-mode rate.** Fixed ~44.1 kHz callback (22.675737 us) when JERRY is slave (`docs/jtrm-jerry.md`
gotcha 8, `src/jerry/jerry.c`); no manual figure.

---

## C. Atari's own bug lists (v8 pp.133-141 + HBW + SWR) with a modeled? column

v8 "TOM and JERRY Bugs List" is for "revision code 2 silicon"; levels 1-3 per its legend (3 = prevents part of the
ASIC operating, 2 = work-around leaves function impaired, 1 = simple work-around, no loss). Some entries are marked
level 0 with no legend entry. HBW (26 Apr 1995) is a separate, partly overlapping list.

### TOM bugs (v8)

| # | Title (short) | v8 pp. | Applies | Lvl | HBW xref | Modeled? |
|---|---|---|---|---|---|---|
| 1 | Unscaled 16-bit object fetch every 3 ticks (should be 2) | 133-134 | TOM/OP | 1 | n/a | no: OP cost is an occupancy estimate (`bus_arbiter_op_charge`), not tick-level |
| 2 | Scoreboard failure on indexed-store data | 134 | TOM+JERRY | 1 | GPU/DSP #1 | **DSP, partial**: `div` immediately followed by store 49/50/60/61 of its quotient register stores the old value (`dsp.c` `dsp_div_store_fixups`, #811: Xenowings' joypad reader relies on it). Not modeled: a later store inside the divide latency, the external-load variant, and the GPU |
| 3 | Transparency with HILO set | 134 | TOM v1 only | 2 | n/a | n/a (not on production Tom v2) |
| 4 | Horizontal Period register length | 134 | TOM | 0 | n/a | n/a (`TOMReset` writes HP 844/850) |
| 5 | Clipping inefficiency (720-px line buffer) | 134-135 | TOM/OP | 1 | n/a | n/a, perf only (720 limit in `op.c`) |
| 6 | Async BG can crash arbitration | 135 | TOM | 1 | n/a | n/a |
| 7 | No auto-vector (all IRQs vector `$40`) | 135 | TOM | 0 | n/a | **yes**: `jaguar.c` ~451-464 (level 2, vector 64) |
| 8 | FC[0..2] while Jerry owns bus | 135 | TOM | 0 | n/a | n/a |
| 9 | SRCSHADE only works if GOURZ set | 135 | blitter | 1 | Blitter #2 | **partial**: acknowledged (`blitter.c` ~3487) but the shade path is selected without GOURZ (~3662-3667), so the emulator is more permissive |
| 10 | Blitter pointer registers read from wrong address | 135 | blitter | 1 | n/a | **yes**: `blitter_mmio.c` ~222-229 (A1_PIXEL at `$F02204`, A2_PIXEL at `$F0222C`) |
| 11 | A2 Y-add ignored; A1 Y-add control affects both | 135-136 | blitter | 2 | Blitter #1 | **yes**: fast `blitter.c` ~1293, accurate ~2347; `test/acid/tests/quirks/a2_yadd_tied_to_a1.s` |
| 12 | JERRY bus-grant pulses | 136 | TOM/JERRY | 1 | n/a | n/a |
| 13 | Scoreboard failure on successive writes | 136 | TOM+JERRY | 0 | GPU/DSP #2 | **GPU: yes, PR #819** (not yet on develop at 285a642); DSP: no. `docs/gpu-timing-spec.md` ~88-92 had noted write-write was resolved by write-back priority. Debug-style "load then moveq same reg" |
| 14 | Single-step with MOVEI first | 136 | TOM+JERRY | 1 | n/a | no (debug only) |
| 15 | JUMP/JR from external memory unreliable | 137 | TOM+JERRY | 3 | GPU/DSP #3 | no (emulator runs external code cleanly; software avoids it) |
| 16 | High long-word register unscoreboarded; any external load clobbers it | 137 | GPU | 2 | GPU/DSP #8 | no: `gpu_hidata` set only by the LOADP/STOREP path (`gpu.c` ~3084). Plausible to matter for ISRs that load externally under a LOADP |
| 17 | ADDDSEL/SRCSHADE with Z-buffer corrupts | 137 | blitter | 2 | Blitter #5 | no |
| 18 | A1 clip with non-phrase X clips even without A1_CLIP | 137 | blitter | 1 | Blitter #3 | no: write-mask clipping is gated on `clip_a1` (`blitter.c` ~3530) |
| 19 | 2bpp unaligned source shifts fail | 137-138 | blitter | 1 | Blitter #4 | no/unknown |
| 20 | A1 clipping and DSTA2 | 138 | blitter | 1 | Blitter #3 | **yes (accurate engine)**: `blitter.c` ~3525 ("the other Jaguar I bug"); fast engine unverified |
| 21 | 32-bit DSP treated as 16 | 138 | JERRY | 1 | n/a | n/a (not the console) |
| 22 | RMW object last-pixel corruption | 138 | OP | 1 | OP #1 | no |
| 23 | GO bit may only be cleared locally | 138 | GPU/DSP | 1 | GPU/DSP #6 | no. Commit `646d82f` (WMCJ) relies on the manual's workaround pattern |
| 24 | No master above the OP priority (DMAEN/BUSHI) | 138 | GPU/blitter | 2 | GPU/DSP #5; DSP #4 | no (see A3). **Plausible to matter**: games that set BUSHI render clean here, stripe on hardware; BUSHI blits would change OP/GPU timing |
| 25 | Consecutive divides fail | 139 | TOM+JERRY | 1 | GPU/DSP #9 | no |
| 26 | Z comparators fail in pixel mode without BKGWREN | 139 | blitter | 1 | n/a | unknown: accurate engine is a netlist port and might reproduce 26-29; untested |
| 27 | Z registers shifted if SRCEN set | 139-140 | blitter | 1 | n/a | unknown (see 26) |
| 28 | A1 clipping one write too soon | 140 | blitter | 1 | n/a | unknown |
| 29 | A1 clipping can fail to clip | 140 | blitter | 1 | n/a | unknown |

### JERRY bugs (v8 p.141)

| # | Title (short) | Lvl | HBW xref | Modeled? |
|---|---|---|---|---|
| 1 | RESETIL is plain CMOS | 1 | n/a | n/a |
| 2 | DSP slave reads need IOSPEED=3 | 1 | n/a | n/a (reads always valid; see A7) |
| 3 | Jerry sees previous DBGL | 1 | n/a | n/a |
| 4 | Long-transfer size bits wrong | 1 | n/a | n/a |
| 5 | Jerry ignores MASKA | 1 | n/a | n/a |
| 6 | DSP MMULT matrix address only in low 4K of RAM | 1 | GPU/DSP #10 | **yes**: `dsp.c` ~774 (`0xF1B000 | ...`) |

### In HBW or SWR only (not in v8's list)

| Source | Title (short) | Modeled? |
|---|---|---|
| HBW GPU/DSP #4 | DSP DMAEN set -> external load/store hangs the DSP | no |
| HBW GPU/DSP #7 | DSP external write must follow an external read that completes (intermittent) | no. Plausible to matter for DSP mixers writing external buffers; emulator always clean, hardware intermittent |
| HBW GPU/DSP #11 / SWR p.46 | G_FLAGS/D_FLAGS write pipelining (2 NOPs; 4 for indexed) | DSP: partial, inferred (B10). GPU: no |
| HBW OP #2 | VSCALE above 7.0 fails | no (no check in `op.c`) |
| HBW OP #3 | HSCALE other than 1.0 on 24-bit scaled bitmaps distorts | no. SWR separately says scaled bitmaps do not display properly in 24-bit RGB |
| HBW Misc #1 | UART double-shifts a start bit at a certain phase | no (`src/jerry/uart.c`). Matters only for JagLink/ComLynx/modem |
| HBW Misc #2 | 68K `clr.l <ea>` and `move.l <ea>,-(An)` write halves in the wrong order to GPU/DSP regs and RAM (`$F02000-$F07FFF`, `$F1A000-$F1F000`) | **yes** via the commit-on-partner latch, `jaguar.c` ~733-770 |
| SWR | Programming restrictions around jumps, MAC and MMULT (no MOVEI after a jump, no back-to-back jumps, no MOVE PC after a jump, MAC chains only followed by MAC/RESMAC, no LOAD/STORE before MMULT) | no (undefined-behaviour rules, unmodeled by design) |
| v8 p.140 "Lies and Damned Lies" | Unconfirmed allegations: jump + indexed load/store crash, MMULT spacing, IMASK clear in the interrupt-return jump's delay slot | no |

**Unmodeled bugs that could plausibly affect real games, ranked:** TOM #24 (BUSHI/DMAEN timing and OP stripes),
TOM #16 (HIDATA clobber), TOM #2/#25 (scoreboard, divide; only matter if code relies on or trips them), HBW
OP #1/#2/#3, HBW GPU/DSP #7, GPU flags-store retire delay (B10).

---

## D. Where we got it wrong and the manual was right (short list; fixed in the docs)

Kept so nobody re-introduces them. Full history is in git; tracking issue #820.

- D1. PIT clock: a half-rate PIT was attributed to "JTRM section 3.7". Wrong -- PITs divide the full processor
  clock (v8 p.16). Reverted (`4e51603`); guards `test/test_pit_clock_rate.c`,
  `test/acid/tests/timing/pit_countdown_rate.s`. Residual: the Battle Sphere menu problem that motivated it was
  never explained.
- D2. HSCALE/VSCALE direction and REMAINDER width (commit `2d1625c`).
- D3. CRY field layout inverted in a doc (`b8d4c7e`, #214).
- D4. OP object tables: GPUOBJ LINK field, BRANCH CC order, OB row RO (`d6e6e20`).
- D5. Blitter window-width encoding, PITCH 3, XADDCTL 3 (`dd437b3`).
- D6. OB "corroboration" from MAME enum labels was retracted (`b4556e6`); the netlist won (`f810ef7`).
- D7. LOADB/LOADW from internal RAM: internal memory does a 32-bit read (v8 p.49); code returned a masked lane,
  broke Cinepak (`f13892b`, `49a5154`).
- D8. MMULT vector bank: bank 1, absolute (v8 "Systolic Matrix Multiplies"); code read the other bank, clipped
  Baldies (`338ed48`).
- D9. Bus-arbiter DRAM table cited a JTRM figure the manual does not support, direction inverted (`6644e3f`).
- D10. Field rate 60.05445 / 50.08013 Hz from 524/624 halflines (v8 p.15, v10 p.8); 59.94 is the interlaced rate
  (`7de96cb`, #392).
- D11. #522: 21 of 23 `Source:` lines cited code not the manual; CLK1/2/3 formula was the SCLK one; wavetable 256 vs
  128 (`1ae4908`).
- D12. #820 docs pass (this file's PR): opcode table (27 wrong numbers, 14 instructions missing), DSP flags/ctrl layout (int 5 at bits 16/17),
  LFU encodings, A1/A2 PITCH 3 = 2 gaps, SRCZ1/2 swap, MEMCON1/2 bit placement, wavetable geometry, Known Hardware
  Bugs attribution (DMAEN/BUSHI are in HBW + v8 p.138, not only SWR), video timing and pixel-divisor tables,
  and the `bcompen_basic.s` claim that blitter.c "differs from JTRM's older bit numbering" (it does not: v8
  pp.73-75 give SRCEN 0, PATDSEL 16, LFUFUNC 21-24, CMPDST 25, BCOMPEN 26, DCOMPEN 27, BKGWREN 28, BUSHI 29,
  SRCSHADE 30).

---

## E. Still unverified (pointer)

The full prioritised list of load-bearing unverified items lives on issue #820 (halted-RISC interrupt latching,
GPU-object-ignores-YPOS vs the OP netlist, B_COUNT inner 0 vs the blitter netlist, unaligned LOAD/STORE, D_FLAGS
retire delay, OP RMW/VSCALE/24-bit HSCALE, blitter fast-path width decode, A2_PIXEL writeback divergence,
VC=`$FFFF` claim, BUTCH/DSA beyond MiSTer). Rule: a line tagged `Derived from: ... NOT verified against the JTRM`
is not grounds for a hardware-accuracy decision -- read the cited PDF page yourself.
