# ARM64 / NEON SIMD delegation spec

Delegatable, per-task instructions for ARM64 and NEON optimizations in the Virtual
Jaguar libretro core. Shorthand, LLM-oriented — each task is self-contained so a
cheaper model can implement one without reading the others.

**Audience:** implementers (human or agent) picking up a single task from this list.

**Chip-tuning companion:** [`arm-chip-tuning.md`](arm-chip-tuning.md) — per-chip ISA
floors, which feature macros each target actually defines (iOS/tvOS sit at the A7
baseline — see §Do-not-re-propose), AArch64-only intrinsics to avoid, and the armv7
portability compile check required below.

**Branch:** off `libretro/develop` (GitFlow integration branch; local `develop` may be
stale). PRs target `develop`, not `master`.

**C89 / GNU89:** strict outside intrinsics files. No mid-block declarations, no C99
(`for (int i…)`, compound literals, designated initializers, VLAs). New `*_simd_neon.c`
/ `*_simd_neon.h` files go on the `scripts/c89-lint.sh::skip_file` list (same pattern as
`src/tom/blitter_simd_neon.c`). Run `bash scripts/c89-lint.sh src/YOURFILE.c` before
pushing.

**Host builds on macOS:** prefix `DEVELOPER_DIR=/Library/Developer/CommandLineTools` on
every `make`/`cc` invocation (see [`CLAUDE.md`](../../CLAUDE.md)).

**Test gates (every task):**

- `make TEST_EXPORTS=1 test` — full white-box suite (auto-relinks with wide ABI).
- `test/tools/ir_ab.sh --ref libretro/develop HEAD` — instruction-count A/B vs base.
- `make -C test/acid test` — acid gate (`check-baseline.py`; `Regressions: N` with N>0
  is a failure).
- Framebuffer A/B where noted: `test/tools/frame_hash_ab` ≥8,000 frames on Iron Soldier /
  AvP / Doom / Tempest 2000 (see [`docs/profiling.md`](../profiling.md)).
- Audio tasks under `src/jerry/`: **both** `test_audio_clipping` and `test_audio_presence`.
- RetroArch smoke on at least one commercial title before claiming done (headless cannot
  catch BIOS-mode crashes or judge music vs structured noise).
- **armv7 portability check:** every new `*_simd_neon.*` file must compile-check against
  `clang -target armv7-none-linux-gnueabihf -mfpu=neon` (or the documented equivalent in
  the arm-linux-gnueabihf container, [`docs/agent/build.md`](../agent/build.md)) before
  review, so it stays rpi2/rpi3-32-bit portable. AArch64-only intrinsics to avoid and the
  verified-portable list: [`arm-chip-tuning.md`](arm-chip-tuning.md) §4.

**Measurement (before claiming a device win):**

- RPi: `test/tools/rpi_perf.sh` (`build` → `doctor` → `deploy` → `profile`).
- iOS/tvOS: `test/tools/device_perf.sh` (`doctor` → `build` → `install` → `cfg` →
  `capture` → `pull` → `report`).
- macOS host: Instruments Time Profiler on `retro_run` hot paths.

**NEON guards:** reuse `BLITTER_SIMD_HAVE_NEON` from
[`src/tom/blitter_simd_arch.h`](../../src/tom/blitter_simd_arch.h). **rpi0 / rpi1 stay
scalar** (ARM1176JZF-S has no NEON). Wire new arch files through `Makefile.common`'s
`BLITTER_SIMD` selection pattern; only introduce a new `BUILD_AXES` flag if a task truly
needs a compile-time knob beyond the existing capability macros.

---

## Do not re-propose

These are already done, rejected, or superseded. Do not open duplicate PRs.

