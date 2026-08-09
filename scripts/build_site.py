#!/usr/bin/env python3
"""Static-site generator for the Virtual Jaguar libretro GitHub Pages site.

Stdlib only -- no pip, no external JS/CSS/fonts/trackers.

Inputs (committed in this repo):
  site/pages/*.html        page body fragments (title/nav/description in
                           leading comments)
  site/style.css           the one stylesheet
  site/assets/*            images, copied verbatim
  docs/cd-boot-matrix.md   parsed for the CD compatibility table
  Makefile                 CORE_BASE_VERSION -> JSON-LD softwareVersion

Output:
  _site/                   ready to serve (used by .github/workflows/pages.yml)
  _site/sitemap.xml        one <url> per page, lastmod from git
  _site/robots.txt         advisory (see render_robots for the caveat)

Usage:
  python3 scripts/build_site.py            # build into _site/
  python3 scripts/build_site.py --out DIR  # build elsewhere
  python3 scripts/build_site.py --build-date YYYY-MM-DD   # pin the fallback

The CD-matrix parser is deliberately strict: if docs/cd-boot-matrix.md drifts
from the expected shape, the build FAILS with a loud message instead of
publishing garbage.  Tolerated in-format noise: `<!-- build:<rev> -->` stamps
inside cells (extracted, reported, stripped) and unknown Stage strings (mapped
to the honest "not yet verified" bucket).

SEO surface (all generated here, never hand-written into a page fragment):
per-page <title>/description, rel=canonical, Open Graph + Twitter card,
JSON-LD (SoftwareApplication on the home page, BreadcrumbList on subpages),
sitemap.xml and robots.txt.  No trackers, no analytics, no external assets --
that is a hard rule, see docs/site-maintenance.md.
"""

import argparse
import datetime
import html
import json
import re
import shutil
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SITE_SRC = ROOT / "site"
MATRIX_MD = ROOT / "docs" / "cd-boot-matrix.md"
CART_MATRIX_MD = ROOT / "docs" / "cart-boot-matrix.md"
MAKEFILE = ROOT / "Makefile"

REPO_URL = "https://github.com/libretro/virtualjaguar-libretro"

# The canonical base for every absolute URL the site emits; for a given page
# the <link rel=canonical>, og:url and the sitemap <loc> must come out
# byte-identical -- a mismatch between those three is the classic own-goal.
#
# Default: jaguar.provenance-emu.com, GitHub Pages on the Provenance-Emu fork
# with a verified custom domain.  The libretro org's own Pages routing sends
# this repo's site to www.libretro.com/virtualjaguar-libretro/, which that
# host does not serve (404) and only a libretro org owner can fix; if that
# ever changes, migrate by flipping VJ_SITE_BASE.  Keep the trailing slash.
import os as _os
SITE_BASE = _os.environ.get(
    "VJ_SITE_BASE", "https://jaguar.provenance-emu.com/")
if not SITE_BASE.endswith("/"):
    SITE_BASE += "/"
SITE_NAME = "Virtual Jaguar libretro"

# Official libretro documentation for this core: the canonical reference
# manual, maintained in libretro/docs as docs/library/virtual_jaguar.md.
# This site is the project showcase; that page is the manual.
LIBRETRO_DOCS = "https://docs.libretro.com/library/virtual_jaguar/"
LIBRETRO_DOCS_OPTIONS = LIBRETRO_DOCS + "#core-options"
LIBRETRO_DOCS_CONTROLS = LIBRETRO_DOCS + "#controllers"
LIBRETRO_DOCS_SRC = ("https://github.com/libretro/docs/blob/master/"
                     "docs/library/virtual_jaguar.md")
# libretro's own community-maintained Jaguar compatibility list -- a different
# document from our generated CD boot matrix; both are linked, labelled.
LIBRETRO_DOCS_COMPAT = "https://docs.libretro.com/library/compatibility/jaguar/"

# Default social share image.  Dimensions are read from the file itself
# (png_size) so og:image:width/height can never drift from reality.
SHARE_IMAGE = "assets/truecolor_ab_cybermorph.png"
SHARE_IMAGE_ALT = (
    "Three-panel comparison of Cybermorph running in the Virtual Jaguar "
    "libretro core: stock 16-bit CRY output, true-color output, and a "
    "64x-amplified difference map.")

