# Per-title enhancement hooks

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

All four run on repo-resident public content, so all four gate CI.

The end-to-end test installs its hooks with `TitleDBSetHooksForTest()`
rather than adding a canary row to the shipped table. That is not
squeamishness: `test_pertitle_db --case 5` uses `yarc.j64` as the
deliberate *non-DB control* and asserts that no CRC match happens, so any
yarc row would break it — and test scaffolding does not belong in front of
users anyway.
