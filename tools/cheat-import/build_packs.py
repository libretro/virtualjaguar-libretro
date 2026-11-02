#!/usr/bin/env python3
"""Build the committed community cheat store (site/cheats/) from pinned community C sources.

Community sources are untrusted data: they are only parsed by convert.py, never compiled,
imported or executed. build() is pure (bytes in, bytes out); main() does the I/O. See
docs/cheat-store-design.md ("Data and CI") and README.md.
"""
import argparse
import fnmatch
import hashlib
import html
import http.client
import importlib.util
import io
import json
import os
import re
import shutil
import sys
import tarfile
import urllib.error
import urllib.request
from pathlib import Path


def _load(name, filename):
    if name not in sys.modules:
        spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(filename))
        module = importlib.util.module_from_spec(spec)
        sys.modules[name] = module
        spec.loader.exec_module(module)
    return sys.modules[name]


convert = _load("cheat_converter", "convert.py")
scan_cartridges = _load("scan_cartridges", "scan_cartridges.py")

MARKER = ".vj-cheat-packs"
CODEL_URL = "https://codeload.github.com/{repo}/tar.gz/{commit}"
FETCH_CAP, MEMBER_CAP, SCAN_CAP, FETCH_TIMEOUT = 32 << 20, 1 << 20, 256 << 20, 60
MAX_MEMBERS, TAR_BLOCK = 20000, 512
MANIFEST_CAP, CORE_LIMIT = 256 << 10, 256
CALL_RE = re.compile(r"\b(bigpemu_\w+|printf_notify|printf|memcpy)\b")
SOURCE_KEYS = {"id", "kind", "repo", "branch", "commit", "commit_date", "include", "exclude", "notes",
               "author", "credit", "permission", "license"}
# What convert.py can raise on hostile input (same set convert.main() reports as a refusal).
MODULE_ERRORS = (ValueError, RecursionError, KeyError, AttributeError, IndexError, TypeError, SyntaxError,
                 convert.c_parser.ParseError)
SOURCES_URL = "https://github.com/libretro/virtualjaguar-libretro/tree/develop/tools/cheat-import"


class BuildError(ValueError):
    """Validation failure (exit 1)."""


class FetchError(Exception):
    """Source fetch failure (exit 2)."""


def dumps(obj):
    return json.dumps(obj, indent=2, sort_keys=True, ensure_ascii=True) + "\n"


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def one_line(text):
    return " ".join(re.sub(r"[\x00-\x1f\x7f  ]", " ", str(text)).split())


def scrub(text):
    """Source text with comments and string/char literals blanked: what the scans look at."""
    return re.sub(r'"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\'', " ", convert.uncomment(text))


# ---------------------------------------------------------------- sources.json

