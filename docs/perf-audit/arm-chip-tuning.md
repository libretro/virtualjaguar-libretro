# ARM per-chip tuning reference

Pro-level per-chip build-tuning reference for this core's slowest targets (Apple A/M, Snapdragon
handhelds, generic ARM64 Linux, 32-bit ARM). Shorthand, LLM-oriented. Companion to the SIMD task
spec [`simd-neon-arm64.md`](simd-neon-arm64.md) (owned by a separate track — reference only, do
not edit from here).

**DOCUMENT ONLY.** Nothing here is applied. Every settings change is listed in §6 and marked
**requires approval / not applied**. Ground rules that bind every proposal:

- **#516:** the *last* `-O` wins; never append an `-O` in a platform block; never re-specify
  platform flags on the command line ([`agent/build.md`](../agent/build.md)).
- **#517:** no `-Ofast` / `-ffast-math` anywhere (determinism: run-ahead, netplay, savestates).
- **#560:** per-SoC `-mcpu` is only justified where the platform name identifies the silicon
  exactly (the `rpi*` model). Generic names get generic flags.

**Provenance of claims.** Compiler behavior was verified on 2026-08-27 on the author's macOS
host: Apple clang 17.0.0 (clang-1700.6.4.2), iPhoneOS 26.5 / AppleTVOS 26.5 SDKs, via
`cc -E -dM` macro dumps and `--print-supported-cpus`. LLVM CPU feature sets read from
`llvm-project` `AArch64.td`. NDK/ARM facts from developer.android.com and the Arm ARM. Anything
not checked that way is marked **UNVERIFIED**. Linux-gcc cross checks could not run on this host
(no `aarch64-linux-gnu-gcc` installed) — those are flagged inline.

## What the build passes today (read from source, 2026-08-27)

| Target | `Makefile` lines | Arch/CPU flags actually passed | NEON blitter |
| --- | --- | --- | --- |
| `osx` | 266–291 | none (`-arch` via `$(ARCHFLAGS)` only; no `-mcpu`/`-march`) | via `Makefile.common:218-219` host uname |
| `ios-arm64` | 303–316 | `cc -arch arm64 -isysroot <iphoneos>` + `-miphoneos-version-min=8.0`; **no `-mcpu`** | `Makefile.common:158-159` |
| `tvos-arm64` | 318–331 | `cc -arch arm64 -isysroot <appletvos>` + `-mappletvos-version-min=11.0`; **no `-mcpu`** | `Makefile.common:158-159` |
| generic `arm64`/`aarch64` | 359–364 | **nothing** — no `-march`, no `-mcpu`; toolchain default (normally `armv8-a`) | `Makefile.common:162-163` via `ARCH=aarch64` |
| `rpi0`–`rpi5_64` | 398–440 | per-SoC `-mcpu` (+ 32-bit `-mfpu`/`-mfloat-abi`) — see [`agent/build.md`](../agent/build.md) table | rpi2+ |
| generic `armv*` | 442–455 | name-derived only; `HAVE_NEON` iff name contains `neon` | conditional |
| `libnx` (Switch) | 467 | `-march=armv8-a -mtune=cortex-a57 -mtp=soft -mcpu=cortex-a57+crc+fp+simd` | via `armv8` name match |
| Android (`jni/Android.mk`) | Android.mk 24–33, 52–54 | ndk-build ABI defaults (arm64-v8a ⇒ `armv8-a` baseline); `COREFLAGS` adds `-O3` + #569 visibility flags, **no `-march`/`-mcpu`** | Android.mk 25–28 |

