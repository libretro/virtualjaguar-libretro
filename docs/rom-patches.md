# ROM patches (soft patching)

Fan patches and romhacks exist for several Jaguar titles.  With this core you
do **not** patch the ROM file: RetroArch applies patches at load time ("soft
patching"), leaving the original file untouched.  This works today and needs
no core option.

## How it works

The core reports `need_fullpath = false`, so the frontend loads cartridge
content into memory and patches that buffer before the core sees it.  (This
contract is pinned by `test/tools/test_memory_map.c`.)  RetroArch supports
**IPS, BPS, UPS, and xdelta** patches.

Place the patch next to the content, named after it:

    Doom (World).j64
    Doom (World).ips        <- applied automatically

- Multiple patches chain in order: `rom.ips`, `rom.ips1`, `rom.ips2`.
- For zipped content, where the patch goes depends on how the content path
  names the archive. If the frontend passed a bare archive path (`game.zip`,
  no inner-file delimiter), the patch sits beside it and takes the
  **archive's** basename: `game.zip` + `game.ips`. If the path names an entry
  inside the archive (`comp.zip#inner.j64`), RetroArch's basename resolution
  cuts at the `#` delimiter and the patch instead takes the **inner file's**
  basename, in the archive's directory: `comp.zip#inner.j64` + `inner.ips`.
  Both shapes occur in practice depending on how the content was scanned.
- `--no-patch` on the RetroArch command line disables patching; `--ips`,
  `--bps`, `--ups`, `--xdelta` name a patch explicitly.
- Other libretro frontends may support fewer formats or none.

## Pick the right dump — IPS has no safety net

**An IPS patch carries no source checksum.**  Applied to the wrong dump it
silently produces a corrupt ROM — no error, just glitches or a black screen.
BPS and UPS patches carry and verify a source CRC32 and will refuse a wrong
base; RetroArch's xdelta decoder (`vcdiff_decode`) does not verify one
either, so a wrong base under xdelta fails the same silent way IPS does.
Every row below names the dump its patch is known to target; verify your
file's CRC32 first (`crc32 <file>`, or check the core log, which prints it).

## Enhancement defaults on patched ROMs

The per-title defaults DB (`src/core/titledb.c`) is keyed by CRC32, and a
patched ROM hashes differently from its retail base.  The Doom EX builds
catalogued below have alias rows, so they keep Doom's 2x + true-color
defaults.  Any other patched ROM falls through: the core logs
`[titledb] no per-title entry for CRC32 $XXXXXXXX` and you set enhancement
options by hand.

## Jaguar CD content cannot be soft patched

For CD images (`.cue`/`.cdi`) the core opens the file by path, so the
frontend's patched buffer is discarded.  Workaround: patch the `.bin`
offline (e.g. with Flips or `xdelta3`) and load the patched image.

## Known patches

Patches are **linked, not hosted** — they are third-party works.  "Target
CRC" is the CRC32 of the unpatched dump the patch is known to apply to;
"Patched CRC" identifies the result (and its titledb alias row, where one
exists).

### Doom EX family (base: Doom, CRC32 5E2CDBC0)

| Patch | Format | Patched CRC32 | Boot-verified | Author / source |
|---|---|---|---|---|
| JagDoomEX | IPS | 754096DB | yes | ChillyWillyGuru — [JagDoomEX](https://github.com/ChillyWillyGuru/JagDoomEX) (Linux/original build); companion Windows build [JagdoomE](https://github.com/Tolbat/JagdoomE), whose README credits Tolbat and ChillyWillyGuru as its primary developers |
| JagDoomEX 2 | IPS | 4643E9DB | yes | unknown |
| JagDoomEX 3 | IPS | 35743B9C | yes | unknown |
| JagDoomEX 4 | IPS | AD6B68BA | yes | unknown |
| JagDoomEX 5 | IPS | C4F4CACF | yes | unknown |
| JagDoomEX 6 | IPS | 1F4EE4A5 | yes | unknown |
| JagDoomEX [spectral] | IPS | 013A5359 | yes | unknown |
| JagDoomEX [transparent] | IPS | B92D1CA3 | yes | unknown |
| JagDoom2EX | IPS | EA12E234 | yes | unknown. No source names an author or explains the "2" — likely not a Doom II conversion: the patched image differs from the plain JagDoomEX build by only 1,679 of 4,194,304 bytes (measured in issue #409 task 3), far too small for a level/texture swap. What specifically distinguishes this build is unconfirmed. |

### Other titles

| Patch | Title | Format | Target dump / CRC | Author / source |
|---|---|---|---|---|
| Checkered Flag steering fix | Checkered Flag | IPS | unknown (no CRC32 published in any source found) | Cyrano Jones (AtariAge) — [Reboot-Games writeup](https://www.reboot-games.com/rebootnews/checkered-flag-steering-patch/), discussed on [AtariAge](https://forums.atariage.com/topic/358509-checkered-flag-just-got-awesome/) |
| Super Cross 3D – No Stadium | Super Cross 3D | IPS | Super Cross 3D (1995).jag — CRC32 4A08A2BD | Sporadic (AtariAge) — [AtariAge thread](https://forums.atariage.com/topic/379109-supercross-3d-rom-hacking-for-performance-there-is-none/), posted 2025-01-29 |
| Super Cross 3D – No Stadium or Rails | Super Cross 3D | IPS | Super Cross 3D (1995).jag — CRC32 4A08A2BD | Sporadic (AtariAge) — [AtariAge thread](https://forums.atariage.com/topic/379109-supercross-3d-rom-hacking-for-performance-there-is-none/), posted 2025-01-29 |
| Flashback jingle restore | Flashback | IPS | unknown (a search engine's summary of the unreadable romhacking.net listing states the patch applies to any Jaguar ROM extension — `*.rom`, `*.j64`, `*.jag`, etc. — provided it passes the CRC check, but no CRC32 value appears in that summary) | unknown — [romhacking.net hack #6135](https://www.romhacking.net/hacks/6135/) (page is Cloudflare-gated to automated fetches; author not visible in indexed search results) |
| Wolf3dJagPatch | Wolfenstein 3D | xdelta | unknown — a search engine's summary of the (unreadable) romhacking.net listing states a "file" SHA-1 EE553176F0A32683B517B84B12C6FAE13C15C3D0 / CRC32 E91BD644, but whether that identifies the required base dump or the patch file itself is not stated in that summary | unknown — [romhacking.net hack #5650](https://www.romhacking.net/hacks/5650/) (page is Cloudflare-gated to automated fetches; author not visible in indexed search results) |

Additions welcome: PR a row with the patch's source link, its target dump
CRC, and (ideally) a note that the patched build boots in this core.
