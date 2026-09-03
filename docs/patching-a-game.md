# Patching a game

Four different things get called "patching", and this core supports all four
through different doors. Start here and follow the one that matches what you
have.

| I have… | Use | Doc |
|---|---|---|
| A hex code from a cheat site | **Cheats** | [below](#i-have-a-hex-code-from-a-cheat-site) |
| An `.ips` / `.bps` / `.ups` file (translation, romhack) | **Soft patching** | [`rom-patches.md`](rom-patches.md) |
| Specific bytes I want changed in one game | **Enhancement hooks** | [`enhancement-hooks.md`](enhancement-hooks.md) |
| A game that needs different *emulator settings* | **Per-title defaults** — not a patch at all | [`enhancement-hooks.md`](enhancement-hooks.md#is-my-idea-a-hook) |

If you are not sure which of the last two you want, the "Is my idea a hook?"
table in `enhancement-hooks.md` draws that line properly: **if it can be
expressed as a core option value, it is a per-title default, not a patch.**

---

## I have a hex code from a cheat site

Jaguar cheats are Pro Action Replay / GameShark style `ADDRESS:VALUE` codes.

Enter them through your frontend's cheat UI (in RetroArch: **Quick Menu →
Cheats**), or put them in a `.cht` file the frontend loads. Nothing goes in the
system directory and no core option needs turning on.

Multiple codes in one entry are separated with `+`:

```
00123456:00FF+00123458:0001
```

The core applies them every frame, so a code keeps working across resets and
savestates.

---

## I have an IPS / BPS / UPS patch

This is the route for translations and romhacks — anything distributed as a
patch file to be applied to a ROM you already own.

Put the patch file **next to the ROM, with the same base name**:

```
Cybermorph (1993).jag
Cybermorph (1993).ips
```

Your **frontend** applies it before the core ever sees the image; the core
receives an already-patched ROM and has no idea a patch was involved. In
RetroArch this is on by default; the relevant setting is **Settings → Core →
Load Content-Specific Core Options / soft patching**.

**Cartridges only.** A CD image is not a ROM file the frontend can patch this
way. Full rules, including the multi-format precedence order, are in
[`rom-patches.md`](rom-patches.md).

---

## I want to change specific bytes in one game

That is an **enhancement hook**: a byte patch into the cartridge image, keyed
on the ROM's CRC32, applied once at load.

Two ways to get one, and until now only the first existed.

### Built into the core

Hooks shipped in the core's own table (`src/core/titledb.c`). Authoring one is
a pull request — see [`enhancement-hooks.md`](enhancement-hooks.md) for the
schema, the fences, and what qualifies.

### Your own hook file

Create `vj_hooks.txt` in your frontend's **system directory** (the same place
`vj_netlink.txt` goes):

```
# Lines starting with # are comments.
# crc= opens a section; every hook= under it belongs to that title.

crc=DC187F82
hook=my-patch 0x0012A4 4E71 4E75
```

A `hook=` line is four fields:

| Field | Meaning |
|---|---|
| `my-patch` | a short name, used in the log line |
| `0x0012A4` | **payload-relative** cart offset (bus address = `$800000 +` this) |
| `4E71` | the bytes you expect to find there **already** |
| `4E75` | the bytes to write instead |

**You must turn hooks on.** Set the core option **Enhancement Hooks**
(`virtualjaguar_enhancement_hooks`) to `enabled` — it is off by default, and a
hook file cannot turn its own gate on.

Things worth knowing before you write one:

- **`expect` is mandatory and is checked.** If the bytes at that offset are not
  what you said, the patch is refused and logged. This is what stops a hook
  written for one dump from corrupting a different one.
- **All or nothing.** If any hook in your file fails to apply, *none* of them
  are written.
- **Any malformed line discards the whole file.** Not just that line — a
  half-understood patch set is more dangerous than none. The log says which
  line.
- **Your file wins over a built-in hook** for the same CRC, and the override is
  logged naming both.
- **Offsets are payload-relative.** A 512-byte copier header is stripped before
  both the CRC and the load, so an offset read out of a headered dump in a hex
  editor is 512 bytes too high while the CRC is identical for both dumps. This
  is the single most likely reason a hook you wrote gets refused.
- **Cartridges only**, same as the built-in table. CD content and RAM-loaded
  executables are refused.
- Maximum 4 hooks per title, 64 bytes each.

Check the frontend log if a hook does not take effect — every refusal says why.

---

## I want the emulator to behave differently for one game

Not a patch. The core carries a per-title defaults database that sets *core
options* for recognized titles — internal resolution, idle-skip, and so on —
and that is almost always what you actually want. See
[`enhancement-hooks.md`](enhancement-hooks.md#is-my-idea-a-hook).

A per-title default can never override a setting you chose yourself.
