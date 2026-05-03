#!/usr/bin/env python3
"""
check-baseline.py -- compare a fresh acid-test run against the
checked-in BASELINE and gate PRs on regressions.

Behaviour:

  Test was PASS in baseline AND PASS now      -> OK
  Test was FAIL/NOT-RUN in baseline AND PASS   -> IMPROVEMENT (good!)
  Test was PASS in baseline AND FAIL now       -> REGRESSION (CI fails)
  Test was FAIL in baseline AND FAIL now       -> KNOWN FAIL (OK)
  Test was FAIL in baseline AND NOT-RUN now    -> RUNNER-ERROR (CI fails)
  Test in baseline but missing from run        -> MISSING (CI fails)
  Test in run but missing from baseline        -> NEW (OK; baseline
                                                  needs updating)

Exit code 0 if no regressions; 1 if any regression / runner error /
missing test.

The acceptance philosophy: we *encourage* adding tests that FAIL --
those are checked-in descriptions of known bugs.  We block PRs that
break a previously-PASSing test, because that's a real regression.

Usage:
  python3 check-baseline.py <results.txt> [BASELINE.txt]

results.txt: lines like `[PASS       ] tests/foo/bar.jag ...`
             (the raw stdout from `make -C test/acid test`).
BASELINE.txt: defaults to test/acid/BASELINE.txt; lines like
              `[STATUS test/path.jag` (one per file).
"""
import os
import re
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__)))))
DEFAULT_BASELINE = os.path.join(REPO_ROOT, "test", "acid", "BASELINE.txt")

RESULT_RE = re.compile(r"^\[(PASS|FAIL|NOT-RUN-YET)\s*\]\s+(\S+\.jag)")
BASELINE_RE = re.compile(r"^\[(PASS|FAIL|NOT-RUN-YET)\s+(\S+\.jag)")


def parse_results(path):
    """Returns dict: rom_path -> status."""
    out = {}
    with open(path) as fh:
        for line in fh:
            m = RESULT_RE.match(line)
            if m:
                out[m.group(2)] = m.group(1)
            else:
                m = BASELINE_RE.match(line)
                if m:
                    out[m.group(2)] = m.group(1)
    return out


def main():
    if len(sys.argv) < 2:
        print(f"usage: {sys.argv[0]} <results.txt> [BASELINE.txt]",
              file=sys.stderr)
        return 2
    results_path = sys.argv[1]
    baseline_path = sys.argv[2] if len(sys.argv) > 2 else DEFAULT_BASELINE

    results = parse_results(results_path)
    baseline = parse_results(baseline_path)

    if not results:
        print(f"ERROR: no test results parsed from {results_path}",
              file=sys.stderr)
        return 2
    if not baseline:
        print(f"ERROR: no baseline parsed from {baseline_path}",
              file=sys.stderr)
        return 2

    regressions = []   # was PASS, now FAIL/NOT-RUN
    improvements = []  # was FAIL/NOT-RUN, now PASS
    known_fails = 0
    new_tests = []     # in run, not in baseline
    missing = []       # in baseline, not in run (broken assemble?)

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

    # Report.
    print(f"## Acid suite vs baseline")
    print(f"Total in run:       {len(results)}")
    print(f"Total in baseline:  {len(baseline)}")
    print(f"Known FAILs (OK):   {known_fails}")
    print(f"Improvements:       {len(improvements)}")
    print(f"New tests:          {len(new_tests)}")
    print(f"Regressions:        {len(regressions)}")
    print(f"Missing from run:   {len(missing)}")
    print()

    if improvements:
        print("### Improvements (was FAIL/NOT-RUN, now PASS)")
        for rom, prev in improvements:
            print(f"  {prev:>11} -> PASS  {rom}")
        print()
    if new_tests:
        print("### New tests (not yet in baseline)")
        for rom, status in new_tests:
            print(f"  {status:>11}        {rom}")
        print()
        print("Update test/acid/BASELINE.txt to record these.")
        print()
    if regressions:
        print("### REGRESSIONS (was PASS, now FAIL/NOT-RUN) -- BLOCKING")
        for rom, prev, status in regressions:
            print(f"  PASS -> {status:<11} {rom}")
        print()
    if missing:
        print("### MISSING (in baseline, no result this run) -- BLOCKING")
        print("  Probably a build / assemble failure; check the make log.")
        for rom, prev in missing:
            print(f"  baseline={prev:<11} {rom}")
        print()

    if regressions or missing:
        print("FAIL: regressions or missing tests detected.")
        return 1

    print("OK: no regressions.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