# Page order defines nav order.
PAGES = ["index.html", "compatibility.html", "enhancements.html",
         "why-this-core.html"]

EXPECTED_HEADER = ["Title", "Mode", "Score", "Stage", "Watchdog", "PC evidence"]
CART_EXPECTED_HEADER = ["Title", "HLE", "HLE notes", "Real BIOS", "BIOS notes"]
BUILD_STAMP_RE = re.compile(r"<!--\s*build:([^\s>]+)\s*-->")
ISO_DATE_RE = re.compile(r"^\d{4}-\d{2}-\d{2}$")


def die(msg):
    sys.stderr.write("\nFATAL: build_site.py: %s\n" % msg)
    sys.stderr.write("Refusing to publish a site with bad or missing data.\n")
    sys.exit(1)


# ------------------------------------------------------- repo-derived facts

def read_core_version(makefile):
    """softwareVersion for the JSON-LD, straight from the Makefile.

    Pulled from the build system rather than typed into the site so a release
    bump can't leave stale structured data behind.  Fails loudly, like the
    matrix parser: publishing a wrong version is worse than not publishing.
    """
    if not makefile.is_file():
        die("%s not found -- cannot determine the core version" % makefile)
    m = re.search(r"^CORE_BASE_VERSION\s*:=\s*v?([0-9][0-9A-Za-z.\-]*)\s*$",
                  makefile.read_text(encoding="utf-8"), re.M)
    if not m:
        die("no 'CORE_BASE_VERSION := vX.Y.Z' line in %s -- structured data "
            "would publish a stale or missing version" % makefile)
    return m.group(1)


def png_size(path):
    """(width, height) from a PNG IHDR, so og:image dimensions can't lie."""
    if not path.is_file():
        die("share image %s is missing" % path)
    data = path.read_bytes()[:24]
    if len(data) < 24 or data[:8] != b"\x89PNG\r\n\x1a\n":
        die("%s is not a PNG -- refusing to guess og:image dimensions" % path)
    return struct.unpack(">II", data[16:24])


def git_output(args):
    """Run git at ROOT; stripped stdout, or None on ANY failure.

    Never fatal: a source tarball, a missing git binary and a detached CI
    checkout must all still produce a site.
    """
    try:
        proc = subprocess.run(["git", "-C", str(ROOT)] + args,
                              stdout=subprocess.PIPE,
                              stderr=subprocess.DEVNULL, timeout=15)
    except (OSError, subprocess.SubprocessError):
        return None
    if proc.returncode != 0:
        return None
    return proc.stdout.decode("utf-8", "replace").strip()


def repo_is_shallow():
    return git_output(["rev-parse", "--is-shallow-repository"]) == "true"


def git_commit_date(path):
    """YYYY-MM-DD of the last commit touching `path`, or None."""
    out = git_output(["log", "-1", "--format=%cs", "--", str(path)])
    # `%cs` needs git >= 2.21; an older git echoes the literal format string,
    # which the regex rejects -- so we degrade to the fallback date instead of
    # emitting '%cs' as a lastmod.
    if not out or not ISO_DATE_RE.match(out):
        return None
    return out


def last_modified(paths, fallback, shallow):
    """Newest git commit date across `paths`, else `fallback`.

    A page's real lastmod depends on more than its own fragment: every page
    depends on this generator's template, and the compatibility page is
    generated from docs/cd-boot-matrix.md.  Callers pass all of them.

    On a SHALLOW clone (`actions/checkout` defaults to fetch-depth: 1) `git
    log -1 -- <path>` reports the single checkout commit for *every* path, so
    all pages would get the same wrong date while looking perfect locally.
    We refuse that data outright; .github/workflows/pages.yml sets
    fetch-depth: 0 so the real dates are available in CI.
    """
    if shallow:
        return fallback
    best = None
    for p in paths:
        d = git_commit_date(p)
        if d and (best is None or d > best):
            best = d
    return best or fallback


# ---------------------------------------------------------------- CD matrix

