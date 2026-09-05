# Virtual Jaguar libretro v3.6.1

**A patch release for one thing: booting the core with no content now lands you
in the CD BIOS, which is the screen you insert a disc from.**

v3.6.0 shipped no-content boot (#646) and the disk-control interface (#651) so
you could launch the core bare, put in a disc, and reach the Virtual Light
Machine with an audio CD. That combination did not work. This fixes it.

---

## The fix

No-content boot resolved to a **bare console**: the boot ROM running against a
zeroed cartridge window. That is authentic for a Jaguar with no CD unit
attached — and it is a dead end. The CD BIOS is what offers "insert a disc",
and it is mapped as a cartridge at `$800000`, so nothing reachable from the
empty-slot screen could ever get to it. The headline feature of v3.6.0 was
unreachable by construction.

Starting with no content now boots the **real CD BIOS**, which comes up on its
own insert-disc screen. No files are required: an external CD BIOS ROM in your
system directory is preferred if you have one, and otherwise an embedded image
is used.

Two smaller consequences of the same path, fixed alongside:

- It reported cartridge mode rather than **CD mode with an empty tray**, which
  inverted the options menu against a machine sitting in the CD BIOS — CD Boot
  Mode and CD BIOS Type were hidden, and the cartridge-BIOS option was shown.
- The **Memory Track** cart plugs into the CD unit and is present whether or not
  a disc is, but it stayed absent until the first insert.

## `supports_no_game` was never advertised

`dist/info` still said `supports_no_game = "false"` after
`RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME` shipped in v3.6.0. RetroArch reads the
**info database**, not the runtime environment call, to decide whether to offer
a contentless launch — so frontends were never told the core supports it. Now
`"true"`.

If your frontend does not offer "Start Core" without content, its info file is
out of date; update it from the libretro info repository.

## Why the tests did not catch this

Two tests asserted which boot *strategy* resolved and never checked what the
machine did with it, so a correctly-resolved dead end passed cleanly. Both now
pin the corrected behaviour.

One of them is worth noting for anyone reading the diff: `test_disk_control`
case 1 compared strategy names either side of a disc insert. With no-content now
resolving to `"bios"` — and a real-BIOS disc also resolving to `"bios"` — that
comparison **would have gone vacuous**, passing while asserting nothing. It now
pairs the strategy name with `isCDGame`, which is what actually moves: false for
the bare BIOS on its insert screen, true once a disc is mounted.

---

## Upgrading

Nothing to do beyond installing the core. Savestates, per-title defaults, hooks
and cheats are unchanged from v3.6.0.

**If you reported the v3.6.0 no-content boot as broken, check which core you
have installed.** The original report for #726 was made against a v3.5.1 core —
a version that predates the feature entirely — which is worth ruling out before
anything else.

## Known limitations

Unchanged from v3.6.0:

- Inserting a disc restarts the console. A continuous swap depends on CD BIOS
  disc-presence polling that has not been verified.
- The enhancement profile governs `internal_resolution` and `true_color` only.
- `docs/WHATSNEW` has no sections for v3.1.0 through v3.5.1; the per-release
  notes in `docs/RELEASE_NOTES_v*.md` are authoritative for those.
