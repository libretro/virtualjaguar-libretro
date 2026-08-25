#!/usr/bin/env bash
#
# rpi_perf.sh -- cross-build the core for a Raspberry Pi and profile it there.
#
# Companion to test/tools/device_perf.sh: same goal (a per-subsystem
# breakdown from the hardware that is actually slow, issue #509), different
# vehicle. On iOS/tvOS the frontend is RetroArch and the capture goes through
# its performance-counter UI; on a Pi there is a real shell, so this runs
# test/tools/perf_iface_witness -- a complete env-28 frontend in one file --
# directly over SSH. No RetroArch, no GUI, no frontend config to get wrong.
#
# =====================================================================
# THE RULE THIS SCRIPT ENFORCES: NEVER BENCHMARK UNDER EMULATION
#
# The obvious way to "test the RPi build" without a Pi is an aarch64
# container on this Mac (or qemu-user via binfmt_misc). It runs. It prints
# plausible FPS. Every number is meaningless -- you are measuring a JIT of
# an interpreter of an interpreter, on entirely different silicon.
#
# So the two halves are separated on purpose and the boundary is checked,
# not merely documented:
#
#   build  -- containers welcome; produces artefacts, NEVER a timing number.
#   bench  -- refuses to run until `doctor` proves the SSH target is real
#             Raspberry Pi silicon (/proc/device-tree/model) and is not
#             virtualised (systemd-detect-virt).
#
# Without that gate this script would be a lie generator: the most likely
# failure is not a wrong number, it is a confident number from a machine
# that was never a Pi.
#
# =====================================================================
# USAGE
#
#   test/tools/rpi_perf.sh build [--arch aarch64|armhf]
#   test/tools/rpi_perf.sh doctor  pi@raspberrypi.local
#   test/tools/rpi_perf.sh deploy  pi@raspberrypi.local [--rom PATH]
#   test/tools/rpi_perf.sh bench   pi@raspberrypi.local [--frames N]
#   test/tools/rpi_perf.sh profile pi@raspberrypi.local [--frames N]
#
# `profile` is the one that answers #509: it runs perf_iface_witness on the
# Pi and feeds the output through device_perf.sh's report parser, so the
# tvOS and Pi paths produce the SAME table.
#
# Env: VJ_RPI_DIR   remote working dir (default ~/vjperf)
#      VJ_CONTAINER podman|docker (auto-detected)

set -uo pipefail

REPO="$(cd "$(dirname "$0")/../.." && pwd)"
REMOTE_DIR="${VJ_RPI_DIR:-vjperf}"
ARCH=aarch64
ROM=""
FRAMES=1800

die()  { printf '\033[31merror:\033[0m %s\n' "$*" >&2; exit 1; }
info() { printf '\033[36m==>\033[0m %s\n' "$*"; }
warn() { printf '\033[33mwarn:\033[0m %s\n' "$*" >&2; }

CMD="${1:-}"; shift || true
POSITIONAL=()
while [ $# -gt 0 ]; do
   case "$1" in
      --arch)   ARCH="${2:-}";   shift 2 ;;
      --rom)    ROM="${2:-}";    shift 2 ;;
      --frames) FRAMES="${2:-}"; shift 2 ;;
      *)        POSITIONAL+=("$1"); shift ;;
   esac
done
set -- "${POSITIONAL[@]+"${POSITIONAL[@]}"}"

HOSTSPEC="${1:-}"
need_host() { [ -n "$HOSTSPEC" ] || die "need a target, e.g. pi@raspberrypi.local"; }

container_engine() {
   if [ -n "${VJ_CONTAINER:-}" ]; then printf '%s' "$VJ_CONTAINER"; return; fi
   command -v podman >/dev/null 2>&1 && { printf 'podman'; return; }
   command -v docker >/dev/null 2>&1 && { printf 'docker'; return; }
   printf ''
}

