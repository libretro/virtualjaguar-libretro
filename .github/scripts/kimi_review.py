#!/usr/bin/env python3
"""Post a Kimi-authored review comment on a pull request.

Deliberately dependency-free (stdlib + the `gh` CLI that GitHub runners ship)
so there is no requirements file to rot and no third-party action to drift.

Fails soft: any problem here is logged and the job still exits 0.  A review is
advisory; it must never fail a PR whose code compiles and whose tests pass.
"""

import json
import os
import subprocess
import sys
import urllib.error
import urllib.request

# A diff larger than this is summarised rather than sent whole: model context is
# finite, and a 5,000-line generated-table diff produces confident nonsense.
MAX_DIFF_CHARS = 180_000

# Paths whose diffs are noise to a reviewer. Mirrors .coderabbit.yaml's filters
# and scripts/c89-lint.sh's skip_file: machine-generated 68K dispatch, bin2c
# BIOS blobs, vendored libretro-common, and generated matrices.
SKIP_PREFIXES = (
    "src/m68000/cpuemu.c",
    "src/m68000/cpustbl.c",
    "src/m68000/cpudefs.c",
    "src/m68000/readcpu.c",
    "src/bios/jagbios.c",
    "src/bios/jagcdbios.c",
    "src/bios/jagdevcdbios.c",
    "libretro-common/",
    "docs/cart-boot-matrix.md",
    "docs/cd-boot-matrix.md",
    "test/baselines/",
)

SYSTEM_PROMPT = """\
You are reviewing a pull request against the Virtual Jaguar libretro core: an \
Atari Jaguar emulator written in C, GPLv3, targeting the libretro API.

House rules that matter more here than generic advice:

* C89/GNU89 is STRICT. The libretro buildbot compiles with MSVC on Windows, so \
C99 is a build failure, not a style preference. Flag: declarations after a \
statement in a block (by far the most common violation), `for (int i = ...)`, \
compound literals, designated initializers, and VLAs. All variables go at the \
top of their block.
* The emulated machine is big-endian on little-endian hosts. Emulated memory \
must be accessed through the GET8/GET16/GET32 and SET8/SET16/SET32 macros, \
never a direct pointer cast.
* NEVER treat a source comment as evidence about hardware behaviour. Clock \
rates and register semantics in this codebase have been wrong for years at a \
time. Ground truth is the Jaguar Technical Reference Manual (distilled in \
docs/jtrm-*.md) and the jag_sim netlists. If a change asserts a timing figure, \
ask what supports it.
* src/core/state.h is the savestate format: any field added, removed or \
reordered breaks compatibility, and emulated state that lives outside a saved \
region is a real defect class here (it has broken run-ahead twice).
* Changes to src/jerry/dac.c or src/jerry/dsp.c must clear BOTH the clipping \
and the presence audio tests. Clipping alone misses the failure mode where a \
"fix" silences a game, because silence has zero saturation.
* Shell: `set -u`, quote expansions, and never a bare rm/cp/mv (aliased to \
interactive forms that hang with no TTY). Note that `exit` inside `$(...)` only \
exits the subshell.

How to report:

Lead with a one-line verdict. Then list only findings you are confident are \
real, most serious first, each as `path:line — what breaks, and when`. Prefer \
one well-evidenced defect over ten speculative nits; say plainly when you find \
nothing substantive. Do not restate what the diff does — the author knows. Do \
not suggest cycle-accuracy rewrites of the blitter; it is knowingly not \
cycle-accurate. If the diff is mostly documentation, review it for claims that \
contradict the code, not for prose style.
"""


def sh(args):
    return subprocess.run(args, capture_output=True, text=True).stdout


def filtered_diff(base, head):
    """Diff with generated/vendored paths dropped, so the model reads signal."""
    names = sh(["git", "diff", "--name-only", f"{base}...{head}"]).split()
    keep = [n for n in names if not n.startswith(SKIP_PREFIXES)]
    dropped = len(names) - len(keep)
    if not keep:
        return "", dropped
    return sh(["git", "diff", f"{base}...{head}", "--"] + keep), dropped


def call_kimi(base_url, key, key_id, model, system, user):
    body = json.dumps(
        {
            "model": model,
            "temperature": 0.2,
            "messages": [
                {"role": "system", "content": system},
                {"role": "user", "content": user},
            ],
        }
    ).encode()
    headers = {
        "Content-Type": "application/json",
        "Authorization": "Bearer " + key,
    }
    if key_id:
        # Harmless where unused; some consoles issue an id alongside the secret.
        headers["X-Key-Id"] = key_id
    req = urllib.request.Request(
        base_url.rstrip("/") + "/chat/completions", data=body, headers=headers
    )
    with urllib.request.urlopen(req, timeout=180) as r:
        payload = json.load(r)
    return payload["choices"][0]["message"]["content"]


def main():
    key = os.environ.get("KIMI_KEY_VALUE", "")
    if not key:
        print("KIMI_KEY_VALUE is empty -- is the secret set on this repo?")
        return 0

    repo = os.environ["REPO"]
    pr = os.environ["PR_NUMBER"]

    meta = json.loads(
        sh(["gh", "pr", "view", pr, "--repo", repo, "--json",
            "title,body,baseRefName,headRefOid,baseRefOid"])
        or "{}"
    )
    if not meta:
        print("could not read PR metadata")
        return 0

    base, head = meta["baseRefOid"], meta["headRefOid"]
    diff, dropped = filtered_diff(base, head)
    if not diff.strip():
        print("nothing reviewable after filtering generated paths")
        return 0

    truncated = False
    if len(diff) > MAX_DIFF_CHARS:
        diff = diff[:MAX_DIFF_CHARS]
        truncated = True

    user = (
        f"Pull request: {meta['title']}\n"
        f"Target branch: {meta['baseRefName']}\n\n"
        f"Description:\n{(meta.get('body') or '(none)')[:4000]}\n\n"
        f"Diff:\n```diff\n{diff}\n```"
    )
    if truncated:
        user += "\n\n(The diff was truncated; review what is shown and say so.)"

    try:
        review = call_kimi(
            os.environ["KIMI_API_BASE"], key, os.environ.get("KIMI_KEY_ID"),
            os.environ["KIMI_MODEL"], SYSTEM_PROMPT, user,
        )
    except urllib.error.HTTPError as e:
        detail = e.read().decode("utf-8", "replace")[:500]
        print(f"Kimi API returned HTTP {e.code}: {detail}")
        if e.code in (401, 403):
            print(
                "Auth failed. KIMI_KEY_VALUE is being sent as a bearer token. "
                "If the provider issues an AK/SK pair needing a signed request, "
                "the auth block in call_kimi() is what needs changing."
            )
        return 0
    except Exception as e:  # noqa: BLE001 - advisory job, never fail the PR
        print(f"Kimi review failed: {type(e).__name__}: {e}")
        return 0

    note = ""
    if dropped:
        note += f"\n\n<sub>{dropped} generated/vendored file(s) excluded from review.</sub>"
    if truncated:
        note += "\n\n<sub>Diff truncated before review — large PR.</sub>"

    comment = f"## 🌙 Kimi review\n\n{review}{note}"
    p = "/tmp/kimi-review.md"
    with open(p, "w") as f:
        f.write(comment)
    r = subprocess.run(
        ["gh", "pr", "comment", pr, "--repo", repo, "--body-file", p],
        capture_output=True, text=True,
    )
    print("posted" if r.returncode == 0 else f"could not post: {r.stderr}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
