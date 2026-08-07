#!/usr/bin/env python3
"""Static-site generator for the Virtual Jaguar libretro GitHub Pages site.

Stdlib only -- no pip, no external JS/CSS/fonts/trackers.

Inputs (committed in this repo):
  site/pages/*.html        page body fragments (title/nav in leading comments)
  site/style.css           the one stylesheet
  site/assets/*            images, copied verbatim
  docs/cd-boot-matrix.md   parsed for the CD compatibility table

Output:
  _site/                   ready to serve (used by .github/workflows/pages.yml)

Usage:
  python3 scripts/build_site.py            # build into _site/
  python3 scripts/build_site.py --out DIR  # build elsewhere

The CD-matrix parser is deliberately strict: if docs/cd-boot-matrix.md drifts
from the expected shape, the build FAILS with a loud message instead of
publishing garbage.  Tolerated in-format noise: `<!-- build:<rev> -->` stamps
inside cells (extracted, reported, stripped) and unknown Stage strings (mapped
to the honest "not yet verified" bucket).
"""

import argparse
import html
import re
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SITE_SRC = ROOT / "site"
MATRIX_MD = ROOT / "docs" / "cd-boot-matrix.md"

REPO_URL = "https://github.com/libretro/virtualjaguar-libretro"

# Page order defines nav order.
PAGES = ["index.html", "compatibility.html", "enhancements.html",
         "why-this-core.html"]

EXPECTED_HEADER = ["Title", "Mode", "Score", "Stage", "Watchdog", "PC evidence"]
BUILD_STAMP_RE = re.compile(r"<!--\s*build:([^\s>]+)\s*-->")


def die(msg):
    sys.stderr.write("\nFATAL: build_site.py: %s\n" % msg)
    sys.stderr.write("Refusing to publish a site with bad or missing data.\n")
    sys.exit(1)


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


# ---------------------------------------------------------------- pages

META_RE = re.compile(r"<!--\s*(title|nav):\s*(.*?)\s*-->")


def read_fragment(path):
    text = path.read_text(encoding="utf-8")
    meta = dict(META_RE.findall(text))
    if "title" not in meta or "nav" not in meta:
        die("%s is missing a '<!-- title: ... -->' or '<!-- nav: ... -->' "
            "comment" % path)
    body = META_RE.sub("", text).strip()
    return meta, body


def layout(page_name, meta, body, nav_items):
    nav = []
    for fname, label in nav_items:
        cls = ' class="active"' if fname == page_name else ""
        nav.append('<a href="%s"%s>%s</a>' % (fname, cls, html.escape(label)))
    return """<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta name="color-scheme" content="light dark">
<title>%(title)s</title>
<link rel="stylesheet" href="style.css">
</head>
<body>
<header class="site-header"><div class="inner">
  <a class="site-title" href="index.html">Virtual <span class="jag">Jaguar</span> libretro</a>
  <nav class="site-nav">%(nav)s</nav>
</div></header>
<main>
%(body)s
</main>
<footer class="site-footer"><div class="inner">
  Virtual Jaguar libretro &mdash; GPLv3 &mdash;
  <a href="%(repo)s">source on GitHub</a> &middot;
  <a href="%(repo)s/discussions">Discussions</a> &middot;
  <a href="%(repo)s/issues">Issues</a>.
  This site is generated from committed repository data by
  <a href="%(repo)s/blob/develop/scripts/build_site.py">scripts/build_site.py</a>;
  every claim links to its evidence.
</div></footer>
</body>
</html>
""" % {"title": html.escape(meta["title"]), "nav": "\n".join(nav),
       "body": body, "repo": REPO_URL}


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--out", default=str(ROOT / "_site"),
                    help="output directory (default: _site/)")
    args = ap.parse_args()
    out = Path(args.out)

    for p in PAGES:
        if not (SITE_SRC / "pages" / p).is_file():
            die("missing page fragment site/pages/%s" % p)

    # Parse compatibility data first: a broken matrix must fail the build.
    rows, build_ids = parse_cd_matrix(MATRIX_MD)
    table_html, n_titles, n_good = render_cd_table(rows)
    print("cd-boot-matrix: %d rows, %d disc images, build stamp(s): %s"
          % (len(rows), n_titles, ", ".join(build_ids) or "(none)"))

    subs = {
        "{{REPO}}": REPO_URL,
        "{{COMPAT_TABLE}}": table_html,
        "{{COMPAT_BUILD_IDS}}":
            ", ".join("<code>%s</code>" % html.escape(b) for b in build_ids)
            or "no build stamp found",
        "{{COMPAT_N_TITLES}}": str(n_titles),
        "{{COMPAT_N_HLE_GOOD}}": str(n_good["hle"]),
        "{{COMPAT_N_BIOS_GOOD}}": str(n_good["bios"]),
    }

    # Fresh output dir.
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)

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

    for p in PAGES:
        meta, body = frags[p]
        for k, v in subs.items():
            body = body.replace(k, v)
        leftover = re.search(r"\{\{[A-Z_]+\}\}", body)
        if leftover:
            die("unresolved placeholder %s in site/pages/%s"
                % (leftover.group(0), p))
        (out / p).write_text(layout(p, meta, body, nav_items),
                             encoding="utf-8")
        print("wrote %s" % (out / p))

    print("site built: %s" % out)


if __name__ == "__main__":
    main()