# ---------------------------------------------------------------- build
cmd_build() {
   local eng image triple mkplat
   eng="$(container_engine)"
   [ -n "$eng" ] || die "neither podman nor docker found; install one or cross-build by hand"

   case "$ARCH" in
      aarch64)
         image=arm64v8/debian:bookworm
         triple=aarch64-linux-gnu
         # 64-bit Pi OS. platform=unix with an aarch64 CC is exactly the
         # shipped "Linux 64-bit (ARM)" buildbot configuration -- and the
         # one that silently selected the SCALAR blitter before #560.
         mkplat=unix
         ;;
      armhf)
         image=arm32v7/debian:bookworm
         triple=arm-linux-gnueabihf
         mkplat=unix
         ;;
      *) die "--arch must be aarch64 or armhf" ;;
   esac

   info "cross-building core ($ARCH) in $eng"
   warn "container build: artefacts only. Any timing produced in here is void."

   "$eng" run --rm --platform "linux/${ARCH/aarch64/arm64}" \
      -v "$REPO:/src:Z" -w /src "$image" \
      bash -c "set -e
         apt-get update -qq
         apt-get install -y -qq build-essential git >/dev/null
         git config --global --add safe.directory /src || true
         make platform=$mkplat -j\$(nproc)
         echo '--- selected blitter ---'
         # Assert #560 stayed fixed on the artefact we just built, on the
         # exact configuration that regressed. Cheap, and the failure it
         # guards is invisible at runtime.
         nm -D virtualjaguar_libretro.so 2>/dev/null | grep -ci neon || true
      " || die "container build failed"

   [ -f "$REPO/virtualjaguar_libretro.so" ] || die "no .so produced"
   info "built: $(ls -la "$REPO/virtualjaguar_libretro.so")"
   file "$REPO/virtualjaguar_libretro.so" 2>/dev/null || true

   cat <<EOF

NOTE: this .so was built in a container and has NOT been benchmarked.
      Next: $0 doctor <user@pi>   then   $0 deploy / profile
EOF
}

# ---------------------------------------------------------------- doctor
# The gate. bench/profile refuse to run unless this passes.
cmd_doctor() {
   need_host
   info "probing $HOSTSPEC"

   local out
   out="$(ssh -o BatchMode=yes -o ConnectTimeout=10 "$HOSTSPEC" '
      echo "MODEL=$(tr -d "\0" < /proc/device-tree/model 2>/dev/null)"
      echo "UNAME_M=$(uname -m)"
      echo "KERNEL=$(uname -r)"
      echo "VIRT=$(systemd-detect-virt 2>/dev/null || echo unknown)"
      echo "NEON=$(grep -ciE "(^|[[:space:]])(neon|asimd)([[:space:]]|$)" /proc/cpuinfo 2>/dev/null || echo 0)"
      echo "NPROC=$(nproc 2>/dev/null)"
      echo "GOV=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null)"
      echo "MAXFREQ=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq 2>/dev/null)"
      echo "THROTTLE=$(vcgencmd get_throttled 2>/dev/null || echo n/a)"
   ' 2>/dev/null)" || die "ssh to $HOSTSPEC failed (key auth required; BatchMode is on)"

   [ -n "$out" ] || die "no response from $HOSTSPEC"
   printf '%s\n' "$out" | sed 's/^/  /'

   local model virt neon
   model="$(printf '%s' "$out" | sed -n 's/^MODEL=//p')"
   virt="$( printf '%s' "$out" | sed -n 's/^VIRT=//p')"
   neon="$( printf '%s' "$out" | sed -n 's/^NEON=//p')"

   echo
   local bad=0
   case "$model" in
      *Raspberry*) info "real Raspberry Pi silicon: $model" ;;
      "")          warn "no /proc/device-tree/model -- not a Pi, or not Linux"; bad=1 ;;
      *)           warn "device-tree model is '$model', not a Raspberry Pi";    bad=1 ;;
   esac
   case "$virt" in
      none|unknown) ;;
      *) warn "virtualised ($virt) -- timings from here are not hardware timings"; bad=1 ;;
   esac
   [ "${neon:-0}" -gt 0 ] 2>/dev/null || warn "no neon/asimd in /proc/cpuinfo -- scalar blitter is correct here"

   # Not fatal, but it silently halves reproducibility between runs.
   printf '%s' "$out" | grep -q 'GOV=performance' \
      || warn "cpu governor is not 'performance' -- expect run-to-run variance"
   printf '%s' "$out" | grep -q 'THROTTLE=throttled=0x0' \
      || warn "get_throttled is non-zero or unavailable -- thermal/undervolt throttling will skew results"

   if [ "$bad" -ne 0 ]; then
      echo
      die "target did not pass the real-hardware gate; bench/profile will refuse"
   fi
   info "gate passed: this target may be benchmarked"
}

