# Atari - Jaguar CD thumbnails

Hosted on the Virtual Jaguar site (https://jaguar.provenance-emu.com/thumbnails/) until
libretro-thumbnails has an "Atari - Jaguar CD" repository (libretro-thumbnails/libretro-thumbnails#141).
The Virtual Jaguar desktop app falls back to this mirror for disc titles that libretro-thumbnails
does not have, so upstream art wins as soon as it exists. URL layout is the same as
thumbnails.libretro.com: `<base>/Atari - Jaguar CD/<Named_Boxarts|Named_Titles|Named_Snaps>/<name>.png`
(spaces and punctuation URL-encoded). Offered to libretro-thumbnails in
libretro-thumbnails/libretro-thumbnails#141; the site also serves every image as one zip,
`<base>/Atari - Jaguar CD.zip`, built by `scripts/build_site.py`. It unzips to `Atari - Jaguar CD/Named_*/`.

Layout follows libretro-thumbnails: `Named_Boxarts/`, `Named_Titles/`, `Named_Snaps/`, one PNG per
game, named exactly after the game name in libretro-database
`metadat/redump/Atari - Jaguar CD.dat` (27 entries, 26 unique names; "Vid Grid (USA)" appears twice
in the dat and so yields one file). The characters `&*/:\`<>?\|"` would be replaced with `_`; none of
the 23 generated names contain any.

## Counts (dat has 26 unique names)

| Folder | Files |
| --- | --- |
| Named_Boxarts | 20 |
| Named_Titles | 23 |
| Named_Snaps | 19 |

`SOURCES.tsv` has one row per dat name: TGDB game id, TGDB title, image URL, and which emulator frame
was used for the title and snap.

## Boxart (TheGamesDB)

- Mapping came from Provenance's offline TGDB dump (`thegamesdb.sqlite`, platform 29 "Atari Jaguar CD"),
  not from scraping. Matching was by normalized title (HTML-unescaped, case/punctuation/spacing
  ignored) and checked by eye against each row.
- Image: `https://cdn.thegamesdb.net/images/original/boxart/front/<id>-1.jpg` (front cover), fetched
  sequentially with a delay, converted to PNG with `sips`. Width over 512 px was scaled to 512 px wide;
  smaller sources (Frog Feast, Robinson's Requiem, Vid Grid) are kept as-is. Metadata stripped,
  recompressed with ImageMagick.
- TGDB has a single entry per title, so regional/reissue dat entries share one cover:
  both Blue Lightning entries (TGDB region_id 2; ids are raw, the dump has no region table, so
  region mismatches against the Redump names could not be checked), both World Tour Racing, both Vid Grid, and the two
  Songbird reissues (Battle Morph, Iron Soldier 2) use the generic title's cover; whether TGDB's
  artwork matches the reissue was not verified.
- TGDB title quirks accepted: "Battlemorph", "Brain Dead 13", "Frog Feast CD", "Robinson's Requiem CD",
  "Philia the sequal to Elansar" (not the separate "Elansar" entry). "Myst [Demo]" was not used for Myst.
- Missing (no TGDB platform-29 entry): Beebris, Ocean Depths, Simone, Demolition Man & Commander Blood,
  Varuna's Forces Disc 1 and Disc 2. No guess was made from near matches.
- Copyright: box art is third-party publisher artwork hosted by TGDB; libretro-thumbnails already hosts
  scans of this kind ("Substitutions" / Credits in its README), but the maintainer should decide.

## Titles and snaps (generated with this emulator)

- Core: libretro/release/v3.8.1 @ 550a695, built with TEST_EXPORTS=1 on the LAN build host, default
  core options (HLE CD boot, no shaders, no filtering, no enhancements, 1x).
- Tool: `test/tools/hires_shot` (shared harness), headless, frame dumps every 300 frames up to 12000
  (frame dumps every 200 up to 7000 for the scripted-input pass). Contact sheets were reviewed by eye
  and one frame picked per image.
- Disc images identified by CRC32 of the first track against the dat (all 23 matched). The Beebris-based
  homebrew discs (Ants, Fast Food 64, Frogz 64, Klax, Saucer Wars) share Beebris's track 1 and were
  skipped: not in the dat. Duplicates by content (Blue Lightning, Vid Grid, World Tour Racing) have
  identical frames, so those pairs are byte-identical images.
- Input-driven pass (`option`/`a` pressed every 400 frames from frame 900): needed for Frog Feast,
  Hover Strike, Alice's Mom's Rescue, Philia (English menu), Myst. Everything else is the no-input run.
- Size: native framebuffer, unscaled (libretro-thumbnails: 512 px wide or narrower is added as-is):
  326x240 for most discs, 326x224 Alice's Mom's Rescue, 326x241 Frog Feast, 260x240 Iron Soldier 2 and
  Myst, 186x240 for the World Tour Racing snaps. The core reports a 4:3 display aspect, so the narrow
  modes appear horizontally stretched in the game itself; thumbnails are not aspect-corrected.
- Not generated (disc image not available on the host): Demolition Man & Commander Blood, Varuna's
  Forces Disc 1 and 2.
- Titles exist for all 23 discs that were on the host; all 23 booted.
- No snap for Beebris, Ocean Depths, Philia, Simone: these only ever show a static start screen and the
  scripted presses did not get past it, so a snap would duplicate the title.
- Snap quality caveats: several discs are FMV/cutscene heavy and the snap is an intro/attract/briefing
  frame rather than gameplay: Baldies (clay intro), Highlander, Space Ace, Dragon's Lair, BrainDead 13,
  Vid Grid (intro clip), Robinson's Requiem, Myst (starfield intro), Hover Strike (mission briefing).
  Real gameplay snaps: Battle Morph, Blue Lightning, Frog Feast, Alice's Mom's Rescue, Iron Soldier 2,
  Primal Rage, World Tour Racing. Better gameplay snaps for the rest would need a human at the pad.
- Headless caveat from the repo docs applies: confirm in RetroArch if exact presentation matters.

## Upstream status (2026-10-17)

For RetroArch to show these without the mirror, four things have to land:

1. **Thumbnail repo.** `libretro-thumbnails/Atari_-_Jaguar_CD` does not exist yet. Only an org
   maintainer can create it; requested in libretro-thumbnails#141. After that, these files go in by
   PR, and a submodule entry goes into libretro-thumbnails/libretro-thumbnails.
2. **Database.** libretro-database already has `metadat/redump/Atari - Jaguar CD.dat` (one Track 1
   CRC per game), but no `rdb/Atari - Jaguar CD.rdb`. The rdb is built by libretro-super's
   `libretro-build-database.sh`, which needs a `build_libretro_database "Atari - Jaguar CD" "rom.crc"` line.
3. **Core info.** `virtualjaguar_libretro.info` has to list the database:
   `database = "Atari - Jaguar|Atari - Jaguar CD"`.
4. **RetroArch scanner.** Every Jaguar CD track is `AUDIO` in the cue sheet. RetroArch's cue and CHD
   CRC paths hash only data tracks, so a scan computes no CRC and an rdb alone matches nothing. The
   fix has to hash Track 1.

Until all four land, Manual Scan with the custom system name `Atari - Jaguar CD` labels Redump-named
discs by file name, and that is enough for thumbnails, once the repo from item 1 exists.
