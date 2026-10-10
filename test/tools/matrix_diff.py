#!/usr/bin/env python3
"""matrix_diff.py -- never-backward gate for the boot matrices (issue #749).

    python3 -I test/tools/matrix_diff.py [--json] OLD.md NEW.md

Compares two committed copies of docs/cart-boot-matrix.md (or two copies of
docs/cd-boot-matrix.md) row by row and prints:

  regressed         a stage moved backward, or a crash-watchdog signature
                    appeared that the OLD row did not have
  still-asymmetric  (NEW file) cart: HLE stage != BIOS stage;
                    CD: the hle and bios rows of a title differ in stage or
                    watchdog signatures
  improved          a stage moved forward, or a signature disappeared

plus informational lists: lateral (stage label changed within a rank), added
rows and removed rows.

Exit status: 1 if anything regressed, 0 otherwise, 2 on a usage or parse error
(unreadable file, no recognisable table, OLD and NEW of different kinds, an
unknown stage label).  Added and removed rows never change the exit status;
the release checklist (docs/release-process.md) makes a human account for them.

This runs on the corpus machine against the committed docs.  It needs no ROMs
itself, but the matrices it compares can only be produced with the private
corpus, so it is a release-gate step, not a GitHub CI step.

Format detection (header of the FIRST table in the file):
    | Title | HLE | HLE notes | Real BIOS | BIOS notes |   -> cart
    | Title | Mode | Score | Stage | Watchdog | PC evidence |   -> CD
Only that first table is read.  cd-boot-matrix.md carries several historical
"| Title | Mode |" tables further down; they are dated snapshots and must not
overwrite current rows (cd_boot_matrix.sh scopes its resume guard the same way).
`<!-- build:... -->` stamps (after the last pipe in the cart doc, inside the last
cell in the CD doc) are stripped before anything is compared.

Row keys: cart = title; CD = title + mode.  Cart titles are not unique (the
extension is dropped, so two dumps of one game collide).  Duplicates are
paired between OLD and NEW by closest match -- identical rows first, then rows
sharing the most fields, then file order -- and keyed "Title #2" when
ambiguous.  A row whose partner cannot be found shows up as added/removed.

Stage ranking (low = worse).  A move to a lower rank is BACKWARD:

    -1  ? (core_error) / ? (build_mismatch) / ? (no_reg)
        harness faults: the row says nothing about the title and invalidates
        the sweep, so moving into one is blocking
     0  LOAD_FAIL (incl. "LOAD_FAIL (harness crash)")
     1  HARNESS_HANG, "?", ? (pc_escape), ? (timeout), ? (BIOS service band ...)
        and every other "? (...)" variant: ran, then faulted or could not be
        classified.  Moving into rank 1 from a real stage is a loss of
        evidence, so it blocks; moving between rank-1 labels is lateral.
     2  BIOS_INTRO      (CD, bios mode)
     3  BOOT_STUB       (CD, bios mode)
     4  GAME_CODE       (the only real stage the cart matrix produces)
     5  MENU
     6  IN_GAME

Cart reduces to LOAD_FAIL < ? < GAME_CODE.  The CD order is the legend order in
docs/cd-boot-matrix.md (LOAD_FAIL -> BIOS_INTRO -> BOOT_STUB -> GAME_CODE ->
MENU -> IN_GAME).  An unrecognised stage label is a parse error, never a silent
rank.

Signatures: a row's notes (cart) or Watchdog cell (CD) is searched for the
exact crash-watchdog names gpu_wedge, dsp_wedge, inframe_hang, video_stall,
cd_seek_wedge, gpu_pc_escape, dsp_pc_escape (word-bounded; never read from the
stage label).  At EQUAL stage, a signature in NEW that OLD lacks is backward
and one that went away is an improvement.  If the stage moved, the stage
decides: a forward move is not turned into a regression by a signature, and
the new signatures are listed in the detail text for the human reading it.

Not part of the gate: video/audio/"black video" notes (the headless
"black video (headless — undetermined)" note is informational, never
backward), the CD Score column (the legend says the stage is the gate), and
the CD evidence column (final_pc / ram_payload differ between runs).

Standard library only.
"""

