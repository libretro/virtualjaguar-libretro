# Windows Store listing draft — English (United States)

Product: **Virtual Jaguar**, Store ID `9PP6567FHBMS`.
Submission: `1152921505702250954` (draft; never submitted by the packaging workflow).
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

## Certification notes (draft; qualify before sending)

This is a native Win32 SDL3/OpenGL desktop emulator packaged as MSIX, not a PWA
or Xbox console app. The runFullTrust capability supports ordinary desktop game
file/folder selection, controllers, OpenGL and optional networking. There is no
installer, elevation request or app self-updater inside the package. App code
updates are delivered by Microsoft Store.

Use File > Open or the library's folder selection to import test content.
Keyboard/gamepad controls and current limitations are documented at the project
website. Supply a verified redistributable test ROM and exact steps with the
final candidate; do not give reviewers private ROM corpus paths. Do not claim
that Store installation or GUI qualification passed until recorded evidence exists.

## Remaining before certification

- Qualified Windows x64/ARM64 MSIX artifacts and install/upgrade/GUI evidence.
- Accurate category, privacy declarations, IARC questionnaire and capability review.
- Screenshots from the actual Windows candidate and required Store image sizes.
- Review support/privacy URLs and price/market settings; await tax/payout validation.
- Confirm the assigned publisher display-name spelling in Partner Center.
- Explicit maintainer release decision; this document is not authorization to publish.
