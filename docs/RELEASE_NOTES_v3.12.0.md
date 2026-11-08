# Virtual Jaguar v3.12.0

Graphics startup is checked before the main window opens, fresh installs use a
mild CRT filter, and cheats support editable values and one-shot application.
This release also adds an Emulation Server library browser and expands package
and graphics testing across desktop platforms.

## Graphics and startup

- An animated initialization screen accompanies graphics qualification before
  the main window opens. Windows can select the bundled software OpenGL path
  when the system driver is insufficient (#1183).
- Fix bundled Mesa shutdown hangs and repeated-start problems on Windows (#1167).
- The mild `vj-scanlines` preset is enabled when no shader choice was saved.
  Existing choices remain intact; a failed default filter leaves the plain
  picture usable. `--shader NAME|FILE|off` overrides the filter for one run (#1159).
- Windows x64 and ARM64 use a shader runtime built from pinned source instead
  of the upstream DLL with additional legacy DirectX dependencies (#1161).
- ARM64 Flatpak packages now include the shader runtime (#1168).

## Cheats and library

- Select labeled cheat values from a dropdown, edit numeric values, and choose
  from small explicit ranges. Apply once writes the value without leaving a
  per-frame freeze enabled (#1186).
- RetroAchievements hardcore restrictions still apply. Queued cheat commands
  use an atomic permission flag to avoid a race with the UI policy (#1190).
- Browse and launch games from an Emulation Server in the library (#1188).
- Correct the Mac App Store library build configuration for persistent
  bookmarks, and improve font-worker validation (#1176, #1175).

## Packaging and verification

Cloud tests exercise windowed OpenGL startup and bundled shader presets on
Windows x64/ARM64, Linux and macOS. Release packaging also checks the Windows
portable ZIP and installed application. Download verification uses build
provenance and the per-artifact verification table attached to the release.

## Compatibility and limitations

Core options and v17 savestates carry over from 3.11.0. RISC pipeline measurement
tests were added without changing the timing model (#1158).

macOS OpenGL stops at 4.1: presets requiring unsupported features are refused.
Complex CRT presets may be too slow on software rendering; the default filter
is deliberately lightweight. On Windows ARM64, use the ARM64 package; native
ARM64 testing does not establish compatibility of every x64 graphics component
under Windows emulation.

Store availability is separate from this GitHub release. No commercial games
are included; supply your own game files.
