# Packaging the Virtual Jaguar app (#839)

CI builds every installable package from one reusable workflow,
[`.github/workflows/app-package.yml`](../../.github/workflows/app-package.yml). It never publishes:

| Caller | When | Result |
|---|---|---|
| `release.yml` | tag `v*`, push to `develop`, push to `release/**` | artifacts are attached next to the core assets (release / rolling `nightly` / rolling `prerelease`) and listed in `SHA256SUMS.txt` |
| `app.yml` | pull requests | artifacts plus one sticky PR comment linking them; signed and notarized like a release when the secrets exist (same-repo PRs), ad-hoc signed for fork PRs |
| manual dispatch | on demand | artifacts only; the `sign` input exercises the signing path |

## Microsoft Store (MSIX)

The separate Windows Store MSIX workflow prepares unsigned x64/ARM64 submission
packages for the reserved product, outside the website release artifact run.
See [`windows/msix/README.md`](windows/msix/README.md) for identity, build commands,
validation and remaining Windows install/upgrade qualification. Website ZIP/EXE
packages retain their existing delivery path.

## Assets

`X.Y.Z` is the tag without `v`; nightlies use `nightly`, PRs `prNNN`, the `release/**` lane `prerelease`.

Same-repository `release/**` PRs share the Release push workflow's package builds when the PR's
synthetic merge tree is identical to its head tree. Ordinary PRs, forks, manual app builds, and
release PRs whose merge adds different content keep their own package builds. Native app test
matrices and ThreadSanitizer still run on the PR; Release still builds and tests every package.
The PR's **Verify shared Release packages** check requires all twelve package/runtime jobs and
all six nonempty, unexpired `app-package-*` artifact groups from the latest Release push run for
that exact head, branch and repository. Pending, failed, cancelled, skipped or missing package
checks cannot pass the shared gate. A changed PR head or merge tree also fails it.

Shared packages retain their `prerelease` filenames, artifact names and 14-day retention in the
Release run. The sticky PR comment links directly to those artifacts and reports the verifier's
result; it does not rename, rebuild or copy the archives. Tag/nightly publication and checksum
collection still consume the Release workflow's artifacts as before. The verification job uses
a hosted runner, not an allowlisted temporary runner that Release metadata jobs may need, so
waiting for those jobs cannot deadlock that runner. It times out red after 345 minutes rather
than bypassing unfinished work. If it fails, fix or rerun the failing Release package job, then
rerun the PR's shared-package verifier; no duplicate package dispatch is needed.

| File | Notes |
|---|---|
| `VirtualJaguar-X.Y.Z-macos-universal.zip` | `Virtual Jaguar.app` (the zip's only top-level entry), arm64 + x86_64, macOS 11+; also the Homebrew cask's download. There is no DMG. |
| `VirtualJaguar-X.Y.Z-windows-x64.zip` | portable folder; `portable.txt` keeps settings in `.\data` |
| `VirtualJaguar-X.Y.Z-windows-x64-setup.exe` | Inno Setup installer, per-user by default (no admin prompt) |
| `VirtualJaguar-X.Y.Z-windows-arm64.zip`, `...-windows-arm64-setup.exe` | the same two packages, native ARM64 (clang); the arm64 installer refuses to run on x64 Windows |
| `VirtualJaguar-X.Y.Z-linux-x86_64.AppImage`, `...-linux-aarch64.AppImage` | built on Ubuntu 22.04 (glibc 2.35) |
| `virtualjaguar_X.Y.Z_amd64.deb`, `virtualjaguar_X.Y.Z_arm64.deb` | installs to `/usr/lib/virtualjaguar`, symlink in `/usr/bin` |
| `VirtualJaguar-X.Y.Z-linux-x86_64.flatpak` | single-file Flatpak bundle, app id `io.github.libretro.VirtualJaguar`; see Flatpak notes |

The `.deb`'s internal `Version:` is Debian-legal (`3.9.1`, `3.9.1~rc1`, `3.9.1~nightly.<sha>`; the version is the core's); the
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

