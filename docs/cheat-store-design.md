# Community cheat store: design (#1071)

Follow-up to #1050 / #1052. CI converts pinned community C cheat sources with the existing
`tools/cheat-import/convert.py`, commits the packs under `site/cheats/`, the project site serves
them, and the desktop app installs them per cartridge from a "Community cheats" section.

Permission: jguffer (author of jguffer/BigPEmu_Scripts and #1050) gave the maintainer permission
to convert and redistribute his scripts. Every pack and the app credit him.
`scripts/cheat_aliens_vs_predator.c` is excluded: it credits values to a third-party script
bundled with BigPEmu.

Prototype (61 sources at `24211503`, local corpus): 17 cartridge packs convert today. One
converter fix (accept `bigpemu_register_setting_int_full` as a non-boolean, therefore omitted,
setting; writes that depend on it are still refused by the existing dependency tracking) lifts it
to 22. The rest go to the worklist: pointer/array declarations (6), CD titles, RAM reads,
`printf_notify`, >10 settings, unknown cartridge hashes (no corpus cart).

BigPEmu API map (user request): `tools/cheat-import/bigpemu-api.json` lists every BigPEmu call the
sources use, what it does (from the community sources, not an SDK), our equivalent, and its status
(`supported`, `setting-omitted`, `ignored`, `unsupported`; anything else is `unmapped`). The
generator records per-source API use in `unsupported.json` (`calls`, top-level `api_usage`), packs
carry a note for ignored calls, and CI warns on unmapped calls, so the worklist grows from data.
Mapping `printf_notify` as ignored (message only) adds Iron Soldier (3/3): 23 packs.

Two PRs into `develop`: (1) data + CI + site (`Refs #1071`), (2) app (`Closes #1071`).

## Data and CI

Scope: everything except the app UI. Converter = `tools/cheat-import/convert.py`, unchanged in
logic (one factoring: split `cht_text` into `cht_header` + `cht_entries`, see §5; plus the
coordinator's `register_setting_int_full` fix, which only widens the unsupported-setting path).
Committed outputs live under `site/cheats/`; the Provenance-fork "Publish site" cron copies them
verbatim within the hour, exactly like `site/thumbnails/`.

### 1. `tools/cheat-import/sources.json` (input, hand-edited / bumped by CI)

```json
{
  "format": 1,
  "sources": [
    {
      "id": "jguffer",
      "kind": "github",
      "repo": "jguffer/BigPEmu_Scripts",
      "branch": "main",
      "commit": "24211503339a969f8494b8e189b8f2aa0cb9eba6",
      "commit_date": "2026-04-15",
      "include": ["scripts/cheat_*.c"],
      "exclude": [
        {"path": "scripts/cheat_aliens_vs_predator.c",
         "reason": "credits values to a third-party script bundled with BigPEmu (\"Taken from Whitehouse avp_common.c script\"); permission covers only the author's own work"}
      ],
      "notes": {
        "scripts/cheat_baldies.c": "disc title; v1 packs cover cartridges only (converter refuses discs, app has no disc CRC)"
      },
      "author": "jguffer",
      "credit": "Cheats by jguffer (https://github.com/jguffer/BigPEmu_Scripts), converted by the Virtual Jaguar project with the author's permission.",
      "permission": "Author granted conversion and redistribution to the maintainer (see libretro/virtualjaguar-libretro#1071, #1050). Repository has no license file.",
      "license": null
    }
  ]
}
```

- `kind`: `"github"` now; `"local"` reserved for an in-repo directory of manual ports
  (`{"id":"vj","kind":"local","path":"tools/cheat-import/ports","include":["*.c","*.cht"]}`; a `.cht`
  there is copied through verbatim after guard validation). Schema only; not created now.
- `branch` is what the bump workflow polls; `commit` is the pin; `commit_date` (upstream committer
  date, ISO) is written by the bump and feeds the deterministic `updated` fields (§6).
- `include`/`exclude` are fnmatch globs relative to the repo root. Only `scripts/cheat_*.c` is ever
  fetched; the generator refuses any include pattern not ending in `.c`.
- `notes[path]` is a free-text line appended to that file's worklist entry whatever its outcome.
- Pinning: **commit only**. The generator fetches ONE tarball per source
  (`https://codeload.github.com/<repo>/tar.gz/<commit>`, limit 32 MiB, extracted in memory, only
  members matching `include` kept, each capped at 1 MiB). A commit is content-addressed; per-file
  sha256 pins are redundant as input and go to the OUTPUT instead (manifest + `.cht` header) as
  provenance. `community-cases.json` style per-file hashes are therefore dropped (§7).

### 2. `tools/cheat-import/cartridges.json` (committed identity table, hashes only)

```json
{"format": 1, "cartridges": [
  {"fnv1a64": "75F2216BD4F54AC2", "crc32": "5E2CDBC0", "size": 4194304,
   "title": "Doom", "name": "Doom - Evil Unleashed (1994)"}
]}
```

Sorted by `fnv1a64` (upper-case hex, 16 chars); `crc32` upper-case 8 chars; `size` = native payload
bytes; `title` = display title the manifest shows; `name` = No-Intro-style full name (region/rev)
for disambiguation when one title has several revisions. Uniqueness: `fnv1a64` unique; `crc32`
unique (a collision fails the generator; it would break the app's binding anyway).

Built by `tools/cheat-import/scan_cartridges.py --rom-dir DIR [--merge cartridges.json] --output NEW.json`:
walks `*.j64/*.jag/*.rom/*.bin`, reuses `convert.rom_bytes` (same copier-header refusal) and
`convert.fnv1a64` + `zlib.crc32`, pre-fills `name` from the file stem and `title` from the stem up to
the first ` (`. `--merge` keeps existing curated `title`/`name` for known hashes and only appends.
Output is a new file (never overwrites). The maintainer edits titles and opens a PR; review = the
diff. Never ROM bytes, never file paths, in the committed file.

### 3. Output layout under `site/cheats/` (committed)

```
site/cheats/manifest.json        packs index (schema §4)
site/cheats/unsupported.json     worklist
site/cheats/packs/<CRC32>.cht    one pack per cartridge CRC32 (upper-case hex)
site/cheats/index.html           static human page, same pattern as site/thumbnails/index.html
site/cheats/README.md            provenance + credit + how it is generated (copied verbatim)
```

`build_site.py` adds, next to the thumbnails block: `copytree(SITE_SRC/"cheats", out/"cheats")`.
`index.html` is NOT in `ALL_PAGES` (it is `noindex`, hand-written, like the thumbnails one) and is
regenerated by `build_packs.py` so its game list cannot go stale: title, cart name, CRC, cheat
count, partial flag, link to the pack, credit line, and a link to `unsupported.json`.

**One pack per CRC32, merging every source that names that cartridge.** Rules:
- Order: sources in `sources.json` order, files in sorted path order, settings in registration
  order. Stable across runs.
- Dedupe: identical `(casefold(name), code)` pairs collapse to one entry (kasumi_ninja vs
  kasumi_ninja_infinite_health). The manifest lists both files as contributors.
- Same name, different code (mc3d_classic vs mc3d_virtual "Infinite X"): the later entry gets the
  suffix ` (<module.title>)` if the two modules' category titles differ, else ` (<file stem minus
  cheat_>)`. Mandatory because `parseCht` merges CONSECUTIVE identically named entries into one
  toggle; the suffix ` (Part N)` is reserved and already refused by the converter.
- Cross-source interaction: impossible by construction (each BigPEmu module reads only its own
  settings, so no `(A && B)` guard can span files); the converter's combination check stays
  per-source. What CAN happen across sources is address overlap with different values. The
  generator computes each cheat's byte image; overlapping pairs are shipped (the app applies in a
  fixed order, same as any libretro-database set) and recorded in the manifest as
  `overlaps: [[i, j], ...]` plus a `# note:` comment in the `.cht`. Not refused.
- A source whose literals hit two table rows (two revisions) is converted twice, once per digest,
  and lands in two packs.
- Partial sources (`--allow-partial` semantics): only complete toggles are exported; every omission
  is a `notes[]` row on the pack AND a row in `unsupported.json`.
- A source with no table hit, a module-level `Unsupported`/`ParseError`, or zero converted toggles
  produces no pack entry and one worklist row.

`unsupported.json`:
```json
{"format": 1, "entries": [
  {"source": "jguffer", "path": "scripts/cheat_baldies.c", "sha256": "...",
   "stage": "module",            // "identity" | "module" | "setting"
   "crc32": null, "setting": null,
   "reason": "unsupported initialisation call bigpemu_register_setting_int_full",
   "note": "disc title; v1 packs cover cartridges only ..."}
]}
```
`stage=identity` reasons: `"no known cartridge for literals [..]"` / `"no 64-bit literal found"`.
`stage=setting` rows carry `crc32` and `setting`. Sorted by (source, path, setting).

### 4. `site/cheats/manifest.json` (exact schema; flat for `app/src/neuro/neuro_json.h`, which is the app's only JSON reader: doubles, strings, arrays, objects, sorted keys)

```json
{
  "format": 1,
  "generated_from": {
    "converter_sha256": "<sha256 of tools/cheat-import/convert.py>",
    "generator_sha256": "<sha256 of tools/cheat-import/build_packs.py>",
    "cartridges_sha256": "<sha256 of cartridges.json>",
    "sources": [{"id": "jguffer", "repo": "jguffer/BigPEmu_Scripts",
                 "commit": "2421150...", "commit_date": "2026-04-15",
                 "url": "https://github.com/jguffer/BigPEmu_Scripts/tree/2421150...",
                 "author": "jguffer", "credit": "...", "permission": "..."}]
  },
  "packs": [
    {
      "crc32": "5E2CDBC0", "fnv1a64": "75F2216BD4F54AC2",
      "title": "Doom", "name": "Doom - Evil Unleashed (1994)",
      "file": "packs/5E2CDBC0.cht", "bytes": 1234,
      "sha256": "<sha256 of the .cht bytes>",
      "updated": "2026-04-15",
      "credit": "Cheats by jguffer (...), converted ... with the author's permission.",
      "cheats": 8, "writes": 21, "partial": false, "exceeds_core_limit": false,
      "sources": [{"source": "jguffer", "path": "scripts/cheat_doom.c",
                   "sha256": "197cc6...", "converted": 8, "omitted": 0}],
      "notes": [{"source": "jguffer", "path": "scripts/cheat_atari_karts.c",
                 "setting": "Unlock All Challenges, Cups, Racers",
                 "reason": "unsupported call bigpemu_jag_read8 (...)"}],
      "overlaps": []
    }
  ],
  "unsupported": "unsupported.json",
  "counts": {"packs": 20, "cheats": 60, "partial_packs": 4, "unsupported_entries": 41}
}
```

- **Pack version = `sha256`.** No separate version field: it changes iff the bytes change, and the
  app can recompute it from a stored file offline (update check = compare manifest `sha256` with
  the local file's hash; integrity check after download = same value). Hex strings everywhere;
  counts/bytes as integers (exact in double).
- `updated` = max `commit_date` over the pack's contributing sources: deterministic, human-meaningful,
  no build clock. `credit` is pre-rendered per pack (joined with `; ` when several sources
  contribute) so the app shows one string without templating.
- `packs` sorted by `crc32`; `sources`/`notes` inside a pack in the generation order of §3.
- URL: the app resolves `file` against `https://jaguar.provenance-emu.com/cheats/`; the manifest
  carries no absolute URL so a mirror or local build serves the same bytes.

### 5. `.cht` content

Factor `convert.cht_text(title, crc, entries, source_name, source_sha)` into
`cht_header(crc, comment_lines)` + `cht_entries(entries)`; `cht_text` keeps its signature and
output byte-for-byte (existing tests stay green). `build_packs.py` writes:

```
# Community cheats for Doom (Doom - Evil Unleashed (1994)); nothing is enabled automatically.
# Credit: Cheats by jguffer (https://github.com/jguffer/BigPEmu_Scripts), converted by the Virtual Jaguar project with the author's permission.
# Source: jguffer/BigPEmu_Scripts@24211503339a969f8494b8e189b8f2aa0cb9eba6 scripts/cheat_doom.c
# Source SHA256: 197cc6ff...
# Omitted: "Unlock All Challenges, Cups, Racers" (scripts/cheat_atari_karts.c): unsupported call bigpemu_jag_read8 (...)
# Overlap: cheat 2 and cheat 5 write the same bytes; later wins (app order).
# Cartridge revision guard is enforced by the Virtual Jaguar app.
vj_rom_crc32 = "5E2CDBC0"
cheats = 8

cheat0_desc = "Infinite Ammo"
cheat0_code = "..."
cheat0_enable = false
```

Parser facts (`app/src/cheats.cpp`): `parseCht` skips blank and `#` lines and any key not matching
`cheat<N>_`; `loadChtFile` scans only `vj_rom_crc32` (duplicate/malformed = whole set blocked).
So comments and unknown top-level keys are safe. **No new key is needed**: the app derives the
version by hashing the file; credits/notes for the UI come from the manifest, not from parsing
comments. `quoted()` already refuses quotes/backslashes/control chars in names; the generator
additionally strips CR/LF from any text it puts in a comment line.

### 6. Determinism and the regen check

- `json.dumps(obj, indent=2, sort_keys=True, ensure_ascii=True) + "\n"`; LF only; files opened with
  `newline="\n"`; no timestamps, hostnames, paths or Python version anywhere in outputs.
- All orderings fixed (§3). Tarball extraction order ignored (members sorted by path).
- `index.html` is generated from the manifest only.
- Regen check (CI, §8): `build_packs.py --out $RUNNER_TEMP/cheats` then
  `diff -r $RUNNER_TEMP/cheats site/cheats`; any diff fails with the diff in the log. Second
  invariant in the same job: run the generator twice to separate temp dirs and `diff -r` them
  (catches nondeterminism independently of what is committed).
- Converter invariant run on every real source during generation: `module.convert(digest ^ 1)`
  must yield no `code` (wrong-hash activation = hard failure), replacing the same check in
  `community_test.py`.

### 7. Generator CLI and tests

`tools/cheat-import/build_packs.py` (Python 3.10+, pycparser; stdlib `tarfile`/`urllib`):

```
build_packs.py --sources sources.json --cartridges cartridges.json --out DIR
               [--source-dir ID=PATH ...]   # local checkout instead of tarball fetch (tests, offline)
               [--allow-network/--no-network]  # default: network only when no --source-dir covers a source
               [--summary OLD_MANIFEST NEW_MANIFEST]  # prints Markdown added/changed/removed per game (bump PR body)
               [--max-pack-bytes 65536] [--max-total-bytes 1048576]
```
Exit 0 = wrote `manifest.json`, `unsupported.json`, `packs/`, `index.html`, `README.md` into DIR
(refuses a non-empty DIR without its own `.vj-cheat-packs` marker, like `build_site.py`); exit 1 =
validation failure (collision, budget, wrong-hash activation, bad schema); exit 2 = fetch failure.
Zero converted packs overall is exit 1 (an empty store is never published silently).

Identity scan per file: `re.findall(r"0x[0-9A-Fa-f]{9,16}", text)` on the uncommented source,
intersect with `cartridges.json` keys (case-insensitive). Literals that are not in the table are
listed in the worklist reason so a corpus gap is visible.

Tests `test_build_packs.py` (synthetic only, same style as `test_convert.py`; reuse its `source()`
builder, `ROM`, `DIGEST`): two-sources-one-CRC merge with exact-duplicate dedupe; same-name
different-code suffixing; two-revision source yields two packs; no-table-hit, module-error and
setting-error worklist rows with the right `stage`; `notes` appended; excluded file listed with
reason and never parsed; `include` not ending in `.c` refused; overlap recorded; `exceeds_core_limit`
set at 257 writes; wrong-hash activation fails; determinism (two runs byte-equal); pack/total byte
budgets; `--summary` output for an added, a changed and a removed cheat; `scan_cartridges.py`
on synthetic ROM bytes (+ copier-header refusal, `--merge` keeps curated titles).

`community_test.py`, `test_community.py`, `community-cases.json`: **deleted.** The committed packs
plus the regen-diff job ARE the conversion contract (a changed value shows as a diff in
`site/cheats/packs/*.cht` in the PR that changed the converter), the fetch is now the tarball
step, and the wrong-hash check moved into the generator. `README.md` loses its "does not
redistribute" paragraph and the community_test section; it gains: permission statement (verbatim
from #1071), the credit requirement, the sources/cartridges/pack workflow, and "how to add a
source" (PR with author permission note).

### 8. Workflows (CI budget: nothing runs on unrelated PR pushes)

**`cheat-import.yml` (extend).** Triggers: `push`/`pull_request` with paths
`tools/cheat-import/**`, `site/cheats/**`, the workflow file; `workflow_dispatch`. Add
`concurrency: {group: cheat-import-${{ github.ref }}, cancel-in-progress: true}`. Jobs:
1. `converter` (existing matrix 3.10/3.13): `unittest discover` (now includes generator tests);
   drop the `community_test.py --fetch` step.
2. `packs` (3.13 only, `needs: converter`): `build_packs.py --out $RUNNER_TEMP/a`, again to `/b`,
   `diff -r a b`, `diff -r a site/cheats`, then `python3 scripts/build_site.py --releases
   scripts/testdata/releases.json --out $RUNNER_TEMP/site && python3 scripts/check_site.py
   $RUNNER_TEMP/site` so a manifest/`.cht` the site check rejects never merges.

**`cheat-packs-bump.yml` (new).** `schedule: cron '23 6 * * 1'` (Monday) + `workflow_dispatch`;
`permissions: {contents: write, pull-requests: write}`; `concurrency: {group: cheat-packs-bump,
cancel-in-progress: false}`; `GH_TOKEN: ${{ github.token }}` (no PAT exists; GITHUB_TOKEN is the
choice). Steps:
1. For each `kind=github` source: `gh api repos/<repo>/commits/<branch> --jq .sha,.commit.committer.date`.
   No pin moved -> `exit 0` (log only).
2. Write new `commit`/`commit_date` into `sources.json` (via a `--bump ID=SHA:DATE` mode of
   `build_packs.py` so the JSON formatting stays canonical), regenerate to `site/cheats/`, run the
   unit tests and the same regen/site checks as job `packs`. Any failure: open/refresh the PR
   anyway, body headed "VALIDATION FAILED", with the log tail; never push a red pin silently.
3. `build_packs.py --summary <old manifest from git show HEAD:site/cheats/manifest.json> <new>` ->
   Markdown: per game added/changed/removed cheats, new/removed packs, worklist delta, byte totals.
4. Branch `cheats/pin-bump` (fixed name; force-push), commit
   `data(cheats): bump jguffer/BigPEmu_Scripts to <sha7>` (+ attribution lines), `gh pr create
   --base develop` if no open PR for that head, else `gh pr edit` body. Body = step-3 summary,
   step-2 results, upstream compare link, `Refs #1071`, and the note below.
5. `gh workflow run cheat-import.yml --ref cheats/pin-bump`. `workflow_dispatch` and
   `repository_dispatch` are the documented exceptions to "GITHUB_TOKEN events create no runs", and
   the run attaches to the branch head SHA so it shows in the PR's Checks tab. The PR body states:
   "Opened by Actions with GITHUB_TOKEN: `pull_request` workflows did not fire. The converter
   workflow was dispatched on this branch; to run the full PR suite, close and reopen the PR or
   push an empty commit." Repo settings required: "Allow GitHub Actions to create and approve pull
   requests" (confirmed on). Branch protection on `develop` is unchanged; a maintainer merges.

**`pages.yml`**: add `site/cheats/**` and `tools/cheat-import/**` to `paths`. (Dead publisher here,
but the filter documents intent; the Provenance-fork cron publishes regardless.)
**`check_site.py`**: new `check_cheats(out)`: `cheats/manifest.json` parses, `format == 1`, every
`packs[].file` exists, its sha256/bytes match, its `vj_rom_crc32` equals `crc32`, no `.cht` in
`packs/` is unlisted, `unsupported.json` parses, `index.html` exists and every local link in it
resolves, sizes within §9. Skipped only when `cheats/` is absent (older checkouts).
**`site/pages/privacy.html`**: one row "Community cheats (jaguar.provenance-emu.com/cheats/)":
manifest + pack download, no identifiers sent, cached locally.

### 9. Size and limits

- Pack: hard max 64 KiB (`--max-pack-bytes`); realistic worst case is 256 writes * 14 chars ~ 4 KiB
  per cheat, 10 settings -> ~40 KiB. Manifest hard max 256 KiB. `site/cheats/` total hard max 1 MiB
  (`--max-total-bytes`); today's expectation: ~20 packs, < 40 KiB total, manifest ~15 KiB.
- `CheatManager::kCoreLimit = 256` applies to the sum of ENABLED segments. A pack is written as-is;
  `writes` (sum of all segments) and `exceeds_core_limit` (`writes > 256`) are in the manifest so
  the app can warn "enabling every cheat in this pack exceeds the core limit" without parsing.
  The converter already refuses a single cheat over 256 writes.
- Fetch: tarball 32 MiB cap, 60 s timeout, each kept member 1 MiB cap (converter's own limit).

### 10. Risks

- Upstream has no license file; redistribution rests on the author's message. `permission` text
  and per-pack credit are the mitigation; a withdrawal means deleting `site/cheats/packs/*` in one PR.
- Values are BigPEmu-authored; nothing here proves gameplay correctness. Packs ship off by default
  and partial; the manifest says which settings were omitted. Worklist entries are converter work,
  not app work.
- The 64-bit-literal scan is heuristic: a non-hash literal that collides with a table row would
  bind a script to the wrong cartridge. Collision with a real FNV1a64 is negligible, but the
  generator lists all literals in the worklist so review sees them.
- `cartridges.json` only grows by maintainer PR from the private corpus; six scripts currently have
  no table hit and stay on the worklist until someone with those carts scans them.
- Cross-source address overlap is shipped, not refused (§3); the app's fixed order makes it
  deterministic but a user enabling both gets last-writer semantics.
- The app matches `.cht` sets by name stem, not CRC; the app side must store downloaded packs where
  its "Community" section binds them by `vj_rom_crc32`, or stem matching will not find `<CRC>.cht`.
- Bump PRs via GITHUB_TOKEN never get the full `pull_request` suite until a human close/reopen;
  the dispatched converter run and the in-body validation are the gate until then.
- `community_test.py` removal drops the Python-3.10 contract run on real sources; the `packs` job
  runs on 3.13 only. The converter's unit tests still run on both.

## App

Scope: the "Community cheats" section of the Cheats window and everything behind it. The data side
(design-data.md) is fixed: `manifest.json` (§4) at `https://jaguar.provenance-emu.com/cheats/`, packs at
`cheats/packs/<CRC32>.cht`, pack version == sha256 of the bytes, `vj_rom_crc32` guard inside.

Facts the design leans on (verified in the tree):
- `CheatManager::rescan()` lists `<dir>/*.cht` and `<dir>/Atari - Jaguar/*.cht` only, non-recursive, so a
  sub-folder of `<data>/cheats/` is invisible to the stem-matched library path.
- `loadChtFile` already enforces the `vj_rom_crc32` guard; `parseCht` ignores `#` lines and non-`cheatN_` keys.
- The core (`src/core/cheat.c`) appends segments in the order `retro_cheat_set` is called and stops at 256;
  the index is only a tag. So the batch order in `apply()` IS the write order and the 256 cap is "first 256
  segments in that order".
- The app has ONE downloads switch: `library.art` (`LibraryService::downloadArt`); RetroAchievements images
  already treat it as the global offline switch (`Achievements::updateImageNetwork`).
- Background HTTP pattern: `Worker` (library_service.h) + `http::get` on the worker + results handed over under a
  mutex + `pump()` once per UI frame on the main thread (`LibraryService::updateDatabase`, `ra::ImageCache`).
  `ra::ImageCache` injects `fetch` as a `std::function` for tests. The app has no SHA-256 anywhere.
- `neuro::Json` (parse never throws, depth 32, doubles/strings/arrays/objects) reads the manifest as-is.

### 1. Storage (`<data>/cheats/community/`)

```
<data>/cheats/community/manifest.json      last manifest fetched, verbatim bytes (cap 256 KiB)
<data>/cheats/community/packs/<CRC32>.cht  installed pack, bytes exactly as served (cap 64 KiB)
<data>/cheats/community/packs/<CRC32>.json manifest `packs[i]` object for that pack, Json::dump() at install
```
`<CRC32>` is upper-case 8 hex (`%08X` of `GameIdentity.crc`), same as the manifest/URL. A pack is "installed"
iff `<CRC32>.cht` exists; the `.json` sidecar is what the UI shows offline (title, credit, cheats, writes,
partial, exceeds_core_limit, notes[], updated, sha256). If the sidecar is missing (hand-copied file) the section
says "credits unavailable: use Update" and still works. Last manifest check time: `cheats.community.checked`
(unix seconds) in config.ini. No separate index file: the directory is the index.

Install is atomic and verified in memory before anything is written: `http::get(url, resp, 64 KiB)` ->
`resp.status == 200` -> `resp.body.size() == pack.bytes` -> `sha256(body) == pack.sha256` ->
`loadChtText(body, set)` parses with >= 1 cheat and `set.revisionBound && set.restriction.empty() &&
set.romCrc == crc` -> `writeFileAtomic(<CRC>.cht)` then `writeFileAtomic(<CRC>.json)` (temp + rename, per-process
temp name, already in paths.cpp). Any failure leaves the previous install untouched. `loadChtText` is
`loadChtFile` minus the `readFile` (tiny refactor; `loadChtFile` calls it).

### 2. CheatManager changes

- `enum class Origin { Database, Community }` and a second set: `ChtSet community_; bool communityLoaded_;`
  with `setCommunityDir(dir)` (`<data>/cheats/community/packs/`). `gameLoaded()` calls `loadCommunity()` after
  `pickSet()`: path `<dir>/<%08X of crc>.cht`, only when `crc != 0`; `loadChtFile` + the same guard check as
  `loadSelectedSet` (a mismatched or unbound file is loaded with every cheat marked unsupported and
  `restriction` set, never silently applied). `reloadCommunity()` is public for the store to call after an
  install/update/remove of the current game's pack. Accessor `const ChtSet* communitySet() const`.
  Independent of `selected_`, `matches_`, `library_`: the database set stays exactly as today.
- Toggles: `isEnabled(c, Origin)`, `setEnabled(c, on, Origin)`, `setEnabledByName(name, on)` searches database,
  then community, then user (CLI `--cheat-on` gains packs for free). Ini keys: database `on.<id>`, community
  `cp.<id>`; `enabled_` holds the prefixed key, so ids can never collide. `id` is unchanged (FNV-1a of
  desc + "\n" + code): across a pack update an unchanged cheat keeps its toggle, a cheat whose text OR code
  changed gets a new id and comes back OFF (stated in the UI once per update: "N cheats changed and were turned
  off"). `loadCommunity()` prunes `cp.*` keys that match no cheat in the loaded pack (stale after an update), so
  the ini never grows.
- `apply()` order: database set (index i), community pack (index `kCommunityIndexBase = 5000 + i`), user codes
  (10000 + i). Rationale: the core writes in batch order and `apply()` is the only writer, so "later wins" is
  deterministic; the CRC-bound pack is more specific than a stem-matched database file, so it overrides it on
  an overlapping address; user codes are explicit intent and win over everything. `appliedSegments_` sums all
  three and the existing > `kCoreLimit` warning stays the single gate; the manifest's `writes`/`exceeds_core_limit`
  let the UI warn BEFORE the user ticks everything. `enabledCount()`, `disableAll()` cover the pack too.
- Uninstall (`Remove`): deletes `<CRC>.cht` + `.json`, erases every `cp.*` key of the game's ini, re-applies.
  Reinstalling starts with everything off. `gameClosed()` clears `community_` like `set_`.

### 3. Update and merge rules

- User codes (`UserCheat`, `user.N.*`) are never read or written by install/update/remove. A pack cheat the user
  copied into Manual codes is an ordinary user code: unaffected, applied last, duplicates the write (harmless).
- Update = install over the existing file (same verification); toggles follow §2 by id. There is no merge of
  cheat lists: the pack file is always exactly the served bytes.
- Manifest no longer lists an installed CRC: the file stays, the section shows "No longer offered by the store
  (installed copy kept)", toggles keep working, `Remove` is offered, `Update` is not.
- Manifest sha256 differs from the installed `.json`'s sha256 (not a rehash of the file: the sidecar is the
  record of what was verified at install): state `UpdateAvailable`. If the `.cht` on disk hashes differently
  from its sidecar (user edited it), the section says "edited locally" and `Update` replaces it.

### 4. Fetch flow

`CheatStore` (app/src/cheat_store.{h,cpp}), no SDL/ImGui/GL, two injected hooks so the whole thing runs
synchronously in tests: `fetch(url, maxBytes, body, err) -> bool` (default `http::get`, status != 200 is an
error) and `run(job)` (default `app.cheatNet.post` on a `Worker` owned by `App`, created in `cheatsInit`;
tests run the job inline). Results cross back under a mutex and `pump()` (called from `drawCheats`, main
thread) applies them: writes the cache files, updates `PackState`, asks `CheatManager::reloadCommunity()`.
`stop()` sets the cancel flag and joins (app shutdown).

- Manifest fetch: when the Cheats window becomes visible with a cartridge loaded AND downloads are on AND
  `http::available()` AND `now - cheats.community.checked >= 6 h`; and on the "Check for updates" button
  (no interval). Never at app start and never from the library/scan code. Install/Update fetch the pack using
  the cached manifest's sha256; a server-side change between the two shows as "pack changed on the server:
  check for updates again", never as a silent install.
- Limits: manifest 256 KiB, pack 64 KiB (`maxBytes`, data §9); http.cpp's own 10 s connect / 20 s stall /
  120 s total; `format != 1` or a pack entry missing `crc32|file|sha256|bytes` is "store manifest not
  understood (format N)", entries with a bad CRC/sha hex are skipped individually. `file` must be a relative
  path with no `..`; resolved against `kCheatStoreBase` (`https://jaguar.provenance-emu.com/cheats/`, config
  override `cheats.community.url` for the loopback test server, like `library.art_mirror`).
- Errors are a status line inside the section (`CheatStore::status()`, same idea as `LibraryService::status()`),
  never a toast: "Could not reach the cheat store: <error>. Installed packs still work." A failed manifest fetch
  does not touch the cached manifest or the check time (so the next window open retries).
- Downloads off (`library.art` false): no fetch; the section shows the installed pack if any, otherwise
  "Downloads are off (Library: Download art and cheat packs). Turn it on to see community packs." The Library
  checkbox label becomes "Download art and cheat packs"; the key stays `library.art`.
- Hardcore: install/update/remove are data-only and stay enabled; toggles sit inside the existing
  `BeginDisabled(hardcore)` and `blocked()` keeps `apply()` empty. The section shows one line: "Installed; cheats
  cannot be enabled in hardcore mode."

### 5. UI (cheats_ui.cpp, `drawCommunity`)

Position: after "Database cheats", before "Manual codes", header "Community cheats". `drawSetCheats` becomes
`drawCheatRows(cm, set, Origin)` and both sections use it (checkbox + wrapped label + tooltip = code/note).

| State | Shown |
|---|---|
| no game | section absent (existing "Load a cartridge or disc" text) |
| disc (crc == 0) | "Community packs cover cartridges only." |
| downloads off, nothing installed | the note from §4 |
| no manifest yet / fetching | "Checking the community store..." (spinner text), button disabled |
| not offered, nothing installed | "No community pack for this cartridge (CRC 5E2CDBC0)." + Check for updates |
| offered, not installed | title, "N cheats (M writes) by <credit>, updated <date>", partial note, **Install** |
| installed, current | credit line, cheat rows, partial notes, **Remove**, Check for updates |
| update available | same + warning "Update available (pack changed <date>)", **Update** first, Remove |
| no longer offered | rows + "No longer offered by the store; installed copy kept", Remove |
| offline / error | rows (if installed) + the status line |

Always under the header when a pack is known: credit as `TextDisabled` wrapped (the manifest's pre-rendered
string); `notes[]` as a collapsed `TreeNode("N settings could not be converted")` listing `setting: reason`;
`exceeds_core_limit` as a warning "Enabling every cheat here exceeds the core's 256-write limit; the first 256
writes win" (the existing red line still reports the actual count). Hardcore line per §4. Buttons go through
`ui::Flow`. Library-wide affordance: **v1 is the loaded game only**. The manifest is small, so nothing stops a
later "games in your library with a pack" list on the Library tab, but it needs tile plumbing and a second
`CheatStore` consumer; the issue's wording "(or the loaded game)" allows deferring it, and the site's
`cheats/index.html` already lists every pack.

### 6. Tests (app/tests, no network)

`vj_cheats_test` gains `src/cheat_store.cpp` + `src/sha256.cpp` + `src/neuro/neuro_json.cpp`; fixtures
`fixtures/community/manifest.json` and `fixtures/community/5E2CDBC0.cht` (guard = the Test Game CRC used in
`testRevisionGuard`). Fetcher is a `std::map<url, body>` lambda; `run` executes inline.
- `testSha256`: FIPS vectors ("", "abc", the 448-bit block) and a > 64 KiB buffer.
- `testManifestParse`: good; `format 2`; a pack with a bad sha hex is skipped and the rest kept; truncated JSON;
  oversize body (fetcher reports "response too large") -> error status, cache untouched.
- `testInstallVerify`: sha mismatch, byte-count mismatch, `vj_rom_crc32` for another CRC, missing guard, a
  body with no cheats, HTTP 404: each leaves no `.cht`/`.json` and sets the matching status; success writes both
  and `communitySet()` has the cheats, all off.
- `testCommunityToggles`: enable two, "restart" (new manager) -> still on; install an updated fixture where one
  cheat's code changed -> that one off, the other on, stale `cp.` key gone; `Remove` -> ini has no `cp.` keys.
- `testApplyOrder`: db set + pack + user code all on -> batch is `reset`, db entries, `set 5000+i`, `set 10000+i`;
  `appliedSegments()` is the sum; the warn fires above 256.
- `testStates`: offered/not installed, installed/current, update available (manifest sha changed), dropped from
  manifest, disc identity (crc 0) never loads or fetches, `library.art` off never calls the fetcher, a hardcore
  `blocked` returning true still installs but `apply()` sends only `reset`.
- `testNoManifestRetryGate`: fetch within 6 h is skipped; "Check for updates" forces it.

### 7. Files

| File | Change | ~Lines |
|---|---|---|
| app/src/sha256.{h,cpp} | new, FIPS 180-4, one function | 120 |
| app/src/cheat_store.{h,cpp} | new: manifest/PackMeta parse, verify, paths, states, store service with `fetch`/`run` hooks, `pump`, `status` | 450 |
| app/src/cheats.{h,cpp} | Origin, community set + guard, `cp.` prefix, prune, apply order, `loadChtText`, `reloadCommunity` | +130 |
| app/src/cheats_core.cpp | `App::cheatNet` Worker, store wiring (dirs, base URL override, downloads flag, hooks), stop on shutdown | +40 |
| app/src/cheats_ui.cpp | `drawCommunity`, `drawCheatRows` refactor | +160 |
| app/src/app.h / app.cpp | `CheatStore cheatStore; Worker cheatNet;` + stop | +10 |
| app/src/library_ui.cpp | checkbox label | 1 |
| app/cmake/VJCheatTests.cmake, app/CMakeLists.txt | sources, fixtures | +10 |
| app/tests/cheats_test.cpp + fixtures/community/* | §6 | +300 |

### 8. Docs

- `site/pages/app.html`: the Cheats bullet and gallery caption mention community packs (install, credits,
  off by default, update, cartridges only); a short paragraph under Cheats.
- `site/pages/privacy.html`: the row the data design specifies (service: this project's website; when:
  opening the Cheats window with a cartridge loaded and downloads on, at most every 6 h, or on Check for
  updates / Install; sent: a request for the pack index and the pack named by the cartridge's CRC32, nothing
  else); the local-files list gains `cheats/community/`.
- `tools/cheat-import/README.md`: one cross-link paragraph: packs built by CI are served by the store and
  installed from the Cheats window; manual `.cht` use stays as documented.
- `docs/WHATSNEW` line; `docs/app-debug-tools.md` unchanged.

### 9. Risks

- SHA-256 is new code in the trust path: pinned by test vectors, and a wrong hash fails closed (no install).
- A pack beyond 256 enabled writes starves user codes, which come last; the manifest warning plus the
  existing count line are the mitigation, not a fix.
- The one downloads switch now also gates cheats; a user who turned art off for bandwidth loses the store
  silently unless the section's note is read.
- Stale cached manifest after the CI bump: at most 6 h plus the window-open gate; acceptable for cheats.
- Hand-copied or edited files under `packs/` bypass the store's verification but not the CRC guard.