| Item | Status |
| --- | --- |
| Accurate-blitter phrase ops (LFU, DCOMP, ZCOMP, byte merge, ADDARRAY) | **Done** — `src/tom/blitter_simd_neon.{c,h}`, `blitter_simd_sse2.c` |
| Fast-blitter SIMD | **Rejected** — audit **F8** ([`perf-audit/blitter.md`](blitter.md) §F8): `blitter_generic` is per-pixel scalar; no natural 4-lane batch without a phrase-batched redesign (F3/F4 class) |
| Audit **P3** (fast blitter per-pixel `JaguarRead*` dispatch) | **Plan of record** for blitter — see [`perf-audit-2026-08.md`](../perf-audit-2026-08.md) §P3 |
| ELF `-fvisibility=hidden` / `-fno-semantic-interposition` | **Done** — #569 |
| Per-SoC `-mcpu` flags | **Done for `rpi*` only** — #560 (`Makefile` 398–440). **NOT Apple targets:** the `ios-arm64`/`tvos-arm64` blocks (`Makefile` 303–331) pass only `-arch arm64` + min-version — no `-mcpu` — so those builds sit at the A7-class baseline (no `__ARM_FEATURE_CRC32`, no LSE). #560 gave iOS/tvOS correct platform blocks, not CPU pins. Detail: [`arm-chip-tuning.md`](arm-chip-tuning.md) §1 |
| Frontend glue (`libretro.c`) | **Already optimal** — zero frame copies, one audio batch, geometry only on change |
| Savestate `memcpy`, `crash_detect` 256-sample hash | **Already optimal** |
| CRY 16bpp scanline LUT (`CRY16ToRGB32`) | **Rejected for NEON** — 64K-entry gather; no win (see T3) |
| MOVEM batching in `src/m68000/cpuemu.c` | **Rejected** — 68K is 0.7–2.6% of frame |
| DSP `mmult` / `div` vectorization | **Rejected** — ≤15 serial MACs / bit-serial divider |
| `DSPSampleCallback` batching | **Rejected** — event-driven, no inner batch loop |
| TOM CRY LUT replacement | **Rejected** — see T3 |

---

## Codegen probes (T1 / T3 verification)

Run on **2026-08-27** on the author's macOS arm64 host
(`DEVELOPER_DIR=/Library/Developer/CommandLineTools`). Cross-compilers
(`aarch64-linux-gnu-gcc`, `arm-linux-gnueabihf-gcc`) were **not installed** on that
machine; RPi/gcc-armhf numbers should be re-checked on a Linux box or in CI before
closing T1 for 32-bit ARM.

### GET32 / GET16 probe

Isolated TUs compiled with `-O3 -S`. Current macros match
[`src/core/vjag_memory.h`](../../src/core/vjag_memory.h) lines 83–85; proposed path uses
`memcpy` + `__builtin_bswap32` / `__builtin_bswap16`.

| Variant | Host clang -O3 (arm64) | Host gcc -O3 (arm64, Apple clang) |
| --- | --- | --- |
| `GET32` macro (4× byte load + shift/or) | **4× `ldrb` + `bfi`/`orr`** — **no `rev`** | Same as clang |
| `GET32` bswap (`memcpy` + `__builtin_bswap32`) | **`ldr` + `rev`** | **`ldr` + `rev`** |
| `GET16` macro (2× byte load + shift/or) | **2× `ldrb` + `bfi`** — **no `rev16`** | Same |
| `GET16` bswap (`memcpy` + `__builtin_bswap16`) | **`ldrh` + `rev` + `lsr #16`** | Same |

**Conclusion for T1:** on AArch64 host toolchains the macro path does **not** fuse to
`rev`/`rev16`; the `__builtin_bswap*` path does. Task value on arm64 is fewer
instructions and better ILP either way. On **arm-linux-gnueabihf GCC** (RPi 32-bit) the
plan expected macro failure to `rev` — **re-verify there** before shrinking scope; Clang
armhf often already fuses.

Probe recipe (repeat after toolchain changes):

```bash
DEVELOPER_DIR=/Library/Developer/CommandLineTools clang -O3 -S -o - probe_get32.c \
  | sed -n '/get32_macro/,/^$/p'
DEVELOPER_DIR=/Library/Developer/CommandLineTools clang -O3 -S -o - probe_get32.c \
  | sed -n '/get32_bswap/,/^$/p'
```

### BGEN clear probe

Loop extracted from [`src/tom/tom.c`](../../src/tom/tom.c) lines 1602–1607 (719× u16
constant fill after seeding bytes 0–1).

| Compiler | Result |
| --- | --- |
| Host clang -O3 (arm64) | **Auto-vectorized:** `dup.8h` + `stur`/`stp q0` (128-bit stores), **no scalar `strh` loop** |
| Host gcc -O3 (arm64) | Same vectorized output |

**Conclusion for T3 BGEN:** **no hand-written NEON required** on AArch64 `-O3`; the
compiler already widens the fill. Optional `VJ_RESTRICT` on the line-buffer pointer may
help marginally on other targets. Do not spend effort on intrinsics here unless a
`-O2`/armhf build disassembles to a scalar `strh` loop.

