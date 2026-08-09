# T1 — Savestate header inspector (`vjss_info`)

## Environment (do this first, exactly)

```bash
cd /Users/jmattiello/Workspace/Provenance/virtualjaguar-libretro
export DEVELOPER_DIR=/Library/Developer/CommandLineTools
git fetch libretro
git checkout -b tools/vjss-info libretro/develop
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

Add `test/tools/vjss_info.c` that reads the first 16 bytes of a Virtual Jaguar
savestate, prints magic/version/flags/reserved, detects host byte order, and
prints a loadability verdict against `STATE_VERSION` / `STATE_MIN_VERSION`.

## Files you may touch (explicit allowlist)

- `test/tools/vjss_info.c`  (**create**)
- `test/tools/README-vjss-info.md`  (**create**, optional one-screen usage note)

Do **not** edit `src/core/state.h`, `libretro.c`, or any shipped core code.

## Critical fact (read before writing a single line)

`STATE_SAVE_VAR` in `src/core/state.h` is a plain `memcpy` of a host
`uint32_t`. Headers are **host-endian**, not big-endian and not a literal
ASCII `"VJSS"` string on disk.

On a little-endian host the on-disk magic `0x564A5353` ("VJSS" as a
big-endian fourcc value stored as a native `uint32_t`) appears as bytes:

```
53 53 4A 56
```

A naive `memcmp(buf, "VJSS", 4)` reports every valid little-endian state as
corrupt. Your tool MUST:

1. Read 16 bytes (magic, version, flags, reserved — four `uint32_t`s).
2. Interpret them as little-endian AND as big-endian.
3. Report which interpretation yields `magic == 0x564A5353`.
4. Use that endianness for version/flags/reserved.
5. Print a verdict:
   - `loadable` if `STATE_MIN_VERSION (2) <= version <= STATE_VERSION (7)`
   - `too_old` if `version < 2`
   - `too_new` if `version > 7`
   - `bad_magic` if neither endianness matches

Constants (do not hardcode differently):

```c
/* from src/core/state.h — copy the numeric values, do not #include state.h
 * into a standalone tool unless you also pull its dependencies cleanly.
 * Prefer duplicating the three numbers with a comment citing state.h. */
#define VJSS_MAGIC        0x564A5353u  /* "VJSS" */
#define VJSS_VERSION      7u
#define VJSS_MIN_VERSION  2u
```

## Steps (numbered, copy-pasteable commands)

1. Create `test/tools/vjss_info.c` as a standalone C89 program (no libretro
   link required). CLI:

   ```
   vjss_info <statefile>
   ```

   Required stdout (one line or a short block — keep it machine-grepable):

   ```
   magic=0x564A5353 endian=le version=7 flags=0x00000000 reserved=0x00000000 verdict=loadable
   ```

   Use `endian=le` or `endian=be`. Exit codes:
   - `0` — file opened and magic matched (any verdict including too_old/too_new)
   - `1` — I/O error or bad_magic
   - `2` — usage error

2. Build the tool:

   ```bash
   DEVELOPER_DIR=/Library/Developer/CommandLineTools \
     cc -O2 -Wall -std=c89 -o test/tools/vjss_info test/tools/vjss_info.c
   ```

3. Build a known-good current-version state for the gate. Prefer a tiny
   throwaway dump (do **not** commit the dump helper or the `.state` file):

   ```bash
   DEVELOPER_DIR=/Library/Developer/CommandLineTools \
     make -j"$(getconf _NPROCESSORS_ONLN)" TEST_EXPORTS=1

   # Dump first 16+ bytes via a one-shot Python helper (not committed):
   python3 <<'PY'
   import ctypes, sys
   core = ctypes.CDLL("./virtualjaguar_libretro.dylib")
   # Minimal libretro dance is painful in Python; instead use test_state_compat
   # which already serializes — OR write /tmp/vjss_dump.c as below.
   print("use /tmp/vjss_dump.c recipe in step 3b", file=sys.stderr)
   sys.exit(0)
   PY
   ```

   **Step 3b (recommended dump recipe)** — write `/tmp/vjss_dump.c` (NOT in
   the repo), build it against the harness, dump a state from `test/roms/yarc.j64`:

   ```bash
   cat > /tmp/vjss_dump.c <<'EOF'
   #include <stdio.h>
   #include <stdlib.h>
   #include <stdint.h>
   #include <string.h>
   #include "test/harness/harness.h"
   int main(int argc, char **argv) {
       harness_config cfg = HARNESS_CONFIG_DEFAULT;
       size_t (*ser_size)(void);
       bool (*ser)(void *, size_t);
       void *buf;
       size_t n;
       FILE *f;
       cfg.frames = 30;
       if (!harness_init_from_args(&cfg, argc, argv)) return 1;
       if (!harness_load_rom(&cfg)) return 1;
       harness_run(&cfg);
       ser_size = (size_t (*)(void))harness_dlsym(&cfg, "retro_serialize_size");
       ser = (bool (*)(void *, size_t))harness_dlsym(&cfg, "retro_serialize");
       if (!ser_size || !ser) { fprintf(stderr, "missing serialize\n"); return 1; }
       n = ser_size();
       buf = malloc(n);
       if (!buf || !ser(buf, n)) return 1;
       f = fopen("/tmp/vjss_yarc.state", "wb");
       if (!f) return 1;
       fwrite(buf, 1, n, f);
       fclose(f);
       printf("wrote /tmp/vjss_yarc.state bytes=%zu\n", n);
       harness_shutdown(&cfg);
       free(buf);
       return 0;
   }
   EOF

   DEVELOPER_DIR=/Library/Developer/CommandLineTools \
     cc -O2 -Wall -std=c99 \
       -I. -I./libretro-common/include \
       -o /tmp/vjss_dump /tmp/vjss_dump.c test/harness/harness.c -ldl -lm

   VJ_EXPECT_BUILD=$(./scripts/build-id.sh) \
     /tmp/vjss_dump ./virtualjaguar_libretro.dylib test/roms/yarc.j64 --frames 30 --quiet
   ```

4. Run the inspector on that state:

   ```bash
   ./test/tools/vjss_info /tmp/vjss_yarc.state
   ```

5. Lint:

   ```bash
   bash scripts/c89-lint.sh test/tools/vjss_info.c
   ```

## Acceptance gate (literal command + expected exit code / expected output)

```bash
bash scripts/c89-lint.sh test/tools/vjss_info.c
# exit 0

