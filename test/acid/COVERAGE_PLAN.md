# Acid-test coverage plan (PR #130 follow-on)

Goal: write **strict** tests that fail unless the emulator is correct.
NOT permissive tests that pass if the blit ran at all.  Each test
makes a precise behavioural claim and FAILs with a diagnostic if
reality diverges.

This doc partitions the work into chunks small enough for one
sub-agent each.  Status legend: `[--]` not started, `[wip]` claimed,
`[ok]` landed and PASSing, `[FAIL]` landed but FAILs (real bug
documented), `[def-FAIL]` deliberate placeholder fail.

## Ground rules for all new tests

1. **Use `include "include/jaguar_regs.s"`** for every register name
   and bit field.  Never hard-code MMIO addresses or cmd bits.
2. **Run `make -C test/acid lint`** before claiming a test is done.
   If the linter warns, fix it.
3. **Strict assertions.**  A test that PASSes only because it never
   ran is worse than no test.  Write down the *exact* expected value
   for every byte/word/long you check.
4. **Failure detail codes** must distinguish sub-tests.  A FAIL that
   says `detail=1` for every possible cause isn't actionable.
5. **Pre-init scratch RAM with a sentinel** so you can tell whether
   a write happened at all vs landed wrong.

## Chunk 1: tighten existing trivially-passing tests

Currently many tests PASS for the wrong reason -- the assertion is
too loose.  Audit and strengthen each.

| Test | Today's assertion | Tighten to |
|---|---|---|
| `timing/vc_advance` `[ok]` | VC differs across spin | exact: VC monotonically increases by 1 per halfline |
| `timing/hc_advance` `[ok]` | HC differs across spin | exact: HC alternates 0 / HP/2 by halfline parity |
| `gpu/gpu_basic_run` `[ok]` | G_PC > start address | exact: G_PC == start + 2*N where N=halflines run |
| `dsp/dsp_basic_run` `[ok]` | D_PC > start address | exact: D_PC == start + 2*N |
| `op/op_stop_terminates` `[ok]` | sentinel intact | sentinel intact AND framebuffer write-counter is zero |
| `op/op_branch_object` `[ok]` | sentinel intact | sentinel intact AND OP fetch-pointer reaches the branch target |
| `quirks/m68k_set_sr_supervisor` `[ok]` | S bit set | S bit set AND IPL == initial value |
| `stress/deep_call_chain` `[ok]` | all 16 flags | all 16 flags AND SP returns to start AND SR unchanged |
| `bus/cpu_blitter_concurrent` `[ok]` | post-blit src==expected | post-blit src AND dst correct AND blitter_calls==1 |
| `perf/memcpy_loop` / `gpu_loop_stub` / `dsp_loop_stub` `[ok]` | spot-check | exact: memory layout matches expected pattern |

Estimated 10 file edits.  **Sub-agent owner: A**.

## Chunk 2: blitter pixsize × phrase matrix

Currently we test pixsize 8/16/32 in phrase mode only.  Need full
matrix: 6 pixsizes × 2 (phrase/non-phrase) = 12 tests.

| pixsize | phrase | filename |
|---:|:--:|---|
| 1   | yes | `blitter/copy_pix1_phrase.s` |
| 1   | no  | `blitter/copy_pix1_pixel.s` |
| 2   | yes | `blitter/copy_pix2_phrase.s` |
| 2   | no  | `blitter/copy_pix2_pixel.s` |
| 4   | yes | `blitter/copy_pix4_phrase.s` |
| 4   | no  | `blitter/copy_pix4_pixel.s` |
| 8   | yes | already have (`copy_pix8.s`) `[ok]` |
| 8   | no  | `blitter/copy_pix8_pixel.s` |
| 16  | yes | already have (`copy_simple.s`) `[partial]` |
| 16  | no  | `blitter/copy_pix16_pixel.s` |
| 32  | yes | already have (`copy_pix32.s`) `[ok]` |
| 32  | no  | `blitter/copy_pix32_pixel.s` |

10 new tests.  **Sub-agent owner: B**.

## Chunk 3: blitter LFU completion (16 functions)

Currently 7 of 16.  Add the missing 9:

