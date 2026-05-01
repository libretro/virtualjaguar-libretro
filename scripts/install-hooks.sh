#!/bin/sh
# Install git hooks for this repository.
#
# Currently installs a pre-commit that runs:
#   - scripts/c89-lint.sh on staged .c files (catches MSVC C89 violations)
#   - scripts/check-info-version.sh if dist/info/.info or Makefile is staged
#
# Skip with `git commit --no-verify` if you really need to (e.g., a WIP
# squash); CI will catch it later anyway.

set -e

HOOK_DIR="$(git rev-parse --git-dir)/hooks"

cat > "$HOOK_DIR/pre-commit" << 'HOOK'
#!/bin/sh
set -e

STAGED=$(git diff --cached --name-only --diff-filter=ACM)

# C89 lint on staged .c files
STAGED_C=$(echo "$STAGED" | grep '\.c$' || true)
if [ -n "$STAGED_C" ]; then
  scripts/c89-lint.sh $STAGED_C
fi

# .info / Makefile version sync check
if echo "$STAGED" | grep -qE '^(dist/info/|Makefile$)'; then
  scripts/check-info-version.sh
fi
HOOK

chmod +x "$HOOK_DIR/pre-commit"
echo "Installed pre-commit hook (C89 lint + .info version check)"
