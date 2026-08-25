#!/usr/bin/env python3
"""Post-build checks for the generated site.  Stdlib only.

`build_site.py` fails loudly on bad *input*; this checks the *output*, so the
SEO and accessibility guarantees documented in docs/site-maintenance.md stay
true as pages are edited.  Run in CI right after the build:

  python3 scripts/build_site.py && python3 scripts/check_site.py _site

Checks, per page: exactly one <h1>, lang="en", non-empty descriptive alt on
every image, every local href/src resolves inside the output, no leftover
{{PLACEHOLDER}}, canonical == og:url == the sitemap <loc>, the full Open
Graph / Twitter card set present with an absolute image that exists, JSON-LD
parses and is the expected @type.  Site-wide: titles and descriptions are
distinct, sitemap.xml is well-formed and lists exactly the generated pages,
robots.txt carries an absolute Sitemap: line, and no page references a
tracker, an external asset, or any <script> other than JSON-LD blocks and
deferred <script src> tags pointing at local files under assets/js/ that
exist in the output (inline and external scripts stay hard failures).  The
site currently ships no script beyond JSON-LD; that allowance is the
contract for future pages, not something in use.
"""

import json
import re
import sys
import xml.etree.ElementTree as ET
from html.parser import HTMLParser
from pathlib import Path
from urllib.parse import urldefrag

sys.path.insert(0, str(Path(__file__).resolve().parent))
from build_site import PAGES, SITE_BASE, page_url  # noqa: E402

# Substrings that must never appear: analytics, tag managers, CDNs, web fonts.
FORBIDDEN = ("googletagmanager", "google-analytics", "gtag(", "plausible.io",
             "matomo", "cdn.jsdelivr", "unpkg.com", "cdnjs.cloudflare",
             "fonts.googleapis", "fonts.gstatic")

REQUIRED_META = ("og:type", "og:title", "og:description", "og:image",
                 "og:image:width", "og:image:height", "og:image:alt",
                 "twitter:card", "twitter:title", "twitter:description",
                 "twitter:image")

FAILS = []


def check(cond, msg):
    if not cond:
        FAILS.append(msg)
    return cond


class PageParser(HTMLParser):
    def __init__(self):
        HTMLParser.__init__(self, convert_charrefs=True)
        self.h1 = 0
        self.imgs = []
        self.links = []
        self.meta = {}
        self.canonical = None
        self.lang = None
        self.title = ""
        self.ld = []
        self._in_title = False
        self._in_ld = False

    def handle_starttag(self, tag, attrs):
        a = dict(attrs)
        if tag == "html":
            self.lang = a.get("lang")
        elif tag == "h1":
            self.h1 += 1
        elif tag == "title":
            self._in_title = True
        elif tag == "img":
            self.imgs.append((a.get("src"), a.get("alt")))
        elif tag == "a" and a.get("href"):
            self.links.append(a["href"])
        elif tag == "link":
            if a.get("rel") == "canonical":
                self.canonical = a.get("href")
            elif a.get("href"):
                self.links.append(a["href"])
        elif tag == "meta":
            key = a.get("name") or a.get("property")
            if key:
                self.meta[key] = a.get("content", "")
        elif tag == "script" and a.get("type") == "application/ld+json":
            self._in_ld = True

    def handle_endtag(self, tag):
        if tag == "title":
            self._in_title = False
        elif tag == "script":
            self._in_ld = False

    def handle_data(self, data):
        if self._in_title:
            self.title += data
        if self._in_ld:
            self.ld.append(data)


