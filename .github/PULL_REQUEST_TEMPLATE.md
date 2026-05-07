<!--
Thanks for the PR!  This repo uses a GitFlow-style integration
branch: please target `develop` (the default for new feature work).
Use `master` only for hotfixes from `hotfix/*` or release branches
from `release/*`.
-->

## Summary

<!-- 1-3 sentences: what changed and why. -->

## Test plan

<!-- How did you verify this?  Build commands run, tests added, manual
checks against a ROM, etc.  CI green is necessary but not sufficient. -->

- [ ] `make -j$(getconf _NPROCESSORS_ONLN)` builds clean on host
- [ ] `make test` passes (or N/A — explain why)
- [ ] `bash scripts/c89-lint.sh` passes (no mid-block declarations)

## Branch base

- [ ] **Base is `develop`** (the integration branch)
- [ ] OR base is `master` *and* this is a `hotfix/*` or `release/*` branch
