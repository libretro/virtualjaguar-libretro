#!/usr/bin/env bash
# Deterministic A/B performance measurement by counting retired instructions
# under valgrind's cachegrind (issue #534).
#
# WHY THIS EXISTS
# ---------------
# Every wall-clock number this project produces depends on a quiet host, and
# that dependency has produced two wrong answers already:
#
#   - #515 measured -O3 at +12.5% sequentially and -11.7% interleaved from THE
#     SAME TWO BINARIES, minutes apart.  The sign flipped on host drift.
#   - #520's timing phase was blocked outright (load 19.08 on 12 cores), so the
#     measurement simply did not happen.
#
# This emulator is deterministic, so the work it performs for a fixed ROM and
# frame count is a fixed number of instructions.  cachegrind counts exactly
# that, in simulation, and returns bit-identical numbers across runs and across
# machines for the same binary.  Not low-variance: zero-variance.
#
# NO LOAD-AVERAGE GATE — ON PURPOSE.  test/tools/opt_ab.sh refuses to run above
# one load per core, and that is correct there: it measures wall time.  Ir is
# load-INSENSITIVE (cachegrind simulates; a busy host makes the run slower, not
# different).  The whole point of this script is that it works on a loaded dev
# box and on a shared CI runner.  Do not "fix" this by adding a load guard back.
#
# WHAT Ir IS NOT
# --------------
# Ir is not wall time.  It models neither branch prediction nor ILP nor real
# cache behaviour (the simulated D1/LL misses close only part of that gap).  A
# change can legitimately RAISE Ir and still be faster (inlining, unrolling),
# and a change that removes dependent loads — exactly the #532 class — helps
# latency more than it helps instruction count, so a small Ir delta there
# understates the win.  Treat this as a gate and a regression detector, not as
# a replacement for wall-clock measurement on real hardware.
#
# Absolute Ir is NOT comparable across architectures (an arm64 container and an
# x86_64 CI runner disagree by construction).  Only the within-run A/B delta is
# meaningful, so never store a baseline Ir without keying it by runner arch.
#
# USAGE
# -----
#   # flag flip (mirrors opt_ab.sh: make arguments passed verbatim)
#   test/tools/ir_ab.sh 'OPT_LEVEL=-O2' 'OPT_LEVEL=-O3'
#
#   # source change: two git refs (this is what a PR-vs-base comparison needs)
#   test/tools/ir_ab.sh --ref libretro/develop HEAD
#
#   # instrument validation: same source both arms, Ir must come out IDENTICAL
#   test/tools/ir_ab.sh --selftest
#
#   # single-arm workload profile: where does a ROM's Ir actually go?
#   # (this is how #533's "is this ROM a representative benchmark" question
#   #  gets answered -- per-function Ir, no source changes, no sampling)
#   IR_ROM=test/roms/yarc.j64 test/tools/ir_ab.sh --profile
#
# Env knobs:
#   IR_ROM        ROM to run           (default test/roms/jagniccc.j64).
#                 A path (absolute or repo-relative) OR a find-rom.sh glob:
#                 IR_ROM='Alien vs Predator*' finds it in the private corpus
#                 wherever that machine keeps it.  Resolved to a physical
#                 path before the container mount -- see ROM PATH below.
#   IR_FRAMES     frames per arm       (default 300; cachegrind is ~50-100x slow,
#                                       so this costs ~3 min per arm.  Drop it
#                                       for a quick --selftest; do NOT drop it
#                                       for a real A/B without checking the
#                                       workload still covers your change --
#                                       see WORKLOAD below.)
#   IR_MAKE_ARGS  extra make args applied to BOTH arms (ref mode)
#   IR_JOBS       build parallelism    (default nproc)
#   IR_GATE=1     exit 3 when B regresses by more than IR_THRESHOLD
#   IR_THRESHOLD  regression threshold in percent (default 0.5)
#   IR_KEEP=1     keep the work directory
#   IR_IMAGE      container image tag  (default vj-cachegrind)
#   IR_NO_CONTAINER=1  fail instead of falling back to a container
#
# HOW IT AVOIDS THE CHIMERA-BINARY CLASS
# --------------------------------------
# Each arm is built in its OWN pristine source tree copied into a scratch dir,
# never in the working tree, so there is no stale-object path at all (stronger
# than `make clean`, which the repo's own notes record as insufficient when a
# compile-affecting switch is not in BUILD_AXES).  The two libraries are then
# SHA-256'd and the run aborts if they are identical, because two identical
# binaries cannot produce a meaningful A/B.
#
# THE RUNNER IS BUILT ONCE, FROM ARM A, AND USED FOR BOTH ARMS.  It is the
# instrument; it must not vary between arms.  Its own instruction count is
# included in both totals and cancels in the delta.
#
# Determinism also requires identical argv and environment between arms, so the
# measured process runs under `env -i` with a fixed PATH (this also scrubs any
# VJ_* debug knobs the caller may have exported), and the per-arm paths are
# constructed to be the SAME LENGTH ("$WORK/a" vs "$WORK/b").  Argument bytes
# land on the guest stack; unequal lengths perturb it.
#
# WORKLOAD
# --------
# test/tools/frame_hash_ab drives the core: it has no wall-clock read anywhere
# in its output path (unlike test_benchmark, whose FPS printf differs run to
# run and would inject noise into Ir), and it emits a per-frame framebuffer
# hash.  That CSV doubles as an accuracy gate: byte-identical CSVs across arms
# is direct evidence that the change altered nothing the emulated machine can
# see.  An A/B whose CSVs differ is reported, not silently scored.
#
# CHOOSING THE ROM AND FRAME COUNT IS PART OF THE MEASUREMENT (issue #533).  An
# A/B on a workload that never enters the code you changed reports 0.0% forever
# and looks perfectly healthy.  Measured here with --profile:
#
#   jagniccc.j64 @  90 frames   still in BIOS boot.  ZERO GPU-interpreter
#                               instructions; 37% of Ir is the 68K traceback
#                               hook.  Useless as a GPU/DSP/blitter gate.
#   jagniccc.j64 @ 300 frames   GPU interp 38.6%, DSP interp 22.3%, blitter
#                               7.1%, 68K ~5.9%.  THE DEFAULT: it is the
#                               NICCC-2000 port, its GPU renderer starts only
#                               after the boot window, and this is the cheapest
#                               frame count that reaches the steady-state mix
#                               (600 frames gives the same shape for 2.7x the
#                               simulation cost).
#   yarc.j64     @  90 frames   GPU interp 71.2%, blitter 17.5%, DSP 0.08%.
#                               The GPU amplifier -- but its opcode mix is
#                               jr-heavy (#533), so per-instruction loop
#                               overhead is an unusually large share of it and
#                               a loop-bookkeeping change scores HIGH here
#                               relative to a real game.
#
# Note the Ir denominator includes this harness: frame_hash_ab's own hashing
# (`ab_video`) is ~4.4% of total on jagniccc@300 and 13.4% on jagniccc@90.  An
# ir_ab percentage is therefore a share of emulator-plus-instrument work and is
# not directly comparable to a sampling profiler's "% of process time".