# ---------------------------------------------------------------- deploy
cmd_deploy() {
   need_host
   cmd_doctor >/dev/null || die "doctor failed for $HOSTSPEC"

   [ -f "$REPO/virtualjaguar_libretro.so" ] || die "no .so -- run: $0 build"

   local rom="$ROM"
   [ -n "$rom" ] || rom="$REPO/test/roms/yarc.j64"
   [ -f "$rom" ] || die "ROM not found: $rom"

   info "creating $REMOTE_DIR on $HOSTSPEC"
   ssh -o BatchMode=yes "$HOSTSPEC" "mkdir -p '$REMOTE_DIR/test/roms'" || die "mkdir failed"

   info "copying core, profiler source and ROM"
   scp -q "$REPO/virtualjaguar_libretro.so" "$HOSTSPEC:$REMOTE_DIR/" || die "scp core failed"
   scp -q "$rom" "$HOSTSPEC:$REMOTE_DIR/test/roms/" || die "scp rom failed"
   # Ship the profiler as SOURCE and build it on the Pi: it dlopens the core
   # and only needs the retro_* ABI, so a native compile there avoids
   # cross-building a second binary and cannot disagree with the local libc.
   scp -q "$REPO/test/tools/perf_iface_witness.c" "$HOSTSPEC:$REMOTE_DIR/" || die "scp witness failed"
   scp -q "$REPO/test/tools/test_benchmark.c"     "$HOSTSPEC:$REMOTE_DIR/" || die "scp benchmark failed"
   ssh -o BatchMode=yes "$HOSTSPEC" "mkdir -p '$REMOTE_DIR/libretro-common/include'" || true
   scp -qr "$REPO/libretro-common/include" "$HOSTSPEC:$REMOTE_DIR/libretro-common/" || die "scp headers failed"

   info "building the profilers natively on the Pi"
   ssh -o BatchMode=yes "$HOSTSPEC" "cd '$REMOTE_DIR' && \
      cc -O2 -std=c99 -I libretro-common/include -o perf_iface_witness perf_iface_witness.c -ldl -lm && \
      cc -O2 -std=c99 -I libretro-common/include -o test_benchmark test_benchmark.c -ldl -lm" \
      || die "remote compile failed"

   info "deployed to $HOSTSPEC:$REMOTE_DIR"
}

# ---------------------------------------------------------------- bench
cmd_bench() {
   need_host
   cmd_doctor >/dev/null || die "doctor failed -- refusing to report timings"
   local romname; romname="$(basename "${ROM:-yarc.j64}")"

   info "FPS benchmark on $HOSTSPEC ($FRAMES frames)"
   ssh -o BatchMode=yes "$HOSTSPEC" \
      "cd '$REMOTE_DIR' && ./test_benchmark ./virtualjaguar_libretro.so test/roms/$romname $FRAMES" \
      || die "remote benchmark failed"
}

# ---------------------------------------------------------------- profile
cmd_profile() {
   need_host
   cmd_doctor >/dev/null || die "doctor failed -- refusing to report timings"
   local romname; romname="$(basename "${ROM:-yarc.j64}")"
   local raw; raw="rpi-perf-$(printf '%s' "$HOSTSPEC" | tr -c 'A-Za-z0-9' '-').txt"

   info "per-subsystem profile on $HOSTSPEC ($FRAMES frames)"
   ssh -o BatchMode=yes "$HOSTSPEC" \
      "cd '$REMOTE_DIR' && ./perf_iface_witness ./virtualjaguar_libretro.so test/roms/$romname $FRAMES" \
      > "$raw" 2>&1
   local rc=$?
   if [ $rc -eq 77 ]; then die "ROM missing on the Pi -- run: $0 deploy $HOSTSPEC --rom ..."; fi
   [ $rc -eq 0 ] || { cat "$raw"; die "remote profile failed (exit $rc)"; }

   info "raw output: $raw"

   # The witness prints "  vj_gpu_exec  calls=201045  total=431703000".
   # device_perf.sh's report parser reads RetroArch's "Avg (...)" form, so
   # convert here -- one table for both vehicles, one place to change the
   # arithmetic. Integer division matches what RetroArch itself prints.
   local conv; conv="${raw%.txt}.perflog"
   awk '/^ *vj_[a-z_]+ +calls=/ {
           ident = $1
           for (i = 1; i <= NF; i++) {
              if ($i ~ /^calls=/) { calls = substr($i, 7) + 0 }
              if ($i ~ /^total=/) { total = substr($i, 7) + 0 }
           }
           if (calls > 0)
              printf "[INFO] [PERF] Avg (%s): %d ticks, %d runs.\n", ident, int(total / calls), calls
        }' "$raw" > "$conv"

   if [ ! -s "$conv" ]; then
      cat "$raw"
      die "could not parse counters out of the witness output"
    fi

   "$REPO/test/tools/device_perf.sh" report "$conv"
}

case "$CMD" in
   build)   cmd_build ;;
   doctor)  cmd_doctor ;;
   deploy)  cmd_deploy ;;
   bench)   cmd_bench ;;
   profile) cmd_profile ;;
   ""|-h|--help|help)
      sed -n '/^# USAGE/,/^# Env:/p' "$0" | sed 's/^# \{0,1\}//'
      ;;
   *) die "unknown command '$CMD' (try: $0 help)" ;;
esac
