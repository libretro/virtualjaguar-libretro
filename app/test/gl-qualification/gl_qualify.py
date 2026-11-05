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

Everything the run produced (screenshots, per-preset logs, results.json, times.tsv, the summary)
lands in --out, which the workflow uploads pass or fail.  Standard library only; run with `python3 -I`.

A slow leg (hosted macOS, Apple's software renderer) splits the list with --shard I/N (preset
index modulo N; every shard also captures `off`, its own baseline) and a later job joins the
shards with --merge DIR... into one summary.  --known-failures FILE --leg NAME reads the leg's
`<leg> slow <measured> <preset>` lines: a listed preset is stopped at --slow-threshold seconds and
counted as a tracked known failure; an unlisted one fails only if it does not render within
--timeout, and one that renders but takes longer than --slow-threshold is a notice (with the line
to paste into that file) counted in the summary.
"""
import argparse
import concurrent.futures
import json
import os
import pathlib
import re
import shutil
import signal
import subprocess
import sys
import time
import zlib

RENDERER_LINE = re.compile(r"^\[gl\] version=(?P<version>.*?); vendor=(?P<vendor>.*?); renderer=(?P<renderer>.*)$", re.M)
SHADER_LINE = re.compile(r"^\[shader\] (?P<path>.+?) (?P<what>built|refused|failed): (?P<detail>.*)$", re.M)

# A representative subset for a quick run (--preset-set smoke): only workflow_dispatch's preset_set=smoke
# selects it; PR, nightly and release runs render all ~500.
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


def png_complete(path):
    """The PNG is on disk and ends with its IEND chunk (SDL_SavePNG writes it last)."""
    try:
        with open(path, "rb") as f:
            f.seek(-12, 2)
            return f.read(12)[4:8] == b"IEND"
    except (OSError, ValueError):
        return False


def kill_tree(proc, grace=0):
    """Kill the launch and whatever it spawned (on Windows the parent waits on the Mesa child).

    grace > 0 (POSIX): SIGTERM first and that many seconds to quit through the app's own teardown
    (on macOS a GL process killed outright left the next window creation hanging).  Returns False
    when something of the process group is still alive afterwards."""
    if os.name == "nt":
        subprocess.run(["taskkill", "/T", "/F", "/PID", str(proc.pid)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            proc.wait(timeout=30)
        except subprocess.TimeoutExpired:
            pass
        return True
    def group_alive():
        try:
            os.killpg(proc.pid, 0)
            return True
        except (ProcessLookupError, PermissionError):
            return False
    if grace > 0:
        try:
            os.killpg(proc.pid, signal.SIGTERM)
        except (ProcessLookupError, PermissionError):
            pass
        t_end = time.monotonic() + grace
        while time.monotonic() < t_end and (proc.poll() is None or group_alive()):
            time.sleep(0.25)
    try:
        os.killpg(proc.pid, signal.SIGKILL)   # its own session (start_new_session): the whole group
    except (ProcessLookupError, PermissionError):
        if proc.poll() is None:
            proc.kill()
    try:
        proc.wait(timeout=30)
    except subprocess.TimeoutExpired:
        pass
    t_end = time.monotonic() + 5
    while group_alive() and time.monotonic() < t_end:
        time.sleep(0.25)
    return not group_alive()


# macOS: Apple's GL compiles shaders in a launchd service (CVMServer, com.apple.cvmsServ, with CVMCompiler
# workers).  Run 41957577821's stack samples of every launch that never got a window end in
# CGLChoosePixelFormat -> cvms_connection_create -> xpc_connection_send_message_with_reply_sync: the service
# did not answer, apparently still busy with a heavy chain's compile for a launch killed at the time limit
# (three runs stalled ~15 min in the vectorscale/xbr stretch of the list).  --reset-cvms kills it before the
# retry; launchd starts a fresh one on the next connection.  VJ_GLQ_CVMS_RESET overrides the command (tests).
CVMS_RESET = ["sudo", "-n", "/usr/bin/killall", "-9", "CVMCompiler", "CVMServer"]


def reset_cvms():
    cmd = os.environ.get("VJ_GLQ_CVMS_RESET", "").split() or CVMS_RESET
    try:
        p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=30)
        return "restarted Apple's GL compile service before the retry (`%s`: exit %d%s)" % (
            " ".join(cmd), p.returncode, (", " + p.stdout.decode(errors="replace").strip()[:120]) if p.stdout.strip() else "")
    except (OSError, subprocess.TimeoutExpired) as e:
        return "could not restart Apple's GL compile service (%s)" % e


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
    if args.emu_frames:
        cmd += ["--screenshot-emu-frames", str(args.emu_frames)]
    if args.low_dpi:
        cmd.append("--low-dpi")
    cmd += args.extra_arg
    if args.content:
        cmd.append(str(args.content))
    res = {"preset": label, "cmd": cmd, "png": str(png), "log": str(log), "errors": [], "notes": []}
    slow = args.slow_listed.get(label)
    limit = args.slow_threshold if (slow is not None and args.slow_threshold) else args.timeout
    t0 = time.monotonic()
    # stdout+stderr straight to the log file (no pipe to drain), the process polled: a leg with a
    # tracked exit hang (--known-exit-hang) is given a few seconds after its screenshot is complete,
    # then its process tree is killed and the launch counts, with a note, instead of timing out.
    with open(log, "wb") as lf:
        proc = subprocess.Popen(cmd, stdout=lf, stderr=subprocess.STDOUT, start_new_session=(os.name != "nt"))
        shot_done_at = None
        while True:
            code = proc.poll()
            if code is not None:
                res["exit"] = code
                break
            now = time.monotonic()
            if now - t0 > limit:
                if sys.platform == "darwin" and shutil.which("sample") and "[gl]" not in log.read_text(errors="replace"):
                    # No window yet: where the app is stuck (twice a run, a hosted Mac stopped giving every
                    # launch a window for ~15 min; the logs end before the window is created).
                    sample = log.with_name(log.stem + ".sample.txt")
                    if sample.exists():   # the retry's sample beside the first launch's
                        sample = log.with_name(log.stem + ".retry.sample.txt")
                    try:
                        subprocess.run(["sample", str(proc.pid), "3", "-file", str(sample)], stdout=subprocess.DEVNULL,
                                       stderr=subprocess.DEVNULL, timeout=60)
                        res["notes"].append("no `[gl]` line at the timeout: stack sample in logs/%s" % sample.name)
                    except (OSError, subprocess.TimeoutExpired):
                        pass
                if not kill_tree(proc, args.graceful_kill):
                    res["leftover"] = True
                    res["notes"].append("part of the process group was still alive after the kill")
                res["exit"] = None
                if slow is not None:
                    res["slow_capped"] = limit   # judged in judge_slow: a tracked known failure
                else:
                    res["timed_out"] = limit
                    res["errors"].append("timed out after %d s" % limit)
                break
            if args.known_exit_hang:
                if shot_done_at is None and png_complete(png):
                    shot_done_at = now
                elif shot_done_at is not None and now - shot_done_at > args.hang_grace:
                    kill_tree(proc, args.graceful_kill)
                    res["exit"] = None
                    res["notes"].append("did not exit %d s after its screenshot; killed (known failure: %s)"
                                        % (args.hang_grace, args.known_exit_hang))
                    res["known_exit_hang"] = True
                    break
            time.sleep(0.25)
    res["seconds"] = round(time.monotonic() - t0, 1)
    text = log.read_text(encoding="utf-8", errors="replace")
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


def judge_slow(res, args):
    """The leg's time limit (--slow-threshold) and its `slow` known failures.

    Listed and stopped at the limit: a tracked known failure, `slow-built` when the chain was built
    (it compiled on this GL, it just did not finish a frame in time) or `slow-nochain` when it was
    not.  Listed but finished in time: checked like any other preset, noted.  Unlisted and timed out
    (no picture within --timeout): a failure, flagged `slow_unlisted` so --fail-fast does not count
    it.  Unlisted, rendered, but over the limit: `slow_drift`, a notice and a summary count, not a
    failure.  No `[gl]` line at all is never "slow": the window did not come up."""
    if res["preset"] == "off" or not args.slow_threshold:
        return
    listed = args.slow_listed.get(res["preset"])
    has_gl = res.get("gl_lines", 0) > 0
    built = any(s["what"] == "built" for s in res["shader"])
    if res.get("slow_capped") is not None:
        if not has_gl:
            res["errors"].append("no `[gl]` line within %d s: the window never came up (not counted as slow)" % res["slow_capped"])
            return
        if res.get("verdict") not in ("built", "no-chain"):
            return   # failed / refused / known-broken before the limit: that verdict stands
        res["errors"] = [e for e in res["errors"] if not e.startswith("no `[shader] ... built` line")]
        res["verdict"] = "slow-built" if built else "slow-nochain"
        res["notes"].append("%s within %d s on this leg (known failure: slow, measured %s)"
                            % ("chain built, not rendered" if built else "no chain", res["slow_capped"], listed))
        return
    if listed is not None and not res["errors"]:
        res["notes"].append("listed as slow (measured %s) but rendered in %s s: drop it from known-failures.txt"
                            % (listed, res["seconds"]))
        res["stale_slow"] = True
        return
    # The gate (option b, PR #1160): an unlisted preset FAILS only when it does not render within --timeout.
    # One that renders but takes longer than --slow-threshold is drift, not a failure: hosted-Mac times swing
    # ~2x between runs (sabr-hybrid-deposterize: 131 s, 91 s, 229 s), so a hard 120 s line would turn up new
    # failures every run.  Drift is a GitHub notice with the line to paste and a count in the summary.
    if listed is None and has_gl and res.get("timed_out"):
        res["slow_unlisted"] = True
    elif listed is None and built and not res["errors"] and res["seconds"] > args.slow_threshold:
        res["slow_drift"] = True
        res["notes"].append("rendered, but took %s s, over this leg's %d s slow limit: consider listing it"
                            % (res["seconds"], args.slow_threshold))


def read_known_slow(path, leg):
    """`<leg> slow <measured> <preset path, may contain spaces>` lines of known-failures.txt."""
    out = {}
    if not path:
        return out
    for line in pathlib.Path(path).read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split(None, 3)
        if len(parts) == 4 and parts[0] == leg and parts[1] == "slow":
            out[parts[3]] = parts[2]
    return out


def slow_list_statement(listed, leg, limit):
    """One plain sentence on what the leg's `slow` list leaves unchecked, with the measured times
    (a `>240s` entry timed out: its time is a lower bound, so it is counted apart, not averaged in)."""
    if not listed:
        return ""
    done, over = [], 0
    for v in listed.values():
        m = re.match(r"(>?)(\d+(?:\.\d+)?)s", v)
        if m and m.group(1):
            over += 1
        elif m:
            done.append(float(m.group(2)))
    done.sort()
    parts = []
    if done:
        n = len(done)
        med = done[n // 2] if n % 2 else (done[n // 2 - 1] + done[n // 2]) / 2
        parts.append("%d rendered in %d-%d s, median %d s" % (n, done[0], done[-1], med))
    if over:
        parts.append("%d timed out at 240 s without a picture" % over)
    return ("**%d presets are listed `%s slow` in known-failures.txt: each is stopped at %d s, and one stopped there "
            "is NOT rendered or checked on this leg** (when measured, %s).  A listed preset that renders within the "
            "limit is checked like any other.  An unlisted preset fails only when it does not render within the "
            "timeout; one that renders but takes over %d s is a notice, counted below." % (
                len(listed), leg, limit, "; ".join(parts) or "no usable time", limit))


def slow_line(leg, r):
    """The known-failures.txt line for a slow preset, measured in this run."""
    return "%s slow %s@%s %s" % (leg, (">%ds" % r["timed_out"]) if r.get("timed_out") else ("%ss" % r["seconds"]),
                                 os.environ.get("GITHUB_RUN_ID", "local"), r["preset"])


def times_tsv(results):
    rows = ["preset\tverdict\tseconds\tstatus"]
    for r in results:
        if r["preset"] == "(aborted)":
            continue
        status = ("slow-capped" if r.get("slow_capped") is not None else
                  "timed-out" if r.get("timed_out") else "failed" if r["errors"] else "ok")
        rows.append("%s\t%s\t%s\t%s" % (r["preset"], r.get("verdict", ""), r.get("seconds", ""), status))
    return "\n".join(rows) + "\n"


def write_report(out, meta, results, summary_path):
    """summary.md (+ the job summary), times.tsv; returns the number of failed entries."""
    off = next((r for r in results if r["preset"] == "off"), {"renderer": "", "gl_version": ""})
    failed = [r for r in results if r["errors"]]
    built = [r for r in results if r.get("verdict") == "built" and not r["errors"]]
    refused = [r for r in results if r.get("verdict") == "refused" and not r["errors"]]
    broken = [r for r in results if r.get("verdict") == "known-broken" and not r["errors"]]
    hung = [r for r in results if r.get("known_exit_hang")]
    slow_built = [r for r in results if r.get("verdict") == "slow-built" and not r["errors"]]
    slow_nochain = [r for r in results if r.get("verdict") == "slow-nochain" and not r["errors"]]
    unlisted = [r for r in results if r.get("slow_unlisted")]
    drift = [r for r in results if r.get("slow_drift")]
    stale = [r for r in results if r.get("stale_slow")]
    retried = [r for r in results if r.get("retried")]
    cvms = [r for r in results if r.get("cvms_reset")]
    leftover = [r for r in results if r.get("leftover")]
    (out / "times.tsv").write_text(times_tsv(results), encoding="utf-8")
    md = ["## GL qualification: %s" % meta["title"], "",
          "| | |", "|---|---|",
          "| Executable | `%s` |" % meta["exe"],
          "| Renderer (plain launch) | `%s` (OpenGL %s) |" % (off.get("renderer", ""), off.get("gl_version", "")),
          "| Presets | %d (%s) |" % (meta["presets"], meta["set"]),
          "| Built | %d |" % len(built),
          "| Refused (allowlisted) | %d |" % len(refused),
          "| Known broken (allowlisted) | %d |" % len(broken),
          "| Did not exit (known failure%s) | %d |" % ((": " + meta["known_exit_hang"]) if meta.get("known_exit_hang") else "", len(hung))]
    if meta.get("slow_threshold"):
        md += ["| Slow, chain built, not rendered in %d s (known failure) | %d |" % (meta["slow_threshold"], len(slow_built)),
               "| Slow, no chain in %d s (known failure) | %d |" % (meta["slow_threshold"], len(slow_nochain)),
               "| Unlisted, rendered but over %d s (notice, not a failure) | %d |" % (meta["slow_threshold"], len(drift))]
    md += ["| **Failed** | **%d** |" % len(failed), ""]
    if meta.get("slow_statement"):
        md += [meta["slow_statement"] + " **This run: %d stopped at the limit and not rendered, %d rendered in time.**" % (
            len(slow_built) + len(slow_nochain), len(stale)), ""]
    if meta.get("settings"):
        md += ["Settings: %s" % meta["settings"], ""]
    if retried or leftover:
        md += ["Retried after no `[gl]` line: %d (%d of them after restarting Apple's GL compile service, %d of those "
               "then got a window); process group alive after a kill: %d"
               % (len(retried), len(cvms), sum(1 for r in cvms if r.get("gl_lines", 0) > 0), len(leftover)), ""]
    if refused:
        md += ["<details><summary>Refused presets (allowlisted)</summary>", ""]
        md += ["- `%s` — %s" % (r["preset"], r["notes"][0] if r["notes"] else "") for r in refused]
        md += ["", "</details>", ""]
    if broken:
        md += ["<details><summary>Known-broken presets (allowlisted, fail to build everywhere)</summary>", ""]
        md += ["- `%s` — %s" % (r["preset"], r["notes"][0] if r["notes"] else "") for r in broken]
        md += ["", "</details>", ""]
    if slow_built or slow_nochain:
        md += ["<details><summary>Slow presets (known failures: stopped at %d s, not rendered)</summary>" % meta["slow_threshold"], ""]
        md += ["- `%s` — %s" % (r["preset"], r["notes"][0] if r["notes"] else "") for r in slow_built + slow_nochain]
        md += ["", "</details>", ""]
    if unlisted:
        md += ["### Unlisted presets that did not render", "",
               "The chain was built but no picture came within the timeout: a failure.  If this leg simply cannot "
               "render them in time, list them in `app/test/gl-qualification/known-failures.txt`:", "", "```"]
        md += [slow_line(meta["leg"], r) for r in unlisted]
        md += ["```", ""]
    if drift:
        md += ["<details><summary>Unlisted presets that rendered but took over %d s (%d, notices)</summary>"
               % (meta["slow_threshold"], len(drift)), "",
               "Not failures (the gate fails only a preset that does not render within the timeout); lines to "
               "paste if they keep drifting:", "", "```"]
        md += [slow_line(meta["leg"], r) for r in drift]
        md += ["```", "", "</details>", ""]
    if stale:
        md += ["<details><summary>Listed as slow but rendered in time (%d): drop these entries</summary>" % len(stale), ""]
        md += ["- `%s` — %s s" % (r["preset"], r["seconds"]) for r in stale]
        md += ["", "</details>", ""]
    if failed:
        md += ["### Failures", ""]
        for r in failed:
            md += ["**`%s`** (exit %s, %s s)" % (r["preset"], r.get("exit"), r.get("seconds"))]
            md += ["- %s" % e for e in r["errors"]]
            try:
                if not r.get("log"):
                    raise OSError("no log")
                tail = pathlib.Path(r["log"]).read_text(encoding="utf-8", errors="replace").splitlines()
                gl = [l for l in tail if l.startswith("[gl]") or l.startswith("[shader]")]
                md += ["", "```", *gl[-12:], "```", ""]
            except OSError:
                md += [""]
    timed = sorted((r for r in results if isinstance(r.get("seconds"), (int, float)) and r["preset"] not in ("off", "(aborted)", "(merge)")),
                   key=lambda r: -r["seconds"])[:25]
    if timed:
        md += ["<details><summary>Slowest launches</summary>", "", "| Preset | Seconds | Verdict |", "|---|---|---|"]
        md += ["| `%s` | %s | %s |" % (r["preset"], r["seconds"], "FAIL" if r["errors"] else r.get("verdict", "")) for r in timed]
        md += ["", "</details>", ""]
    def visible_notes(r):
        return [n for n in r["notes"] if n != "no golden" and not n.startswith("did not exit")]
    notes = [r for r in results if visible_notes(r) and not r["errors"] and r.get("verdict") == "built" and not r.get("stale_slow")]
    if notes:
        md += ["<details><summary>Notes (%d)</summary>" % len(notes), ""]
        md += ["- `%s`: %s" % (r["preset"], "; ".join(visible_notes(r))) for r in notes]
        md += ["", "</details>", ""]
    summary = "\n".join(md) + "\n"
    (out / "summary.md").write_text(summary, encoding="utf-8")
    if summary_path:
        with open(summary_path, "a", encoding="utf-8") as f:
            f.write(summary)
    print("gl_qualify: %d built, %d refused, %d slow (known), %d failed"
          % (len(built), len(refused), len(slow_built) + len(slow_nochain), len(failed)), flush=True)
    return len(failed)


def merge_main(args):
    """--merge: one summary for a leg split with --shard.  Fails unless every shard is there and
    the shards together ran every preset exactly once."""
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    shards, problems = {}, []
    for d in args.merge:
        p = pathlib.Path(d) / "results.json"
        if not p.is_file():
            problems.append("`%s` has no results.json (the shard did not get as far as its preset loop)" % d)
            continue
        j = json.loads(p.read_text(encoding="utf-8"))
        idx, n = (int(x) for x in j.get("shard", "0/1").split("/"))
        if args.expect_shards and n != args.expect_shards:
            problems.append("`%s` is shard %d/%d, expected N=%d" % (d, idx, n, args.expect_shards))
        if idx in shards:
            problems.append("shard %d appears twice" % idx)
        for r in j["results"]:   # each log as downloaded, beside this shard's results.json
            if r.get("log"):
                r["log"] = str(pathlib.Path(d) / "logs" / pathlib.PurePath(r["log"]).name)
        shards[idx] = j
    want = args.expect_shards or (max(shards) + 1 if shards else 1)
    for i in range(want):
        if i not in shards:
            problems.append("shard %d/%d is missing" % (i, want))
    results, seen = [], {}
    total = None
    for i in sorted(shards):
        j = shards[i]
        total = j.get("presets_total", total)
        for r in j["results"]:
            if r["preset"] == "off":
                if i == 0 or r["errors"]:
                    r = dict(r)
                    if i != 0:
                        r["preset"] = "off (shard %d)" % i
                    results.append(r)
                continue
            if r["preset"] != "(aborted)":
                if r["preset"] in seen:
                    problems.append("`%s` ran in shards %d and %d" % (r["preset"], seen[r["preset"]], i))
                seen[r["preset"]] = i
            results.append(r)
        missing = sorted(set(j.get("selected", [])) - {r["preset"] for r in j["results"]})
        if missing:
            problems.append("shard %d did not run %d of its presets (aborted or cancelled)" % (i, len(missing)))
    if total is not None and len(seen) != total and not problems:
        problems.append("the shards ran %d presets, the list has %d" % (len(seen), total))
    for p in problems:
        results.append({"preset": "(merge)", "errors": [p], "notes": [], "verdict": "merge", "exit": None,
                        "seconds": 0, "log": "", "png": "", "shader": []})
    first = shards[min(shards)] if shards else {}
    meta = {"title": "%s (%d shards)" % (args.os, want), "exe": first.get("exe", ""), "presets": total or 0,
            "set": first.get("preset_set", ""), "known_exit_hang": first.get("known_exit_hang", ""),
            "slow_threshold": first.get("slow_threshold", 0), "leg": first.get("leg", args.os),
            "settings": first.get("settings", "")}
    if args.known_failures and meta["slow_threshold"]:
        meta["slow_statement"] = slow_list_statement(read_known_slow(args.known_failures, meta["leg"]),
                                                     meta["leg"], meta["slow_threshold"])
    (out / "results.json").write_text(json.dumps({"os": args.os, "shards": want, "presets_total": total,
                                                  "results": results}, indent=1), encoding="utf-8")
    return 1 if write_report(out, meta, results, args.summary) else 0


# ---------------------------------------------------------------------------------------
# main
# ---------------------------------------------------------------------------------------
def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--exe", type=pathlib.Path, help="the app executable (install tree: <prefix>/bin/virtualjaguar[.exe])")
    ap.add_argument("--shaders", type=pathlib.Path, help="the shaders/ folder beside it (first-party + slang-shaders/)")
    ap.add_argument("--out", required=True, type=pathlib.Path, help="output folder: shots/, logs/, results.json, summary.md")
    ap.add_argument("--os", required=True, choices=["linux", "windows", "macos"], help="leg name, for the summary and the golden layout")
    ap.add_argument("--content", type=pathlib.Path, default=None, help="a ROM to run (the picture the chain filters); default: no content")
    ap.add_argument("--frame", type=int, default=20, help="--screenshot-frame (UI frames before the capture)")
    ap.add_argument("--emu-frames", type=int, default=0,
                    help="--screenshot-emu-frames: the capture also waits until the core has shown this many frames "
                         "(the picture is the game's, however fast or crowded the host)")
    ap.add_argument("--timeout", type=int, default=180, help="seconds per launch")
    ap.add_argument("--jobs", type=int, default=1, help="launches at once (each has its own --data-dir)")
    ap.add_argument("--known-exit-hang", default="", help="this leg's app is known not to exit (cite the issue): a launch with a complete screenshot is killed after --hang-grace seconds and passes with a note")
    ap.add_argument("--hang-grace", type=int, default=5, help="seconds a launch may keep running after its screenshot before --known-exit-hang kills it")
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
    ap.add_argument("--shard", default="0/1", help="I/N: run the presets whose index modulo N is I (plus `off`, the shard's baseline)")
    ap.add_argument("--known-failures", default="", help="known-failures.txt: this leg's `slow` lines (--leg)")
    ap.add_argument("--leg", default="", help="this leg's name in known-failures.txt (macos-arm64, ...)")
    ap.add_argument("--slow-threshold", type=int, default=0,
                    help="seconds: a listed `slow` preset is stopped here and counted as a known failure; an unlisted one "
                         "that renders but takes longer is a notice, and fails only if it does not render within "
                         "--timeout (0 = no limit beyond --timeout)")
    ap.add_argument("--graceful-kill", type=int, default=0, help="POSIX: SIGTERM and this many seconds before SIGKILL")
    ap.add_argument("--retry-no-gl", action="store_true", help="launch again (once) a preset whose log has no `[gl]` line")
    ap.add_argument("--reset-cvms", action="store_true",
                    help="macOS hosted runners: before that retry, kill Apple's GL compile service (CVMServer/CVMCompiler, "
                         "sudo -n) so the next launch gets a fresh one")
    ap.add_argument("--merge", nargs="+", default=[], help="join these shard output folders into --out (no launches)")
    ap.add_argument("--expect-shards", type=int, default=0, help="with --merge: how many shards there must be")
    args = ap.parse_args()
    if args.merge:
        return merge_main(args)
    if not args.exe or not args.shaders:
        sys.exit("gl_qualify: --exe and --shaders are required")
    try:
        shard_i, shard_n = (int(x) for x in args.shard.split("/"))
        assert 0 <= shard_i < shard_n
    except (ValueError, AssertionError):
        sys.exit("gl_qualify: --shard wants I/N with 0 <= I < N, got %r" % args.shard)
    args.slow_listed = read_known_slow(args.known_failures, args.leg) if args.leg else {}

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
    presets_total = len(presets)
    presets = [p for k, p in enumerate(presets) if k % shard_n == shard_i]
    refused_ok = read_list(args.refused_allowlist)
    identical_ok = read_list(args.identical_ok)
    known_broken = read_list(args.known_broken)

    print("gl_qualify: %s, %d presets (shard %s of %d), frame %d, %d job(s), timeout %d s, %d listed slow"
          % (args.os, len(presets), args.shard, presets_total, args.frame, args.jobs, args.timeout,
             sum(1 for p in presets if p in args.slow_listed)), flush=True)

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
            if args.retry_no_gl and r.get("gl_lines", 0) == 0:
                # The window never came up: once more after a pause, the first attempt kept in the result.
                first = {k: r[k] for k in ("seconds", "errors", "exit")}
                try:
                    os.replace(r["log"], r["log"] + ".first")
                except OSError:
                    pass
                reset_note = reset_cvms() if args.reset_cvms else ""
                time.sleep(10)
                r = launch(args, rel, str(args.shaders / rel), args.out)
                r["retried"] = first
                if reset_note:
                    r["cvms_reset"] = True
                    r["notes"].append(reset_note)
                r["notes"].append("retried: the first launch had no `[gl]` line (%s)" % "; ".join(first["errors"]))
            judge_shader(r, refused_ok, known_broken)
            judge_slow(r, args)
            if r["verdict"] not in ("known-broken", "slow-built", "slow-nochain") and r.get("slow_capped") is None:
                analyse_png(r, off_img, identical_ok, args)
            return r
        streak, aborted = 0, False
        with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, args.jobs)) as ex:
            for r in ex.map(one, presets):
                results.append(r)
                tag = "FAIL" if r["errors"] else r["verdict"]
                print("  %-60s %-8s %5s s%s" % (r["preset"], tag, r["seconds"],
                      ("  " + "; ".join(r["errors"])) if r["errors"] else ""), flush=True)
                if r.get("slow_drift") and os.environ.get("GITHUB_ACTIONS"):
                    print("::notice title=slow preset (%s)::%s rendered in %s s, over the %d s slow limit; to list it: %s"
                          % (args.leg or args.os, r["preset"], r["seconds"], args.slow_threshold, slow_line(args.leg or args.os, r)),
                          flush=True)
                # An unlisted slow preset fails but is no sign of a broken GL path: it does not stop the shard.
                streak = streak + 1 if (r["errors"] and not r.get("slow_unlisted")) else 0
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

    settings = "%d job(s) at once, --frame %d, --emu-frames %d, timeout %d s%s" % (
        args.jobs, args.frame, args.emu_frames, args.timeout,
        (", slow limit %d s" % args.slow_threshold) if args.slow_threshold else "")
    meta = {"title": args.os + ("" if shard_n == 1 else " shard %s" % args.shard), "exe": str(args.exe),
            "presets": len(presets), "set": args.preset_set if not args.only else "selected",
            "known_exit_hang": args.known_exit_hang, "slow_threshold": args.slow_threshold,
            "leg": args.leg or args.os, "settings": settings,
            "slow_statement": slow_list_statement(args.slow_listed, args.leg, args.slow_threshold) if args.slow_threshold else ""}
    (args.out / "results.json").write_text(json.dumps({
        "os": args.os, "exe": str(args.exe), "frame": args.frame, "presets": len(presets),
        "presets_total": presets_total, "shard": args.shard, "selected": presets, "preset_set": meta["set"],
        "leg": meta["leg"], "known_exit_hang": args.known_exit_hang, "slow_threshold": args.slow_threshold,
        "settings": settings,
        "built": sum(1 for r in results if r.get("verdict") == "built" and not r["errors"]),
        "refused": sorted(r["preset"] for r in results if r.get("verdict") == "refused" and not r["errors"]),
        "known_broken": sorted(r["preset"] for r in results if r.get("verdict") == "known-broken" and not r["errors"]),
        "did_not_exit": sum(1 for r in results if r.get("known_exit_hang")),
        "failed": sum(1 for r in results if r["errors"]),
        "results": results}, indent=1), encoding="utf-8")
    return 1 if write_report(args.out, meta, results, args.summary) else 0


if __name__ == "__main__":
    sys.exit(main())