import json
import re
import sys

STAMP_RE = re.compile(r"\s*<!--\s*build:[^>]*?-->\s*")
UNESCAPED_PIPE = re.compile(r"(?<!\\)\|")
SIG_RE = re.compile(
    r"\b(gpu_pc_escape|dsp_pc_escape|gpu_wedge|dsp_wedge|inframe_hang"
    r"|video_stall|cd_seek_wedge)\b"
)
PROBE_ARGS_RE = re.compile(r"^Probe arguments for every run:\s*`([^`]*)`", re.M)

CART_HEADER = ["title", "hle", "hle notes", "real bios", "bios notes"]
CD_HEADER = ["title", "mode", "score", "stage", "watchdog", "pc evidence"]

RANK_INVALID = -1
RANK_LOAD_FAIL = 0
RANK_UNCLASSIFIED = 1
STAGE_RANKS = {
    "BIOS_INTRO": 2,
    "BOOT_STUB": 3,
    "GAME_CODE": 4,
    "MENU": 5,
    "IN_GAME": 6,
}
INVALID_PREFIXES = ("? (core_error", "? (build_mismatch", "? (no_reg")


class ParseError(Exception):
    pass


def stage_rank(stage):
    s = stage.strip().strip("`").strip()
    if s.startswith(INVALID_PREFIXES):
        return RANK_INVALID
    if s.startswith("LOAD_FAIL"):
        return RANK_LOAD_FAIL
    if s == "HARNESS_HANG" or s == "?" or s.startswith("? ("):
        return RANK_UNCLASSIFIED
    if s in STAGE_RANKS:
        return STAGE_RANKS[s]
    raise ParseError("unknown stage label: %r" % stage)


def signatures(text):
    return frozenset(SIG_RE.findall(text))


def split_row(line):
    line = STAMP_RE.sub("", line.rstrip("\n")).strip()
    if not (line.startswith("|") and line.endswith("|")):
        return None
    cells = UNESCAPED_PIPE.split(line[1:-1])
    return [c.strip() for c in cells]


def read_matrix(path):
    """Return dict(kind, rows, probe_args).  rows: list of dicts in file order."""
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as fh:
            text = fh.read()
    except OSError as exc:
        raise ParseError("cannot read %s: %s" % (path, exc))

    probe = PROBE_ARGS_RE.search(text)
    lines = text.splitlines()
    kind = None
    start = None
    for i, line in enumerate(lines):
        cells = split_row(line)
        if cells is None:
            continue
        norm = [c.lower() for c in cells]
        if norm == CART_HEADER:
            kind, start = "cart", i + 1
            break
        if norm == CD_HEADER:
            kind, start = "cd", i + 1
            break
    if kind is None:
        raise ParseError("%s: no cart or CD matrix table header found" % path)

    ncols = len(CART_HEADER) if kind == "cart" else len(CD_HEADER)
    rows = []
    for line in lines[start:]:
        if not line.startswith("|"):
            break  # end of the primary (first) table
        if line.startswith("|---"):
            continue
        cells = split_row(line)
        if cells is None or len(cells) != ncols:
            raise ParseError("%s: malformed table row: %s" % (path, line[:100]))
        if kind == "cart":
            title, hle, hle_notes, bios, bios_notes = cells
            rows.append({
                "title": title,
                "cells": {
                    "HLE": (hle, hle_notes),
                    "BIOS": (bios, bios_notes),
                },
            })
        else:
            title, mode, score, stage, watchdog, evidence = cells
            rows.append({
                "title": title + " [" + mode + "]",
                "title_only": title,
                "mode": mode,
                "score": score,
                "cells": {"": (stage, watchdog)},
            })
    if not rows:
        raise ParseError("%s: matrix table has no rows" % path)
    for r in rows:
        for label, (stage, text_) in r["cells"].items():
            r.setdefault("rank", {})[label] = stage_rank(stage)
            r.setdefault("sigs", {})[label] = signatures(text_)
    return {"kind": kind, "rows": rows,
            "probe_args": probe.group(1) if probe else None}


