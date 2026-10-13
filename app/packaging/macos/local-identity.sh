# shellcheck shell=bash disable=SC2034
# Sourced by local-package.sh: choose a macOS signing identity from the login keychain.
#
#   vj_pick_identity    sets VJ_ID (what codesign gets: a SHA-1 hash, or "-"), VJ_ID_NAME,
#                       VJ_ID_EXPIRY (YYYY-MM-DD or "") and VJ_ID_KIND
#                       (explicit | developer-id | development | adhoc)
#
# Precedence: VJ_SIGN_IDENTITY (name, SHA-1 or "-") > Developer ID Application > Apple
# Development > ad hoc.  When several valid identities carry the same name (renewed
# certificates), the one expiring last wins, and it is passed to codesign by SHA-1 because a
# bare name is ambiguous between duplicates.

# <sha1> -> "<epoch> <YYYY-MM-DD>" for that certificate's notAfter, or nothing.
vj_cert_expiry() {
  local pem end epoch
  pem="$(security find-certificate -a -Z -p 2>/dev/null | awk -v h="$1" '
    /^SHA-1 hash:/ { on = ($3 == h); next }
    on && /-----BEGIN/ { p = 1 }
    on && p { print }
    on && /-----END/ { exit }')"
  [ -n "$pem" ] || return 0
  end="$(printf '%s\n' "$pem" | openssl x509 -noout -enddate 2>/dev/null | sed 's/^notAfter=//' | tr -s ' ')"
  [ -n "$end" ] || return 0
  epoch="$(date -j -u -f '%b %d %T %Y %Z' "$end" +%s 2>/dev/null)" || return 0
  echo "$epoch $(date -j -u -r "$epoch" +%Y-%m-%d)"
}

# Newest-expiring valid identity whose name starts with $1 -> "<sha1>|<name>|<expiry>".
vj_best_identity() {
  local prefix="$1" hash name info best="" best_epoch=-1 best_name="" best_date=""
  while IFS='|' read -r hash name; do
    [ -n "$hash" ] || continue
    info="$(vj_cert_expiry "$hash")"
    # shellcheck disable=SC2086
    set -- ${info:-0 unknown}
    if [ "$1" -gt "$best_epoch" ]; then best="$hash"; best_epoch="$1"; best_name="$name"; best_date="$2"; fi
  done < <(security find-identity -v -p codesigning 2>/dev/null \
            | sed -n "s/^ *[0-9]*) \([0-9A-F]\{40\}\) \"\($prefix.*\)\"\$/\1|\2/p")
  [ -n "$best" ] && echo "$best|$best_name|$best_date"
  return 0
}

vj_pick_identity() {
  local found
  VJ_ID=""; VJ_ID_NAME=""; VJ_ID_EXPIRY=""; VJ_ID_KIND=""
  if [ -n "${VJ_SIGN_IDENTITY:-}" ]; then
    VJ_ID="$VJ_SIGN_IDENTITY"; VJ_ID_KIND=explicit
    if [ "$VJ_ID" = "-" ]; then VJ_ID_KIND=adhoc; VJ_ID_NAME="ad hoc"; else VJ_ID_NAME="$VJ_ID"; fi
    return 0
  fi
  found="$(vj_best_identity 'Developer ID Application:')"
  if [ -n "$found" ]; then VJ_ID_KIND=developer-id
  else
    found="$(vj_best_identity 'Apple Development:')"
    [ -z "$found" ] || VJ_ID_KIND=development
  fi
  if [ -n "$found" ]; then
    VJ_ID="${found%%|*}"; found="${found#*|}"
    VJ_ID_NAME="${found%%|*}"; VJ_ID_EXPIRY="${found#*|}"
  else
    VJ_ID="-"; VJ_ID_KIND=adhoc; VJ_ID_NAME="ad hoc"
  fi
  return 0
}
