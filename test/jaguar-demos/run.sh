#!/usr/bin/env bash
#
# test/jaguar-demos/run.sh — fetch / select / probe JaguarDemos ROMs.
#
# Usage:
#   ./test/jaguar-demos/run.sh fetch
#   ./test/jaguar-demos/run.sh build          # optional lyxass rebuild
#   ./test/jaguar-demos/run.sh smoke [core]
#   ./test/jaguar-demos/run.sh full  [core]
#   ./test/jaguar-demos/run.sh baseline [core]  # rewrite BASELINE.txt from full
#   ./test/jaguar-demos/run.sh --worker <core> <relpath>   # internal
#
# Env:
#   JAGUAR_DEMOS_DIR     clone path (default test/vendor/JaguarDemos)
#   JAGUAR_DEMOS_JOBS    parallel probe workers (default nproc or 4)
#   JAGUAR_DEMOS_LOGDIR  per-ROM logs (default /tmp/jaguar-demos-logs)
#   JAGUAR_DEMOS_WRITE_BASELINE=1  after smoke/full, rewrite BASELINE.txt
#   VJ_EXPECT_BUILD      optional build-id guard (set by Makefile)
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
PIN_FILE="${SCRIPT_DIR}/PIN"
SMOKE_LIST="${SCRIPT_DIR}/smoke.list"
SKIP_LIST="${SCRIPT_DIR}/skip.list"
BASELINE="${SCRIPT_DIR}/BASELINE.txt"
VENDOR_DIR="${JAGUAR_DEMOS_DIR:-${REPO_ROOT}/test/vendor/JaguarDemos}"
LOGDIR="${JAGUAR_DEMOS_LOGDIR:-/tmp/jaguar-demos-logs}"
JOBS="${JAGUAR_DEMOS_JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)}"
PROBE="${REPO_ROOT}/test/tools/cart_boot_probe"

MIN_NONBLACK_PCT="${JAGUAR_DEMOS_MIN_NONBLACK:-0.5}"
MIN_LIT_FRAMES="${JAGUAR_DEMOS_MIN_LIT:-1}"

cd "${REPO_ROOT}"

read_pin() {
    grep -E "^${1}=" "${PIN_FILE}" | head -1 | cut -d= -f2-
}

trim() {
    echo "$1" | sed 's/^[[:space:]]*//;s/[[:space:]]*$//'
}

cmd_fetch() {
    local url ref sha
    url="$(read_pin URL)"
    ref="$(read_pin REF)"
    sha="$(read_pin SHA)"
    mkdir -p "$(dirname "${VENDOR_DIR}")"
    if [ ! -d "${VENDOR_DIR}/.git" ]; then
        echo "==> Cloning JaguarDemos (${sha})"
        git clone --filter=blob:none "${url}" "${VENDOR_DIR}"
    else
        echo "==> Fetching JaguarDemos"
        git -C "${VENDOR_DIR}" remote set-url origin "${url}" || true
        git -C "${VENDOR_DIR}" fetch --depth 1 origin "${sha}" 2>/dev/null \
            || git -C "${VENDOR_DIR}" fetch origin "${ref}"
    fi
    if ! git -C "${VENDOR_DIR}" cat-file -e "${sha}^{commit}" 2>/dev/null; then
        git -C "${VENDOR_DIR}" fetch --depth 1 origin "${sha}"
    fi
    git -C "${VENDOR_DIR}" checkout --detach "${sha}"
    echo "==> JaguarDemos at $(git -C "${VENDOR_DIR}" rev-parse --short HEAD)"
}

have_jag_toolchain() {
    command -v lyxass >/dev/null 2>&1 \
        && command -v rmac >/dev/null 2>&1 \
        && command -v rln >/dev/null 2>&1 \
        && [ -n "${BJL_ROOT:-}" ] && [ -d "${BJL_ROOT}" ]
}