Nothing is Developer ID signed until these exist; without them forks, PRs and the first runs still build
(macOS ad-hoc signed), with a warning in the log. Repository secrets (Settings > Secrets and variables > Actions):

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

Signing runs when the caller asks (`release.yml` and `app.yml` do; a manual dispatch needs the `sign`
input) and the secrets exist. GitHub passes no secrets to a fork pull request, so those builds stay
ad-hoc signed and the log warns.

### Every macOS build is signed (#989)

All the macOS files people download are signed with the Developer ID Application certificate (hardened
runtime, secure timestamp) and notarized, by one composite action,
[`.github/actions/macos-sign`](../../.github/actions/macos-sign/action.yml), which wraps
[`macos/codesign.sh`](macos/codesign.sh). It imports the certificate into a temporary keychain that is
always deleted again, signs, submits one zip per call to `notarytool` (API key auth), and staples bundles.

| What | Where | Stapled |
|---|---|---|
| `Virtual Jaguar.app` in the macOS zip | `app-package.yml` (PRs, nightlies, tags, dispatch with `sign`) | yes |
| `virtualjaguar` binary, artifact `virtualjaguar-macOS arm64` | `app.yml` | no |
| `virtualjaguar_libretro-macos-{arm64,x86_64}.dylib` | `release.yml` (tags, nightly, prerelease) | no |
| `cue2cdi-macos-universal` | `release.yml` | no |
| `chdman`, `jagcd-chd-check` in `jagcd-tools-macos.zip` | `chdman-tools.yml` | no |

A bare Mach-O file cannot hold a stapled ticket: it is notarized inside a zip and Gatekeeper looks the
ticket up online the first time. iOS and tvOS dylibs are not signed here (a Developer ID does not apply
to them; Provenance signs them when it embeds them). A notarization failure (an Apple outage, a timeout)
fails the step on a tag and only warns anywhere else, so PR CI keeps the signed-but-unnotarized build.
Without the secrets the action prints the warning and does nothing.

The secrets reach a same-repo pull request's workflow run, so anyone with write access to the repository
can have CI sign a file. Keep that in mind when granting write access.

## Signing locally

`local-package.sh` packages a normal build on your own machine and signs it with a certificate it
finds there; CI is unaffected. Build first, then:

```sh
cmake -S app -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j4
bash app/packaging/local-package.sh --sign              # dist/Virtual Jaguar.app (+ a .zip)
bash app/packaging/local-package.sh --notarize          # also submit to Apple, staple, spctl
```

Without `--sign` it only assembles (macOS ad-hoc signs, as Apple Silicon requires). The local build is
host-architecture only; the universal build is CI's.

**Settings** come from the environment, then from a repo-root `.env` (gitignored; copy
[`env.example`](env.example), which lists every key). Only `VJ_*` and `APPLE_*` keys are read, values
are literal (nothing is evaluated), and a variable already set in your shell wins. A value written as
`op://vault/item/field` is resolved with the 1Password CLI when `op` is installed, so a secret need not
sit in the file (for `APPLE_NOTARY_KEY_PATH` and `VJ_SIGN_PFX` the item is written to a private temp file for
the run). `VJ_ENV_FILE` points at another file. Values are never printed, only key names.

macOS identity, first match wins:

1. `VJ_SIGN_IDENTITY`: a name, a SHA-1 hash, or `-` for ad hoc
2. the newest-expiring valid **Developer ID Application** identity in your keychain (chosen and passed
   by SHA-1, so renewed duplicates with the same name are not ambiguous)
3. **Apple Development** (runs on this Mac; cannot be notarized, and a warning says so)
4. ad hoc

It prints the identity name and expiry it picked. List yours with
`security find-identity -v -p codesigning`. The first signature may raise a keychain prompt for the
private key: click Always Allow.

Notarization (`--notarize`, needs a Developer ID identity), first match wins:

1. `VJ_NOTARY_PROFILE`: a keychain profile. Recommended, since no secret leaves the keychain. Create it once:
   `xcrun notarytool store-credentials vj-notary --key AuthKey_XXXX.p8 --key-id KEYID --issuer ISSUER-UUID`
