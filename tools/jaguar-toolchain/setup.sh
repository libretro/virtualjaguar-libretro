#!/usr/bin/env bash
#
# tools/jaguar-toolchain/setup.sh -- fetch/build/env the Jaguar assembler
# toolchain (rmac, rln, lyxass, pc_jagcrypt, new_bjl/BJL_ROOT).
#
# Usage:
#   tools/jaguar-toolchain/setup.sh fetch
#   tools/jaguar-toolchain/setup.sh build
#   tools/jaguar-toolchain/setup.sh env
#
# Mirrors test/jaguar-demos/run.sh's fetch/build pattern: clone-on-demand
# into a gitignored vendor dir, pinned to a SHA in PIN. Never vendors
# source into this repo's own history.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
PIN_FILE="${SCRIPT_DIR}/PIN"
VENDOR_DIR="${JAGUAR_TOOLCHAIN_DIR:-${REPO_ROOT}/tools/vendor/jaguar-toolchain}"

TOOLS=(rmac rln lyxass pc_jagcrypt new_bjl)

cd "${REPO_ROOT}"

# read_pin <tool> <key> -- e.g. read_pin rmac URL
read_pin() {
    local tool="$1" key="$2"
    awk -v tool="[$tool]" -v key="$key" '
        $0 == tool { infile=1; next }
        /^\[/ { infile=0 }
        infile && $0 ~ "^"key"=" { sub("^"key"=", ""); print; exit }
    ' "${PIN_FILE}"
}

cmd_fetch() {
    local tool url ref sha dest
    mkdir -p "${VENDOR_DIR}"
    for tool in "${TOOLS[@]}"; do
        url="$(read_pin "${tool}" URL)"
        ref="$(read_pin "${tool}" REF)"
        sha="$(read_pin "${tool}" SHA)"
        dest="${VENDOR_DIR}/${tool}"
        echo "==> ${tool}"
        if [ ! -d "${dest}/.git" ]; then
            echo "    cloning ${url}"
            git clone --filter=blob:none "${url}" "${dest}"
        else
            echo "    fetching ${url}"
            git -C "${dest}" remote set-url origin "${url}" || true
            git -C "${dest}" fetch origin "${ref}"
        fi
        if [ "${sha}" != "HEAD" ]; then
            if ! git -C "${dest}" cat-file -e "${sha}^{commit}" 2>/dev/null; then
                git -C "${dest}" fetch origin "${sha}" || true
            fi
            git -C "${dest}" checkout --detach "${sha}"
        else
            git -C "${dest}" checkout --detach "origin/${ref}"
        fi
        echo "    at $(git -C "${dest}" rev-parse --short HEAD)"
    done
}

case "${1:-}" in
    fetch) cmd_fetch ;;
    *)
        echo "usage: $0 {fetch|build|env}" >&2
        exit 2
        ;;
esac
