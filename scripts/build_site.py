#!/usr/bin/env python3
"""Static-site generator for the Virtual Jaguar libretro GitHub Pages site.

Stdlib only -- no pip, no external JS/CSS/fonts/trackers.

Inputs (committed in this repo):
  site/pages/*.html        page body fragments (title/nav/description in
                           leading comments)
  site/style.css           the one stylesheet
  site/assets/*            images, copied verbatim
  site/thumbnails/*        Jaguar CD artwork mirror, copied verbatim
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
# raw.githubusercontent for the install one-liner; derived from REPO_URL so a
# fork or rename cannot leave the curl command pointing at the wrong repo.
RAW_URL = REPO_URL.replace("https://github.com/",
                           "https://raw.githubusercontent.com/") + "/develop"

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

# The Provenance family of emulator sites this showcase is published
# alongside (jaguar.provenance-emu.com is hosted by Provenance).  Source of
# truth: provenance-emu.github.io/data/family.yml.  Keep in sync by hand.
PROVENANCE_ORG_ID = "https://provenance-emu.com/#organization"
FAMILY = [
    ("Provenance", "https://provenance-emu.com/",
     "Multi-system retro emulator for iOS, tvOS and macOS"),
    ("iCube", "https://icube-emu.com/",
     "GameCube & Wii emulator for iOS and tvOS"),
    ("iFly", "https://ifly-emu.com/",
     "Dreamcast emulator for iOS and tvOS"),
    ("Provenance Wiki", "https://wiki.provenance-emu.com/",
     "Guides, ROM ripping, BIOS help"),
]


def family_links_html():
    """Footer links to the sibling Provenance sites, one per line, with the
    tagline as the title attribute so the footer stays one short paragraph."""
    return " &middot;\n".join(
        '    <a href="%s" title="%s">%s</a>' % (url, html.escape(tagline), name)
        for name, url, tagline in FAMILY)
LIBRETRO_DOCS_CONTROLS = LIBRETRO_DOCS + "#controllers"
LIBRETRO_DOCS_SRC = ("https://github.com/libretro/docs/blob/master/"
                     "docs/library/virtual_jaguar.md")
# libretro's own community-maintained Jaguar compatibility list -- a different
# document from our generated CD boot matrix; both are linked, labelled.
LIBRETRO_DOCS_COMPAT = "https://docs.libretro.com/library/compatibility/jaguar/"

# Social share cards (og:image / twitter:image), per page, with a default.
# The PNGs are generated by scripts/make_share_cards.py and committed.
# Dimensions are read from each file (png_size) so og:image:width/height can
# never drift from reality.
SHARE_DEFAULT = (
    "assets/share/home.png",
    "Virtual Jaguar: eight Atari Jaguar games running in the emulator (Doom, "
    "Raiden, Tempest 2000, Super Burnout, Iron Soldier, Wolfenstein 3D, Alien "
    "vs Predator and Atari Karts) above the Virtual Jaguar name and app icon.")
SHARE_IMAGES = {
    "app.html": (
        "assets/share/app.png",
        "The Virtual Jaguar desktop app: the cartridge library with box art, "
        "Rayman under a CRT shader, the palette debugger and the light theme, "
        "above the Virtual Jaguar name and app icon."),
}

# Favicon set, generated by scripts/make_share_cards.py from the app icon.
# favicon.ico is also copied to the output root, where browsers ask for it
# unprompted.
ICON_DIR = "assets/icons/"
ICON_PNG_SIZES = (192, 512)

# Page order defines nav order.
PAGES = ["index.html", "download.html", "app.html", "compatibility.html",
         "enhancements.html", "performance.html", "why-this-core.html"]
# Legal pages: built, in the sitemap and checked like any other page, but kept
# out of the primary nav -- they are linked from the footer instead.
LEGAL_PAGES = ["privacy.html", "terms.html"]
ALL_PAGES = PAGES + LEGAL_PAGES

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

DASH_RE = re.compile("[—–]")


def normalize_dashes(s):
    """Em/en dash -> hyphen, for text arriving from the generated matrices.

    The site's copy standard is zero em-dashes, and it has to hold for cells
    the sweep tools wrote as much as for prose someone typed: cart_boot_matrix
    emits notes like "black video (headless -- undetermined)", which reach the
    reader through a title= tooltip.  Normalizing here rather than editing
    docs/cart-boot-matrix.md keeps that file exactly as its generator wrote it
    -- the compatibility page's whole claim is that nothing on it was typed by
    hand.  This changes punctuation only; no cell's meaning moves.
    """
    return DASH_RE.sub("-", s)


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
        cells = [normalize_dashes(BUILD_STAMP_RE.sub("", c).strip())
                 for c in cells]
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
        # No em-dash anywhere the site renders: a plain hyphen is what a
        # 1994 printout put in an empty cell, and it is the character the
        # copy standard on every page uses.
        cells.append("<td>%s</td>" % (" &middot; ".join(notes) if notes
                                      else "-"))
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
            [normalize_dashes(c.strip()) for c in cells]
        if not title:
            die("row %d in %s: empty Title" % (i + 1, path))
        for st in (hle_stage, bios_stage):
            # Infrastructure-invalid rows (build-guard refusal, missing PC
            # evidence) are sweep failures, not title results -- rendering
            # them at all would publish "Known issue" for titles that are
            # fine.  Fail the build so the sweep gets fixed and re-run.
            if st.startswith("? (build_mismatch") or st.startswith("? (no_reg"):
                die("row %d in %s has invalid-infrastructure stage %r -- "
                    "re-run cart_boot_matrix.sh against a matching wide-ABI "
                    "core (make TEST_EXPORTS=1)" % (i + 1, path, st))
        rows.append({"title": title,
                     "hle_stage": hle_stage, "hle_notes": hle_notes,
                     "bios_stage": bios_stage, "bios_notes": bios_notes})
    if not rows:
        die("cart matrix in %s has zero data rows" % path)
    return rows, sorted(build_ids)


def classify_cart_stage(stage, notes, mode="hle"):
    """Map a cart stage + notes to (css_class, human_label).

    Same honest three-state as the CD table, plus 'plain' for an
    authentic outcome that is neither a pass nor a bug (#976).

    * 'GAME_CODE' with a black-video note downgrades to warn: the title
      executes but headless video evidence is inconclusive (headless
      read-path caveat).
    * In the real-BIOS column, a run whose notes never mention the
      handoff had no boot phase at all (cart_classify.sh: bios_ran=0, a
      RAM-loaded executable), so the cell says the boot ROM was not used
      rather than implying it accepted the file.
    * 'BIOS_REJECT' is the real boot ROM's red "Jaguar" reject screen
      (#852/#744): an observed, authentic outcome, never "not verified".
    * '? (trap_loop)' / '? (bios_trap)' are crashes (#975)."""
    s = stage.strip()
    if s == "GAME_CODE":
        no_boot_rom = mode == "bios" and "handoff" not in notes
        if "black video" in notes:
            return ("warn", "Boot ROM not used; runs, no picture in the test"
                    if no_boot_rom else
                    "Runs; no picture in the test")
        return ("good", "Boot ROM not used; runs" if no_boot_rom
                else "Boots and runs")
    if s == "BIOS_REJECT":
        return ("plain", "Rejected by the boot ROM")
    if s.startswith("? (trap_loop"):
        return ("bad", "Known issue: crashes at start")
    if s.startswith("? (bios_trap"):
        return ("bad", "Known issue: crashes after the boot ROM")
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
    n_good = {"hle": 0, "bios": 0, "hle_run": 0, "bios_run": 0}
    for r in rows:
        cells = ['<td>%s</td>' % html.escape(r["title"])]
        notes = []
        for mode, stage_k, notes_k in (("hle", "hle_stage", "hle_notes"),
                                       ("bios", "bios_stage", "bios_notes")):
            cls, label = classify_cart_stage(r[stage_k], r[notes_k], mode)
            if cls == "good":
                n_good[mode] += 1
            # "Reaches game code" regardless of the headless video verdict.
            if r[stage_k].strip() == "GAME_CODE":
                n_good[mode + "_run"] += 1
            cells.append('<td><span class="badge %s" title="%s">%s</span></td>'
                         % (cls, html.escape(r[notes_k], quote=True),
                            html.escape(label)))
            wd = summarize_watchdog(r[notes_k])
            if wd:
                notes.append('<span title="%s"><code>%s</code> (%s)</span>'
                             % (html.escape(r[notes_k], quote=True),
                                html.escape(wd), mode))
        # No em-dash anywhere the site renders: a plain hyphen is what a
        # 1994 printout put in an empty cell, and it is the character the
        # copy standard on every page uses.
        cells.append("<td>%s</td>" % (" &middot; ".join(notes) if notes
                                      else "-"))
        out.append("<tr>%s</tr>" % "".join(cells))
    out.append("</table></div>")
    return "\n".join(out), len(rows), n_good


# ------------------------------------------------------------- downloads

# Release data is fetched by the Pages workflow (GitHub API: releases/latest,
# releases/tags/nightly and the last few tagged releases) and passed in with
# --releases.  It is never committed: the asset list changes with every
# release and the file names of the app packages carry the version.  Without
# the file the Download page still builds, with links to the release pages
# instead of to the files.  scripts/testdata/releases.json is a trimmed
# fixture of the same shape for offline builds and tests.
#
# Every release is rendered to its own panel here, in Python, from one
# classification table, so the platform/arch grouping exists exactly once.
# assets/js/download.js only reveals the panel the reader picks (the page
# without it shows the latest release); it never rebuilds markup.

APP_ASSET_RE = re.compile(
    r"^(?:VirtualJaguar-[^/]+\.(?:zip|exe|AppImage|flatpak)|virtualjaguar_[^/]+\.deb)$")
CORE_ASSET_RE = re.compile(
    r"^virtualjaguar_libretro-(.+)\.(so|dll|dylib|bc|a)$")
DEBUG_ASSET_RE = re.compile(r"^virtualjaguar_libretro-(.+)-debug\.tar\.gz$")
TOOL_ASSET_RE = re.compile(
    r"^(jagcd-tools|cue2cdi)-(linux|macos|windows)(?:-([\w.]+?))?(?:\.zip|\.exe)?$")
SUMS_NAME = "SHA256SUMS.txt"
INFO_NAME = "virtualjaguar_libretro.info"

# Platform id -> (label, icon id).  Order is display order for the core;
# the app and the tools list follow the same order.  The icons are generic
# glyphs (a laptop, a window, a prompt, a board ...) on purpose: no vendor
# logo is redistributable here.  The name is always printed beside the icon,
# so the icon itself is decoration (aria-hidden).
PLATFORMS = [
    ("macos", "macOS", "laptop"),
    ("windows", "Windows", "window"),
    ("linux", "Linux", "terminal"),
    ("rpi", "Raspberry Pi", "board"),
    ("android", "Android", "phone"),
    ("ios", "iOS and tvOS", "tablet"),
    ("web", "Web (Emscripten)", "globe"),
    ("handheld", "Switch and Vita", "handheld"),
    ("other", "Other", "box"),
]
PLATFORM = dict((k, (label, icon)) for k, label, icon in PLATFORMS)
# Groups opened by default; the long tail stays one line each until asked.
PLATFORMS_OPEN = ("macos", "windows", "linux")

# How an app package reads on the page: (file-name regex, platform, arch
# label, package label).  Order is display order.
APP_KINDS = [
    (r"-macos-universal\.zip$", "macos", "universal", "App (zip)"),
    (r"-windows-x64-setup\.exe$", "windows", "x64", "Installer"),
    (r"-windows-x64\.zip$", "windows", "x64", "Portable zip"),
    (r"-windows-arm64-setup\.exe$", "windows", "arm64", "Installer"),
    (r"-windows-arm64\.zip$", "windows", "arm64", "Portable zip"),
    (r"-linux-x86_64\.AppImage$", "linux", "x86_64", "AppImage"),
    (r"_amd64\.deb$", "linux", "x86_64", ".deb"),
    (r"-linux-x86_64\.flatpak$", "linux", "x86_64", "Flatpak"),
    (r"-linux-aarch64\.AppImage$", "linux", "aarch64", "AppImage"),
    (r"_arm64\.deb$", "linux", "aarch64", ".deb"),
]

TOOL_BLURB = {
    "jagcd-tools": "session-aware chdman for Jaguar CD images",
    "cue2cdi": "converts CUE/BIN to CDI",
}


def arch_icon(arch):
    """Icon id for an architecture or core-target string."""
    a = arch.lower()
    if a == "universal":
        return "layers"
    if a == "wasm":
        return "cube"
    if re.search(r"arm|aarch|rpi|cortex", a):
        return "chip-arm"
    return "chip"


def core_platform(target):
    """(platform id, short target) for the part of a core file name between
    'virtualjaguar_libretro-' and the extension."""
    if target.startswith("linux-rpi"):
        return "rpi", target[len("linux-"):]
    for prefix, plat in (("windows-", "windows"), ("macos-", "macos"),
                         ("linux-", "linux"), ("android-", "android"),
                         ("emscripten-", "web")):
        if target.startswith(prefix):
            return plat, target[len(prefix):]
    if target.startswith("ios-"):
        return "ios", "iOS " + target[4:]
    if target.startswith("tvos-"):
        return "ios", "tvOS " + target[5:]
    if target in ("switch", "vita"):
        return "handheld", target
    return "other", target


def load_releases(path):
    """{"latest": <release>, "nightly": <release>, "older": [<release>...]}
    in the GitHub API shape.  Every key is optional; a present release must
    look like one.  "older" is newest first."""
    try:
        data = json.loads(Path(path).read_text(encoding="utf-8"))
    except (OSError, ValueError) as e:
        die("--releases %s: %s" % (path, e))
    if not isinstance(data, dict):
        die("--releases %s: expected an object with latest/nightly" % path)
    rels = [(k, data.get(k)) for k in ("latest", "nightly")]
    rels += [("older[%d]" % i, r) for i, r in enumerate(data.get("older") or [])]
    for key, rel in rels:
        if rel is None:
            continue
        if not (isinstance(rel, dict) and rel.get("tag_name")
                and isinstance(rel.get("assets"), list)):
            die("--releases %s: %r is not a GitHub release object" % (path, key))
        for a in rel["assets"]:
            if not (a.get("name") and a.get("browser_download_url")):
                die("--releases %s: %r has an asset without name/url"
                    % (path, key))
    return data


GITHUB_API = "https://api.github.com/repos/" + REPO_URL[len("https://github.com/"):]
OLDER_RELEASES = 10     # how many releases before the latest to offer


def fetch_releases():
    """The same {"latest", "nightly", "older"} shape as --releases, straight
    from the GitHub API.  Unauthenticated is fine (three requests); GH_TOKEN
    or GITHUB_TOKEN is sent when set, which CI should do.  Any failure is
    fatal: a publisher that cannot fetch must keep the previous deployment
    rather than publish a Download page without the links."""
    import urllib.request
    import urllib.error
    token = _os.environ.get("GH_TOKEN") or _os.environ.get("GITHUB_TOKEN")

    def get(path):
        req = urllib.request.Request(
            "%s/%s" % (GITHUB_API, path),
            headers={"Accept": "application/vnd.github+json",
                     "User-Agent": "virtualjaguar-build_site",
                     **({"Authorization": "Bearer " + token} if token else {})})
        try:
            with urllib.request.urlopen(req, timeout=30) as resp:
                return json.loads(resp.read().decode("utf-8"))
        except (urllib.error.URLError, ValueError, OSError) as e:
            die("--releases-from-github: GET %s failed: %s" % (path, e))

    out = {"latest": get("releases/latest"),
           "nightly": get("releases/tags/nightly")}
    listing = get("releases?per_page=30")
    for key, rel in out.items():
        if not (rel.get("tag_name") and isinstance(rel.get("assets"), list)):
            die("--releases-from-github: %s is not a release object" % key)
    if not isinstance(listing, list):
        die("--releases-from-github: releases listing is not a list")
    # Tagged releases only: drafts, the rolling nightly and the rolling
    # release-candidate prerelease are all marked draft/prerelease.
    out["older"] = [
        r for r in listing
        if not r.get("draft") and not r.get("prerelease")
        and r.get("tag_name") and r["tag_name"] != out["latest"]["tag_name"]
        and isinstance(r.get("assets"), list)][:OLDER_RELEASES]
    return out


def _size(n):
    try:
        n = int(n)
    except (TypeError, ValueError):
        return ""
    if n >= 1048576:
        return "%.1f MB" % (n / 1048576.0)
    return "%d KB" % max(1, round(n / 1024.0))


def _esc(s):
    return html.escape(s, quote=True)


def _icon(icon_id):
    return ('<svg class="ico" aria-hidden="true" focusable="false">'
            '<use href="#i-%s"></use></svg>' % icon_id)


def _plat_cell(plat):
    label, icon = PLATFORM[plat]
    return '<span class="plat-name">%s%s</span>' % (_icon(icon), _esc(label))


def _arch_cell(arch):
    return '<span class="arch">%s%s</span>' % (_icon(arch_icon(arch)),
                                                _esc(arch))


def _dl(asset, text, cls=""):
    """A download link with the file name as its tooltip."""
    return '<a%s href="%s" title="%s">%s</a>' % (
        (' class="%s"' % cls) if cls else "",
        _esc(asset["browser_download_url"]), _esc(asset["name"]), text)


def classify_assets(assets):
    """Sort a release's assets into the page's four lists.  Every asset
    lands in exactly one list; nothing is dropped.
      app:   [(platform, arch, label, asset)]
      core:  [(platform, target, ext, asset)]
      debug: [(platform, target, asset)]
      tools: [(tool, platform, arch, asset)]
      other: [asset]   (SHA256SUMS.txt and the .info file are returned by
                        name, not listed here)
    """
    app, core, debug, tools, other = [], [], [], [], []
    sums = info = None
    seen = set()
    for pat, plat, arch, label in APP_KINDS:
        for a in assets:
            if (APP_ASSET_RE.match(a["name"]) and a["name"] not in seen
                    and re.search(pat, a["name"])):
                app.append((plat, arch, label, a))
                seen.add(a["name"])
    for a in assets:
        n = a["name"]
        if n in seen:
            continue
        m_dbg = DEBUG_ASSET_RE.match(n)
        m_core = CORE_ASSET_RE.match(n)
        m_tool = TOOL_ASSET_RE.match(n)
        if n == SUMS_NAME:
            sums = a
        elif n == INFO_NAME:
            info = a
        elif m_dbg:
            # Keep the full id (linux-x86_64): out of its platform group a
            # bare "x86_64" would not say which build the symbols belong to.
            debug.append((core_platform(m_dbg.group(1))[0], m_dbg.group(1), a))
        elif m_core:
            plat, tgt = core_platform(m_core.group(1))
            core.append((plat, tgt, "." + m_core.group(2), a))
        elif m_tool:
            tools.append((m_tool.group(1), m_tool.group(2),
                          m_tool.group(3) or "", a))
        elif APP_ASSET_RE.match(n):
            app.append(("other", "", n, a))     # a package we have no row for
        else:
            other.append(a)
    return {"app": app, "core": core, "debug": debug, "tools": tools,
            "other": other, "sums": sums, "info": info}


def _plat_order(plat):
    return [k for k, _l, _i in PLATFORMS].index(plat)


def _app_table(rows):
    """Platform / Arch / Download, one row per (platform, arch)."""
    out = ['<div class="table-wrap"><table class="dl-table">',
           "<thead><tr><th scope=\"col\">Platform</th>"
           "<th scope=\"col\">Arch</th>"
           "<th scope=\"col\">Download</th></tr></thead>"]
    by_plat = []
    for plat, arch, label, a in rows:
        if not by_plat or by_plat[-1][0] != plat:
            by_plat.append((plat, []))
        grp = by_plat[-1][1]
        if not grp or grp[-1][0] != arch:
            grp.append((arch, []))
        grp[-1][1].append((label, a))
    for plat, archs in by_plat:
        out.append("<tbody>")
        for i, (arch, files) in enumerate(archs):
            cells = []
            if i == 0:
                cells.append('<th scope="rowgroup" rowspan="%d">%s</th>'
                             % (len(archs), _plat_cell(plat)))
            cells.append("<td>%s</td>" % (_arch_cell(arch) if arch else ""))
            cells.append('<td class="files">%s</td>' % " ".join(
                '<span class="file">%s<small>%s</small></span>'
                % (_dl(a, _esc(label)), _esc(_size(a.get("size"))))
                for label, a in files))
            out.append("<tr>%s</tr>" % "".join(cells))
        out.append("</tbody>")
    out.append("</table></div>")
    return "\n".join(out)


def _chip(asset, target, ext, arch=None):
    return ('<li><a class="chip" href="%s">%s<span class="t">%s</span>'
            '<small>%s &middot; %s</small></a></li>'
            % (_esc(asset["browser_download_url"]),
               _icon(arch_icon(arch if arch is not None else target)),
               _esc(target), _esc(ext), _esc(_size(asset.get("size")))))


def _core_groups(core, debug):
    """One <details> per platform: icon, name and count in the summary,
    a grid of download chips inside."""
    by = {}
    for plat, tgt, ext, a in core:
        by.setdefault(plat, []).append((tgt, ext, a))
    out = []
    for plat, _l, _i in PLATFORMS:
        items = by.get(plat)
        if not items:
            continue
        hint = ""
        if plat == "rpi":
            hint = ('<p class="dl-hint">Match RetroArch&#39;s word size, not '
                    'the kernel&#39;s: <code>rpi4</code> is 32-bit, '
                    '<code>rpi4_64</code> is 64-bit.</p>')
        out.append(
            '<details class="plat"%s><summary>%s<span class="count">%d '
            'build%s</span></summary>%s<ul class="chips">%s</ul></details>'
            % (" open" if plat in PLATFORMS_OPEN else "", _plat_cell(plat),
               len(items), "" if len(items) == 1 else "s", hint,
               "".join(_chip(a, tgt, ext) for tgt, ext, a in items)))
    if debug:
        out.append(
            '<details class="plat"><summary><span class="plat-name">%s'
            'Debug symbols</span><span class="count">%d bundle%s</span>'
            '</summary><ul class="chips">%s</ul></details>'
            % (_icon("box"), len(debug), "" if len(debug) == 1 else "s",
               "".join(_chip(a, tgt, ".debug.tar.gz")
                       for _p, tgt, a in debug)))
    return "\n".join(out)


def _tools_table(cls):
    rows = []
    for tool, plat, arch, a in sorted(
            cls["tools"], key=lambda t: (t[0], _plat_order(t[1]
                                         if t[1] in PLATFORM else "other"))):
        rows.append(
            '<tr><td><code>%s</code></td><td>%s%s</td><td>%s</td>'
            '<td>%s</td></tr>'
            % (_esc(tool), _plat_cell(plat) if plat in PLATFORM else _esc(plat),
               (" " + _arch_cell(arch)) if arch else "",
               _dl(a, _esc(a["name"])), _esc(_size(a.get("size")))))
    if cls["info"]:
        a = cls["info"]
        rows.append('<tr><td>core info</td><td>any</td><td>%s</td><td>%s</td></tr>'
                    % (_dl(a, _esc(a["name"])), _esc(_size(a.get("size")))))
    for a in cls["other"]:
        rows.append('<tr><td>other</td><td></td><td>%s</td><td>%s</td></tr>'
                    % (_dl(a, _esc(a["name"])), _esc(_size(a.get("size")))))
    if not rows:
        return ""
    return ('<div class="table-wrap"><table class="dl-table">'
            '<thead><tr><th scope="col">Tool</th><th scope="col">Platform</th>'
            '<th scope="col">File</th><th scope="col">Size</th></tr></thead>'
            '<tbody>%s</tbody></table></div>' % "".join(rows))


def _panel_id(tag):
    return "rel-" + re.sub(r"[^\w.-]", "_", tag)


def render_panel(rel, kind, hidden, app_in=(), app_only=False):
    """One release's panel.  kind is 'latest', 'nightly' or 'older'.
    app_in: [(tag, url, label)] of the releases that ship the desktop app,
    for the pointer shown when this one does not.  app_only: the app page's
    copy, which lists the app packages and nothing else (callers only pass
    releases that carry the app)."""
    tag = rel["tag_name"]
    cls = classify_assets(rel["assets"])
    url = rel.get("html_url") or "%s/releases/tag/%s" % (REPO_URL, tag)
    date = (rel.get("published_at") or "")[:10]
    badge = {"latest": ' <span class="badge good">latest</span>',
             "nightly": ' <span class="badge warn">nightly</span>',
             "older": ""}[kind]
    meta = ["<strong>%s</strong>%s" % (_esc("nightly build" if kind == "nightly"
                                             else tag), badge)]
    if date:
        meta.append("<time datetime=\"%s\">%s</time>" % (_esc(date), _esc(date)))
    meta.append('<a href="%s">%s</a>' % (
        _esc(url), "release page" if kind == "nightly" else "release notes"))
    if cls["sums"]:
        s = cls["sums"]
        meta.append(_dl(s, "SHA256SUMS.txt") + " <small>(%s)</small>"
                    % _esc(_size(s.get("size"))))
    out = ['<div class="dl-panel" id="%s" data-rel="%s" data-kind="%s"%s>'
           % (_esc(_panel_id(tag)), _esc(tag), kind, " hidden" if hidden else ""),
           '<p class="dl-meta">%s</p>' % " &middot; ".join(meta)]
    if kind == "nightly":
        out.append('<p class="dl-note">Rebuilt from <code>develop</code> on '
                   'every push that compiles; gated on compiling, not on the '
                   'test suite. File names never change, so these links '
                   'always fetch the newest build.</p>')

    if app_only:
        out.append(_app_table(cls["app"]))
        out.append("</div>")
        return "\n".join(out)
    out.append('<h3 class="dl-h">Desktop app</h3>')
    if cls["app"]:
        out.append(_app_table(cls["app"]))
    else:
        # Point at the builds that do carry it (data-rel-link: download.js
        # switches the panel; the href is the GitHub page without it).
        pointers = " or ".join(
            '<a href="%s" data-rel-link="%s">%s</a>' % (_esc(u), _esc(t), l)
            for t, u, l in app_in if t != tag)
        out.append('<p class="dl-empty">No desktop app in this release.%s</p>'
                   % ((" It is in the %s." % pointers) if pointers else ""))
    n_core = len(cls["core"])
    out.append('<h3 class="dl-h">RetroArch core <small>%d builds</small></h3>'
               % n_core)
    if n_core or cls["debug"]:
        out.append(_core_groups(cls["core"], cls["debug"]))
    else:
        out.append('<p class="dl-empty">No core builds are attached to this '
                   'release.</p>')
    tools = _tools_table(cls)
    if tools:
        out.append(
            '<details class="plat tools" id="tools-%s"><summary>'
            '<span class="plat-name">%sTools and info file</span>'
            '<span class="count">%s</span></summary>'
            '<p class="dl-hint"><code>jagcd-tools</code> is a session-aware '
            '<code>chdman</code> for Jaguar CD images; <code>cue2cdi</code> '
            'converts CUE/BIN to CDI.</p>%s</details>'
            % (_esc(_panel_id(tag)[4:]), _icon("wrench"),
               "%d files" % (len(cls["tools"]) + bool(cls["info"])
                             + len(cls["other"])), tools))
    out.append("</div>")
    return "\n".join(out)


def _installer_widget(what=None):
    """The one-line installers as tabs (JS) or three stacked blocks (no JS).
    download.js rewrites the commands for the selected release from the
    data-cmd / data-base / data-what attributes.  what: preselect --what
    (the app page passes 'app')."""
    sh = "curl -fsSL %s/tools/install.sh | sh" % RAW_URL
    ps = "irm %s/tools/install.ps1 | iex" % RAW_URL
    sh_text = sh + (" -s -- --what %s" % what if what else "")
    ps_text = ("$env:VJ_WHAT = '%s'; " % what if what else "") + ps
    what_attr = ' data-what="%s"' % _esc(what) if what else ""
    return """<details class="plat inst" id="installer">
  <summary><span class="plat-name">%(i_term)sInstall with one command</span><span class="count">core, app or both, checksum-verified</span></summary>
  <div class="inst-tabs" role="tablist" aria-label="Operating system" hidden>
    <button type="button" role="tab" id="inst-t-sh" aria-controls="inst-p-sh" aria-selected="true">macOS, Linux, Pi</button>
    <button type="button" role="tab" id="inst-t-ps" aria-controls="inst-p-ps" aria-selected="false" tabindex="-1">Windows</button>
    <button type="button" role="tab" id="inst-t-pm" aria-controls="inst-p-pm" aria-selected="false" tabindex="-1">Package managers</button>
  </div>
  <div class="inst-panel" id="inst-p-sh" role="tabpanel" aria-labelledby="inst-t-sh">
    <p class="inst-label"><strong>macOS, Linux, Raspberry Pi</strong></p>
    <pre><code data-cmd="sh" data-base="%(sh)s"%(what_attr)s>%(sh_text)s</code></pre>
  </div>
  <div class="inst-panel" id="inst-p-ps" role="tabpanel" aria-labelledby="inst-t-ps">
    <p class="inst-label"><strong>Windows</strong> (PowerShell)</p>
    <pre><code data-cmd="ps" data-base="%(ps)s"%(what_attr)s>%(ps_text)s</code></pre>
  </div>
  <div class="inst-panel" id="inst-p-pm" role="tabpanel" aria-labelledby="inst-t-pm">
    <p class="inst-label"><strong>Package managers</strong></p>
    <p>Homebrew, winget and Scoop manifests are rendered by CI as templates in
    <a href="%(repo)s/blob/develop/app/packaging/manifests">app/packaging/manifests</a>;
    none is submitted yet. The Linux app also comes as a Flatpak bundle (table below).</p>
  </div>
  <p class="footnote">%(what_note)s; it verifies each file against
  <code>SHA256SUMS.txt</code> and updates what you already have when run again. Selecting a release above
  adds its <code>--channel</code>. Other options and install locations are on the
  <a href="./#get">home page</a>.</p>
</details>""" % {"sh": _esc(sh), "ps": _esc(ps), "repo": REPO_URL,
                 "sh_text": _esc(sh_text), "ps_text": _esc(ps_text),
                 "what_attr": what_attr,
                 "what_note": ("<code>--what %s</code> is already filled in"
                               % _esc(what)) if what else
                              "It asks what to install (<code>--what core|app|both</code>)",
                 "i_term": _icon("terminal")}


ICON_SPRITE = """<svg class="sprite" width="0" height="0" aria-hidden="true" focusable="false">
<symbol id="i-laptop" viewBox="0 0 16 16"><rect x="3" y="3.5" width="10" height="7"/><path d="M1.5 12.5h13"/></symbol>
<symbol id="i-window" viewBox="0 0 16 16"><rect x="2" y="3" width="12" height="10"/><path d="M2 6h12M4 4.5h.5M6 4.5h.5"/></symbol>
<symbol id="i-terminal" viewBox="0 0 16 16"><rect x="2" y="3" width="12" height="10"/><path d="M4.5 6.5l2 1.5-2 1.5M8 10.5h3"/></symbol>
<symbol id="i-board" viewBox="0 0 16 16"><rect x="1.5" y="3" width="13" height="10"/><rect x="4.5" y="5.5" width="4" height="5"/><path d="M10.5 6h2M10.5 8h2M10.5 10h2"/></symbol>
<symbol id="i-phone" viewBox="0 0 16 16"><rect x="4.5" y="1.5" width="7" height="13" rx="1"/><path d="M7 12.5h2"/></symbol>
<symbol id="i-tablet" viewBox="0 0 16 16"><rect x="2.5" y="2" width="11" height="12" rx="1"/><path d="M8 12h.01"/></symbol>
<symbol id="i-globe" viewBox="0 0 16 16"><circle cx="8" cy="8" r="6"/><path d="M2 8h12M8 2c-2.5 2.5-2.5 9.5 0 12M8 2c2.5 2.5 2.5 9.5 0 12"/></symbol>
<symbol id="i-handheld" viewBox="0 0 16 16"><rect x="1.5" y="4.5" width="13" height="7" rx="1.5"/><path d="M5 6.5v3M3.5 8h3M11 7.5h.01M12.5 9h.01"/></symbol>
<symbol id="i-box" viewBox="0 0 16 16"><path d="M2 5l6-3 6 3v6l-6 3-6-3zM2 5l6 3 6-3M8 8v6"/></symbol>
<symbol id="i-cube" viewBox="0 0 16 16"><path d="M2 5l6-3 6 3v6l-6 3-6-3zM2 5l6 3 6-3M8 8v6"/></symbol>
<symbol id="i-wrench" viewBox="0 0 16 16"><path d="M10.5 2.5a3.5 3.5 0 0 0-3.2 4.8L2.5 12.1l1.4 1.4 4.8-4.8a3.5 3.5 0 0 0 4.8-3.2l-2 2-1.8-.4-.4-1.8z"/></symbol>
<symbol id="i-chip" viewBox="0 0 16 16"><rect x="4" y="4" width="8" height="8"/><path d="M6 1.5V4M10 1.5V4M6 12v2.5M10 12v2.5M1.5 6H4M1.5 10H4M12 6h2.5M12 10h2.5"/></symbol>
<symbol id="i-chip-arm" viewBox="0 0 16 16"><rect x="4" y="4" width="8" height="8"/><circle cx="8" cy="8" r="1.4"/><path d="M6 1.5V4M10 1.5V4M6 12v2.5M10 12v2.5M1.5 6H4M1.5 10H4M12 6h2.5M12 10h2.5"/></symbol>
<symbol id="i-layers" viewBox="0 0 16 16"><rect x="2" y="2" width="8" height="8"/><rect x="6" y="6" width="8" height="8"/></symbol>
</svg>"""


def render_downloads(releases, app_only=False):
    """The body of the Download page below the lede: one window holding the
    release picker, the installer widget, one panel per release and the
    verification note.

    app_only: the app page's "Get it" window -- the same picker, but only
    over releases that ship the app, each panel just the app table, and the
    installer preset to --what app."""
    releases = releases or {}
    latest = releases.get("latest")
    nightly = releases.get("nightly")
    older = releases.get("older") or []
    if app_only:
        return _render_app_downloads(latest, nightly, older)
    if not latest:
        return """<section class="win" id="app">
  <h2>Releases</h2>
  <div class="win-body">
    <p>
      This copy of the site was built without release data, so here are the
      release pages: the <a href="%(repo)s/releases/latest">latest release</a>
      (the desktop app packages and the RetroArch core for every platform)
      and the <a href="%(repo)s/releases/tag/nightly">nightly build</a>.
    </p>
  </div>
</section>""" % {"repo": REPO_URL}

    ltag = latest["tag_name"]
    def rel_url(rel):
        return rel.get("html_url") or "%s/releases/tag/%s" % (
            REPO_URL, rel["tag_name"])

    app_in = []
    if _has_app(latest):
        app_in.append((ltag, rel_url(latest), "latest release"))
    if nightly and _has_app(nightly):
        app_in.append((nightly["tag_name"], rel_url(nightly), "nightly build"))
    panels = []
    if nightly:
        panels.append(render_panel(nightly, "nightly", True, app_in))
    panels.append(render_panel(latest, "latest", False, app_in))
    for r in older:
        panels.append(render_panel(r, "older", True, app_in))

    # The picker.  Hidden until download.js runs: without it there is
    # nothing to operate, so the no-JS note below takes its place.
    older_opts = "".join(
        '<option value="%s">%s%s</option>' % (
            _esc(r["tag_name"]), _esc(r["tag_name"]),
            _esc(" (%s)" % r["published_at"][:10]) if r.get("published_at")
            else "") for r in older)
    picker = ['<div class="dl-picker" hidden>',
              '<span class="dl-label" id="dl-pick-label">Release</span>',
              '<div class="dl-seg" role="group" aria-labelledby="dl-pick-label">']
    if nightly:
        picker.append('<button type="button" class="dl-btn" data-rel="nightly" '
                      'aria-pressed="false">Nightly</button>')
    picker.append('<button type="button" class="dl-btn" data-rel="%s" '
                  'aria-pressed="true">Latest <code>%s</code></button>'
                  % (_esc(ltag), _esc(ltag)))
    picker.append("</div>")
    if older:
        picker.append(
            '<label class="dl-older"><span class="sr-only">Older releases'
            '</span><select id="dl-older"><option value="">Older releases '
            '(%d)</option>%s</select></label>' % (len(older), older_opts))
    picker.append('<p class="sr-only" id="dl-status" role="status" '
                  'aria-live="polite"></p></div>')

    nojs = ('<p class="dl-nojs">Showing %s, the latest release. '
            '<a href="%s/releases/tag/nightly">Nightly build</a> &middot; '
            '<a href="%s/releases">All releases</a> on GitHub.</p>'
            % (_esc(ltag), REPO_URL, REPO_URL))

    return """%(sprite)s
<section class="win dl" id="app">
  <h2>Downloads</h2>
  <div class="win-body">
%(nojs)s
%(picker)s
%(installer)s
%(panels)s
    <p class="footnote" id="verify">
      Compare a download's SHA-256 with its line in <code>SHA256SUMS.txt</code>
      (<code>shasum -a 256 &lt;file&gt;</code> on macOS and Linux,
      <code>Get-FileHash &lt;file&gt;</code> in PowerShell). The macOS app is
      Developer ID signed and notarized; the Windows installer is not yet
      code-signed, so SmartScreen may warn; the core files are unsigned.
      <a href="%(repo)s/blob/develop/app/packaging/README.md">Packaging details</a>.
    </p>
  </div>
</section>
<script src="assets/js/download.js" defer></script>""" % {
        "sprite": ICON_SPRITE, "nojs": nojs, "picker": "\n".join(picker),
        "installer": _installer_widget(), "panels": "\n".join(panels),
        "repo": REPO_URL}


def _has_app(rel):
    return any(APP_ASSET_RE.match(a["name"]) for a in rel["assets"])


def _render_app_downloads(latest, nightly, older):
    """{{APP_DOWNLOADS}}: see render_downloads(app_only=True).  The section
    id is "get" (the app page's existing anchor); download.js finds it by
    its .dl class."""
    rels = [r for r in [latest] + list(older) if r and _has_app(r)]
    if not rels:
        return """<section class="win" id="get">
  <h2>Get it</h2>
  <div class="win-body">
    <p>
      The app packages are attached to each
      <a href="%(repo)s/releases">GitHub release</a> next to the core builds;
      the <a href="%(repo)s/releases/tag/nightly">nightly build</a> carries
      them too.
    </p>
  </div>
</section>""" % {"repo": REPO_URL}

    head = rels[0]
    htag = head["tag_name"]
    panels = []
    if nightly and _has_app(nightly):
        panels.append(render_panel(nightly, "nightly", True, app_only=True))
    panels.append(render_panel(head, "latest", False, app_only=True))
    for r in rels[1:]:
        panels.append(render_panel(r, "older", True, app_only=True))

    picker = ['<div class="dl-picker" hidden>',
              '<span class="dl-label" id="dl-pick-label">Release</span>',
              '<div class="dl-seg" role="group" aria-labelledby="dl-pick-label">']
    if nightly and _has_app(nightly):
        picker.append('<button type="button" class="dl-btn" data-rel="nightly" '
                      'aria-pressed="false">Nightly</button>')
    picker.append('<button type="button" class="dl-btn" data-rel="%s" '
                  'aria-pressed="true">Latest <code>%s</code></button>'
                  % (_esc(htag), _esc(htag)))
    picker.append("</div>")
    if len(rels) > 1:
        picker.append(
            '<label class="dl-older"><span class="sr-only">Older releases'
            '</span><select id="dl-older"><option value="">Older releases '
            '(%d)</option>%s</select></label>' % (len(rels) - 1, "".join(
                '<option value="%s">%s%s</option>' % (
                    _esc(r["tag_name"]), _esc(r["tag_name"]),
                    _esc(" (%s)" % r["published_at"][:10])
                    if r.get("published_at") else "") for r in rels[1:])))
    picker.append('<p class="sr-only" id="dl-status" role="status" '
                  'aria-live="polite"></p></div>')

    nojs = ('<p class="dl-nojs">Showing %s, the latest release. '
            '<a href="%s/releases/tag/nightly">Nightly build</a> &middot; '
            '<a href="download.html">Download page</a> (the RetroArch core '
            'and older releases).</p>' % (_esc(htag), REPO_URL))

    return """%(sprite)s
<section class="win dl" id="get">
  <h2>Get it</h2>
  <div class="win-body">
    <p>
      The packages are attached to each
      <a href="%(repo)s/releases">GitHub release</a> next to the core builds
      and listed in its <code>SHA256SUMS.txt</code>. Pick a release; the
      RetroArch core builds are on the <a href="download.html">Download</a>
      page.
    </p>
%(nojs)s
%(picker)s
%(installer)s
%(panels)s
    <p class="footnote" id="verify">
      Compare a download's SHA-256 with its line in <code>SHA256SUMS.txt</code>
      (<code>shasum -a 256 &lt;file&gt;</code> on macOS and Linux,
      <code>Get-FileHash &lt;file&gt;</code> in PowerShell). The macOS app is
      Developer ID signed and notarized, so it opens without a Gatekeeper
      prompt; the Windows installer is not yet code-signed, so SmartScreen may
      warn. Builds you make yourself, and builds from forks, are unsigned.
      <a href="%(repo)s/blob/develop/app/packaging/README.md">Packaging details</a>.
    </p>
  </div>
</section>
<script src="assets/js/download.js" defer></script>""" % {
        "sprite": ICON_SPRITE, "nojs": nojs, "picker": "\n".join(picker),
        "installer": _installer_widget("app"), "panels": "\n".join(panels),
        "repo": REPO_URL}


def latest_link_html(releases):
    """For the home page: 'v3.8.0' linked to its release, or the releases list."""
    latest = (releases or {}).get("latest")
    if latest:
        url = latest.get("html_url") or "%s/releases/tag/%s" % (
            REPO_URL, latest["tag_name"])
        return 'latest: <a href="%s"><strong>%s</strong></a>' % (
            html.escape(url), html.escape(latest["tag_name"]))
    return '<a href="%s/releases">Releases</a>' % REPO_URL


# ------------------------------------------------------------ release log
#
# The home page's release log is generated from docs/RELEASE_NOTES_v*.md, so a
# new release appears on the site as soon as its notes are committed.  Each
# row's summary is the notes file's own opening paragraph (the bold tagline
# under the "# Virtual Jaguar libretro vX.Y.Z" heading), trimmed to whole
# sentences.  Nothing is typed into the page by hand.

NOTES_DIR = ROOT / "docs"
NOTES_RE = re.compile(r"^RELEASE_NOTES_v(\d+)\.(\d+)\.(\d+)\.md$")
RELEASE_LOG_MIN_MAJOR = 3      # v2.x predates the libretro rework; GitHub has it
RELEASE_LOG_SHOWN = 5          # newest rows visible; the rest sit in a <details>
RELEASE_LOG_SUMMARY_MAX = 240  # characters, cut at a sentence boundary
RELEASE_LOG_SUMMARY_MIN = 80   # a shorter opener takes the next sentence too
SENTENCE_RE = re.compile(r"(?<=[.!?])\s+(?=[A-Z])")


def release_summary(text):
    """Opening paragraph of a notes file as one plain-text summary.

    Skips the H1, takes the first paragraph, drops markdown emphasis, code
    ticks, links and "Closes ..." bookkeeping sentences, then keeps whole
    sentences up to RELEASE_LOG_SUMMARY_MAX characters (at least
    RELEASE_LOG_SUMMARY_MIN, so a one-line opener is not the whole row).
    """
    lines = text.splitlines()
    i = 0
    while i < len(lines) and (not lines[i].strip() or lines[i].startswith("#")):
        i += 1
    para = []
    while i < len(lines) and lines[i].strip() and not lines[i].startswith(
            ("#", "---", "- ", "* ")):
        para.append(lines[i].strip())
        i += 1
    flat = " ".join(para)
    flat = re.sub(r"\[([^\]]+)\]\([^)]*\)", r"\1", flat)
    flat = re.sub(r"https?://\S+", "", flat)
    flat = flat.replace("**", "").replace("`", "")
    flat = re.sub(r"\s*\(#\d+[^)]*\)", "", flat)
    flat = re.sub(r"\s+", " ", flat).strip()
    sentences = [x for x in SENTENCE_RE.split(flat)
                 if x and not x.startswith("Closes")
                 and not re.search(r"\d+ commits", x)]
    out = ""
    for sent in sentences:
        if (len(out) >= RELEASE_LOG_SUMMARY_MIN
                and len(out) + 1 + len(sent) > RELEASE_LOG_SUMMARY_MAX):
            break
        out = (out + " " + sent).strip()
    return normalize_dashes(out)


def load_release_notes(notes_dir=NOTES_DIR):
    """[(version tuple, 'vX.Y.Z', summary)] newest first, v3+ only."""
    found = []
    for path in notes_dir.glob("RELEASE_NOTES_v*.md"):
        m = NOTES_RE.match(path.name)
        if not m:
            continue
        ver = tuple(int(x) for x in m.groups())
        if ver[0] < RELEASE_LOG_MIN_MAJOR:
            continue
        summary = release_summary(path.read_text(encoding="utf-8"))
        if not summary:
            die("%s: no opening paragraph to use as the release summary"
                % path.name)
        found.append((ver, "v%d.%d.%d" % ver, summary))
    if not found:
        die("no docs/RELEASE_NOTES_v*.md files found for the release log")
    found.sort(reverse=True)
    return found


def drop_unreleased(entries, releases):
    """With release data in hand, hide notes newer than the latest release.

    Notes are committed before the tag exists (docs/release-process.md), and a
    row whose release link 404s would announce a release that has not
    shipped.  Without release data (offline build) every notes file shows.
    """
    tag = ((releases or {}).get("latest") or {}).get("tag_name", "")
    m = re.match(r"^v(\d+)\.(\d+)\.(\d+)$", tag)
    if not m:
        return entries
    newest = tuple(int(x) for x in m.groups())
    return [e for e in entries if e[0] <= newest]


def render_release_log(entries):
    """The home page's release table, newest first; older rows folded."""
    def row(tag, summary):
        return (
            "      <tr>\n"
            "        <td><strong>%(tag)s</strong></td>\n"
            "        <td>%(summary)s</td>\n"
            "        <td><a href=\"%(repo)s/releases/tag/%(tag)s\">release</a>"
            " &middot;\n"
            "            <a href=\"%(repo)s/blob/develop/docs/"
            "RELEASE_NOTES_%(tag)s.md\">notes</a></td>\n"
            "      </tr>\n" % {"tag": tag, "repo": REPO_URL,
                              "summary": html.escape(summary)})

    def table(rows):
        return ("    <div class=\"table-wrap\"><table class=\"release-log\">\n"
                "      <tr><th>Version</th><th>What landed</th>"
                "<th>Links</th></tr>\n" + "".join(rows) +
                "    </table></div>")

    rows = [row(tag, summary) for _, tag, summary in entries]
    out = table(rows[:RELEASE_LOG_SHOWN])
    if rows[RELEASE_LOG_SHOWN:]:
        out += (
            "\n    <details class=\"fold\">\n"
            "      <summary>Older releases (%s to %s)</summary>\n%s\n"
            "    </details>"
            % (entries[RELEASE_LOG_SHOWN][1], entries[-1][1],
               table(rows[RELEASE_LOG_SHOWN:])))
    out += ("\n    <p class=\"footnote\">Every release, with its binaries, is "
            "on the <a href=\"%s/releases\">releases page</a>.</p>" % REPO_URL)
    return out


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
        # The showcase site is published by Provenance; the Organization node
        # itself lives on provenance-emu.com and is referenced by @id.
        "publisher": {
            "@type": "Organization",
            "@id": PROVENANCE_ORG_ID,
            "name": "Provenance",
            "url": "https://provenance-emu.com/",
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
    share = ctx["share"][page_name]
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
        '<meta property="og:image" content="%s">' % esc(share["url"]),
        '<meta property="og:image:type" content="image/png">',
        '<meta property="og:image:width" content="%d">' % share["w"],
        '<meta property="og:image:height" content="%d">' % share["h"],
        '<meta property="og:image:alt" content="%s">' % esc(share["alt"]),
        '<meta name="twitter:card" content="summary_large_image">',
        '<meta name="twitter:title" content="%s">' % esc(title),
        '<meta name="twitter:description" content="%s">' % esc(desc),
        '<meta name="twitter:image" content="%s">' % esc(share["url"]),
        '<meta name="twitter:image:alt" content="%s">' % esc(share["alt"]),
        '<link rel="icon" href="favicon.ico" sizes="32x32">',
        '<link rel="icon" href="%sfavicon.svg" type="image/svg+xml">'
        % ICON_DIR,
    ] + [
        '<link rel="icon" href="%sicon-%d.png" type="image/png" '
        'sizes="%dx%d">' % (ICON_DIR, n, n, n) for n in ICON_PNG_SIZES
    ] + [
        '<link rel="apple-touch-icon" href="%sapple-touch-icon.png">'
        % ICON_DIR,
    ]
    if page_name == "index.html":
        data = software_application_ld(ctx["version"], share["url"], desc)
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
    #
    # color-scheme is "dark light": dark is the primary theme (the console
    # was a black box), light is a deliberate period-desktop variant, not a
    # fallback.  The order tells the UA which one to paint before CSS lands.
    #
    # The two <div class="hazard"> bars are the page's only pure decoration:
    # a diagonal red stripe bookending the content, drawn in CSS.  They are
    # aria-hidden by being empty non-semantic divs.
    return """<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta name="color-scheme" content="dark light">
%(headmeta)s
<link rel="stylesheet" href="style.css">
</head>
<body>
<header class="site-header"><div class="inner">
  <a class="site-title" href="./">Virtual <span class="jag">Jaguar</span> libretro</a>
  <nav class="site-nav" aria-label="Primary">%(nav)s</nav>
</div></header>
<div class="hazard"></div>
<main>
%(body)s
</main>
<div class="hazard"></div>
<footer class="site-footer"><div class="inner">
  <p>
    Virtual Jaguar libretro. GPLv3.
    <a href="%(repo)s">Source on GitHub</a> &middot;
    <a href="%(repo)s#readme">README</a> &middot;
    <a href="%(repo)s/releases">releases</a> &middot;
    <a href="%(repo)s/releases/tag/nightly">nightly build</a> &middot;
    <a href="%(repo)s/discussions">Discussions</a> &middot;
    <a href="%(repo)s/issues">Issues</a> &middot;
    <a href="privacy.html">Privacy</a> &middot;
    <a href="terms.html">Terms</a>.
  </p>
  <p>
    Core options, controls and file extensions are documented in the
    <a href="%(docs)s">official libretro documentation for this core</a> on
    docs.libretro.com. That page is the reference manual. This site is the
    project showcase.
  </p>
  <p>
    Generated from committed repository data by
    <a href="%(repo)s/blob/develop/scripts/build_site.py">scripts/build_site.py</a>.
    Every claim links to its evidence.
  </p>
  <p class="family">
    More from Provenance:
%(family)s
  </p>
</div></footer>
</body>
</html>
""" % {"headmeta": head_meta(page_name, meta, ctx), "nav": "\n".join(nav),
       "body": body, "repo": REPO_URL, "docs": LIBRETRO_DOCS,
       "family": family_links_html()}


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
    ap.add_argument("--releases", default=None, metavar="JSON",
                    help="GitHub release data ({\"latest\": ..., \"nightly\": "
                         "..., \"older\": [...]}) for direct download links; without it the "
                         "Download page links the release pages instead")
    ap.add_argument("--releases-from-github", action="store_true",
                    help="fetch that data from the GitHub API instead "
                         "(GH_TOKEN/GITHUB_TOKEN used when set); a failed "
                         "fetch fails the build")
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

    for p in ALL_PAGES:
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
    if args.releases and args.releases_from_github:
        die("--releases and --releases-from-github are exclusive")
    releases = (load_releases(args.releases) if args.releases
                else fetch_releases() if args.releases_from_github else None)
    if releases:
        print("releases: latest %s (%d assets), nightly %s, %d older"
              % (releases.get("latest", {}).get("tag_name", "-"),
                 len(releases.get("latest", {}).get("assets", [])),
                 "present" if releases.get("nightly") else "absent",
                 len(releases.get("older") or [])))
    else:
        print("releases: none (no --releases/--releases-from-github); the "
              "Download page links the release pages, not the files")
    share = {}
    for p in ALL_PAGES:
        rel, alt = SHARE_IMAGES.get(p, SHARE_DEFAULT)
        w, h = png_size(SITE_SRC / rel)
        share[p] = {"url": SITE_BASE + rel, "alt": alt, "w": w, "h": h}
    shallow = repo_is_shallow()
    ctx = {"version": version, "share": share}
    print("core version: %s | share images: %s | lastmod source: %s"
          % (version, ", ".join(sorted(set(
              "%s %dx%d" % (v["url"][len(SITE_BASE):], v["w"], v["h"])
              for v in share.values()))),
             "build date %s (shallow clone or no git)" % build_date
             if shallow else "git log (fallback %s)" % build_date))

    subs = {
        "{{REPO}}": REPO_URL,
        "{{RAW_URL}}": RAW_URL,
        "{{CORE_VERSION}}": html.escape(version),
        "{{DOCS_URL}}": LIBRETRO_DOCS,
        "{{DOCS_OPTIONS_URL}}": LIBRETRO_DOCS_OPTIONS,
        "{{DOCS_CONTROLS_URL}}": LIBRETRO_DOCS_CONTROLS,
        "{{DOCS_COMPAT_URL}}": LIBRETRO_DOCS_COMPAT,
        "{{DOCS_SRC_URL}}": LIBRETRO_DOCS_SRC,
        "{{DOWNLOADS}}": render_downloads(releases),
        "{{APP_DOWNLOADS}}": render_downloads(releases, app_only=True),
        "{{LATEST_LINK}}": latest_link_html(releases),
        "{{RELEASE_LOG}}": render_release_log(
            drop_unreleased(load_release_notes(), releases)),
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
        "{{CART_N_HLE_RUN}}":
            "%d of %d" % (cart_n_good["hle_run"], cart_n_titles),
        "{{CART_N_BIOS_RUN}}":
            "%d of %d" % (cart_n_good["bios_run"], cart_n_titles),
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
    shutil.copy2(SITE_SRC / ICON_DIR / "favicon.ico", out / "favicon.ico")
    assets_src = SITE_SRC / "assets"
    if assets_src.is_dir():
        shutil.copytree(assets_src, out / "assets")
    # Jaguar CD artwork mirror (libretro-thumbnails layout), served at
    # /thumbnails/<System>/<Type>/<Name>.png; the desktop app falls back to it.
    thumbs_src = SITE_SRC / "thumbnails"
    if thumbs_src.is_dir():
        shutil.copytree(thumbs_src, out / "thumbnails")
    (out / ".nojekyll").write_text("")

    nav_items = []
    frags = {}
    for p in ALL_PAGES:
        meta, body = read_fragment(SITE_SRC / "pages" / p)
        if p in PAGES:
            nav_items.append((p, meta["nav"]))
        frags[p] = (meta, body)

    # Every page's rendered output depends on the generator's template too,
    # and the compatibility page additionally on the parsed matrix doc.
    common_deps = [Path(__file__).resolve(), SITE_SRC / "style.css"]
    sitemap_entries = []

    for p in ALL_PAGES:
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
        if p == "index.html":
            deps.extend(sorted(NOTES_DIR.glob("RELEASE_NOTES_v*.md")))
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
