#!/usr/bin/env pwsh
<#
.SYNOPSIS
Check the Authenticode signatures of Windows PE files (issue #1150).

.DESCRIPTION
  verify-signatures.ps1 -Expect signed|unsigned|report [-Path <file>...] [-Folder <dir>...] [-Recurse]

Every .exe and .dll named by -Path, found in a -Folder (its top level, or the whole tree with
-Recurse), or packed in a .zip given to -Path (expanded to a temporary directory and always
walked recursively) gets one row: file, signature status, signer, timestamp authority.  The
table goes to the console and to the job summary when GITHUB_STEP_SUMMARY is set.

  -Expect signed    every PE file must verify as Valid under Windows' Authenticode policy (a
                    chain to a trusted root, the same check as `signtool verify /pa`) AND carry
                    a countersignature timestamp.
  -Expect unsigned  no PE file may carry a signature (a run that was not meant to sign).
  -Expect report    print only; never fails.

Exit 1 when the expectation is not met, with one ::error:: line per offending file.  Used by
.github/actions/windows-sign right after signing, by app-package.yml on the final packages, and
by its smoke jobs on the DOWNLOADED artifacts (an in-job check alone proves nothing about what
was uploaded).  Any CPU: WinVerifyTrust reads the image, it never runs it, so an x64 host
checks the ARM64 packages too.
#>
[CmdletBinding()]
param(
  [string[]]$Path = @(),
  [string[]]$Folder = @(),
  [switch]$Recurse,
  [ValidateSet('signed', 'unsigned', 'report')][string]$Expect = 'report',
  [string]$Title = 'Authenticode signatures'
)
$ErrorActionPreference = 'Stop'

function Get-PeFiles([string]$Dir, [bool]$Deep) {
  $items = if ($Deep) { Get-ChildItem -LiteralPath $Dir -File -Recurse } else { Get-ChildItem -LiteralPath $Dir -File }
  $items | Where-Object { $_.Extension -in '.exe', '.dll' } | Sort-Object FullName
}
function Add-Rows([System.Collections.Generic.List[object]]$List, [string]$Dir, [bool]$Deep, [string]$Prefix) {
  foreach ($i in (Get-PeFiles $Dir $Deep)) {
    $rel = [System.IO.Path]::GetRelativePath($Dir, $i.FullName).Replace('\', '/')
    $List.Add([pscustomobject]@{ File = $i.FullName; Label = $Prefix + $rel })
  }
}
function Get-ShortSubject([System.Security.Cryptography.X509Certificates.X509Certificate2]$Cert) {
  if (-not $Cert) { return '' }
  return (($Cert.Subject -split ',')[0] -replace '^\s*CN=', '').Trim()
}

$tmp = Join-Path ([System.IO.Path]::GetTempPath()) ('vj-verify-' + [System.IO.Path]::GetRandomFileName())
New-Item -ItemType Directory -Path $tmp | Out-Null
try {
  $targets = [System.Collections.Generic.List[object]]::new()
  foreach ($p in $Path) {
    if ([string]::IsNullOrWhiteSpace($p)) { continue }
    $full = (Resolve-Path -LiteralPath $p).Path
    if ($full -like '*.zip') {
      $name = [System.IO.Path]::GetFileName($full)
      $d = Join-Path $tmp ([System.IO.Path]::GetRandomFileName())
      Expand-Archive -LiteralPath $full -DestinationPath $d
      Add-Rows $targets $d $true "$name!"
    } else {
      $targets.Add([pscustomobject]@{ File = $full; Label = [System.IO.Path]::GetFileName($full) })
    }
  }
  foreach ($f in $Folder) {
    if ([string]::IsNullOrWhiteSpace($f)) { continue }
    $full = (Resolve-Path -LiteralPath $f).Path
    Add-Rows $targets $full ([bool]$Recurse) ''
  }
  if ($targets.Count -eq 0) {
    Write-Host "::error::verify-signatures: no PE files found (paths: $($Path -join ', '); folders: $($Folder -join ', '))"
    exit 1
  }

  $rows = @("| File | Status | Signer | Timestamp authority | Result |", "|---|---|---|---|---|")
  $failures = @()
  $signedCount = 0
  foreach ($t in $targets) {
    $sig = Get-AuthenticodeSignature -LiteralPath $t.File
    $status = $sig.Status.ToString()
    $signer = Get-ShortSubject $sig.SignerCertificate
    $tsa = Get-ShortSubject $sig.TimeStamperCertificate
    if ($status -ne 'NotSigned') { $signedCount++ }
    $problem = ''
    switch ($Expect) {
      'signed' {
        if ($status -ne 'Valid') { $problem = "status $status" + $(if ($sig.StatusMessage) { " ($($sig.StatusMessage.Trim()))" } else { '' }) }
        elseif (-not $sig.TimeStamperCertificate) { $problem = 'signed but NOT timestamped' }
      }
      'unsigned' {
        if ($status -ne 'NotSigned') { $problem = "unexpected signature ($status, $signer)" }
      }
    }
    $result = if ($problem) { "❌ $problem" } else { '✅' }
    if ($problem) { $failures += "$($t.Label): $problem" }
    $rows += "| ``$($t.Label)`` | $status | $signer | $tsa | $result |"
  }
  $summary = "$($targets.Count) PE files, $signedCount with a signature; expectation: $Expect; " +
    $(if ($failures.Count) { "**$($failures.Count) do not meet it**" } else { 'all meet it' }) + '.'
  $text = @("### $Title", '') + $rows + @('', $summary)
  $text | ForEach-Object { Write-Host $_ }
  if ($env:GITHUB_STEP_SUMMARY) { ($text + @('')) | Add-Content -Path $env:GITHUB_STEP_SUMMARY }
  foreach ($m in $failures) { Write-Host "::error title=Authenticode::$m" }
  if ($failures.Count) { exit 1 }
  exit 0
} finally {
  Remove-Item -LiteralPath $tmp -Recurse -Force -ErrorAction SilentlyContinue
}
