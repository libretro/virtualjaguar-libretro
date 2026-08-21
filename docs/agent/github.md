# GitHub, releases & site

Detail for [`CLAUDE.md`](../../CLAUDE.md).

## Every PR must be linked to an issue — `Closes #N` will NOT do it

**Hard rule: link the PR to its issue in the GitHub *Development* panel (right sidebar → gear →
pick the issue). A closing keyword in the body is not a substitute and does not work here.**

GitHub creates the linked-issue relationship from a closing keyword only when the PR targets the
repo's **default branch**. This repo's default is `master` and essentially every PR targets
`develop`, so `Closes #N` here creates **no link at all** — not merely "no auto-close".

Measured 2026-08-20: PRs #537/#539/#542/#544/#545/#547 all carried `Closes #N`; every one
reported `closingIssuesReferences: []`. Only #546, linked by hand in the UI, had a real link. It
went unnoticed because timeline cross-references still appear — the issue looks connected while
its Development panel stays empty, so the issue never shows which release/pre-release tag
contains the work (that tag is the whole reason to link).

- Check: `gh pr view N --json closingIssuesReferences --jq '.closingIssuesReferences | length'`
  — `0` = unlinked.
- **No public API creates the link.** Web UI only; don't burn time on a `gh` flag or GraphQL
  mutation.
- CI enforces it: `.github/workflows/pr-issue-link.yml` fails any PR with no linked issue.
  Genuinely issue-less PR → add the `no-issue` label.
- Still close the issue by hand when the PR merges (keywords don't fire on `develop`).

## GitHub Copilot PR reviews

- List unresolved threads: `gh api graphql -f query='{repository(owner:"libretro",name:"virtualjaguar-libretro"){pullRequest(number:N){reviewThreads(first:30){nodes{id isResolved comments(first:1){nodes{id author{login} body}}}}}}}'`
- Inline reply: `gh api -X POST repos/libretro/virtualjaguar-libretro/pulls/N/comments/<REST_ID>/replies -f body="..."` — parent is the REST `id` from `gh api .../comments`, NOT the GraphQL `PRRC_*` id (404s).
- Resolve thread: `gh api graphql -f query='mutation { resolveReviewThread(input: {threadId: "PRRT_..."}) { thread { isResolved } } }'`.
- Always reply AND resolve when addressing feedback — an open thread after a fix is noise.
- **Do NOT trigger Copilot reviews.** `gh pr comment N --body "@copilot review"` bills per token
  since 2026-06-01 and spawns a coding-agent session; `kimi-review.yml` already reviews every PR
  to `develop`/`master` automatically. Use only when a second opinion is worth the spend, and say
  why in the PR.
- Repo-level Copilot instructions: `.github/copilot-instructions.md` (input on every request —
  keep short). Depth goes in `.github/prompts/*.prompt.md` (loads only when invoked).
- Review bots CodeRabbit/Copilot/Cursor all need a libretro-org App install we cannot do; every
  PR push spends Kimi quota; `@kimi review` needs the workflow on master.

## Branching (GitFlow)

Branch new work off **`develop`** (integration branch); `master` is release-only (tagged
commits, hotfix/release merges). PRs targeting `master` get auto-warned by
`.github/workflows/warn-pr-base.yml` — retarget to `develop` unless source is `hotfix/*` or
`release/*`. Full flow in `docs/release-process.md`. **Branch off `libretro/develop`, not local
`develop`** (local can be stale).

## Release process (GitFlow)

Full detail in `docs/release-process.md`. Quick reference:

**Nightlies:** every push to `develop` reruns `release.yml`'s 16-platform matrix and replaces the
rolling `nightly` prerelease + its pinned tracking issue. Gated on *compiling*, not on the test
suite — don't call nightlies "CI-verified". The `nightly` tag sits outside the `v*` filter so it
can't trigger a real release.

Cutting a release:

1. `git checkout develop && git checkout -b release/vX.Y.Z`
2. Bump version (all must match): `Makefile` → `CORE_BASE_VERSION := vX.Y.Z`;
   `dist/info/virtualjaguar_libretro.info` → `display_version = "vX.Y.Z"`; `src/core/version.h`
   auto-generated (gitignored, `bash scripts/gen-version-h.sh` or rebuild).
3. Release notes: `docs/RELEASE_NOTES_vX.Y.Z.md` (template: `docs/RELEASE_NOTES_v2.3.0.md`) —
   highlights, bug fixes, perf, testing, known issues, stats (`git diff --shortstat
   vPREV..HEAD`), downloads, maintainers.
4. Verify: `make clean && make -j…` clean, `make test` passes, `strings *.dylib | grep vX.Y.Z`.
5. Commit: `chore: bump version to vX.Y.Z, add release notes`.
6. Push + PR: `git push -u libretro release/vX.Y.Z` then `gh pr create --base master`.
7. After merge to master: tag `vX.Y.Z` and push — `release.yml` builds 16 platforms + publishes.
8. Back-merge: `git checkout develop && git merge master && git push libretro develop`.
9. libretro-super: PR updating `dist/info/virtualjaguar_libretro.info` there.

Don't: tag before the PR merges to master; put new features on a release branch (bug fixes only);
forget the back-merge (step 8) or develop diverges from the tagged version string.

Savestate version policy: one bump per release (all in-flight changes share the version);
develop breakage OK.

## The website is NOT published by this repo's `pages.yml`

Live site: **https://jaguar.provenance-emu.com/**, built from `site/pages/*.html` +
`scripts/build_site.py` on **`develop`**.

**A green `Deploy Pages` run here is NOT evidence the site updated.** The libretro org's Pages
config redirects every project site to `www.libretro.com/<repo>/`, which 404s — so this repo's
`pages.yml` deploys, reports success, and publishes to a dead path.

Real publisher: **`Publish site` (workflow id `329140069`) on the
`Provenance-Emu/virtualjaguar-libretro` fork**, branch `provenance`. Checks out
`libretro/…@develop` (`fetch-depth: 0` — generator derives sitemap `lastmod` from per-file git
dates, refuses a shallow clone), builds, deploys to the fork's Pages with the verified domain.
Trigger: cron `17 * * * *` (hourly) + `workflow_dispatch`. A change merged to `develop` goes live
up to an hour later. Publish immediately:

```bash
gh workflow run 329140069 --repo Provenance-Emu/virtualjaguar-libretro --ref provenance
```

Verify against the live domain, not workflow status:

```bash
curl -sI https://jaguar.provenance-emu.com/ | grep -i last-modified
```

That timestamp tracks the fork's last publish; if it predates your `develop` commit the site is
stale. Responses carry `cache-control: max-age=600` — polling past ~10 min with unchanged
`last-modified` means content is old, not cached. Local preview: `python3 scripts/build_site.py`
→ `_site/`; `scripts/check_site.py` asserts canonical/`og:url`/sitemap/robots agree with
`SITE_BASE` (override `VJ_SITE_BASE`).

## Roadmap

Planning lives in GitHub milestones (pinned issue #371), never in md files.
