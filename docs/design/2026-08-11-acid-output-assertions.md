# Host-side output assertions for the acid suite

**Date:** 2026-08-11
**Status:** design, approved in outline
**Motivating bugs:** the 60 Hz cartridge crackle, the 108 samples/sec underrun
deficit, and the savestate DAC-register gap (PR #391, issues #392/#393)

## Problem

Three audio bugs shipped and reached users on device while CI was green and a
143-ROM synthetic test suite passed. Two structural reasons, both fixable.

### 1. The assertion plane is wrong

An acid test reports by writing a signature into emulated RAM at `0x100000`.
That can only observe what is visible **from inside the emulated machine**.
The bugs we shipped are all properties of what the core **emits to the
frontend**:

| bug | observable from inside the ROM? |
|---|---|
| 60 Hz step at every audio-batch seam | no |
| 108 samples/sec below the advertised rate | no |
| DAC registers absent from every savestate | no |
| geometry renegotiated per frame | no |

`test/acid/run.c` makes this explicit — its audio callbacks are no-op stubs:

```c
static size_t audio_sample_batch(const int16_t *d, size_t f) { (void)d; return f; }
```

The samples are discarded. No ROM-authored signature could ever have caught
these.

### 2. CI has no ROMs, so the tests that would have caught them never ran

The checks that *can* see output — `test_audio_clipping`,
`test_audio_presence`, and the new `test_audio_boundary` / `test_audio_rate` —
all depend on the private commercial corpus. In CI that corpus is absent, so
`scripts/find-rom.sh` misses, the skip ledger records it, and the suite exits
0. Those tests have only ever run on one maintainer's machine.

This is already documented (CLAUDE.md, "fresh worktrees skip audio tests and
still exit 0") and it is the more damaging of the two problems: it means
coverage that exists is not coverage that runs.

## Approach

Two tiers, with an explicit lifecycle between them.

### Tier 1 — synthetic (permanent, runs everywhere)

Checked-in `.s` ROMs assembled by vasm, plus host-side observers watching what
the core emits. This is the primary and permanent coverage. It runs in CI, on
a fresh clone, and on a contributor's machine with no private data.

**Goal: every known failure mode has a synthetic reproduction.** Where a
failure mode can be expressed synthetically, it should be — that is the
long-term replacement for commercial-ROM dependence.

### Tier 2 — quarantine (temporary, local/nightly only)

A commercial-ROM check is added when a bug is found and no synthetic ROM
reproduces it yet. It is explicitly temporary: once a Tier 1 ROM reproduces
the same failure, the Tier 2 check is **deleted**, not kept "for safety".

Tier 2 tests must record, in a comment, which bug they exist for and what
would let them retire. A Tier 2 test with no open bug is dead weight and
should be removed.

## Architecture

### Observers

Passive watchers installed by the acid runner, uniform interface, one concern
each:

```c
typedef struct {
    const char *name;
    void (*on_audio)(void *ctx, const int16_t *data, size_t frames);
    void (*on_video)(void *ctx, const void *px, unsigned w, unsigned h,
                     size_t pitch);
    void (*on_frame)(void *ctx, unsigned frame);
    int  (*verdict)(void *ctx, char *detail, size_t len); /* 0 pass 1 fail 2 n/a */
} acid_observer;
```

Files: `test/acid/observers/obs_audio.c`, `obs_video.c`, `obs_state.c`, each
with a header exposing only its `acid_observer` instance. The runner does not
know what any observer measures.

**`obs_audio`**
- delivered rate: `samples_per_frame * advertised_fps` vs advertised
  `sample_rate`, within a tolerance in samples/sec
- batch-seam continuity: mean `|s[i]-s[i-1]|` adjacent to a batch boundary vs
  the interior mean
- gaps: zero-runs and missing/short batches
- for tone ROMs: dominant frequency and amplitude

**`obs_video`**
- geometry renegotiation churn (`SET_GEOMETRY` / `SET_SYSTEM_AV_INFO` counts)
- stale rows, alpha integrity — subsumes `test_framebuffer_integrity` and
  `test_frontend_pacing`, which should be retired into this once it lands

**`obs_state`** — not passive. Requires the runner to execute the ROM twice:
once straight, once with a serialize/unserialize at a checkpoint frame, then
assert both output streams and framebuffers are byte-identical. This is the
determinism property, and it is what catches the "field lives in no saved
region" class (the v8 DAC-register bug). **Opt-in per test**, because it
doubles that test's runtime.

### Self-declaring contracts

Not every ROM makes sound, so globally-applied assertions would produce false
failures across the existing 143. Each ROM declares what applies to it by
extending the signature block in `test/acid/include/acid_test.s`: a flags word
plus parameters.

```
ACID_OUT_PRODUCES_AUDIO   $0001
ACID_OUT_EXPECT_SILENCE   $0002
ACID_OUT_TONE_HZ          $0004   ; param word = expected Hz
ACID_OUT_STABLE_GEOMETRY  $0008
ACID_OUT_STATE_SAFE       $0010   ; param word = checkpoint frame
```

Chosen over a side manifest file so the contract cannot drift from the ROM it
describes, and because it extends a convention that already exists rather than
adding a parallel one.

### Layered verdicts

- **Analytic** for anything derivable: rate arithmetic, continuity, gaps, tone
  frequency. A failure states a physical fact ("delivered 47892 Hz, expected
  48000") and survives legitimate emulator change without baseline churn.
- **Hash**, opt-in per test: FNV over the sample stream and over checkpoint
  framebuffers, recorded in `BASELINE.txt`. Catches classes nobody predicted.

Gating follows the existing acid model: `check-baseline.py` fails on
**regressions** only, so intentionally-failing tests stay documented rather
than blocking.

## New synthetic ROMs

Targeting the known failure modes, cheapest-first:

| ROM | shape | catches |
|---|---|---|
| `audio_tone_pit.s` | PIT-paced DSP writing a known square wave | rate deficit, seam step; the Atari Karts producer/consumer shape |
| `audio_tone_ssi.s` | same tone, SSI/word-strobe paced | strobe-vs-write capture divergence |
| `audio_silence.s` | DSP running, writing zeros | false-positive guard; silencing regressions |
| `audio_sclk_change.s` | reprograms SCLK mid-stream | resampler ratio transitions |
| `audio_slave_mode.s` | slave/CD-mode producer | the CD/BUTCH path, no disc image needed |
| `state_audio_roundtrip.s` | tone + `STATE_SAFE` checkpoint | savestate fields missing from saved regions |

## Sequencing

Ordered by value-per-effort, each step independently useful:

1. **Harvest what exists.** Stop discarding samples in `run.c`; add
   `obs_audio` with rate + seam + gap checks; apply to the existing 143 ROMs
   via their declared contracts. Near-zero new assembly, immediate broad
   coverage, runs in CI. Would have caught three of the four bugs above.
2. **`audio_tone_pit.s` + `audio_tone_ssi.s`.** The tone ROMs, which make
   failures attributable to a specific pacing model.
3. **`obs_video`**, retiring `test_framebuffer_integrity` and
   `test_frontend_pacing` into it.
4. **`obs_state` + `state_audio_roundtrip.s`.**
5. **Retire Tier 2.** Delete each commercial-ROM check whose failure mode a
   synthetic now reproduces.

## Boundaries — what this does not do

State it plainly so nobody over-trusts the suite:

- It cannot judge **emulation accuracy**. The Atari Karts grit is a
  producer/consumer rate mismatch where both clock models match the JTRM; no
  output assertion resolves that. Accuracy questions need hardware or BigPEmu
  ground truth.
- It cannot tell **music from structured noise**. Human listening stays
  required before declaring an audio change done.
- Synthetic ROMs only cover combinations someone thought to write. They
  exhaust a feature axis; commercial ROMs hit combinations nobody predicted.
  That is the argument for keeping *some* Tier 2 capacity available even when
  the current quarantine list is empty.

## Success criteria

- The three PR #391 bugs each fail a Tier 1 test on the pre-fix core and pass
  on the post-fix core (negative controls, mandatory — a test that has never
  been seen to fail is not evidence).
- `make -C test/acid test` runs the output assertions **with no private ROMs
  present**, i.e. in CI, and gates via `check-baseline.py`.
- The audio skip-ledger entries in `make test` shrink as Tier 2 retires.
