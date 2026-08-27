# Non-SIMD hot-path followups — 2026-08-27

Seven non-SIMD findings from a fresh hot-path hunt over the current tree (2026-08-27),
ranked. Shorthand, LLM-oriented, same delegation format as
[`simd-neon-arm64.md`](simd-neon-arm64.md) — each finding is self-contained. All impact
tiers are **code-reading estimates, unprofiled**; audit **P1** (idle-loop fast-forward,
[`perf-audit-2026-08.md`](../perf-audit-2026-08.md)) still dwarfs everything here.

**Branch:** off `libretro/develop`. **C89 strict** (`bash scripts/c89-lint.sh` before
pushing). **Test gates (every finding):** `make TEST_EXPORTS=1 test` + RetroArch smoke on
a commercial title. Audio-path changes (F6, F7): **both** `test_audio_clipping` **and**
`test_audio_presence` — clipping alone misses the silencing-regression class.

---

## F1 — Use `GET_CURRENT_SOFTWARE_FRAMEBUFFER` to skip the frontend frame copy

| | |
| --- | --- |
| **Impact** | MED on Pi-class software-video drivers — **speculative, needs device A/B** |
| **Risk** | MED — buffer lifetime/pitch handling; must fall back cleanly |
| **Effort** | M |

`RETRO_ENVIRONMENT_GET_CURRENT_SOFTWARE_FRAMEBUFFER` is unused. The core renders into its
own calloc'd buffer and hands it to `video_cb`; RetroArch's software video driver then
copies it into a staging texture every frame (~300 KB/frame at 1x, ~1.2 MB at 2x).

### Files and lines

- `libretro.c:5049-5051` — allocation:

  ```c
  video_buffer_alloc_pixels =
     VIDEO_BUFFER_PIXELS * shadowHiresN * shadowHiresN;
  videoBuffer  = (uint32_t *)calloc(sizeof(uint32_t), video_buffer_alloc_pixels);
  ```

