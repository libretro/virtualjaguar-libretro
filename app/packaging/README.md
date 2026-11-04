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
The PR's **Verify shared Release packages** check requires all fifteen package/runtime jobs and
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
| `VirtualJaguar-X.Y.Z-linux-x86_64.flatpak`, `...-linux-aarch64.flatpak` | single-file Flatpak bundles, app id `io.github.libretro.VirtualJaguar`; see Flatpak notes |

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

Windows (Authenticode through **Azure Artifact Signing**, [issue #1150](https://github.com/libretro/virtualjaguar-libretro/issues/1150);
the one-time Azure setup is under "Every Windows binary is signed" below). Nothing certificate-shaped is
stored anywhere: CI obtains a short-lived certificate from Microsoft's HSM at signing time, authenticated
with OpenID Connect, so there is no client secret either.

| Name | Kind | What |
|---|---|---|
| `AZURE_CLIENT_ID` | secret | application (client) id of the Entra app registration CI signs as |
| `AZURE_TENANT_ID` | secret | the Entra tenant (directory) id |
| `AZURE_SUBSCRIPTION_ID` | secret | the subscription that holds the Artifact Signing account |
| `AZURE_SIGNING_ENDPOINT` | variable | the account's regional endpoint, e.g. `https://eus.codesigning.azure.net` |
| `AZURE_SIGNING_ACCOUNT` | variable | the Artifact Signing account name |
| `AZURE_SIGNING_PROFILE` | variable | the certificate profile name (Public Trust) |

All six must exist for signing to run; a partial set skips with a `::notice::` naming what is missing,
and the packages ship unsigned (SmartScreen then warns on first run). Variables, not secrets, for the
last three: they are names, they appear in the job log anyway, and `release.yml`'s gate reads them to
decide whether to expect a signature.

Release key material (issue #1151; one-time setup, commands below):

| Name | Kind | What |
|---|---|---|
| `MINISIGN_SECRET_KEY` | secret | the minisign secret key that signs `SHA256SUMS.txt` (the key file's contents) |
| `MINISIGN_PASSWORD` | secret | its password (empty for a key generated with `-W`) |
| `APPIMAGE_GPG_PRIVATE_KEY` | secret | armored GPG private key that signs the AppImages |
| `APPIMAGE_GPG_PASSPHRASE` | secret | its passphrase (empty for a key without one) |

Signing runs when the caller asks (`release.yml` and `app.yml` do; a manual dispatch needs the `sign`
input) and the secrets exist. GitHub passes no secrets to a fork pull request, so those builds stay
ad-hoc signed and the log warns. The checksum signature and the attestations are made by `release.yml`
only (its `stage` job), never by PR runs.

### Signed checksums (minisign, #1151)

`release.yml`'s `stage` job writes `SHA256SUMS.txt` once for the whole release and signs it with the
project release key: `SHA256SUMS.txt.minisig` is attached next to it on every tag, nightly and
prerelease ([`scripts/release/sign-sums.sh`](../../scripts/release/sign-sums.sh)). The public half is
committed as `docs/release-signing.pub` and shown on the site's Download page; the installers pin the
same key. Without the secret the step prints a loud warning and ships no signature; once the public key
is committed the gate below fails a tag or `release/**` build that lacks the signature.

The maintainer generates the key on their own machine. `minisign` is in Homebrew and in Ubuntu 23.04+:

```sh
minisign -G -p docs/release-signing.pub -s ~/.minisign/virtualjaguar-release.key   # asks for a password
cat docs/release-signing.pub                     # two lines; the second is the key itself
gh secret set MINISIGN_SECRET_KEY --repo libretro/virtualjaguar-libretro < ~/.minisign/virtualjaguar-release.key
gh secret set MINISIGN_PASSWORD   --repo libretro/virtualjaguar-libretro   # paste the password
```

Then, in one commit: `docs/release-signing.pub`, and the key's second line pasted into
`MINISIGN_PUBKEY=` in `tools/install.sh` and `$script:MinisignPubKey` in `tools/install.ps1`
(the Installer workflow fails if the three disagree). Keep the secret key out of the repository and
back it up: a lost key means a new key, a new `.pub`, and every pinned copy updated.

Verify a download: `minisign -Vm SHA256SUMS.txt -p docs/release-signing.pub`, then
`sha256sum --check SHA256SUMS.txt`.

### Signed AppImages (GPG, #1151)

`appimagetool --sign` embeds an armored GPG signature of the image (and the public key) in the
AppImage's `.sha256_sig` / `.sig_key` ELF sections; `app-package.yml`'s Linux jobs do that when the
`APPIMAGE_GPG_PRIVATE_KEY` secret exists. appimagetool signs through gpgme and takes the passphrase from
`APPIMAGETOOL_SIGN_PASSPHRASE`, so a passphrase-protected key works in CI. appimagetool ships no
verifier: [`linux/verify-appimage.py`](linux/verify-appimage.py) re-hashes the file with the two
sections zeroed and runs `gpg --verify`, on Linux or macOS. The public key is committed as
`docs/release-signing-appimage.asc`; once it is, the gate requires every AppImage to be signed by it.

```sh
gpg --quick-generate-key "Virtual Jaguar release signing <releases@jaguar.provenance-emu.com>" ed25519 sign never
gpg --armor --export  "Virtual Jaguar release signing" > docs/release-signing-appimage.asc
gpg --armor --export-secret-keys "Virtual Jaguar release signing" \
  | gh secret set APPIMAGE_GPG_PRIVATE_KEY --repo libretro/virtualjaguar-libretro
gh secret set APPIMAGE_GPG_PASSPHRASE --repo libretro/virtualjaguar-libretro   # the key's passphrase
```

Verify a download: `python3 app/packaging/linux/verify-appimage.py VirtualJaguar-X.Y.Z-linux-x86_64.AppImage --pubkey docs/release-signing-appimage.asc`.

### Build provenance attestations (#1151)

The `stage` job also runs `actions/attest-build-provenance` over every file it stages, `SHA256SUMS.txt`
and its signature included, so GitHub holds a signed SLSA provenance statement naming the commit and
workflow run that produced each asset. No secret is involved (the job's OIDC token signs it). Verify
any asset with the GitHub CLI:

```sh
gh attestation verify virtualjaguar_libretro-linux-x86_64.so --repo libretro/virtualjaguar-libretro
```

### The release gate (#1152)

After staging and before anything is published, `release.yml`'s `verify-signatures` job
([`scripts/release/verify.sh`](../../scripts/release/verify.sh), on a macOS runner) checks every asset
against the state its class is expected to have: macOS files Developer ID signed and notarized
(`codesign`, `spctl`, `stapler`, plus the sidecar the signing job wrote for bare Mach-O files, which
cannot be stapled); the Windows app zips and installers Authenticode-signed whenever the six Azure values
exist (`WINDOWS_APP_EXPECT` and `WINDOWS_EXPECT`, passed by `release.yml`), the Windows core DLLs and
tools likewise; AppImages GPG-signed by the committed key once that key exists; `SHA256SUMS.txt` signed once its key exists; everything attested.
The result is one table per artifact (signed, notarized, attested) in the job summary and in the
release body. On tags and `release/**` a failed expectation fails the run and nothing is published;
on `develop` it only warns, so a signing-service outage cannot withhold the core nightly. A
`workflow_dispatch` of `release.yml` with **Dry run** ticked runs the whole pipeline up to the gate on
any branch and publishes nothing.

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

Each of these signing steps also records what it did in a `signing-status-*` artifact
([`scripts/release/signing-status.sh`](../../scripts/release/signing-status.sh)): the release gate
re-checks the signature on the file itself, and reads the sidecar only for whether Apple issued the
notarization ticket, which a bare Mach-O file cannot carry.

### Every Windows binary is signed (#1150)

Every Windows file we ship, the app packages, the core DLLs, `cue2cdi` and the jagcd tools, carries an
Authenticode signature (SHA-256 digest,
RFC 3161 timestamp from `http://timestamp.acs.microsoft.com`) made with **Azure Artifact Signing**
(formerly Trusted Signing, Basic tier). Microsoft issues a short-lived Public Trust certificate from its
HSM for each signing session, so no certificate file or password exists in the repository or in the
secrets, and CI authenticates with OpenID Connect. The composite action
[`.github/actions/windows-sign`](../../.github/actions/windows-sign/action.yml) wraps `azure/login` and
`azure/artifact-signing-action` (both pinned by commit) and then re-reads every file with
[`windows/verify-signatures.ps1`](windows/verify-signatures.ps1) (`Get-AuthenticodeSignature`, the
`signtool verify /pa` verdict): a file that is not `Valid` and timestamped fails the job before anything
is packaged. SmartScreen reputation is per file hash and accrues on its own; the signature buys the named
publisher and removes the "unknown publisher" dialog, nothing more, and an EV certificate would not either.

| What | Where | Signed |
|---|---|---|
| `virtualjaguar.exe`, `librashader.dll`, the `software-opengl/` copy of both and the bundled Mesa DLLs, x64 and arm64 | `app-package.yml` job `windows-package`, before packaging | yes |
| `VirtualJaguar-X.Y.Z-windows-{x64,arm64}-setup.exe` | the same job, after Inno Setup built them | yes |
| `unins000.exe`, the uninstaller Inno embeds | no: Inno's `SignTool` hook needs a command-line signer and the action is a PowerShell module; it runs per-user, so no UAC prompt names it | no |
| the Store MSIX (`windows-store.yml`) | Microsoft signs it during certification | not by us |
| `virtualjaguar_libretro-windows-{x86_64,i686}.dll`, `cue2cdi-windows-x86_64.exe` | `release.yml` job `build`, Windows rows, after strip and before the upload | yes |
| `chdman.exe`, `jagcd-chd-check.exe` in `jagcd-tools-windows-x64.zip` | `chdman-tools.yml`, before the zip | yes |

How the Windows leg of `app-package.yml` is laid out, and why:

* **Build on each architecture's own runner** (`windows`, x64 on `windows-latest`, arm64 on
  `windows-11-arm`), ending in the install tree, uploaded as the intermediate `windows-stage-<arch>`
  (never a release asset: `scripts/release/stage.sh` skips it).
* **Sign and package both architectures in one x64 job** (`windows-package`): the signing action does
  not run on Windows ARM runners (its signing dlib ships x64 and x86 only; "we currently do not support
  Windows Arm runners"), and Authenticode does not care which CPU an image is for. It signs every PE file
  in both trees, builds the portable zips and the Inno installers from the signed trees, signs the
  installers, checks every PE inside the final zips and installers, writes the gate sidecars and uploads
  `app-package-windows-<arch>`.
* **Smoke-test the final packages on each architecture's native runner** (`windows-smoke`), from the
  uploaded artifact: Authenticode re-checked on the downloaded files, the portable zip run, the installer
  silently installed, run and uninstalled.
* **The Mesa DLLs are signed too.** They are MSYS2's MIT-licensed build, unsigned upstream, and nothing in
  them checks its own signature. We are the publisher of the package they ship in, the gate scans every PE
  file inside the zip, and a tree with a mix of signed and unsigned DLLs is exactly what heuristics flag.
  Cost: about ten extra signatures per architecture per run.
* **Where it runs:** same-repo pull requests (`app.yml` passes the three secrets and `id-token: write`),
  nightlies, `release/**` and tags (`release.yml`), and a dispatch with `sign` ticked. Fork pull requests
  receive no secrets and build unsigned, with the notice. **Quota:** the Basic tier includes 5,000
  signatures a month; one `app-package.yml` run spends about 40 (two trees of 19 PE files plus two
  installers), a `release.yml` run about 45 more for the cores and tools. If pull-request traffic ever
  threatens the quota, the lever is `sign: false` in `app.yml`'s `package` job; the release lanes keep
  signing.
* **The gate:** `release.yml`'s `verify-signatures` passes `WINDOWS_APP_EXPECT=signed` (app zips and
  installers) and `WINDOWS_EXPECT=signed` (core DLLs, `cue2cdi`, the tools zip) when all six Azure
  values exist, so a tag or `release/**` build whose Windows files lost their signature fails before
  publication; without the values the gate expects `unsigned` and only the notice says so (the
  v3.11.0 state).

#### One-time Azure setup (the maintainer)

1. **Resource provider.** On the subscription, register `Microsoft.CodeSigning`
   (`az provider register --namespace Microsoft.CodeSigning`; `az extension add --name artifact-signing`).
2. **Account.** Create an Artifact Signing account, SKU Basic, in a supported region; the region fixes
   the endpoint (East US = `https://eus.codesigning.azure.net`, West US 2 = `https://wus2.codesigning.azure.net`,
   North Europe = `https://neu.codesigning.azure.net`, ...):
   `az artifact-signing create -n <account> -l eastus -g <resource-group> --sku Basic`.
3. **Identity validation.** Assign yourself **Artifact Signing Identity Verifier** on the account (IAM),
   then Objects > Identity validations > New identity > Public, type Individual (US or Canada), which reads
   the name and address from the subscription's billing account; the validated name becomes the
   certificate's CN. Portal only; 1 to 20 business days. This is the long pole: start it first.
4. **Certificate profile.** Objects > Certificate profiles > Create > Public Trust, bound to that
   validation (`az artifact-signing certificate-profile create -g <rg> --account-name <account>
   -n <profile> --profile-type PublicTrust --identity-validation-id <id>`).
5. **App registration** (Entra ID > App registrations > New), no client secret. Note the Application
   (client) ID and the Directory (tenant) ID.
6. **Role.** On the account's IAM, add the role assignment **Artifact Signing Certificate Profile
   Signer** to the app registration's service principal (search "User, group, or service principal";
   only users are listed by default).
7. **Federated credentials** on the app registration (Certificates & secrets > Federated credentials >
   Add > GitHub Actions deploying Azure resources; issuer `https://token.actions.githubusercontent.com`,
   audience `api://AzureADTokenExchange`). The subjects CI presents, one credential each:
   * `repo:libretro/virtualjaguar-libretro:pull_request` -- every same-repo pull request (`app.yml`);
   * `repo:libretro/virtualjaguar-libretro:ref:refs/heads/develop` -- the nightly;
   * `repo:libretro/virtualjaguar-libretro:ref:refs/heads/release/vX.Y.Z` and
     `repo:libretro/virtualjaguar-libretro:ref:refs/tags/vX.Y.Z` -- the release lane and the tag. Either
     one pair per release, added with the release branch (a missing one makes `azure/login` fail, so a
     release can never slip out unsigned by accident), or once and for all with two *flexible* federated
     credentials (portal or Graph only, still preview): `claims['sub'] matches
     'repo:libretro/virtualjaguar-libretro:ref:refs/heads/release/*'` and the same for
     `refs/tags/v*`;
   * a `workflow_dispatch` of `app-package.yml` with `sign` presents `ref:refs/heads/<that branch>`.
   An alternative that needs one credential for everything is `environment: windows-signing` on the
   `windows-package` job (subject `repo:libretro/virtualjaguar-libretro:environment:windows-signing`); not
   used because every pull-request run would then record a "deployment" on the PR.
8. **Secrets and variables:** `gh secret set AZURE_CLIENT_ID`, `AZURE_TENANT_ID`, `AZURE_SUBSCRIPTION_ID`;
   `gh variable set AZURE_SIGNING_ENDPOINT`, `AZURE_SIGNING_ACCOUNT`, `AZURE_SIGNING_PROFILE`.
9. **Prove it:** dispatch `app-package.yml` on `develop` with `sign` ticked (or open a PR that touches
   `app/`). The `Windows sign and package` job summary carries the Authenticode table with the validated
   name as signer and Microsoft's timestamp authority; both `Windows smoke` jobs re-check the downloaded
   files. Then dispatch `release.yml` with **Dry run**: the gate's table must show every Windows app row
   signed. From a downloaded asset: `signtool verify /pa /v <file>` or
   `(Get-AuthenticodeSignature <file>).Status`. The first signed tag closes #1150.

What goes wrong, and how it shows: no federated credential for this run's subject -> `azure/login`
fails and the job is red (never an unsigned release); the role is missing -> the signing action fails
with a 403 naming the profile; a runner whose certificate store has not yet fetched Microsoft's
"Microsoft Identity Verification Root Certificate Authority 2020" -> the verification step reports
`UnknownError` ("terminated in a root certificate which is not trusted"); Windows fetches that root on
demand through the automatic root update, so a rerun normally clears it.

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

**`librashader/pin.env` is the single source of the runtime's version on every platform** (tag, commit, archive
SHA-256, Rust version, Windows feature set): every package below is built from it, none takes an upstream binary.

| Package | Where the library comes from |
|---|---|
| macOS universal (arm64 + x86_64) | **built from source in CI**, both slices, then `lipo`-merged |
| AppImage / `.deb`, x86_64 and aarch64 | **built from source in CI** on the packaging runner (Ubuntu 22.04) |
| Windows x64 / ARM64 (zip, installer, Store MSIX) | **built from source in CI** (#1153): MSVC targets, OpenGL runtime only, static CRT |
| Flatpak | see Flatpak notes |

Why source: upstream's `x86_64-macos` v0.12.0 zip holds an **arm64** dylib (`lipo`-verified), so Intel Macs had no
shader runtime, and upstream publishes no Linux binary. Building both macOS slices ourselves (instead of mixing
upstream's arm64 with a self-built x86_64) keeps one version, one deployment target and one install name in the
universal file. Upstream's Windows DLLs (v0.12.0) import `D3DX9_43.dll`, the June 2010 DirectX SDK
redistributable that a stock Windows 10/11 does not have (`librashader-runtime-d3d9` links it with
`kind = "raw-dylib"`), plus `VCRUNTIME140.dll` / `MSVCP140.dll` from the VC++ redistributable, so `LoadLibrary`
fails wherever those are not installed: on GitHub's runners the v3.11.0 `shaders_library` test **skipped** on both
Windows legs (the DLL "failed to load", reported as a skip because nothing required it) and the packages shipped a
runtime that never worked on a clean machine. The from-source DLL is built with `--no-default-features --features
runtime-opengl` (the app only uses the OpenGL chain; the Flatpak module does the same) and
`RUSTFLAGS=-C target-feature=+crt-static`, so it imports nothing but Windows' own DLLs.

* **The recipe** is [`.github/workflows/librashader.yml`](../../.github/workflows/librashader.yml), called by
  `app-package.yml` (job `librashader`, which the macOS and Linux jobs `need`). The pin is
  [`librashader/pin.env`](librashader/pin.env): tag `librashader-v0.12.1`, its commit and the SHA-256 of the GitHub
  archive for that commit, a pinned stable Rust, and the macOS deployment target. The build is
  [`librashader/build-from-source.sh`](librashader/build-from-source.sh): fetch, verify the digest, then
  `cargo xtask build --profile optimized --target <triple> -- --locked`. `VJShaders.cmake` reads the same tag for
  the C headers, whose digests are pinned there (the 0.12.1 headers are byte-identical to 0.12.0's, ABI 2).
* **Targets:** `x86_64-apple-darwin` (cross-built on the Apple-silicon runner), `aarch64-apple-darwin`,
  `x86_64-unknown-linux-gnu` and `aarch64-unknown-linux-gnu` (native, `ubuntu-22.04` / `ubuntu-22.04-arm`,
  glibc 2.35 like the packages), `x86_64-pc-windows-msvc` (native) and `aarch64-pc-windows-msvc` (cross-compiled
  with Visual Studio's ARM64 tools), both on `windows-latest` with rustup's pinned toolchain, as upstream's own CI
  builds them. macOS uses `MACOSX_DEPLOYMENT_TARGET=11.0` and the install name `@rpath/librashader.dylib` (cargo's
  default would bake the runner's absolute path in). Windows uses the MSVC targets rather than MSYS2's Rust because
  MSYS2's rustc follows its repository, not `pin.env`, and a DLL's C ABI is toolchain-neutral: the MinGW-built
  `virtualjaguar.exe` loads it like any system DLL (proven by `shaders_library` on both legs).
* **Cache and artifacts.** Each target is cached under
  `librashader-<commit>-<triple>-rust<ver>-mac<min>-feat<features>-v2` (a cold build is ~10 minutes with LTO, a hit
  is seconds; bump `-v2` in the workflow to force a rebuild after a recipe change) and uploaded as the artifact
  `librashader-<triple>` (library + `BUILD-INFO.txt`). These are intermediates: `release.yml` leaves `librashader-*`
  out of the release assets. The workflow takes a `targets` input (a JSON array of triples): `windows-store.yml` and
  `windows-gl-test.yml` build only the Windows ones.
* **Checks.** In `librashader.yml`: the architecture (`lipo -archs` / ELF machine / PE machine), `minos`, install
  name, the highest `GLIBC` symbol version (must be <= 2.35), only C/C++-runtime `NEEDED` entries, on Windows an
  import table of Windows' own DLLs only plus the exported C API
  ([`windows/check-librashader-dll.py`](windows/check-librashader-dll.py), stdlib Python, runnable on any host),
  and (same-architecture runners) a load that reads the ABI version. In `app-package.yml`: `vj_shader_tests library`
  with `VJ_REQUIRE_LIBRASHADER=1` on **every** leg, Windows included (a runtime that does not load fails instead of
  skipping, and the test prints why on both streams); the macOS bundle's universal dylib is loaded by an arm64
  process and, under Rosetta, an x86_64 one; for the AppImage and the `.deb`, `linux/check-librashader.sh`, and for
  the unpacked Windows zip and the silently installed Inno Setup tree, `windows/check-librashader.sh`, check the
  file, its CPU and dependencies, load it, and run `vj_shader_tests probe` from the package's own folder, i.e. the
  app's real lookup (beside the executable, `VJ_LIBRASHADER` unset). `windows-store.yml` runs the DLL check on the
  install tree, the MSIX layout and the MakeAppx round-trip copy.
* **Bumping:** change `LIBRASHADER_TAG`, `LIBRASHADER_COMMIT` and `LIBRASHADER_SHA256` together (and re-pin the header
  digests in `VJShaders.cmake` if `include/` changed; its comment says how), update `THIRD-PARTY-NOTICES.txt`.
* **Local Windows builds** get no runtime by default (`VJ_APP_FETCH_LIBRASHADER` is OFF there since #1153; the
  configure log says so): build one with `build-from-source.sh x86_64-pc-windows-msvc <dir>` (rustup + Visual
  Studio, from Git Bash or MSYS2) or download the `librashader-<triple>` artifact of any recent run, then pass
  `-DVJ_LIBRASHADER_LIB=<dir>/librashader.dll`. `-DVJ_APP_FETCH_LIBRASHADER=ON` still downloads upstream's v0.12.0
  DLL as an explicit opt-in; it loads only with the June 2010 DirectX and VC++ redistributables installed.
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
  x64 package also installs, emulated, on ARM64 Windows). The `aarch64-pc-windows-msvc` librashader runtime,
  built from source (Shader runtime above), is bundled.
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
* **CI:** `app-package.yml` job `flatpak` is a matrix over `x86_64` (`ubuntu-24.04`) and `aarch64` (`ubuntu-24.04-arm`,
  #1156), each leg NATIVE inside the multi-arch Flathub image `freedesktop-25.08` with
  `flatpak/flatpak-github-actions/flatpak-builder` v6.8 (pinned by commit) and the action's own artifact upload disabled
  (it would not be named `app-package-*`). The job refuses a non-native host (`uname -m` must equal the leg's arch), so
  the Rust librashader module is never built under QEMU user-mode emulation. Each bundle is smoke-tested (install,
  `flatpak info` arch, headless frame hash, bundled data, `librashader.so` resolves and loads through ctypes with ABI 2)
  and uploaded as `app-package-flatpak` (x86_64, the historical name) / `app-package-flatpak-aarch64`. Each leg writes its
  wall time and whether the flatpak-builder cache was warm to the job summary. The release signing gate
  (`scripts/release/verify.sh`) classifies both bundles as `flatpak` by extension (attestation only, no platform signature).
* **Install:** `flatpak remote-add --user --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo`
  (the runtime comes from Flathub), then `flatpak install --user VirtualJaguar-X.Y.Z-linux-<x86_64|aarch64>.flatpak`.

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

Windows packages also stage a bundled Mesa OpenGL through
`windows/stage-software-gl.py` (MSYS2 Mesa, Python and objdump required) in
the `software-opengl/` folder (the name predates the Direct3D 12 path below).
The normal executable automatically retries once with the bundled app if
graphics startup fails, which on a stock Windows 10/11 install without a vendor
OpenGL driver (Windows on ARM, VMs, Remote Desktop, the basic display adapter)
means WGL handed us the GDI Generic OpenGL 1.1 driver. The retry preserves game
arguments, working directory and the original config/save root; users do not
need a separate launcher. The launcher picks Mesa's driver: `d3d12` (OpenGL on
the machine's Direct3D 12 driver, hardware rendering, the same layer as
Microsoft's OpenGL Compatibility Pack) when a hardware D3D12 adapter exists,
pinned to that adapter; `llvmpipe` (CPU rendering, slower) otherwise, including
when the only D3D12 adapter is the Microsoft Basic Render Driver (WARP), which
is software too and slower than llvmpipe. A `GALLIUM_DRIVER` already in the
environment wins. The `[gl]` log lines name the choice and the renderer that
came up. Keep the dependency notices and matching architecture alongside the
bundled executable.
