# vjtrace — flight-recorder debugging infrastructure

Design spec, approved 2026-08-11. Context: issue #408 (timing accuracy master
plan). The timing/accuracy hunts (Doom menu 2x #399, Hover Strike, #406) each
required bespoke probes and still could not answer intra-field questions like
"who wrote this address, at which halfline, from which PC". This spec defines
one unified, core-side, dev-build-only trace facility so those questions are
answerable in one run, and so future hunts start from tooling instead of
building it.

## Decisions (maintainer-approved)

1. **Infra first, #408 Phase 1 as first client.** The decisive menu-2x
   experiment (count `phasetime` writes and GPU-object IRQs per field in the
   Doom menu) is the acceptance demo for the recorder.
2. **Core-side, dev-build only.** Compiled only when `TEST_EXPORTS=1`. All
   emit sites are macros that compile to nothing otherwise; shipped builds
   carry zero cost and zero symbols (`nm -gU` must not show `vjtrace_*`).
3. **Record + analyze CLI is the primary model.** A gdb RSP stub is Phase 2,
   layered on the same hooks; native cross-arch gdb (`set architecture m68k`)
   drives the 68K side.

## Components

### 1. Core module `src/core/vjtrace.{c,h}`

Fixed-capacity binary event ring (default 1M records, overridable via env
`VJ_TRACE_RING`). One record:

```
typedef struct {
    uint64_t seq;        /* monotonic emit counter; ordering key is
                             (frame, halfline, seq); cycle-domain
                             timestamps deferred */
    uint32_t frame;      /* harness frame counter */
    uint16_t halfline;   /* TOM VC at emit time */
    uint8_t  type;       /* VJT_EV_* */
    uint8_t  who;        /* existing who-code (68K, GPU, DSP, OP, blitter, host) */
    uint32_t pc;         /* emitting processor's PC (0 if n/a) */
    uint32_t addr;
    uint32_t value;
} vjtrace_ev;            /* 28 bytes, packed to 32 */
```

Event types (initial set):

| type | emitted at |
|---|---|
| `IRQ_ASSERT` / `IRQ_DISPATCH` | INT1 source assert; 68K autovector dispatch (addr = source id) |
| `GPU_GO` / `GPU_STOP` | G_CTRL GO transitions (value = new G_PC / final PC) |
| `OP_LIST_START` | OP begins display-list traversal for a halfline |
| `OP_OBJECT` | each object processed (addr = object phrase addr, value = type) |
| `OP_GPU_OBJ` | GPU-interrupt object fired |
| `OP_BRANCH` | branch object taken (value = target) |
| `BLIT_CMD` | blitter kicked (addr = B_CMD value, value = A1_BASE) |
| `INPUT_EDGE` | joypad state change (addr = pad index, value = new bits) |
| `WATCH_RD` / `WATCH_WR` | watched-range hit (addr/value/who/pc) |
| `SNAPSHOT` | snapshot taken (value = snapshot ordinal) |
| `MARK` | harness-injected marker (value = user tag) |

Ring, emitters, and all hooks live behind `#ifdef VJ_TRACE` (defined by the
Makefile when `TEST_EXPORTS=1`). Emit macro is a guarded inline store —
no function call, no formatting, cheap enough to leave every site on.

### 2. Watchpoints with writer attribution

Up to 16 ranges, registered at runtime via exports:

```
int  vjtrace_watch_add(uint32_t lo, uint32_t hi, unsigned rw); /* rw: 1=r 2=w 3=rw */
void vjtrace_watch_clear(void);
```

Hooked in the `JaguarReadByte/Word/Long` and `JaguarWriteByte/Word/Long`
dispatch (`src/core/jaguar.c`), which already receives the `who` origin code.
On hit, the record captures who + that processor's current PC (68K via
`m68k_get_reg(NULL, M68K_REG_PC)`, GPU/DSP via their PC state). Sub-dispatch
writes that bypass JaguarWrite* (GPU-local stores to GPU RAM, DSP-local) are
out of scope for v1 and documented as such in the header.

### 3. Per-processor PC history rings

The 68K `pcQueue` (jaguar.c) already exists. Add GPU and DSP rings of the same
depth (0x400) under `VJ_TRACE`, updated in each core's execute loop, plus one
export to dump all three:

