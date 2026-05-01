#!/bin/sh
# Install git hooks for this repository
HOOK_DIR="$(git rev-parse --git-dir)/hooks"

cat > "$HOOK_DIR/pre-commit" << 'HOOK'
#!/bin/sh
STAGED_C=$(git diff --cached --name-only --diff-filter=ACM | grep '\.c$' || true)
if [ -z "$STAGED_C" ]; then exit 0; fi
exec scripts/c89-lint.sh $STAGED_C
HOOK

chmod +x "$HOOK_DIR/pre-commit"
echo "Installed pre-commit hook (C89 lint)"
