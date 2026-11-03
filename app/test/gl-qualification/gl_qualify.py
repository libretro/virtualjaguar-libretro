#!/usr/bin/env python3
"""GL and shader qualification driver (issue #1155; .github/workflows/gl-qualification.yml).

Boots the desktop app windowed once per bundled shader preset, with --shader <preset>
--screenshot <png>, and checks what one leg of CI can check without a human looking at
the window:

  * the app exits 0 before the timeout;
  * the `[gl] version=...; renderer=...` line names the renderer this leg must land on
    (llvmpipe under Xvfb, bundled Mesa after the automatic retry on Windows, Apple on macOS),
    and every extra --expect-log pattern is in the log;
  * the PNG exists, decodes and is not black;
  * the `[shader] <preset> built|refused|failed` line (shaders.cpp) says the chain was built,
    or the preset is in the leg's refused allowlist (macOS OpenGL 4.1 and arrays of arrays),
    or in known-broken.txt (presets the bundled librashader rejects on every GL; a listed
    preset that starts building fails too, so that list can only shrink);
  * the picture differs from the plain --shader off capture, unless the preset is listed in
    identical-to-plain.txt (nearest-neighbour presets at integer scale draw the same pixels).

Goldens (second pass, not blocking): with --goldens DIR, a <DIR>/<preset>.png is compared
(mean absolute channel difference) and reported; --goldens-strict turns a miss into a failure.
--refresh-goldens copies every passing capture into the golden layout under the output folder
(app/test/golden/gl/<os>/<preset>.png) for a human to commit.

Everything the run produced (screenshots, per-preset logs, results.json, the summary) lands in
--out, which the workflow uploads pass or fail.  Standard library only; run with `python3 -I`.
"""
import argparse
import concurrent.futures
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys
import time
import zlib

RENDERER_LINE = re.compile(r"^\[gl\] version=(?P<version>.*?); vendor=(?P<vendor>.*?); renderer=(?P<renderer>.*)$", re.M)
SHADER_LINE = re.compile(r"^\[shader\] (?P<path>.+?) (?P<what>built|refused|failed): (?P<detail>.*)$", re.M)

# A representative subset for PR runs when the full ~500 is too slow (--preset-set smoke).
SMOKE_PRESETS = [
    "vj-sharp-bilinear.slangp",
    "vj-scanlines.slangp",
    "slang-shaders/bilinear.slangp",
    "slang-shaders/nearest.slangp",
    "slang-shaders/crt/crt-royale.slangp",
    "slang-shaders/crt/crt-geom.slangp",
    "slang-shaders/crt/crt-easymode.slangp",
    "slang-shaders/crt/crt-lottes.slangp",
    "slang-shaders/crt/crt-hyllian.slangp",
    "slang-shaders/scanlines/scanline.slangp",
    "slang-shaders/interpolation/bicubic.slangp",
    "slang-shaders/pixel-art-scaling/pixellate.slangp",
    "slang-shaders/ntsc/ntsc-adaptive.slangp",
    "slang-shaders/misc/test-pattern-append.slangp",
]