cmd_build() {
    if [ ! -d "${VENDOR_DIR}" ]; then
        echo "ERROR: clone missing; run fetch first" >&2
        return 2
    fi
    if ! have_jag_toolchain; then
        echo "==> Skipping JaguarDemos rebuild (need lyxass, rmac, rln, BJL_ROOT)"
        echo "    Emulator suite will use committed .j64 files only."
        return 0
    fi
    echo "==> Building JaguarDemos with lyxass/rmac/rln (BJL_ROOT=${BJL_ROOT})"
    local mf fail=0
    while IFS= read -r mf; do
        echo "---- make -C $(dirname "${mf}")"
        if ! make -C "$(dirname "${mf}")" -j1; then
            echo "WARN: build failed in $(dirname "${mf}")" >&2
            fail=$((fail + 1))
        fi
    done <<EOF
$(find "${VENDOR_DIR}" -name Makefile -type f | LC_ALL=C sort)
EOF
    echo "==> JaguarDemos build done (${fail} directories failed)"
    return 0
}

ensure_probe() {
    if [ -x "${PROBE}" ]; then
        return 0
    fi
    echo "==> Building cart_boot_probe"
    extra=""
    if [ "$(uname -s)" = Linux ]; then
        extra="-ldl"
    fi
    ${CC:-cc} -O2 -Wall -std=c99 \
        -I"${REPO_ROOT}" -I"${REPO_ROOT}/src" \
        -I"${REPO_ROOT}/libretro-common/include" -I"${REPO_ROOT}/test" \
        -o "${PROBE}" \
        "${REPO_ROOT}/test/tools/cart_boot_probe.c" \
        "${REPO_ROOT}/test/harness/harness.c" \
        ${extra} -lm
}

find_core() {
    local c
    for c in \
        "${1:-}" \
        "${REPO_ROOT}/virtualjaguar_libretro.so" \
        "${REPO_ROOT}/virtualjaguar_libretro.dylib" \
        "${REPO_ROOT}/virtualjaguar_libretro.dll"
    do
        if [ -n "${c}" ] && [ -f "${c}" ]; then
            echo "${c}"
            return 0
        fi
    done
    echo "ERROR: core not found; build with make TEST_EXPORTS=1" >&2
    return 2
}

is_skipped() {
    local rel="$1" pat
    [ -f "${SKIP_LIST}" ] || return 1
    while IFS= read -r pat || [ -n "${pat}" ]; do
        pat="${pat%%#*}"
        pat="$(trim "${pat}")"
        [ -z "${pat}" ] && continue
        case "${rel}" in
            ${pat}) return 0 ;;
        esac
    done < "${SKIP_LIST}"
    return 1
}

rom_rank() {
    local base
    base="$(basename "$1" .j64)"
    case "${base}" in
        vj) echo 0 ;;
        *_M|*_m) echo 1 ;;
        *_K|*_k) echo 3 ;;
        *) echo 2 ;;
    esac
}

# Prefer vj.j64, then Model-M, then unsuffixed, then _K — one ROM per directory.
select_roms_full() {
    local tmp best_file f rel dir rank best_rank cur
    tmp="$(mktemp)"
    best_file="$(mktemp)"
    find "${VENDOR_DIR}" -type f -name '*.j64' | LC_ALL=C sort >"${tmp}"
    while IFS= read -r f; do
        rel="${f#"${VENDOR_DIR}"/}"
        if is_skipped "${rel}"; then
            continue
        fi
        dir="$(dirname "${rel}")"
        rank="$(rom_rank "${rel}")"
        cur="$(grep -F "|${dir}|" "${best_file}" 2>/dev/null | head -1 || true)"
        if [ -z "${cur}" ]; then
            echo "${rank}|${dir}|${rel}" >>"${best_file}"
        else
            best_rank="$(echo "${cur}" | cut -d'|' -f1)"
            if [ "${rank}" -lt "${best_rank}" ]; then
                grep -v -F "|${dir}|" "${best_file}" >"${best_file}.new" || true
                mv "${best_file}.new" "${best_file}"
                echo "${rank}|${dir}|${rel}" >>"${best_file}"
            fi
        fi
    done < "${tmp}"
    cut -d'|' -f3 "${best_file}" | LC_ALL=C sort
    rm -f "${tmp}" "${best_file}" "${best_file}.new"
}