| LFU | Op | Note | Status |
|---:|---|---|---|
| $0 | always 0 | `lfu_zero_fill.s` | `[ok]` |
| $1 | ~S & ~D  | new -- needs SRCEN+DSTEN | `[--]` |
| $2 | ~S & D   | new -- needs SRCEN+DSTEN | `[--]` |
| $3 | ~S       | `lfu_invert_src.s` | `[ok]` |
| $4 | S & ~D   | new -- needs SRCEN+DSTEN | `[--]` |
| $5 | ~D       | new -- needs DSTEN | `[--]` |
| $6 | S ^ D    | `lfu_xor.s` | `[ok]` |
| $7 | ~S | ~D | new -- needs SRCEN+DSTEN | `[--]` |
| $8 | S & D    | `lfu_and.s` | `[ok]` |
| $9 | ~(S^D)   | new -- needs SRCEN+DSTEN | `[--]` |
| $A | D        | new -- needs DSTEN | `[--]` |
| $B | ~S | D   | new -- needs SRCEN+DSTEN | `[--]` |
| $C | S        | `lfu_passthrough_src.s` | `[ok]` |
| $D | S | ~D   | new -- needs SRCEN+DSTEN | `[--]` |
| $E | S | D    | `lfu_or.s` | `[ok]` |
| $F | always 1 | `lfu_one_fill.s` | `[ok]` |

9 new tests.  Each verifies the EXACT bit-pattern result.  **Sub-agent owner: C**.

## Chunk 4: fast-vs-accurate blitter divergence

For each blitter test, run twice -- once with
`virtualjaguar_usefastblitter=enabled`, once with `disabled` -- and
compare the dest bit-for-bit.  Today the runner only runs each ROM
once.

Two pieces of work:
1. Extend `test/acid/run.c` with a `--blitter both` mode that runs
   the same .jag twice and reports DIVERGE if dest bytes differ.
2. New top-level `make acid-fastvsaccurate` target that runs every
   `tests/blitter/*.jag` in this mode.

This will FAIL on any blit where the two paths disagree -- which is
**the most useful regression gate we can build** for blitter accuracy.

**Sub-agent owner: D**.

## Chunk 5: GPU opcode coverage

Pick the 16 most critical GPU opcodes (out of ~64).  For each, write
a test that:
1. Loads a 3-instruction GPU program: `MOVEI` immediate, `<opcode>`,
   `STOREB result_addr` (or similar).
2. Sets G_PC, GO, waits, STOPs.
3. Reads result_addr from 68K and verifies exact value.

Critical opcodes:
- `add`, `sub`, `and`, `or`, `xor` -- arithmetic
- `mult`, `imult`, `imultn`, `imacn`, `resmac` -- MAC chain
- `div`, `abs` -- harder paths
- `sh`, `sha`, `shlq`, `shrq`, `sharq`, `ror`, `rorq` -- shifts
- `cmp`, `cmpq`, `bset`, `bclr`, `btst` -- flags
- `jump`, `jr` -- control flow

16 tests.  **Sub-agent owner: E**.

## Chunk 6: DSP opcode coverage + 40-bit MAC

Same shape as GPU but DSP-specific (and replaces the placeholder
`dsp_mac_accumulator.s`).  16 most critical DSP opcodes plus ONE
real 40-bit MAC accumulator test:

- All the GPU opcodes above (DSP shares the ISA)
- **40-bit MAC test**: do N `imacn`s with operands chosen to overflow
  32 bits, then `resmac` and verify high bits are preserved per
  `src/jerry/dsp_acc40.h`.
- **DSP IRQ delivery** to 68K via JERRY external IRQ
- **DSP <-> 68K mailbox** (D_FLAGS / D_HIDATA round-trip)

~18 tests.  **Sub-agent owner: F**.

## Chunk 7: OP scenarios beyond STOP / scaled / branch

- `op/op_bitmap_render.s` -- BITMAP type 0 with known data, verify
  framebuffer pixels match