# ---------------------------------------------------------------------------------------
# PNG: enough of a decoder for SDL_SavePNG's output (8-bit RGB/RGBA, no interlace)
# ---------------------------------------------------------------------------------------
class Png:
    def __init__(self, width, height, channels, rows):
        self.width, self.height, self.channels, self.rows = width, height, channels, rows

    @staticmethod
    def read(path):
        data = pathlib.Path(path).read_bytes()
        if data[:8] != b"\x89PNG\r\n\x1a\n":
            raise ValueError("not a PNG")
        pos, idat, ihdr = 8, [], None
        while pos + 8 <= len(data):
            length = int.from_bytes(data[pos:pos + 4], "big")
            kind = data[pos + 4:pos + 8]
            body = data[pos + 8:pos + 8 + length]
            pos += 12 + length
            if kind == b"IHDR":
                ihdr = body
            elif kind == b"IDAT":
                idat.append(body)
            elif kind == b"IEND":
                break
        if ihdr is None or not idat:
            raise ValueError("PNG without IHDR/IDAT")
        w = int.from_bytes(ihdr[0:4], "big")
        h = int.from_bytes(ihdr[4:8], "big")
        depth, ctype, interlace = ihdr[8], ihdr[9], ihdr[12]
        if depth != 8 or interlace != 0 or ctype not in (2, 6):
            raise ValueError("unsupported PNG (depth %d, colour type %d, interlace %d)" % (depth, ctype, interlace))
        ch = 4 if ctype == 6 else 3
        raw = zlib.decompress(b"".join(idat))
        stride = w * ch
        rows, prev, p = [], bytearray(stride), 0
        for _ in range(h):
            f = raw[p]
            line = bytearray(raw[p + 1:p + 1 + stride])
            p += 1 + stride
            if f == 1:
                for i in range(ch, stride):
                    line[i] = (line[i] + line[i - ch]) & 0xFF
            elif f == 2:
                for i in range(stride):
                    line[i] = (line[i] + prev[i]) & 0xFF
            elif f == 3:
                for i in range(stride):
                    a = line[i - ch] if i >= ch else 0
                    line[i] = (line[i] + ((a + prev[i]) >> 1)) & 0xFF
            elif f == 4:
                for i in range(stride):
                    a = line[i - ch] if i >= ch else 0
                    b = prev[i]
                    c = prev[i - ch] if i >= ch else 0
                    pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                    pred = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                    line[i] = (line[i] + pred) & 0xFF
            elif f != 0:
                raise ValueError("bad PNG filter %d" % f)
            rows.append(bytes(line))
            prev = line
        return Png(w, h, ch, rows)

    def rgb_stats(self):
        """(fraction of pixels that are not black, mean of max(R,G,B))."""
        lit, total, acc = 0, self.width * self.height, 0
        ch = self.channels
        for row in self.rows:
            for x in range(0, len(row), ch):
                m = max(row[x], row[x + 1], row[x + 2])
                acc += m
                if m > 8:
                    lit += 1
        return (lit / total if total else 0.0), (acc / total if total else 0.0)

    def differs_from(self, other):
        """Fraction of pixels whose RGB differs (1.0 when the sizes differ)."""
        if (self.width, self.height) != (other.width, other.height):
            return 1.0
        diff, total = 0, self.width * self.height
        ca, cb = self.channels, other.channels
        for ra, rb in zip(self.rows, other.rows):
            for x in range(self.width):
                a, b = x * ca, x * cb
                if ra[a:a + 3] != rb[b:b + 3]:
                    diff += 1
        return diff / total if total else 0.0

    def mean_abs_diff(self, other):
        if (self.width, self.height) != (other.width, other.height):
            return None
        acc, n = 0, self.width * self.height * 3
        ca, cb = self.channels, other.channels
        for ra, rb in zip(self.rows, other.rows):
            for x in range(self.width):
                a, b = x * ca, x * cb
                acc += abs(ra[a] - rb[b]) + abs(ra[a + 1] - rb[b + 1]) + abs(ra[a + 2] - rb[b + 2])
        return acc / n if n else 0.0


# ---------------------------------------------------------------------------------------
# one launch
# ---------------------------------------------------------------------------------------
def safe_name(rel):
    return rel.replace("/", "__").replace("\\", "__")


def read_list(path):
    out = set()
    if not path:
        return out
    for line in pathlib.Path(path).read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if line and not line.startswith("#"):
            out.add(line.replace("\\", "/"))
    return out


