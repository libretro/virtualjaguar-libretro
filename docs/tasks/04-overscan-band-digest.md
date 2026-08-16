# T4 — Overscan column-band digest (`VJFBDIG3`) for #266 tooling

## Environment (do this first, exactly)

```bash
cd /Users/jmattiello/Workspace/Provenance/virtualjaguar-libretro
export DEVELOPER_DIR=/Library/Developer/CommandLineTools
git fetch libretro
git checkout -b tools/overscan-band-digest libretro/develop
[ -e test/roms/private ] || ln -sfn "${JAGUAR_ROMS_PRIVATE:?set me}" test/roms/private
```

## Hard rules (violating any of these fails the task)

- NEVER run `git clean -xfd` or any recursive delete at repo root.
- C89 only: all declarations at top of block, no `for (int i…)`, no
  designated initializers. Run `bash scripts/c89-lint.sh <file>` before commit.
- Branch from `libretro/develop`. Never `master`. Never stack on another task.
- Never relax a test threshold to make something pass.
- Touch ONLY the files in "Files you may touch". If the fix seems to need
  another file, STOP and report.

## Goal (one sentence)

Extend `fb_row_digest` / `fb_row_diff.py` so each frame also records a digest
of columns `320 .. width-1` (the overscan/border strip), bumping the file
magic from `VJFBDIG2` to `VJFBDIG3`.

## Files you may touch (explicit allowlist)

- `test/tools/fb_row_digest.c`
- `test/tools/fb_row_diff.py`
- `test/tools/fb_ab_sweep.sh`  (only if needed for a one-line comment or
  magic-related flag; prefer not to change behaviour)

Do **not** edit `src/tom/tom.c`, OP, blitter, or blank columns 320–325 in the
core. This task is tooling only.

## Critical facts

1. AvP presents wider than 320. `game_width` comes from
   `TOMGetVideoModeWidth()` (up to 652). Columns 320–325 are **inside** the
   buffer the harness already hashes; they are just folded into the whole-row
   hash today.
2. Active area for the band digest is columns `x >= 320`. If `width <= 320`,
   band width is 0 and band fields are zeros / sentinel.
3. `fb_ab_sweep.sh` uses `find "$ROMS_ROOT"` **without** `-L`. Passing the
   `test/roms/private` **symlink** finds nothing. Always pass the resolved
   path, e.g. `"$(cd test/roms/private && pwd -P)"` or
   `$JAGUAR_ROMS_PRIVATE`.

## Format change (implement exactly)

Bump magic: `VJFBDIG2` → `VJFBDIG3`.

Per-frame record today (`VJFBDIG2`):

```
uint32 width, height, frame_hash, written_extent
uint32 row_hash[height]
```

Per-frame record for `VJFBDIG3` (append after `row_hash[height]`):

```
uint32 band_x0          /* always 320, or 0 if width <= 320 */
uint32 band_width       /* max(0, width - 320) */
uint32 band_hash        /* FNV-1a over all pixels in columns [band_x0, width) for every row; 0 if band_width==0 */
uint32 band_nonblack    /* count of pixels in the band with RGB != 0; 0 if band_width==0 */
uint32 band_first_x     /* first non-black x in band, or 0xFFFFFFFF if none */
uint32 band_first_y     /* first non-black y in band, or 0xFFFFFFFF if none */
```

Use the same little-endian `put_u32` path as existing fields. Reuse the
existing FNV-1a helper.

`fb_row_diff.py` must:

- Accept `VJFBDIG3` (and may reject `VJFBDIG2` with a clear error, **or**
  accept both — pick one and document it in a comment at the magic constant).
- Prefer: require matching magic on both inputs; if either is v2, exit 2 with
  `need VJFBDIG3`.
- When comparing v3 files, if geometry/audio/row rules pass, also report
  `band_changed_frames=N` and fail (exit 1) if the patched band_hash differs
  on any frame **unless** you add an explicit `--ignore-band` flag defaulting
  to off. For this task, **failing on band_hash mismatch is correct** when
  the rest of the A/B rules would have passed — but if that breaks the
  existing brown-bar allow rule unexpectedly, STOP and report rather than
  inventing a new allow rule.

