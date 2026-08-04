# T3 — #269 CDI bad-dump diagnostics (warn-and-refuse, no behaviour change)

## Environment (do this first, exactly)

```bash
cd /Users/jmattiello/Workspace/Provenance/virtualjaguar-libretro
export DEVELOPER_DIR=/Library/Developer/CommandLineTools
git fetch libretro
git checkout -b fix/cdi-bad-dump-diag libretro/develop
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

When `CDIntfExtractBootStub` rejects a disc for magic mismatch, log **why**
(zero-filled header vs partial N/32 match vs other), document the four known
bad CDI V2 dumps, and keep refuse-as-refuse — **do not** tolerate truncated
magic to force a boot.

## Files you may touch (explicit allowlist)

- `src/cd/cdintf.c`  (only the magic-mismatch diagnostics path around the
  `memcmp(swapped + 0x42, MAGIC, sizeof(MAGIC))` check — today ~line 1441)
- `docs/cd-known-issues.md`  (add a "Known bad CDI dumps" section)

Do **not** edit `src/cd/cdrom.c`, HLE boot stub injection in `jaguar.c`,
session-count gates, or `intl/`.

## What is established (do not re-investigate)

Of 14 local CDI images, 4 fail load — all CDI V2:

| Image (local names) | Defect |
|---|---|
| ironsoldier2, mystdemo, vidgrid | boot-header region **zero-filled in the file** |
| worldtourracing | **partial 22/32** boot magic |

The CDI walk/offset math is correct. Missing data cannot be invented.

**Product decision for this task (already chosen):** warn and refuse, with an
actionable message. Do **not** implement truncated-magic acceptance.

## Steps (numbered, copy-pasteable commands)

1. Locate the magic check:

   ```bash
   rg -n 'Magic mismatch at \+0x42' src/cd/cdintf.c
   ```

2. Replace the single generic `LOG_ERR` on mismatch with C89-legal code that:

   a. Counts how many of the 32 magic bytes at `swapped + 0x42` match
      `MAGIC[i]`.
   b. Detects all-zero: every byte in that 32-byte window is `0x00`.
   c. Logs exactly one of:

      - **All zero:**
        `[CD-BOOTSTUB] Boot header region is zero-filled at +0x42 — this image is an incomplete / bad rip, not an unsupported format`
      - **Partial (1..31 matches):**
        `[CD-BOOTSTUB] Magic mismatch at +0x42 of session-2 track BIN (matched N/32 bytes)`
        where `N` is the match count (worldtourracing should show `22/32`).
      - **Zero matches but not all-zero:**
        keep a clear generic mismatch line (may reuse the old text plus
        `(matched 0/32 bytes)`).

   d. Still `return false;` in every branch. **No boot on mismatch.**

   Declarations stay at the top of the enclosing block (C89).

3. Add a section to `docs/cd-known-issues.md` titled **Known bad CDI dumps**
   listing the four titles above, stating warn-and-refuse is intentional, and
   pointing at the new log lines. Do not claim a core "fix" for missing data.

4. Lint + build:

   ```bash
   bash scripts/c89-lint.sh src/cd/cdintf.c
   DEVELOPER_DIR=/Library/Developer/CommandLineTools \
     make -j"$(getconf _NPROCESSORS_ONLN)" TEST_EXPORTS=1
   ```

5. Run the CDI corpus sweep (private ROM tree required):

   ```bash
   DEVELOPER_DIR=/Library/Developer/CommandLineTools \
     make test/test_cd_hle_boot
   VJ_EXPECT_BUILD=$(./scripts/build-id.sh) \
     VJ_TEST_CD_EXTS=cdi ./test/test_cd_hle_boot 2>&1 | tee /tmp/cdi_hle_boot.log
   ```

6. Confirm messages (adjust filenames if your corpus uses different basename
   spellings — match on the log text, not a hardcoded path):

   ```bash
   rg -n 'zero-filled|matched 22/32|incomplete / bad rip' /tmp/cdi_hle_boot.log
   ```

7. Regression gate — boot matrix must stay all `GAME_CODE` (do not skip):

   ```bash
   # Use the project's usual invocation; keep the run bounded if the script
   # supports CD_MATRIX_* knobs. Resume is OK if OUT already has same-build rows.
   VJ_EXPECT_BUILD=$(./scripts/build-id.sh) \
     bash test/tools/cd_boot_matrix.sh
   ```

   If a full matrix is too long for the session, run at least one known-good
   CUE title through `test/test_cd_hle_boot` without `VJ_TEST_CD_EXTS=cdi` and
   state clearly in the PR that the matrix must be finished before merge —
   but prefer finishing the matrix.

## Acceptance gate (literal command + expected exit code / expected output)

```bash
bash scripts/c89-lint.sh src/cd/cdintf.c
# exit 0

