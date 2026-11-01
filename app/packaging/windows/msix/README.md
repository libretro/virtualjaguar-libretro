# Microsoft Store MSIX

Store ID: `9PP6567FHBMS`. Exact assigned identity is in `identity.json`; Partner
Center currently spells the publisher **Joseph Mattello**, and the package name
**JosephMattello.VirtualJaguar**. These are public manifest values, not credentials.
Do not correct that spelling in a manifest without first reconciling Partner Center.

This packages the existing Win32 app as a full-trust desktop MSIX, Windows 10
2004+ / Windows 11, x64 or ARM64. It requires `runFullTrust` because the app uses
ordinary desktop file dialogs, OpenGL, controllers and optional network features.
The capability needs an explanation in Partner Center. It does not add DRM or
change emulation. The app retains Windows' assigned shell identity when packaged.

## Build

Use PowerShell **7+** and a Windows SDK containing MakeAppx. Build the app using
its existing Windows toolchain, then install it into a fresh prefix:

```powershell
cmake --install build --prefix stage-store
./app/packaging/windows/msix/package.ps1 -InstallPrefix stage-store `
  -Version 3.10.0 -Architecture x64 -OutputDirectory store-out
```

The script validates the input PE machine and version (`major.minor.patch.0`,
major > 0, components <= 65535, final component zero), places the contents of `bin/` at the
package root and preserves all installed documentation, renders `AppxManifest.xml`, packs
with SDK validation enabled, unpacks and checks every staged file's SHA256.
The output includes an **unsigned** `.msix` and checksum/identity summary JSON.
A fresh output folder avoids silently replacing packages from another source.
`-MakeAppx C:\path\to\makeappx.exe` overrides SDK discovery.

`-StageOnly` works on macOS/Linux with PowerShell 7 and produces the inspectable
layout without pretending to produce a valid MSIX. Test staging with:

```powershell
./app/packaging/windows/msix/test-package.ps1
```

`portable.txt` is rejected: this package uses the app's per-user data path rather
than trying to write into read-only WindowsApps. Input/output trees must be
separate. Symlink/reparse-point payloads are rejected.

The separate **Windows Store MSIX** workflow builds both architectures from the
selected source ref and uploads `store-msix-windows-*` artifacts. Its run is
separate from `release.yml`, whose artifact collector publishes everything in
its own run. No workflow uploads to Partner Center, signs with production keys,
changes prices or publishes. Numeric versions are the same for PR/manual builds;
only upload a qualified release candidate, not every artifact.

The x64 qualification job installs a test-signed copy on a disposable runner and
runs the installed app's public-ROM hash check. It removes the test installation
and certificates and retains diagnostic logs. This does not qualify GUI graphics,
controller/audio behaviour or upgrade retention; those need an interactive Windows
machine with a supported OpenGL driver. A workflow dispatch can reuse an earlier
packaging run's artifacts via `candidate_run` without rebuilding the app.

## Qualification before submission

- Upload the qualified x64 and ARM64 packages to the existing **draft** product.
  Microsoft signs Store distribution; unsigned files are not end-user installers.
- For local testing, sign a COPY with a test certificate matching the publisher
  and trust it on an isolated Windows test machine; never upload that private key
  or turn off signature enforcement. Do not distribute test-signed copies.
- Test install/Start-menu launch, pinned taskbar identity, controller/audio/video,
  import paths containing spaces/non-ASCII, external folders and multi-file CDs.
- Test normal saves/config storage, upgrade retention, coexistence with website
  editions and uninstall. MSIX filesystem virtualization may isolate data from
  the unpackaged app; automatic migration is **not** claimed.
- This initial manifest does not declare Explorer file associations or execution
  aliases. Import through File > Open, library folders or drag/drop; qualify shell
  activation separately before adding associations.
- Store screenshots, age ratings, privacy declarations and capability rationale
  must match the exact package. `listing-en-us.md` is draft copy, not certification.

The Store version's last component is reserved for Microsoft. Any future release
must increase the first three components; do not publish nightly/PR suffixes or
upload multiple different builds under an already published version.

References: [manual packaging](https://learn.microsoft.com/en-us/windows/msix/desktop/desktop-to-uwp-manual-conversion),
[version rules](https://learn.microsoft.com/en-us/windows/apps/publish/publish-your-app/package-version-numbering),
[signing](https://learn.microsoft.com/en-us/windows/msix/package/signing-package-overview).

Windows packages include a `software-opengl/` app copy and Mesa dependency notices.
Startup tries the normal driver first. If window/context/renderer initialization
fails, it releases the first instance and retries once with the bundled Mesa,
preserving command-line arguments and the original data root. Mesa renders on
the machine's hardware Direct3D 12 adapter when there is one (`d3d12`) and on
the CPU otherwise (`llvmpipe`; the Basic Render Driver does not count as
hardware). Only the child's environment selects Mesa; system drivers and
settings are unchanged.
Qualify both the normal and fallback launch paths for each Store architecture.