def check_page(out, name):
    path = out / name
    if not check(path.is_file(), "missing output page %s" % name):
        return None
    text = path.read_text(encoding="utf-8")
    left = re.search(r"\{\{[A-Z_]+\}\}", text)
    check(not left, "%s: unresolved placeholder %s"
          % (name, left.group(0) if left else ""))

    pp = PageParser()
    pp.feed(text)
    url = page_url(name)

    check(pp.lang == "en", "%s: <html lang> is %r, expected 'en'"
          % (name, pp.lang))
    check(pp.h1 == 1, "%s: %d <h1> elements, expected exactly 1"
          % (name, pp.h1))
    check(pp.title.strip(), "%s: empty <title>" % name)
    check(pp.meta.get("description"), "%s: no meta description" % name)

    for src, alt in pp.imgs:
        check(alt and alt.strip(),
              "%s: <img src=%r> has no alt text" % (name, src))
        check(alt is None or len(alt) > 15,
              "%s: <img src=%r> alt is not descriptive: %r" % (name, src, alt))

    for href in pp.links + [s for s, _ in pp.imgs]:
        if not href or re.match(r"^(https?:|mailto:|#|data:)", href):
            continue
        # The home page is canonical as the directory form, so nothing may
        # link it as index.html -- that would point crawlers at the duplicate
        # URL rel=canonical explicitly rejects.
        check(urldefrag(href)[0] != "index.html",
              "%s: links the home page as %r; use './' to match its canonical "
              "URL" % (name, href))
        target = urldefrag(href)[0]
        if target:
            check((out / target).exists(),
                  "%s: local link %r resolves to nothing" % (name, href))

    check(pp.canonical == url, "%s: canonical %r != expected %r"
          % (name, pp.canonical, url))
    check(pp.meta.get("og:url") == url, "%s: og:url %r != canonical %r"
          % (name, pp.meta.get("og:url"), url))
    for key in REQUIRED_META:
        check(pp.meta.get(key), "%s: missing %s" % (name, key))
    check(pp.meta.get("twitter:card") == "summary_large_image",
          "%s: twitter:card is %r" % (name, pp.meta.get("twitter:card")))
    for key in ("og:image", "twitter:image"):
        val = pp.meta.get(key, "")
        check(val.startswith(SITE_BASE),
              "%s: %s is not an absolute site URL: %r" % (name, key, val))
        rel = val[len(SITE_BASE):] if val.startswith(SITE_BASE) else ""
        check(rel and (out / rel).is_file(),
              "%s: %s does not point at a published file: %r"
              % (name, key, val))

    check(pp.ld, "%s: no JSON-LD block" % name)
    want = "SoftwareApplication" if name == "index.html" else "BreadcrumbList"
    for blob in pp.ld:
        try:
            data = json.loads(blob)
        except ValueError as exc:
            FAILS.append("%s: JSON-LD does not parse: %s" % (name, exc))
            continue
        check(data.get("@context") == "https://schema.org",
              "%s: JSON-LD @context %r" % (name, data.get("@context")))
        check(data.get("@type") == want, "%s: JSON-LD @type %r, expected %r"
              % (name, data.get("@type"), want))
        if want == "SoftwareApplication":
            check(re.match(r"^\d+\.\d+\.\d+", data.get("softwareVersion", "")),
                  "%s: softwareVersion %r" % (name, data.get("softwareVersion")))
            check(data.get("isAccessibleForFree") is True,
                  "%s: isAccessibleForFree is not true" % name)
            check("gpl-3.0" in data.get("license", ""),
                  "%s: license %r" % (name, data.get("license")))
            # We have no ratings or reviews; publishing any would be a lie.
            check("aggregateRating" not in data and "review" not in data,
                  "%s: JSON-LD carries rating/review data we do not have"
                  % name)
        else:
            items = data.get("itemListElement", [])
            check(len(items) == 2,
                  "%s: breadcrumb has %d items, expected 2" % (name, len(items)))
            check(items and items[-1].get("item") == url,
                  "%s: breadcrumb tail %r != %r"
                  % (name, items[-1].get("item") if items else None, url))

    for bad in FORBIDDEN:
        check(bad not in text, "%s: references %r -- no trackers or external "
                               "assets are allowed" % (name, bad))
    # Scripts: JSON-LD blocks, or <script src> pointing at a LOCAL file
    # under assets/js/ that exists in the output.  The site currently ships
    # NO script at all -- the period chrome is pure CSS -- so this loop
    # normally has nothing to inspect; the rule stays as the contract for
    # any future page that needs behaviour.  External src and inline
    # non-JSON-LD scripts remain hard failures: no CDNs, no trackers, no
    # surprises (see docs/site-maintenance.md).
    for m in re.finditer(r"<script([^>]*)>", text):
        attrs = m.group(1)
        if 'type="application/ld+json"' in attrs:
            continue
        srcm = re.search(r'src="([^"]*)"', attrs)
        if not check(srcm is not None,
                     "%s: inline non-JSON-LD <script%s>" % (name, attrs)):
            continue
        src = srcm.group(1)
        check(not re.match(r"^(https?:)?//", src),
              "%s: external script src %r -- vendor it locally" % (name, src))
        check(re.match(r"^assets/js/[\w.-]+\.js$", src),
              "%s: script src %r is outside assets/js/" % (name, src))
        check((out / src).is_file(),
              "%s: script src %r does not exist in the output" % (name, src))
        check(re.search(r"\bdefer\b", attrs) is not None,
              "%s: script src %r must be deferred -- effects are progressive "
              "enhancement, not render-blocking" % (name, src))
    return pp


