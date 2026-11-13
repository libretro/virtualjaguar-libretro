# Publishing direct app updates

The Homebrew cask and WinGet manifest keep using the existing macOS ZIP and
Windows installers. The dedicated update repository only carries the separate
Sparkle and WinSparkle packages. See the
[macOS](../app/packaging/macos/SPARKLE.md) and
[Windows](../app/packaging/windows/UPDATES.md) packaging instructions.

## Provisioning

Set `VJ_UPDATE_REPOSITORY` to the dedicated public GitHub repository with
release immutability enabled. Initialize a `feeds` branch. Configure the
repository variables `VJ_SPARKLE_STABLE_KEY`, `VJ_SPARKLE_NIGHTLY_KEY`,
`VJ_WINSPARKLE_STABLE_KEY`, and `VJ_WINSPARKLE_NIGHTLY_KEY` with the public keys
used by the direct builds. Keep private signing keys in the trusted signing
environment. The `direct-updates` GitHub environment needs a
`VJ_UPDATE_FEED_TOKEN` scoped to writing contents in the dedicated repository.

In the website publisher, check out the dedicated repository's `feeds` branch
into a separate directory and pass its `updates` directory to
`python3 scripts/build_site.py --releases-from-github --update-feeds <checkout>/updates`.
This serves the feeds under the existing `/updates/macos/` and
`/updates/windows/` paths without making feed commits trigger app builds.

## Release procedure

1. Stage the separate direct packages, apply Developer ID and notarization
   or Authenticode, and sign the final distributable bytes with the appropriate
   updater key. Use the same Release workflow run number for both nightly
   platforms and both Windows architectures.
2. Qualify the signed upgrade and save preservation on the supported
   architectures. Publish the immutable releases using the asset names and tags
   in the platform packaging instructions.
3. Dispatch **Publish direct update feeds** on `develop` with the three detached
   signatures and the release version or nightly build number. Leave **publish**
   off to verify the public artifacts and inspect the candidate XML.
4. Dispatch with **publish** on once the candidates have been reviewed. The
   publication step checks the current branch for stale builds, writes all
   three feeds in one commit, and uses a non-force push. A concurrent change
   causes the push to fail. An exact replay is a no-op.
5. Run the site publisher and verify all three feed URLs from the public site.