def split_md_row(line):
    """Split a markdown table row into cells (outer pipes optional)."""
    line = line.strip()
    if line.startswith("|"):
        line = line[1:]
    if line.endswith("|"):
        line = line[:-1]
    return [c.strip() for c in line.split("|")]


def parse_cd_matrix(path):
    """Parse the current Results table from docs/cd-boot-matrix.md.

    Returns (rows, build_ids) where rows is a list of dicts with keys
    title/mode/score/stage/watchdog/evidence, and build_ids is the sorted set
    of build stamps found in the table.

    Only the FIRST table after the '## Results' heading is the current data;
    later sections of the doc contain historical tables that must be ignored.
    """
    if not path.is_file():
        die("%s not found -- the compatibility page has no data source" % path)
    lines = path.read_text(encoding="utf-8").splitlines()

    # Locate the '## Results' heading.
    start = None
    for i, ln in enumerate(lines):
        if ln.strip() == "## Results":
            start = i + 1
            break
    if start is None:
        die("no '## Results' heading in %s -- format drifted?" % path)

    # Find the first table after it, stopping at the next heading.
    hdr_idx = None
    for i in range(start, len(lines)):
        s = lines[i].strip()
        if s.startswith("#"):
            die("hit heading %r before any table under '## Results' in %s"
                % (s, path))
        if s.startswith("|"):
            hdr_idx = i
            break
    if hdr_idx is None:
        die("no table found under '## Results' in %s" % path)

    header = split_md_row(lines[hdr_idx])
    if header != EXPECTED_HEADER:
        die("Results table header changed in %s.\n  expected: %s\n  found:    %s"
            % (path, EXPECTED_HEADER, header))

    sep = lines[hdr_idx + 1].strip() if hdr_idx + 1 < len(lines) else ""
    if not re.fullmatch(r"\|?[\s:|-]+\|?", sep):
        die("missing separator row after Results header in %s (got %r)"
            % (path, sep))

    rows = []
    build_ids = set()
    for i in range(hdr_idx + 2, len(lines)):
        s = lines[i].strip()
        if not s.startswith("|"):
            break  # end of the current table
        cells = split_md_row(lines[i])
        if len(cells) != len(EXPECTED_HEADER):
            die("row %d of the Results table in %s has %d cells, expected %d:\n  %r"
                % (i + 1, path, len(cells), len(EXPECTED_HEADER), lines[i]))
        for m in BUILD_STAMP_RE.finditer(lines[i]):
            build_ids.add(m.group(1))
        cells = [BUILD_STAMP_RE.sub("", c).strip() for c in cells]
        title, mode, score, stage, watchdog, evidence = cells
        if mode not in ("hle", "bios"):
            die("row %d in %s: unknown Mode %r (expected 'hle' or 'bios')"
                % (i + 1, path, mode))
        if not title:
            die("row %d in %s: empty Title" % (i + 1, path))
        rows.append({"title": title, "mode": mode, "score": score,
                     "stage": stage, "watchdog": watchdog,
                     "evidence": evidence})
    if not rows:
        die("Results table in %s has zero data rows" % path)
    return rows, sorted(build_ids)


def classify_stage(stage):
    """Map a Stage cell to (css_class, human_label). Honest three-state."""
    s = stage.strip()
    if s == "GAME_CODE":
        return ("good", "Reaches game code")
    if s in ("LOAD_FAIL", "HARNESS_HANG") or s.startswith("? (pc_escape"):
        return ("bad", "Known issue: " + s)
    # BIOS_INTRO / BOOT_STUB / '? (...)' / anything new: not verified either way.
    return ("warn", "Not yet verified: " + s)


def summarize_watchdog(watchdog):
    """Short label for the watchdog cell; full text goes into a title attr."""
    w = watchdog.strip()
    if w in ("", "(none)"):
        return None
    m = re.search(r"\b(cd_seek_wedge|video_stall|gpu_pc_escape|dsp_pc_escape|"
                  r"gpu_wedge|dsp_wedge)\b", w)
    return m.group(1) if m else "watchdog note"


