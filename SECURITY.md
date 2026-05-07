# Security Policy

## Reporting a Vulnerability

To report a security vulnerability in the emulator code, please use
[GitHub's private vulnerability reporting](https://github.com/libretro/virtualjaguar-libretro/security/advisories/new).
Do **not** open a public issue for security bugs — private reporting ensures
the issue can be reviewed and fixed before details are disclosed.

We aim to acknowledge reports within **5 business days** and to provide an
initial assessment within **10 business days**.

---

## Build Provenance

All release binaries are built **transparently on GitHub Actions** from the
public source in this repository.  There are no private build steps,
post-processing scripts, or externally provided binaries involved.

| What | Where |
|------|-------|
| Build workflow | [`.github/workflows/release.yml`](.github/workflows/release.yml) |
| CI/PR builds | [`.github/workflows/c-cpp.yml`](.github/workflows/c-cpp.yml) |
| Actions run history | [Actions tab](https://github.com/libretro/virtualjaguar-libretro/actions) |

Each release tag's workflow run is permanently linked from the GitHub
Actions history.  You can inspect the exact commands used to compile,
strip, and package every artifact.

---

## Verifying Release Binaries

Every GitHub release includes a `SHA256SUMS.txt` file.  It lists the
SHA-256 hash of every artifact in the release.

### Linux / macOS (with coreutils or shasum)

```bash
# Download the binary and the checksum file, then:
sha256sum --check SHA256SUMS.txt

# If sha256sum is not available (macOS without coreutils):
shasum -a 256 --check SHA256SUMS.txt
```

### Windows (PowerShell)

```powershell
# Replace with the actual filename you downloaded:
$file   = "virtualjaguar_libretro-windows-x86_64.dll"
$sums   = Get-Content SHA256SUMS.txt
$expect = ($sums | Where-Object { $_ -match [regex]::Escape($file) }) -split '\s+' | Select-Object -First 1
$actual = (Get-FileHash $file -Algorithm SHA256).Hash.ToLower()
if ($actual -eq $expect) { "OK" } else { "MISMATCH — do not use this file" }
```

### Comparing against a specific release

The SHA-256 checksums are also embedded in the release body of every
tagged release on the
[Releases page](https://github.com/libretro/virtualjaguar-libretro/releases).
You can compare them without downloading `SHA256SUMS.txt` separately.

---

## Antivirus False Positives

Libretro cores are shared libraries (`.so` / `.dylib` / `.dll`) that
implement a fixed C API.  They contain no install routines, no network
code, and no privilege-escalation logic.

Heuristic antivirus engines occasionally flag emulator cores — and other
optimized native libraries — as suspicious because:

- The binaries are **not code-signed** by a commercial certificate authority.
- Optimized SIMD hot-paths (blitter, DSP) can superficially resemble
  shellcode patterns that some heuristics watch for.
- Broad heuristic labels (see below) are designed to err on the side of
  caution and produce false positives on unusual-but-benign binaries.

### Known false positive: `Trojan:Script/Wacatac.B!ml` (Microsoft Defender)

In 2025 a macOS x86\_64 build of this core was flagged by Microsoft
Defender as `Trojan:Script/Wacatac.B!ml`
([VirusTotal report](https://www.virustotal.com/gui/file/fa637480051c9afe41d6109d037d8307a488fff2ef4feb4f12899652906ce5af)).

This detection is a **confirmed false positive**.  `Wacatac.B!ml` is a
Windows PowerShell exploit family
([Microsoft WDSI entry](https://www.microsoft.com/en-us/wdsi/threats/malware-encyclopedia-description?Name=Trojan:Script/Wacatac.B!ml)).
A macOS Mach-O dynamic library cannot contain or execute PowerShell.
Microsoft's own documentation notes that heuristic `!ml` suffixes
(machine-learning classifiers) are prone to false positives on
legitimate software.

**What to do if you encounter an AV flag:**

1. Download `SHA256SUMS.txt` from the
   [same GitHub release](https://github.com/libretro/virtualjaguar-libretro/releases)
   and verify the hash of the binary matches (see above).
2. Check the [Actions run](https://github.com/libretro/virtualjaguar-libretro/actions)
   for the corresponding release tag to confirm the binary was produced by
   CI from the public source.
3. If the hash matches and CI was clean, the flag is a false positive.
   You can add an exclusion in your AV software for the specific file.
4. If the hash does **not** match the release, do not use that file and
   please open a [security advisory](https://github.com/libretro/virtualjaguar-libretro/security/advisories/new).

### Submitting false positive reports to AV vendors

If you have time, reporting false positives to AV vendors helps improve
their classifiers for everyone.  Most vendors provide an online submission
form:

| Vendor | Submission URL |
|--------|---------------|
| Microsoft | https://www.microsoft.com/en-us/wdsi/filesubmission |
| VirusTotal | https://www.virustotal.com/gui/home/upload |
| Malwarebytes | https://forums.malwarebytes.com/forum/122-false-positives/ |