Minimal acceptable `fb_row_diff.py` behaviour for merge:

- Parse v3.
- Print per-run summary including max `band_nonblack` and whether any frame
  had `band_nonblack > 0`.
- Keep the existing brown-bar classification for row hashes.

## Steps (numbered, copy-pasteable commands)

1. Read `test/tools/fb_row_digest.c` and `test/tools/fb_row_diff.py` end to end.

2. Implement the v3 fields in `on_video()` in `fb_row_digest.c`. Keep C89
   style consistent with the file (the file already uses C99-ish build flags
   in its header comment — match the file's existing declaration style; still
   run `c89-lint` and fix what it flags if the script covers this path).

   Note: `scripts/c89-lint.sh` may skip or flag this file — if the lint script
   skips it, still avoid mid-block declarations in new code you add.

3. Update `fb_row_diff.py` magic + parser.

4. Rebuild tools + core:

   ```bash
   DEVELOPER_DIR=/Library/Developer/CommandLineTools \
     make -j"$(getconf _NPROCESSORS_ONLN)" TEST_EXPORTS=1

   DEVELOPER_DIR=/Library/Developer/CommandLineTools \
     cc -O2 -Wall -std=c99 \
       -I. -I./libretro-common/include \
       -o test/tools/fb_row_digest test/tools/fb_row_digest.c \
       test/harness/harness.c -ldl -lm
   ```

5. Gate run. **AvP's first ~31 frames are the 320-wide title screen** — the
   band only appears once it switches to 326, so run 400 frames and scan every
   frame, never just frame 0. (`yarc.j64` is not a 320-only title either: only
   its frame 0 is 320 wide, frames 1+ are also 326.)

   ```bash
   AVP='test/roms/private/ROMS/Alien vs Predator (1994).jag'
   test -f "$AVP" || AVP="$(find -L test/roms/private -iname 'Alien vs Predator (1994).jag' | head -1)"

   VJ_EXPECT_BUILD=$(./scripts/build-id.sh) \
     ./test/tools/fb_row_digest ./virtualjaguar_libretro.dylib "$AVP" \
       --frames 400 --out /tmp/avp_dig3.bin --quiet

   VJ_EXPECT_BUILD=$(./scripts/build-id.sh) \
     ./test/tools/fb_row_digest ./virtualjaguar_libretro.dylib test/roms/yarc.j64 \
       --frames 120 --out /tmp/yarc_dig3.bin --quiet

   # Inspect with Python (not committed) — scans all frames:
   python3 <<'PY'
   import struct, collections
   def peek(path):
       f = open(path,'rb')
       assert f.read(8) == b'VJFBDIG3'
       nv, na = struct.unpack('<2I', f.read(8))
       widths = collections.Counter()
       first_wide = None
       for i in range(nv):
           w,h,fh,ext = struct.unpack('<4I', f.read(16))
           f.read(4*h)
           band = struct.unpack('<6I', f.read(24))
           widths[w] += 1
           if w > 320 and first_wide is None:
               first_wide = (i, band)
       print(path, 'frames', nv, 'widths', dict(widths), 'first_wide', first_wide)
   peek('/tmp/avp_dig3.bin')
   peek('/tmp/yarc_dig3.bin')
   PY
   ```

## Acceptance gate (literal command + expected exit code / expected output)

