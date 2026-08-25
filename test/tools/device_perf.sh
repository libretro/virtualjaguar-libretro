#!/usr/bin/env bash
#
# device_perf.sh -- capture a per-subsystem millisecond breakdown from the
# core running on a real iOS/tvOS device, via RetroArch's performance-counter
# interface (issue #510) and the epic that needs it (#509).
#
# #509's exit criterion is a measurement, not code: "a per-subsystem
# millisecond breakdown captured on the A10X, not inferred from a Mac".
# Everything host-side we ship (docs/profiling.md, `make benchmark`) is
# structurally unable to produce it.  This script is the path that can.
#
# =====================================================================
# WHAT MAKES THIS NON-OBVIOUS -- read before changing the capture
#
# 1. `perfcnt_enable = true` in retroarch.cfg is MANDATORY, and its absence
#    fails SILENTLY as an empty capture rather than as an error.  In
#    RetroArch's runloop.c:
#
#       runloop_performance_counter_register()  -- never gated
#       core_performance_counter_start/stop()   -- gated on perfcnt_enable
#       runloop_perf_log()                      -- returns early if disabled
#
#    So the core always sees registration succeed and sets vjPerfActive=1,
#    the probes always fire, and every counter stays at zero.  A run with
#    the setting off looks exactly like a run where the core is broken.
#    `cfg` below writes the setting; `report` fails loudly on all-zero.
#
# 2. The log prints an AVERAGE, not a total:
#       #define PERF_LOG_FMT "[PERF] Avg (%s): %llu ticks, %llu runs.\n"
#    Total ticks = avg * runs.  Reading the printed number as a total
#    understates the blitter (few, long calls) against the GPU (many, short
#    ones) -- i.e. it inverts the exact comparison this exists to make.
#
# 3. Counters DELIBERATELY OVERLAP and do not sum to frame time.  A 68K bus
#    access into GPU/DSP local RAM runs RISC cycles inline, and the Object
#    Processor runs the GPU inline from a halfline callback.  The vj_*_sync
#    counters exist to decompose it; see src/core/perf_iface.h.  `report`
#    applies the arithmetic rather than printing a bogus pie chart.
#
# 4. perf_log() fires from VJPerfDeinit() only.  Task-killing RetroArch
#    yields NOTHING.  Content must be closed from the menu (or the app quit
#    cleanly) for the numbers to reach the log.  `capture` says so.
#
# 5. Ticks are a CPU-specific unit (cpu_features_get_perf_counter()).  They
#    are comparable WITHIN one device's run and across builds on the SAME
#    device.  They are NOT comparable between an A10X and an A15, so the
#    report prints shares, and prints ms only when a tick rate is supplied.
#
# =====================================================================
# USAGE
#
#   test/tools/device_perf.sh doctor
#   test/tools/device_perf.sh build   tvos|ios
#   test/tools/device_perf.sh install tvos|ios --device "Bedroom"
#   test/tools/device_perf.sh cfg     --device "Bedroom"
#   test/tools/device_perf.sh capture --device "Bedroom"      # prints steps
#   test/tools/device_perf.sh pull    --device "Bedroom" -o run.log
#   test/tools/device_perf.sh report  run.log [--json out.json]
#
# Env: VJ_RETROARCH_DIR (default ~/Workspace/Provenance/RetroArch)
#      VJ_BUNDLE_ID     (default com.joemattiello.retroarch)

set -uo pipefail

REPO="$(cd "$(dirname "$0")/../.." && pwd)"
RA_DIR="${VJ_RETROARCH_DIR:-$HOME/Workspace/Provenance/RetroArch}"
BUNDLE_ID="${VJ_BUNDLE_ID:-com.joemattiello.retroarch}"
DEVICE=""
OUT=""
JSON=""

die()  { printf '\033[31merror:\033[0m %s\n' "$*" >&2; exit 1; }
info() { printf '\033[36m==>\033[0m %s\n' "$*"; }
warn() { printf '\033[33mwarn:\033[0m %s\n' "$*" >&2; }