```
void vjtrace_backtrace(int who, uint32_t *out, int max, int *count);
```

### 4. Per-field summary table

At each field boundary (existing render/VBlank callback path), append one row
to an in-core table (dumped by the harness at exit, or streamed via
`--field-csv`):

`frame, field, pad0_bits, irq_video, irq_gpu, irq_obj, irq_timer, irq_jerry,
m68k_dispatches, gpu_ops, dsp_ops, blit_count, watch_wr_hits, watch_rd_hits,
fb_hash, presented`

This is the frame-by-frame state file: greppable, and two runs diff
field-by-field.

### 5. Snapshots + memory diff

```
int vjtrace_snapshot(const char *path);  /* main RAM 2MB + GPU RAM + DSP RAM
                                            + TOM/JERRY reg windows + 68K/GPU/DSP
                                            register sets, single file, versioned header */
```

Generalizes what `cd_wedge_probe --snap` does ad hoc. A CLI differ
(`trace_memdiff a b`) prints annotated differences: address, region name
(from the memory-map table), old → new, plus register diffs.

### 6. Harness integration — `test/harness/trace_probe.{h,c}`

Resolves the exports via `harness_dlsym` and adds shared CLI flags available
to every harness-based tool (including `menu_step_probe`) without per-tool
code:

- `--trace-out FILE` — dump the event ring (binary) at exit
- `--watch ADDR[:LEN][:r|w|rw]` — repeatable
- `--snap FRAME` (repeatable) + `--snap-prefix BASE`
- `--field-csv FILE` — per-field summary rows
- `--mark FRAME:TAG` — inject `MARK` events

### 7. Analyzer CLIs (`test/tools/`)

- `trace_dump` — binary ring → text/JSONL; symbolizes known registers
  (B_CMD, G_CTRL, JOYSTICK...) and annotates RAM regions; filters
  (`--type`, `--frame A:B`, `--who`).
- `trace_diff` — two dumps → first divergence (aligned by frame/halfline/type
  stream), for A/B config comparisons.
- `field_diff` — two field-CSVs → first differing field + column.
- `trace_memdiff` — snapshot differ (see §5).

Existing pixel tools (`frame_hash_ab`, `hires_shot`) remain the pixel-exact
layer; field rows carry fb hashes for cheap screen-diff triage.

## First client — the #408 Phase 1 decisive experiment

`menu_step_probe` (state-driven input, already reaches the Doom menu
deterministically) + `--watch <phasetime addr>:16:w --field-csv menu.csv
--trace-out menu.ring`. Read out per field: `OP_GPU_OBJ` count, `WATCH_WR`
count on `phasetime`, writer PCs. Expected 1/field; 2/field names the
mechanism (H1 double GPU-object fire / H2 double list traversal / H3 instant
DSP handshake — the DSP handshake address gets a second watch). This
experiment is the acceptance test of the recorder.

## Phase 2 (separate effort) — gdb RSP stub

TCP server hosted in the harness (not the core): standard remote-serial-
protocol target exposing the 68K (`set architecture m68k` in cross gdb), full
Jaguar address space via the read/write dispatch, breakpoints/single-step via
the 68K execute-loop hook, GPU/DSP registers and RAM via `monitor` commands
(no stock gdb arch exists for Tom/Jerry RISC). Not in v1 scope; hooks from
§1–§3 are designed so the stub needs no new core surface beyond a
step-callback export.

## Non-goals

- No shipped-build tracing (crash_detect remains the user-facing facility).
- No new core options.
- No full deterministic record/replay (rr-style) in this phase.
- No GPU/DSP sub-dispatch (local-store) watch coverage in v1.

## Verification

- C89 clean: `bash scripts/c89-lint.sh` on every new/touched `src/` file.
- Prod build unchanged: `make` (without TEST_EXPORTS) compiles, and
  `nm -gU virtualjaguar_libretro.dylib | grep vjtrace` is empty.
- `make TEST_EXPORTS=1 test` green, including existing audio pair.
- Self-tests: identical runs produce identical field CSVs and empty
  `trace_memdiff`; a watch on a known-written address fires with correct
  who/pc; ring wraps without corruption.
- Acceptance: the menu experiment produces per-field counts and writer PCs
  sufficient to decide H1/H2/H3.