- `op/op_bitmap_each_pixsize.s` -- BITMAP at every pixsize (1,2,4,8,16,32)
- `op/op_branch_conditional.s` -- BRANCH conditional on YPOS
- `op/op_gpu_int_object.s` -- GPU-interrupt OBJECT (type 5)
- `op/op_reflect_modifier.s` -- REFLECT bit
- `op/op_palette_index.s` -- 8bpp palette indexing
- `op/op_olp_alignment.s` -- OLP must be phrase-aligned, what happens
  if not?

7 tests.  **Sub-agent owner: G**.

## Chunk 8: HLE-vs-real-BIOS cross-validation

Most acid tests today run only under HLE BIOS.  For each "what state
should be after init" claim (HLE_post_init_state, vector_table,
border_color, vector_4_is_rte, etc.), add a sibling test that runs
under real BIOS (`virtualjaguar_bios=enabled`) and asserts the same
result.  When HLE diverges from real BIOS, both tests run, only one
PASSes, and the diff is documented automatically.

Two pieces:
1. Extend `test/acid/run.c` with `--bios real` and `--bios hle`
   options.
2. Top-level `make acid-bios-cross` target that runs every test
   labelled `hle/` under both BIOS modes and reports DIVERGENCE.

**Sub-agent owner: H**.

## Chunk 9: real bus contention probes (mostly fail-by-design)

Bus contention isn't modelled.  These tests describe the expected
behaviour and will **fail until** we add contention.

- `bus/cpu_starves_blitter.s` -- 68K hammers RAM during a long blit;
  blit cycle count must be > simple-case (real hw stalls blitter).
- `bus/blitter_starves_cpu.s` -- inverse: large blit runs while 68K
  reads same RAM region; 68K cycles per memory access must be > 1.
- `bus/refresh_steals_cycles.s` -- known to be unmodelled; FAIL gate.

3 tests.  **Sub-agent owner: I**.

## Chunk 10: timing strict assertions

Currently `vc_per_frame.s` and `halfline_count_per_frame.s` are
loose.  Add:

- `timing/vblank_60hz_exact.s` -- count VBlank IRQs in a known wall-
  clock window; must be 60 for NTSC, 50 for PAL, +/-1.
- `timing/halfline_period_us.s` -- two HC-zero events should be
  ~63.5 us apart NTSC.  Read TOM_BG cycle-counter or use a known-
  cycle 68K wait.
- `timing/pit_countdown_rate.s` -- arm PIT, count IRQs in a known
  window, verify rate matches divider.
- `timing/vc_resets_at_vp.s` -- VC must wrap to 0 (or $0800 lower
  field) exactly when VC == VP+1, not before, not after.

4 tests.  **Sub-agent owner: J**.

## Chunk 11: 68K coverage

We have basic 68K via `m68k_set_sr_supervisor`, `unaligned_word`,
`bsr_l_61ff_real`, `bsr_long_61ff`, `illegal_opcode_traps`,
`divl_zero_traps`.  Add:

- `quirks/movem_round_trip.s` -- MOVEM.L D0-D7,(SP) then MOVEM.L
  (SP)+,D0-D7; verify all regs survive
- `quirks/divs_w_signed.s` -- signed 16-bit DIVS with negative
  inputs, check quotient + remainder
- `quirks/abcd_nbcd.s` -- BCD arithmetic
- `quirks/btst_dynamic.s` -- BTST with dynamic bit number

4 tests.  **Sub-agent owner: K**.

## Total

Today: 67/72 PASS.

After this plan completes: **~135 tests across 13 categories**.
Expected pass rate: **~50-60%** -- most blitter LFU/Z/comp tests,
all bus tests, the cycle-strict timing tests, and the HLE-vs-BIOS
cross-validation tests will FAIL.  That's the point: each FAIL is
a checked-in description of an emulator gap, with diagnostic codes
that point to the specific subsystem.

## Sub-agent dispatch

Order:
1. **Chunk 1 (tighten existing) FIRST**, manually -- this pattern
   informs everything else.
2. Then **Chunks 2, 3, 5, 6, 10, 11** in parallel via 6 sub-agents.
3. **Chunks 4, 8** are runner-harness extensions, do them after the
   tests they support.
4. **Chunks 7, 9** parallel after that.

Estimated effort (with the oracle + linter doing the safety work):
~half a day per chunk for the test ROMs, full day each for the
runner extensions.