./test/tools/vjss_info /tmp/vjss_yarc.state
# stdout MUST contain: version=7
# stdout MUST contain: verdict=loadable
# stdout MUST contain: endian=le   (on Apple Silicon / Intel Mac)
# exit 0

# Negative check: truncating magic must fail
dd if=/tmp/vjss_yarc.state of=/tmp/vjss_bad.state bs=16 count=1 2>/dev/null
printf '\x00\x00\x00\x00' | dd of=/tmp/vjss_bad.state bs=1 seek=0 conv=notrunc 2>/dev/null
./test/tools/vjss_info /tmp/vjss_bad.state; echo exit:$?
# stdout MUST contain: verdict=bad_magic
# exit MUST be 1
```

## STOP conditions (abort triggers — report, do not improvise)

- You believe the header is big-endian on disk "because Jaguar is big-endian"
  — wrong; STOP and re-read the Critical fact above.
- Building `/tmp/vjss_dump` fails because of missing symbols — rebuild with
  `make TEST_EXPORTS=1`. If still failing after `make clean && make TEST_EXPORTS=1`,
  STOP and report.
- You are tempted to change `STATE_MIN_VERSION` or add a legacy loader — that
  is T2 / a maintainer decision, not this task.
- `test/roms/yarc.j64` is missing — STOP and report; do not download ROMs
  from the internet into the tree.

## Deliverable (exact commit message, PR title, PR body, issue comment text)

**Commit message:**

```
test(tools): add vjss_info savestate header inspector
```

**PR title:**

```
test(tools): add vjss_info savestate header inspector
```

**PR body template:**

```markdown
## Summary
- Standalone CLI that prints VJSS magic/version/flags and a loadability verdict.
- Handles host-endian `STATE_SAVE_VAR` layout (tries LE and BE).

Closes nothing. Supports triage for #268.

## Acceptance gate transcript
$ bash scripts/c89-lint.sh test/tools/vjss_info.c
<paste>
$ ./test/tools/vjss_info /tmp/vjss_yarc.state
<paste>
```

**Push + PR:**

```bash
git add test/tools/vjss_info.c
git status   # only allowlisted files
git commit -m "$(cat <<'EOF'
test(tools): add vjss_info savestate header inspector

EOF
)"
git push -u libretro HEAD
gh pr create --base develop --title "test(tools): add vjss_info savestate header inspector" --body-file - <<'EOF'
## Summary
- Standalone CLI that prints VJSS magic/version/flags and a loadability verdict.
- Handles host-endian `STATE_SAVE_VAR` layout (tries LE and BE).

Supports triage for #268.

## Acceptance gate transcript
(paste here)
EOF
```

**Issue comment on #268** (after PR exists):

```
Added a header inspector in PR <N>: `./test/tools/vjss_info your.state`
Please paste its one-line output for the two rejected files so we can see
the on-disk version field. Do not close this issue from that PR.
```
