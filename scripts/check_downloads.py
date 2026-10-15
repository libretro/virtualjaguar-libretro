#!/usr/bin/env python3
"""Check the generated Download page against the release data it was built from.

  python3 scripts/build_site.py --out _site --releases scripts/testdata/releases.json
  python3 scripts/check_downloads.py _site scripts/testdata/releases.json

Stdlib only.  For every release in the JSON (latest, nightly, older[]):
  - the page has exactly one panel for it;
  - every asset of that release appears in the panel exactly once, as a link
    whose href is that asset's browser_download_url (so nothing is dropped
    and nothing is listed twice);
and, page-wide, every release-download href resolves to an asset name in the
JSON, element ids are unique, and exactly one panel is visible without
scripts (the latest release).
"""
import json
import re
import sys
from collections import Counter
from html import unescape
from pathlib import Path

FAILS = []


def check(cond, msg):
    if not cond:
        FAILS.append(msg)
    return cond


def main():
    if len(sys.argv) != 3:
        sys.stderr.write(__doc__)
        return 2
    page = (Path(sys.argv[1]) / "download.html").read_text(encoding="utf-8")
    data = json.loads(Path(sys.argv[2]).read_text(encoding="utf-8"))

    rels = []
    for key in ("latest", "nightly"):
        if data.get(key):
            rels.append(data[key])
    rels += data.get("older") or []

    panels = {}
    for m in re.finditer(
            r'<div class="dl-panel" id="([^"]+)" data-rel="([^"]+)"'
            r' data-kind="(\w+)"( hidden)?>', page):
        panels[unescape(m.group(2))] = (m.start(), m.group(3), bool(m.group(4)))
    starts = sorted(v[0] for v in panels.values())
    ends = starts[1:] + [page.index('id="verify"')]
    span = dict((tag, (st, ends[starts.index(st)]))
                for tag, (st, _k, _h) in panels.items())

    check(len(panels) == len(rels), "%d panels for %d releases"
          % (len(panels), len(rels)))
    visible = [t for t, (_s, _k, hid) in panels.items() if not hid]
    check(len(visible) == 1 and visible[0] == data["latest"]["tag_name"],
          "visible-without-JS panels: %r (want only the latest)" % visible)

    total = 0
    for rel in rels:
        tag = rel["tag_name"]
        if not check(tag in panels, "no panel for %s" % tag):
            continue
        st, en = span[tag]
        body = page[st:en]
        hrefs = Counter(unescape(h) for h in re.findall(r'href="([^"]+)"', body))
        for a in rel["assets"]:
            url = a["browser_download_url"]
            n = hrefs.get(url, 0)
            check(n == 1, "%s: %s listed %d times, want exactly 1"
                  % (tag, a["name"], n))
            total += 1
        want = set(a["browser_download_url"] for a in rel["assets"])
        for h in hrefs:
            if "/releases/download/" in h:
                check(h in want, "%s: link %s is not an asset of this release"
                      % (tag, h))

    names = set(a["name"] for r in rels for a in r["assets"])
    for h in re.findall(r'href="([^"]*/releases/download/[^"]+)"', page):
        check(unescape(h).rsplit("/", 1)[1] in names,
              "href %s names no known asset" % h)

    ids = Counter(re.findall(r'\bid="([^"]+)"', page))
    for i, n in ids.items():
        check(n == 1, "duplicate id %r x%d" % (i, n))
    check(page.count("<h1>") == 1, "want one <h1>")

    if FAILS:
        sys.stderr.write("FAIL: check_downloads.py: %d problem(s)\n" % len(FAILS))
        for f in FAILS[:40]:
            sys.stderr.write("  - %s\n" % f)
        return 1
    print("check_downloads.py: OK -- %d releases, %d assets each listed once"
          % (len(rels), total))
    return 0


if __name__ == "__main__":
    sys.exit(main())
