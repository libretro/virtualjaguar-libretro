# Direct Windows updates

The Windows build defaults to the managed distribution. For a separate
direct installer, configure a fresh Windows build directory with
`-DVJ_WINDOWS_DIRECT_UPDATES=ON`, `-DVJ_WINSPARKLE_PUBLIC_KEY=<base64 public key>`,
and `-DVJ_WINSPARKLE_FEED_BASE=<HTTPS feed directory>`. Use
`-DVJ_BUILD_CHANNEL=release` for stable or `-DVJ_BUILD_CHANNEL=nightly`
with `-DVJ_WINSPARKLE_NIGHTLY_BUILD=<positive integer>`. The build number
must increase for every published nightly. Use a single workflow's run
number for both architectures. A rerun can recover an unpublished build
but cannot replace an immutable release.

Install the build into its own staging tree. Run
`package.sh <stage> <display version> <out> --arch <x64|arm64> --installer <ISCC.exe> --direct-installer`.
The direct installer has its own Inno identity and default path for
each channel and architecture. Application data continues to live in
`%APPDATA%\VirtualJaguar`. Package the WinGet installer and portable ZIP
from the ordinary staging tree.

## Feed staging

Use separate files named `stable-x64.xml`, `stable-arm64.xml`,
`nightly-x64.xml` and `nightly-arm64.xml` under the configured feed directory.
Stable uses the numeric release version for comparison. Nightly uses the
build number and displays the release version.

Authenticode-sign the final Inno executable before producing its WinSparkle
Ed25519 signature in the trusted signing environment. The appcast tool takes
the detached base64 signature and public key. For the Windows direct
distribution, the release tag must be `windows-direct-<channel>-<build>`,
and GitHub must report the public release as immutable. Mark these direct-only
releases as prereleases so that the managed `releases/latest` URL continues
to resolve to the ordinary stable release. The rolling nightly release keeps
its existing download paths.

Run `build-appcast.py --help` on Windows with Python 3.11+, OpenSSL with
Ed25519 support and PowerShell. Pass the current channel and architecture
feed with `--previous`, or use `--first-entry` for initial provisioning.
The command downloads the public installer, validates the release asset
size and SHA-256, then verifies Ed25519 and Authenticode. It writes a local
XML file. Publish it only after verifying the signed native upgrades and
checking that the previous feed is still current.

For development, use a disposable keypair and a private fixture feed.
Exercise upgrades on native x64 and ARM64 with per-user and all-users
installs, including custom directories. Confirm that saves and settings
survive, background checks start off, a game can finish before installation,
other channels and architectures are excluded, and tampered installers
are rejected.

## Proposed shared UI and release integration

Expose “Check for Updates…” and “Check for updates automatically” through
the standalone app's menu and settings. Each platform supplies its own
backend. A fresh configuration starts with background checks off. The
updater prompts before installing, and a loaded or opening game prevents
shutdown for an update. A portable Windows ZIP can report an available
version and open the downloads page. The portable action has no installer
callback and leaves the data directory in place. The Store, Steam, WinGet
and Homebrew builds continue to use their distributor's update flow.

The shared app should start the Windows backend after its event loop and
configuration are ready, stop it before SDL teardown, and report game load
and close transitions. A Windows content open must acquire the backend's
install guard before starting a load. The backend posts the ordinary SDL
quit event after the user approves installation and the installer launches.

In the shared release workflow, keep direct builds and ordinary package
staging separate. The Windows path signs and verifies both native direct
installers before creating an immutable release. Feed validation takes
place against the public asset URLs after publication of the immutable
release. The shared feed publisher should serialize updates per channel
and reject stale build numbers immediately before publishing.
