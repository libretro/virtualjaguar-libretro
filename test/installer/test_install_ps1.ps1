<#
  Hermetic tests for tools/install.ps1 (#840).  Windows PowerShell 5.1 and PowerShell 7.

    pwsh       -NoProfile -File test/installer/test_install_ps1.ps1
    powershell -NoProfile -File test/installer/test_install_ps1.ps1     (Windows)

  Nothing touches the real system: LOCALAPPDATA/APPDATA point into a temp dir, RetroArch
  is a fake PE stub (VJ_INSTALL_RETROARCH), and the "release" is a directory tree read
  through file:// (VJ_INSTALL_BASE_URL).  VJ_INSTALL_ALLOW_ANY_OS lets -DryRun and the
  temp-dir installs run on macOS/Linux too.
#>
$ErrorActionPreference = 'Stop'
$here   = Split-Path -Parent $MyInvocation.MyCommand.Path
$script = Join-Path (Join-Path $here '..\..\tools') 'install.ps1'
$script = (Resolve-Path $script).Path
$T      = Join-Path ([IO.Path]::GetTempPath()) ('vjps-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $T | Out-Null
$hostExe = (Get-Process -Id $PID).Path

$script:Pass = 0; $script:Fail = 0
function Pass([string]$n) { $script:Pass++; Write-Host "  ok   $n" }
function Fail([string]$n, [string]$detail) { $script:Fail++; Write-Host "  FAIL $n"; if ($detail) { ($detail -split "`n") | ForEach-Object { Write-Host "         | $_" } } }
function Check([string]$n, [bool]$cond, [string]$detail) { if ($cond) { Pass $n } else { Fail $n $detail } }

# --- fixtures --------------------------------------------------------------
function New-PeStub([string]$Path, [string]$Machine, [string]$Marker) {
  # MZ header, e_lfanew = 0x80, "PE\0\0", Machine; padded past the 100 KB "is this a core" floor
  $b = New-Object byte[] 131072
  $b[0] = 0x4D; $b[1] = 0x5A; $b[0x3C] = 0x80
  $b[0x80] = 0x50; $b[0x81] = 0x45
  switch ($Machine) { 'x64' { $b[0x84] = 0x64; $b[0x85] = 0x86 } 'x86' { $b[0x84] = 0x4C; $b[0x85] = 0x01 } }
  $m = [Text.Encoding]::ASCII.GetBytes($Marker)
  [Array]::Copy($m, 0, $b, 0x200, $m.Length)
  [IO.File]::WriteAllBytes($Path, $b)
}

function Write-Sums([string]$Dir) {
  $lines = foreach ($f in (Get-ChildItem -LiteralPath $Dir -File | Where-Object { $_.Name -ne 'SHA256SUMS.txt' } | Sort-Object Name)) {
    '{0}  {1}' -f (Get-FileHash -LiteralPath $f.FullName -Algorithm SHA256).Hash.ToLower(), $f.Name
  }
  Set-Content -LiteralPath (Join-Path $Dir 'SHA256SUMS.txt') -Value $lines -Encoding ASCII
}

function New-Release([string]$Dir, [string]$AppVer, [string]$Marker) {
  New-Item -ItemType Directory -Force -Path $Dir | Out-Null
  New-PeStub (Join-Path $Dir 'virtualjaguar_libretro-windows-x86_64.dll') 'x64' $Marker
  New-PeStub (Join-Path $Dir 'virtualjaguar_libretro-windows-i686.dll') 'x86' $Marker
  if ($AppVer) {
    $w = Join-Path $T ('zipwork-' + [Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Force -Path (Join-Path $w 'VirtualJaguar') | Out-Null
    Set-Content -LiteralPath (Join-Path $w 'VirtualJaguar/virtualjaguar.exe') -Value "exe $Marker"
    Set-Content -LiteralPath (Join-Path $w 'VirtualJaguar/README.txt') -Value $Marker
    Compress-Archive -Path (Join-Path $w 'VirtualJaguar') -DestinationPath (Join-Path $Dir "VirtualJaguar-$AppVer-windows-x64.zip") -Force
    Set-Content -LiteralPath (Join-Path $Dir "VirtualJaguar-$AppVer-windows-x64-setup.exe") -Value "setup $Marker"
    Remove-Item -LiteralPath $w -Recurse -Force
  }
  Write-Sums $Dir
}

$Old = Join-Path $T 'old'; $New = Join-Path $T 'new'
New-Release "$Old/latest/download" '' 'old-core'
New-Release "$New/latest/download" '4.0.0' 'new-latest'
New-Release "$New/download/v4.0.0" '4.0.0' 'new-latest'
New-Release "$New/download/nightly" 'nightly' 'nightly-one'

$RA = Join-Path $T 'ra'
New-Item -ItemType Directory -Force -Path (Join-Path $RA 'cores') | Out-Null
New-PeStub (Join-Path $RA 'retroarch.exe') 'x64' 'fake-ra'
Set-Content -LiteralPath (Join-Path $RA 'retroarch.cfg') -Value 'libretro_directory = ":\cores"'

# --- runner ----------------------------------------------------------------
function Url([string]$p) {
  $full = [IO.Path]::GetFullPath($p)
  # [Uri] reads a bare '/unix/path' as relative on macOS/Linux; on Windows it yields file:///C:/...
  if ($full.StartsWith('/')) { return 'file://' + $full }
  return ([Uri]$full).AbsoluteUri
}
function Invoke-Vj([string]$Base, [string[]]$ScriptArgs) {
  $env:VJ_INSTALL_BASE_URL = Url $Base
  $env:VJ_INSTALL_RETROARCH = (Join-Path $RA 'retroarch.exe')
  $env:VJ_INSTALL_NO_TTY = '1'
  $env:VJ_INSTALL_ALLOW_ANY_OS = '1'
  $env:LOCALAPPDATA = Join-Path $T 'localappdata'
  $env:APPDATA = Join-Path $T 'appdata'
  # Windows PowerShell 5.1 turns the first stderr line of a native process into a
  # terminating error under 'Stop'; the child's own exit code is what we assert on.
  $eap = $ErrorActionPreference; $ErrorActionPreference = 'Continue'
  $out = & $hostExe -NoProfile -ExecutionPolicy Bypass -File $script @ScriptArgs 2>&1 | Out-String
  $ErrorActionPreference = $eap
  return @{ Out = $out; Rc = $LASTEXITCODE }
}
function Snapshot([string[]]$Dirs) {
  $o = foreach ($d in $Dirs) { if (Test-Path $d) { Get-ChildItem -LiteralPath $d -Recurse -Force | ForEach-Object { $_.FullName } } }
  return (($o | Sort-Object) -join "`n")
}

Write-Host "host: $hostExe  (PowerShell $($PSVersionTable.PSVersion))"
$LAD = Join-Path $T 'localappdata/VirtualJaguar'
$AD = Join-Path $T 'apps'; $CD = Join-Path $RA 'cores'

# --- no app published (today's releases) ------------------------------------
$r = Invoke-Vj $Old @('-What', 'app', '-DryRun', '-AppDest', $AD)
Check 'app-only on a release with no app: exit 3' ($r.Rc -eq 3) $r.Out
Check 'no-app message is friendly' ($r.Out -like '*No app build is published for this channel yet*') $r.Out
$r = Invoke-Vj $Old @('-What', 'app', '-Channel', 'v9.9.9', '-DryRun', '-AppDest', $AD)
Check 'missing release tag degrades to the same message' ($r.Rc -eq 3 -and $r.Out -like '*No app build is published*') $r.Out
$r = Invoke-Vj $Old @('-What', 'both', '-DryRun', '-CoreDir', $CD, '-AppDest', $AD)
Check 'both with no app published still succeeds' ($r.Rc -eq 0) $r.Out
Check 'both: core planned (x86_64 from the PE header)' ($r.Out -like '*would install virtualjaguar_libretro-windows-x86_64.dll*') $r.Out

# --- legacy-style default: core only, dry run changes nothing -----------------
$s0 = Snapshot @($LAD, $AD, $CD)
$r = Invoke-Vj $Old @('-Yes', '-DryRun')
Check 'default run (cfg ":\cores" resolved against retroarch.exe) plans the core' ($r.Rc -eq 0 -and $r.Out -like "*$((Join-Path $RA 'cores'))*") $r.Out
Check 'default run does not mention the app' ($r.Out -notlike '*Virtual Jaguar app*') $r.Out
Check 'dry-run changed nothing' ($s0 -eq (Snapshot @($LAD, $AD, $CD))) ''

# --- app plans ----------------------------------------------------------------
$r = Invoke-Vj $New @('-What', 'app', '-Channel', 'nightly', '-DryRun', '-AppDest', $AD)
Check 'nightly app dry-run exit 0' ($r.Rc -eq 0) $r.Out
Check 'plan names the nightly zip' ($r.Out -like '*VirtualJaguar-nightly-windows-x64.zip*') $r.Out
Check 'plan mentions the Start-menu shortcut' ($r.Out -like '*Start-menu shortcut*') $r.Out
$r = Invoke-Vj $New @('-What', 'app', '-DryRun', '-AppDest', $AD)
Check 'latest resolves the version from SHA256SUMS' ($r.Out -like '*(version 4.0.0)*') $r.Out
$r = Invoke-Vj $New @('-What', 'app', '-DryRun', '-AppFormat', 'setup', '-Channel', 'v4.0.0')
Check 'setup format plans the silent installer' ($r.Out -like '*setup.exe /VERYSILENT /NORESTART /SUPPRESSMSGBOXES*' -and $r.Out -like '*windows-x64-setup.exe*') $r.Out
Check 'dry-run (all of the above) changed nothing' ($s0 -eq (Snapshot @($LAD, $AD, $CD))) ''

# --- real install into temp dirs, update, force -------------------------------
$r = Invoke-Vj $New @('-What', 'app', '-Channel', 'nightly', '-Yes', '-AppDest', $AD, '-NoShortcut')
Check 'app install exit 0' ($r.Rc -eq 0) $r.Out
Check 'exe installed' (Test-Path (Join-Path $AD 'virtualjaguar.exe')) $r.Out
Check 'SHA-256 verified' ($r.Out -like '*SHA-256 matches SHA256SUMS.txt*') $r.Out
Check 'receipt written' (Test-Path (Join-Path $T 'localappdata/VirtualJaguar/install-receipt.txt')) ''
$r = Invoke-Vj $New @('-What', 'app', '-Channel', 'nightly', '-Yes', '-AppDest', $AD, '-NoShortcut')
Check 'second run: already up to date' ($r.Out -like '*already up to date*') $r.Out
$r = Invoke-Vj $New @('-What', 'app', '-Channel', 'nightly', '-Yes', '-Force', '-AppDest', $AD, '-NoShortcut')
Check '-Force reinstalls' ($r.Out -like '*reinstall*') $r.Out

$New2 = Join-Path $T 'new2'
Copy-Item -LiteralPath $New -Destination $New2 -Recurse
Remove-Item -LiteralPath "$New2/download/nightly" -Recurse -Force
New-Release "$New2/download/nightly" 'nightly' 'nightly-two'
$r = Invoke-Vj $New2 @('-What', 'app', '-Channel', 'nightly', '-Yes', '-AppDest', $AD, '-NoShortcut')
Check 'new nightly is an update' ($r.Out -like '*update:*') $r.Out
Check 'previous app kept aside' (Test-Path "$AD.bak") $r.Out
Check 'new exe in place' ((Get-Content -LiteralPath (Join-Path $AD 'virtualjaguar.exe')) -like '*nightly-two*') ''
Check 'no doubled folder (zip has one top folder)' (-not (Test-Path (Join-Path $AD 'VirtualJaguar'))) ''

# a nightly reports the project version (4.0.0) like a release: a release must still update it
$r = Invoke-Vj $New @('-What', 'app', '-Channel', 'v4.0.0', '-DryRun', '-AppDest', $AD)
Check 'nightly install is updated by the release of the same project version' ($r.Out -like '*update:*' -and $r.Out -notlike '*already up to date*') $r.Out

$r = Invoke-Vj $New @('-List', '-CoreDir', $CD, '-AppDest', $AD)
Check '-List shows the installed app' ($r.Out -like '*installed (portable)*') $r.Out

$r = Invoke-Vj $New @('-Yes', '-Channel', 'v4.0.0', '-DryRun', '-AppDest', $AD, '-CoreDir', $CD)
Check 'unattended default adds an app this script installed' ($r.Out -like '*Virtual Jaguar app*') $r.Out

# --- verification fails closed -----------------------------------------------
$Bad = Join-Path $T 'bad'
Copy-Item -LiteralPath $New -Destination $Bad -Recurse
Add-Content -LiteralPath "$Bad/download/nightly/VirtualJaguar-nightly-windows-x64.zip" -Value 'tampered'
$AD2 = Join-Path $T 'apps-bad'
$r = Invoke-Vj $Bad @('-What', 'app', '-Channel', 'nightly', '-Yes', '-AppDest', $AD2, '-NoShortcut')
Check 'tampered app is rejected' ($r.Rc -ne 0 -and -not (Test-Path $AD2)) $r.Out
Add-Content -LiteralPath "$Bad/latest/download/virtualjaguar_libretro-windows-x86_64.dll" -Value 'tampered'
$r = Invoke-Vj $Bad @('-What', 'core', '-Yes')
Check 'tampered core is rejected' ($r.Rc -eq 1 -and $r.Out -like '*SHA-256 mismatch*' -and @(Get-ChildItem $CD).Count -eq 0) $r.Out

# --- the core installs, into the cfg-resolved folder, with a backup -------------
$r = Invoke-Vj $New @('-What', 'core', '-Yes')
Check 'core install exit 0' ($r.Rc -eq 0) $r.Out
Check 'core dll in the folder retroarch.cfg points at' (Test-Path (Join-Path $CD 'virtualjaguar_libretro.dll')) $r.Out
$r = Invoke-Vj $New2 @('-What', 'core', '-Yes', '-Channel', 'nightly')
Check 'core update keeps a .bak' (@(Get-ChildItem $CD -Filter '*.bak-*').Count -eq 1) $r.Out

# --- a 32-bit RetroArch gets the 32-bit core --------------------------------
New-PeStub (Join-Path $RA 'retroarch.exe') 'x86' 'fake-ra32'
$r = Invoke-Vj $New @('-What', 'core', '-DryRun', '-Yes')
Check '32-bit RetroArch -> i686 core' ($r.Out -like '*would install virtualjaguar_libretro-windows-i686.dll*') $r.Out
New-PeStub (Join-Path $RA 'retroarch.exe') 'x64' 'fake-ra'

# --- both, for real ------------------------------------------------------------
$AD3 = Join-Path $T 'apps3'; $CD3 = Join-Path $T 'cores3'; New-Item -ItemType Directory -Force -Path $CD3 | Out-Null
$r = Invoke-Vj $New @('-What', 'both', '-Yes', '-Channel', 'v4.0.0', '-CoreDir', $CD3, '-AppDest', $AD3, '-NoShortcut')
Check 'both installs' ($r.Rc -eq 0 -and (Test-Path (Join-Path $CD3 'virtualjaguar_libretro.dll')) -and (Test-Path (Join-Path $AD3 'virtualjaguar.exe'))) $r.Out

# --- Start-menu shortcut (Windows only: WScript.Shell) ---------------------------
if ($env:OS -eq 'Windows_NT') {
  $sm = Join-Path $T 'appdata\Microsoft\Windows\Start Menu\Programs'
  New-Item -ItemType Directory -Force -Path $sm | Out-Null
  $r = Invoke-Vj $New @('-What', 'app', '-Yes', '-Channel', 'v4.0.0', '-AppDest', (Join-Path $T 'apps-lnk'))
  Check 'Start-menu shortcut created' ((Test-Path (Join-Path $sm 'Virtual Jaguar.lnk')) -and $r.Rc -eq 0) $r.Out
}

# --- irm | iex shape: runs in-process, must not exit the host -------------------
$env:VJ_INSTALL_BASE_URL = Url $Old
$env:VJ_WHAT = 'app'; $env:VJ_DRYRUN = '1'; $env:VJ_APP_DEST = $AD
$ErrorActionPreference = 'Continue'
$out = & $hostExe -NoProfile -Command "iex (Get-Content -Raw '$script'); 'STILL-ALIVE'" 2>&1 | Out-String
$ErrorActionPreference = 'Stop'
Check 'iex form: env-var config works and the session survives' ($out -like '*No app build is published*' -and $out -like '*STILL-ALIVE*') $out
Remove-Item Env:VJ_WHAT, Env:VJ_DRYRUN, Env:VJ_APP_DEST

Remove-Item -LiteralPath $T -Recurse -Force -ErrorAction SilentlyContinue
Write-Host ''
Write-Host "$($script:Pass) passed, $($script:Fail) failed"
if ($script:Fail -gt 0) { exit 1 }