select_roms_smoke() {
    local line missing=0
    while IFS= read -r line || [ -n "${line}" ]; do
        line="${line%%#*}"
        line="$(trim "${line}")"
        [ -z "${line}" ] && continue
        if [ ! -f "${VENDOR_DIR}/${line}" ]; then
            echo "ERROR: smoke ROM missing: ${line}" >&2
            missing=1
            continue
        fi
        echo "${line}"
    done < "${SMOKE_LIST}"
    if [ "${missing}" = 1 ]; then
        return 2
    fi
    return 0
}

frames_for() {
    case "$1" in
        64/*|128/*|256/*|512/*) echo 180 ;;
        *) echo 600 ;;
    esac
}

classify_probe() {
    local line="$1" pct lit
    if echo "${line}" | grep -q 'load_fail=1'; then
        echo FAIL
        return
    fi
    pct="$(echo "${line}" | sed -n 's/.*nonblack_max_pct=\([0-9.][0-9.]*\).*/\1/p')"
    lit="$(echo "${line}" | sed -n 's/.*lit_frames=\([0-9][0-9]*\).*/\1/p')"
    [ -z "${pct}" ] && pct=0
    [ -z "${lit}" ] && lit=0
    if awk -v p="${pct}" -v m="${MIN_NONBLACK_PCT}" \
           -v l="${lit}" -v ml="${MIN_LIT_FRAMES}" \
           'BEGIN { exit !((p+0) >= (m+0) && (l+0) >= (ml+0)) }'
    then
        echo PASS
    else
        echo FAIL
    fi
}

result_path() {
    echo "${LOGDIR}/$(echo "$1" | tr '/' '_').result"
}

log_path() {
    echo "${LOGDIR}/$(echo "$1" | tr '/' '_').log"
}

run_one() {
    local core="$1" rel="$2" frames out status line rc
    frames="$(frames_for "${rel}")"
    out="$(log_path "${rel}")"
    mkdir -p "$(dirname "${out}")" "${LOGDIR}"
    set +e
    "${PROBE}" "${core}" "${VENDOR_DIR}/${rel}" --frames "${frames}" --quiet \
        >"${out}" 2>&1
    rc=$?
    set -e
    line="$(grep -E '^CARTPROBE ' "${out}" | tail -1 || true)"
    if [ -z "${line}" ]; then
        status=FAIL
        line="CARTPROBE rom=\"${rel}\" probe_error=1 exit=${rc}"
        echo "${line}" >>"${out}"
    else
        status="$(classify_probe "${line}")"
    fi
    printf '%s\t%s\t%s\n' "${status}" "${rel}" "${line}" >"$(result_path "${rel}")"
    printf '[%s] %s\n' "${status}" "${rel}"
}

write_baseline_from_results() {
    local results_file="$1"
    sed -E 's/^\[(PASS|FAIL|SKIP)\] /[\1 /' "${results_file}" \
        | LC_ALL=C sort >"${BASELINE}"
    echo "==> Wrote ${BASELINE} ($(wc -l < "${BASELINE}" | tr -d ' ') rows)"
}

filter_baseline_for_smoke() {
    local out="$1" line b_rom rel found
    : >"${out}"
    while IFS= read -r line; do
        b_rom="$(echo "${line}" | sed -E 's/^\[[A-Z]+[[:space:]]+//')"
        found=0
        for rel in ${SMOKE_ROMS}; do
            if [ "${rel}" = "${b_rom}" ]; then
                found=1
                break
            fi
        done
        if [ "${found}" = 1 ]; then
            echo "${line}" >>"${out}"
        fi
    done < "${BASELINE}"
}

