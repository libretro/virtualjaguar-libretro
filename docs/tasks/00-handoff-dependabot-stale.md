# T0 — Correct handoff §9 (dependabot bumps are merged)

## Environment (do this first, exactly)

```bash
cd /Users/jmattiello/Workspace/Provenance/virtualjaguar-libretro
export DEVELOPER_DIR=/Library/Developer/CommandLineTools
git fetch libretro
# PR #282 is already open on this branch — do NOT create a new branch.
git checkout docs/open-issue-handoff
git merge --ff-only libretro/develop || true
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

Update `docs/open-issue-handoff.md` so §9 records that dependabot PRs
#278–#281 already merged, move the untested `ad-m/github-push-action`
major-bump risk into §6 (#252), and drop table row 12 ("Review dependabot
#278").

## Files you may touch (explicit allowlist)

- `docs/open-issue-handoff.md`

## Steps (numbered, copy-pasteable commands)

1. Confirm you are on the PR #282 branch:

   ```bash
   git branch --show-current   # must print: docs/open-issue-handoff
   gh pr view 282 --json number,headRefName,state
   ```

2. Confirm the bumps are merged on `libretro/develop` (do not re-merge them):

   ```bash
   git log --oneline libretro/develop | head -20
   # Expect commits mentioning ad-m/github-push-action, setup-python,
   # checkout, setup-java (hashes around 6249339 / c4edff9 / 791d292 / f1e84d8).
   ```

3. Edit `docs/open-issue-handoff.md`:

   **§6 (#252)** — after the per-language enablement paragraph, add a
   **Risk note** that:
   - states PRs #278–#281 all merged before any live Crowdin run;
   - names #278 as the non-routine bump (`ad-m/github-push-action` 0.8.0 → 1.3.0);
   - notes the other three (`setup-python` 4→7, `checkout` 4→7, `setup-java` 4→5);
   - says a sync failure is ambiguous until §6 step 4 has one green run.

   **§9** — replace the entire "Open dependabot PRs — #278, #279, #280, #281"
   section with a short **merged (history)** section that records the four
   merges and points the risk note at §6. No open-work language.

   **Suggested ticket breakdown table** — delete the row
   `| 12 | Review dependabot #278 major bump | Crowdin green run | XS |`
   and renumber `#254 SDL frontend` to row 12. Update the prose under the
   table ("Item 13" → "Item 12"). Optionally note that cheap-model task
   files for items 1–4 and 6 live under `docs/tasks/`.

4. Do not invent new tickets. Do not close any GitHub issues.

## Acceptance gate (literal command + expected exit code / expected output)

```bash
# Gate A: #279/#280/#281 appear only in the merged-history / risk-note lines,
# never as "open dependabot PRs" work.
rg -n '#279|#280|#281' docs/open-issue-handoff.md
```

Expected: matches only inside the §6 risk note and/or the §9 merged-history
paragraph. Zero matches that say the PRs are still open.

```bash
# Gate B: table row 12 must NOT be the dependabot review ticket.
rg -n 'Review dependabot #278' docs/open-issue-handoff.md; echo exit:$?
```

Expected: no matches, exit 1 from `rg` (not found).

```bash
# Gate C: section 9 title reflects merged state.
rg -n '^## 9\.' docs/open-issue-handoff.md
```

Expected: a title containing `merged` or `history`, not `Open dependabot PRs`.

## STOP conditions (abort triggers — report, do not improvise)

- You are not on `docs/open-issue-handoff` / PR #282 cannot be found.
- `docs/open-issue-handoff.md` already has the §9 merged-history rewrite and
  Gates A–C pass — then make no further edits; report "already done".
- Any change would require editing a file outside the allowlist.

## Deliverable (exact commit message, PR title, PR body, issue comment text)

**Commit message:**

```
docs: mark dependabot #278-#281 merged; move push-action risk to #252
```

**PR:** this lands on existing PR #282 (`docs: verification and ticket plan
for every open issue`). Push to the same branch:

```bash
git add docs/open-issue-handoff.md
git commit -m "$(cat <<'EOF'
docs: mark dependabot #278-#281 merged; move push-action risk to #252

EOF
)"
git push libretro HEAD:docs/open-issue-handoff
```

**PR comment** (post on #282):

```
Updated §9: #278–#281 are merged history, not open work. Untested
ad-m/github-push-action major bump is now a risk note under §6 (#252).
Dropped table row "Review dependabot #278".
```

**Do not close any issue.**
