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

# 68K-only sources: header + code in one translation unit, linked flat at
# the cart base.  (Tasks 2-5 add GPU/DSP blobs via .incbin of a lyxass
# output with its 12-byte BS94 header stripped -- see README.md.)
for src in bench68k.s benchgpu_arith.s benchgpu_branch.s benchdsp.s benchblit.s; do
    [ -f "${src}" ] || continue
    base="${src%.s}"
    echo "==> ${base}"
    rmac -fb -o "${base}.o" "${src}"
    rln -n -a 800000 x x -o "${base}.j64" "${base}.o" >/dev/null
    command rm -f "${base}.o"
    echo "    $(wc -c < "${base}.j64" | tr -d ' ') bytes"
done
