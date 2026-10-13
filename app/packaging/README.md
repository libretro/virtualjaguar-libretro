# Packaging the Virtual Jaguar app (#839)

CI builds every installable package from one reusable workflow,
[`.github/workflows/app-package.yml`](../../.github/workflows/app-package.yml). It never publishes:

| Caller | When | Result |
|---|---|---|
| `release.yml` | tag `v*`, push to `develop`, push to `release/**` | artifacts are attached next to the core assets (release / rolling `nightly` / rolling `prerelease`) and listed in `SHA256SUMS.txt` |
| `app.yml` | pull requests | unsigned artifacts plus one sticky PR comment linking them |
| manual dispatch | on demand | artifacts only; the `sign` input exercises the signing path |

## Assets

`X.Y.Z` is the tag without `v`; nightlies use `nightly`, PRs `prNNN`, the `release/**` lane `prerelease`.

| File | Notes |
|---|---|
| `VirtualJaguar-X.Y.Z-macos-universal.zip` | `Virtual Jaguar.app` (the zip's only top-level entry), arm64 + x86_64, macOS 11+; also the Homebrew cask's download. There is no DMG. |
| `VirtualJaguar-X.Y.Z-windows-x64.zip` | portable folder; `portable.txt` keeps settings in `.\data` |
| `VirtualJaguar-X.Y.Z-windows-x64-setup.exe` | Inno Setup installer, per-user by default (no admin prompt) |
| `VirtualJaguar-X.Y.Z-linux-x86_64.AppImage`, `...-linux-aarch64.AppImage` | built on Ubuntu 22.04 (glibc 2.35) |
| `virtualjaguar_X.Y.Z_amd64.deb`, `virtualjaguar_X.Y.Z_arm64.deb` | installs to `/usr/lib/virtualjaguar`, symlink in `/usr/bin` |

The `.deb`'s internal `Version:` is Debian-legal (`4.0.0`, `4.0.0~rc1`, `4.0.0~nightly.<sha>`); the
file name keeps the contract name.

### The version guard

The app reports `project(VERSION ...)` from `app/CMakeLists.txt`. A tag whose version (minus any
`-suffix`) differs from it builds **no** app packages and logs a warning, so a core-only release never
ships an app that says another number. To ship packages with a release, bump the app version in the
release commit. A commit without `app/` is skipped the same way.

## The install tree

`cmake --install build --prefix P` produces `P/bin/virtualjaguar[.exe]`, **everything the build staged
beside the executable** (`cheats/`, `libretro-db/`, `shaders/`, `librashader.*`) in `P/bin/`,
and licences/notices in `P/share/doc/virtualjaguar/`; Linux adds desktop, appstream and icon files.
The sweep is in [`app/cmake/VJPackage.cmake`](../cmake/VJPackage.cmake): a feature that bundles
another directory adds its name to `VJ_PACKAGE_DATA_DIRS` once. Each platform script then relocates
`bin/`: `Contents/Resources` (+ `Contents/Frameworks` for dylibs) in the `.app`, `/usr/lib/virtualjaguar`
in the `.deb`, `usr/bin` in the AppImage. CI checks that each package holds every data directory the
build tree had (`check-payload.sh`).

## Secrets the maintainer must add

Nothing is signed until these exist; without them forks, PRs and the first runs still build, with a
warning in the log. Repository secrets (Settings > Secrets and variables > Actions):

macOS (Developer ID + notarization, existing Apple Developer account):

| Secret | What |
|---|---|
| `APPLE_DEVELOPER_ID_P12_BASE64` | `base64 -i DeveloperID.p12` of the **Developer ID Application** certificate with its private key |
| `APPLE_DEVELOPER_ID_P12_PASSWORD` | the password the `.p12` was exported with |
| `APPLE_NOTARY_KEY_ID` | App Store Connect API key id (Users and Access > Integrations > Keys) |
| `APPLE_NOTARY_ISSUER_ID` | the issuer UUID shown on that page |
| `APPLE_NOTARY_KEY_P8_BASE64` | `base64 -i AuthKey_XXXX.p8` |

Windows (SignPath Foundation, **dormant until the OSS application is approved**):

| Name | Kind | What |
|---|---|---|
| `SIGNPATH_API_TOKEN` | secret | submitter API token |
| `SIGNPATH_ORGANIZATION_ID` | variable | organization id |
| `SIGNPATH_PROJECT_SLUG`, `SIGNPATH_SIGNING_POLICY_SLUG` | variables, optional | default `virtualjaguar` / `release-signing` |

The SignPath steps are written to the action's documented interface but have **never run**; expect to
adjust them against the artifact configuration SignPath gives you (they sign `virtualjaguar.exe` before
packaging and the installer after).

Signing runs only when the caller asks (`release.yml` does, PRs do not) and the secrets exist. PR builds
are never notarized: a notarization round trip per push is slow and the PRs come from branches that
should not get certificate access by default.

## macOS notes

* **Layout.** `SDL_GetBasePath()` in a `.app` is `Contents/Resources/` (SDL's default `resource`
  base-dir type), so cheats, the dat files and shader presets live there; `librashader.dylib`
  goes in `Contents/Frameworks/`, one of the places the shader code probes. The dev target is
  deliberately not a `MACOSX_BUNDLE`: its POST_BUILD copies would land in `Contents/MacOS`.
* **Universal.** Two single-architecture builds merged with `lipo` (`make-universal.sh`), because
  `VJShaders.cmake` fetches the prebuilt librashader for one architecture per configure. A gate
  (`check-universal.sh`) fails the build if any Mach-O lacks a slice, and checks every binary's
  `minos` against the deployment target (11.0).
  **Known gap:** the `x86_64-macos` archive of librashader v0.12.0 contains an arm64 dylib (verified with
  `lipo`). `librashader.dylib` therefore ships arm64-only, with a warning; on an Intel Mac the app
  reports the shader runtime as unavailable and runs without shaders. Fix: build librashader for
  `x86_64-apple-darwin` in CI (Rust) and merge.
* **Hardened runtime and entitlements.** Signed with `--options runtime`. Library validation (on with
  the hardened runtime) refuses any dylib not signed by Apple or by our team. The bundled
  `librashader.dylib` is re-signed with our identity, so it needs no exception. The shader code also
  probes `/opt/homebrew/lib` and `/usr/local/lib` for a user's own librashader; that one is signed
  by someone else (or ad hoc), and was verified to be refused without an exception and accepted with
  `com.apple.security.cs.disable-library-validation`. That is the only entitlement in
  [`entitlements.plist`](macos/entitlements.plist); no JIT, camera, microphone or sandbox is involved.
  Delete the entitlement to forbid user-supplied shader runtimes.
* **Order.** sign the dylib then the app, notarize the app and staple the `.app`, then zip it with
  `ditto -c -k --sequesterRsrc --keepParent` (the stapled ticket and signature travel inside the app). CI
  unpacks the finished zip and re-verifies the signature (and, when signed, the staple and Gatekeeper's
  verdict) on the unpacked app. The zip itself is never notarized separately; Apple's ticket is on the app.
* **Unsigned builds** are ad-hoc signed (`codesign -s -`), which Apple Silicon requires; Gatekeeper
  still refuses them until right-click > Open or `xattr -dr com.apple.quarantine`.

## Windows notes

* MinGW-w64 (the core's toolchain), statically linked C/C++ runtime; CI fails if the exe imports
  anything but Windows system DLLs. GUI subsystem (no console window); `console_attach.cpp` re-attaches to
  the parent console so `--help`/`--version` still print.
* Installer: **Inno Setup 6.7.3** (pinned by SHA-256). Chosen over NSIS for a maintained modern wizard,
  per-user install without a UAC prompt by default (`PrivilegesRequiredOverridesAllowed=dialog`), clean
  uninstall registration, and winget's `inno` installer type with known silent switches. The installed copy is not
  portable (no `portable.txt`; Program Files is not writable); data lives in `%APPDATA%\VirtualJaguar`.
* CI installs the setup silently, runs the installed exe, uninstalls, and checks it is gone.

## Linux notes

* AppImage: linuxdeploy `1-alpha-20251107-1` + appimagetool `1.9.1`, both pinned by SHA-256, run with
  `APPIMAGE_EXTRACT_AND_RUN=1` (no FUSE needed). SDL3 is static and `dlopen`s the host's display, audio and GL.
* `.deb`: built by `make-deb.sh` (not CPack) so the executable and its data stay together under
  `/usr/lib/virtualjaguar` with a `/usr/bin` symlink (`SDL_GetBasePath()` resolves `/proc/self/exe`).
  `Depends` comes from `dpkg-shlibdeps`; the display and audio stacks SDL loads at run time are `Recommends`.
* Built on Ubuntu 22.04 runners, the oldest available, for the lowest glibc floor.

## Package managers (templates; nothing is submitted by CI)

Templates live in [`manifests/`](manifests/): Homebrew cask (macOS zip) and formula (source build),
winget (3-file manifest for the Inno installer), Scoop (portable zip), Flatpak (offline-source manifest).
CI renders and parses them on every run with placeholder checksums. After a release is published:

```sh
curl -LO https://github.com/libretro/virtualjaguar-libretro/releases/download/vX.Y.Z/SHA256SUMS.txt
bash app/packaging/manifests/render.sh --version X.Y.Z --sums SHA256SUMS.txt \
     --fetch-source --commit "$(git rev-parse vX.Y.Z)" --out /tmp/vj-manifests
```

then, by hand (these are outward actions that stay with the maintainer):

* **Homebrew cask** (`homebrew/virtualjaguar.rb`): PR to your tap or `Homebrew/homebrew-cask`
  (`brew audit --new --cask`, `brew install --cask ./virtualjaguar.rb`). Homebrew requires casks to be
  signed and notarized, so wait for a notarized release.
* **Homebrew formula** (`virtualjaguar-formula.rb`): tap only; its configure step downloads data
  (not allowed in homebrew-core).
* **winget**: `wingetcreate` or a PR to `microsoft/winget-pkgs` with the three `winget/*.yaml` files
  (`winget validate`). Unsigned installers trigger SmartScreen; reviewers may ask for signing.
* **Scoop**: add `scoop/virtualjaguar.json` to a bucket (`scoop install ./virtualjaguar.json`).
* **Flatpak / Flathub**: new-app PR to `flathub/flathub` with `flatpak/io.github.libretro.VirtualJaguar.yml`.
  The `io.github.libretro.*` app id assumes the libretro GitHub organization owns the app; change it everywhere
  (`app/cmake/VJPackage.cmake`, `app/packaging/linux/*`, `macos/Info.plist.in`) if a different id is wanted.
  The manifest builds offline and has **not been built yet**; run `flatpak-builder` once before submitting.

## Placeholder icon

`icons/virtualjaguar.svg` is a placeholder (original artwork, no Atari marks). Replace it and run
`icons/make-icons.sh` (needs `rsvg-convert`, ImageMagick, macOS `iconutil`) to regenerate the committed
PNG, `.ico` and `.icns` files.

## Clean-machine checks CI cannot do

Gatekeeper first-run on a freshly downloaded signed zip, SmartScreen on the installer, Intel Mac behavior
(CI may not run the x86_64 slice), Wayland/X11/PipeWire sound on real Linux desktops, and the AppImage on
distributions older than Ubuntu 22.04.