Probe recipe:

```bash
DEVELOPER_DIR=/Library/Developer/CommandLineTools clang -O3 -S -o - probe_bgen_clear.c \
  | sed -n '/bgen_clear_line:/,/\.Lfunc_end/p'
```

---

## T1 — GET16/GET32/SET16/SET32 `__builtin_bswap` intrinsics

| | |
| --- | --- |
| **Impact** | HIGH — macros run on every 68K/GPU/DSP/blitter memory access (`JaguarReadLong` et al. ≈8% of Iron Soldier host profile) |
| **Risk** | LOW — byte-identical by construction if swap direction matches current shift-or semantics |
| **Effort** | S |

### Files and lines

- [`src/core/vjag_memory.h`](../../src/core/vjag_memory.h) lines 73–85 — `SET64`/`GET64`/`SET32`/`GET32`/`SET16`/`GET16` macros.

### Mechanism

Replace hot `GET16`/`GET32`/`SET16`/`SET32` macros with `static INLINE` functions on
GCC/Clang (`__GNUC__`): unaligned-safe `memcpy` load/store + `__builtin_bswap16` /
`__builtin_bswap32`. Keep the existing shift-or macros as the portable fallback (MSVC:
`_byteswap_ushort` / `_byteswap_ulong`). `GET64`/`SET64` are cold; optional same treatment
or leave as macros.

Codegen probe results are in §Codegen probes above.

### Code sample (C89-compliant)

Add near the top of `vjag_memory.h` (after includes, before macros):

```c
#include <string.h>

#if defined(_MSC_VER)
#  include <stdlib.h>
#  define VJ_BSWAP16(v) ((uint16_t)_byteswap_ushort((unsigned short)(v)))
#  define VJ_BSWAP32(v) ((uint32_t)_byteswap_ulong((unsigned long)(v)))
#elif defined(__GNUC__)
#  define VJ_BSWAP16(v) ((uint16_t)__builtin_bswap16((uint16_t)(v)))
#  define VJ_BSWAP32(v) ((uint32_t)__builtin_bswap32((uint32_t)(v)))
#endif

#if defined(VJ_BSWAP32)
static INLINE uint32_t vj_get32(const uint8_t *r, unsigned a)
{
   uint32_t v;
   memcpy(&v, r + a, 4);
   return VJ_BSWAP32(v);
}

static INLINE void vj_set32(uint8_t *r, unsigned a, uint32_t v)
{
   uint32_t be = VJ_BSWAP32(v);
   memcpy(r + a, &be, 4);
}

static INLINE uint16_t vj_get16(const uint8_t *r, unsigned a)
{
   uint16_t v;
   memcpy(&v, r + a, 2);
   return VJ_BSWAP16(v);
}

static INLINE void vj_set16(uint8_t *r, unsigned a, uint16_t v)
{
   uint16_t be = VJ_BSWAP16(v);
   memcpy(r + a, &be, 2);
}

#  define GET32(r, a)  vj_get32((const uint8_t *)(r), (unsigned)(a))
#  define SET32(r, a, v) vj_set32((uint8_t *)(r), (unsigned)(a), (uint32_t)(v))
#  define GET16(r, a)  vj_get16((const uint8_t *)(r), (unsigned)(a))
#  define SET16(r, a, v) vj_set16((uint8_t *)(r), (unsigned)(a), (uint16_t)(v))
#else
/* existing shift-or macros unchanged */
#endif
```

### Guards

None beyond `__GNUC__` / MSVC split. No NEON required.

### Test gates

`make TEST_EXPORTS=1 test`, `test/tools/ir_ab.sh --ref libretro/develop HEAD`, acid gate.
Optional: `test/tools/frame_hash_ab` on yarc.j64 (trivial sanity).

### Measurement

`ir_ab.sh` should show lower Ir on memory-heavy ROMs. Device: `rpi_perf.sh profile` on
Iron Soldier; host: Instruments on `JaguarReadLong`.

---

## T2 — Object Processor 16bpp phrase NEON fast path

| | |
| --- | --- |
| **Impact** | MED–HIGH — `OPProcessFixedBitmap` is 278–613 host samples/frame (same order as blitter) |
| **Risk** | MED — bounds, transparency, shadow-FB hooks must match scalar byte-for-byte |
| **Effort** | M |