def launch(args, label, shader_arg, out_dir):
    """Run the app once.  Returns a result dict (no picture analysis yet)."""
    shots = out_dir / "shots"
    logs = out_dir / "logs"
    data = out_dir / "data" / safe_name(label)
    shots.mkdir(parents=True, exist_ok=True)
    logs.mkdir(parents=True, exist_ok=True)
    data.mkdir(parents=True, exist_ok=True)
    if args.seed_config:
        shutil.copyfile(args.seed_config, data / "config.ini")
    png = shots / (safe_name(label) + ".png")
    log = logs / (safe_name(label) + ".log")
    cmd = [str(args.exe), "--data-dir", str(data), "--shader", shader_arg,
           "--screenshot", str(png), "--screenshot-frame", str(args.frame)]
    if args.low_dpi:
        cmd.append("--low-dpi")
    cmd += args.extra_arg
    if args.content:
        cmd.append(str(args.content))
    res = {"preset": label, "cmd": cmd, "png": str(png), "log": str(log), "errors": [], "notes": []}
    t0 = time.monotonic()
    try:
        p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=args.timeout)
        text = p.stdout.decode("utf-8", "replace")
        res["exit"] = p.returncode
    except subprocess.TimeoutExpired as e:
        text = (e.stdout or b"").decode("utf-8", "replace") if isinstance(e.stdout, bytes) else ""
        res["exit"] = None
        res["errors"].append("timed out after %d s" % args.timeout)
    res["seconds"] = round(time.monotonic() - t0, 1)
    log.write_text(text, encoding="utf-8")
    if res["exit"] not in (None, 0):
        res["errors"].append("exit code %d" % res["exit"])
    # The LAST renderer line: on stock Windows the first names the GDI Generic 1.1 context the
    # app rejected before relaunching beside the bundled Mesa, whose line comes second.
    gl_lines = list(RENDERER_LINE.finditer(text))
    m = gl_lines[-1] if gl_lines else None
    res["renderer"] = m.group("renderer").strip() if m else ""
    res["gl_version"] = m.group("version").strip() if m else ""
    res["gl_lines"] = len(gl_lines)
    if not m:
        res["errors"].append("no `[gl] version=...; renderer=` line")
    elif args.expect_renderer and not re.search(args.expect_renderer, res["renderer"]):
        res["errors"].append("renderer `%s` does not match /%s/" % (res["renderer"], args.expect_renderer))
    for pat in args.expect_log:
        if not re.search(pat, text, re.M):
            res["errors"].append("log lacks /%s/" % pat)
    shader_lines = SHADER_LINE.findall(text)
    res["shader"] = [{"path": s[0], "what": s[1], "detail": s[2]} for s in shader_lines]
    return res


def analyse_png(res, off_png, identical_ok, args):
    png_path = pathlib.Path(res["png"])
    if not png_path.is_file() or png_path.stat().st_size == 0:
        res["errors"].append("no screenshot was written")
        return None
    try:
        img = Png.read(png_path)
    except Exception as e:  # noqa: BLE001 - any decode problem is a failure
        res["errors"].append("screenshot does not decode: %s" % e)
        return None
    lit, mean = img.rgb_stats()
    res["lit_fraction"] = round(lit, 4)
    res["mean_level"] = round(mean, 1)
    res["size"] = "%dx%d" % (img.width, img.height)
    if lit < 0.001:
        res["errors"].append("screenshot is black (%.2f%% lit pixels)" % (lit * 100))
    if off_png is not None and res["preset"] != "off":
        d = img.differs_from(off_png)
        res["differs_from_plain"] = round(d, 4)
        if d == 0.0 and res["preset"] not in identical_ok:
            res["errors"].append("identical to the --shader off capture (list it in identical-to-plain.txt if that is right)")
        elif d == 0.0:
            res["notes"].append("identical to the plain picture (allowed)")
    if args.goldens and res["preset"] != "off":
        g = pathlib.Path(args.goldens) / (res["preset"] + ".png")
        if g.is_file():
            try:
                gd = img.mean_abs_diff(Png.read(g))
            except Exception as e:  # noqa: BLE001
                gd = None
                res["notes"].append("golden does not decode: %s" % e)
            res["golden_mean_abs_diff"] = None if gd is None else round(gd, 3)
            if gd is None:
                msg = "golden size differs"
            elif gd > args.golden_tolerance:
                msg = "golden mean abs diff %.2f > %.2f" % (gd, args.golden_tolerance)
            else:
                msg = None
            if msg:
                (res["errors"] if args.goldens_strict else res["notes"]).append(msg)
        else:
            res["notes"].append("no golden")
    return img


