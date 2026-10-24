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
Native Windows PowerShell 5.1/7 installer CI also passed in #1059 and #1065.

## Repeatable published-download qualification

Manually dispatch `Installer scripts` with `qualification_tag=prerelease` and
`qualification_sha=304867bd48eb430a33b26dca22fd38e160123591` on the qualification branch.
The optional jobs install a real RetroArch frontend and exercise published core,
app and both selections, no-TTY operation, dry-run invariance, normal updates,
forced updates, public-ROM launch and user-data preservation in isolated profiles.
Real upgrade tests first install published v3.9.1, then require replacement by the
candidate (including changed payload hashes). Installed cores must load and run
public content in RetroArch, with nonzero exit or failed load treated as fatal.
The tag is resolved to a commit before and after testing (release metadata may
name a branch rather than a SHA). The resolved SHA and asset fingerprint must stay
unchanged. Six provenance tests cover valid branch-target metadata, a wrong or moved
tag, and replacement of the source or assets.

They retain exact OS, candidate, outcomes and command logs as artifacts. A changed
rolling-release source SHA fails the run instead of silently qualifying another build.

The matrix covers Windows PowerShell 5.1 and 7, Apple Silicon and Intel macOS, Ubuntu 22.04
x86_64 and Ubuntu 24.04 ARM64. Local supplemental Linux tests use a fresh Ubuntu
22.04.5 ARM64 VM with no host-directory mounts.

## Published installer evidence completed locally

The repeatable published-package runner passed all 25 recorded command checks on the
Apple Silicon Mac (macOS 26.6.2, isolated profile), including real core/app/both
installation, normal and forced update, before/after dry-run byte comparisons,
post-update launches and user-data sentinels. All bundled Mach-O files are universal
and both slices have minimum macOS 11.0.

A fresh Ubuntu 22.04.5 ARM64 VM (glibc 2.35, native virtualization, no host mounts)
passed all 19 Linux command checks for the published AppImage/core installer flows.
Those initial normal-update invocations detected an already-current installation;
they are not evidence of a real version-to-version upgrade.
These are actual published downloads, not hermetic fixture success. The evidence
pins GitHub release id `376361064` and the exact source SHA above.

Initial hosted Windows PowerShell 5.1/7 and Mac/Linux published-download tests
passed install/already-current/forced-update paths. Strengthened version-to-version
qualification is pending fresh hosted results; earlier passes do not substitute for it.

## Strengthened real-upgrade evidence

After review identified the already-current path, the strengthened runner was
executed locally on the Apple Silicon Mac. It installed published **v3.9.1** for
each core/app/both selection, upgraded normally to the candidate, required changed
payload hashes, then forced an update. All checks passed, including fatal RetroArch
core-load/public-content runs, post-update app hashes, data sentinels and the final
unchanged-release check. macOS RetroArch uses OpenGL: the local Cocoa frontend's
null-video path crashes, while the OpenGL path runs and exits successfully.

Hosted Linux qualification passed after correcting the Ubuntu 24.04 audio package
name and authenticating release metadata requests. An earlier Intel Mac attempt
failed with an Apple CloudKit error 68; the strengthened clean Intel run subsequently
passed. No signing check was skipped to claim success.

The fresh Ubuntu 22.04.5 ARM64 VM also passed all 21 strengthened checks: actual
v3.9.1 upgrades of core/app/both, changed payload hashes, RetroArch core runs,
post-update public-ROM app launches, user-data preservation, dry-run invariance and
final release provenance. Its distro RetroArch 1.7.3 crashes in the null-video path;
the same core runs successfully with software OpenGL under Xvfb. Linux qualification
therefore uses the real GL frontend under a virtual display, with failure still fatal.

The live website's POSIX and PowerShell copy/paste URLs and explicit `both` examples
match the scripts. Both scripts expose explicit channel selection; qualification
passes named baseline/candidate channels rather than relying on `latest`.

## Completed hosted qualification

All required platform lanes now have successful actual published-download evidence:

| Environment | Successful job | Workflow run |
|---|---|---|
| macOS Intel, macOS 15.7.9 | 119032021639 | [39541919634](https://github.com/libretro/virtualjaguar-libretro/actions/runs/39541919634) |
| macOS Apple Silicon | 119032021912 | [39541919634](https://github.com/libretro/virtualjaguar-libretro/actions/runs/39541919634) |
| Ubuntu 22.04 x86_64 | 119033827891 | [39542703457](https://github.com/libretro/virtualjaguar-libretro/actions/runs/39542703457) |
| Ubuntu 24.04 ARM64 | 119033827546 | [39542703457](https://github.com/libretro/virtualjaguar-libretro/actions/runs/39542703457) |
| Windows Server 2025 x64, PowerShell 7 | 119628322467 | [39719144944](https://github.com/libretro/virtualjaguar-libretro/actions/runs/39719144944) |
| Windows Server 2025 x64, PowerShell 5.1 | 119628322649 | [39719144944](https://github.com/libretro/virtualjaguar-libretro/actions/runs/39719144944) |

These lanes require genuine v3.9.1-to-candidate upgrades for core/app/both, changed
payload hashes, actual installed-core execution in RetroArch, post-update public-ROM
app launches, dry-run invariance and preserved user data. Windows additionally
verifies the downloaded setup checksum, installs, launches, updates and uninstalls
it while retaining user data. Both Windows lanes passed all 26 command checks.
The setup SHA-256 is `b671e875038a6b8044b7af17fa3beb952b309f988f9ba5556f18a2b6a3eb1303`;
Authenticode status is **NotSigned**. The Windows runtime lane uses Server 2025,
not a claim of testing every supported Windows desktop version.

The initial Windows qualification failed because the Chocolatey launcher returned
before the frontend log existed. #1065 invokes the real installed RetroArch executable
directly; both fresh Windows lanes above pass with this fix. #1065 also completed
38 successful PR checks and two legitimate configured skips before merging into develop.

## Remaining distribution work

- Integrate this scoped installer backport into master after its fresh applicable CI
  and reviews pass. No final v3.10.0 tag has been created by this qualification work.
- Windows signing remains an external service dependency; report unsigned assets honestly.
- Final-tag filenames, checksums and package-manager manifests can only be verified against
  the actual final-tag artifacts after tagging. Do not submit templates with placeholder
  source hashes or nonexistent URLs. Homebrew/winget/Scoop/Flatpak submission and external
  acceptance remain post-publication work, distinct from template syntax validation.
- Keep #839 open for remaining distribution acceptance. #840's install/upgrade evidence
  is complete; its installer fix still requires release integration.
