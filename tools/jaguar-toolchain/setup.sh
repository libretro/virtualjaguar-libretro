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

# build_pc_jagcrypt <dest> -- pc_jagcrypt's makefile hardcodes the GNU-strip
# long option `strip --strip-all`, which BSD/macOS strip (short options only,
# see `strip -h`) rejects outright, aborting the recipe right after a
# successful link -- the object files and linked binary are fine, only the
# final `strip` line fails. Shim a `strip` on PATH for just this build that
# drops the unsupported --strip-all flag and calls through to the real
# strip: a bare `strip <binary>` already does full symbol stripping on both
# GNU and BSD strip for a standalone, non-shared executable like this one
# (verified: output is a working, stripped Mach-O). This is a build-host
# compatibility shim, not a change to pc_jagcrypt's own sources.
build_pc_jagcrypt() {
    local dest="$1" real_strip shim_dir rc
    real_strip="$(command -v strip || true)"
    if [ -z "${real_strip}" ]; then
        # No strip on PATH at all -- let make fail with its own real error.
        make -C "${dest}" -j1
        return $?
    fi
    shim_dir="$(mktemp -d)"
    cat > "${shim_dir}/strip" <<EOF
#!/usr/bin/env bash
args=()
for a in "\$@"; do
    case "\$a" in
        --strip-all) ;;  # bare strip already strips everything it can here
        *) args+=("\$a") ;;
    esac
done
exec "${real_strip}" "\${args[@]}"
EOF
    chmod +x "${shim_dir}/strip"
    PATH="${shim_dir}:${PATH}" make -C "${dest}" -j1
    rc=$?
    rm -rf "${shim_dir}"
    return "${rc}"
}

# build_one <tool> <dest> -- per-tool build dispatch. Plain `make -C dest -j1`
# (default `all` target) is correct for rmac, rln and lyxass as-is -- verified
# against each tool's actual makefile, not assumed. pc_jagcrypt needs the
# strip shim above.
build_one() {
    local tool="$1" dest="$2"
    case "${tool}" in
        pc_jagcrypt) build_pc_jagcrypt "${dest}" ;;
        *) make -C "${dest}" -j1 ;;
    esac
}

cmd_build() {
    local tool dest fail=0
    for tool in rmac rln lyxass pc_jagcrypt; do
        dest="${VENDOR_DIR}/${tool}"
        if [ ! -d "${dest}" ]; then
            echo "ERROR: ${tool} not fetched -- run 'setup.sh fetch' first" >&2
            return 2
        fi
        echo "==> building ${tool}"
        if ! build_one "${tool}" "${dest}"; then
            echo "ERROR: build failed for ${tool}" >&2
            fail=$((fail + 1))
        fi
    done
    # new_bjl ships prebuilt binaries under bin/ -- nothing to build.
    if [ "${fail}" -gt 0 ]; then
        echo "==> ${fail} tool(s) failed to build" >&2
        return 1
    fi
    echo "==> all tools built"
}

cmd_env() {
    local bjl_root="${VENDOR_DIR}/new_bjl"
    echo "export PATH=\"${VENDOR_DIR}/rmac:${VENDOR_DIR}/rln:${VENDOR_DIR}/lyxass:${VENDOR_DIR}/pc_jagcrypt:\${PATH}\""
    echo "export BJL_ROOT=\"${bjl_root}\""
}

case "${1:-}" in
    fetch) cmd_fetch ;;
    build) cmd_build ;;
    env) cmd_env ;;
    *)
        echo "usage: $0 {fetch|build|env}" >&2
        exit 2
        ;;
esac
