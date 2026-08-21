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

## Why R2

Any S3-compatible store works; nothing about the tooling is R2-specific.
Cloudflare R2 is the recommended default for one reason: **zero egress fees**.
CI is an egress-shaped workload — it downloads far more than it stores — which
is exactly where per-GB bandwidth pricing bills you for a corpus that costs
cents to store. 22 MB on R2 rounds to free; the full 24 GB would be ~$0.36/mo.

Avoid GitHub LFS for this. Its bandwidth is sold in $5 / 50 GB packs, and a
CI-shaped workload burns packs fast.

**The bucket must be private.** These are commercial ROMs and a BIOS image.
`rom-store.sh` never creates a bucket, never sets an ACL, and never prints a
credential.

## One-time setup

1. Create a **private** R2 bucket (Cloudflare dashboard → R2 → Create bucket),
   e.g. `vj-test-roms`.
2. Create an R2 API token scoped to **that bucket only**, with Object
   Read & Write. Note the Access Key ID, Secret Access Key, and your account
   ID — the endpoint is `https://<account-id>.r2.cloudflarestorage.com`.
3. Publish from a machine that has the full corpus:

   ```bash
   export VJ_ROM_ENDPOINT=https://<account-id>.r2.cloudflarestorage.com
   export VJ_ROM_BUCKET=vj-test-roms
   export VJ_ROM_KEY=...            # R2 access key id
   export VJ_ROM_SECRET=...         # R2 secret access key
   make roms-manifest               # regenerate from your local corpus
   make roms-publish
   ```

4. Add four repository secrets so CI can read the bucket. Use a **read-only**
   token here, not the read-write one used to publish:

   ```bash
   gh secret set VJ_ROM_ENDPOINT --repo libretro/virtualjaguar-libretro
   gh secret set VJ_ROM_BUCKET   --repo libretro/virtualjaguar-libretro
   gh secret set VJ_ROM_KEY      --repo libretro/virtualjaguar-libretro
   gh secret set VJ_ROM_SECRET   --repo libretro/virtualjaguar-libretro
   ```

Secrets are unavailable to fork PRs by design. The fetch step is best-effort
and those runs simply skip the ROM-gated checks, exactly as they do today.

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