# ---------------------------------------------------------------- args
CMD="${1:-}"; shift || true
POSITIONAL=()
while [ $# -gt 0 ]; do
   case "$1" in
      -d|--device) DEVICE="${2:-}"; shift 2 ;;
      -o|--out)    OUT="${2:-}";    shift 2 ;;
      --json)      JSON="${2:-}";   shift 2 ;;
      *)           POSITIONAL+=("$1"); shift ;;
   esac
done
set -- "${POSITIONAL[@]+"${POSITIONAL[@]}"}"

need_device() {
   [ -n "$DEVICE" ] || die "--device required (name or UDID; see: $0 doctor)"
}

# platform -> (make platform, dylib name, RetroArch modules subdir, scheme)
resolve_platform() {
   case "${1:-}" in
      tvos|tvOS)
         MK_PLATFORM=tvos-arm64
         DYLIB=virtualjaguar_libretro_tvos.dylib
         MODULES="$RA_DIR/pkg/apple/tvOS/modules"
         SCHEME="RetroArch tvOS Release"
         SDK=appletvos
         # xcodebuild -destination platform= uses the DESTINATION namespace,
         # which is not the SDK name: appletvos/iphoneos are SDKs, tvOS/iOS
         # are destinations. Deriving one from the other silently produces
         # `platform=appletvos`, which xcodebuild rejects. Confirmed against
         # `xcodebuild -showdestinations`, which prints `platform:tvOS`.
         DEST_PLATFORM=tvOS
         ;;
      ios|iOS)
         MK_PLATFORM=ios-arm64
         DYLIB=virtualjaguar_libretro_ios.dylib
         MODULES="$RA_DIR/pkg/apple/iOS/modules"
         SCHEME="RetroArch iOS Release"
         SDK=iphoneos
         DEST_PLATFORM=iOS
         ;;
      *) die "platform must be 'tvos' or 'ios' (got: '${1:-}')" ;;
   esac
}

# ---------------------------------------------------------------- doctor
cmd_doctor() {
   local ok=0
   info "environment"
   printf '  repo          %s\n' "$REPO"
   printf '  RetroArch     %s' "$RA_DIR"
   if [ -d "$RA_DIR/pkg/apple" ]; then printf ' (ok)\n'; else printf ' \033[31m(MISSING)\033[0m\n'; ok=1; fi
   printf '  bundle id     %s\n' "$BUNDLE_ID"
   printf '  build id      %s\n' "$(cd "$REPO" && ./scripts/build-id.sh 2>/dev/null || echo '?')"

   printf '  xcode         %s\n' "$(xcodebuild -version 2>/dev/null | head -1)"
   [ -d "$RA_DIR/pkg/apple/RetroArch_iOS13.xcodeproj" ] \
      || { warn "RetroArch_iOS13.xcodeproj not found -- schemes will not resolve"; ok=1; }

   echo
   info "devices (physical only; simulators cannot produce a valid number)"
   # Filter OUT simulators rather than requiring "physical": an unavailable
   # device (asleep / off-network) prints a blank Reality column, so an
   # /physical/ match hides exactly the box you are waiting on -- which is
   # the A10X, the whole point of this script.
   xcrun devicectl list devices 2>/dev/null \
      | awk 'NR<=2 || !/simulated/' \
      | sed 's/^/  /'
   echo
   cat <<'EOF'
  A "connected" physical device is required. "unavailable" means asleep,
  off-network, or not trusted -- wake it and re-check.

  NOTE: an Apple TV 4K 1st gen reports as AppleTV6,2. That is the A10X part
  #509 names. AppleTV14,1 is the 3rd gen (A15) -- useful for validating this
  pipeline, but NOT a substitute for the A10X in the issue's exit criterion.
EOF
   return $ok
}