run_suite() {
    local mode="$1" core="$2"
    local roms_file results_file pass fail rel st gate_base
    local -a roms

    ensure_probe
    core="$(cd "$(dirname "${core}")" && pwd)/$(basename "${core}")"

    mkdir -p "${LOGDIR}"
    rm -f "${LOGDIR}"/*.result "${LOGDIR}"/*.log 2>/dev/null || true

    roms_file="$(mktemp)"
    if [ "${mode}" = smoke ]; then
        if ! select_roms_smoke >"${roms_file}"; then
            echo "ERROR: smoke.list entries missing from the pinned clone" >&2
            rm -f "${roms_file}"
            return 2
        fi
    else
        select_roms_full >"${roms_file}"
    fi

    if [ ! -s "${roms_file}" ]; then
        echo "ERROR: no ROMs selected (${mode})" >&2
        rm -f "${roms_file}"
        return 2
    fi

    echo "==> Probing $(wc -l < "${roms_file}" | tr -d ' ') JaguarDemos ROM(s) (${mode}, jobs=${JOBS})"

    # Parallel workers: each invokes this script as --worker.
    # Prefer GNU xargs -a; fall back to stdin feed for BSD xargs (macOS).
    if xargs -a /dev/null true 2>/dev/null; then
        xargs -P "${JOBS}" -a "${roms_file}" -I{} \
            "${SCRIPT_DIR}/run.sh" --worker "${core}" {}
    else
        # shellcheck disable=SC2002
        cat "${roms_file}" | xargs -P "${JOBS}" -I{} \
            "${SCRIPT_DIR}/run.sh" --worker "${core}" {}
    fi

    results_file="${LOGDIR}/results.txt"
    : >"${results_file}"
    pass=0
    fail=0
    while IFS= read -r rel; do
        if [ -f "$(result_path "${rel}")" ]; then
            st="$(cut -f1 "$(result_path "${rel}")")"
            printf '[%s] %s\n' "${st}" "${rel}" >>"${results_file}"
            if [ "${st}" = PASS ]; then
                pass=$((pass + 1))
            else
                fail=$((fail + 1))
            fi
        else
            printf '[FAIL] %s\n' "${rel}" >>"${results_file}"
            fail=$((fail + 1))
        fi
    done < "${roms_file}"
    LC_ALL=C sort -o "${results_file}" "${results_file}"

    echo "----"
    echo "JaguarDemos ${mode}: ${pass} PASS / ${fail} FAIL / $((pass + fail)) total"
    cat "${results_file}"

    if [ "${JAGUAR_DEMOS_WRITE_BASELINE:-0}" = 1 ]; then
        write_baseline_from_results "${results_file}"
        rm -f "${roms_file}"
        return 0
    fi

    if [ ! -f "${BASELINE}" ]; then
        echo "ERROR: no ${BASELINE}; run: make jaguar-demos-baseline" >&2
        rm -f "${roms_file}"
        return 2
    fi

    gate_base="${BASELINE}"
    if [ "${mode}" = smoke ]; then
        SMOKE_ROMS="$(tr '\n' ' ' < "${roms_file}")"
        export SMOKE_ROMS
        gate_base="${LOGDIR}/baseline-smoke.txt"
        filter_baseline_for_smoke "${gate_base}"
        if [ ! -s "${gate_base}" ]; then
            echo "ERROR: none of the smoke ROMs are in BASELINE.txt" >&2
            echo "       run: make jaguar-demos-baseline" >&2
            rm -f "${roms_file}"
            return 2
        fi
    fi

    rm -f "${roms_file}"
    python3 "${SCRIPT_DIR}/check-baseline.py" "${results_file}" "${gate_base}"
}

usage() {
    echo "Usage: $0 {fetch|build|smoke|full|baseline} [core]" >&2
    exit 2
}

case "${1:-}" in
    --worker)
        # Internal: run_one <core> <relpath>
        shift
        run_one "$1" "$2"
        ;;
    fetch)
        cmd_fetch
        ;;
    build)
        cmd_build
        ;;
    smoke|full)
        mode="$1"
        shift
        cmd_fetch
        core="$(find_core "${1:-}")" || exit 2
        run_suite "${mode}" "${core}"
        ;;
    baseline)
        shift
        cmd_fetch
        core="$(find_core "${1:-}")" || exit 2
        JAGUAR_DEMOS_WRITE_BASELINE=1 run_suite full "${core}"
        ;;
    *)
        usage
        ;;
esac
