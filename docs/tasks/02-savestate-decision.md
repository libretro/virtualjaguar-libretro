# T2 — #268 maintainer decision + docs (does NOT close the issue)

## Environment (do this first, exactly)

```bash
cd /Users/jmattiello/Workspace/Provenance/virtualjaguar-libretro
export DEVELOPER_DIR=/Library/Developer/CommandLineTools
git fetch libretro
git checkout -b docs/savestate-min-version libretro/develop
[ -e test/roms/private ] || ln -sfn "${JAGUAR_ROMS_PRIVATE:?set me}" test/roms/private
```

## Hard rules (violating any of these fails the task)

- NEVER run `git clean -xfd` or any recursive delete at repo root.
- C89 only: all declarations at top of block, no `for (int i…)`, no
  designated initializers. Run `bash scripts/c89-lint.sh <file>` before commit.
- Branch from `libretro/develop`. Never `master`. Never stack on another task.
- Never relax a test threshold to make something pass.
- Touch ONLY the files in "Files you may touch". If the fix seems to need
  another file, STOP and report.

## Goal (one sentence)

Record a deliberate decision that states below `STATE_MIN_VERSION` (2) are
rejected by design, document how to inspect a file with `vjss_info`, and post
that decision on GitHub issue #268 — **without closing the issue**.

## Depends on

T1 (`vjss_info`) must already be merged to `libretro/develop`, OR your branch
must contain `test/tools/vjss_info.c` (cherry-pick / merge T1 first).

```bash
test -f test/tools/vjss_info.c || {
  echo "STOP: T1 not present — merge/cherry-pick tools/vjss-info first"
  exit 1
}
```

## Files you may touch (explicit allowlist)

- `docs/savestate-compat.md`  (**create**)
- Optionally one cross-link line in `docs/open-issue-handoff.md` §3 pointing
  at `docs/savestate-compat.md` (only if that file exists on your branch)

Do **not** change `STATE_MIN_VERSION`, `STATE_VERSION`, or any loader code.

## Decision to document (do not invent a different policy)

**Recommended / required for this task:** leave gating as-is.

- Current core writes `STATE_VERSION = 7`.
- Current core loads only `2 <= version <= 7`.
- Rejection of older states is **expected behaviour**, not an AvP-specific bug.
- A Battle Sphere Gold state loading on the same build already proved the
  loader works; the reporter's files were never measured (do not claim they
  are "v1").
- A best-effort legacy loader below `STATE_MIN_VERSION` is explicitly **out of
  scope** for this task.

## Steps (numbered, copy-pasteable commands)

1. Create `docs/savestate-compat.md` with at least:

   - Title + date
   - Table of `STATE_VERSION` / `STATE_MIN_VERSION` values (cite
     `src/core/state.h`)
   - The deliberate decision paragraph above
   - How to inspect a file:

     ```bash
     ./test/tools/vjss_info path/to/file.state
     ```

   - Note that RetroArch `.state` files are raw `retro_serialize()` payloads
     for this core (no extra wrapper), so `vjss_info` can read them directly
     on the first 16 bytes
   - Link to issue #268 as the tracker for user-facing reports

2. Build `vjss_info` if the binary is missing:

   ```bash
   DEVELOPER_DIR=/Library/Developer/CommandLineTools \
     cc -O2 -Wall -std=c89 -o test/tools/vjss_info test/tools/vjss_info.c
   ```

3. Post the issue comment (see Deliverable). **Do not** run
   `gh issue close 268`.

## Acceptance gate (literal command + expected exit code / expected output)

```bash
test -f docs/savestate-compat.md && echo OK_FILE
rg -n 'STATE_MIN_VERSION|vjss_info|deliberate|expected behaviour|expected behavior' docs/savestate-compat.md
# Must hit deliberate/expected + vjss_info + STATE_MIN_VERSION

# Must NOT have closed the issue
gh issue view 268 --json state -q .state
# Expected: OPEN
```

## STOP conditions (abort triggers — report, do not improvise)

- T1 / `test/tools/vjss_info.c` is absent — STOP.
- You are about to lower `STATE_MIN_VERSION` or write a legacy loader — STOP.
- You are about to close #268 — STOP. Maintainer closes after reading the
  comment and any reporter follow-up.
- Reporter's two files appear in the tree — do not commit them; run
  `vjss_info` locally and paste output into the issue comment only.

## Deliverable (exact commit message, PR title, PR body, issue comment text)

**Commit message:**

```
docs: record savestate STATE_MIN_VERSION policy for #268
```

**PR title:**

```
docs: record savestate STATE_MIN_VERSION policy for #268
```

**PR body template:**

```markdown
## Summary
- Documents that rejecting states below STATE_MIN_VERSION (2) is deliberate.
- Points reporters at `vjss_info` for header triage.
- Does **not** close #268.

## Acceptance gate transcript
$ gh issue view 268 --json state -q .state
OPEN
$ rg -n 'STATE_MIN_VERSION|vjss_info' docs/savestate-compat.md
<paste>
```

**Push + PR:**

```bash
git add docs/savestate-compat.md
git commit -m "$(cat <<'EOF'
docs: record savestate STATE_MIN_VERSION policy for #268

EOF
)"
git push -u libretro HEAD
gh pr create --base develop --title "docs: record savestate STATE_MIN_VERSION policy for #268" --body-file - <<'EOF'
## Summary
- Documents that rejecting states below STATE_MIN_VERSION (2) is deliberate.
- Points reporters at `vjss_info` for header triage.
- Does **not** close #268.

## Acceptance gate transcript
(paste here)
EOF
```

**Issue comment on #268** (required; copy-paste):

```markdown
## Decision (documented in docs/savestate-compat.md, PR <N>)

This is savestate **version gating working as designed**, not an AvP-specific
loader bug.

- Current cores write `STATE_VERSION = 7` and load only
  `STATE_MIN_VERSION (2) … STATE_VERSION (7)`.
- Older Virtual Jaguar states below that window are refused by
  `retro_unserialize()` (returns false).
- We are **not** adding a best-effort legacy loader in this pass.

### Please run this on the two rejected files

```bash
./test/tools/vjss_info /path/to/file.state
```

Paste the one-line output (magic/version/verdict) for each file. That turns
"what version is this" into a measurable fact.

Leaving the issue **open** until that output lands (or a maintainer
explicitly closes as expected behaviour).
```