def row_fingerprint(row):
    return tuple(
        (label, row["cells"][label][0], tuple(sorted(row["sigs"][label])))
        for label in sorted(row["cells"])
    )


def pair_rows(old_rows, new_rows):
    """Pair OLD/NEW rows by title, resolving duplicate titles by closeness.

    Returns (pairs, added, removed); pairs are (key, old_row, new_row)."""
    def group(rows):
        g = {}
        for r in rows:
            g.setdefault(r["title"], []).append(r)
        return g

    og, ng = group(old_rows), group(new_rows)
    pairs, added, removed = [], [], []
    order = []
    for r in new_rows + old_rows:
        if r["title"] not in order:
            order.append(r["title"])
    for title in order:
        o, n = list(og.get(title, [])), list(ng.get(title, []))
        dup = max(len(o), len(n)) > 1
        scored = []
        for i, orow in enumerate(o):
            for j, nrow in enumerate(n):
                of, nf = row_fingerprint(orow), row_fingerprint(nrow)
                score = sum(1 for a, b in zip(of, nf) if a == b) + (
                    10 if of == nf else 0)
                scored.append((-score, i, j))
        scored.sort()
        used_o, used_n, matched = set(), set(), []
        for _, i, j in scored:
            if i in used_o or j in used_n:
                continue
            used_o.add(i)
            used_n.add(j)
            matched.append((i, j))
        matched.sort(key=lambda ij: ij[1])
        for k, (i, j) in enumerate(matched):
            key = title + (" #%d" % (k + 1) if dup else "")
            pairs.append((key, o[i], n[j]))
        for j, nrow in enumerate(n):
            if j not in used_n:
                added.append(nrow["title"] + (" (duplicate title)" if dup else ""))
        for i, orow in enumerate(o):
            if i not in used_o:
                removed.append(orow["title"] + (" (duplicate title)" if dup else ""))
    return pairs, added, removed


def compare_cell(old_stage, old_rank, old_sigs, new_stage, new_rank, new_sigs):
    """Return (verdict, detail); verdict in backward/forward/lateral/same."""
    gained = sorted(new_sigs - old_sigs)
    lost = sorted(old_sigs - new_sigs)
    sig_txt = ""
    if gained:
        sig_txt += "; new signature: " + ",".join(gained)
    if lost:
        sig_txt += "; signature gone: " + ",".join(lost)
    arrow = "%s -> %s" % (old_stage, new_stage)
    if new_rank < old_rank:
        return "backward", arrow + sig_txt
    if new_rank > old_rank:
        return "forward", arrow + sig_txt
    if gained:
        return "backward", "%s (stage unchanged)%s" % (new_stage, sig_txt)
    if lost:
        return "forward", "%s (stage unchanged)%s" % (new_stage, sig_txt)
    if old_stage != new_stage:
        return "lateral", arrow
    return "same", ""


def asymmetric(kind, rows):
    """NEW-file rows whose hle/bios halves disagree -> {title: detail}."""
    out = {}
    if kind == "cart":
        for r in rows:
            h, b = r["cells"]["HLE"][0], r["cells"]["BIOS"][0]
            if h != b:
                out.setdefault(r["title"], []).append("HLE %s / BIOS %s" % (h, b))
    else:
        by = {}
        for r in rows:
            by.setdefault(r["title_only"], {})[r["mode"]] = r
        for title, modes in by.items():
            if "hle" in modes and "bios" in modes:
                h, b = modes["hle"], modes["bios"]
                hs, bs = h["cells"][""][0], b["cells"][""][0]
                hg, bg = h["sigs"][""], b["sigs"][""]
                if hs != bs or hg != bg:
                    out.setdefault(title, []).append(
                        "hle %s%s / bios %s%s" % (
                            hs, " (%s)" % ",".join(sorted(hg)) if hg else "",
                            bs, " (%s)" % ",".join(sorted(bg)) if bg else ""))
    return {k: "; ".join(v) for k, v in out.items()}