def check_sitemap(out):
    path = out / "sitemap.xml"
    if not check(path.is_file(), "missing sitemap.xml"):
        return
    try:
        root = ET.fromstring(path.read_text(encoding="utf-8"))
    except ET.ParseError as exc:
        FAILS.append("sitemap.xml is not well-formed XML: %s" % exc)
        return
    ns = "{http://www.sitemaps.org/schemas/sitemap/0.9}"
    check(root.tag == ns + "urlset", "sitemap root tag is %r" % root.tag)
    locs = []
    for u in root.findall(ns + "url"):
        locs.append(u.findtext(ns + "loc"))
        lastmod = u.findtext(ns + "lastmod")
        check(lastmod and re.match(r"^\d{4}-\d{2}-\d{2}$", lastmod),
              "sitemap lastmod %r is not YYYY-MM-DD" % lastmod)
    check(set(locs) == set(page_url(p) for p in PAGES),
          "sitemap URLs %r do not match the generated pages" % sorted(locs))


def check_robots(out):
    path = out / "robots.txt"
    if not check(path.is_file(), "missing robots.txt"):
        return
    body = path.read_text(encoding="utf-8")
    check("User-agent: *" in body, "robots.txt has no 'User-agent: *' line")
    check("Sitemap: " + SITE_BASE + "sitemap.xml" in body,
          "robots.txt has no absolute 'Sitemap:' line")


def main():
    out = Path(sys.argv[1] if len(sys.argv) > 1 else "_site").resolve()
    if not out.is_dir():
        sys.stderr.write("no such build output directory: %s\n" % out)
        return 2

    parsed = {}
    for name in PAGES:
        pp = check_page(out, name)
        if pp is not None:
            parsed[name] = pp

    titles = dict((n, p.title.strip()) for n, p in parsed.items())
    descs = dict((n, p.meta.get("description")) for n, p in parsed.items())
    check(len(set(titles.values())) == len(titles),
          "duplicate <title> across pages: %r" % titles)
    check(len(set(descs.values())) == len(descs),
          "duplicate meta descriptions across pages")
    for name, title in titles.items():
        if name != "index.html":
            check(title.endswith("Virtual Jaguar libretro"),
                  "%s: title %r should end with the site name" % (name, title))

    check_sitemap(out)
    check_robots(out)

    if FAILS:
        sys.stderr.write("\nFAIL: check_site.py found %d problem(s):\n"
                         % len(FAILS))
        for f in FAILS:
            sys.stderr.write("  - %s\n" % f)
        return 1
    print("check_site.py: OK -- %d pages, sitemap and robots.txt verified"
          % len(parsed))
    return 0


if __name__ == "__main__":
    sys.exit(main())