def render_cd_table(rows):
    """Group hle/bios rows per title into one table row each."""
    order = []
    by_title = {}
    for r in rows:
        if r["title"] not in by_title:
            by_title[r["title"]] = {}
            order.append(r["title"])
        by_title[r["title"]][r["mode"]] = r

    out = []
    out.append('<div class="table-wrap"><table>')
    out.append("<tr><th>Disc image</th><th>HLE mode</th><th>Real-BIOS mode</th>"
               "<th>Notes</th></tr>")
    n_good = {"hle": 0, "bios": 0}
    for title in order:
        cells = ['<td>%s</td>' % html.escape(title)]
        notes = []
        for mode in ("hle", "bios"):
            r = by_title[title].get(mode)
            if r is None:
                cells.append('<td><span class="badge warn">not run</span></td>')
                continue
            cls, label = classify_stage(r["stage"])
            if cls == "good":
                n_good[mode] += 1
            cells.append('<td><span class="badge %s">%s</span></td>'
                         % (cls, html.escape(label)))
            wd = summarize_watchdog(r["watchdog"])
            if wd:
                notes.append('<span title="%s"><code>%s</code> (%s)</span>'
                             % (html.escape(r["watchdog"], quote=True),
                                html.escape(wd), mode))
        cells.append("<td>%s</td>" % (" &middot; ".join(notes) if notes
                                      else "&mdash;"))
        out.append("<tr>%s</tr>" % "".join(cells))
    out.append("</table></div>")
    return "\n".join(out), len(order), n_good


def parse_cart_matrix(path):
    """Parse the single table in docs/cart-boot-matrix.md.

    Returns (rows, build_ids) where rows is a list of dicts with keys
    title/hle_stage/hle_notes/bios_stage/bios_notes.  Deliberately strict,
    like parse_cd_matrix: if the generator's format drifts, fail the site
    build rather than render half a table.
    """
    if not path.is_file():
        die("%s not found -- run test/tools/cart_boot_matrix.sh" % path)
    lines = path.read_text(encoding="utf-8").splitlines()

    hdr_idx = None
    for i, ln in enumerate(lines):
        if ln.strip().startswith("|"):
            hdr_idx = i
            break
    if hdr_idx is None:
        die("no table found in %s" % path)
    header = split_md_row(lines[hdr_idx])
    if header != CART_EXPECTED_HEADER:
        die("cart matrix header changed in %s.\n  expected: %s\n  found:    %s"
            % (path, CART_EXPECTED_HEADER, header))
    sep = lines[hdr_idx + 1].strip() if hdr_idx + 1 < len(lines) else ""
    if not re.fullmatch(r"\|?[\s:|-]+\|?", sep):
        die("missing separator row after cart matrix header in %s (got %r)"
            % (path, sep))

    rows = []
    build_ids = set()
    for i in range(hdr_idx + 2, len(lines)):
        s = lines[i].strip()
        if not s.startswith("|"):
            break
        for m in BUILD_STAMP_RE.finditer(lines[i]):
            build_ids.add(m.group(1))
        cells = split_md_row(BUILD_STAMP_RE.sub("", lines[i]).strip())
        if len(cells) != len(CART_EXPECTED_HEADER):
            die("row %d of %s has %d cells, expected %d:\n  %r"
                % (i + 1, path, len(cells), len(CART_EXPECTED_HEADER),
                   lines[i]))
        title, hle_stage, hle_notes, bios_stage, bios_notes = \
            [c.strip() for c in cells]
        if not title:
            die("row %d in %s: empty Title" % (i + 1, path))
        rows.append({"title": title,
                     "hle_stage": hle_stage, "hle_notes": hle_notes,
                     "bios_stage": bios_stage, "bios_notes": bios_notes})
    if not rows:
        die("cart matrix in %s has zero data rows" % path)
    return rows, sorted(build_ids)


def classify_cart_stage(stage, notes):
    """Map a cart stage + notes to (css_class, human_label).

    Same honest three-state as the CD table.  'GAME_CODE' with a
    black-video note downgrades to warn: the title executes but headless
    video evidence is inconclusive (headless read-path caveat)."""
    s = stage.strip()
    if s == "GAME_CODE":
        if "black video" in notes:
            return ("warn", "Runs; video undetermined headlessly")
        return ("good", "Boots and runs")
    if (s == "LOAD_FAIL" or s.startswith("? (pc_escape") or
            s.startswith("? (timeout") or
            s.startswith("? (build_mismatch") or
            s.startswith("? (no_reg")):
        return ("bad", "Known issue: " + s)
    return ("warn", "Not yet verified: " + s)


