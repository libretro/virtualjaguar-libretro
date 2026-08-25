# Tier-1 test ROM store

Most checks worth running in this repo need a commercial ROM that cannot be
committed: the audio pair, the clipping sentinels, the #406 DRAM-scale gate,
blit memoisation, the lightgun probe, and any cachegrind A/B on a *real game*
rather than a GPU-heavy demo.

Without those ROMs `make test` exits 0 having skipped all of them. That is the
single most expensive recurring failure mode in this project's history — the
Skyhammer clipping sentinel looked for `Skyhammer_(1999).jag`, matched nothing,
and reported success for months. **A skip is not a pass.**

## What is in it, and why it is small

The full private corpus is ~24 GB. The subset the automated suite actually
asks for is **9 files, 22 MB**:

| file | requested by |
|---|---|
| `Iron Soldier (1994).jag` | audio presence/clipping/pipeline baseline |
| `Iron Soldier 2 (World).j64` | clipping sentinel (#170 regression class) |
| `Super Burnout (1995).jag` | GPU spin-loop false-positive gate (#378) |
| `Atari Karts (1995).jag` | clipping negative control |
| `Skyhammer (World).j64` | clipping sentinel |
| `Doom - Evil Unleashed (1994).jag` | `dram_scale_sweep.sh` (#406 gate), ticrate, audio gaps |
| `Alien vs Predator (1994).jag` | AvP gameplay fixture, blit memo (#411) |
| `BALLOONS.BIN` | lightgun probe |
| `[BIOS] Atari Jaguar (World).j64` | BIOS-mode boot checks |

`test/roms/tier1.patterns` is the source of truth, and every entry in it is
**copied from a real `scripts/find-rom.sh` call site** in the Makefile or
`test/tools/*.sh`. That is deliberate: the list cannot drift into disagreeing
with what the tests ask for, and adding a title without a test that requests it
just grows the download.

22 MB fits GitHub's 10 GB Actions cache with four orders of magnitude spare, so
steady-state CI egress is effectively zero — only a manifest change re-downloads.

## Choosing a backend

Two are supported. `rom-store.sh` picks GitHub when `VJ_ROM_GH_REPO` is set,
otherwise S3.

### GitHub release asset (simplest, free)

One zip attached to a release in a **private** repo, moved with `gh`. Free,
one secret, and `gh` is already on every runner. At 22 MB nothing is near a
limit (100 MB/file, 2 GB/asset).

A release **asset**, not files committed to the repo: git keeps every version
of a binary forever, so re-dumping a 4 MB ROM would add 4 MB to every clone
permanently, while an asset is replaced in place.

This is **not** the GitHub LFS path, whose bandwidth is sold in $5/50 GB packs.
Regular git operations and release assets are not metered that way.

### S3-compatible (Cloudflare R2 etc.)

Zero egress on R2, ~$0.36/mo for the entire 24 GB corpus. Worth it when the
corpus outgrows what belongs on GitHub — Tier 2 CD images are GB-scale, where
asset and repo ceilings bite long before R2's economics do.

### The risk that is not about cost

These are commercial ROMs plus a BIOS image. Hosting them on GitHub is a
takedown surface, and **a strike lands on the account or org, not just the
file**. Atari SA holds the Jaguar IP and does act on it. If you use the GitHub
backend, prefer a **personal account over the org that hosts the emulator
itself** — losing a scratch ROM repo is an inconvenience; a strike against the
project's org is not.

Either way the repo/bucket must be **private**. `rom-store.sh` never creates a
bucket, never sets an ACL, and never prints a credential.

## One-time setup

### GitHub backend

1. Create a **private** repo, e.g. `JoeMatt/vj-test-roms`. It needs no content;
   the ROMs live in a release asset.
2. Publish from a machine that has the full corpus:

   ```bash
   export VJ_ROM_GH_REPO=JoeMatt/vj-test-roms
   export GH_TOKEN=...              # or just be logged in: gh auth login
   make roms-manifest               # regenerate from your local corpus
   make roms-publish                # creates the release, uploads tier1.zip
   ```

3. Add two repository secrets. `VJ_ROM_GH_TOKEN` must be a **fine-grained PAT
   with read-only Contents** on the ROM repo — the built-in `GITHUB_TOKEN` is
   scoped to this repository and cannot read another private one (it fails as
   a 404, not a permission error):

   ```bash
   gh secret set VJ_ROM_GH_REPO  --repo libretro/virtualjaguar-libretro
   gh secret set VJ_ROM_GH_TOKEN --repo libretro/virtualjaguar-libretro
   ```

### S3 / R2 backend

1. Create a **private** R2 bucket (Cloudflare dashboard → R2), e.g.
   `vj-test-roms`.
2. Create an R2 API token scoped to that bucket, Object Read & Write. The
   endpoint is `https://<account-id>.r2.cloudflarestorage.com`.
3. Publish:

   ```bash
   export VJ_ROM_ENDPOINT=https://<account-id>.r2.cloudflarestorage.com
   export VJ_ROM_BUCKET=vj-test-roms
   export VJ_ROM_KEY=... VJ_ROM_SECRET=...
   make roms-manifest && make roms-publish
   ```

4. Add four secrets, using a **read-only** token for CI rather than the one
   used to publish:

   ```bash
   gh secret set VJ_ROM_ENDPOINT --repo libretro/virtualjaguar-libretro
   gh secret set VJ_ROM_BUCKET   --repo libretro/virtualjaguar-libretro
   gh secret set VJ_ROM_KEY      --repo libretro/virtualjaguar-libretro
   gh secret set VJ_ROM_SECRET   --repo libretro/virtualjaguar-libretro
   ```

Secrets are unavailable to fork PRs by design. The fetch step is best-effort
and those runs skip the ROM-gated checks, exactly as they do today.

## Daily use

```bash
make roms-fetch     # download + verify into test/roms/tier1
make roms-verify    # hash-check what is on disk
ROMS_PRIVATE_ROOT=test/roms/tier1 make test
```

**If you already have the corpus locally, you do not need any of this** — point
`ROMS_PRIVATE_ROOT` at your own tree and never fetch.

## Behaviour that matters

- **Exit 77 means SKIP** (store not configured), and it is deliberately not 0.
  Callers must be able to tell "no store" from "the store is broken". CI turns
  the two into a `notice` and a `warning` respectively — a configured store
  that fails is visible, not silent.
- **Verify-before-rename.** A download lands at `<file>.part`, is checked for
  size *and* SHA-256, and only then moved into place. A corrupt transfer can
  never be picked up by a later run as a valid cached copy.
- **Fetch is idempotent and offline-clean.** If every file is already present
  and hashes match, the store is not contacted at all.
- **The GitHub backend verifies the archive after unpacking, into a staging
  tree first.** A half-extracted or tampered zip is rejected (`fetched archive
  does not match the manifest`, exit 1) rather than being left in place where
  the next run's fast path would treat it as a verified corpus.
- **`manifest` and `publish` share one resolver.** They both walk
  `tier1.patterns`; neither re-derives a source path by globbing a manifest
  basename. That is not hypothetical tidiness — the first version did re-glob,
  and `[BIOS] Atari Jaguar (World).j64` became unfindable by its own name
  because `find -iname` reads `[BIOS]` as a character class matching one of
  B/I/O/S.

## Adding a title

1. Add a test that asks for it via `scripts/find-rom.sh`.
2. Copy that call's patterns into `test/roms/tier1.patterns`.
3. `make roms-manifest && make roms-publish`, and commit the manifest.

The manifest is committed; the ROMs are not, and `test/roms/tier1/` is
gitignored.

## Tier 2 and 3

Only Tier 1 is wired up. Tier 2 (a few CD images for `cd_boot_matrix`) is the
obvious next step but is ~GB-scale and wants CHD conversion first, which needs
a post-2026-08 `chdman` with CHSE session tags (see `docs/jagcd-chd.md`).
Tier 3 — the full corpus — belongs on the self-hosted runner (#535), where it
costs no transfer at all and also unblocks wall-clock benchmarking.