- `libretro.c:6213` — submit: `video_cb(videoBuffer, game_width, game_height, game_width << 2);`
- `src/core/jaguar.c:1198-1209` — `JaguarSetScreenBuffer()` / `JaguarSetScreenPitch()`
  (where TOM's render target is pointed).
- `libretro.c:6144-6194` — tail-row blanking block (writes `videoBuffer` directly).

### Mechanism

Query the env call at the top of `retro_run` each frame. When the frontend offers a
buffer with format XRGB8888 and compatible size/pitch, point `JaguarSetScreenBuffer()` /
`JaguarSetScreenPitch()` at the frontend buffer for that frame; pass the same pointer to
`video_cb`. Fall back to `videoBuffer` when the call is declined or the offer is
incompatible. The tail-row blanking block (6144-6194) must target whichever buffer is
active that frame (it currently hardcodes `videoBuffer`), as must
`CrashDetectFrameTick`'s fb argument (6210).

Worth nothing on GPU-accelerated video drivers, which decline the call — **A/B on a Pi
with the software video driver before building this out.**

### Test gates

`make TEST_EXPORTS=1 test`; RetroArch smoke with both a driver that grants the call and
one that declines it; savestate/rewind smoke (buffer swaps between frames).

---

## F2 — Memory Track: 128 KB memcpy per armed flash write

| | |
| --- | --- |
| **Impact** | MED during in-game saves — certain |
| **Risk** | LOW |
| **Effort** | S |

`mt_pack_save_buf` memcpys the whole 128 KB Memory Track (`MT_SAVE_SIZE`, defined
`0x20000` at `libretro.c:189`) on **every** armed write. `mt_dirty_cb` fires from
`MTWriteByte` / `MTWriteWord` per byte/word of the AT29C010 flash protocol, so an 8 KB
save bursts ~1 GB of memcpy traffic.

### Files and lines

- `libretro.c:5431-5434`:

  ```c
  static void mt_pack_save_buf(void)
  {
     memcpy(eeprom_save_buf + MT_SAVE_OFFSET, mtMem, MT_SAVE_SIZE);
  }
  ```

- `libretro.c:5096` — `mt_dirty_cb = mt_pack_save_buf;`
- `src/core/memtrack.c:163-203` — `MTWriteByte` / `MTWriteWord` fire `mt_dirty_cb` at
  170-171 and 199-200 (once per armed byte/word write).
- `src/core/nvmbios.c:87-88` — NVM BIOS HLE write path also fires `mt_dirty_cb`.

### Mechanism

Set a dirty flag in `mt_dirty_cb`; do the memcpy once per frame in `retro_run` when the
flag is set (then clear it). This matches the freshness contract the EEPROM path already
provides — the save buffer only needs to be current when the frontend reads it, which
happens between frames.

### Test gates

`make TEST_EXPORTS=1 test` (includes `test_memtrack`, `test_nvmbios`); RetroArch smoke:
in-game save on a Jaguar CD title, exit, reload, verify the save survives.

---

## F3 — retro_serialize zero-fills ~350-450 KB of slack per call

| | |
| --- | --- |
| **Impact** | LOW-MED when rewind/run-ahead active (they serialize every frame) |
| **Risk** | LOW |
| **Effort** | S |

### Files and lines

- `libretro.c:4132-4134`:

  ```c
  /* Zero-fill remaining bytes for deterministic save states */
  if (written < STATE_SIZE)
     memset(buf, 0, STATE_SIZE - written);
  ```

- `src/core/state.h:185` — `#define STATE_SIZE 0x280000` (2,621,440 bytes) vs a ~2.2 MB
  payload.
- `libretro.c:4118-4129` — the one-shot headroom log prints the true payload/slack.

### Mechanism

Query `RETRO_ENVIRONMENT_GET_SAVESTATE_CONTEXT` once (it is stable per session); skip the
zero-fill for `RETRO_SAVESTATE_CONTEXT_ROLLBACK_NETPLAY` /
`RETRO_SAVESTATE_CONTEXT_RUNAHEAD_SAME_INSTANCE` contexts, where the buffer never touches
disk and slack bytes are never compared. Keep the zero-fill for normal (on-disk) states —
deterministic bytes there are load-bearing for state-diff tooling.

### Test gates

`make TEST_EXPORTS=1 test` (savestate round-trip tests); RetroArch smoke with rewind and
run-ahead enabled; save-to-disk / load-from-disk still byte-stable across two saves.

---

## F4 — Second unconditional per-frame `clock_gettime`

| | |
| --- | --- |
| **Impact** | LOW — certain |
| **Risk** | LOW |
| **Effort** | S |

Audit P8 flags the per-frame clock query inside `JLinkFrameTick`
(`src/jerry/jlink.c:689-694`, called at `libretro.c:5957`). There is a **second** one:
the netlink peer-picker rate-limit block calls `JLinkNowMs()` every frame even with
netlink disabled. P8's "one clock query/frame" count is stale — it is two.

### Files and lines

- `libretro.c:6087-6098`:

  ```c
  {
     uint32_t disc_now = JLinkNowMs();
     if (JLinkDiscConsumeChanged())
        netlink_peers_dirty = 1;
     if (netlink_peers_dirty
         && (uint32_t)(disc_now - netlink_last_rebuild_ms) >= 2000)
     ...
  ```

- `src/jerry/jlink.c:82-91` — `JLinkNowMs()` → `clock_gettime(CLOCK_MONOTONIC)`.

### Mechanism

Guard the block on `JLinkMode() != JLINK_MODE_DISABLED || netlink_peers_dirty` so the
clock read (and the `JLinkDiscConsumeChanged()` call) is skipped in the common
netlink-off case. Fold into the P8 jlink gate when that lands — the sticky
`netlink_peers_dirty` latch semantics (see the comment block at `libretro.c:6069-6086`)
must be preserved: a change may be delayed, never lost.

### Test gates

`make TEST_EXPORTS=1 test` (includes `test_jlink_netpacket`, netlink witnesses);
RetroArch smoke with netlink off and a two-instance netlink session.

---

## F5 — `LOG_DBG` has no compile-out (regression-proofing)

| | |
| --- | --- |
| **Impact** | ~0 today — regression-proofing only |
| **Risk** | LOW |
| **Effort** | S |

### Files and lines

- `src/core/log.h:26-31` — `VJ_LOG` always evaluates args + makes an indirect call:

  ```c
  #define VJ_LOG(level, ...) do { \
     if (vj_log_cb) vj_log_cb(level, __VA_ARGS__); \
     else vj_log_stderr(__VA_ARGS__); \
  } while (0)

  #define LOG_DBG(...) VJ_LOG(RETRO_LOG_DEBUG, __VA_ARGS__)
  ```

- Current hot-path uses are all rate-capped or edge-gated, so today's cost is compares:
  `src/core/jaguar.c:1176-1178` (edge-gated on one address), `src/jerry/dac.c:493-496`
  (rate-capped), `src/jerry/dsp.c:606-612` (rate-capped).

### Mechanism

`#define LOG_DBG(...) do {} while (0)` under `NDEBUG` (or behind a `VJ_DEBUG_LOG` opt-in);
keep `LOG_INF`/`LOG_WRN`/`LOG_ERR` live. Without this, any future `LOG_DBG` dropped into a
per-access path ships at full argument-evaluation cost silently.

### Test gates

`make TEST_EXPORTS=1 test` on both a stock and an `NDEBUG`/opt-in build; grep that no
release-relevant diagnostics (crash watchdog signatures) were downgraded to `LOG_DBG`.

---

## F6 — CDDA-DIAG residue in the hottest write funnels

| | |
| --- | --- |
| **Impact** | LOW — certain; removal already sanctioned by the comments themselves |
| **Risk** | LOW |
| **Effort** | S |

Diagnostic compares for the Primal Rage CDDA investigation sit in the DSP/Jaguar write
funnels and are self-documented as removable once that investigation closes.

### Files and lines

- `src/jerry/dsp.c:606-612` — `DSPWriteWord` mailbox range compare:

  ```c
  if (offset >= 0xF1B270 && offset <= 0xF1B277)
  {
     static uint32_t mboxWrites = 0;
     mboxWrites++;
     if (mboxWrites <= 40 || (mboxWrites % 10000) == 0)
        LOG_DBG("[CDDA] DSP mailbox write.w ...");
  ```

- `src/jerry/dsp.c:679-686` — the `DSPWriteLong` twin (same range compare).
- `src/core/jaguar.c:1176-1178` — `JaguarWriteLong`:

  ```c
  if (addr == 0xF1B274 && data != 0)
     LOG_DBG("[CDDA] DSP mailbox $F1B274 = %08X who=%u 68kpc=$%06X\n", ...);
  ```

### Mechanism

Delete the three blocks (and the one-shot `VJ_CDDA_SNAPDIR` snapshot at
`dsp.c:613-634`) when `docs/cd-diagnosis/primal-rage-cdda-diagnosis.md` closes. Bundle
with the next `dsp.c` / `jaguar.c` perf PR — not worth a standalone one.

### Test gates

Audio-adjacent (DSP write path): **both** `test_audio_clipping` **and**
`test_audio_presence`; `make TEST_EXPORTS=1 test`; RetroArch smoke on a music-heavy title.

---

## F7 — `GET_AUDIO_VIDEO_ENABLE` audio bit unused

| | |
| --- | --- |
| **Impact** | LOW — only during fast-forward; likely not worth standalone |
| **Risk** | LOW-MED (audio-path change → dual gate) |
| **Effort** | S |

### Files and lines

- `libretro.c:5898-5912` — env query uses the video bit only (**correctly** — the DSP
  must run every frame for determinism; only presentation may be skipped):

  ```c
  enum retro_av_enable_flags av_enable_flags = RETRO_AV_ENABLE_VIDEO;
  environ_cb(RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE, &av_enable_flags);
  tomSkipVideoPresent = (av_enable_flags & RETRO_AV_ENABLE_VIDEO) ? 0 : 1;
  ```

- Presentation stages that could honour the audio bit: `libretro.c:6106-6116`
  (`DACPrepareFrame`, `VoiceChatMixInto`, `SoundCallback` call), with the resample loop
  and the `audio_batch_cb` submit inside `SoundCallback` at `src/jerry/dac.c:376-425`.

### Mechanism

When the frontend clears `RETRO_AV_ENABLE_AUDIO` (fast-forward), skip the presentation
stages only — the `SoundCallback` resample loop, `VoiceChatMixInto`, and the
`audio_batch_cb` submit. DSP/DAC emulation stays untouched. Bundle only if already
touching `dac.c`; the dual audio test gate makes a standalone PR not worthwhile.

### Test gates

**Both** `test_audio_clipping` **and** `test_audio_presence`; `make TEST_EXPORTS=1 test`;
RetroArch smoke including fast-forward on/off transitions (no pops, audio resumes).

---

## Verified clean (do not re-hunt)

Checked in the same 2026-08-27 pass; nothing to do:

- No hot-path allocations — all buffers are load-time or opt-in.
- No `time()` / `fflush` / `sprintf` / `fopen` / `getenv` in frame paths (`getenv`
  results are latched to statics, e.g. `jlink.c` wait-debug, `dsp.c` snapshot dir).
- Titlehook work is load-time only.
- Memory Track **reads** are a 3-compare fast path (`memtrack.c`) — only writes have the
  F2 issue.
- `BUTCHExec` is gated and cheap when idle.
- CHD access uses a single-hunk cache — fine.
- JERRY / 68K dispatch is RAM-first.
- The only per-frame heap-adjacent item (`branch_condition_table`) is already audit
  **P4** ([`perf-audit-2026-08.md`](../perf-audit-2026-08.md)).

## Related documents

- [`perf-audit-2026-08.md`](../perf-audit-2026-08.md) — main audit (P1–P9); P1 dominates.
- [`simd-neon-arm64.md`](simd-neon-arm64.md) — ARM64/NEON SIMD delegation tasks (T1–T6).
- [`arm-chip-tuning.md`](arm-chip-tuning.md) — per-chip build tuning reference.
- [`../agent/testing.md`](../agent/testing.md) — harness catalog, audio test pair, acid gate.