def check_sources(doc):
    """The list of validated source dicts; BuildError on anything the schema forbids."""
    if not isinstance(doc, dict) or doc.get("format") != 1 or not isinstance(doc.get("sources"), list) or not doc["sources"]:
        raise BuildError('sources.json must be {"format": 1, "sources": [...]}')
    seen = set()
    for src in doc["sources"]:
        if isinstance(src, dict) and src.get("kind") == "local":
            raise BuildError("source kind 'local' is reserved and not implemented")
        if not isinstance(src, dict) or set(src) != SOURCE_KEYS or src["kind"] != "github":
            raise BuildError(f"source must be a github object with exactly {sorted(SOURCE_KEYS)}")
        strings = ("id", "repo", "branch", "commit", "commit_date", "author", "credit", "permission")
        if not all(isinstance(src[k], str) and src[k] for k in strings):
            raise BuildError(f"source {src.get('id')!r}: {', '.join(strings)} must be non-empty strings")
        if not re.fullmatch(r"[a-z0-9][a-z0-9_-]*", src["id"]) or src["id"] in seen:
            raise BuildError(f"bad or duplicate source id {src['id']!r}")
        seen.add(src["id"])
        if not re.fullmatch(r"[\w.-]+/[\w.-]+", src["repo"]):
            raise BuildError(f"{src['id']}: repo must be owner/name")
        if not re.fullmatch(r"[0-9a-f]{40}", src["commit"]):
            raise BuildError(f"{src['id']}: commit must be a full lower-case SHA-1")
        if not re.fullmatch(r"\d{4}-\d{2}-\d{2}", src["commit_date"]):
            raise BuildError(f"{src['id']}: commit_date must be YYYY-MM-DD")
        include = src["include"]
        if not (isinstance(include, list) and include and all(isinstance(p, str) and p.endswith(".c") for p in include)):
            raise BuildError(f"{src['id']}: include must be a non-empty list of patterns ending in .c")
        if not (isinstance(src["exclude"], list) and all(
                isinstance(e, dict) and set(e) == {"path", "reason"} and all(isinstance(v, str) and v for v in e.values())
                for e in src["exclude"])):
            raise BuildError(f"{src['id']}: exclude must be a list of {{path, reason}}")
        if not (isinstance(src["notes"], dict) and all(isinstance(k, str) and isinstance(v, str) for k, v in src["notes"].items())):
            raise BuildError(f"{src['id']}: notes must map path to text")
        if src["license"] is not None and not isinstance(src["license"], str):
            raise BuildError(f"{src['id']}: license must be null or text")
    return doc["sources"]


def matches(path, patterns):
    return any(fnmatch.fnmatchcase(path, pattern) for pattern in patterns)


def bump(doc, spec):
    """Apply "ID=SHA:DATE" to a parsed sources.json; only commit and commit_date change."""
    match = re.fullmatch(r"([a-z0-9][a-z0-9_-]*)=([0-9a-f]{40}):(\d{4}-\d{2}-\d{2})", spec)
    if not match:
        raise BuildError("--bump expects ID=<40 hex sha>:<YYYY-MM-DD>")
    for src in check_sources(doc):
        if src["id"] == match[1]:
            src["commit"], src["commit_date"] = match[2], match[3]
            return doc
    raise BuildError("--bump: no source with id " + match[1])


# ---------------------------------------------------------------- fetching

def reject_control(path, where, error=None):
    """Fail closed on a path with a control character: names reach ::warning:: annotations and PR bodies."""
    if any(ord(c) < 32 or ord(c) == 127 for c in path):
        raise (error or FetchError)(f"{where}: refusing path with a control character: {path!r}")


def fetch_github(repo, commit, include):
    """{path: bytes} for tarball members matching include (codeload, in memory, bounded)."""
    if not re.fullmatch(r"[\w.-]+/[\w.-]+", repo) or not re.fullmatch(r"[0-9a-f]{40}", commit):
        raise FetchError(f"refusing to fetch {repo}@{commit}")
    url = CODEL_URL.format(repo=repo, commit=commit)
    try:
        with urllib.request.urlopen(urllib.request.Request(url, headers={"User-Agent": "vj-cheat-packs"}),
                                    timeout=FETCH_TIMEOUT) as response:
            blob = response.read(FETCH_CAP + 1)
    except (urllib.error.URLError, http.client.HTTPException, OSError, ValueError) as error:
        raise FetchError(f"cannot fetch {url}: {error}") from error
    if len(blob) > FETCH_CAP:
        raise FetchError(f"{url}: archive exceeds {FETCH_CAP >> 20} MiB")
    found, scanned, members = {}, 0, 0
    try:
        with tarfile.open(fileobj=io.BytesIO(blob), mode="r|gz") as archive:
            for member in archive:
                members += 1
                scanned += TAR_BLOCK + max(member.size, 0)  # header block + payload, so empty members count
                if scanned > SCAN_CAP or members > MAX_MEMBERS:
                    raise FetchError(f"{url}: archive has too many members or expands past {SCAN_CAP >> 20} MiB")
                reject_control(member.name, url)
                parts = member.name.split("/", 1)  # drop codeload's <repo>-<sha>/ prefix
                if not member.isfile() or len(parts) != 2 or ".." in parts[1].split("/") or not matches(parts[1], include):
                    continue
                # Read one byte past the cap: an oversized script reaches the converter, which refuses it.
                found[parts[1]] = archive.extractfile(member).read(MEMBER_CAP + 1)
    except (tarfile.TarError, OSError, EOFError) as error:
        raise FetchError(f"{url}: unreadable archive: {error}") from error
    return found


