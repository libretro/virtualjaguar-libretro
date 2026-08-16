#!/usr/bin/env python3
"""
check-baseline.py -- compare a JaguarDemos probe run against BASELINE.txt.

Same acceptance philosophy as test/acid/scripts/check-baseline.py:
  PASS -> FAIL  is a regression (CI fails)
  FAIL -> PASS  is an improvement (OK)
  new FAIL/PASS rows are OK (baseline needs a refresh)
  missing baseline rows in the run are blocking

Result / baseline lines look like:
  [PASS path/to/rom.j64
  [FAIL path/to/rom.j64
"""
import re
import sys

RESULT_RE = re.compile(r"^\[(PASS|FAIL|SKIP)\s*\]\s+(\S+\.j64)")
BASELINE_RE = re.compile(r"^\[(PASS|FAIL|SKIP)\s+(\S+\.j64)")


def parse(path):
    out = {}
    with open(path) as fh:
        for line in fh:
            m = RESULT_RE.match(line)
            if not m:
                m = BASELINE_RE.match(line)
            if m:
                out[m.group(2)] = m.group(1)
    return out


def main():
    if len(sys.argv) < 3:
        print(f"usage: {sys.argv[0]} <results.txt> <BASELINE.txt>",
              file=sys.stderr)
        return 2

    results = parse(sys.argv[1])
    baseline = parse(sys.argv[2])

    if not results:
        print(f"ERROR: no results parsed from {sys.argv[1]}", file=sys.stderr)
        return 2
    if not baseline:
        print(f"ERROR: no baseline parsed from {sys.argv[2]}", file=sys.stderr)
        return 2

    regressions = []
    improvements = []
    known_fails = 0
    new_tests = []
    missing = []

    for rom, status in sorted(results.items()):
        if rom not in baseline:
            new_tests.append((rom, status))
            continue
        prev = baseline[rom]
        if prev == "PASS" and status != "PASS":
            regressions.append((rom, prev, status))
        elif prev != "PASS" and status == "PASS":
            improvements.append((rom, prev))
        elif prev != "PASS" and status != "PASS":
            known_fails += 1

    for rom in sorted(baseline):
        if rom not in results:
            missing.append((rom, baseline[rom]))

    print("## JaguarDemos suite vs baseline")
    print(f"Total in run:       {len(results)}")
    print(f"Total in baseline:  {len(baseline)}")
    print(f"Known FAILs (OK):   {known_fails}")
    print(f"Improvements:       {len(improvements)}")
    print(f"New tests:          {len(new_tests)}")
    print(f"Regressions:        {len(regressions)}")
    print(f"Missing from run:   {len(missing)}")
    print()

    if improvements:
        print("### Improvements (was FAIL/SKIP, now PASS)")
        for rom, prev in improvements:
            print(f"  {prev:>4} -> PASS  {rom}")
        print()
    if new_tests:
        print("### New tests (not yet in baseline)")
        for rom, status in new_tests:
            print(f"  {status:>4}        {rom}")
        print()
        print("Update test/jaguar-demos/BASELINE.txt to record these.")
        print()
    if regressions:
        print("### REGRESSIONS (was PASS, now FAIL/SKIP) -- BLOCKING")
        for rom, prev, status in regressions:
            print(f"  PASS -> {status:<4} {rom}")
        print()
    if missing:
        print("### MISSING (in baseline, no result this run) -- BLOCKING")
        for rom, prev in missing:
            print(f"  baseline={prev:<4} {rom}")
        print()

    if regressions or missing:
        print("FAIL: regressions or missing tests detected.")
        return 1

    print("OK: no regressions.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
