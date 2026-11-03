# RISC pipeline stall counters

**Status:** diagnostics, not behaviour.  The counters do not schedule, do not
charge ticks and are not serialized; they only record what the pipeline model
in `src/tom/gpu.c` / `src/jerry/dsp.c` already decided.  They are exported
under `TEST_EXPORTS=1` so a harness can read them.

The model they instrument is the one already in the tree, enabled by the core
option `virtualjaguar_gpu_pipeline_timing` (both RISC cores).  With the option
off the counters stay at zero.

## Why the total is not enough

`gpu_pipe_stall_total` / `dsp_pipe_stall_total` answer "how many ticks were
paid".  They cannot answer *why*, so they cannot tell a hand-written schedule
that is hazard-free from one that is merely lucky.  The breakdown does:

| counter | rule | fires when |
|---|---|---|
| `*_stall_score` | scoreboard | this instruction reads a register whose load/divide result has not arrived yet (R5/R15) |
| `*_stall_raw` | ALU result-use interlock | this instruction reads the register the previous one wrote (R2) |
| `*_stall_port` | write-back port conflict | two register reads, neither of them the pending write-back target (R4) |
| `*_stall_flags` | flags interlock | a flag-dependent instruction follows a flag-setter (R3) |
| `*_stall_extra` | fixed issue extras | `movei` (+2), indexed load (+2), indexed store (+1): structural, not a hazard |

`*_stall_total` is still the sum the pipeline clock charges; `score + raw +
port + flags + extra` is the decomposition of it.

## Per-opcode histograms

`gpu_pipe_raw_by_op[64]`, `gpu_pipe_score_by_op[64]` (and the `dsp_` pair)
record which instruction paid.  Only the two hazards that depend on the
instruction are broken out; `port` and `flags` have no per-opcode form.
The index is the opcode-table index (0..63), not the PC.

## Issue and ticks

`*_pipe_issue_total` counts instructions issued; `*_pipe_ticks_total` counts
the ticks they consumed (issue cost + bus stall + core stall).  Together they
give instructions per tick and ticks per instruction over an epoch:

    ipc     = issue_total / ticks_total
    ticks/i = ticks_total / issue_total

## Epoch

The counters are monotonic since the last `GPUReset()` / `DSPReset()`, which
is the machine-reset epoch.  `GPUPipeTimingReset()` / `DSPPipeTimingReset()`
reset the model's *clock* (and are used to start a measurement window) but
deliberately do not clear the counters — read them, subtract the value taken
at the window start, or call the machine reset.

## Reading them from a test

`make test` builds the core with `TEST_EXPORTS=1`; a harness then `dlopen`s
the core and `dlsym`s the variables above.  `test/test_gpu_interleave.c` is a
worked example: it turns the model on by writing `vjs.gpuPipelineTiming`
directly (the option path needs a frontend), resets the pipeline clock, runs
an assembled program through the real GPU, and asserts the per-cause split.

## What a good schedule looks like

- `score` and `raw` at zero: the schedule has no data hazards left by
  construction.  Anything above zero is a register dependency the schedule
  could have removed.
- `port` present: structural.  It fires on back-to-back two-source ALU
  instructions that do not read the pending write-back target, so it shows up
  in straight-line code as well.  Only a schedule that spaces the writes or
  reads the target avoids it.
- `flags` present: a flag-dependent instruction (branch, `addc`/`subc`) sits
  immediately after a flag-setter.  One transparent instruction between them
  removes it.
- `extra` present: instruction mix, not scheduler error.

A measured example, from `test_gpu_interleave.c` (the note's row as written,
36 instructions):

    ticks=53 instr=36 stalls: score=0 raw=0 port=17 flags=0 extra=0

No data hazard was paid; every stall is the structural write-back port
conflict.

## Limits

- Counters exist only while the pipeline model is on; they are not a
  substitute for wall-clock measurement.
- 64-bit, reset at machine reset, not saved in save states.
- `port` and `flags` are aggregate only; use `score`/`raw` per-op histograms
  to locate a hazard.