set -euo pipefail

REPO=$(cd "$(dirname "$0")/../.." && pwd)

MODE=makeargs
ARM_A=""; ARM_B=""
ORIG_ARGS=("$@")   # re-passed verbatim to the in-container re-exec below
case "${1:-}" in
  --selftest) MODE=selftest; shift ;;
  --profile)  MODE=profile; shift ;;
  --ref)      MODE=ref; shift
              ARM_A="${1:?--ref needs two refs}"; ARM_B="${2:?--ref needs two refs}"; shift 2 ;;
  -h|--help)  sed -n '2,80p' "$0"; exit 0 ;;
  *)          ARM_A="${1:?usage: ir_ab.sh <make-args-A> <make-args-B> | --ref A B | --selftest}"
              ARM_B="${2:?usage: ir_ab.sh <make-args-A> <make-args-B> | --ref A B | --selftest}"
              shift 2 ;;
esac

ROM_REQ="${IR_ROM:-test/roms/jagniccc.j64}"
FRAMES="${IR_FRAMES:-300}"
JOBS="${IR_JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)}"
THRESHOLD="${IR_THRESHOLD:-0.5}"
IMAGE="${IR_IMAGE:-vj-cachegrind}"

# --------------------------------------------------------------- ROM path ---
# IR_ROM is a path (absolute or repo-relative) OR a find-rom.sh glob.  The
# glob form is what makes an invocation portable: the private corpus is laid
# out differently on every machine, so `IR_ROM='Alien vs Predator*'` keeps
# working where a hardcoded spelling silently stops matching -- the failure
# mode scripts/find-rom.sh exists to prevent (it is how the Skyhammer
# clipping sentinel went inert).
#
# The result is then resolved to a PHYSICAL path, and that is not cosmetic:
# test/roms/private is a SYMLINK to a tree outside every checkout, and
# `podman run -v` cannot create a bind mountpoint underneath a symlinked
# path.  crun fails with
#     creating `.../test/roms/private/ROMS/<title>.jag`: openat2
#     `.../test/roms/private`: No such file or directory
# so before this resolution NO private-corpus ROM could be measured at all.
resolve_rom() {  # resolve_rom <path-or-glob> -> physical path on stdout
  local r="$1" d b t hops=0
  case "$r" in /*) ;; *) [ -f "$REPO/$r" ] && r="$REPO/$r" ;; esac
  if [ ! -f "$r" ]; then
    r=$( cd "$REPO" && bash scripts/find-rom.sh "$1" 2>/dev/null ) || return 1
    [ -n "$r" ] || return 1
    case "$r" in /*) ;; *) r="$REPO/$r" ;; esac
  fi
  [ -f "$r" ] || return 1
  d=$(cd "$(dirname "$r")" && pwd -P) || return 1
  b=$(basename "$r")
  # ...and a symlinked FILE inside the corpus resolves too.  Bounded, so a
  # symlink cycle reports rather than spins.
  while [ -L "$d/$b" ]; do
    hops=$((hops + 1))
    [ "$hops" -gt 16 ] && { echo "ir_ab: symlink loop at $d/$b" >&2; return 1; }
    t=$(readlink "$d/$b")
    case "$t" in
      /*) d=$(cd "$(dirname "$t")" && pwd -P) || return 1 ;;
      *)  d=$(cd "$d/$(dirname "$t")" && pwd -P) || return 1 ;;
    esac
    b=$(basename "$t")
  done
  [ -f "$d/$b" ] || return 1
  printf '%s/%s\n' "$d" "$b"
}

ROM=$(resolve_rom "$ROM_REQ") || {
  echo "ir_ab: no ROM matching '$ROM_REQ'" >&2
  echo "  IR_ROM takes a path (absolute or repo-relative) or a find-rom.sh" >&2
  echo "  glob, e.g. IR_ROM='Alien vs Predator*'.  Private-corpus ROMs live" >&2
  echo "  under test/roms/private (a symlink; ROMS_PRIVATE_ROOT overrides)." >&2
  exit 1; }
[ "$ROM" = "$ROM_REQ" ] || echo "ir_ab: ROM '$ROM_REQ' -> $ROM"

# ---------------------------------------------------------------- work dir ---
# Prepared on the host (needs git); the build+measure half needs no git at all,
# which is what lets the container step run on a bare toolchain image.
if [ -n "${IR_AB_WORK:-}" ]; then
  WORK="$IR_AB_WORK"
else
  WORK=$(mktemp -d "${TMPDIR:-/tmp}/ir_ab.XXXXXX")
  [ "${IR_KEEP:-0}" = 1 ] || trap 'command rm -rf "$WORK"' EXIT
fi

snapshot_worktree() {  # snapshot_worktree <dest>
  mkdir -p "$1"
  tar -cf - -C "$REPO" \
      --exclude=.git --exclude=.claude --exclude=test/roms/private \
      --exclude=test/baselines --exclude='*.o' --exclude='*.so' \
      --exclude='*.dylib' --exclude='*.dll' --exclude=.build-config . \
    | tar -xf - -C "$1"
}

snapshot_ref() {       # snapshot_ref <ref> <dest>
  mkdir -p "$2"
  # `git archive` rather than `git worktree add`/`git stash`: it never writes
  # to the repository, so a concurrent session's worktree metadata cannot be
  # disturbed and there is no stash-cycle chimera window.
  git -C "$REPO" archive --format=tar "$1" | tar -xf - -C "$2"
}

if [ "${IR_AB_PREPARED:-0}" != 1 ]; then
  case "$MODE" in
    ref)      echo "ir_ab: snapshotting $ARM_A -> a, $ARM_B -> b"
              snapshot_ref "$ARM_A" "$WORK/a"; snapshot_ref "$ARM_B" "$WORK/b" ;;
    selftest) echo "ir_ab: snapshotting working tree -> a, b (selftest)"
              snapshot_worktree "$WORK/a"; snapshot_worktree "$WORK/b" ;;
    profile)  echo "ir_ab: snapshotting working tree -> a (profile)"
              snapshot_worktree "$WORK/a" ;;
    makeargs) echo "ir_ab: snapshotting working tree -> a, b"
              snapshot_worktree "$WORK/a"; snapshot_worktree "$WORK/b" ;;
  esac
fi

# --------------------------------------------------------------- container ---
# Cachegrind is Linux-only in practice (no valgrind on Apple Silicon), so on a
# Mac the whole build+measure half re-execs inside a Linux container.  The
# source trees are already prepared on the host, so only $WORK and the ROM need
# to cross the boundary and the image needs no git.
if ! command -v valgrind >/dev/null 2>&1 && [ "${IR_AB_IN_CONTAINER:-0}" != 1 ]; then
  [ "${IR_NO_CONTAINER:-0}" = 1 ] && { echo "ir_ab: valgrind not found" >&2; exit 1; }
  ENGINE=$(command -v podman || command -v docker) || {
    echo "ir_ab: valgrind not found and no podman/docker to fall back on." >&2
    echo "  Linux:  apt-get install valgrind" >&2
    echo "  macOS:  install podman (valgrind does not support Apple Silicon)" >&2
    exit 1; }
  if ! "$ENGINE" image exists "$IMAGE" 2>/dev/null && \
     ! "$ENGINE" image inspect "$IMAGE" >/dev/null 2>&1; then
    echo "ir_ab: building container image $IMAGE"
    printf 'FROM docker.io/library/ubuntu:24.04\nRUN apt-get update && apt-get install -y --no-install-recommends build-essential valgrind python3 ca-certificates && rm -rf /var/lib/apt/lists/*\n' \
      | "$ENGINE" build -t "$IMAGE" -f - . >/dev/null
  fi
  echo "ir_ab: no host valgrind -- re-executing under $ENGINE ($IMAGE)"
  # Run as a CHILD, not exec.  `exec` replaces this shell and destroys the EXIT
  # trap set above, and the container run re-enters this script with
  # IR_AB_WORK set, which skips the trap-setup branch there too -- so with
  # `exec` nothing ever cleans up and every macOS run left two full source
  # snapshots plus build trees in $TMPDIR forever, despite IR_KEEP defaulting
  # to 0.  Running it as a child keeps the host trap alive to fire below.
  rc=0
  "$ENGINE" run --rm \
      -v "$WORK:$WORK" -v "$ROM:$ROM:ro" -v "$REPO:$REPO:ro" \
      -e IR_AB_IN_CONTAINER=1 -e IR_AB_PREPARED=1 -e "IR_AB_WORK=$WORK" \
      -e "IR_ROM=$ROM" -e "IR_FRAMES=$FRAMES" -e "IR_THRESHOLD=$THRESHOLD" \
      -e "IR_GATE=${IR_GATE:-0}" -e "IR_MAKE_ARGS=${IR_MAKE_ARGS:-}" \
      -e "IR_JOBS=${IR_JOBS:-}" \
      -w "$WORK" "$IMAGE" \
      bash "$REPO/test/tools/ir_ab.sh" ${ORIG_ARGS[@]+"${ORIG_ARGS[@]}"} || rc=$?
  exit "$rc"
fi

command -v valgrind >/dev/null 2>&1 || { echo "ir_ab: valgrind missing" >&2; exit 1; }

# ------------------------------------------------------------------ builds ---
case "$(uname -s)" in
  Darwin) LIB=virtualjaguar_libretro.dylib ;;
  *)      LIB=virtualjaguar_libretro.so ;;
esac

build() {  # build <tag> <make-args>
  # shellcheck disable=SC2086
  ( cd "$WORK/$1" && make -j"$JOBS" $2 ) >"$WORK/build.$1.log" 2>&1 || {
      echo "ir_ab: build failed for arm $1 ($2); tail of log:" >&2
      tail -20 "$WORK/build.$1.log" >&2; exit 1; }
  printf '  arm %s  %s  %s\n' "$1" \
    "$(sha256sum "$WORK/$1/$LIB" 2>/dev/null || shasum -a 256 "$WORK/$1/$LIB")" "$2" \
    | cut -c1-90
}

if [ "$MODE" = profile ]; then
  echo "ir_ab: building arm a with debug info (per-line Ir attribution)"
  build a "RELEASE_DEBUG_INFO=1 ${IR_MAKE_ARGS:-}"
else
  echo "ir_ab: building both arms (one session, independent trees)"
  case "$MODE" in
    makeargs) build a "$ARM_A"; build b "$ARM_B" ;;
    *)        build a "${IR_MAKE_ARGS:-}"; build b "${IR_MAKE_ARGS:-}" ;;
  esac
fi

if [ "$MODE" != profile ]; then
HA=$( (sha256sum "$WORK/a/$LIB" 2>/dev/null || shasum -a 256 "$WORK/a/$LIB") | cut -d' ' -f1)
HB=$( (sha256sum "$WORK/b/$LIB" 2>/dev/null || shasum -a 256 "$WORK/b/$LIB") | cut -d' ' -f1)
if [ "$MODE" = selftest ]; then
  [ "$HA" = "$HB" ] || echo "ir_ab: NOTE selftest arms differ in SHA (nondeterministic build?)"
elif [ "$HA" = "$HB" ]; then
  echo "ir_ab: ABORT -- both arms produced a byte-identical library." >&2
  echo "  The change under test did not reach the compiler.  A/B is meaningless." >&2
  exit 2
fi
fi

# ------------------------------------------------------------------ runner ---
# Built once, from arm A, and used for BOTH arms: the instrument must not vary.
echo "ir_ab: building workload runner (from arm a)"
cc -O2 -Wall -std=c99 -I"$WORK/a" -I"$WORK/a/test/harness" \
   -I"$WORK/a/libretro-common/include" -o "$WORK/runner" \
   "$WORK/a/test/tools/frame_hash_ab.c" "$WORK/a/test/harness/harness.c" \
   -ldl -lm 2>"$WORK/runner.log" || {
     echo "ir_ab: runner build failed:" >&2; cat "$WORK/runner.log" >&2; exit 1; }

# ----------------------------------------------------------------- measure ---
measure() {  # measure <tag>
  # env -i: identical, minimal environment for both arms (also scrubs VJ_*
  # debug knobs).  Paths are equal-length by construction -- see header.
  env -i PATH=/usr/bin:/bin:/usr/local/bin HOME=/tmp \
    valgrind --tool=cachegrind --cache-sim=yes --branch-sim=no \
      --cachegrind-out-file="$WORK/cg.$1" \
      "$WORK/runner" "$WORK/$1/$LIB" "$ROM" --frames "$FRAMES" \
      --csv "$WORK/$1.csv" >"$WORK/run.$1.log" 2>&1 || {
        echo "ir_ab: measured run failed for arm $1:" >&2
        tail -30 "$WORK/run.$1.log" >&2; exit 1; }
}

echo "ir_ab: measuring arm a ($FRAMES frames, $(basename "$ROM"))"; measure a

if [ "$MODE" = profile ]; then
  # Per-function/per-line Ir.  This survives inlining in a way that a sampling
  # profiler's symbol attribution does not -- #520 §4 recorded ~23 of the 64
  # GPU opcode bodies being inlined into executeOpcode, which biases any
  # symbol-level split toward overstating dispatch.
  echo
  echo "  workload profile: $(basename "$ROM"), $FRAMES frames"
  echo
  # Parsed straight out of the cachegrind file rather than through
  # cg_annotate: the annotator's table layout has changed between valgrind
  # releases, while the callgrind-format `fn=` records it reads have not.
  python3 - "$WORK/cg.a" <<'PY'
import re, sys, collections
names, per_fn, total = {}, collections.Counter(), 0
cur = None
for line in open(sys.argv[1]):
    line = line.rstrip('\n')
    if line.startswith('fn='):
        m = re.match(r'fn=(?:\((\d+)\)\s*)?(.*)$', line)
        cid, nm = m.group(1), m.group(2)
        if cid and nm:
            names[cid] = nm
        cur = nm if nm else names.get(cid, '???')
    elif line.startswith(('fl=', 'fi=', 'fe=', 'ob=')):
        continue
    elif line and (line[0].isdigit() or line[0] in '+-*'):
        parts = line.split()
        if len(parts) >= 2 and cur is not None:
            try:
                ir = int(parts[1])
            except ValueError:
                continue
            per_fn[cur] += ir
            total += ir
print(f'  {"Ir":>16} {"share":>8}  function')
print('  ' + '-' * 70)
for fn, n in per_fn.most_common(25):
    print(f'  {n:>16,} {100.0*n/total:>7.2f}%  {fn[:52]}')
print(f'\n  total Ir attributed: {total:,}')
PY
  echo
  echo "  Read this as: how much of the ROM's total work each engine does."
  echo "  A benchmark ROM whose GPU time is dominated by one branch opcode is"
  echo "  measuring a spin loop, not a rasteriser (issue #533)."
  exit 0
fi

echo "ir_ab: measuring arm b ($FRAMES frames, $(basename "$ROM"))"; measure b

# ------------------------------------------------------------------ report ---
if cmp -s "$WORK/a.csv" "$WORK/b.csv"; then
  ACC="identical (frame-hash CSVs match byte for byte)"
else
  ACC="DIFFERENT -- the arms do not emulate identically; the delta below mixes a behaviour change with a cost change"
fi

A_LABEL="$ARM_A" B_LABEL="$ARM_B" MODE="$MODE" ACC="$ACC" \
THRESHOLD="$THRESHOLD" GATE="${IR_GATE:-0}" \
python3 - "$WORK/cg.a" "$WORK/cg.b" <<'PY'
import sys, os

def read(path):
    ev, summ = None, None
    for line in open(path):
        if line.startswith('events:'):
            ev = line.split(':', 1)[1].split()
        elif line.startswith('summary:'):
            summ = [int(x) for x in line.split(':', 1)[1].split()]
    if not ev or not summ:
        sys.exit(f'ir_ab: cannot parse cachegrind output {path}')
    d = dict(zip(ev, summ))
    return {
        'Ir':  d.get('Ir', 0),
        'D1':  d.get('D1mr', 0) + d.get('D1mw', 0),
        'LL':  d.get('DLmr', 0) + d.get('DLmw', 0) + d.get('ILmr', 0),
        'I1':  d.get('I1mr', 0),
    }

a, b = read(sys.argv[1]), read(sys.argv[2])
mode = os.environ['MODE']
la = os.environ['A_LABEL'] or 'a'
lb = os.environ['B_LABEL'] or 'b'
if mode == 'selftest':
    la, lb = 'run 1', 'run 2'

print()
print(f'  {"metric":<10}{"arm A":>20}{"arm B":>20}{"delta":>12}')
print(f'  {"-"*62}')
for k, name in (('Ir', 'Ir'), ('I1', 'I1 miss'), ('D1', 'D1 miss'), ('LL', 'LL miss')):
    va, vb = a[k], b[k]
    d = (vb / va - 1) * 100 if va else 0.0
    print(f'  {name:<10}{va:>20,}{vb:>20,}{d:>+11.3f}%')
print()
print(f'  A = {la}')
print(f'  B = {lb}')
print(f'  emulated output: {os.environ["ACC"]}')

ir_d = (b['Ir'] / a['Ir'] - 1) * 100 if a['Ir'] else 0.0
thr = float(os.environ['THRESHOLD'])
if mode == 'selftest':
    same = a['Ir'] == b['Ir']
    print()
    print('  DETERMINISM: ' + ('PASS -- Ir is identical, not merely close.'
                               if same else
                               f'FAIL -- Ir differs by {b["Ir"]-a["Ir"]:+,} '
                               'instructions.  Do not trust any A/B from this '
                               'host until the cause is found (environment '
                               'drift, ASLR-sensitive code, or a wall-clock '
                               'read in the measured path).'))
    verdict = 'DETERMINISTIC' if same else 'NONDETERMINISTIC'
else:
    if abs(ir_d) < thr:
        verdict = 'NO-CHANGE'
        print(f'\n  VERDICT: |{ir_d:+.3f}%| is under the {thr}% threshold -- no Ir change.')
    elif ir_d < 0:
        verdict = 'IMPROVED'
        print(f'\n  VERDICT: B executes {-ir_d:.3f}% fewer instructions.')
    else:
        verdict = 'REGRESSED'
        print(f'\n  VERDICT: B executes {ir_d:.3f}% MORE instructions.')
    print('  (Ir is a work count, not a clock.  A load-elimination change '
          'understates here;\n   an inlining/unrolling change can raise Ir and '
          'still be faster.)')

print(f'\nIR_AB_RESULT verdict={verdict} ir_a={a["Ir"]} ir_b={b["Ir"]} '
      f'delta_pct={ir_d:.4f} d1_a={a["D1"]} d1_b={b["D1"]} '
      f'll_a={a["LL"]} ll_b={b["LL"]}')

if verdict == 'REGRESSED' and os.environ.get('GATE') == '1':
    sys.exit(3)
if verdict == 'NONDETERMINISTIC':
    sys.exit(4)
PY