2. `APPLE_NOTARY_KEY_ID` + `APPLE_NOTARY_ISSUER_ID` + `APPLE_NOTARY_KEY_PATH` (the `.p8` on disk)

Notarizing uploads the app to Apple and waits for the verdict, then staples the ticket and runs
`spctl -a -t exec -vv`. Hardened runtime and the entitlements are the same as CI's.

Windows (Git Bash / MSYS2; builds the portable zip, plus the installer when Inno Setup's `ISCC.exe` is
found), certificate, first match wins:

1. `VJ_SIGN_PFX` (+ `VJ_SIGN_PFX_PASSWORD`): a `.pfx` file
2. `VJ_SIGN_CERT_SHA1`: the thumbprint of a certificate in the CurrentUser\My store
3. `signtool sign /a`, which picks an installed code-signing certificate; none usable means unsigned,
   with a message

`signtool.exe` is found on `PATH` or in the newest Windows SDK under
`C:\Program Files (x86)\Windows Kits\10\bin\*\x64`; `VJ_SIGNTOOL` overrides. It signs `virtualjaguar.exe`,
the DLLs, and the installer, timestamped with `/tr` `VJ_SIGN_TIMESTAMP_URL` (default
`http://timestamp.digicert.com`, SHA-256). This path has not been run on Windows yet. Note `signtool /p` shows
the password in the process list; prefer the store thumbprint on a shared machine.

**Dev executable.** `-DVJ_APP_SIGN_DEV_BUILD` (macOS; ON by default outside CI, `=OFF` to skip) signs `build/virtualjaguar` after
every build with the identity chosen above, so macOS stops re-prompting for network and keychain access
after each rebuild of an unsigned binary. It does not use hardened runtime and never fails the build.

## macOS notes

* **Layout.** `SDL_GetBasePath()` in a `.app` is `Contents/Resources/` (SDL's default `resource`
  base-dir type), so cheats, the dat files and shader presets live there; `librashader.dylib`
  goes in `Contents/Frameworks/`, one of the places the shader code probes. The dev target is
  deliberately not a `MACOSX_BUNDLE`: its POST_BUILD copies would land in `Contents/MacOS`.
* **Universal.** Two single-architecture builds merged with `lipo` (`make-universal.sh`), because
  each configure is handed one single-architecture librashader (`-DVJ_LIBRASHADER_LIB`). A gate
  (`check-universal.sh`) fails the build if any Mach-O, `librashader.dylib` included, lacks a slice, and checks every
  binary's `minos` against the deployment target (11.0). Both dylibs are built from source in CI from the same pinned
  tag, see [Shader runtime](#shader-runtime-librashader).
* **OpenGL 4.1.** macOS stops at OpenGL 4.1 and has no `GL_ARB_arrays_of_arrays`. librashader 0.12 never
  sets SPIRV-Cross's `flatten_multidimensional_arrays`, so a shader with a 2-D array (crt-geom's
  `vec3 slotmask[3][4]`) compiles to GLSL that requires that extension and cannot build there. 15 bundled
  presets do this: crt-geom, -deluxe, -tate; crt-hyllian, -ntsc, -ntsc-rainbow, -sinc-composite; crt-nobody,
  crt-super-xbr, vt220, crt-fast-bilateral-super-xbr, img_mod, test-pattern-append/-prepend and
  res-independent-scanlines. Where the context has neither GL 4.3 nor the extension, the preset list greys
  them out ("needs OpenGL 4.3"), and a preset chosen anyway (config.ini, file dialog) gets that reason in
  the on-screen message instead of the GL compiler's text. The list is `presetsNeedingArraysOfArrays()`
  (`present_util.cpp`), pinned to the bundled slang-shaders commit; `shaders_bundled` and `shaders_gl`
  check it. Real fix: librashader setting `flatten_multidimensional_arrays` when it generates GLSL below
  4.30.
  The chain asks librashader for the context's own GLSL version (4.10 here). It used to ask for 3.30, which
  cost 11 presets GLSL 4.00 features the context has (lottesRVM, nnedi3 x5, mmpx-ADV, vectorscale x4).
  Rendering all 498 bundled presets on an M4 Max (GL 4.1 Metal) at 4.10, 473 work. Besides the 15 above,
  fsr and smaa+fsr need `packHalf2x16` (GLSL 4.20), and 8 fail for reasons that do not look macOS-specific
  (not checked elsewhere): the six nedi bilateral-variant presets (unknown semantic `play`),
  crt-black_crush-koko (preset parse error) and ntsc-blastem (varying type mismatch at link).
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
* **Builds without the secrets** (fork pull requests, local builds) are ad-hoc signed (`codesign -s -`),
  which Apple Silicon requires; Gatekeeper still refuses them until right-click > Open or
  `xattr -dr com.apple.quarantine`.

