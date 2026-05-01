# Release process

How to ship a new tagged release of the libretro core.

## Branching model (GitFlow)

This repo uses a [GitFlow](https://nvie.com/posts/a-successful-git-branching-model/)-style layout:

- `master` — release-only.  Tagged commits, hotfix merges, release-branch merges.  Default branch on GitHub for visibility / clone defaults.
- `develop` — integration branch.  All feature work flows in here first.
- `feature/*` — branched from `develop`, merged back via PR.
- `release/X.Y.Z` — branched from `develop` when cutting a release; bug-fix-only until tagged on `master`, then back-merged to `develop`.
- `hotfix/X.Y.Z` — branched from `master`, fixed, tagged on `master`, back-merged to `develop`.

PRs targeting `master` directly trigger a friendly comment from `.github/workflows/warn-pr-base.yml` asking the contributor to retarget to `develop` (skipped for `release/*` and `hotfix/*` source branches).

## TL;DR

1. Cut `release/X.Y.Z` from `develop`.  Bump `CORE_BASE_VERSION` in `Makefile` (or use the `Bump Version & Release` workflow).  Update `docs/RELEASE_NOTES_vX.Y.Z.md`.  Open a PR from `release/X.Y.Z` → `master`.
2. Merge into `master`.
3. `git tag vX.Y.Z && git push libretro vX.Y.Z` (or via GitHub UI).
4. Watch [Actions](https://github.com/libretro/virtualjaguar-libretro/actions) — `release.yml` builds 14 platforms, generates `SHA256SUMS.txt`, and publishes the release with `docs/RELEASE_NOTES_vX.Y.Z.md` as the body.
5. **Back-merge `master` → `develop`** so the version bump and any release-branch fixes are in `develop`: `git checkout develop && git merge master && git push libretro develop`.
6. After the tag publishes, send a small PR to [libretro/libretro-super](https://github.com/libretro/libretro-super) updating `dist/info/virtualjaguar_libretro.info` to match `dist/info/virtualjaguar_libretro.info` from this repo at the tag.

## Detail

### 1. Pre-tag checklist

- `make` (default) builds clean, `make test` is green, `bash scripts/c89-lint.sh` passes.
- CI on the release PR is green except `claude-review` (non-blocking; AI review service refuses diffs > 20k lines).
- `docs/WHATSNEW` v`X.Y.Z` section is up-to-date.
- `docs/RELEASE_NOTES_vX.Y.Z.md` exists. (See below for how to generate one.)
- `dist/info/virtualjaguar_libretro.info` is up-to-date — `display_version` matches the new tag, any new feature flags are reflected (`savestate`, `cheats`, `disk_control`, `hw_render`, etc.).

### 2. Tag

```
git tag -a v2.2.0 -m "v2.2.0"
git push libretro v2.2.0
```

The push triggers `.github/workflows/release.yml` which runs only on `v*` tags.

### 3. What `release.yml` does on tag push

For each of 14 target platforms (Linux x86_64/aarch64/i686, macOS arm64/x86_64, Windows x86_64/i686, Emscripten WASM, Android arm64-v8a/armeabi-v7a/x86_64/x86, iOS arm64, tvOS arm64), in parallel:

1. Build the core with `RELEASE_DEBUG_INFO=1` so the optimized binary keeps `-g` symbols.
2. Extract split debug info into a `<platform>-debug.tar.gz` archive (`objcopy --only-keep-debug` for ELF, `dsymutil` for Mach-O).
3. Strip the shipped binary so it's small.
4. Upload as a CI artifact.

Two extra container-based jobs do PS Vita (`vitasdk/vitasdk:latest`) and Nintendo Switch (`devkitpro/devkita64:latest`).

After all platform builds finish, the `release` job:

1. Collects every artifact into `release/`.
2. Adds `dist/info/virtualjaguar_libretro.info` to `release/`.
3. Generates `release/SHA256SUMS.txt` (sorted, `sha256sum -c` compatible).
4. Reads `docs/RELEASE_NOTES_v<TAG>.md` if present and uses it as the release body, with the artifact list appended. Falls back to GitHub `--generate-notes` if no curated file is found.
5. Creates the GitHub release with all of `release/*` attached.

### 4. Post-tag: update libretro-super

RetroArch ships with a `.info` file per core, sourced from `libretro/libretro-super/dist/info/`. Update is **manual** — there's no automated mirror.

```
# Fork libretro-super, then:
cd libretro-super
git checkout master && git pull
cp <this-repo>/dist/info/virtualjaguar_libretro.info dist/info/
git checkout -b update-virtualjaguar-vX.Y.Z
git add dist/info/virtualjaguar_libretro.info
git commit -m "virtualjaguar: bump to vX.Y.Z"
git push origin update-virtualjaguar-vX.Y.Z
gh pr create --repo libretro/libretro-super \
  --title "virtualjaguar: bump info to vX.Y.Z" \
  --body  "Mirrors dist/info/virtualjaguar_libretro.info from
libretro/virtualjaguar-libretro at tag vX.Y.Z.  Includes new
savestate/cheats/cheevos flags now that the core supports them."
```

The libretro-super maintainers (typically @inactive123, @MrHuu, @aliaspider) merge these. The change appears in the next RetroArch update / shipped install.

### 5. Generating release notes

The `docs/RELEASE_NOTES_v<TAG>.md` file is written by hand (or with a sub-agent's help) before tagging. Recommended structure is in [`docs/RELEASE_NOTES_v2.2.0.md`](RELEASE_NOTES_v2.2.0.md) — Highlights, What's new (lifted from `docs/WHATSNEW`), Game compatibility summary, Known issues, "Compared to upstream" stats from `git shortlog -sn` and `git diff --shortstat libretro/master...HEAD`, Downloads list, Maintainers.

For future releases (where there's a previous tag to diff against), use `git shortlog vPREV..HEAD` instead of `libretro/master..HEAD`.

### Hotfix flow

When a critical bug ships in a release and can't wait for the next `develop` cycle:

```
git checkout master && git pull
git checkout -b hotfix/X.Y.Z+1
# ... fix, bump CORE_BASE_VERSION patch, update RELEASE_NOTES, commit ...
gh pr create --base master --title "hotfix: <issue>" --body "Fixes #N.  Bumps to vX.Y.Z+1."
# After merge:
git tag vX.Y.Z+1 && git push libretro vX.Y.Z+1
git checkout develop && git pull && git merge master && git push libretro develop
```

The `hotfix/*` source branch suppresses the warn-on-master-PR workflow.

### 6. If a release-job step fails

The workflow runs only on `v*` tag push, but the `release` job is re-runnable from the [Actions tab](https://github.com/libretro/virtualjaguar-libretro/actions) without needing a new tag. Common failure modes:

- **A platform build broke** (compiler upgrade, NDK version drift): re-run from Actions, or fix and push a new tag `vX.Y.Z+1`.
- **`gh release create` 422'd because the tag already has a release**: delete the partial release in the GitHub UI and re-run the `release` job.
- **`objcopy` not found on a less-tested platform** (Vita, Switch): the workflow uses `|| cp` fallbacks so the build succeeds with an unsplit debug archive; the binary itself ships fine.

### 7. Artifact naming

Each binary follows `virtualjaguar_libretro-<platform>.<ext>`:

```
virtualjaguar_libretro-linux-x86_64.so
virtualjaguar_libretro-linux-x86_64-debug.tar.gz
virtualjaguar_libretro-macos-arm64.dylib
virtualjaguar_libretro-macos-arm64-debug.tar.gz   # contains a .dSYM bundle
virtualjaguar_libretro-windows-x86_64.dll
virtualjaguar_libretro-windows-x86_64-debug.tar.gz
virtualjaguar_libretro-android-arm64-v8a.so
virtualjaguar_libretro-emscripten-wasm.bc
virtualjaguar_libretro-ios-arm64.dylib
virtualjaguar_libretro-tvos-arm64.dylib
virtualjaguar_libretro-vita.a
virtualjaguar_libretro-switch.a
SHA256SUMS.txt
virtualjaguar_libretro.info
```

The `.info` file in the release is the same one that should land in libretro-super.