### Files and lines

- [`src/tom/op.c`](../../src/tom/op.c) lines 1147–1216 — `OPProcessFixedBitmap`, `depth == 4` (16 BPP).
- [`src/tom/op.c`](../../src/tom/op.c) lines 49–50 — `OP_LBUF_IN_BOUNDS`.
- New: `src/tom/op_simd_neon.h` (or extend `blitter_simd_arch.h` only for guards).
- [`scripts/c89-lint.sh`](../../scripts/c89-lint.sh) — add `op_simd_neon.h` to `skip_file` if it contains intrinsics.

### Mechanism

Inside the `while (i++ < 4)` inner loop (lines 1166–1211), add a guarded NEON fast path
for one 4-pixel phrase when **all** of:

- `depth == 4` (16 bpp) — already in this branch.
- `!flagRMW`
- `lbufDelta == 2` (no `OPFLAG_REFLECT`; REFLECT makes delta ≠ 2).
- `OP_LBUF_IN_BOUNDS(currentLineBuffer, 8)` — whole phrase fits LBUF.
- `!shadowFBActive && !shadowHiresActive` — shadow hooks at lines 1188–1200 must run scalar.

Load 8 source bytes from the shifted `pixels` register (big-endian u16 lanes). Store 8 bytes
to `currentLineBuffer` with transparency: skip lanes where both hi and lo bytes are zero
when `flagTRANS`. Fall through to the existing scalar loop for RMW, REFLECT, shadow paths,
and partial phrases (`firstPix` / `i` start mid-phrase).

Reference NEON style: [`src/tom/blitter_simd_neon.h`](../../src/tom/blitter_simd_neon.h).

### Code sample (intrinsics header — exempt from C89 lint)

```c
#include <arm_neon.h>
#include <stdint.h>
#include <string.h>

static INLINE void op_store_phrase_16bpp_neon(
      uint8_t *lbuf, uint64_t pixels, bool flagTRANS)
{
   uint8_t bytes[8];
   uint8x8_t src;
   uint8x8_t cur;
   uint8x8_t mask;
   uint16x4_t pix;
   uint16x4_t zero;
   uint16x4_t tmask;

   bytes[0] = (uint8_t)(pixels >> 56);
   bytes[1] = (uint8_t)(pixels >> 48);
   bytes[2] = (uint8_t)(pixels >> 40);
   bytes[3] = (uint8_t)(pixels >> 32);
   bytes[4] = (uint8_t)(pixels >> 24);
   bytes[5] = (uint8_t)(pixels >> 16);
   bytes[6] = (uint8_t)(pixels >> 8);
   bytes[7] = (uint8_t)(pixels);

   src = vld1_u8(bytes);
   if (!flagTRANS)
   {
      vst1_u8(lbuf, src);
      return;
   }

   pix = vreinterpret_u16_u8(src);
   zero = vdup_n_u16(0);
   tmask = vceq_u16(pix, zero);
   mask = vreinterpret_u8_u16(tmask);
   cur = vld1_u8(lbuf);
   src = vbsl_u8(mask, cur, src);
   vst1_u8(lbuf, src);
}
```

Call site sketch (C89 — vars at block top):

```c
#if defined(BLITTER_SIMD_HAVE_NEON)
            if (!flagRMW && lbufDelta == 2
                  && !shadowFBActive && !shadowHiresActive
                  && OP_LBUF_IN_BOUNDS(currentLineBuffer, 8)
                  && i == 0 && firstPix == 0)
            {
               op_store_phrase_16bpp_neon(currentLineBuffer, pixels, flagTRANS);
               currentLineBuffer += 8;
               i = 4;
               continue;
            }
#endif
```

(Integrate carefully with `i`/`firstPix` control flow — the snippet is illustrative.)

### Guards

`#if defined(BLITTER_SIMD_HAVE_NEON)` — include `blitter_simd_arch.h`. Scalar path
unchanged when undefined.

### Test gates

Framebuffer A/B ≥8,000 frames: Iron Soldier, AvP, Doom, Tempest 2000 (`frame_hash_ab`).
`make TEST_EXPORTS=1 test`, acid gate, RetroArch smoke on a 16bpp-heavy title.

### Measurement

