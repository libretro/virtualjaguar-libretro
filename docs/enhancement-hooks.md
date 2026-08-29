# Per-title enhancement hooks

> See [**Patching a game**](patching-a-game.md) for a router across cheats, soft patches, hooks, and per-title defaults.

Issue [#370](https://github.com/libretro/virtualjaguar-libretro/issues/370),
track 4b of epic [#338](https://github.com/libretro/virtualjaguar-libretro/issues/338).

A **hook** is a small, verified byte patch that the core writes into the
loaded cartridge image at content load, keyed by the same CRC32 the
per-title defaults DB uses. It is the half of the per-title surface that a
`{key, value}` core-option pair cannot express.

**The table currently ships zero hook rows.** The mechanism is complete,
gated off, and CI-covered; rows land as data once a behaviour has been
researched to the standard in the checklist below. That is a deliberate
state, not an unfinished one — inventing a row to justify the array is
exactly what this feature must not do.

## Is my idea a hook?

> If an intervention can be expressed as a core-option value, it is a
> `TitleDBPair` and it ships today. `hooks[]` exists **only** for writes to
> emulated memory.

| Idea | Verdict |
|---|---|
| Targeted per-game byte fix | **Hook.** |
| Overclock preset, true color, internal resolution, blit memo | Not a hook — `pairs[]`, already shipping. |
| Frame pacing / VRR friendliness | Not a hook — host output, nothing written to emulated memory. Core options. |
| Widescreen | Not a hook as things stand. The Jaguar has no viewport-scaling stage a patch can flip; a wider picture needs the *game* to render a wider viewport. If research finds a title with a settable viewport width in ROM, that specific title becomes a hook. |
| A new controller type (e.g. a rotary) | Not a hook — an input device the core must implement. |
| "Game X runs at the wrong speed / input repeats" | **Not a hook.** Those are emulator timing bugs. Papering over our own inaccuracy with a per-game patch is the failure mode this feature must never become; such a proposal is re-filed against the accuracy track (#319 / #408). |

## Negative / known-bad entries (issue #464)

`pairs[]` and `hooks[]` only let a title **opt in** to something. There was
no way to record the opposite — "this setting is known to **break** this
title" — until issue #464 added `negative[]` to `TitleDBEntry`
(`src/core/titledb.h`). Same discriminator as the table above: a negative
entry is still a `{key, value}` pair (settings, not bytes), it just means
*refuse/warn* instead of *apply*.

```c
typedef struct {
   const char *key;     /* core option key */
   const char *value;   /* exact unsafe value, or TITLEDB_NEG_ANY_NONDEFAULT ("*") */
} TitleDBNegativePair;
```

`value` is either an exact string (only that one setting is known-bad) or
the sentinel `TITLEDB_NEG_ANY_NONDEFAULT` (`"*"`), meaning "every value
other than this option's own registered default is unsafe" — the natural
shape for an enhancement slider like a clock-scale preset, where every
non-1x step is the thing being called unsafe, not one specific step.

**Behaviour on match**, decided once in `libretro.c`'s
`get_variable_pertitle()` and never per-row (this stays a policy, not a
per-title knob):

- A **per-title default substitution** (the value a `pairs[]` row, or any
  other per-title mechanism, would apply because the user left the option
  untouched) that matches a negative entry is **refused**, with a
  `LOG_WRN`. A default must never be unsafe — refusing it costs the user
  nothing they asked for, since they never asked for it.
- The user's own **explicit** choice is **always honoured**, even if it
  matches a negative entry — refusing it would break the DB's one hard
  rule ("user-set values always win", already true for `pairs[]`). A
  warning is still logged, latched once per option key per content load,
  so a bug report against that title starts from the right hypothesis
  instead of a multi-session investigation (see #463 below).
- Gated by the same `virtualjaguar_pertitle_defaults` switch as the
  positive path — one feature, one on/off knob. Disabling per-title
  defaults silences both the substitution *and* the known-bad warning,
  which is the same "disable per-title defaults" bug-report step the
  clock-scale option text already asks for.

**Evidence bar — stricter than a positive row, not looser.** A negative
entry is a stronger claim than a positive one: it changes what a user is
*prevented* from getting by default, and it is trivially easy to justify
with a single bug report where the real cause was never isolated. The bar
is the same corpus-grade evidence every `pairs[]`/`hooks[]` row already
requires, restated for this shape in `src/core/titledb.c`'s `negative[]`
authoring checklist:

1. The regression was **reproduced and confirmed**, not hypothesized.
2. The specific value is **isolated** from every other explanation
   (emulator regression, a different timing model, an unrelated input
   class) — not merely "the option was live" (it changed *something*),
   but that changing it is what produced the specific symptom.
3. The row's comment cites the evidence the way every positive row does —
   a doc, a committed measurement, a closed-out reproduction — never "a
   user reported it" alone.
4. `TITLEDB_NEG_ANY_NONDEFAULT` is for a genuinely monotonic class of
   unsafe values, not a shortcut for "didn't test every step".
5. No key may appear in both `pairs[]` and `negative[]` of the same row
   with the same value — a row that both applies X and calls X unsafe is
   a table bug (`test/tools/test_titledb.c` asserts this mechanically).

**Case in point — why the table ships zero negative rows.** Issue #463
(Cybermorph, Codex level) is the motivating example: render-bound at stock
clocks, +43% frame rate at RISC 1.5x, and a user report of "ship movement
and altitude gone nuts" that reads like the exact symptom of a
frame-coupled simulation getting overclocked along with the picture. That
is a strong *hypothesis*. It is not evidence: every headless investigation
(byte-identical framebuffers vs. `develop`, every timing model tried,
14,400 fields with `crash_detect=verbose` and no signature) came back
clean, and the deciding experiment — is Cybermorph's simulation actually
frame-coupled? — has not been run. #463 sits `blocked` pending reporter
artifacts. Shipping a negative row on a hypothesis is exactly the folklore
this evidence bar exists to keep out; the mechanism lands in #464, the row
lands only if and when #463 clears it.

## Using it

Core option `virtualjaguar_enhancement_hooks`, **default `disabled`**.

- Read raw from the frontend at content load, never through the per-title
  substitution path — so a database row cannot switch its own gate on.
  `test_titledb` fails the build if any row names the gate key.
- Independent of `virtualjaguar_pertitle_defaults`: two features, two
  switches.
- Latched at load. Changing it mid-session logs
  `[hooks] enhancement hooks … takes effect on restart` and does nothing
  else — un-patching would need a saved original the design deliberately
  does not keep, and would desync anything running.
- Cartridge content only. CD content never reaches the table at all.

What you see in the log:

```
[hooks] <Title>: applied <name> @ $8xxxxx +4
[hooks] <Title>: applied 1/1
[hooks] <Title>: REFUSED <name> @ +$xxxxxx -- expected bytes not found
        (this is not the dump the hook was written for); no bytes written
```

## How it behaves

- **One trigger: cartridge ROM, at load.** No RAM patches, no per-frame
  enforcement, no watchpoints. A per-frame RAM write is what the cheat
  engine already is (`src/core/cheat.c`), and a user-authored ROM patch is
  what soft patching already is (`docs/rom-patches.md`).
- **No savestate story.** Cartridge ROM is not in the state blob and
  `JaguarReset()` never touches it, so a patch survives `retro_reset()` and
  a serialize/unserialize round trip with no re-apply, no new state field
  and no version bump. Both are asserted by `test_hook_gate --case on`.
  The accepted hole: a state saved with the gate on and loaded with it off
  (or netplay between peers that disagree) diverges with no marker. A
  fingerprint in the state header's unused `reserved` word is the
  mitigation if that ever bites; it is deliberately not in v1, because with
  zero rows it would distinguish nothing.
- **Run-ahead is safe** — it is intra-session, so both sides of every
  rollback see the same loaded image.
- **All-or-nothing per title.** Every hook in a row is validated first;
  one failure means zero bytes are written. A row is a single
  intervention, and half of one is a state no author ever tested.
- **`jaguarMainROMCRC32` is left at its pre-patch value** on purpose, so
  title identity, EEPROM naming, the Memory Track checks and frontend
  content hashing all keep seeing the dump the user supplied.

## The three fences that are not obvious

1. **GameDrive content is refused.** `JaguarLoadFileInternal` copies the
   first ≤6 MB flat into the cart window *and* hands the full image to
   `JGDLoadROM`, which `malloc`s a separate 16 MB copy. Bank switching
   serves reads out of that copy, so a patch to the flat window is
   silently defeated the moment the game switches banks.
   `JGDWriteROM8()` is the extension point if GD hooks are ever wanted.
2. **The cart entry vector `$400..$407` is refused.** The loader reads the
   entry point out of ROM into `jaguarRunAddress` *before* hooks run, and
   `retro_reset()` reuses that cached variable rather than re-reading ROM.
   A hook there would pass `expect[]`, write successfully, and do nothing
   — forever. This is the one failure mode `expect[]` cannot catch, so it
   gets an explicit range check.
3. **`TitleHook*` needs its own entry in the export lists.** The existing
   `TitleDB*` / `_TitleDB*` wildcards in `link-test.T` and
   `exports-test.list` do **not** match it. Forget that and the end-to-end
   test fails at `dlsym` looking exactly like an applier bug.

## Authoring a row

The record, in `src/core/titledb.h`:

```c
typedef struct {
   uint8_t        kind;     /* TITLEDB_HOOK_ROM_PATCH; 0 terminates */
   uint8_t        len;      /* 1..64 */
   uint32_t       offset;   /* PAYLOAD-relative; bus addr = $800000 + offset */
   const uint8_t *expect;   /* mandatory precondition, len bytes */
   const uint8_t *patch;    /* replacement, len bytes */
   const char    *name;     /* stable short id for the log line */
} TitleDBHook;
```

Bytes go in named `static const uint8_t` arrays beside the table so each
patch is a greppable, commentable object, and so the ~24 rows with no
hooks do not each grow by half a kilobyte.

A row is admissible only when all of the following hold:

1. `offset` is **payload-relative** — verified against a headerless dump,
   or with 512 subtracted from a headered one.
   `DetectPrependedHeaderSize()` strips the header before *both* the CRC
   and the copy into the cart window, so a raw hex-editor offset from a
   headered file is wrong by 512 while the CRC — the table key — is
   identical for both dumps.
2. `expect[]` was read out of the **shipped image** at that offset, and
   `crc32` is the CRC of that exact image.
   **Never inherited from an alias row.** The nine Doom EX rows
   deliberately reuse retail Doom's `pairs[]`; that is safe for settings
   (same engine, same rendering) and unsafe for bytes, because offset `$X`
   in a romhack is exactly the region the hack may have rewritten. That is
   why `expect[]` is mandatory rather than optional: a future contributor
   who clones the alias pattern for a hook row gets a refusal and a log
   line, not silent corruption.
3. The behaviour was determined by **our own analysis** — disassembly,
   `vjtrace`, `m68k_pc_histogram`, `gpu_disasm_dump`, `trace_dump` — and
   the comment cites which. No third-party patch data is copied,
   transliterated or mechanically converted into this table; behaviour
   facts (an address, a value) are free, someone else's expression is not.
4. The defect is a **game** defect, not one of ours.
5. Before/after measurement is committed as the row's evidence, the way
   every existing titledb row cites its census numbers.
6. `cart_boot_probe` boots the title clean with the hook on.
7. `frame_hash_ab` with the gate **off** is byte-identical to the
   pre-change build.

## Tests

| Test | What it gates |
|---|---|
| `test/test_titlehook` | Host unit test of the applier: clean apply, every refusal fence, and — on every refusal — that the image is byte-for-byte unchanged. No dlopen, no ROMs. |
| `test/test_titledb` | Invariants over the **shipped** table: `len`, non-NULL `expect`/`patch`/`name`, no no-op hook, proper termination, no intra-row overlap, unique names, no row naming the gate key, nothing on the entry vector. Currently vacuous by design — it exists so the *first* row added is checked before it reaches a user. |
| `test/tools/test_hook_gate` | End-to-end through the real core on `yarc.j64`: gate on patches and logs; gate off (the default) does not; a deliberate `expect[]` mismatch refuses and logs; the patch survives `retro_reset()` and a state round trip. |
| `test/tools/hook_identity_ab.sh` | Stock-path identity: per-frame framebuffer-hash CSVs are byte-identical with the gate at its default, explicitly disabled, and explicitly enabled — plus a base-vs-base determinism control, because a nondeterministic run makes every other comparison meaningless. |
| `test/test_titledb` (negative-entry section) | `TitleDBUnsafeValue()` lookup: no content, unknown content, exact-value match, wildcard (`"*"`) match against a caller-supplied default, the test-only override, and the shipped-table integrity checks (`negative[]` termination, key/value non-empty, no key=value shared with `pairs[]`, no duplicate within a row). |
| `test/tools/test_pertitle_db --case 7/8` | End-to-end through the real core on AvP: a negative row on `virtualjaguar_true_color=enabled` (the value AvP's own `pairs[]` row would substitute at default) is **refused** with a logged warning when it would apply as a default (case 7), and **honoured** with a logged warning when the user sets it explicitly (case 8). Installed programmatically via `TitleDBSetNegativeForTest()`, same "no canary row in the shipped table" reasoning as the hooks gate test. |

All four run on repo-resident public content, so all four gate CI — and
`hook_identity_ab.sh` enforces that for itself by counting the ROMs it
compared and failing on zero, rather than trusting the ROMs to be present.

**What the A/B does not cover.** With no shipped row carrying a hook, the
applier's body is unreachable in every configuration that ships today, so
`hook_identity_ab.sh` proves the applier is *unreached*, not that it is
*inert* — replace its body with a `memset` and the A/B still passes. The
applier's own behaviour is covered by `test_titlehook` and
`test_hook_gate`, which install hooks programmatically; those are the
tests that go red if it breaks. Read the A/B as the stock-path identity
guard it is: it catches an option that perturbs rendering merely by being
registered, which is a real hazard, but not a broken patch path.

The end-to-end test installs its hooks with `TitleDBSetHooksForTest()`
rather than adding a canary row to the shipped table. That is not
squeamishness: `test_pertitle_db --case 5` uses `yarc.j64` as the
deliberate *non-DB control* and asserts that no CRC match happens, so any
yarc row would break it — and test scaffolding does not belong in front of
users anyway.