## Shader runtime (librashader)

The app loads [librashader](https://github.com/SnowflakePowered/librashader) with `dlopen` (#834); every
package ships it beside the executable (`Contents/Frameworks/librashader.dylib`, `usr/bin/librashader.so` in the
AppImage, `/usr/lib/virtualjaguar/librashader.so` in the `.deb`, `librashader.dll` next to the exe), and
`THIRD-PARTY-NOTICES.txt` carries its licence (MPL-2.0 OR GPL-3.0-only).

| Package | Where the library comes from |
|---|---|
| macOS universal (arm64 + x86_64) | **built from source in CI**, both slices, then `lipo`-merged |
| AppImage / `.deb`, x86_64 and aarch64 | **built from source in CI** on the packaging runner (Ubuntu 22.04) |
| Windows x64 / ARM64 | upstream's prebuilt `librashader-v0.12.0` zip (SHA-256-pinned in `app/cmake/VJShaders.cmake`) |
| Flatpak | see Flatpak notes |

Why source: upstream's `x86_64-macos` v0.12.0 zip holds an **arm64** dylib (`lipo`-verified), so Intel Macs had no
shader runtime, and upstream publishes no Linux binary. Building both macOS slices ourselves (instead of mixing
upstream's arm64 with a self-built x86_64) keeps one version, one deployment target and one install name in the
universal file.

* **The recipe** is [`.github/workflows/librashader.yml`](../../.github/workflows/librashader.yml), called by
  `app-package.yml` (job `librashader`, which the macOS and Linux jobs `need`). The pin is
  [`librashader/pin.env`](librashader/pin.env): tag `librashader-v0.12.1`, its commit and the SHA-256 of the GitHub
  archive for that commit, a pinned stable Rust, and the macOS deployment target. The build is
  [`librashader/build-from-source.sh`](librashader/build-from-source.sh): fetch, verify the digest, then
  `cargo xtask build --profile optimized --target <triple> -- --locked`. `VJShaders.cmake` reads the same tag for
  the C headers, whose digests are pinned there (the 0.12.1 headers are byte-identical to 0.12.0's, ABI 2).
* **Targets:** `x86_64-apple-darwin` (cross-built on the Apple-silicon runner), `aarch64-apple-darwin`,
  `x86_64-unknown-linux-gnu` and `aarch64-unknown-linux-gnu` (native, `ubuntu-22.04` / `ubuntu-22.04-arm`,
  glibc 2.35 like the packages). macOS uses `MACOSX_DEPLOYMENT_TARGET=11.0` and the install name
  `@rpath/librashader.dylib` (cargo's default would bake the runner's absolute path in).
* **Cache and artifacts.** Each target is cached under `librashader-<commit>-<triple>-rust<ver>-mac<min>-v1`
  (a cold build is ~10 minutes with LTO, a hit is seconds; bump `-v1` in the workflow to force a rebuild after a
  recipe change) and uploaded as the artifact `librashader-<triple>` (library + `BUILD-INFO.txt`). These are
  intermediates: `release.yml` leaves `librashader-*` out of the release assets.
* **Checks.** In `librashader.yml`: the architecture (`lipo -archs` / ELF machine), `minos`, install name, the
  highest `GLIBC` symbol version (must be <= 2.35), only C/C++-runtime `NEEDED` entries, and (same-architecture
  runners) a load that reads the ABI version. In `app-package.yml`: `vj_shader_tests library` with
  `VJ_REQUIRE_LIBRASHADER=1` (a missing runtime fails instead of skipping); the macOS bundle's universal dylib
  is loaded by an arm64 process and, under Rosetta, an x86_64 one; for the AppImage and the `.deb`,
  `linux/check-librashader.sh` checks the file, its CPU and dependencies, and runs `vj_shader_tests probe` from the
  package's own folder, i.e. the app's real lookup (beside the executable, `VJ_LIBRASHADER` unset).
* **Bumping:** change `LIBRASHADER_TAG`, `LIBRASHADER_COMMIT` and `LIBRASHADER_SHA256` together (and re-pin the header
  digests in `VJShaders.cmake` if `include/` changed; its comment says how), update `THIRD-PARTY-NOTICES.txt`.
* **Without the bundled file** the Shader section of the app says why (not bundled / failed to load: reason) and
  links to the "Getting the shader runtime" section of [`app/README.md`](../README.md#getting-the-shader-runtime).

## Windows notes

* MinGW-w64 (the core's toolchain), statically linked C/C++ runtime; CI fails if the exe imports
  anything but Windows system DLLs.
* **ARM64.** Built natively on GitHub's `windows-11-arm` runner in the MSYS2 `CLANGARM64` environment
  (clang, libc++, lld), the ARM64 flavour of the same MinGW-w64 setup; MSVC was not used because the
  MSVC build of the core has never passed. The blitter's SIMD choice is autodetected, so ARM64 gets the
  NEON implementation; CI checks that exactly one `blitter_simd_ops` is linked, and that the exe is an
  ARM64 image. The same `package.sh` and `virtualjaguar.iss` serve both architectures (`--arch arm64`,
  `/DArm64`): the arm64 installer is `ArchitecturesAllowed=arm64`, the x64 one `x64compatible` (so the
  x64 package also installs, emulated, on ARM64 Windows). librashader's `aarch64-windows` runtime is bundled.
  `tools/install.ps1` picks the ARM64 package on ARM64 Windows and falls back to x64 for older releases. GUI subsystem (no console window); `console_attach.cpp` re-attaches to
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
* `librashader.so` is built from source on the same runners (see Shader runtime above) and installed beside the
  executable by `cmake --install`; it is not a linked dependency (no rpath needed, the app `dlopen`s it by
  absolute path from `SDL_GetBasePath()`), so linuxdeploy and `dpkg-shlibdeps` leave it alone.

## Build channel

`-DVJ_BUILD_CHANNEL=` (`release` | `nightly` | `dev`, default `dev`) decides what Settings > Interface > Emulation thread
starts as for a user who never chose (a stored choice always wins): `nightly` on, `release` and `dev` off. `dev` is everything
else: pull-request artifacts, `prerelease`, local builds. `app-package.yml` derives `CHANNEL` from `release_tag` (`nightly` ->
nightly, `v*` -> release, anything else -> dev) in its top-level `env:` beside `TV_DEFAULT`, and passes it to the macOS, Windows
and Linux configures. The Flatpak manifest commits `-DVJ_BUILD_CHANNEL=release` (so the Flathub render is a release build);
the flatpak job rewrites that line to `$CHANNEL` and fails if the rewrite matched nothing. About shows the channel and the start-up
log prints `[emu] channel=...`.

## Flatpak notes

* **Manifest:** [`flatpak/io.github.libretro.VirtualJaguar.yml`](flatpak/io.github.libretro.VirtualJaguar.yml),
  runtime `org.freedesktop.Platform` 25.08. It builds the checked-out tree (`type: dir`); `manifests/render.sh`
  derives the Flathub form (git tag + commit) from the same file, so there is one manifest.
* **Offline.** Flatpak builds have no network. SDL3, Dear ImGui, FreeType, rcheevos, the librashader headers and source archive (+ vendored crates), the
  slang-shaders archive, the two libretro dats and the 41 cheat files + licence are `archive`/`file` sources with
  SHA-256, wired in through the app's offline knobs (`FETCHCONTENT_SOURCE_DIR_SDL3|IMGUI|FREETYPE|RCHEEVOS`,
  `VJ_LIBRASHADER_INCLUDE_DIR`, `VJ_SLANG_SHADERS_ARCHIVE`, `VJ_APP_LIBRETRO_DB_DIR`, `VJ_APP_CHEATS_SOURCE_DIR`),
  and `FETCHCONTENT_FULLY_DISCONNECTED=ON` so a missed download fails the build. The cheat sources are generated
  from `app/cmake/VJCheatManifest.cmake`: when that file's pinned commit changes, regenerate the manifest's cheat
  entries (same names, URLs and hashes). A `post-install` step fails the build if the data directories or libcurl
  are missing.
* **Shaders (librashader, #951).** Upstream publishes no Linux binary and the sandbox cannot load a host library,
  so the manifest's `librashader` module builds the C API from the pinned release tag
  (`librashader-v0.12.1` archive, SHA-256) with `org.freedesktop.Sdk.Extension.rust-stable` (Rust 1.98 on the 25.08
  branch; librashader needs >= 1.88), offline: `build-options` puts `/usr/lib/sdk/rust-stable/bin` on `PATH`, sets
  `CARGO_HOME=/run/build/librashader/cargo` and `CARGO_NET_OFFLINE=true`, and every crate is a hash-pinned source
  in [`flatpak/cargo-sources.json`](flatpak/cargo-sources.json) (which also carries the `cargo/config` that points
  cargo at the vendored sources). Only the OpenGL runtime is built
  (`--no-default-features --features runtime-opengl`, the one backend the app uses). The result is installed as
  `/app/lib/librashader.so`, which the app finds through the sandbox's `LD_LIBRARY_PATH`, with the MPL-2.0 text in
  `/app/share/licenses/io.github.libretro.VirtualJaguar/`. The slang presets ship beside the executable as before.
  **Bumping librashader** (also keep the header pins and `VJ_LIBRASHADER_TAG` in `cmake/VJShaders.cmake` in step
  when the C ABI changes):
  1. change the tag in the module's archive `url`, and its `sha256` (`curl -L <url> | shasum -a 256`);
  2. regenerate the crate list from that tag's lock file with
     [flatpak-cargo-generator](https://github.com/flatpak/flatpak-builder-tools/tree/master/cargo)
     (`pip install aiohttp tomlkit PyYAML`; last run with `cargo/flatpak-cargo-generator.py` at
     flatpak-builder-tools `41c20aa`):
     ```
     curl -L https://github.com/SnowflakePowered/librashader/archive/refs/tags/librashader-vX.Y.Z.tar.gz | tar xz
     python3 flatpak-cargo-generator.py librashader-librashader-vX.Y.Z/Cargo.lock -o app/packaging/flatpak/cargo-sources.json
     ```
  3. commit both together. The file lists every crate in the workspace lock (a vendored build must be able to
     resolve all of them, including the Windows/Metal/Vulkan backends it does not compile); that is why it is
     large. `manifests/render.sh` copies it next to the rendered Flathub manifest.
* **Permissions:** network (achievements, box art, netplay), Wayland + X11 fallback, PulseAudio, `--device=all` (gamepads),
  IPC, and `--filesystem=home:ro`. The last is needed because the library scans folders and a `.cue` needs its
  sibling `.bin` tracks, neither of which the file-chooser portal grants; it is read-only and limited to the home
  directory. Settings, saves and states live in `~/.var/app/io.github.libretro.VirtualJaguar/data/VirtualJaguar`
  (SDL's pref path follows `XDG_DATA_HOME`, which Flatpak sets).
* **CI:** `app-package.yml` job `flatpak` uses `flatpak/flatpak-github-actions/flatpak-builder` v6.8 (pinned by commit) in
  the Flathub image `freedesktop-25.08`, with the action's own artifact upload disabled (it would not be named
  `app-package-*`); the bundle is smoke-tested (install, headless frame hash) and uploaded as `app-package-flatpak`.
  x86_64 only for now; a native aarch64 leg on `ubuntu-24.04-arm` would be a matrix entry.
* **Install:** `flatpak remote-add --user --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo`
  (the runtime comes from Flathub), then `flatpak install --user VirtualJaguar-X.Y.Z-linux-x86_64.flatpak`.

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
  The manifest is built in CI on every package run (bundle smoke test included). Flathub's own linter and
  review may ask for changes (for instance a librashader module).

## File associations and native integration (#833)

Which extensions each package registers, and where the list lives. The set is `.j64 .jag .rom .abs .cof`
(cartridges, executables) and `.cue .chd .cdi` (CD images); `.bin` / `.prg` are deliberately left out (too generic).
**One source of truth:** `VJ_ASSOC_LIST` in `src/platform/commands.h`. The CTest `platform_assoc_sync` fails if any
of these drifts, and checks that the application id `io.github.libretro.VirtualJaguar` is the same in all of them:

| OS | Where | What it does |
| --- | --- | --- |
| macOS | `macos/Info.plist.in`: `CFBundleDocumentTypes` (+ an exported UTI `io.github.libretro.virtualjaguar.rom` for `.j64` / `.jag`) | Finder shows "Atari Jaguar game", Open With lists the app, a game dropped on the Dock icon or opened from Finder arrives as the drop event. Rank: Default for games, Alternate for CD images (never takes over `.cue` / `.chd`). |
| Windows | `windows/virtualjaguar.iss`: `[Registry]` | Always: `Applications\virtualjaguar.exe` (`SupportedTypes`: Open With, and what lets Windows show recent games in the Jump List). The **associate** task (unchecked, per user via `HKA`): ProgId `VirtualJaguar.Game`, an `OpenWithProgids` entry per extension, and the default handler for `.j64` / `.jag` only. Shortcuts carry `AppUserModelID` = the id the program sets itself. Windows 10/11 keep the user's own default (UserChoice). |
| Linux | `linux/io.github.libretro.VirtualJaguar.xml` (shared-mime-info) and `MimeType=` in the `.desktop` file | Installed to `share/mime/packages` by `cmake --install`, so the .deb (dpkg trigger), the Flatpak (export) and the AppImage's AppDir carry it. `rom`, `abs`, `cof`, `cue`, `chd`, `cdi` are generic: they have a lower weight (30) so a type another package already defines keeps winning. |

Other native pieces that live in packaging: the Windows manifest (`windows/virtualjaguar.manifest`, per-monitor-v2 DPI,
long paths, Windows 10/11; MSVC links it from the source list, MinGW's windres embeds it from the `.rc`), the `.desktop`
Actions (Open Game Library, Boot Without Cartridge) and `StartupWMClass` (= the application id, which SDL uses as the
Wayland `app_id` / X11 class), and the Flatpak's `--talk-name=org.kde.StatusNotifierWatcher` for the optional tray icon.

## Placeholder icon

`icons/virtualjaguar.svg` is a placeholder (original artwork, no Atari marks). Replace it and run
`icons/make-icons.sh` (needs `rsvg-convert`, ImageMagick, macOS `iconutil`) to regenerate the committed
PNG, `.ico` and `.icns` files.

## Clean-machine checks CI cannot do

Gatekeeper first-run on a freshly downloaded signed zip, SmartScreen on the installer, Intel Mac behavior
(CI may not run the x86_64 slice), Wayland/X11/PipeWire sound on real Linux desktops, and the AppImage on
distributions older than Ubuntu 22.04.

Windows packages also stage Mesa software OpenGL through
`windows/stage-software-gl.py` (MSYS2 Mesa, Python and objdump required).
The normal executable automatically retries once with the bundled software app
if graphics startup fails. The retry preserves game arguments, working directory
and the original config/save root; users do not need a separate launcher.
Software rendering uses the CPU and can be slower. Keep its dependency notices
and matching architecture alongside the software executable.