# ---------------------------------------------------------------- build
cmd_build() {
   resolve_platform "${1:-}"
   cd "$REPO" || die "cd $REPO"

   info "building core for $MK_PLATFORM"
   # NOTE: no DEVELOPER_DIR override here. The iOS/tvOS SDK lives inside
   # Xcode.app; pointing at CommandLineTools (which the host-build guidance
   # in CLAUDE.md requires, to avoid the App Management prompt) makes
   # `xcodebuild -version -sdk appletvos Path` fail and the build picks up
   # no sysroot.
   make platform="$MK_PLATFORM" -j"$(getconf _NPROCESSORS_ONLN)" \
      || die "core build failed for $MK_PLATFORM"

   [ -f "$DYLIB" ] || die "expected $DYLIB, not produced"

   local want got
   want="$(./scripts/build-id.sh)"
   # The core stamps "vX.Y.Z <gitrev>[-dirty]" into the binary; assert the
   # thing we are about to ship to a device is the tree we are sitting on.
   # This is the stale-installed-core failure mode: RetroArch may already
   # carry a nightly build of this same dylib, and a device round-trip that
   # profiles someone else's binary is worse than no measurement.
   got="$(strings "$DYLIB" 2>/dev/null | grep -oE 'v[0-9]+\.[0-9]+\.[0-9]+ [0-9a-f]{7,}(-dirty)?' | head -1)"
   if [ -z "$got" ]; then
      warn "could not read a version string out of $DYLIB -- cannot verify identity"
   elif ! printf '%s' "$got" | grep -qF "$want"; then
      die "build identity mismatch: binary says '$got', tree is '$want'"
   else
      info "build identity ok: $got"
   fi

   ls -la "$DYLIB"
}

# ---------------------------------------------------------------- install
cmd_install() {
   resolve_platform "${1:-}"
   need_device
   cd "$REPO" || die "cd $REPO"
   [ -f "$DYLIB" ] || die "$DYLIB not built -- run: $0 build ${1:-}"
   [ -d "$RA_DIR/pkg/apple" ] || die "RetroArch checkout not at $RA_DIR (set VJ_RETROARCH_DIR)"

   mkdir -p "$MODULES"
   info "injecting core into $MODULES"
   # `command cp` bypasses this user's `cp -i` alias, which would otherwise
   # sit on an unanswerable overwrite prompt forever and leave the OLD core
   # in place -- a silent stale-binary measurement.
   command cp -f "$DYLIB" "$MODULES/$DYLIB" || die "copy into modules failed"

   # tvOS/iOS refuse to dlopen an unsigned dylib. The RetroArch tree ships
   # the signing helper; without it the app launches fine and simply cannot
   # load the core, which reads as "our core is broken".
   if [ -x "$RA_DIR/pkg/apple/code-sign-cores.sh" ]; then
      info "signing cores"
      ( cd "$RA_DIR/pkg/apple" && ./code-sign-cores.sh ) \
         || warn "code-sign-cores.sh returned non-zero -- the core may fail to dlopen"
   else
      warn "code-sign-cores.sh not found; Xcode's build phase may still sign"
   fi

   info "building + installing RetroArch ($SCHEME) to '$DEVICE'"
   ( cd "$RA_DIR" && xcodebuild \
        -project pkg/apple/RetroArch_iOS13.xcodeproj \
        -scheme "$SCHEME" \
        -destination "platform=$DEST_PLATFORM,name=$DEVICE" \
        -allowProvisioningUpdates \
        build ) || die "xcodebuild failed -- see output above"

   cat <<EOF

Installed. Next:
  $0 cfg     --device "$DEVICE"     # turn perf counters ON (mandatory)
  $0 capture --device "$DEVICE"
EOF
}

# ---------------------------------------------------------------- cfg
# Push the two settings the capture cannot work without.
cmd_cfg() {
   need_device
   local tmp; tmp="$(mktemp -d)"
   trap 'rm -rf "$tmp"' RETURN

   info "pulling current retroarch.cfg"
   if ! xcrun devicectl device copy from --device "$DEVICE" \
        --domain-type appDataContainer --domain-identifier "$BUNDLE_ID" \
        --source Documents/RetroArch/config/retroarch.cfg \
        --destination "$tmp/retroarch.cfg" >/dev/null 2>&1; then
      warn "no existing retroarch.cfg found on device; writing a minimal one"
      : > "$tmp/retroarch.cfg"
   fi

   # Rewrite in place: drop any existing copy of each key, then append ours,
   # so re-running is idempotent instead of stacking duplicate lines.
   grep -vE '^[[:space:]]*(perfcnt_enable|log_to_file|libretro_log_level)[[:space:]]*=' \
      "$tmp/retroarch.cfg" > "$tmp/new.cfg" 2>/dev/null || : > "$tmp/new.cfg"
   cat >> "$tmp/new.cfg" <<'EOF'
perfcnt_enable = "true"
log_to_file = "true"
libretro_log_level = "0"
EOF

   info "pushing retroarch.cfg with perfcnt_enable=true"
   xcrun devicectl device copy to --device "$DEVICE" \
      --domain-type appDataContainer --domain-identifier "$BUNDLE_ID" \
      --source "$tmp/new.cfg" \
      --destination Documents/RetroArch/config/retroarch.cfg \
      || die "could not push config -- is the app installed and the device unlocked?"

   info "done. RetroArch must be relaunched for this to take effect."
}