Host `sample` on `OPProcessFixedBitmap`; device `rpi_perf.sh` / `device_perf.sh` on AvP.

---

## T3 — TOM scanline converters, BGEN clear, `__restrict`

| | |
| --- | --- |
| **Impact** | MED — concentrates on RGB-mode titles and 2×/true-colour configs (complements audit P9) |
| **Risk** | LOW for scanlines; BGEN already vectorized at `-O3` (see probe) |
| **Effort** | M |

### Files and lines

| Function | File:lines | Notes |
| --- | --- | --- |
| `tom_render_24bpp_scanline` | [`src/tom/tom.c`](../../src/tom/tom.c) 1227–1271 | G,R,pad,B → XRGB8888 |
| `tom_render_16bpp_direct_scanline` | [`src/tom/tom.c`](../../src/tom/tom.c) 1276–1293 | BE u16 → `color >> 1` |
| `tom_render_16bpp_rgb_scanline` | [`src/tom/tom.c`](../../src/tom/tom.c) 1311–1373 | `RGB16ToRGB32` LUT gather |
| RGB expansion arithmetic (LUT source) | [`src/tom/tom.c`](../../src/tom/tom.c) 632–636 | RRRR RBBB BBGG GGGG → ARGB |
| BGEN line clear | [`src/tom/tom.c`](../../src/tom/tom.c) 1587–1607 | 720× u16 fill |
| Border fill | [`src/tom/tom.c`](../../src/tom/tom.c) 1670–1679 | scalar `uint32_t` stores |

### Mechanism

1. **`tom_render_16bpp_direct_scanline` (1276–1293):** when `pwidth_scale == 1`, NEON load
   8 BE u16 (16 bytes as `uint8x16_t` + `vrev16q_u8`), `vshrq_n_u16` by 1, widen with
   `vmovl_u16` / `vshll_n_u16`, store 8× `uint32_t`. Keep scalar loop for `pwidth_scale > 1`.

2. **`tom_render_24bpp_scanline` (1257–1270):** treat 4 pixels as structure-of-bytes;
   `vld4_u8` on interleaved G,R,?,B and `vst4_u8` into XRGB lanes with alpha `0xFF` (or
   equivalent `uint32x4_t` pack). Guard: `pwidth_scale == 1`, no shadow hooks in this path.

3. **`tom_render_16bpp_rgb_scanline` (1365–1372):** for `pwidth_scale == 1` and
   `!shadowFBActive` (stock loop only), replace `RGB16ToRGB32[color]` gather with in-register
   expansion matching lines 632–636:

   ```c
   /* per u16 color c (conceptual): */
   r = (c & 0xF800) << 8;
   g = (c & 0x003F) << 10;
   b = (c & 0x07C0) >> 3;
   out = 0xFF000000 | r | g | b;
   ```

   Process 4 or 8 pixels per vector. **Do not** vectorize the shadow/pack path (1347–1362).

4. **CRY / `CRY16ToRGB32` / `tom_render_16bpp_cry_scanline`:** **rejected** — 64K LUT
   gather dominates; NEON cannot beat L1 LUT latency.

5. **BGEN clear (1602–1607):** codegen probe shows **compiler auto-vectorization** on arm64
   `-O3` (`dup` + `stp`). **No intrinsics unless** armhf `-O2` disassembly shows scalar
   `strh`. Optional: widen manually to `uint64_t` stores if needed on a specific target.

6. **`VJ_RESTRICT` macro** (C89-safe): define in a shared header (e.g. `vjag_memory.h` or
   `tom.h`):

   ```c
   #if defined(__GNUC__) || defined(__clang__)
   #  define VJ_RESTRICT __restrict
   #else
   #  define VJ_RESTRICT
   #endif
   ```

   Apply to `backbuffer`, `current_line_buffer` parameters in scanline functions to unlock
   autovec independent of intrinsics.

### Guards

`#if defined(BLITTER_SIMD_HAVE_NEON)` around hand-written scanline kernels. BGEN: rely on
`-O3` autovec first.

### Test gates

Framebuffer A/B byte-identical (`frame_hash_ab`) on AvP (RGB16), Iron Soldier (mixed), yarc,
jagniccc. Full `make TEST_EXPORTS=1 test`, acid gate.

### Measurement

`device_perf.sh` on AvP with titledb 2× + true colour; compare `TOMExecHalfline` / scanline
symbols before/after.

