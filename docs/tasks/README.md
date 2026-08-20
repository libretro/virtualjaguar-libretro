# Cheap-model task files

> **All six tasks in this directory are complete and merged as of 2026-08-04.**
> They are kept as worked examples of the self-contained task format, not as
> open work. For current status use the GitHub issues and milestones — that
> is where planning lives.

Self-contained prompts for weak models in fresh sessions. Each file inlines
its own environment block, hard rules, allowlist, steps, acceptance gate,
STOP conditions, and PR/issue deliverable text. Do not rely on shared
context across sessions.

The open-issue picture lives in GitHub issues and milestones, not in a
checked-in document. [`docs/open-issue-handoff.md`](../open-issue-handoff.md)
is a point-in-time snapshot from 2026-08-04 whose tracked tickets have all
since closed; do not treat it as current.
Read [`CLAUDE.md`](../../CLAUDE.md) before any code change.

## Execution protocol

1. **One task per session.** One branch per task. One PR per task.
2. Branch fresh from `libretro/develop` every time — never stack on another
   task branch. (Exception: T0 lands on the already-open `docs/open-issue-handoff` / PR #282.)
3. Paste the entire task file into the session. Do not summarise it first.
4. A PR without a pasted acceptance-gate transcript in the body is incomplete.
5. If any STOP condition fires, stop and report. Do not improvise.

## Order

| File | Ticket | Depends | Parallel? |
|---|---|---|---|
| [00-handoff-dependabot-stale.md](00-handoff-dependabot-stale.md) | Handoff doc §9 | — | yes (on PR #282 branch) |
| [01-vjss-info.md](01-vjss-info.md) | #268 tooling | — | yes |
| [02-savestate-decision.md](02-savestate-decision.md) | #268 decision | T1 merged | after 01 |
| [03-cdi-bad-dump-diag.md](03-cdi-bad-dump-diag.md) | #269 | — | yes |
| [04-overscan-band-digest.md](04-overscan-band-digest.md) | #266 tooling | — | yes |
| [05-avp-input-fixture.md](05-avp-input-fixture.md) | #267 unlock | — | yes |

T0, T1, T3, T4, T5 may run in parallel. T2 waits on T1.

## What is NOT here

Judgment-heavy work stays with a strong model / maintainer:

- #266 write-side instrumentation and RetroArch nightly capture
- #267 root-cause after the fixture exists
- VLM / audio-CD chain
- #254 SDL frontend
- Closing #266 or #267 on headless evidence alone (forbidden)

## Shared preamble (every task file inlines this)

The blocks below are copied verbatim into each task file so a session that
only receives one file still has them.

### Environment

```bash
cd /Users/jmattiello/Workspace/Provenance/virtualjaguar-libretro
export DEVELOPER_DIR=/Library/Developer/CommandLineTools
git fetch libretro
# then: git checkout -b <BRANCH> libretro/develop   (see each task)
[ -e test/roms/private ] || ln -sfn "${JAGUAR_ROMS_PRIVATE:?set me}" test/roms/private
```

### Hard rules

- NEVER run `git clean -xfd` or any recursive delete at repo root.
- C89 only for C: all declarations at top of block, no `for (int i…)`, no
  designated initializers, no VLAs. Prefer `/* */` comments.
  Run `bash scripts/c89-lint.sh <file>` before commit on every touched `.c`.
- Branch from `libretro/develop`. Never `master`. Never stack on another task.
- Never relax a test threshold to make something pass.
- Touch ONLY the files in that task's "Files you may touch". If the fix
  seems to need another file, STOP and report.
- Prefix host builds with `DEVELOPER_DIR=/Library/Developer/CommandLineTools`.
- After edits: `VJ_EXPECT_BUILD=$(./scripts/build-id.sh)` when running harnesses.
- If `make` fails with *"building for 'macOS', but linking in object file
  built for 'iOS'"*, run `make clean` and rebuild — do not investigate.

### PR evidence requirement

Every PR body MUST end with:

```
## Acceptance gate transcript
$ <exact command from the task>
<paste stdout/stderr and exit code>
```