# ---------------------------------------------------------------- capture
cmd_capture() {
   need_device
   cat <<EOF
Capture procedure on '$DEVICE':

  1. Launch RetroArch (fresh launch -- it reads retroarch.cfg at startup,
     so a config pushed under a running app does nothing).

  2. Load Virtual Jaguar + a ROM. Prefer a title with known prior history
     so the result can be compared: Doom or AvP.

  3. Play REAL GAMEPLAY for 60+ seconds. Not a menu, not an attract loop --
     the ranking this produces is only as representative as the workload.
     AvP idles at the same cost it moves at (measured, issue #411), but
     most titles do not.

  4. Close Content from the RetroArch menu.  <-- REQUIRED

     The core dumps its counters from VJPerfDeinit(), which runs on
     retro_deinit. Task-killing the app, or pulling the log while content
     is still loaded, yields an empty capture.

  5. $0 pull --device "$DEVICE" -o run.log
     $0 report run.log
EOF
}

# ---------------------------------------------------------------- pull
cmd_pull() {
   need_device
   [ -n "$OUT" ] || OUT="device-perf-$(printf '%s' "$DEVICE" | tr -c 'A-Za-z0-9' '-').log"

   local tmp; tmp="$(mktemp -d)"
   trap 'rm -rf "$tmp"' RETURN

   info "pulling logs from $BUNDLE_ID on '$DEVICE'"
   if ! xcrun devicectl device copy from --device "$DEVICE" \
        --domain-type appDataContainer --domain-identifier "$BUNDLE_ID" \
        --source Documents/RetroArch/logs \
        --destination "$tmp/logs" 2>&1; then
      die "log pull failed. Is log_to_file enabled ($0 cfg) and the device connected?"
   fi

   # Newest log wins -- RetroArch rotates per session.
   local newest
   newest="$(find "$tmp/logs" -type f -name '*.log' -print0 2>/dev/null \
             | xargs -0 ls -t 2>/dev/null | head -1)"
   [ -n "$newest" ] || die "no .log file inside the pulled logs directory"

   command cp -f "$newest" "$OUT" || die "could not write $OUT"
   info "wrote $OUT ($(wc -l < "$OUT" | tr -d ' ') lines)"

   if ! grep -q '\[PERF\]' "$OUT"; then
      warn "no [PERF] lines in this log."
      warn "Most likely: perfcnt_enable was off (run '$0 cfg'), or content was"
      warn "never closed from the menu so retro_deinit never ran."
   fi
   printf '\nNext: %s report %s\n' "$0" "$OUT"
}

# ---------------------------------------------------------------- report
cmd_report() {
   local log="${1:-}"
   [ -n "$log" ] && [ -f "$log" ] || die "usage: $0 report <logfile> [--json out.json]"

   awk -v jsonpath="$JSON" '
      # "[PERF] Avg (vj_gpu_exec): 2148 ticks, 201045 runs."
      match($0, /Avg \(vj_[a-z0-9_]+\)/) {
         ident = substr($0, RSTART + 5, RLENGTH - 5)
         gsub(/[()]/, "", ident)
         avg = runs = 0
         for (i = 1; i <= NF; i++) {
            if ($i == "ticks,") avg  = $(i-1) + 0
            if ($i == "runs.")  runs = $(i-1) + 0
         }
         # PERF_LOG_FMT prints the AVERAGE. Total is what we want to rank by;
         # reading the printed figure as a total inverts blitter vs GPU.
         total[ident] = avg * runs
         calls[ident] = runs
         seen++
      }
      END {
         if (!seen) {
            print "report: no vj_* counters in this log." > "/dev/stderr"
            print "        perfcnt_enable off, or content never closed." > "/dev/stderr"
            exit 2
         }

         # Whole-truth totals: every path that executes RISC cycles goes
         # through GPUExec()/DSPExec(), so these need no correction. The
         # counters overlap by design (perf_iface.h), so this is a ranking
         # of where time is spent, NOT a partition of frame time.
         gpu = total["vj_gpu_exec"];  dsp = total["vj_dsp_exec"]
         m68 = total["vj_m68k_slice"]
         gsy = total["vj_gpu_sync"];  dsy = total["vj_dsp_sync"]
         blt = total["vj_blitter"];   op  = total["vj_op_halfline"]
         dac = total["vj_dac_mix"]

         m68_real = m68 - gsy - dsy          # 68K minus RISC pulled in inline
         if (m68_real < 0) m68_real = 0

         base = gpu + dsp + blt + dac + m68_real
         if (base <= 0) base = 1

         printf "\nPer-subsystem cost (ticks; overlapping counters decomposed)\n"
         printf "%-16s %16s %14s %8s\n", "subsystem", "total ticks", "calls", "share"
         printf "%-16s %16s %14s %8s\n", "----------------", "----------------", "--------------", "--------"

         n = 0
         name[++n] = "GPU RISC";   val[n] = gpu;      cnt[n] = calls["vj_gpu_exec"]
         name[++n] = "blitter";    val[n] = blt;      cnt[n] = calls["vj_blitter"]
         name[++n] = "68K (real)"; val[n] = m68_real; cnt[n] = calls["vj_m68k_slice"]
         name[++n] = "DSP RISC";   val[n] = dsp;      cnt[n] = calls["vj_dsp_exec"]
         name[++n] = "DAC mix";    val[n] = dac;      cnt[n] = calls["vj_dac_mix"]

         # simple descending sort
         for (i = 1; i <= n; i++)
            for (j = i + 1; j <= n; j++)
               if (val[j] > val[i]) {
                  t = val[i]; val[i] = val[j]; val[j] = t
                  s = name[i]; name[i] = name[j]; name[j] = s
                  c = cnt[i];  cnt[i]  = cnt[j];  cnt[j]  = c
               }

         for (i = 1; i <= n; i++)
            printf "%-16s %16d %14d %7.1f%%\n", name[i], val[i], cnt[i], 100 * val[i] / base

         printf "\nnesting (already removed above, shown for audit)\n"
         printf "  %-22s %16d   GPU cycles run inline from a 68K bus access\n", "vj_gpu_sync", gsy
         printf "  %-22s %16d   DSP cycles run inline from a 68K bus access\n", "vj_dsp_sync", dsy
         printf "  %-22s %16d   OP halfline; CONTAINS GPU, so it is not added in\n", "vj_op_halfline", op

         printf "\nTicks are this CPU'\''s unit. Valid within this device and\n"
         printf "across builds on it; NOT comparable to another device.\n"

         if (jsonpath != "") {
            printf "{\n" > jsonpath
            printf "  \"unit\": \"ticks\",\n" > jsonpath
            for (i = 1; i <= n; i++) {
               key = name[i]; gsub(/[^A-Za-z0-9]/, "_", key)
               printf "  \"%s\": {\"ticks\": %d, \"calls\": %d, \"share\": %.4f},\n",
                      tolower(key), val[i], cnt[i], val[i] / base > jsonpath
            }
            printf "  \"vj_gpu_sync\": %d,\n  \"vj_dsp_sync\": %d,\n  \"vj_op_halfline\": %d\n}\n",
                   gsy, dsy, op > jsonpath
         }
      }
   ' "$log"
   local rc=$?
   [ -n "$JSON" ] && [ $rc -eq 0 ] && info "wrote $JSON"
   return $rc
}

case "$CMD" in
   doctor)  cmd_doctor  "$@" ;;
   build)   cmd_build   "$@" ;;
   install) cmd_install "$@" ;;
   cfg)     cmd_cfg     "$@" ;;
   capture) cmd_capture "$@" ;;
   pull)    cmd_pull    "$@" ;;
   report)  cmd_report  "$@" ;;
   ""|-h|--help|help)
      sed -n '/^# USAGE/,/^$/p' "$0" | sed 's/^# \{0,1\}//'
      ;;
   *) die "unknown command '$CMD' (try: $0 help)" ;;
esac
