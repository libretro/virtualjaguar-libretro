# Virtual Jaguar v3.13.0

The app patches cartridges with IPS files, browses RomM servers and server
add-ons, and gains a debugger loop for writing your own Jaguar code. Windows
downloads are Authenticode-signed for the first time.

## Library and patches

- Open or drop an `.ips` patch. Choose the base cartridge from the library, the
  running game or a file; the dialog shows the base and patched SHA-256 values
  and can check them against expected ones. Boot the patched cartridge from
  memory, save it as a separate copy, or replace the original after a
  byte-for-byte recovery copy has been written. Saves and states of a patched
  game are kept apart from the original's. Copies can be saved to FAT32 and
  exFAT drives too (#1208).
- Server libraries are named tabs, each with its own address, sign-in, search
  and artwork. A RomM 5.4 server can be browsed directly, alongside Emulation
  Server (#1209).
- Selecting a server game lists its add-ons. Downloads are checked against the
  server's size and SHA-256 when it gives them. A cartridge IPS add-on can be
  launched straight away; other formats are download-only (#1216).

## Debugger

For homebrew, the Tools menu now covers the edit, build and debug loop
(`docs/app-debug-tools.md`, "Homebrew workflow") (#1218):

- **Break at start** halts every load and every Reset at the 68000's first
  instruction, so breakpoints can be set before anything runs.
- Reloading the program you are working on keeps its breakpoints, and each one
  follows the label it was set on to its address in the new build. The CPU
  debugger, memory viewer and Symbols windows stay open.
- **Reload when the file changes** loads a rebuild by itself.
- One **memory watchpoint**: a byte range, reads and/or writes. It sees 68000
  accesses only; a write by the GPU, DSP or blitter does not trigger it.
- With symbols loaded, the CPU debugger labels its listing, names labelled
  operands, takes a symbol in Go to, and the halt bar says where it stopped.
- From a build script:
  `virtualjaguar --break-at-boot --reload-on-change --break main --watch score:2:w game.cof`

The GDB stub's watchpoints (`Z2`/`Z3`/`Z4`) now honour the requested length and
trigger on any overlapping 68000 access. Before, only an access at exactly the
watched address did.

## Signing and verification

- Windows app packages, installers, core DLLs and tools are signed with Azure
  Artifact Signing, and the release gate verifies each one against Microsoft's
  root with a verified timestamp (#1150, #1200). The v3.12.0 Windows downloads
  remain unsigned.
- `SHA256SUMS.txt` is minisign-signed and AppImages carry an embedded GPG
  signature. The public keys are in `docs/` and the minisign key is pinned in
  both install scripts (#1199).
- A stable release is published only after the exact staged Windows installers
  have been installed, booted and uninstalled on native x64, native ARM64 and
  x64 under ARM emulation (#1210).
- The per-artifact verification table attached to the release is the
  authority for what each file's signature state is.

The stable macOS app is also available from the [project Homebrew tap](https://github.com/Provenance-Emu/homebrew-virtualjaguar) (#1205).

## Compatibility and limitations

The 155-title cartridge and 28-case CD boot sweeps have no backward moves
from 3.12.0, and no row changed stage.

Core options and v17 savestates carry over from 3.12.0. The emulated machine is
unchanged in this release: the core changes are the debugger's watchpoint and
the cartridge loader entry the IPS path uses.

Sparkle (macOS) and WinSparkle (Windows) self-update support and a verified
feed publisher were merged as opt-in builds (#1213, #1214, #1215). The
downloads of this release do not include an updater.

The new debugger windows and the reload-on-change path were tested headlessly
and against a mock core; report anything that looks wrong in the window.

Store availability is separate from this GitHub release. No commercial games
are included; supply your own game files.