---

## T4 — `VoiceChatMixInto` saturating NEON mix

| | |
| --- | --- |
| **Impact** | LOW–MED — only when voice chat enabled; 800–960 stereo pairs/frame at 48 kHz |
| **Risk** | LOW if saturation matches scalar clamps |
| **Effort** | S |

### Files and lines

- [`src/jerry/voicechat.c`](../../src/jerry/voicechat.c) lines 699–768 — `VoiceChatMixInto`.
- [`src/jerry/voicechat.h`](../../src/jerry/voicechat.h) lines 26–31 — `VC_FRAME_SAMPLES`
  (160), `VC_UPSAMPLE` (6), `VC_MAX_SPEAKERS` (3).

### Mechanism

The outer loop (723–765) upsamples 8 kHz far-end/monitor samples by 6×. For each group of
6 output pairs, `mixed` is **constant** (lines 747–751). Vectorize the final add + saturate:

- `vdupq_n_s16(mixed)` broadcast.
- Load interleaved stereo with `vld1q_s16` (4 consecutive pairs = 8 lanes).
- `vqaddq_s16` with existing buffer (saturating).
- Store with `vst1q_s16`.

Keep the scalar upsample / jitter pop logic (726–745) unchanged. Process 4 pairs per vector;
tail scalar for `pairs % 4`.

### Code sample (inside `VoiceChatMixInto`, C89 block)

```c
#if defined(BLITTER_SIMD_HAVE_NEON)
#include <arm_neon.h>
#endif
/* ... */
#if defined(BLITTER_SIMD_HAVE_NEON)
         {
            int16x8_t vm;
            int16x8_t vbuf;
            int16x8_t vout;
            vm = vdupq_n_s16((int16_t)mixed);
            for (; i + 4 <= pairs; i += 4)
            {
               vbuf = vld1q_s16(&stereo[i * 2]);
               vout = vqaddq_s16(vbuf, vm);
               vst1q_s16(&stereo[i * 2], vout);
            }
         }
#endif
         for (; i < pairs; i++)
         {
            /* existing scalar clamp loop */
         }
```

(Integrate with the existing `for (i = 0; i < pairs; i++)` — refactor so the upsample
block runs per `i`, vector block batches groups of 4 where `mixed` is stable.)

### Guards

`BLITTER_SIMD_HAVE_NEON` (or a jerry-local mirror if you prefer not to include tom headers
in jerry — acceptable to include `blitter_simd_arch.h` from `voicechat.c`).

### Test gates

**Mandatory:** `test_audio_clipping` **and** `test_audio_presence`. Also:
`test/test_voice_netpacket`, `test/test_voicechat`, `make TEST_EXPORTS=1 test`, acid gate,
RetroArch voice-chat smoke.

### Measurement

Profile `VoiceChatMixInto` with voice chat enabled; negligible frame impact expected — goal
is headroom on weak ARMs when chat is on.

---

## T5 — ARMv8 hardware CRC32 (optional)

| | |
| --- | --- |
| **Impact** | LOW — ROM/CD load and titledb identification only, not `retro_run` frame time |
| **Risk** | LOW — polynomial matches ISO-HDLC |
| **Effort** | S |

### Files and lines

- [`src/core/crc32.c`](../../src/core/crc32.c) lines 55–63 — `crc32_calcCheckSum` table loop.

### Mechanism

Standard CRC-32/ISO-HDLC (poly `0xEDB88320`, reflected) matches ARMv8-A CRC32 extension
(`__crc32b` / `__crc32w` with `__ARM_FEATURE_CRC32`). Guard:

```c
#if defined(__ARM_FEATURE_CRC32) && defined(__aarch64__)
/* hardware path: crc = __crc32b(crc, *data++); etc. */
#else
/* existing crctable loop */
#endif
```