# After VJ_TEST_CD_EXTS=cdi ./test/test_cd_hle_boot:
rg -c 'zero-filled' /tmp/cdi_hle_boot.log
# Expected: >= 3  (ironsoldier2, mystdemo, vidgrid)

rg -n 'matched 22/32' /tmp/cdi_hle_boot.log
# Expected: at least one hit (worldtourracing)

# Behaviour unchanged: those four still fail to become GAME_CODE / still
# fail load. Count successful CDI loads must remain 10 of 14 (or whatever
# the pre-change baseline was — do not increase by "fixing" dumps).
```

## STOP conditions (abort triggers — report, do not improvise)

- You are about to accept a 22/32 magic match and boot worldtourracing — STOP.
  That is the rejected alternative in the handoff doc (false-positive risk).
- `test/roms/private` is missing or has fewer than 14 `.cdi` files — STOP and
  report; do not download dumps.
- Magic check site has moved and you cannot find `memcmp(swapped + 0x42` —
  STOP and report the new locus; do not rewrite the whole extractor.
- Boot matrix loses any `GAME_CODE` row — revert and STOP.
- Stale iOS objects break the macOS link — `make clean` once, then rebuild.
  If still broken, STOP.

## Deliverable (exact commit message, PR title, PR body, issue comment text)

**Commit message:**

```
fix(cd): diagnose zero-filled / partial CDI boot headers (#269)
```

**PR title:**

```
fix(cd): diagnose zero-filled / partial CDI boot headers (#269)
```

**PR body template:**

```markdown
## Summary
- Distinguish zero-filled vs partial ATARI magic mismatches in CDIntfExtractBootStub.
- Document known-bad CDI V2 dumps.
- Refuse stays refuse — no truncated-magic boot path.

Addresses #269 (does not claim the bad dumps become playable).

## Acceptance gate transcript
$ bash scripts/c89-lint.sh src/cd/cdintf.c
<paste>
$ rg -n 'zero-filled|matched 22/32' /tmp/cdi_hle_boot.log
<paste>
```

**Push + PR:**

```bash
git add src/cd/cdintf.c docs/cd-known-issues.md
git commit -m "$(cat <<'EOF'
fix(cd): diagnose zero-filled / partial CDI boot headers (#269)

EOF
)"
git push -u libretro HEAD
gh pr create --base develop --title "fix(cd): diagnose zero-filled / partial CDI boot headers (#269)" --body-file - <<'EOF'
## Summary
- Distinguish zero-filled vs partial ATARI magic mismatches in CDIntfExtractBootStub.
- Document known-bad CDI V2 dumps.
- Refuse stays refuse.

Addresses #269.

## Acceptance gate transcript
(paste here)
EOF
```

**Issue comment on #269:**

```markdown
PR <N> implements warn-and-refuse diagnostics:
- zero-filled +0x42 → explicit bad-rip message
- partial match → `matched N/32 bytes` (worldtourracing = 22/32)
- known-bad table in docs/cd-known-issues.md

No truncated-magic acceptance (false-positive risk). Bad dumps remain unloadable.
```