def render_cart_table(rows):
    """One table row per title; badge per boot mode; merged notes cell."""
    out = []
    out.append('<div class="table-wrap"><table>')
    out.append("<tr><th>Cartridge</th><th>HLE boot</th><th>Real-BIOS boot</th>"
               "<th>Notes</th></tr>")
    n_good = {"hle": 0, "bios": 0}
    for r in rows:
        cells = ['<td>%s</td>' % html.escape(r["title"])]
        notes = []
        for mode, stage_k, notes_k in (("hle", "hle_stage", "hle_notes"),
                                       ("bios", "bios_stage", "bios_notes")):
            cls, label = classify_cart_stage(r[stage_k], r[notes_k])
            if cls == "good":
                n_good[mode] += 1
            cells.append('<td><span class="badge %s" title="%s">%s</span></td>'
                         % (cls, html.escape(r[notes_k], quote=True),
                            html.escape(label)))
            wd = summarize_watchdog(r[notes_k])
            if wd:
                notes.append('<span title="%s"><code>%s</code> (%s)</span>'
                             % (html.escape(r[notes_k], quote=True),
                                html.escape(wd), mode))
        cells.append("<td>%s</td>" % (" &middot; ".join(notes) if notes
                                      else "&mdash;"))
        out.append("<tr>%s</tr>" % "".join(cells))
    out.append("</table></div>")
    return "\n".join(out), len(rows), n_good


# ---------------------------------------------------------------- pages

META_RE = re.compile(r"<!--\s*(title|nav|description):\s*(.*?)\s*-->")
REQUIRED_META = ("title", "nav", "description")


def read_fragment(path):
    text = path.read_text(encoding="utf-8")
    meta = dict(META_RE.findall(text))
    for key in REQUIRED_META:
        if not meta.get(key):
            die("%s is missing a non-empty '<!-- %s: ... -->' comment"
                % (path, key))
    # Search engines truncate around 160 characters; 200 leaves slack while
    # still catching "someone pasted a paragraph in here".
    if len(meta["description"]) > 200:
        die("%s: description is %d characters (max 200) -- it would be "
            "truncated in results:\n  %r"
            % (path, len(meta["description"]), meta["description"]))
    body = META_RE.sub("", text).strip()
    return meta, body


# ------------------------------------------------------------ SEO / metadata

def page_url(page_name):
    """Absolute, canonical URL for a page.  Single source of truth for
    rel=canonical, og:url and the sitemap <loc> -- they must not diverge."""
    if page_name == "index.html":
        return SITE_BASE
    return SITE_BASE + page_name


def page_href(page_name):
    """Relative link to a page, matching page_url()'s choice of form.

    The home page is reachable both as `.../` and `.../index.html`; the
    canonical is the directory form, so every internal link must use `./` --
    otherwise the site's own links tell crawlers the home page is the URL
    rel=canonical says it isn't.  check_site.py asserts no href is
    "index.html".
    """
    if page_name == "index.html":
        return "./"
    return page_name


def json_ld(obj):
    """Serialize structured data from a dict, so it is valid JSON by
    construction.  '<' is escaped: no payload can close the <script>."""
    return json.dumps(obj, indent=2, ensure_ascii=False).replace("<", "\\u003c")


def software_application_ld(version, share_url, description):
    """Describes the core itself.  Deliberately omits aggregateRating and
    review: we have no such data, and inventing it is out of the question."""
    return {
        "@context": "https://schema.org",
        "@type": "SoftwareApplication",
        "name": SITE_NAME,
        "alternateName": "virtualjaguar_libretro",
        "description": description,
        "applicationCategory": "GameApplication",
        "applicationSubCategory": "Emulator",
        # Exactly the targets .github/workflows/release.yml builds.
        "operatingSystem": ["Linux", "macOS", "Windows", "Android", "iOS",
                            "tvOS", "Web browser (WebAssembly)"],
        "softwareVersion": version,
        "license": "https://www.gnu.org/licenses/gpl-3.0.html",
        "isAccessibleForFree": True,
        "programmingLanguage": "C",
        "url": SITE_BASE,
        "image": share_url,
        "codeRepository": REPO_URL,
        "downloadUrl": REPO_URL + "/releases",
        "softwareHelp": {
            "@type": "CreativeWork",
            "name": "Virtual Jaguar core documentation (docs.libretro.com)",
            "url": LIBRETRO_DOCS,
        },
    }


