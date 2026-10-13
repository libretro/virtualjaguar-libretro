# shellcheck shell=bash
# Sourced by local-package.sh: load local signing settings from the repo-root `.env`.
#
#   - Only lines of the form KEY=value are read, and only keys starting VJ_ or APPLE_ are
#     accepted (so a stray .env cannot set PATH, DYLD_* and the like). Nothing is eval'd and
#     no $VAR / `cmd` expansion happens: the value is taken literally (one pair of matching
#     surrounding quotes is stripped).
#   - A variable that is already set in the environment wins over the file.
#   - A value of the form op://vault/item/field is resolved with `op read` when the 1Password
#     CLI is installed (APPLE_NOTARY_KEY_PATH and VJ_SIGN_PFX are written to a private temp
#     file and the variable points at that file).  Otherwise it is skipped with a note.
#   - Values are never printed.  Only the NAMES of the keys that were loaded are.
#
# Location: <repo-root>/.env (gitignored), or the file named by VJ_ENV_FILE.

VJ_ENV_TMP=""

vj_env_cleanup() { [ -n "$VJ_ENV_TMP" ] && rm -rf "$VJ_ENV_TMP"; return 0; }

vj_load_env() {
  local file="${VJ_ENV_FILE:-$1/.env}" line key val loaded="" skipped="" out
  [ -f "$file" ] || { echo "[env] no $file (using the process environment only)"; return 0; }
  while IFS= read -r line || [ -n "$line" ]; do
    line="${line%$'\r'}"
    case "$line" in [A-Z_]*=*) ;; *) continue ;; esac
    key="${line%%=*}"; val="${line#*=}"
    case "$key" in
      *[!A-Z0-9_]*) continue ;;
      VJ_*|APPLE_*) ;;
      *) skipped="$skipped $key"; continue ;;
    esac
    # Already set (even to empty) in the environment: the environment wins.
    if [ -n "${!key+x}" ]; then continue; fi
    case "$val" in
      \"*\") val="${val#\"}"; val="${val%\"}" ;;
      \'*\') val="${val#\'}"; val="${val%\'}" ;;
    esac
    [ -n "$val" ] || continue
    case "$val" in
      op://*)
        if ! command -v op >/dev/null 2>&1; then
          echo "[env] $key is an op:// reference but the 1Password CLI (op) is not installed; skipped"
          continue
        fi
        case "$key" in
          APPLE_NOTARY_KEY_PATH|VJ_SIGN_PFX)
            [ -n "$VJ_ENV_TMP" ] || { VJ_ENV_TMP="$(mktemp -d)"; chmod 700 "$VJ_ENV_TMP"; }
            out="$VJ_ENV_TMP/$key"
            if ! op read --force --out-file "$out" "$val" >/dev/null 2>&1; then
              echo "[env] 'op read' failed for $key; skipped"; continue
            fi
            chmod 600 "$out"; val="$out" ;;
          *)
            if ! val="$(op read "$val" 2>/dev/null)"; then
              echo "[env] 'op read' failed for $key; skipped"; continue
            fi ;;
        esac ;;
    esac
    export "$key=$val"
    loaded="$loaded $key"
  done < "$file"
  echo "[env] $file: loaded${loaded:- nothing}"
  [ -z "$skipped" ] || echo "[env] ignored (only VJ_* and APPLE_* are read):$skipped"
  return 0
}
