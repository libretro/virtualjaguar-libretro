# Site maintenance

The project website (<https://www.libretro.com/virtualjaguar-libretro/>) is a
static site generated from **committed repository data** — no CMS, no external
dependencies, no hand-maintained compatibility tables.

## How the automation works

- **Sources** live in `site/`:
  - `site/pages/*.html` — page body fragments. Each starts with
    `<!-- title: ... -->` and `<!-- nav: ... -->` comments; the generator wraps
    them in the shared layout (header, nav, footer) and substitutes
    placeholders (`{{REPO}}`, `{{COMPAT_TABLE}}`, …).
  - `site/style.css` — the one stylesheet (system fonts, light/dark via
    `prefers-color-scheme`, no external assets of any kind).
  - `site/assets/` — images, copied verbatim (the true-color A/B screenshots
    live here).
- **Generator**: `scripts/build_site.py` — python3 **stdlib only** (no pip).
  It renders everything into `_site/` (gitignored).
- **Deploy**: `.github/workflows/pages.yml` runs on every push to `develop`
  that touches `site/**`, `scripts/build_site.py`, `docs/cd-boot-matrix.md`,
  or the workflow itself (plus manual `workflow_dispatch`). It builds `_site/`
  and publishes via `actions/configure-pages` → `upload-pages-artifact` →
  `deploy-pages`. Pages is configured with `build_type=workflow`.

## Where the compatibility data comes from

The CD table on the compatibility page is **parsed at build time** from
`docs/cd-boot-matrix.md` (the first table under its `## Results` heading —
later tables in that doc are historical and ignored). The flow is:

```
test/tools/cd_boot_matrix.sh    (run locally against the private disc corpus)
        → docs/cd-boot-matrix.md   (commit the updated Results table)
        → push to develop          (pages.yml rebuilds and redeploys the site)
```

So updating the site's compatibility data is just: re-run the matrix script,
commit the doc, push. Nothing on the site is typed in by hand.

### Parser strictness (deliberate)

`build_site.py` **fails the workflow loudly** — red run, no deploy — if the
matrix format drifts: missing `## Results` heading, changed column header,
wrong cell count in a row, or an unknown `Mode`. Tolerated in-format noise:

- `<!-- build:<rev> -->` stamps inside cells are extracted (shown on the page
  as the corpus revision) and stripped from the rendered output.
- Unknown `Stage` strings don't fail the build; they render honestly in the
  "not yet verified" bucket. `GAME_CODE` renders as verified; `LOAD_FAIL`,
  `HARNESS_HANG`, and `? (pc_escape …)` render as known issues.

If you change the matrix format on purpose, update `EXPECTED_HEADER` /
`classify_stage()` in `scripts/build_site.py` in the same commit.

## Local preview (one command)

```bash
python3 scripts/build_site.py && python3 -m http.server -d _site 8080
```

Then open <http://localhost:8080/>. The build itself takes well under a second.

## Editing guidelines (honesty guardrails)

These are hard rules, inherited from how the site was built:

- **Every claim links to evidence**: a committed doc, a merged PR, or the
  validation text of an open PR. Open-PR features must be labeled
  "in review"; roadmap items "in design".
- **No invented compatibility rows** — the table only renders what
  `docs/cd-boot-matrix.md` contains. Cartridge highlights must cite release
  notes or `docs/cart-issue-triage.md`.
- **No unverifiable superlatives** ("best", "fastest"). Real numbers with
  sources are the hype.
- **No claims about other emulators' internals.** The single comparative line
  on the why-this-core page is phrased as current knowledge with an explicit
  invitation to correct it in Discussions.
- Screenshots must be real captures produced by the test tooling, presented
  with honest framing (e.g. the true-color difference panel states its 64×
  amplification).