def diff(old, new):
    kind = old["kind"]
    pairs, added, removed = pair_rows(old["rows"], new["rows"])
    res = {"kind": kind, "regressed": [], "improved": [], "lateral": [],
           "still_asymmetric": [], "added": sorted(added),
           "removed": sorted(removed)}
    for key, orow, nrow in pairs:
        for label in sorted(nrow["cells"]):
            verdict, detail = compare_cell(
                orow["cells"][label][0], orow["rank"][label], orow["sigs"][label],
                nrow["cells"][label][0], nrow["rank"][label], nrow["sigs"][label])
            if verdict == "same":
                continue
            name = key + (" [%s]" % label if label else "")
            bucket = {"backward": "regressed", "forward": "improved",
                      "lateral": "lateral"}[verdict]
            res[bucket].append({"row": name, "detail": detail})
            # CD score is informational only; nothing to add here.
    old_asym = asymmetric(kind, old["rows"])
    for title, detail in sorted(asymmetric(kind, new["rows"]).items()):
        res["still_asymmetric"].append({
            "row": title, "detail": detail,
            "new": title not in old_asym})
    for bucket in ("regressed", "improved", "lateral"):
        res[bucket].sort(key=lambda e: e["row"])
    return res


def render(res, old_path, new_path, n_old, n_new, warnings):
    out = ["matrix_diff: %s matrix  old=%s (%d rows)  new=%s (%d rows)" % (
        res["kind"], old_path, n_old, new_path, n_new)]
    for w in warnings:
        out.append("warning: " + w)

    def section(name, items, fmt):
        out.append("")
        out.append("%s (%d)" % (name, len(items)))
        for it in items:
            out.append("  " + fmt(it))

    section("regressed", res["regressed"], lambda e: "%s: %s" % (e["row"], e["detail"]))
    section("still-asymmetric", res["still_asymmetric"],
            lambda e: "%s: %s%s" % (e["row"], e["detail"],
                                    "  [new asymmetry]" if e["new"] else ""))
    section("improved", res["improved"], lambda e: "%s: %s" % (e["row"], e["detail"]))
    section("lateral (informational)", res["lateral"],
            lambda e: "%s: %s" % (e["row"], e["detail"]))
    section("added rows", res["added"], str)
    section("removed rows", res["removed"], str)
    out.append("")
    if res["regressed"]:
        out.append("RESULT: BACKWARD -- %d regressed cell(s); blocks the tag until "
                   "ticketed and explicitly deferred" % len(res["regressed"]))
    else:
        out.append("RESULT: OK -- no backward moves")
    return "\n".join(out)


def main(argv):
    args = argv[1:]
    as_json = False
    if "--json" in args:
        args.remove("--json")
        as_json = True
    if len(args) != 2 or any(a.startswith("-") and a != "-" for a in args):
        sys.stderr.write(__doc__.split("\n\n")[0] + "\n")
        sys.stderr.write("usage: matrix_diff.py [--json] OLD.md NEW.md\n")
        return 2
    try:
        old = read_matrix(args[0])
        new = read_matrix(args[1])
        if old["kind"] != new["kind"]:
            raise ParseError("OLD is a %s matrix but NEW is a %s matrix" % (
                old["kind"], new["kind"]))
        res = diff(old, new)
    except ParseError as exc:
        sys.stderr.write("matrix_diff: error: %s\n" % exc)
        return 2
    warnings = []
    if old["probe_args"] != new["probe_args"]:
        warnings.append(
            "probe arguments differ (old=%r new=%r): the sweeps are not "
            "comparable (Fast vs Accurate blitter?)" % (
                old["probe_args"], new["probe_args"]))
    res["warnings"] = warnings
    res["exit"] = 1 if res["regressed"] else 0
    if as_json:
        print(json.dumps(res, indent=2, sort_keys=True))
    else:
        print(render(res, args[0], args[1], len(old["rows"]),
                     len(new["rows"]), warnings))
    return res["exit"]


if __name__ == "__main__":
    sys.exit(main(sys.argv))
