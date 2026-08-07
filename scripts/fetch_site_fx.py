#!/usr/bin/env python3
"""Deploy-time fetch + compile of the Canvas UI hero effects.

Canvas UI (https://canvasui.dev, github.com/DavidHDev/canvas-ui) is licensed
MIT + Commons Clause: use, copying, modification and publication "as part of
an application, website, or product" are expressly granted; redistributing
the components themselves (alone, bundled, or ported) is not.  Therefore the
component source is **never committed to this repository**.  Instead, this
script runs at DEPLOY TIME, gated behind VJ_SITE_FX_FETCH=1:

  1. downloads the vanilla TypeScript sources for the VHS and ParticleReveal
     components from the pinned upstream commit,
  2. compiles them with esbuild (via `npx --yes esbuild`) into one IIFE
     bundle exposing window.CanvasUIVHS and window.CanvasUIParticle,
  3. prepends the upstream license text as a comment header (the license
     requires the notice to accompany copies) plus provenance,
  4. writes _site/assets/js/canvas-ui-fx.js and injects its <script> tag
     into _site/index.html ahead of the committed fallback scripts.

Failure contract: without VJ_SITE_FX_FETCH=1 this is a silent no-op; with it,
ANY failure (network, npx missing, compile error, unexpected content, missing
build output) prints a loud warning and exits 0 leaving the site untouched —
the committed original crt-fx.js then drives the hero, so a broken fetch can
never blank the page or fail a deploy.  Run AFTER build_site.py and BEFORE
check_site.py:

  python3 scripts/build_site.py
  VJ_SITE_FX_FETCH=1 python3 scripts/fetch_site_fx.py _site
  python3 scripts/check_site.py _site
"""

import os
import subprocess
import sys
import tempfile
import urllib.request
from pathlib import Path

PIN = "880de315dd23d8add253575655ddc57f2160a19d"
RAW = "https://raw.githubusercontent.com/DavidHDev/canvas-ui/" + PIN + "/"
SOURCES = {
    "VHSVanilla.ts": "src/lib/VHS/VHSVanilla.ts",
    "ParticleRevealVanilla.ts": "src/lib/ParticleReveal/ParticleRevealVanilla.ts",
}
LICENSE_PATH = "LICENSE.md"

ENTRY = """\
import * as vhs from "./VHSVanilla";
import * as particle from "./ParticleRevealVanilla";
(window as any).CanvasUIVHS = vhs;
(window as any).CanvasUIParticle = particle;
"""

OUT_NAME = "canvas-ui-fx.js"
INJECT_BEFORE = '<script src="assets/js/crt-fx.js" defer></script>'
TAG = '<script src="assets/js/%s" defer></script>' % OUT_NAME


def warn(msg):
    sys.stderr.write("\nWARNING: fetch_site_fx.py: %s\n" % msg)
    sys.stderr.write("Falling back to the committed crt-fx.js hero effect; "
                     "the site remains fully functional.\n")


def fetch(path):
    with urllib.request.urlopen(RAW + path, timeout=30) as resp:
        return resp.read().decode("utf-8")


def main():
    out = Path(sys.argv[1] if len(sys.argv) > 1 else "_site").resolve()

    if os.environ.get("VJ_SITE_FX_FETCH") != "1":
        print("fetch_site_fx.py: VJ_SITE_FX_FETCH not set -- skipping "
              "(committed crt-fx.js drives the hero). Local builds stay "
              "offline by design.")
        return 0

    index = out / "index.html"
    if not index.is_file():
        warn("no %s -- run build_site.py first" % index)
        return 0

    try:
        srcs = {}
        for name, path in SOURCES.items():
            srcs[name] = fetch(path)
        license_text = fetch(LICENSE_PATH)
    except Exception as exc:
        warn("download failed: %s" % exc)
        return 0

    # Sanity: refuse to publish something that is not what we pinned.
    if "createVHS" not in srcs["VHSVanilla.ts"] or \
       "createParticleReveal" not in srcs["ParticleRevealVanilla.ts"] or \
       "Commons Clause" not in license_text:
        warn("fetched sources do not look like the pinned components")
        return 0

    try:
        with tempfile.TemporaryDirectory() as tmp:
            tmpp = Path(tmp)
            for name, text in srcs.items():
                (tmpp / name).write_text(text, encoding="utf-8")
            (tmpp / "entry.ts").write_text(ENTRY, encoding="utf-8")
            proc = subprocess.run(
                ["npx", "--yes", "esbuild", str(tmpp / "entry.ts"),
                 "--bundle", "--format=iife", "--target=es2019",
                 "--log-level=warning"],
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=300)
            if proc.returncode != 0:
                warn("esbuild failed:\n%s"
                     % proc.stderr.decode("utf-8", "replace")[-2000:])
                return 0
            js = proc.stdout.decode("utf-8")
    except Exception as exc:
        warn("compile step failed: %s" % exc)
        return 0

    if "CanvasUIVHS" not in js or len(js) < 1000:
        warn("compiled bundle looks wrong (%d bytes)" % len(js))
        return 0

    header = (
        "/*!\n"
        " * Canvas UI VHS + ParticleReveal (vanilla) -- fetched and compiled\n"
        " * at deploy time by scripts/fetch_site_fx.py.  NOT part of the\n"
        " * repository; served first-party as part of this website, as the\n"
        " * license expressly permits.\n"
        " *\n"
        " * Upstream: https://github.com/DavidHDev/canvas-ui @ %s\n"
        " * Files:    %s\n"
        " *\n"
        " * Upstream license (reproduced as required):\n"
        " *\n"
        "%s"
        " */\n"
    ) % (
        PIN,
        ", ".join(SOURCES.values()),
        "".join(" * %s\n" % line.replace("*/", "*\\/")
                for line in license_text.splitlines()),
    )

    js_dir = out / "assets" / "js"
    js_dir.mkdir(parents=True, exist_ok=True)
    (js_dir / OUT_NAME).write_text(header + js, encoding="utf-8")

    html = index.read_text(encoding="utf-8")
    if TAG in html:
        pass  # already injected (re-run)
    elif INJECT_BEFORE in html:
        html = html.replace(INJECT_BEFORE, TAG + "\n" + INJECT_BEFORE, 1)
        index.write_text(html, encoding="utf-8")
    else:
        (js_dir / OUT_NAME).unlink()
        warn("could not find the fallback script tag in index.html to "
             "inject ahead of")
        return 0

    print("fetch_site_fx.py: wrote %s (%d bytes) and injected its tag "
          "into index.html" % (js_dir / OUT_NAME, len(header + js)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
