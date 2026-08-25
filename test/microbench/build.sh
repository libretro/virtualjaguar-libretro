#!/usr/bin/env bash
#
# test/microbench/build.sh -- regenerate the committed microbenchmark .j64
# images from their .s sources.
#
# CI never runs this: the .j64 files are committed so the test suite needs
# no assembler (issue #536's own requirement).  Run it only when you edit
# a .s source, then commit the regenerated .j64 alongside it.
#
# Requires the Jaguar toolchain:
#   make jaguar-toolchain-build
#   eval "$(tools/jaguar-toolchain/setup.sh env)"
#   test/microbench/build.sh
#
# rmac/rln output is byte-reproducible (verified: two builds of the same
# source cmp identical), so a regenerated .j64 diffs only when the source
# actually changed.
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"

for tool in rmac rln; do
    command -v "${tool}" >/dev/null 2>&1 || {
        echo "error: ${tool} not on PATH -- run: eval \"\$(tools/jaguar-toolchain/setup.sh env)\"" >&2
        exit 1
    }
done

# GPU/DSP RISC blobs (tasks 2-5): a *_gpu.s or *_dsp.s source is the
# companion RISC program for a same-named 68K bootstrap (e.g.
# benchgpu_arith_gpu.s -> benchgpu_arith.s's `.incbin
# "benchgpu_arith_gpu.bin"`).  lyxass targets the GPU/DSP ISA, not the
# 68000, so it needs its own tool and its own pass, run BEFORE the 68K
# source that .incbin's the result.  lyxass output is a 12-byte BS94
# header (magic + run address + code length) followed by the raw code;
# a naive .incbin of the .o would feed that header to the RISC core as
# instructions, so strip it here -- see README.md "embedding a lyxass
# GPU/DSP blob".
for risc_src in *_gpu.s *_dsp.s; do
    [ -f "${risc_src}" ] || continue
    command -v lyxass >/dev/null 2>&1 || {
        echo "error: lyxass not on PATH -- run: eval \"\$(tools/jaguar-toolchain/setup.sh env)\"" >&2
        exit 1
    }
    risc_base="${risc_src%.s}"
    echo "==> ${risc_base} (lyxass)"
    lyxass -o "${risc_base}.o" "${risc_src}"
    tail -c +13 "${risc_base}.o" > "${risc_base}.bin"
    command rm -f "${risc_base}.o"
    echo "    $(wc -c < "${risc_base}.bin" | tr -d ' ') bytes (header stripped)"
done

# 68K-only sources: header + code in one translation unit, linked flat at
# the cart base.  A source that .incbin's a *_gpu.bin/*_dsp.bin blob (just
# produced above) picks it up automatically -- rmac resolves .incbin
# relative to this directory since we already cd'd here.
for src in bench68k.s benchgpu_arith.s benchgpu_branch.s benchdsp.s benchblit.s; do
    [ -f "${src}" ] || continue
    base="${src%.s}"
    echo "==> ${base}"
    rmac -fb -o "${base}.o" "${src}"
    rln -n -a 800000 x x -o "${base}.j64" "${base}.o" >/dev/null
    command rm -f "${base}.o"
    echo "    $(wc -c < "${base}.j64" | tr -d ' ') bytes"
done

# Intermediate RISC blobs are regenerated from source every run (same
# byte-reproducibility property as the .o files above) -- only the final
# .j64 is committed, so don't leave these lying around.
command rm -f -- *_gpu.bin *_dsp.bin