```bash
# Magic
dd if=/tmp/avp_dig3.bin bs=8 count=1 2>/dev/null | od -An -tc
# Must show V J F B D I G 3

python3 <<'PY'
import struct
NARROW = (0, 0, 0, 0, 0xFFFFFFFF, 0xFFFFFFFF)

def scan(path):
    f = open(path,'rb')
    assert f.read(8) == b'VJFBDIG3', 'bad magic'
    nv, na = struct.unpack('<2I', f.read(8))
    wide, narrow = [], []
    for i in range(nv):
        w,h,fh,ext = struct.unpack('<4I', f.read(16))
        f.read(4*h)
        b = struct.unpack('<6I', f.read(24))
        (wide if w > 320 else narrow).append((i, w, b))
    return nv, wide, narrow

nv, wide, narrow = scan('/tmp/avp_dig3.bin')
assert wide, 'AvP: no frame wider than 320 in %d frames' % nv
fi, fw, fb = wide[0]
assert fb[0] == 320 and fb[1] == fw - 320, (fi, fw, fb)
for i, w, b in narrow:
    assert b == NARROW, ('AvP narrow frame has non-sentinel band', i, w, b)
print('AVP_OK frames=%d first_wide=%d w=%d band_x0=%d band_width=%d narrow=%d'
      % (nv, fi, fw, fb[0], fb[1], len(narrow)))

nv, wide, narrow = scan('/tmp/yarc_dig3.bin')
for i, w, b in wide:
    assert b[0] == 320 and b[1] == w - 320, (i, w, b)
for i, w, b in narrow:
    assert b == NARROW, ('yarc narrow frame has non-sentinel band', i, w, b)
print('YARC_OK frames=%d wide=%d narrow=%d' % (nv, len(wide), len(narrow)))
PY
# exit 0

# Error paths must return 2 with no traceback
python3 test/tools/fb_row_diff.py /tmp/nope.bin /tmp/yarc_dig3.bin --label missing; echo exit:$?
head -c 400 /tmp/yarc_dig3.bin > /tmp/trunc_dig3.bin
python3 test/tools/fb_row_diff.py /tmp/trunc_dig3.bin /tmp/yarc_dig3.bin --label trunc; echo exit:$?
# both: a single 'FAIL <label>: ...' line and exit:2

python3 test/tools/fb_row_diff.py /tmp/yarc_dig3.bin /tmp/yarc_dig3.bin --label yarc
# exit 0 (identical)
```

## STOP conditions (abort triggers — report, do not improvise)

- AvP dump shows `width <= 320` for **all 400** frames — the overscan strip is not
  reachable via this path on current develop; STOP and report (do not blank
  columns in the core to "create" a band).
- You are about to "fix" #266 by clearing columns 320–325 in TOM/OP — STOP.
  Tooling only.
- Tempted to keep writing `VJFBDIG2` with a side-car file — STOP; bump magic.
- `fb_row_diff.py` changes would require redesigning the brown-bar allow rule
  — implement parse + report first; if classification conflicts, STOP with
  notes rather than silently weakening the allow rule.

## Deliverable (exact commit message, PR title, PR body, issue comment text)

**Commit message:**

```
test(tools): add VJFBDIG3 overscan column-band digest (#266)
```

**PR title:**

```
test(tools): add VJFBDIG3 overscan column-band digest (#266)
```

**PR body template:**

```markdown
## Summary
- fb_row_digest emits per-frame digest for columns x>=320 (VJFBDIG3).
- fb_row_diff.py reads the new record.
- Tooling only — does not claim #266 fixed.

## Acceptance gate transcript
$ python3 … (AVP_OK / YARC_OK)
<paste>
```

**Push + PR:**

```bash
git add test/tools/fb_row_digest.c test/tools/fb_row_diff.py
git commit -m "$(cat <<'EOF'
test(tools): add VJFBDIG3 overscan column-band digest (#266)

EOF
)"
git push -u libretro HEAD
gh pr create --base develop --title "test(tools): add VJFBDIG3 overscan column-band digest (#266)" --body-file - <<'EOF'
## Summary
- fb_row_digest emits per-frame digest for columns x>=320 (VJFBDIG3).
- fb_row_diff.py reads the new record.
- Tooling only — does not claim #266 fixed. Do not close #266.

## Acceptance gate transcript
(paste here)
EOF
```

**Issue comment on #266:**

```markdown
PR <N> adds an overscan column-band digest (VJFBDIG3) so columns x≥320 are
visible to A/B sweeps. This does **not** close the bug — headless still is
not RetroArch's compositor (see CLAUDE.md caveat). Next: nightly capture
with overscan crop disabled, then write-side instrumentation if still present.
```
