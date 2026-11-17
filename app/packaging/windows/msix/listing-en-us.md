# Windows Store listing draft — English (United States)

Product: **Virtual Jaguar**, Store ID `9PP6567FHBMS`.
Submission `1152921505702250954` for v3.12.0 failed certification under
[Microsoft Store policy 10.13.10](https://learn.microsoft.com/windows/apps/publish/store-policies#1013-gaming-and-xbox),
which excludes game-system emulators on all device families. The report also
flagged the age rating, non-Windows screenshots, and app category. Those listing
changes alone cannot resolve the emulator policy finding.
Planning price: **USD 3.99**, regional prices to review in Partner Center.

## Short description

Play your Atari Jaguar and Jaguar CD games with controller support, save states and a desktop game library.

## Description

Virtual Jaguar brings Atari Jaguar and Jaguar CD emulation to your Windows PC.
Organize your game collection in a visual library, connect a controller, and play
in a desktop window or a controller-friendly TV interface.

Save states let you return to a moment in a game. Configure your controls,
use the on-screen Jaguar keypad, and customize the picture with shader presets.
Optional RetroAchievements support connects compatible games to your account.

Features include cartridge and CD image loading, CHD support, playlists for
multi-disc games, per-game settings, controller remapping, screenshots, cheats,
and built-in debugging tools for people exploring Jaguar software.

You supply your own game files. No commercial games are included. Compatibility
and performance vary by game and PC; see the project website for compatibility
information and documentation. An internet connection is used for optional
online features such as achievements and artwork downloads.

The Microsoft Store edition is a paid way to support continued development and
receive Store-managed installation and updates. Free desktop downloads with the
same core features are also available from the project website.

Virtual Jaguar is an independent open-source project and is not affiliated with
or endorsed by Atari. Atari and Jaguar names belong to their respective owners.

## Feature bullets

- Atari Jaguar cartridge and Jaguar CD emulation
- Game library with favorites, recently played games and artwork
- Save states and per-game configuration
- Gamepad remapping and on-screen Jaguar keypad
- Controller-friendly TV interface
- Shader presets and optional RetroAchievements support
- CHD disc images and multi-disc playlists
- Debugging and inspection tools for Jaguar software

## Links

- Website: https://jaguar.provenance-emu.com/
- Support: https://github.com/libretro/virtualjaguar-libretro/issues
- Privacy: https://jaguar.provenance-emu.com/privacy.html
- Source/license: https://github.com/libretro/virtualjaguar-libretro

## Keywords (draft)

Jaguar; Atari Jaguar; Jaguar CD; emulator; retro; gamepad; save states

## Certification notes (paste into Partner Center after candidate qualification)

Virtual Jaguar is a native Windows desktop emulator distributed as MSIX. No
account, controller, or external BIOS file is required for this cartridge test.
The app package does not include games. Download the optional test cartridge
separately from its author's project:

https://raw.githubusercontent.com/42Bastian/JaguarDemos/1954a05fafd1489bcd3273012df6ca8fc731785d/yarc/yarc.j64

1. Save the file as `yarc.j64`. Launch Virtual Jaguar from Start.
2. Press **Ctrl+O** (or choose **File > Open...**) and select `yarc.j64`.
3. Confirm that the demo shows an animated picture in the game view. If audio
   is enabled, the demo also plays music.
4. On a candidate with shader support, choose **View > Shader** to toggle the
   filter. Confirm the picture changes and the demo keeps running.
5. If a step fails, please include the step number, app version, device and
   Windows build, and any visible error or screenshot in the certification
   report.

YARC (Yet Another Rotating Cube) is by 42Bastian. Its music is *Cream of the
Earth* by Romeo Knight / Red Sector Inc., adapted for Jaguar. The link points
to the author's published copy.

### Maintainer verification before submitting

- Run the steps on the exact Store MSIX candidate on clean Windows x64 and
  ARM64. Record the package version and Windows build. Keep step 4 only if
  that candidate exposes it and it passes.
- Expected SHA-256 for the author-hosted cartridge (byte-identical to the CI
  fixture) is `ea24adaf33265727cdada00f6f2b9d3869d69bb9c86335a9f81152a540b7e1c5`.
- The [upstream license](https://github.com/42Bastian/JaguarDemos/blob/1954a05fafd1489bcd3273012df6ca8fc731785d/LICENSE)
  grants redistribution of the project's software. The
  [demo credits](https://github.com/42Bastian/JaguarDemos/blob/1954a05fafd1489bcd3273012df6ca8fc731785d/yarc/README.md)
  identify third-party music. Use the author-hosted download until permission
  covering the embedded music is documented. A website ZIP can instead use an
  original test cartridge with cleared assets.

## Remaining before certification

- Qualified Windows x64/ARM64 MSIX artifacts and install/upgrade/GUI evidence.
- Accurate category, privacy declarations, IARC questionnaire and capability review.
- Screenshots from the actual Windows candidate and required Store image sizes.
- Review support/privacy URLs and price/market settings; await tax/payout validation.
- Confirm the assigned publisher display-name spelling in Partner Center.
- Explicit maintainer release decision; this document is not authorization to publish.
