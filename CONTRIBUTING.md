# Contributing

Thanks for considering a contribution.  This guide covers the practical mechanics — branching, build, test, lint — that keep CI green and reviews fast.  For architecture context, see [`CLAUDE.md`](CLAUDE.md).

## Branching (GitFlow)

This repo uses a [GitFlow](https://nvie.com/posts/a-successful-git-branching-model/)-style layout:

| Branch        | Role                                                                    |
|---------------|-------------------------------------------------------------------------|
| `master`      | Release-only. Tagged commits, hotfix merges, release-branch merges.     |
| `develop`     | Integration branch. **Default base for new PRs.**                       |
| `feature/*`   | New work. Branch from `develop`, PR back into `develop`.                |
| `release/X.Y.Z` | Release stabilization. Branched from `develop`, merged into `master`. |
| `hotfix/X.Y.Z` | Urgent fix off a tagged release. Branched from `master`, back-merged to `develop`. |

PRs targeting `master` directly trigger an auto-comment from `.github/workflows/warn-pr-base.yml` asking you to retarget to `develop` (skipped automatically if your branch is `release/*` or `hotfix/*`).

```bash
git checkout develop
git pull libretro develop
git checkout -b feature/my-thing
# ... commits ...
git push -u origin feature/my-thing
gh pr create --base develop
```

## Build

```bash
make -j$(getconf _NPROCESSORS_ONLN)          # default platform (auto-detect)
make -j$(getconf _NPROCESSORS_ONLN) DEBUG=1  # -O0 -g
make platform=ios-arm64                       # cross-compile target
make clean
```

Output: `virtualjaguar_libretro.{dylib,so,dll}` at the repo root.

## Test

```bash
make test                                     # full white-box test suite
./test/regression_test.sh ./virtualjaguar_libretro.so   # screenshot regression vs test/baselines/
make benchmark                                # headless wall-clock perf (see docs/profiling.md)
```

`make test` re-invokes the build with `TEST_EXPORTS=1` (link-test.T / exports-test.list) so the test binaries can `dlsym` into the emulator internals.  `make` (default) restores the production-slim symbol set.

## Lint (run before pushing)

CI runs all of these as gates.  Run them locally to avoid round-trips:

```bash
bash scripts/c89-lint.sh                       # mid-block declarations, declaration-after-statement
bash scripts/check-info-version.sh             # display_version matches Makefile CORE_BASE_VERSION
ec                                             # editorconfig-checker (install: brew install editorconfig-checker)
cppcheck --enable=warning,performance,portability \
  --suppressions-list=cppcheck-suppressions.txt \
  -i src/m68000 -i src/bios -i libretro-common src/ libretro.c
clang-format-diff -p1 -style=file < <(git diff -U0 develop...HEAD)  # advisory; format check on changed lines
```

### Pre-commit hook

`scripts/install-hooks.sh` installs a one-line pre-commit that runs `c89-lint.sh` against staged `.c` files.  Run it once after cloning:

```bash
bash scripts/install-hooks.sh
```

## Commit style

- Imperative present (`fix bug`, not `fixed bug`).
- Subject ≤ 72 chars; body wrap ~72.
- Prefix tags help skim `git log`: `ci:`, `build:`, `fix:`, `feat:`, `docs:`, `test:`, `refactor:`, `perf:`.
- Reference issues / PRs in the body, not the subject.

## C89 / GNU89 strict

The libretro buildbot uses MSVC on Windows — MSVC C89 mode is strict about declarations-before-statements and forbids C99 features.  See [`CLAUDE.md`](CLAUDE.md) for the full rule set.

## Releases

See [`docs/release-process.md`](docs/release-process.md) for the full release flow (cut `release/X.Y.Z` from `develop`, tag on `master`, back-merge to `develop`).