`-mcpu=cortex-a53` and later expose `+crc` — today that means `rpi3`/`rpi3_64` and up
(`Makefile` 398–440, #560) and the macOS arm64 host default. **Stock iOS/tvOS builds do
NOT define `__ARM_FEATURE_CRC32`** — the `ios-arm64`/`tvos-arm64` blocks (`Makefile`
303–331) pass no `-mcpu`, leaving the A7 baseline — so on those targets this guard
compiles the table fallback and the hardware path is dead code (correct-by-guard, but
know it). Coverage today: macOS host, `rpi3`+, and any build with an explicit `-mcpu`.
Android / generic-arm64 Linux are single-binary targets that cannot promise CRC at
compile time; if coverage there is ever wanted, use the runtime `getauxval(AT_HWCAP)` +
`__attribute__((target("+crc")))` dispatch described in
[`arm-chip-tuning.md`](arm-chip-tuning.md) §2.3 — do not bake `+crc` into `-march`.
Bundle with T1/T3 if touching the tree anyway.

### Guards

`__ARM_FEATURE_CRC32` + `__aarch64__` (32-bit ARM CRC extension exists but is a separate
feature bit — out of scope unless explicitly tested).

### Test gates

Any existing CRC consumers in `make TEST_EXPORTS=1 test`; titledb load smoke.

### Measurement

Time `crc32_calcCheckSum` on a multi-MiB ROM buffer — expect load-time QoL only.

---

## T6 — Compiler directives and explicit rejections

| | |
| --- | --- |
| **Impact** | Variable (LTO); rejections prevent wasted effort |
| **Risk** | LTO: MED — needs device A/B before default-on |
| **Effort** | S (measurement only) |

### LTO=1 A/B recipe

Knob exists in [`Makefile`](../../Makefile) lines 1046–1055 (`make LTO=1` appends `-flto` to
compile and link for GC-style GNU targets). **Never measured on real Pi hardware** per
comment at 1046–1048.

**RPi4 64-bit A/B/B/A:**

```bash
# A: baseline
test/tools/rpi_perf.sh build --arch aarch64
test/tools/rpi_perf.sh deploy pi@raspberrypi.local
test/tools/rpi_perf.sh profile pi@raspberrypi.local --frames 1800 \
  --make-args '' > /tmp/lto_a.txt

# B: LTO=1
test/tools/rpi_perf.sh build --arch aarch64 --make-args 'LTO=1'
test/tools/rpi_perf.sh deploy pi@raspberrypi.local
test/tools/rpi_perf.sh profile pi@raspberrypi.local --frames 1800 \
  --make-args 'LTO=1' > /tmp/lto_b.txt

# Repeat B then A to cancel thermal drift; compare mean frame μs from rpi_perf output.
```

Also run `make TEST_EXPORTS=1 test` and `ir_ab.sh` on both builds (LTO must not change
emulation semantics).

### Explicit rejections (do not implement)

| Idea | Reason |
| --- | --- |
| Fast-blitter NEON | Audit F8 — no phrase batching in `blitter_generic` |
| CRY LUT NEON gather | 64K table; memory-bound |
| MOVEM batching (`cpuemu.c`) | 68K ≤2.6% frame |
| DSP `mmult` / `div` SIMD | Too few ops per invocation |
| `DSPSampleCallback` vectorization | No batch loop |
| BGEN hand NEON on arm64 `-O3` | Probe: already `dup`/`stp` vectorized |
| Default-on LTO | Needs T6 measurement first |

### Test gates

Same as LTO recipe; no code change required for this task.

### Measurement

`rpi_perf.sh profile` primary; optional `device_perf.sh` on A10X iPad if available.

---

## Execution order (suggested)

1. **T1** — widest coverage, lowest risk.
2. **T3** — scanline wins on RGB-heavy titles; skip BGEN intrinsics unless probe fails on target.
3. **T2** — OP phrase path; needs framebuffer A/B discipline.
4. **T4** — if voice chat is a shipping feature on ARM.
5. **T5** — optional bundle.
6. **T6** — parallel measurement track for LTO decision.

---

## Related documents

- [`docs/perf-audit-2026-08.md`](../perf-audit-2026-08.md) — host/RPi audit (P1–P9).
- [`docs/perf-audit/arm-chip-tuning.md`](arm-chip-tuning.md) — per-chip ISA floors,
  feature-macro matrix, AArch64-only intrinsics, armv7 portability check.
- [`docs/perf-audit/blitter.md`](blitter.md) — blitter F8, accurate-engine NEON.
- [`docs/profiling.md`](../profiling.md) — `rpi_perf.sh`, `device_perf.sh`, `ir_ab.sh`,
  `frame_hash_ab`.
- [`docs/agent/build.md`](../agent/build.md) — C89 exempt list, `TEST_EXPORTS` relink.
- [`docs/agent/testing.md`](../agent/testing.md) — audio test pair, acid gate.