def judge_shader(res, refused_ok, known_broken=frozenset()):
    """The `[shader]` line decides whether the chain really ran."""
    if res["preset"] == "off":
        if res["shader"]:
            res["errors"].append("--shader off still ran a chain: %s" % res["shader"][0])
        res["verdict"] = "plain"
        return
    whats = [s["what"] for s in res["shader"]]
    if "failed" in whats:
        d = next(s for s in res["shader"] if s["what"] == "failed")
        if res["preset"] in known_broken:
            res["verdict"] = "known-broken"
            res["notes"].append("fails to build (known-broken.txt): %s" % d["detail"])
        elif res["preset"] in refused_ok:
            # The leg's GL lacks something the preset needs (macOS 4.1: packHalf2x16 is GLSL 4.20);
            # the app says "failed" with the compiler's text because only the arrays-of-arrays case
            # has its own explanation.  Allowlisted per leg like a refusal.
            res["verdict"] = "refused"
            res["notes"].append("cannot build on this GL (allowlisted): %s" % d["detail"][:200])
        else:
            res["errors"].append("shader failed: %s" % d["detail"])
            res["verdict"] = "failed"
    elif "refused" in whats:
        d = next(s for s in res["shader"] if s["what"] == "refused")
        if res["preset"] in refused_ok:
            res["verdict"] = "refused"
            res["notes"].append("refused (allowlisted): %s" % d["detail"])
        else:
            res["errors"].append("refused and not in the allowlist: %s" % d["detail"])
            res["verdict"] = "refused"
    elif "built" in whats:
        res["verdict"] = "built"
        if res["preset"] in known_broken:
            res["errors"].append("listed in known-broken.txt but it built: drop it from that list")
        if res["preset"] in refused_ok:
            res["errors"].append("listed in this leg's refused allowlist but it built: drop it from that list")
    else:
        res["errors"].append("no `[shader] ... built` line: the chain never ran (picture not rendered by --screenshot-frame?)")
        res["verdict"] = "no-chain"


