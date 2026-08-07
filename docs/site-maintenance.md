# Site maintenance

The project website (<https://jaguar.provenance-emu.com/>) is a static site
generated from **committed repository data** — no CMS, no external
dependencies, no hand-maintained compatibility tables.

## Where the site is hosted, and why

The site is GitHub Pages on the **Provenance-Emu fork** with the verified
custom domain `jaguar.provenance-emu.com` (DNS: a `CNAME` to
`libretro.github.io`, proxy disabled so GitHub can see it and issue the
certificate).

It is not served from the `libretro` org repo because that org's Pages
configuration redirects every project site to
`www.libretro.com/<repo>/`, a host libretro serves elsewhere and which
returns 404 for these paths. Only a libretro org owner can change that.
If it is ever fixed, migrating is one variable: `SITE_BASE` in
`scripts/build_site.py`, overridable at build time with `VJ_SITE_BASE`.
Every absolute URL the site emits -- canonical, og:url, sitemap loc,
robots -- derives from that single string, so they cannot drift apart.


## How the automation works

- **Sources** live in `site/`:
  - `site/pages/*.html` — page body fragments. Each starts with
    `<!-- title: ... -->`, `<!-- nav: ... -->` and `<!-- description: ... -->`
    comments; the generator wraps them in the shared layout (header, nav,
    footer), builds the whole SEO head from that metadata, and substitutes
    placeholders (`{{REPO}}`, `{{COMPAT_TABLE}}`, `{{DOCS_URL}}`, …).
    All three comments are **required** — a missing one, or a description over
    200 characters, fails the build.
  - `site/style.css` — the one stylesheet (system fonts, light/dark via
    `prefers-color-scheme`, no external assets of any kind).
  - `site/assets/` — images, copied verbatim (the true-color A/B screenshots
    live here). `assets/truecolor_ab_cybermorph.png` is the default social
    share image; its `og:image:width`/`height` are read from the PNG's own
    IHDR at build time, so they can't drift.
- **Generator**: `scripts/build_site.py` — python3 **stdlib only** (no pip).
  It renders everything into `_site/` (gitignored).
- **Output check**: `scripts/check_site.py _site` — also stdlib only. The
  generator fails on bad *input*; this fails on bad *output* and runs in CI
  between the build and the deploy, so nothing publishes if it trips. See
  "SEO and structured data" below for what it enforces.
- **Deploy**: `.github/workflows/pages.yml` runs on every push to `develop`
  that touches `site/**`, `scripts/build_site.py`, `docs/cd-boot-matrix.md`,
  the `Makefile` (it supplies the version to the structured data), or the
  workflow itself (plus manual `workflow_dispatch`). It builds `_site/`
  and publishes via `actions/configure-pages` → `upload-pages-artifact` →
  `deploy-pages`. Pages is configured with `build_type=workflow`.
  The checkout uses `fetch-depth: 0` **on purpose** — see "sitemap" below.

### Placeholders available in a page fragment

| Placeholder | Expands to |
|---|---|
| `{{REPO}}` | the GitHub repository URL |
| `{{CORE_VERSION}}` | `CORE_BASE_VERSION` from the `Makefile`, without the `v` |
| `{{DOCS_URL}}` | official libretro docs page for this core |
| `{{DOCS_OPTIONS_URL}}` / `{{DOCS_CONTROLS_URL}}` | its core-options / controllers anchors |
| `{{DOCS_SRC_URL}}` | the Markdown source of that page in `libretro/docs` |
| `{{DOCS_COMPAT_URL}}` | libretro's community Jaguar compatibility list |
| `{{COMPAT_TABLE}}`, `{{COMPAT_BUILD_IDS}}`, `{{COMPAT_N_TITLES}}`, `{{COMPAT_N_HLE_GOOD}}`, `{{COMPAT_N_BIOS_GOOD}}` | parsed from `docs/cd-boot-matrix.md` |

An unresolved `{{PLACEHOLDER}}` fails the build — checked both in the fragment
and in the fully rendered page, so a typo in the template is caught too.

## SEO and structured data

All of this is generated in `scripts/build_site.py`; **never hand-write meta
tags into a page fragment** — that is how tags drift apart page to page.

- Per-page `<title>` and `<meta name="description">` come from the fragment's
  metadata comments. Subpage titles read `Page — Virtual Jaguar libretro`.
- `<link rel="canonical">`, `og:url` and the sitemap `<loc>` are all built from
  `page_url()` so they are byte-identical per page. The home page canonical is
  the directory form (`…/virtualjaguar-libretro/`), subpages are
  `…/virtualjaguar-libretro/<page>.html`.
- **Link the home page as `./`, never `index.html`.** Pages serves it at both
  URLs; the canonical is the directory form, so an internal `index.html` link
  would point crawlers at the duplicate the canonical rejects. `page_href()`
  handles the header and nav; `check_site.py` fails the build if any `href` in
  the output is `index.html`.
- Open Graph + Twitter (`summary_large_image`) tags, with an absolute
  `og:image` pointing at the true-color A/B composite.
- JSON-LD: `SoftwareApplication` on the home page (version pulled from the
  `Makefile`, GPL-3.0, `isAccessibleForFree`, the platform list that
  `release.yml` actually builds) and `BreadcrumbList` on each subpage. Emitted
  via `json.dumps()` of a dict, so it is valid JSON by construction.
  **No `aggregateRating`/`review`** — we have no such data and will not invent
  any.
- **sitemap.xml**: one `<url>` per page. `lastmod` is the newest
  `git log -1 --format=%cs` across the page's fragment, `style.css`,
  `build_site.py`, and (for the compatibility page) `docs/cd-boot-matrix.md`.
  On a **shallow clone** `git log -1 -- <path>` returns the checkout commit for
  every path, which would stamp every page with the same wrong date — the
  generator detects that (`git rev-parse --is-shallow-repository`) and falls
  back to the build date, and `pages.yml` sets `fetch-depth: 0` so CI has the
  real history. Missing git, an old git without `%cs`, or a source tarball all
  degrade to the same fallback; none of them fail the build. Override the
  fallback with `--build-date YYYY-MM-DD` (default: today, UTC — never
  hardcoded in the script).
- **robots.txt** is emitted with an absolute `Sitemap:` line. Be aware it is
  only advisory here: `robots.txt` is honoured at a *domain root*, and this
  site lives under a subdirectory of a domain this repository does not control.
  Treat it as documentation of the sitemap location, not as this host's policy
  file, and don't expect the sitemap to be auto-discovered from it.
- Semantic rules the generator upholds: `lang="en"`, exactly **one `<h1>` per
  page** (the site title in the header is an `<a>`, not a heading — leave it
  that way), and descriptive `alt` on every image.
- Still zero trackers, zero analytics, zero external assets *at runtime*.
  The only `<script>` allowed on a page is `type="application/ld+json"` or a
  **deferred `<script src>` pointing at a local file under
  `site/assets/js/`** that exists in the output. Inline non-JSON-LD scripts
  and external `src` remain hard failures.
- **Committed** site JS must be original code written for this repository
  (GPLv3) — never vendor third-party effect libraries into the repo: the
  Canvas UI Commons Clause forbids redistributing ported components, and
  mixing that license into a GPLv3 tree is a problem on its own
  (`site/assets/js/crt-fx.js` and `hero-fx.js` are ours).
- Third-party effects run only via **deploy-time fetch**:
  `scripts/fetch_site_fx.py`, gated behind `VJ_SITE_FX_FETCH=1`, downloads
  the pinned Canvas UI components, compiles them with esbuild, embeds the
  upstream license text as a header (the license permits use "as part of a
  website" and requires the notice), writes
  `_site/assets/js/canvas-ui-fx.js` and injects its tag.  The source never
  enters git; the served file is first-party.  ANY failure of that script
  (flag unset, network, npx, compile) leaves the site on the committed
  fallback — local builds stay offline and a broken fetch cannot blank the
  hero or fail a deploy.
- Effects are progressive enhancement only: the page must be complete and
  readable with JS disabled; every failure path must leave the real DOM
  content visible (fail closed — no class flips before a successful init).
  `prefers-reduced-motion` gates the *animation* only — engines still render
  one static styled frame.

`scripts/check_site.py` enforces all of the above against the built output:
one `<h1>`, `lang="en"`, descriptive `alt` on every image, every local
`href`/`src` resolving, no leftover `{{PLACEHOLDER}}`, canonical == `og:url`
== sitemap `<loc>`, the full Open Graph / Twitter set with an absolute image
that actually exists, JSON-LD that parses with the expected `@type` and no
fabricated rating data, distinct titles and descriptions, a well-formed
sitemap listing exactly the generated pages, and no tracker or external-asset
reference. Add a check there when you add a guarantee here.

## Official libretro documentation (link-only, on purpose)

The canonical reference manual for this core is
<https://docs.libretro.com/library/virtual_jaguar/>, generated from
`docs/library/virtual_jaguar.md` in the [`libretro/docs`](https://github.com/libretro/docs)
repository. The site links to it from the footer of every page, from the home
page, from the compatibility page and from the enhancements page, framed as
*their page is the manual, this site is the project showcase*.

We deliberately **link rather than vendor** that Markdown:

- a local `python3 scripts/build_site.py` must never touch the network;
- vendored copies need attribution plus a sync/provenance mechanism, and go
  stale silently between syncs;
- the upstream page is a living document whose canonical home should receive
  the traffic and the corrections.

If it is ever mirrored instead, it must carry attribution and a
"synced from libretro/docs at `<commit>`" provenance line, and the fetch must
be non-fatal and off by default.

Note that the upstream page is maintained separately from this repository and
can lag recent releases (its frontend-feature table is a known example). Don't
"fix" our site to match it — send a PR to `libretro/docs` instead.

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
python3 scripts/build_site.py && python3 scripts/check_site.py _site \
  && python3 -m http.server -d _site 8080
```

Then open <http://localhost:8080/>. The build itself takes well under a second,
and neither step touches the network.

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