def breadcrumb_ld(page_name, label):
    return {
        "@context": "https://schema.org",
        "@type": "BreadcrumbList",
        "itemListElement": [
            {"@type": "ListItem", "position": 1, "name": SITE_NAME,
             "item": SITE_BASE},
            {"@type": "ListItem", "position": 2, "name": label,
             "item": page_url(page_name)},
        ],
    }


def head_meta(page_name, meta, ctx):
    """The whole per-page <head> SEO block, generated -- never hand-written
    into a fragment, so the tags can't drift apart page to page."""
    url = page_url(page_name)
    title = meta["title"]
    desc = meta["description"]
    esc = lambda s: html.escape(s, quote=True)
    out = [
        '<title>%s</title>' % html.escape(title),
        '<meta name="description" content="%s">' % esc(desc),
        '<link rel="canonical" href="%s">' % esc(url),
        '<meta property="og:type" content="website">',
        '<meta property="og:site_name" content="%s">' % esc(SITE_NAME),
        '<meta property="og:locale" content="en_US">',
        '<meta property="og:title" content="%s">' % esc(title),
        '<meta property="og:description" content="%s">' % esc(desc),
        '<meta property="og:url" content="%s">' % esc(url),
        '<meta property="og:image" content="%s">' % esc(ctx["share_url"]),
        '<meta property="og:image:width" content="%d">' % ctx["share_w"],
        '<meta property="og:image:height" content="%d">' % ctx["share_h"],
        '<meta property="og:image:alt" content="%s">' % esc(SHARE_IMAGE_ALT),
        '<meta name="twitter:card" content="summary_large_image">',
        '<meta name="twitter:title" content="%s">' % esc(title),
        '<meta name="twitter:description" content="%s">' % esc(desc),
        '<meta name="twitter:image" content="%s">' % esc(ctx["share_url"]),
        '<meta name="twitter:image:alt" content="%s">' % esc(SHARE_IMAGE_ALT),
    ]
    if page_name == "index.html":
        data = software_application_ld(ctx["version"], ctx["share_url"], desc)
    else:
        data = breadcrumb_ld(page_name, meta["nav"])
    out.append('<script type="application/ld+json">\n%s\n</script>'
               % json_ld(data))
    return "\n".join(out)


def layout(page_name, meta, body, nav_items, ctx):
    nav = []
    for fname, label in nav_items:
        cls = ' class="active"' if fname == page_name else ""
        nav.append('<a href="%s"%s>%s</a>'
                   % (page_href(fname), cls, html.escape(label)))
    # NOTE: the site title is an <a>, not a heading, on purpose -- every page
    # must have exactly one <h1> and it belongs to the page content.
    return """<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta name="color-scheme" content="light dark">
%(headmeta)s
<link rel="stylesheet" href="style.css">
</head>
<body>
<header class="site-header"><div class="inner">
  <a class="site-title" href="./">Virtual <span class="jag">Jaguar</span> libretro</a>
  <nav class="site-nav">%(nav)s</nav>
</div></header>
<main>
%(body)s
</main>
<footer class="site-footer"><div class="inner">
  <p>
    Virtual Jaguar libretro &mdash; GPLv3 &mdash;
    <a href="%(repo)s">source on GitHub</a> &middot;
    <a href="%(repo)s#readme">README</a> &middot;
    <a href="%(repo)s/releases">releases</a> &middot;
    <a href="%(repo)s/releases/tag/nightly">nightly build</a> &middot;
    <a href="%(repo)s/discussions">Discussions</a> &middot;
    <a href="%(repo)s/issues">Issues</a>.
  </p>
  <p>
    Core options, controls and file extensions are documented in the
    <a href="%(docs)s">official libretro documentation for this core</a> on
    docs.libretro.com &mdash; that page is the reference manual; this site is
    the project showcase.
  </p>
  <p>
    This site is generated from committed repository data by
    <a href="%(repo)s/blob/develop/scripts/build_site.py">scripts/build_site.py</a>;
    every claim links to its evidence.
  </p>
</div></footer>
</body>
</html>
""" % {"headmeta": head_meta(page_name, meta, ctx), "nav": "\n".join(nav),
       "body": body, "repo": REPO_URL, "docs": LIBRETRO_DOCS}


