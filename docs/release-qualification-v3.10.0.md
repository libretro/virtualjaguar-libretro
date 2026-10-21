# v3.10.0 package qualification

Candidate assets: rolling `prerelease`, source `304867bd48eb430a33b26dca22fd38e160123591`.
Release workflow: [39512209514](https://github.com/libretro/virtualjaguar-libretro/actions/runs/39512209514).
The merged release commit is `e7db53678b86e2fceb459e26cf09d2fdaaa4d395`; its tree matches the candidate.
This records candidate evidence, not a claim that a final v3.10.0 tag exists.

## Downloaded macOS package

Test host: macOS 26.6.2 (25G83), Apple Silicon. Published universal ZIP SHA-256:
`aaefa8ff96c102040a39549634c96686180ddd4718b271d682f42af6da32765f`.
It matches published `SHA256SUMS.txt`. The ARM64 and Intel core dylib hashes also match.

- Deep/strict code-signature verification passes. Developer ID Application:
  Joseph Mattiello (`S32Z3HMYVQ`), hardened runtime and secure timestamp.
- Gatekeeper assessment accepts the downloaded bundle as Notarized Developer ID.
- Stapled notarization-ticket validation passes.
- Published bundle boots public YARC for 300 frames with hash `f4c30ea3`, both
  natively and with the Intel slice under Rosetta.
- Rosetta is not a clean physical Intel Mac qualification. The host is not a clean
  macOS installation; isolated-profile installer checks are separately identified.

## Existing disposable-host package tests

All package jobs in the candidate Release run passed. Logs establish:

- Windows x64 portable extraction, public-ROM launch, payload validation; silent
  per-user installer install, public-ROM launch, silent uninstall and executable removal.
- Linux x86_64 and aarch64 AppImage extraction/launch, payload and dependency checks;
  Debian install, public-ROM launch, `/usr/bin` symlink, payload and shader-runtime
  checks, package removal.
- macOS bundle launch, universal slices, signing and stapling checks.

These tests run before upload on disposable hosted machines. They do not establish
all published-download update/data-preservation cases or physical Intel Mac behavior.
Windows assets are **unsigned**: the job explicitly reports SignPath is not configured.
External signing approval must not be described as complete.

## Installer update defect and regression

Qualification found that `tools/install.ps1` moved the old portable `data/` into the
backup but did not restore it to the new active installation. Forced reinstall and
upgrade each made settings, saves, states, ROMs and hidden files disappear from the
active app. Ten new assertions fail against the original script.

The fix copies portable user data into the staged replacement before moving the old
installation. The expanded PowerShell suite passes 45 assertions locally under
PowerShell 7.6.7. The POSIX suite passes 208 assertions across available shells.
Native Windows PowerShell 5.1/7 evidence is required from the installer CI matrix.

## Repeatable published-download qualification

Manually dispatch `Installer scripts` with `qualification_tag=prerelease` and
`qualification_sha=304867bd48eb430a33b26dca22fd38e160123591` on the qualification branch.
The optional jobs install a real RetroArch frontend and exercise published core,
app and both selections, no-TTY operation, dry-run invariance, normal updates,
forced updates, public-ROM launch and user-data preservation in isolated profiles.
They retain exact OS, candidate, outcomes and command logs as artifacts. A changed
rolling-release source SHA fails the run instead of silently qualifying another build.

The matrix covers Windows PowerShell 5.1 and 7, Apple Silicon macOS, Ubuntu 22.04
x86_64 and Ubuntu 24.04 ARM64. Local supplemental Linux tests use a fresh Ubuntu
22.04.5 ARM64 VM with no host-directory mounts.

## Remaining distribution work

- Complete the published-download jobs and resolve demonstrated failures before readiness.
- Obtain clean Intel Mac install/upgrade evidence; Rosetta coverage is recorded separately.
- Windows signing remains an external service dependency; report unsigned assets honestly.
- Final-tag filenames, checksums and package-manager manifests can only be verified against
  the actual final-tag artifacts after tagging. Do not submit templates with placeholder
  source hashes or nonexistent URLs. Homebrew/winget/Scoop/Flatpak submission and external
  acceptance remain post-publication work, distinct from template syntax validation.
- Keep #839 and #840 open until their respective remaining acceptance evidence exists.
