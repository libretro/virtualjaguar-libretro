# macOS direct updates

Sparkle is opt-in at configure time via `VJ_MAC_DIRECT_SPARKLE_SDK`. Use
`fetch-sparkle.sh` to obtain the pinned SDK. Configure and install the arm64
and x86_64 builds separately with `-DCMAKE_OSX_DEPLOYMENT_TARGET=12.0`, then combine their install trees with
`make-universal.sh`. Use `sparkle-app.sh` to assemble the direct app.
Keep the existing managed build and `VirtualJaguar-<version>-macos-universal.zip`
for Homebrew. The direct edition requires macOS 12 or newer because of the
pinned framework’s deployment target. Steam uses the managed payload. Mac App Store configurations
reject the Sparkle SDK option.

The proposed integration contract for #1211 and #1212 is to show a manual check
and a persisted background-check toggle for direct distributions. Start with
background checks off and require the user to initiate installation. Defer
checks and relaunch while a game is loaded, including a paused game. The macOS
implementation uses the native application menu and Sparkle's preferences.
The Windows backend can use its own menu and updater without sharing native
UI code. Release orchestration should stage and qualify both platforms'
direct artifacts independently of the existing managed artifacts.

## Release handoff

1. Stable uses the stable tag and its numeric release version. Nightly uses
   the release workflow's run number for both the bundle version and the
   `macos-nightly-<run-number>` tag. Serialize nightly publication and
   reject a version at or below the current feed version. A rerun uses the
   same version and can reuse identical published bytes.
2. Assemble the direct app with the channel's trusted public key, then use
   the existing Developer ID app signing action to sign inside out, notarize
   and staple. Create the final ZIP from the stapled bundle with
   `ditto -c -k --keepParent`. Sign the final ZIP bytes with Sparkle's
   `sign_update` in trusted release CI. Keep the private key outside the
   source tree and PR CI. Give stable and nightly distinct trust keys.
3. Use a dedicated releases repository with GitHub release immutability
   enabled so the existing rolling nightly remains replaceable. Upload
   `VirtualJaguar-<tag>-macos-universal-direct.zip` before finalizing
   the immutable release. Use a build-specific immutable prerelease for
   nightly. Preserve the existing rolling nightly release for managed
   downloads.
4. Run `sparkle-appcast.sh` with the releases repository, channel, tag,
   asset name, signature, trusted public key and previous feed version
   (use `0` for the initial release). It downloads the archive
   from the public URL and validates release immutability, asset digest,
   EdDSA signature, Developer ID signature, stapled ticket and channel.
   Publish the resulting candidate to
   `https://jaguar.provenance-emu.com/updates/macos/<channel>/appcast.xml`
   only after that validation succeeds. The script itself writes to stdout.

Before enabling either feed, qualify two consecutive fixture releases on
Intel and Apple Silicon, including signature rejection, stable/nightly
isolation, persisted preferences and save preservation. Use fixture keys
and ad-hoc code signatures for local smoke tests. Use trusted release CI for
Developer ID and notarization qualification.