# ------------------------------------------------------ sitemap  /  robots

def render_sitemap(entries):
    """entries: [(absolute url, YYYY-MM-DD), ...]"""
    out = ['<?xml version="1.0" encoding="UTF-8"?>',
           '<urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9">']
    for url, lastmod in entries:
        out.append("  <url>")
        out.append("    <loc>%s</loc>" % html.escape(url))
        out.append("    <lastmod>%s</lastmod>" % lastmod)
        out.append("  </url>")
    out.append("</urlset>")
    return "\n".join(out) + "\n"


def render_robots():
    """Honest about its own reach: robots.txt is only obeyed at a domain
    root, and this file is published under a subdirectory of a domain this
    repository does not own.  It is emitted anyway because it costs nothing
    and documents the sitemap location; discovery of the sitemap relies on
    the absolute Sitemap: line below, not on this file being authoritative.
    """
    return (
        "# Virtual Jaguar libretro -- generated by scripts/build_site.py.\n"
        "# robots.txt is only honoured at a domain root; this copy lives at\n"
        "# " + SITE_BASE + "robots.txt, so treat it as documentation of the\n"
        "# sitemap location rather than as this host's policy file.\n"
        "User-agent: *\n"
        "Allow: /\n"
        "\n"
        "Sitemap: " + SITE_BASE + "sitemap.xml\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--out", default=str(ROOT / "_site"),
                    help="output directory (default: _site/)")
    ap.add_argument("--build-date", default=None, metavar="YYYY-MM-DD",
                    help="sitemap lastmod fallback when git history is "
                         "unavailable (default: today, UTC)")
    args = ap.parse_args()
    out = Path(args.out)

    # Computed at run time, never baked into the source.
    build_date = args.build_date or datetime.datetime.now(
        datetime.timezone.utc).strftime("%Y-%m-%d")
    if not ISO_DATE_RE.match(build_date):
        die("--build-date must be YYYY-MM-DD, got %r" % build_date)

    for p in PAGES:
        if not (SITE_SRC / "pages" / p).is_file():
            die("missing page fragment site/pages/%s" % p)

    # Parse compatibility data first: a broken matrix must fail the build.
    rows, build_ids = parse_cd_matrix(MATRIX_MD)
    table_html, n_titles, n_good = render_cd_table(rows)
    print("cd-boot-matrix: %d rows, %d disc images, build stamp(s): %s"
          % (len(rows), n_titles, ", ".join(build_ids) or "(none)"))

    cart_rows, cart_build_ids = parse_cart_matrix(CART_MATRIX_MD)
    cart_table_html, cart_n_titles, cart_n_good = render_cart_table(cart_rows)
    print("cart-boot-matrix: %d titles, build stamp(s): %s"
          % (cart_n_titles, ", ".join(cart_build_ids) or "(none)"))

    version = read_core_version(MAKEFILE)
    share_path = SITE_SRC / SHARE_IMAGE
    share_w, share_h = png_size(share_path)
    shallow = repo_is_shallow()
    ctx = {"version": version,
           "share_url": SITE_BASE + SHARE_IMAGE,
           "share_w": share_w, "share_h": share_h}
    print("core version: %s | share image: %dx%d | lastmod source: %s"
          % (version, share_w, share_h,
             "build date %s (shallow clone or no git)" % build_date
             if shallow else "git log (fallback %s)" % build_date))

    subs = {
        "{{REPO}}": REPO_URL,
        "{{CORE_VERSION}}": html.escape(version),
        "{{DOCS_URL}}": LIBRETRO_DOCS,
        "{{DOCS_OPTIONS_URL}}": LIBRETRO_DOCS_OPTIONS,
        "{{DOCS_CONTROLS_URL}}": LIBRETRO_DOCS_CONTROLS,
        "{{DOCS_COMPAT_URL}}": LIBRETRO_DOCS_COMPAT,
        "{{DOCS_SRC_URL}}": LIBRETRO_DOCS_SRC,
        "{{COMPAT_TABLE}}": table_html,
        "{{COMPAT_BUILD_IDS}}":
            ", ".join("<code>%s</code>" % html.escape(b) for b in build_ids)
            or "no build stamp found",
        "{{COMPAT_N_TITLES}}": str(n_titles),
        "{{COMPAT_N_HLE_GOOD}}": str(n_good["hle"]),
        "{{COMPAT_N_BIOS_GOOD}}": str(n_good["bios"]),
        "{{CART_TABLE}}": cart_table_html,
        "{{CART_BUILD_IDS}}":
            ", ".join("<code>%s</code>" % html.escape(b)
                      for b in cart_build_ids) or "no build stamp found",
        "{{CART_N_TITLES}}": str(cart_n_titles),
        "{{CART_N_HLE_GOOD}}": str(cart_n_good["hle"]),
        "{{CART_N_BIOS_GOOD}}": str(cart_n_good["bios"]),
    }

    # Fresh output dir.  Only ever recursively delete a directory this
    # script produced: a stray --out (a typo like `--out ..`, or a
    # misconfigured workflow) must not wipe unrelated files.  The marker
    # is written on every successful build below.
    marker = out / ".vj-generated-site"
    if out.exists():
        if not out.is_dir():
            raise SystemExit("--out exists and is not a directory: %s" % out)
        if any(out.iterdir()) and not marker.exists():
            raise SystemExit(
                "refusing to clear non-empty %s: no %s marker, so this "
                "directory was not produced by this script.  Remove it by "
                "hand or build into a different --out." % (out, marker.name))
        shutil.rmtree(out)
    out.mkdir(parents=True)
    marker.write_text("Generated by scripts/build_site.py. Safe to delete.\n")

    # Static bits.
    shutil.copy2(SITE_SRC / "style.css", out / "style.css")
    assets_src = SITE_SRC / "assets"
    if assets_src.is_dir():
        shutil.copytree(assets_src, out / "assets")
    (out / ".nojekyll").write_text("")

    nav_items = []
    frags = {}
    for p in PAGES:
        meta, body = read_fragment(SITE_SRC / "pages" / p)
        nav_items.append((p, meta["nav"]))
        frags[p] = (meta, body)

    # Every page's rendered output depends on the generator's template too,
    # and the compatibility page additionally on the parsed matrix doc.
    common_deps = [Path(__file__).resolve(), SITE_SRC / "style.css"]
    sitemap_entries = []

    for p in PAGES:
        meta, body = frags[p]
        for k, v in subs.items():
            body = body.replace(k, v)
        leftover = re.search(r"\{\{[A-Z_]+\}\}", body)
        if leftover:
            die("unresolved placeholder %s in site/pages/%s"
                % (leftover.group(0), p))
        page = layout(p, meta, body, nav_items, ctx)
        leftover = re.search(r"\{\{[A-Z_]+\}\}", page)
        if leftover:
            die("unresolved placeholder %s in the rendered %s (template?)"
                % (leftover.group(0), p))
        (out / p).write_text(page, encoding="utf-8")

        deps = [SITE_SRC / "pages" / p] + common_deps
        if p == "compatibility.html":
            deps.append(MATRIX_MD)
        sitemap_entries.append(
            (page_url(p), last_modified(deps, build_date, shallow)))
        print("wrote %s" % (out / p))

    (out / "sitemap.xml").write_text(render_sitemap(sitemap_entries),
                                     encoding="utf-8")
    (out / "robots.txt").write_text(render_robots(), encoding="utf-8")
    print("wrote %s (%d urls) and %s"
          % (out / "sitemap.xml", len(sitemap_entries), out / "robots.txt"))

    print("site built: %s" % out)


if __name__ == "__main__":
    main()