def read_dir(root, include):
    """{path: bytes} for regular files under a local checkout, filtered exactly like fetch_github."""
    found = {}
    for base, dirs, files in os.walk(root, followlinks=False):
        for name in files:
            full = Path(base, name)
            rel = full.relative_to(root).as_posix()
            reject_control(rel, str(root), ValueError)
            if full.is_symlink() or not full.is_file() or not matches(rel, include):
                continue
            with full.open("rb") as handle:
                found[rel] = handle.read(MEMBER_CAP + 1)
    return found


# ---------------------------------------------------------------- conversion

def literals(code_text):
    # 9-16 hex digits; an integer suffix (ULL) may follow, a 17th hex digit may not.
    return sorted({m.upper().zfill(16) for m in re.findall(r"\b0[xX]([0-9A-Fa-f]{9,16})(?![0-9A-Fa-f])", code_text)})


def error_calls(error):
    call = getattr(error, "call", None)
    return [call] if call else []


def setting_call(reason):
    match = re.search(r"unsupported call (\w+)", reason)
    return convert.gap_call(match[1]) if match else None


def byte_addresses(code):
    out = set()
    for segment in code.split("+"):
        address, value = segment.split(":")
        out.update(range(int(address, 16), int(address, 16) + len(value) // 2))
    return out


class Collector:
    def __init__(self, cartridges, api_map):
        self.table, self.api, self.packs, self.rows, self.usage = cartridges, api_map, {}, [], {}

    def row(self, src, path, sha, stage, reason, crc32=None, setting=None, calls=()):
        self.rows.append({"source": src["id"], "path": path, "sha256": sha, "stage": stage, "crc32": crc32,
                          "setting": setting, "reason": one_line(reason), "calls": sorted(set(calls)),
                          "note": src["notes"].get(path)})

    def file(self, src, path, raw):
        sha = sha256(raw)
        exclusion = next((e["reason"] for e in src["exclude"] if fnmatch.fnmatchcase(path, e["path"])), None)
        if exclusion is not None:  # never decoded, scanned or parsed
            return self.row(src, path, sha, "excluded", exclusion)
        try:
            text = raw.decode("utf-8-sig")
        except UnicodeDecodeError:
            return self.row(src, path, sha, "module", "source is not valid UTF-8")
        code_text = scrub(text)
        for call in set(CALL_RE.findall(code_text)):
            self.usage.setdefault(call, set()).add(path)
        lits = literals(code_text)
        hits = sorted((self.table[lit] for lit in lits if lit in self.table), key=lambda cart: cart["crc32"])
        if not hits:
            reason = ("no known cartridge for literals [" + ", ".join("0x" + lit for lit in lits) + "]"
                      if lits else "no 64-bit literal found")
            return self.row(src, path, sha, "identity", reason)
        misses = [lit for lit in lits if lit not in self.table]
        if misses:  # a corpus gap next to a hit stays visible (spec section 7)
            self.row(src, path, sha, "identity", "no known cartridge for literals [" + ", ".join("0x" + lit for lit in misses) + "]")
        try:
            module = convert.Module(text)
        except MODULE_ERRORS as error:
            return self.row(src, path, sha, "module", self.describe(error), calls=error_calls(error))
        for cart in hits:
            self.cartridge(src, path, sha, module, cart)

    @staticmethod
    def describe(error):
        text = str(error)
        if isinstance(error, convert.Unsupported):
            return text
        if isinstance(error, convert.c_parser.ParseError):
            return "parse error: " + text
        return f"converter error: {type(error).__name__}: {text}"

    def cartridge(self, src, path, sha, module, cart):
        digest, crc = int(cart["fnv1a64"], 16), cart["crc32"]
        try:
            results = module.convert(digest)
        except MODULE_ERRORS as error:
            return self.row(src, path, sha, "module", self.describe(error), crc, calls=error_calls(error))
        # convert() resets these on every call: copy them out before the invariant run below.
        messages, dropped, ignored = list(module.messages), list(module.dropped_messages), sorted(module.ignored_calls)
        good = [r for r in results if "code" in r]
        bad = [r for r in results if "code" not in r]
        if not good:
            shown = "; ".join(f"{r['name']}: {r['unsupported']}" for r in bad[:3]) + (f" (+{len(bad) - 3} more)" if len(bad) > 3 else "")
            return self.row(src, path, sha, "module", "no toggle converted: " + shown, crc,
                            calls=[c for c in (setting_call(r["unsupported"]) for r in bad) if c])
        try:
            wrong = module.convert(digest ^ 1)
        except MODULE_ERRORS:
            wrong = []
        if any("code" in r for r in wrong):
            raise BuildError(f"{src['id']}:{path} activates cheats for a different cartridge hash (guard is not hash-specific)")
        for r in bad:
            self.row(src, path, sha, "setting", r["unsupported"], crc, r["name"], calls=[c for c in [setting_call(r["unsupported"])] if c])
        pack = self.packs.setdefault(crc, {"cart": cart, "entries": [], "sources": [], "notes": [], "messages": [],
                                           "keys": set(), "names": {}, "src": []})
        pack["src"].append(src)
        pack["sources"].append({"source": src["id"], "path": path, "sha256": sha, "converted": len(good), "omitted": len(bad)})
        stem = Path(path).stem[len("cheat_"):] if Path(path).stem.startswith("cheat_") else Path(path).stem
        for r in good:
            self.entry(pack, r, module.title, stem)
        for r in bad:
            pack["notes"].append({"source": src["id"], "path": path, "setting": r["name"], "reason": one_line(r["unsupported"])})
        for text in dropped:
            pack["notes"].append({"source": src["id"], "path": path, "setting": None, "reason": one_line(text)})
        for call in ignored:
            ours = self.api.get(call, {}).get("ours", "").removeprefix("none: ")
            pack["notes"].append({"source": src["id"], "path": path, "setting": None,
                                  "reason": f"ignored call {call}" + (f" ({ours})" if ours else "")})
        for text in messages:
            if text not in pack["messages"]:
                pack["messages"].append(text)

    @staticmethod
    def entry(pack, result, title, stem):
        base, code = result["name"], result["code"]
        if (base.casefold(), code) in pack["keys"]:
            return  # identical (name, code) from another source: one toggle
        name, holder = base, pack["names"].get(base.casefold())
        if holder is not None:  # same name, different code: parseCht would merge them into one toggle
            name = f"{base} ({title if title != holder else stem})"
            if name.casefold() in pack["names"] or re.search(r" \(part [0-9]+\)$", name, re.I):
                raise BuildError(f"cheat name {name!r} still collides in pack {pack['cart']['crc32']}")
        pack["keys"].add((base.casefold(), code))
        pack["names"].setdefault(base.casefold(), title)
        pack["names"][name.casefold()] = title
        pack["entries"].append({"name": name, "code": code, "writes": result["writes"]})


def finish_pack(pack):
    cart, entries = pack["cart"], pack["entries"]
    images = [byte_addresses(e["code"]) for e in entries]
    overlaps = [[i, j] for i in range(len(entries)) for j in range(i + 1, len(entries)) if images[i] & images[j]]
    credit = "; ".join(dict.fromkeys(src["credit"] for src in pack["src"]))
    updated = max(src["commit_date"] for src in pack["src"])
    notes_omitted = [n for n in pack["notes"] if n["setting"] is not None]
    label = cart["name"] if cart["name"].startswith(cart["title"]) else f"{cart['title']} ({cart['name']})"
    lines = [f"Community cheats for {label}; nothing is enabled automatically.", "Credit: " + credit]
    srcs = {src["id"]: src for src in pack["src"]}
    for record in pack["sources"]:
        src = srcs[record["source"]]
        lines += [f"Source: {src['repo']}@{src['commit']} {record['path']}", "Source SHA256: " + record["sha256"]]
    lines += ["Message: " + t for t in pack["messages"]]
    for n in pack["notes"]:
        lines.append(f'Omitted: "{n["setting"]}" ({n["path"]}): {n["reason"]}' if n["setting"] is not None
                     else f"Note: {n['path']}: {n['reason']}")
    lines += [f"Overlap: cheat{i} and cheat{j} write the same bytes; later wins (app order)." for i, j in overlaps]
    lines.append("Cartridge revision guard is enforced by the Virtual Jaguar app.")
    try:
        text = "\n".join(convert.cht_header(int(cart["crc32"], 16), [one_line(line) for line in lines])
                         + convert.cht_entries(entries))
    except convert.Unsupported as error:
        raise BuildError(f"pack {cart['crc32']}: {error}") from error
    data = text.encode("utf-8")
    writes = sum(e["writes"] for e in entries)
    return data, {
        "crc32": cart["crc32"], "fnv1a64": cart["fnv1a64"], "title": cart["title"], "name": cart["name"],
        "file": f"packs/{cart['crc32']}.cht", "bytes": len(data), "sha256": sha256(data), "updated": updated,
        "credit": credit, "cheats": len(entries), "writes": writes, "partial": bool(notes_omitted),
        "exceeds_core_limit": writes > CORE_LIMIT, "sources": pack["sources"], "notes": pack["notes"],
        "overlaps": overlaps, "messages": pack["messages"]}


def build(sources, cartridges, fetched, provenance=None, api_map=None, max_pack_bytes=65536, max_total_bytes=1 << 20):
    """{relpath: bytes} for the whole store. sources: parsed sources.json; cartridges: load_cartridges();
    fetched: {source id: {path: bytes}}; provenance: sha256 strings for the manifest (the CLI computes them)."""
    srcs = check_sources(sources)
    api_map = convert.load_api_map() if api_map is None else api_map
    collector = Collector(cartridges, api_map)
    for src in srcs:
        if src["id"] not in fetched:
            raise BuildError("no files fetched for source " + src["id"])
        for path in sorted(fetched[src["id"]]):
            if matches(path, src["include"]):
                collector.file(src, path, fetched[src["id"]][path])
    files, packs = {}, []
    for crc in sorted(collector.packs):
        data, entry = finish_pack(collector.packs[crc])
        if len(data) > max_pack_bytes:
            raise BuildError(f"pack {crc} is {len(data)} bytes, over the {max_pack_bytes} byte limit")
        files[entry["file"]] = data
        packs.append(entry)
    if not packs:
        raise BuildError("no packs were converted; refusing to write an empty store")
    rows = sorted(collector.rows, key=lambda r: (r["source"], r["path"], r["setting"] is not None, r["setting"] or "",
                                                 r["crc32"] or "", r["stage"]))
    usage = {call: {"status": api_map.get(call, {}).get("status", "unmapped"), "sources": sorted(paths)}
             for call, paths in sorted(collector.usage.items())}
    manifest = {
        "format": 1,
        "generated_from": dict(provenance or {"converter_sha256": None, "generator_sha256": None, "cartridges_sha256": None},
                               sources=[{"id": s["id"], "repo": s["repo"], "commit": s["commit"], "commit_date": s["commit_date"],
                                         "url": f"https://github.com/{s['repo']}/tree/{s['commit']}", "author": s["author"],
                                         "credit": s["credit"], "permission": s["permission"]} for s in srcs]),
        "packs": packs, "unsupported": "unsupported.json",
        "counts": {"packs": len(packs), "cheats": sum(p["cheats"] for p in packs),
                   "partial_packs": sum(p["partial"] for p in packs), "unsupported_entries": len(rows)}}
    files["manifest.json"] = dumps(manifest).encode()
    files["unsupported.json"] = dumps({"format": 1, "entries": rows, "api_usage": usage}).encode()
    files["index.html"] = render_index(manifest).encode()
    files["README.md"] = render_readme(srcs).encode()
    files[MARKER] = b"Generated by tools/cheat-import/build_packs.py. Everything in this directory is rewritten by it.\n"
    if len(files["manifest.json"]) > MANIFEST_CAP:
        raise BuildError("manifest.json exceeds its 256 KiB limit")
    if sum(map(len, files.values())) > max_total_bytes:
        raise BuildError(f"output is {sum(map(len, files.values()))} bytes, over the {max_total_bytes} byte limit")
    return dict(sorted(files.items()))


# ---------------------------------------------------------------- rendered pages

def render_index(manifest):
    e = html.escape
    rows = []
    for p in sorted(manifest["packs"], key=lambda p: (p["title"].casefold(), p["crc32"])):
        status = ("partial" if p["partial"] else "complete") + (", over 256 writes" if p["exceeds_core_limit"] else "")
        rows.append(f"<tr><td>{e(p['title'])}</td><td>{e(p['name'])}</td><td><code>{e(p['crc32'])}</code></td>"
                    f"<td>{p['cheats']}</td><td>{e(status)}</td>"
                    f"<td><a href=\"{e(p['file'], quote=True)}\">{e(p['crc32'])}.cht</a></td></tr>")
    credits = "".join(f"<li>{e(s['credit'])}</li>\n" for s in manifest["generated_from"]["sources"])
    counts = manifest["counts"]
    return f"""<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta name="robots" content="noindex">
<title>Community cheats | Virtual Jaguar libretro</title>
<style>
body{{font:16px/1.5 system-ui,sans-serif;max-width:56rem;margin:2rem auto;padding:0 1rem}}
code{{background:rgba(127,127,127,.15);padding:0 .25em;border-radius:3px}}
table{{border-collapse:collapse;width:100%}}
th,td{{text-align:left;padding:.25rem .5rem;border-bottom:1px solid rgba(127,127,127,.3)}}
</style>
</head>
<body>
<h1>Community cheats</h1>
<p>Cheat packs for {counts['packs']} Atari Jaguar cartridges ({counts['cheats']} cheats), converted from community
scripts. The <a href="../app.html">Virtual Jaguar desktop app</a> downloads them per cartridge; every cheat starts off.
Packs are bound to the cartridge by <code>vj_rom_crc32</code>.</p>
<ul>
{credits}</ul>
<table>
<tr><th>Game</th><th>Cartridge</th><th>CRC32</th><th>Cheats</th><th>Status</th><th>Pack</th></tr>
{chr(10).join(rows)}
</table>
<ul>
<li><a href="manifest.json">manifest.json</a>: pack index with checksums</li>
<li><a href="unsupported.json">unsupported.json</a>: scripts and settings that could not be converted yet ({counts['unsupported_entries']} entries)</li>
<li><a href="README.md">README</a>: provenance and how the packs are generated</li>
</ul>
<p><a href="../">Back to the Virtual Jaguar site</a></p>
</body>
</html>
"""


def render_readme(sources):
    parts = ["# Community cheat packs", "",
             "Generated by `tools/cheat-import/build_packs.py`; do not edit by hand. Every file in this directory is rewritten",
             "by the generator and checked in CI, so a hand edit fails the regeneration check.", "",
             "Each `packs/<CRC32>.cht` is one cartridge (bound by `vj_rom_crc32`), converted from community BigPEmu C cheat",
             "scripts with the project's own converter. Every cheat starts disabled. `manifest.json` lists the packs with",
             "their sha256 and the scripts and settings behind each one; `unsupported.json` is the work list of scripts and",
             "settings the converter cannot express yet.", "", "## Credit", ""]
    parts += [f"- {s['credit']}" for s in sources]
    parts += ["", "## Permission", ""]
    parts += [f"- {s['permission']}" for s in sources]
    parts += ["", "## Sources", ""]
    parts += [f"- `{s['id']}`: https://github.com/{s['repo']}/tree/{s['commit']} (commit date {s['commit_date']})" for s in sources]
    parts += ["", "## How it is generated", "",
              "```",
              "python3 tools/cheat-import/build_packs.py --sources tools/cheat-import/sources.json \\",
              "    --cartridges tools/cheat-import/cartridges.json --out site/cheats",
              "```", "",
              "The generator fetches one pinned tarball per source, converts only `scripts/cheat_*.c` files (parsed, never",
              "executed), maps each script's cartridge hash to a CRC32 through the committed hash table, and writes these",
              f"files byte-for-byte reproducibly. See [tools/cheat-import]({SOURCES_URL}).", ""]
    return "\n".join(parts)


# ---------------------------------------------------------------- summary

def pack_cheats(manifest_path, pack):
    """[(name, code)] parsed from the pack file beside the manifest, or None when it is not there."""
    path = Path(manifest_path).parent / pack["file"]
    if not path.is_file():
        return None
    fields = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        match = re.fullmatch(r'cheat(\d+)_(desc|code) = "(.*)"', line)
        if match:
            fields[(int(match[1]), match[2])] = match[3]
    return [(fields.get((i, "desc"), ""), fields.get((i, "code"), "")) for i in sorted({k[0] for k in fields})]


def summarize(old_path, new_path):
    """Markdown added/changed/removed report for the bump PR body; packs are read beside each manifest."""
    old, new = (json.loads(Path(p).read_text(encoding="utf-8")) for p in (old_path, new_path))
    olds, news = ({p["crc32"]: p for p in m["packs"]} for m in (old, new))
    out = ["## Community cheat pack changes", ""]
    for before, after in zip(old["generated_from"]["sources"], new["generated_from"]["sources"]):
        if before["commit"] != after["commit"]:
            out += [f"- `{after['id']}`: [{before['commit'][:7]} -> {after['commit'][:7]}]"
                    f"(https://github.com/{after['repo']}/compare/{before['commit']}...{after['commit']})"]
    changed = []
    for crc in sorted(set(olds) | set(news)):
        a, b = olds.get(crc), news.get(crc)
        label = f"**{(b or a)['title']}** (`{crc}`)"
        if a is None:
            changed.append(f"- Added pack {label}: {b['cheats']} cheats")
        elif b is None:
            changed.append(f"- Removed pack {label}: {a['cheats']} cheats")
        elif a["sha256"] != b["sha256"]:
            x, y = pack_cheats(old_path, a), pack_cheats(new_path, b)
            if x is None or y is None:
                changed.append(f"- Changed pack {label}: sha256 {a['sha256'][:8]} -> {b['sha256'][:8]}")
                continue
            xs, ys = dict(x), dict(y)
            parts = [f'added "{n}"' for n in sorted(ys.keys() - xs.keys())]
            parts += [f'changed "{n}"' for n in sorted(n for n in xs.keys() & ys.keys() if xs[n] != ys[n])]
            parts += [f'removed "{n}"' for n in sorted(xs.keys() - ys.keys())]
            changed.append(f"- Changed pack {label}: " + ("; ".join(parts) or "header or notes only"))
    out += changed or ["- No pack changes."]
    out += ["", f"Worklist entries: {old['counts']['unsupported_entries']} -> {new['counts']['unsupported_entries']}",
            f"Packs: {old['counts']['packs']} -> {new['counts']['packs']}; cheats: {old['counts']['cheats']} -> {new['counts']['cheats']}",
            f"Pack bytes: {sum(p['bytes'] for p in old['packs'])} -> {sum(p['bytes'] for p in new['packs'])}", ""]
    return "\n".join(out)


# ---------------------------------------------------------------- CLI

def write_tree(out_dir, files):
    out = Path(out_dir)
    if out.exists() and not out.is_dir():
        raise BuildError(f"{out} is not a directory")
    if out.is_dir() and any(out.iterdir()):
        if not (out / MARKER).is_file():
            raise BuildError(f"{out} is not empty and has no {MARKER} marker; refusing to overwrite it")
        packs = out / "packs"
        if packs.is_symlink():
            raise BuildError(f"{packs} is a symlink")
        if packs.is_dir():
            shutil.rmtree(packs)
        for name in ("manifest.json", "unsupported.json", "index.html", "README.md"):
            (out / name).unlink(missing_ok=True)
    for rel, data in files.items():
        target = out / rel
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sources", type=Path)
    parser.add_argument("--cartridges", type=Path)
    parser.add_argument("--out", type=Path)
    parser.add_argument("--source-dir", action="append", default=[], metavar="ID=PATH", help="local checkout instead of a fetch")
    parser.add_argument("--allow-network", dest="network", action="store_true", default=True)
    parser.add_argument("--no-network", dest="network", action="store_false")
    parser.add_argument("--summary", nargs=2, metavar=("OLD_MANIFEST", "NEW_MANIFEST"))
    parser.add_argument("--bump", metavar="ID=SHA:DATE", help="rewrite the source pin in sources.json")
    parser.add_argument("--max-pack-bytes", type=int, default=65536)
    parser.add_argument("--max-total-bytes", type=int, default=1 << 20)
    args = parser.parse_args(argv)
    try:
        if args.summary:
            print(summarize(*args.summary))
            return 0
        if not args.sources:
            parser.error("--sources is required")
        doc = json.loads(args.sources.read_text(encoding="utf-8"))
        if args.bump:
            args.sources.write_bytes(dumps(bump(doc, args.bump)).encode())
            if not args.out:
                return 0
        if not (args.cartridges and args.out):
            parser.error("--cartridges and --out are required")
        srcs = check_sources(doc)
        dirs = dict(item.split("=", 1) for item in args.source_dir if "=" in item)
        if len(dirs) != len(args.source_dir) or set(dirs) - {s["id"] for s in srcs}:
            raise BuildError("--source-dir expects ID=PATH for a source id in sources.json")
        fetched = {}
        for src in srcs:
            if src["id"] in dirs:
                fetched[src["id"]] = read_dir(Path(dirs[src["id"]]), src["include"])
            elif args.network:
                fetched[src["id"]] = fetch_github(src["repo"], src["commit"], src["include"])
            else:
                raise FetchError(f"source {src['id']} has no --source-dir and --no-network was given")
        here = Path(__file__)
        provenance = {"converter_sha256": sha256(here.with_name("convert.py").read_bytes()),
                      "generator_sha256": sha256(here.read_bytes()),
                      "cartridges_sha256": sha256(args.cartridges.read_bytes())}
        files = build(doc, scan_cartridges.load_cartridges(args.cartridges), fetched, provenance,
                      max_pack_bytes=args.max_pack_bytes, max_total_bytes=args.max_total_bytes)
        write_tree(args.out, files)
    except FetchError as error:
        print("fetch failed: " + str(error), file=sys.stderr)
        return 2
    except (BuildError, ValueError, OSError) as error:
        print("error: " + str(error), file=sys.stderr)
        return 1
    manifest = json.loads(files["manifest.json"])
    usage = json.loads(files["unsupported.json"])["api_usage"]
    for call, row in usage.items():
        if row["status"] == "unmapped":
            print(f"warning: unmapped BigPEmu call {call} in {', '.join(row['sources'])}", file=sys.stderr)
    counts = manifest["counts"]
    print(f"wrote {counts['packs']} packs, {counts['cheats']} cheats, {counts['unsupported_entries']} worklist entries to {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