`-O3` for the Apple/unix/rpi rows comes from `OPT_O3_PLATFORMS` (`Makefile:70-71`), not the
platform blocks (#516).

---

## 1. Apple A/M series (iOS, tvOS, macOS)

### 1.1 Accepted `-mcpu` values (verified)

Apple clang 17.0.0 `--print-supported-cpus` (host, 2026-08-27): `apple-a7` … `apple-a18`,
`apple-m1` … `apple-m4`, `apple-s4` … `apple-s10`. `-mtune=apple-*` is also accepted (verified:
`-mtune=apple-m1` compiles clean). `-mcpu` implies both ISA selection and tuning.

### 1.2 What the repo's flags produce today (verified via `-E -dM`)

| Build | Defined | NOT defined |
| --- | --- | --- |
| `ios-arm64` flags (`Makefile:303-316`) | `__aarch64__`, `__ARM_NEON`, `__ARM_FEATURE_AES/SHA2/CRYPTO`, `__ARM_ARCH 8` | `__ARM_FEATURE_CRC32`, `__ARM_FEATURE_ATOMICS`, `__ARM_FEATURE_DOTPROD` |
| `tvos-arm64` flags (`Makefile:318-331`) | same | same |
| `tvos` + `-mcpu=apple-a10` (experiment) | + `__ARM_FEATURE_CRC32` | still no `ATOMICS` |
| `tvos` + `-mcpu=apple-a12` (experiment) | + `CRC32`, `ATOMICS`, `QRDMX` | — |
| `osx` host arm64 default | `CRC32`, `ATOMICS`, `DOTPROD`, `NEON` | — |

Read: with no `-mcpu`, Apple clang's device floor for `-arch arm64` iOS/tvOS is the **A7
baseline** (ARMv8.0 + NEON + crypto, no CRC32, no LSE). macOS arm64 defaults to an
apple-m1-class baseline, so the host build already gets CRC32/LSE/DotProd for free. This is why
`simd-neon-arm64.md` T5's `__ARM_FEATURE_CRC32` guard is a no-op on today's iOS/tvOS builds and
active on macOS — by design of the guard, not by accident.

Note: [`simd-neon-arm64.md`](simd-neon-arm64.md) §Do-not-re-propose says per-SoC `-mcpu` is
"Done — #560 (rpi4, ios-arm64, etc.)". For `ios-arm64` that is **not what the Makefile shows**
(lines 303–316 carry no `-mcpu`); #560 gave iOS a correct platform block, not a CPU pin.

### 1.3 Device floors and per-core features

Feature columns verified against LLVM `AArch64.td` (`ProcAppleA7`/`A10`/`A11`…: A7/A8/A9 =
NEON+crypto **without** `FeatureCRC`; A10 adds `FeatureCRC`+`RDM`+`PAN`+`LOR`+`VH` but **not**
`FeatureLSE`; A11 = `HasV8_2aOps` ⇒ v8.1 LSE atomics mandatory; A12 = v8.3; A13 = v8.4;
A14/M1 = v8.4+).

| Chip | ISA class | NEON | crypto | CRC32 | LSE atomics |
| --- | --- | --- | --- | --- | --- |
| A7–A9 | v8.0 | yes | yes | **no** | no |
| A10 (Fusion) | v8.0+ext | yes | yes | yes | **no** |
| A10X | same as A10 | yes | yes | yes (per ProcAppleA10; A10X-specific entry not checked — UNVERIFIED) | no |
| A11 | v8.2 | yes | yes | yes | yes |
| A12 / A12X | v8.3 | yes | yes | yes | yes |
| A13, A14/M1, A15/M2, … | v8.4+ | yes | yes | yes | yes |

OS floors (verified via 2025-06 WWDC coverage; still current for the 26.x cycle):

- **tvOS:** tvOS 26 still supports **Apple TV HD (2015, A8)** → tvOS floor = **A8 = no CRC32,
  no LSE**. Apple TV 4K gen1 = A10X, gen2 (2021) = A12, gen3 (2022) = A15.
- **iOS:** iOS 26 floor = **A13** (iPhone 11 / SE2). iPadOS 26 floor = A12.
- The core's own `MINVERSION` (iOS 8.0 / tvOS 11.0, `Makefile:311/329`) is far below any
  frontend's real floor. RetroArch's and Provenance's current deployment targets were **not
  checked** (UNVERIFIED) — they, not this Makefile, define which chips can actually load the
  dylib. **Do not raise `-mcpu` past the floor of the *loosest* frontend that ships this core.**

Consequence: `-mcpu=apple-a10`+ on **tvOS** emits CRC32 instructions ⇒ SIGILL on Apple TV HD.
`-mcpu=apple-a12`+ on **iOS** is safe only if every shipping frontend requires iOS 17+
(A12 floor) — currently UNVERIFIED, so treated as unsafe.

### 1.4 Why `-mcpu` matters less here anyway

- **No ELF interposition problem:** Mach-O ld64 two-level namespace resolves same-image symbols
  directly; the entire #569 GOT/PLT issue (audit P2) never applied to Apple targets
  ([`agent/build.md`](../agent/build.md) §ELF visibility). The big Apple wins are algorithmic
  (audit P1/P3/P9), not flags.
- **Wide out-of-order cores:** every A10+ big core is a wide OoO design with large unified L2 and
  aggressive prefetch; scheduling models (`-mtune`) move little on such cores compared to
  in-order A53-class parts where `-mcpu` reorders dual-issue pairs. Expect ≤1-2% from tune-only
  changes (estimate, UNVERIFIED — the measured #515 datum is that `-O3` itself was +5.5% on
  macOS arm64).
- **`-O3`, never `-Ofast`** (#517; `Makefile:33-71` comments).
- The only *codegen-feature* upside of a CPU pin on iOS would be LSE atomics + CRC32; this core's
  hot loops are single-threaded integer interpreters that use neither (atomics: see §3.2).

### 1.5 Profiling pointer

dsymutil/Instruments/`xctrace` recipe (incl. keeping `-O3 -g` via `RELEASE_DEBUG_INFO=1`, UUID
match, `device_perf.sh`): [`device-profiling.md`](device-profiling.md); short form in
[`perf-audit-2026-08.md`](../perf-audit-2026-08.md) §Measurement recipes.

---

## 2. Qualcomm Snapdragon (Android handhelds)

### 2.1 SoCs in the wild

Common Jaguar-capable Android gaming handhelds (device↔SoC verified from 2025-26 spec sheets for
RP5/Odin 2; others widely reported, UNVERIFIED individually):

| Device class | SoC | CPU config | Kryo name |
| --- | --- | --- | --- |
| AYN Odin 1 / Retroid Pocket 3+ era | SD 845 | 4×Kryo 385 Gold (Cortex-A75-derived) + 4×Kryo 385 Silver (A55-derived) | Kryo 385 |
| Retroid Pocket 5 / Pocket Mini / Odin 1 Pro-class | SD 865 | 1×A77@2.84 + 3×A77@2.42 + 4×A55@1.8 (Kryo 585 = A77/A55-derived) — verified | Kryo 585 |
| AYN Odin 2 / Odin 2 Mini/Portal | SD 8 Gen 2 | 1×Cortex-X3 + 2×A715 + 2×A710 + 3×A510 — verified | Kryo (unnumbered) |
| 2025+ flagships | SD 8 Elite | Oryon custom cores; **no SVE, NEON only** (widely reported, UNVERIFIED) | — |
| Retroid Pocket 4 Pro (not Snapdragon) | Dimensity 1100 | 4×A78 + 4×A55 (widely reported, UNVERIFIED) | — |

Kryo 3xx/5xx are semi-custom Arm Cortex derivatives; there are **no** `-mcpu=kryo-585`-style
names in GCC/Clang — you tune them as their underlying Cortex cores.

### 2.2 `-mcpu`/`-mtune` values per generation (if you *could* pin one)

GCC AArch64 accepts combined big.LITTLE tuning names — verified in GCC docs: permissible `-mtune`
(and therefore `-mcpu`) values include `cortex-a75.cortex-a55`, `cortex-a76.cortex-a55` (GCC ≥ 8
era; the a75/a76 pairs are listed from GCC 9+; exact first version UNVERIFIED). Clang does **not**
accept the dotted pair names — use the big core alone.

| SoC | GCC | Clang/NDK |
| --- | --- | --- |
| SD 845 | `-mcpu=cortex-a75.cortex-a55` (or `-mcpu=cortex-a75`) | `-mcpu=cortex-a75` |
| SD 865 | `-mcpu=cortex-a77` (no dotted a77 pair in GCC docs read — UNVERIFIED whether one exists) | `-mcpu=cortex-a77` |
| SD 8 Gen 2 | `-mcpu=cortex-x3` | `-mcpu=cortex-x3` |

**But: none of this is shippable in the Android build.** See next.

### 2.3 NDK reality (what is actually feasible)

- `jni/Android.mk` builds whatever ABIs `APP_ABI` lists; there is no per-device axis. The
  official ABI docs state **arm64-v8a is "Armv8.0 only"** (verified) — the NDK baseline is plain
  `armv8-a`: no `+crc`, no `+crypto`, no LSE assumption at compile time.
- CRC32 is **optional in ARMv8.0 and mandatory only from ARMv8.1** (verified, Arm ARM
  DDI0596 CRC32 page). So `-march=armv8-a+crc` baked into a single Play/buildbot APK is a bet
  that no v8.0 device without CRC ever loads it. In practice every Cortex-A53/A55-based Android
  SoC ships CRC (UNVERIFIED as a universal claim — and Apple proved v8.0-without-CRC silicon
  exists), so the safe engineering position is: **don't bake `+crc`, don't bake `+crypto`**
  (crypto is a licensable option and known-absent on some older devices).
- One APK, many chips ⇒ per-device `-mcpu` is off the table. What IS feasible:
  1. **Keep baseline `armv8-a`** (today's state) — correct default.
  2. **Runtime dispatch** for the few optional-ISA wins (only candidate in this codebase:
     hardware CRC32, [`simd-neon-arm64.md`](simd-neon-arm64.md) T5 — cold path, load-time only):

     ```c
     #include <sys/auxv.h>
     #ifndef HWCAP_CRC32
     #define HWCAP_CRC32 (1UL << 7)   /* aarch64 AT_HWCAP bit */
     #endif
     use_hw = (getauxval(AT_HWCAP) & HWCAP_CRC32) != 0;
     ```

     plus a `__attribute__((target("+crc")))` (GCC/Clang AArch64, verified in GCC 14 docs) on
     the HW-path function so only that function is compiled with the extension. Full
     function-multi-versioning (`target_version`/`target_clones`, ifunc-based) also exists on
     AArch64 (GCC 14+/Clang, verified) but is overkill for one cold CRC function — and ifunc
     resolution order inside a `dlopen`ed core is an extra risk for zero gain. Prefer the
     explicit `getauxval` branch.
  3. **`-mtune=<core>` without `-mcpu`** would be ISA-safe (tuning only, no new instructions),
     but there is no single right answer across A55/A77/X3 in one APK; generic tuning is the
     NDK default for a reason. Not proposed.
- On 32-bit `armeabi-v7a`: NEON is guaranteed by NDK r21+ defaults and r26 dropped non-NEON v7
  entirely (`jni/Android.mk:18-19` comment; CI pins r26d). For AArch32 the CRC/AES hwcaps live
  in `AT_HWCAP2`, not `AT_HWCAP` (verified, Arm blog) — T5 correctly stays `__aarch64__`-only.

### 2.4 big.LITTLE scheduling

A libretro core cannot set thread affinity or governor — it is a guest in the frontend process.
Document for users instead:

- RetroArch: threaded video off for this core (single hot thread), and on Android the frame
  cadence lives or dies by whether the hot thread stays on a big core — that is the OS scheduler
  + governor's decision, not ours.
- User-side: "performance" governor / disable battery-saver / device gaming mode. Sustained
  load on SD865-class handhelds with active cooling (RP5 has a fan) holds big-core clocks;
  passively cooled devices (Odin 2) throttle later but from a faster baseline.
- Frontend-side scheduling (RetroArch `sustained_performance_mode` on Android) exists; whether
  current RetroArch builds expose it for cores is UNVERIFIED here.

---

## 3. Generic ARM64 Linux

### 3.1 Today

`Makefile:359-364`: the `arm64`/`aarch64` platform block sets linker/ARCH only — **no `-march`,
no `-mcpu`**. Codegen = toolchain default, i.e. `-march=armv8-a` on standard
`aarch64-linux-gnu` GCC (distros may configure higher; UNVERIFIED per-distro). NEON blitter is
selected by platform/ARCH name (`Makefile.common:162-163`), and #569 gives it
`-fvisibility=hidden -fno-semantic-interposition`. This is the libretro-buildbot target
(`linux-aarch64.yml`), i.e. one binary for every ARM64 Linux device — the same
single-binary constraint as Android. Baseline `armv8-a` is correct; per-chip tuning belongs to
the `rpi*` names, which already have it.

### 3.2 SVE / SVE2 — verdict: not applicable

| Chip in scope | SVE/SVE2 |
| --- | --- |
| Pi 3/4/5 (A53/A72/A76) | none |
| SD 865 (A77) | none |
| SD 8 Gen 1/2/3 (X2/X3/A7xx, Armv9) | SVE2 @ VL=128 (X3/A715 verified); Android exposes via `HWCAP2_SVE2` (kernel mechanism verified; per-device kernel enablement UNVERIFIED) |
| SD 8 Elite (Oryon) | none (widely reported, UNVERIFIED) |
| Apple A-series, M1–M3 | none |
| Apple M4 | SME/SME2, **not** SVE for NEON-style code (widely reported, UNVERIFIED against Apple docs) |

So: of every device this core actually targets, only the SD 8 Gen 2-class handhelds have SVE2 at
all, at VL=128 — the same 128-bit width as NEON, reached only via runtime dispatch inside a
single binary, for kernels ([`simd-neon-arm64.md`](simd-neon-arm64.md) T1–T4) that are small,
already NEON-shaped, and mostly ≤128-bit wide. **No SVE work is justified.** NEON-only
intrinsics, compile-time guarded, remain the policy.

### 3.3 outline-atomics

`-moutline-atomics` is default-on for AArch64 GCC since 10.1, and default for Clang on
Linux/Android targets since D93585 (Clang 12, requires libgcc ≥ 9.3.1 or compiler-rt) — both
verified. It affects only `__atomic`/`_Atomic` operations (call into `__aarch64_ldadd*` etc.
that pick LSE at runtime). This core's emulation path is single-threaded and its hot loops use
no C atomics (netplay/voice paths use sockets, not shared-memory atomics — spot-checked, not
exhaustively audited: UNVERIFIED as an absolute). Verdict: **irrelevant here; leave the default
alone.** On `rpi5*`, `-mcpu=cortex-a76` already implies LSE so any atomic would inline anyway.

---

## 4. 32-bit ARM (rpi2/3 32-bit userland, armv7 Android)

### 4.1 What changes vs AArch64 NEON

- 32 × 64-bit D registers = 16 × 128-bit Q registers (vs 32 × 128-bit V on AArch64): heavier
  register pressure in wide kernels; keep live vector counts small.
- No float64 lanes (`float64x2_t` does not exist — verified: armv7 clang errors).
- Intrinsics operate through the ACLE common subset; the A64-only additions below are simply
  not declared for A32.
- Alignment: `vld1q_u8`-family intrinsics have no alignment requirement on either ISA
  (alignment-hinted encodings exist on A32 but intrinsics don't emit them unless the pointer
  type promises it) — no porting hazard for the planned loads/stores.
- `vrev16q_u8` **exists on ARMv7 NEON** (VREV16 is an ARMv7 Advanced-SIMD instruction) —
  verified by compiling for `armv7-none-linux-gnueabihf`. The T3 scanline plan's byte-swap
  approach is portable as written.

**Compile check (2026-08-27, clang 17, `-target armv7-none-linux-gnueabihf -mfpu=neon`):** every
intrinsic named in the parallel spec's code samples — `vld1_u8`, `vst1_u8`, `vbsl_u8`,
`vceq_u16`, `vdup_n_u16`, `vrev16q_u8`, `vshrq_n_u16`, `vshll_n_u16`, `vld4_u8`, `vqaddq_s16`,
`vld1q_s16`, `vst1q_s16`, `vdupq_n_s16` — **compiles for ARMv7**. The A64-only probes
(`vaddvq_u32`, `vqtbl1q_u8`, `vzip1q_u32`, `vrbit_u8`, `float64x2_t`) all **fail** on the same
target. GCC-armhf behavior assumed identical via ACLE (UNVERIFIED on this host — no cross gcc;
re-check in the arm-linux-gnueabihf container per [`agent/build.md`](../agent/build.md)).

### 4.2 AArch64-only intrinsics to avoid (so SIMD tasks stay rpi2/rpi3-32-bit portable)

Families, not exhaustive (ACLE marks each as A64-only):

| Family | Members | 32-bit replacement |
| --- | --- | --- |
| Horizontal reductions | `vaddv*`, `vaddlv*`, `vmaxv*`, `vminv*` | `vpadd`/`vpmax` pyramid + lane extract |
| Full-width table lookup | `vqtbl1q`–`vqtbl4q`, `vqtbx*` | `vtbl1`–`vtbl4` (64-bit tables) |
| One-instruction permutes | `vzip1/vzip2`, `vuzp1/vuzp2`, `vtrn1/vtrn2` (q and d forms) | A32 `vzip/vuzp/vtrn` (two-result struct forms) |
| Scalar-pairwise f64 | `vpaddd_*`, `vpadds_*` | n/a — avoid |
| Bit reverse | `vrbit*` | scalar `__builtin_bitreverse` or restructure |
| f64 vectors | `float64x1_t`/`float64x2_t` + all `*_f64` | keep float math scalar (hot loops are integer anyway) |
| Widening "high" ops | `vmovl_high_*`, `vaddl_high_*`, `vmull_high_*`, `vshll_high_n_*` | `vget_high_*` + the base op (2 intrinsics) |
| Misc A64 | `vcopy*_lane*`, `vdupq_laneq_*`, `vrnd*` on f32 vectors (v8 FP rounding), `vsqrtq_f32`, `vcvt*_f64` | avoid / scalar |
| CRC32 | `__crc32b/h/w/d` under `__ARM_FEATURE_CRC32` | already `__aarch64__`-gated in T5 — correct |

Rule of thumb already followed by the parallel spec: if a NEON kernel compiles with
`-target armv7-none-linux-gnueabihf -mfpu=neon`, it runs on everything from rpi2 up. Add that
one-line compile check to any new `*_simd_neon.{c,h}` before review.

---

## 5. Feature detection matrix

### Compile-time macros

| Macro | Set by | Meaning / trap |
| --- | --- | --- |
| `__ARM_NEON` | GCC/Clang, both ISAs | AArch64: always. 32-bit: only with NEON `-mfpu`. (Legacy 32-bit spelling `__ARM_NEON__` also exists.) |
| `__aarch64__` | GCC/Clang | 64-bit ISA (implies NEON) |
| `_M_ARM64` | MSVC | 64-bit ARM Windows (buildbot MSVC rows, `Makefile.common:190`: no x86 intrinsics there; blitter NEON path uses GCC-style intrinsics — MSVC ARM64 currently lands scalar) |
| `__ARM_FEATURE_CRC32` | flags say CRC is unconditionally available | today: macOS arm64 host, `rpi3+` (`-mcpu=cortex-a53+` — expected per GCC core defs, UNVERIFIED on this host), libnx (`+crc` explicit, `Makefile:467`). NOT iOS/tvOS/Android/generic-arm64. |
| `__ARM_FEATURE_CRYPTO` | deprecated umbrella (AES+SHA2); prefer `__ARM_FEATURE_AES` / `__ARM_FEATURE_SHA2` | set on all Apple builds (A7 baseline includes crypto) |
| `__ARM_FEATURE_ATOMICS` | LSE guaranteed | macOS host yes; iOS/tvOS default no (A7 floor) |
| `__ARM_FEATURE_SVE` | SVE codegen enabled | never set on any target in scope (§3.2) |

### Runtime detection

| Platform | Mechanism |
| --- | --- |
| Linux / Android 64-bit | `getauxval(AT_HWCAP)` — `HWCAP_CRC32`, `HWCAP_AES`…; `AT_HWCAP2` for SVE2 etc. |
| Linux / Android 32-bit process | ARMv8 features are in `AT_HWCAP2` (NEON in `AT_HWCAP`) — different split, easy to get wrong |
| Apple | `sysctlbyname("hw.optional.armv8_crc32", …)` / newer `hw.optional.arm.FEAT_*` names (names widely documented; not exercised here — UNVERIFIED) |
| Windows ARM64 | `IsProcessorFeaturePresent(PF_ARM_V8_CRC32_INSTRUCTIONS_AVAILABLE)` (UNVERIFIED) |

### Which approach each planned task should use

| Task ([`simd-neon-arm64.md`](simd-neon-arm64.md)) | Detection | Why |
| --- | --- | --- |
| T1 bswap, T2 OP phrase, T3 scanlines, T4 voice mix | **compile-time only** (`BLITTER_SIMD_HAVE_NEON` / `__ARM_NEON`) | builds are per-platform; NEON is a build-time fact on every target that gets these files (`Makefile.common:154-167`, `jni/Android.mk:25-28`); zero runtime cost, zero dispatch state |
| T5 hardware CRC32 | compile-time `__ARM_FEATURE_CRC32 && __aarch64__` as specced — covers macOS + rpi3+. **If** Android/generic-arm64 coverage is ever wanted: add the §2.3 `getauxval` + `target("+crc")` branch; do NOT bake `+crc` into `-march` | single-binary targets can't promise CRC at compile time; cold path so a runtime branch costs nothing |
| Anything SVE | none | rejected, §3.2 |

---

## 6. Recommended settings changes — **all: requires approval / not applied**

Conservative by policy: #516 (flags live in platform blocks, last `-O` wins), #517 (no fast-math),
#560 (`-mcpu` only where the name = the silicon). Every row is a *proposal*; nothing has been
edited.

| # | Change | Exact proposed diff | Recommendation | Status |
| --- | --- | --- | --- | --- |
| 1 | tvOS CPU pin | none — explicitly rejected: floor is Apple TV HD (A8); `-mcpu=apple-a10`+ emits CRC32 ⇒ SIGILL (§1.2/1.3) | keep as-is | rejected, do not apply |
| 2 | iOS CPU pin | opt-in knob only, e.g. in the ios block (`Makefile:303-316`): `ifeq ($(IOS_MCPU),a12)` → `CFLAGS += -mcpu=apple-a12` | do NOT default until the shipping frontends' deployment floors are verified ≥ iOS 17 (A12); expected win small on OoO cores (§1.4) — measure with `device_perf.sh` first | requires approval / not applied |
| 3 | tvOS/iOS tune-only | `CFLAGS += -mtune=apple-a10` (tvOS) — ISA-safe, changes scheduling only | only after an A/B on A8/A10X hardware shows a win; otherwise noise risk for zero verified gain | requires approval / not applied |
| 4 | generic `arm64`/`aarch64` block | **stay `-march=armv8-a` (toolchain default)** — single-binary buildbot target; do not add `+crc`/`+crypto`/`-mcpu` | keep as-is | no change proposed |
| 5 | `jni/Android.mk` `-march` extensions | none — arm64-v8a stays baseline `armv8-a` (§2.3). If T5-on-Android is wanted, that is a *source* change (getauxval dispatch), not a build-flag change | keep as-is | rejected for flags; source-side dispatch requires its own approval |
| 6 | Switch (`libnx`) | note only: `Makefile:467` passes both `-mtune=cortex-a57` and `-mcpu=cortex-a57+crc+fp+simd`; `-mcpu` implies `-mtune`, so the pair is redundant (harmless — `-mcpu` wins for the overlapping part per GCC docs). Cleanup candidate, zero perf impact | optional cleanup | requires approval / not applied |
| 7 | LTO default | unchanged — `LTO=1` knob stays opt-in until the Pi A/B ([`simd-neon-arm64.md`](simd-neon-arm64.md) T6; `Makefile:1046-1055`) | keep as-is | pending measurement |
| 8 | New-file policy | any new `*_simd_neon.*` must pass the §4.2 armv7 compile check before review | process note, no build change | adopt in review checklist |

If #2 or #3 is ever applied: the flag goes **inside the platform block** (never the command
line), no `-O` is touched, and `test/tools/simd_matrix_check.sh` gains a row asserting the new
flag — the same guard pattern the rpi rows use ([`agent/build.md`](../agent/build.md)).

---

## Related documents

- [`simd-neon-arm64.md`](simd-neon-arm64.md) — the SIMD task specs this doc constrains (T1–T6).
- [`perf-audit-2026-08.md`](../perf-audit-2026-08.md) — audit P1–P9; why algorithmic work
  dominates flags on Apple targets.
- [`agent/build.md`](../agent/build.md) — rpi per-SoC table, #516/#517/#569 detail, C89 exempt
  list.
- [`device-profiling.md`](device-profiling.md) — Instruments/xctrace on-device recipe.
