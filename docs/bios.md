# BIOS and boot ROMs

**No BIOS files are required.** Both Jaguar console boot ROMs (Series K and Model M)
and both Jaguar CD BIOSes (retail and developer) are embedded in the core
(`src/bios/`). External images are an optional override for people who want a
specific revision.

Moved here from the top-level README. Behaviour below is from `libretro.c`
(`stage_cart_boot_rom`, `load_external_cart_boot_rom`, `load_external_cd_bios`,
`try_load_cd_bios_file`) and `libretro_core_options.h`; the core logs which image it
staged and where it came from at every boot, so check the frontend log first.

## Boot modes

- **Cartridges.** `BIOS (Cartridges)` (`virtualjaguar_bios`) chooses between the HLE
  BIOS (default: the core does the boot setup itself and skips the boot animation) and
  the real boot ROM. GPU-only / jagcrypt carts (the BootIntro demos) turn the real boot
  ROM on even when this is set to HLE, because they contain no 68K program for the HLE
  path to start.
- **CD discs.** `CD Boot Mode` (`virtualjaguar_cd_boot_mode`) chooses between the HLE CD
  BIOS (default) and a real CD BIOS (`Real BIOS`; `Auto` currently behaves the same).
  It overrides the cartridge setting for CD content. Audio-only (Red Book) discs always
  use the real BIOS. If a real-BIOS mode is selected but no CD BIOS can be staged, the
  core falls back to HLE.

## Cartridge boot ROM

`Cart BIOS Type` (`virtualjaguar_bios_type`, restart required) picks the console boot
ROM used on the real-BIOS cartridge path:

| Setting | Image |
| --- | --- |
| `Series K` (default) | The original Jaguar boot ROM. Embedded. |
| `Model M` | The later revision (patch address `$4804`) most size-coded BootIntros target. Embedded; an optional `jagboot_m.rom` (exactly 128 KB) in the root of the `system` directory replaces it. That file is read as-is, with no checksum or identification step. |
| `Custom` | A 128 KB image loaded from the `system` directory. |

For `Custom`, the core searches these names in this order, and for each name tries
`system/`, then `system/Atari - Jaguar/`, then `system/jaguar/`:

`jagboot.rom`, `boot.rom`, `boot0.rom`, `[BIOS] Atari Jaguar (World).j64`,
`[BIOS] Atari Jaguar Stubulator '94 (World).j64`,
`[BIOS] Atari Jaguar Stubulator '93 (World).j64`

Filename is the outer loop, so a `jagboot.rom` in a subfolder wins over a `boot0.rom`
in the root. The file must be exactly 128 KB. Any content is accepted: the CRC32 is
only used to log the name of a recognized dump (Series K, Model M, Stubulator '93,
Stubulator '94); an unrecognized image loads with a warning. If `Custom` is selected
and nothing usable is found, the core logs a warning and uses the embedded Series K.

## CD BIOS override

Only in the real-BIOS CD path. A CD BIOS file in the `system` directory takes
precedence over the embedded images (the `CD BIOS Type` option only chooses the
embedded image when no file is found). Accepted names:

| Type | Filenames |
| --- | --- |
| Retail | `[BIOS] Atari Jaguar CD (World).j64` / `.rom` / `.bin` |
| Developer | `[BIOS] Atari Jaguar Developer CD (World).j64` / `.rom` / `.bin` |
| Generic | `jaguarcd_bios.bin`, `jagcd_bios.bin`, `jaguarcd.bin`, `jagcd.bin`, `Jaguar CD BIOS.rom`, `Jaguar CD BIOS.bin` |

Search order: the selected `CD BIOS Type`'s names first, then the generic names, then
the other type's names. Within each of those three groups the core walks the
directories (`system/`, `Atari - Jaguar/`, `Atari - Jaguar CD/`, `jaguar/`,
`jaguarcd/`) and, inside each directory, the names. Unlike the cartridge search,
directory is the outer loop within a group, so a root-level file beats a subfolder
file of an earlier name in the same group. A lone file of the "wrong" type still beats
the embedded image.

The filename only decides what is tried first. The contents decide what is accepted:

| Outcome | Condition |
| --- | --- |
| Recognized | Exactly 256 KiB and CRC32 matches the retail or developer dump. Loaded and logged by its real revision, whatever the file is called. |
| Unrecognized | Exactly 256 KiB, unknown CRC32, but the big-endian run address at offset `$404` lies in `$800000-$840000`. Loaded with a warning naming the file as the prime suspect if boot black-screens. |
| Rejected | Wrong size, short read, or a run address outside that window. Skipped. |

If real-BIOS boots misbehave, remove or rename any BIOS files in `system/` to fall back
to the known-good embedded images.