# ---------------------------------------------------------------------------------------
# main
# ---------------------------------------------------------------------------------------
def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--exe", required=True, type=pathlib.Path, help="the app executable (install tree: <prefix>/bin/virtualjaguar[.exe])")
    ap.add_argument("--shaders", required=True, type=pathlib.Path, help="the shaders/ folder beside it (first-party + slang-shaders/)")
    ap.add_argument("--out", required=True, type=pathlib.Path, help="output folder: shots/, logs/, results.json, summary.md")
    ap.add_argument("--os", required=True, choices=["linux", "windows", "macos"], help="leg name, for the summary and the golden layout")
    ap.add_argument("--content", type=pathlib.Path, default=None, help="a ROM to run (the picture the chain filters); default: no content")
    ap.add_argument("--frame", type=int, default=20, help="--screenshot-frame (UI frames before the capture)")
    ap.add_argument("--timeout", type=int, default=180, help="seconds per launch")
    ap.add_argument("--jobs", type=int, default=1, help="launches at once (each has its own --data-dir)")
    ap.add_argument("--fail-fast", type=int, default=8, help="stop after this many consecutive failed presets (0 = never): a leg whose GL or runtime is broken fails in minutes, not hours")
    ap.add_argument("--low-dpi", action="store_true", help="pass --low-dpi (a 1x window on Retina)")
    ap.add_argument("--extra-arg", action="append", default=[], help="another app argument (repeatable)")
    ap.add_argument("--expect-renderer", default="", help="regex the GL renderer string must match")
    ap.add_argument("--expect-log", action="append", default=[], help="regex that must appear in each launch's log (repeatable)")
    ap.add_argument("--refused-allowlist", default="", help="presets this leg's GL cannot build (refused, or failed to compile); one relative path per line")
    ap.add_argument("--seed-config", default="", help="a config.ini copied into every launch's data folder first (window size, ...)")
    ap.add_argument("--known-broken", default="", help="presets that must FAIL to build on every leg (librashader rejects them); one that builds is an error")
    ap.add_argument("--identical-ok", default="", help="presets allowed to draw the same pixels as the plain picture")
    ap.add_argument("--preset-set", choices=["all", "smoke"], default="all")
    ap.add_argument("--only", action="append", default=[], help="run only these presets (relative paths; repeatable)")
    ap.add_argument("--goldens", default="", help="golden folder for this leg (app/test/golden/gl/<os>)")
    ap.add_argument("--golden-tolerance", type=float, default=2.0, help="mean absolute channel difference allowed")
    ap.add_argument("--goldens-strict", action="store_true", help="a golden miss is a failure")
    ap.add_argument("--refresh-goldens", action="store_true", help="write passing captures to <out>/golden/gl/<os>/")
    ap.add_argument("--summary", default=os.environ.get("GITHUB_STEP_SUMMARY", ""), help="append the Markdown summary here too")
    args = ap.parse_args()

    args.exe = args.exe.resolve()
    args.shaders = args.shaders.resolve()
    args.out = args.out.resolve()
    if args.content:
        args.content = args.content.resolve()
    if args.seed_config and not pathlib.Path(args.seed_config).is_file():
        sys.exit("gl_qualify: --seed-config %s is not a file" % args.seed_config)
    args.out.mkdir(parents=True, exist_ok=True)
    if not args.exe.is_file():
        sys.exit("gl_qualify: %s is not a file" % args.exe)
    if not args.shaders.is_dir():
        sys.exit("gl_qualify: %s is not a folder" % args.shaders)

    presets = sorted(p.relative_to(args.shaders).as_posix() for p in args.shaders.rglob("*.slangp"))
    if not presets:
        sys.exit("gl_qualify: no .slangp under %s" % args.shaders)
    if args.only:
        wanted = {o.replace("\\", "/") for o in args.only}
        presets = [p for p in presets if p in wanted]
    elif args.preset_set == "smoke":
        presets = [p for p in SMOKE_PRESETS if p in set(presets)]
    refused_ok = read_list(args.refused_allowlist)
    identical_ok = read_list(args.identical_ok)
    known_broken = read_list(args.known_broken)

    print("gl_qualify: %s, %d presets, frame %d, %d job(s), timeout %d s" % (args.os, len(presets), args.frame, args.jobs, args.timeout), flush=True)

    # The plain picture first: everything else is compared with it.
    off = launch(args, "off", "off", args.out)
    judge_shader(off, refused_ok, known_broken)
    off_img = analyse_png(off, None, identical_ok, args)
    print("  off: exit %s, renderer `%s`, %s s%s" % (off["exit"], off["renderer"], off["seconds"],
          ("  ERRORS: " + "; ".join(off["errors"])) if off["errors"] else ""), flush=True)

    results = [off]
    if not off["errors"]:
        def one(rel):
            r = launch(args, rel, str(args.shaders / rel), args.out)
            judge_shader(r, refused_ok, known_broken)
            if r["verdict"] != "known-broken":
                analyse_png(r, off_img, identical_ok, args)
            return r
        streak, aborted = 0, False
        with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, args.jobs)) as ex:
            for r in ex.map(one, presets):
                results.append(r)
                tag = "FAIL" if r["errors"] else r["verdict"]
                print("  %-60s %-8s %5s s%s" % (r["preset"], tag, r["seconds"],
                      ("  " + "; ".join(r["errors"])) if r["errors"] else ""), flush=True)
                streak = streak + 1 if r["errors"] else 0
                if args.fail_fast and streak >= args.fail_fast:
                    aborted = True
                    print("gl_qualify: %d presets failed in a row; stopping (--fail-fast %d)" % (streak, args.fail_fast), flush=True)
                    ex.shutdown(wait=True, cancel_futures=True)
                    break
        if aborted:
            results.append({"preset": "(aborted)", "errors": ["%d consecutive failures: the remaining %d presets were not run" % (streak, len(presets) - (len(results) - 1))],
                            "notes": [], "verdict": "aborted", "exit": None, "seconds": 0, "log": "", "png": "", "shader": []})
    else:
        print("gl_qualify: the plain launch failed; not running the presets", flush=True)

    # Goldens to commit (a human reviews them; nothing is written into the repository here).
    if args.refresh_goldens:
        gdir = args.out / "golden" / "gl" / args.os
        for r in results:
            if r["preset"] != "off" and not r["errors"] and r["verdict"] == "built":
                dst = gdir / (r["preset"] + ".png")
                dst.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(r["png"], dst)

    failed = [r for r in results if r["errors"]]
    built = [r for r in results if r["verdict"] == "built" and not r["errors"]]
    refused = [r for r in results if r["verdict"] == "refused" and not r["errors"]]
    broken = [r for r in results if r["verdict"] == "known-broken" and not r["errors"]]
    (args.out / "results.json").write_text(json.dumps({
        "os": args.os, "exe": str(args.exe), "frame": args.frame, "presets": len(presets),
        "built": len(built), "refused": sorted(r["preset"] for r in refused),
        "known_broken": sorted(r["preset"] for r in broken), "failed": len(failed),
        "results": results}, indent=1), encoding="utf-8")

    md = ["## GL qualification: %s" % args.os, "",
          "| | |", "|---|---|",
          "| Executable | `%s` |" % args.exe,
          "| Renderer (plain launch) | `%s` (OpenGL %s) |" % (off["renderer"], off["gl_version"]),
          "| Presets | %d (%s) |" % (len(presets), args.preset_set if not args.only else "selected"),
          "| Built | %d |" % len(built),
          "| Refused (allowlisted) | %d |" % len(refused),
          "| Known broken (allowlisted) | %d |" % len(broken),
          "| **Failed** | **%d** |" % len(failed), ""]
    if refused:
        md += ["<details><summary>Refused presets (allowlisted)</summary>", ""]
        md += ["- `%s` — %s" % (r["preset"], r["notes"][0] if r["notes"] else "") for r in refused]
        md += ["", "</details>", ""]
    if broken:
        md += ["<details><summary>Known-broken presets (allowlisted, fail to build everywhere)</summary>", ""]
        md += ["- `%s` — %s" % (r["preset"], r["notes"][0] if r["notes"] else "") for r in broken]
        md += ["", "</details>", ""]
    if failed:
        md += ["### Failures", ""]
        for r in failed:
            md += ["**`%s`** (exit %s, %s s)" % (r["preset"], r["exit"], r["seconds"])]
            md += ["- %s" % e for e in r["errors"]]
            try:
                if not r["log"]:
                    raise OSError("no log")
                tail = pathlib.Path(r["log"]).read_text(encoding="utf-8", errors="replace").splitlines()
                gl = [l for l in tail if l.startswith("[gl]") or l.startswith("[shader]")]
                md += ["", "```", *gl[-12:], "```", ""]
            except OSError:
                md += [""]
    notes = [r for r in results if [n for n in r["notes"] if n != "no golden"] and not r["errors"] and r["verdict"] == "built"]
    if notes:
        md += ["<details><summary>Notes (%d)</summary>" % len(notes), ""]
        md += ["- `%s`: %s" % (r["preset"], "; ".join(n for n in r["notes"] if n != "no golden")) for r in notes]
        md += ["", "</details>", ""]
    summary = "\n".join(md) + "\n"
    (args.out / "summary.md").write_text(summary, encoding="utf-8")
    if args.summary:
        with open(args.summary, "a", encoding="utf-8") as f:
            f.write(summary)
    print("gl_qualify: %d built, %d refused, %d failed" % (len(built), len(refused), len(failed)), flush=True)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
