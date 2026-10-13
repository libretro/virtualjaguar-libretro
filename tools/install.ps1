<#
.SYNOPSIS
  Virtual Jaguar -- installer / updater for the RetroArch core and the app (Windows).

.DESCRIPTION
  PowerShell twin of tools/install.sh.  Works in Windows PowerShell 5.1 and
  PowerShell 7.  https://github.com/libretro/virtualjaguar-libretro

  Run it straight from the web (read it first if you like):

    irm https://raw.githubusercontent.com/libretro/virtualjaguar-libretro/develop/tools/install.ps1 | iex

  `iex` cannot pass arguments, so configure that form with environment variables
  (VJ_WHAT, VJ_CHANNEL, VJ_YES, VJ_DRYRUN, VJ_CORE_DIR, VJ_APP_DEST,
  VJ_APP_FORMAT, VJ_FORCE) -- or hand the script block its parameters:

    & ([scriptblock]::Create((irm https://raw.githubusercontent.com/libretro/virtualjaguar-libretro/develop/tools/install.ps1))) -What both -DryRun

  What it does
    core  finds RetroArch (registry, common folders, retroarch.cfg), reads the
          word size out of retroarch.exe (the core must match RetroArch, not the
          OS), downloads the right virtualjaguar_libretro-windows-*.dll, checks
          it against SHA256SUMS.txt and drops it in RetroArch's cores folder,
          keeping the old one as a .bak.
    app   the portable zip into %LOCALAPPDATA%\Programs\VirtualJaguar with a
          Start-menu shortcut, or -AppFormat setup to run the installer (silently).
    both  both of the above.
    Updates: whichever is installed is replaced when the channel has a newer
    build (nightly: when the published hash differs).  -Force reinstalls.

  Nothing is written under -DryRun, and nothing needs administrator rights
  unless RetroArch lives somewhere protected (then re-run elevated, or pass
  -CoreDir).  Every download is verified against the release's SHA256SUMS.txt.

.PARAMETER What
  core | app | both.  Default: ask; with no terminal, the core, plus the app if
  this script installed it before.
.PARAMETER Channel
  latest (default) | nightly | a tag such as v4.0.0.
.PARAMETER CoreDir
  Install the core here instead of discovering RetroArch.
.PARAMETER AppDest
  Where the portable app goes (default %LOCALAPPDATA%\Programs\VirtualJaguar).
.PARAMETER AppFormat
  portable (zip, default) | setup (the installer .exe).
.PARAMETER SetupArgs
  Arguments for the setup .exe.  Default: /VERYSILENT /NORESTART
.PARAMETER Yes
  No prompts, take every default.
.PARAMETER DryRun
  Print the plan; change nothing.
.PARAMETER List
  Show what was found; install nothing.
.PARAMETER Force
  Reinstall even if already up to date.
.PARAMETER NoShortcut
  Do not create the Start-menu shortcut.
#>
[CmdletBinding()]
param(
  [string]$What       = $env:VJ_WHAT,
  [string]$Channel    = $(if ($env:VJ_CHANNEL) { $env:VJ_CHANNEL } else { 'latest' }),
  [string]$CoreDir    = $env:VJ_CORE_DIR,
  [string]$AppDest    = $env:VJ_APP_DEST,
  [string]$AppFormat  = $env:VJ_APP_FORMAT,
  [string]$SetupArgs  = $(if ($env:VJ_SETUP_ARGS) { $env:VJ_SETUP_ARGS } else { '/VERYSILENT /NORESTART' }),
  [switch]$Yes        = [bool]$env:VJ_YES,
  [switch]$DryRun     = [bool]$env:VJ_DRYRUN,
  [switch]$List,
  [switch]$Force      = [bool]$env:VJ_FORCE,
  [switch]$NoShortcut
)

$script:Repo      = 'libretro/virtualjaguar-libretro'
$script:CoreBase  = 'virtualjaguar_libretro'
$script:AppExe    = 'virtualjaguar.exe'
$script:AppName   = 'Virtual Jaguar'

# ---------------------------------------------------------------------------
# output
# ---------------------------------------------------------------------------
function Write-Step($m) { Write-Host ''; Write-Host "==> $m" -ForegroundColor Green }
function Write-Ok($m)   { Write-Host "  + $m" }
function Write-Note($m) { Write-Host "  - $m" -ForegroundColor DarkGray }
function Write-Warn2($m){ Write-Host "  ! $m" -ForegroundColor Yellow }

# A failure we mean: carries an exit code, no stack trace.
function Stop-Install([string]$Message, [int]$Code = 1) {
  $e = New-Object System.Exception($Message)
  $e.Data['VjExit'] = $Code
  throw $e
}

# ---------------------------------------------------------------------------
# environment
# ---------------------------------------------------------------------------
function Test-WindowsHost {
  # $IsWindows does not exist in Windows PowerShell 5.1; $env:OS does everywhere.
  # VJ_INSTALL_ALLOW_ANY_OS lets the test suite exercise -DryRun on macOS/Linux.
  return (($env:OS -eq 'Windows_NT') -or [bool]$env:VJ_INSTALL_ALLOW_ANY_OS)
}

function Get-LocalAppData {
  if ($env:LOCALAPPDATA) { return $env:LOCALAPPDATA }
  return (Join-Path $HOME 'AppData/Local')
}

function Test-Interactive {
  if ($Yes -or $env:VJ_INSTALL_NO_TTY) { return $false }
  try { if ([Console]::IsInputRedirected) { return $false } } catch { return $false }
  return [Environment]::UserInteractive
}

function Read-Choice([string]$Prompt, [string]$Default) {
  if (-not (Test-Interactive)) { return $Default }
  $a = Read-Host "$Prompt [$Default]"
  if ([string]::IsNullOrWhiteSpace($a)) { return $Default }
  return $a.Trim()
}

function Confirm-Step([string]$Prompt) {
  if (-not (Test-Interactive)) { return $true }
  $a = Read-Host "$Prompt [Y/n]"
  return -not ($a -match '^[nN]')
}

# ---------------------------------------------------------------------------
# release access
# ---------------------------------------------------------------------------
function Get-BaseUrl {
  $b = $env:VJ_INSTALL_BASE_URL
  if (-not $b) { $b = "https://github.com/$($script:Repo)/releases" }
  return $b.TrimEnd('/')
}

function Get-ReleaseUrl([string]$Chan, [string]$File) {
  $b = Get-BaseUrl
  switch ($Chan) {
    'latest'  { return "$b/latest/download/$File" }
    'nightly' { return "$b/download/nightly/$File" }
    default   { return "$b/download/$Chan/$File" }
  }
}

function Receive-File([string]$Url, [string]$Dest) {
  if ($Url -like 'file://*') {
    # PowerShell 7's Invoke-WebRequest rejects file://; a local release tree is a copy.
    Copy-Item -LiteralPath ([Uri]$Url).LocalPath -Destination $Dest -Force -ErrorAction Stop
    return
  }
  $old = $ProgressPreference
  $ProgressPreference = 'SilentlyContinue'      # the 5.1 progress bar makes big downloads crawl
  try {
    for ($i = 1; $i -le 3; $i++) {
      try { Invoke-WebRequest -Uri $Url -OutFile $Dest -UseBasicParsing -ErrorAction Stop; return }
      catch {
        $code = 0
        try { $code = [int]$_.Exception.Response.StatusCode } catch { $code = 0 }
        if ($code -eq 404 -or $i -eq 3) { throw }
        Start-Sleep -Seconds 2
      }
    }
  } finally { $ProgressPreference = $old }
}

# SHA256SUMS.txt -> hashtable name -> lowercase hash.  $null when the channel has none.
function Get-Sums([string]$Work) {
  if ($script:Sums) { return $script:Sums }
  $f = Join-Path $Work 'SHA256SUMS.txt'
  try { Receive-File (Get-ReleaseUrl $Channel 'SHA256SUMS.txt') $f } catch { return $null }
  $t = @{}
  foreach ($line in (Get-Content -LiteralPath $f)) {
    if ($line -match '^([0-9a-fA-F]{64})\s+\*?(.+?)\s*$') { $t[$Matches[2]] = $Matches[1].ToLower() }
  }
  if ($t.Count -eq 0) { return $null }
  $script:Sums = $t
  return $t
}

function Test-FileHash([string]$Path, [string]$Name, [string]$Work) {
  # 'ok' | 'mismatch' | 'unlisted' | 'nosums'
  $s = Get-Sums $Work
  if (-not $s) { return 'nosums' }
  if (-not $s.ContainsKey($Name)) { return 'unlisted' }
  $got = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLower()
  if ($got -eq $s[$Name]) { return 'ok' }
  return "mismatch expected $($s[$Name]) got $got"
}

# ---------------------------------------------------------------------------
# PE inspection: the core must match RETROARCH, not the OS
# ---------------------------------------------------------------------------
function Get-PeArch([string]$Path) {
  if (-not $Path -or -not (Test-Path -LiteralPath $Path -PathType Leaf)) { return $null }
  $b = New-Object byte[] 4096
  $n = 0
  $fs = [IO.File]::OpenRead($Path)
  try { $n = $fs.Read($b, 0, 4096) } finally { $fs.Dispose() }
  if ($n -lt 0x40 -or $b[0] -ne 0x4D -or $b[1] -ne 0x5A) { return $null }
  $off = [BitConverter]::ToInt32($b, 0x3C)
  if ($off -lt 0 -or ($off + 6) -gt $n) { return $null }
  if ($b[$off] -ne 0x50 -or $b[$off + 1] -ne 0x45) { return $null }
  switch ([BitConverter]::ToUInt16($b, $off + 4)) {
    0x8664  { return 'x86_64' }
    0x014C  { return 'i686' }
    0xAA64  { return 'arm64' }
    default { return $null }
  }
}

# ---------------------------------------------------------------------------
# receipt: a CACHE (channel + hash).  Installed-ness comes from the machine.
# ---------------------------------------------------------------------------
function Get-ReceiptPath { return (Join-Path (Join-Path (Get-LocalAppData) 'VirtualJaguar') 'install-receipt.txt') }

function Get-Receipt {
  $r = @{}
  $p = Get-ReceiptPath
  if (Test-Path -LiteralPath $p) {
    foreach ($l in (Get-Content -LiteralPath $p)) { if ($l -match '^([^=]+)=(.*)$') { $r[$Matches[1]] = $Matches[2] } }
  }
  return $r
}

function Set-Receipt([hashtable]$Values) {
  $p = Get-ReceiptPath
  New-Item -ItemType Directory -Force -Path (Split-Path $p) | Out-Null
  $lines = @()
  foreach ($k in 'channel', 'kind', 'path', 'version', 'asset', 'sha256') { $lines += "$k=$($Values[$k])" }
  $lines += ('installed=' + (Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ'))
  Set-Content -LiteralPath $p -Value $lines -Encoding ASCII
}

# ---------------------------------------------------------------------------
# installed app detection
# ---------------------------------------------------------------------------
function Get-DefaultAppDest { return (Join-Path (Join-Path (Get-LocalAppData) 'Programs') 'VirtualJaguar') }

function Find-InstalledApp {
  # -> @{ Kind='portable'|'setup'; Path; Version } or $null
  $dest = $AppDest
  if (-not $dest) { $dest = Get-DefaultAppDest }
  $exe = Join-Path $dest $script:AppExe
  $rc = Get-Receipt
  $wantSetup = ($AppFormat -eq 'setup')

  if (-not $wantSetup -and (Test-Path -LiteralPath $exe)) {
    $v = ''
    if ($rc['path'] -eq $dest -and $rc['kind'] -eq 'portable') { $v = $rc['version'] }
    if (-not $v) { try { $v = (Get-Item -LiteralPath $exe).VersionInfo.ProductVersion } catch { $v = '' } }
    return @{ Kind = 'portable'; Path = $dest; Version = $v }
  }
  if (($wantSetup -or -not $AppFormat) -and $env:OS -eq 'Windows_NT') {
    $roots = @('HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\*',
               'HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\*',
               'HKLM:\Software\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\*')
    foreach ($r in $roots) {
      try {
        $hit = Get-ItemProperty -Path $r -ErrorAction SilentlyContinue |
          Where-Object { $_.DisplayName -like 'Virtual Jaguar*' } | Select-Object -First 1
        if ($hit) { return @{ Kind = 'setup'; Path = [string]$hit.InstallLocation; Version = [string]$hit.DisplayVersion } }
      } catch { }
    }
  }
  return $null
}

function Normalize-Version([string]$v) { if (-not $v) { return '' }; return ($v -replace '^v', '') -replace '-.*$', '' }

# ---------------------------------------------------------------------------
# the core
# ---------------------------------------------------------------------------
function Get-CfgValue([string]$Cfg, [string]$Key) {
  foreach ($l in (Get-Content -LiteralPath $Cfg -ErrorAction SilentlyContinue)) {
    if ($l -match "^\s*$Key\s*=\s*`"?([^`"]*)`"?\s*$") { return $Matches[1] }
  }
  return ''
}

function Find-RetroArchExes {
  $found = @()
  if ($env:VJ_INSTALL_RETROARCH) {
    if (Test-Path -LiteralPath $env:VJ_INSTALL_RETROARCH) { return @($env:VJ_INSTALL_RETROARCH) }
    return @()
  }
  $dirs = @('C:\RetroArch-Win64', 'C:\RetroArch', 'C:\Program Files\RetroArch', 'C:\Program Files (x86)\RetroArch',
            'C:\Program Files (x86)\Steam\steamapps\common\RetroArch',
            (Join-Path $HOME 'scoop\apps\retroarch\current'))
  if ($env:APPDATA)      { $dirs += (Join-Path $env:APPDATA 'RetroArch') }
  if ($env:LOCALAPPDATA) { $dirs += (Join-Path $env:LOCALAPPDATA 'RetroArch') }
  if ($env:OS -eq 'Windows_NT') {
    foreach ($r in 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\*',
                   'HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\*',
                   'HKLM:\Software\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\*') {
      try {
        Get-ItemProperty -Path $r -ErrorAction SilentlyContinue |
          Where-Object { $_.DisplayName -like 'RetroArch*' -and $_.InstallLocation } |
          ForEach-Object { $dirs += [string]$_.InstallLocation }
      } catch { }
    }
    try { $wg = Join-Path $env:LOCALAPPDATA 'Microsoft\WinGet\Packages'
          if (Test-Path $wg) { Get-ChildItem $wg -Directory -Filter 'Libretro.RetroArch*' | ForEach-Object { $dirs += $_.FullName } } } catch { }
  }
  foreach ($d in $dirs) {
    $exe = Join-Path $d 'retroarch.exe'
    if (Test-Path -LiteralPath $exe) { $found += $exe }
  }
  $cmd = Get-Command retroarch.exe -ErrorAction SilentlyContinue
  if ($cmd) { $found += $cmd.Source }
  return @($found | Select-Object -Unique)
}

function Get-CoreCandidates {
  $c = @()
  if ($CoreDir) {
    $exe = $null
    $ras = @(Find-RetroArchExes)
    if ($ras.Count -gt 0) { $exe = $ras[0] }
    return @(@{ Label = 'Chosen with -CoreDir'; Exe = $exe; Dir = $CoreDir })
  }
  foreach ($exe in (Find-RetroArchExes)) {
    $raDir = Split-Path $exe
    $dir = ''
    $cfg = Join-Path $raDir 'retroarch.cfg'
    if (Test-Path -LiteralPath $cfg) {
      $dir = Get-CfgValue $cfg 'libretro_directory'
      # ":\cores" means "relative to the folder retroarch.exe is in"
      if ($dir -like ':*') { $dir = Join-Path $raDir ($dir.Substring(1).TrimStart('\', '/')) }
      if ($dir -like '~*') { $dir = Join-Path $HOME $dir.Substring(1).TrimStart('\', '/') }
    }
    if (-not $dir -or -not (Test-Path -LiteralPath $dir)) { $dir = Join-Path $raDir 'cores' }
    if (Test-Path -LiteralPath $dir) {
      $c += @{ Label = "RetroArch ($raDir)"; Exe = $exe; Dir = $dir }
    }
  }
  # one entry per core dir
  $seen = @{}; $out = @()
  foreach ($e in $c) { if (-not $seen.ContainsKey($e.Dir)) { $seen[$e.Dir] = 1; $out += $e } }
  return $out
}

function Install-Core([string]$Work, [bool]$Soft) {
  Write-Step 'Looking for RetroArch and its core directory'
  $cands = @(Get-CoreCandidates)
  if ($cands.Count -eq 0) {
    if ($Soft) { Write-Note 'no RetroArch core directory found; skipping the core (the app is still updated).'; return 0 }
    Stop-Install "no RetroArch found.  If it is installed, its core folder is under Settings -> Directory -> Cores`n     in RetroArch.  Re-run with  -CoreDir <that folder>."
  }
  if ($List) {
    foreach ($e in $cands) { Write-Host "  $($e.Label)"; Write-Host "      cores:     $($e.Dir)"; Write-Host "      retroarch: $(if ($e.Exe) { $e.Exe } else { '(not found)' })" }
    return 0
  }
  $pick = $cands[0]
  if ($cands.Count -gt 1) {
    Write-Step 'More than one RetroArch found -- pick one'
    for ($i = 0; $i -lt $cands.Count; $i++) { Write-Host "  $($i + 1)) $($cands[$i].Label)"; Write-Host "     $($cands[$i].Dir)" }
    $n = [int](Read-Choice '  Number' '1')
    if ($n -lt 1 -or $n -gt $cands.Count) { Stop-Install "no such choice: $n" }
    $pick = $cands[$n - 1]
  }
  Write-Ok $pick.Label
  Write-Note "cores -> $($pick.Dir)"
  if (-not (Test-Path -LiteralPath $pick.Dir)) { Stop-Install "core directory does not exist: $($pick.Dir)" }

  Write-Step 'Working out which build you need'
  $arch = Get-PeArch $pick.Exe
  if (-not $arch) {
    foreach ($f in (Get-ChildItem -LiteralPath $pick.Dir -Filter '*_libretro.dll' -ErrorAction SilentlyContinue)) {
      $arch = Get-PeArch $f.FullName
      if ($arch) { Write-Note "read from $($f.Name)"; break }
    }
  }
  if (-not $arch) { Stop-Install "could not determine RetroArch's architecture (neither retroarch.exe nor an existing core could be read)." }
  Write-Ok "RetroArch is $arch"
  if ([Environment]::Is64BitOperatingSystem -and $arch -eq 'i686') {
    Write-Warn2 'this is 64-bit Windows but that RetroArch is 32-bit: using the 32-bit core (the core must match RetroArch).'
  }
  switch ($arch) {
    'x86_64' { $asset = "$($script:CoreBase)-windows-x86_64.dll" }
    'i686'   { $asset = "$($script:CoreBase)-windows-i686.dll" }
    default  { Stop-Install "no core build for $arch on Windows.  Builds exist for x86_64 and i686 -- please open an issue at https://github.com/$($script:Repo)/issues" }
  }
  Write-Ok "build: $asset"
  Write-Ok "channel: $Channel"

  $url = Get-ReleaseUrl $Channel $asset
  $dst = Join-Path $pick.Dir 'virtualjaguar_libretro.dll'
  Write-Step 'Downloading'
  Write-Note $url
  $tmp = Join-Path $Work 'core.dll'
  try { Receive-File $url $tmp } catch { Stop-Install "download failed.`n     That release may not ship a build named '$asset'.`n     What it does ship: https://github.com/$($script:Repo)/releases" }
  $size = (Get-Item -LiteralPath $tmp).Length
  if ($size -le 100000) { Stop-Install "downloaded $size bytes -- that is not a core." }
  Write-Ok ("{0} KB" -f [int]($size / 1024))

  Write-Step 'Checking it'
  $v = Test-FileHash $tmp $asset $Work
  if ($v -eq 'ok') { Write-Ok 'SHA-256 matches SHA256SUMS.txt' }
  elseif ($v -like 'mismatch*') { Stop-Install "SHA-256 mismatch for $asset -- nothing was changed.`n     $v`n     Retry in a minute (a nightly may have been replaced mid-download)." }
  else { Write-Warn2 'could not check SHA-256 (the channel has no SHA256SUMS.txt entry for this file).' }
  $dl = Get-PeArch $tmp
  if (-not $dl) { Stop-Install 'the downloaded file is not a Windows DLL this script can read.' }
  if ($dl -ne $arch) { Stop-Install "architecture mismatch -- nothing was changed.`n       RetroArch:  $arch`n       downloaded: $dl" }
  Write-Ok "matches RetroArch ($arch)"

  if ($DryRun) {
    Write-Step '--dry-run: nothing was changed (core)'
    Write-Host "  would install $asset"
    Write-Host "  into          $dst"
    return 0
  }
  if ((Test-Path -LiteralPath $dst) -and -not (Confirm-Step "  Replace the core already at $dst?")) { Stop-Install 'cancelled -- nothing changed.' }

  Write-Step 'Installing the core'
  $backup = ''
  try {
    if (Test-Path -LiteralPath $dst) {
      $backup = "$dst.bak-" + (Get-Date -Format 'yyyyMMdd-HHmmss')
      Copy-Item -LiteralPath $dst -Destination $backup -Force -ErrorAction Stop
      Write-Ok "backed up -> $(Split-Path $backup -Leaf)"
    }
    Copy-Item -LiteralPath $tmp -Destination $dst -Force -ErrorAction Stop
  } catch [System.UnauthorizedAccessException] {
    Stop-Install "no permission to write $($pick.Dir).`n     RetroArch is in a protected folder: re-run PowerShell as Administrator, or install the core elsewhere with -CoreDir."
  } catch {
    Stop-Install "could not write $dst : $($_.Exception.Message)  (is RetroArch running? close it and retry)"
  }
  Write-Ok "installed -> $dst"
  Write-Step 'Core installed'
  Write-Host "  frontend: $($pick.Label)"
  Write-Host "  core:     $dst"
  Write-Host "  channel:  $Channel"
  if ($backup) { Write-Host "  undo:     Copy-Item '$backup' '$dst' -Force" }
  Write-Host ''
  Write-Host '  Put Jaguar ROMs where RetroArch scans for them, then restart it so'
  Write-Host '  the new core is picked up.'
  Set-Content -LiteralPath (Join-Path $Work 'core_dir') -Value $pick.Dir
  return 0
}

# ---------------------------------------------------------------------------
# the app
# ---------------------------------------------------------------------------
function Show-AppStatus {
  Write-Step "$($script:AppName) app"
  $a = Find-InstalledApp
  if (-not $a) { Write-Note 'not installed'; return }
  $v = if ($a.Version) { $a.Version } else { 'unknown' }
  Write-Ok "installed ($($a.Kind)): $($a.Path)   version: $v"
  $rc = Get-Receipt
  if ($rc['channel']) { Write-Note "channel last installed from: $($rc['channel'])" }
}

function Write-NoAppBuild([string]$Why) {
  Write-Host ''
  Write-Host '  No app build is published for this channel yet.'
  Write-Host "  $Why"
  Write-Host '  The standalone app ships from v4.0.  To get the core now:  -What core'
  Write-Host '  Try the nightly channel:  -What app -Channel nightly'
}

function Install-App([string]$Work) {
  Write-Step "$($script:AppName) app"
  $arch = $env:PROCESSOR_ARCHITEW6432
  if (-not $arch) { $arch = $env:PROCESSOR_ARCHITECTURE }
  if (-not $arch -and -not (Test-WindowsHost)) { $arch = 'AMD64' }
  if ($arch -eq 'x86' -or -not [Environment]::Is64BitOperatingSystem) {
    Write-NoAppBuild 'The app is 64-bit only and this is a 32-bit Windows.'
    return 3
  }
  if ($arch -eq 'ARM64') { Write-Note 'ARM64 Windows: using the x64 build (it runs under emulation).' }

  $installed = Find-InstalledApp
  $format = $AppFormat
  if (-not $format) { if ($installed -and $installed.Kind -eq 'setup') { $format = 'setup' } else { $format = 'portable' } }
  if ($format -ne 'portable' -and $format -ne 'setup') { Stop-Install "-AppFormat takes portable or setup (got '$format')." }
  if ($installed) { Write-Ok ("installed now: {0}   version: {1}" -f $installed.Path, $(if ($installed.Version) { $installed.Version } else { 'unknown' })) }
  else { Write-Note 'no existing app install found' }

  $sums = Get-Sums $Work
  if (-not $sums) { Write-NoAppBuild "Could not read $(Get-ReleaseUrl $Channel 'SHA256SUMS.txt') -- that release does not exist (yet), or you are offline."; return 3 }
  $pat = if ($format -eq 'setup') { '^VirtualJaguar-(.+)-windows-x64-setup\.exe$' } else { '^VirtualJaguar-(.+)-windows-x64\.zip$' }
  $asset = $null; $ver = ''
  foreach ($k in ($sums.Keys | Sort-Object)) { if ($k -match $pat) { $asset = $k; $ver = $Matches[1]; break } }
  if (-not $asset) { Write-NoAppBuild "The '$Channel' release has no Windows $format app file."; return 3 }
  $hash = $sums[$asset]
  Write-Ok "channel $Channel ships $asset (version $ver)"

  $dest = $AppDest
  if (-not $dest) { $dest = Get-DefaultAppDest }
  $target = if ($format -eq 'setup') { 'the Virtual Jaguar installer (per the installer)' } else { $dest }

  $action = 'install'
  if ($installed) {
    $action = 'update'
    $rc = Get-Receipt
    if ($rc['path'] -eq $installed.Path -and $rc['sha256'] -eq $hash) { $action = 'current' }
    elseif ($ver -ne 'nightly' -and $installed.Version -and (Normalize-Version $installed.Version) -eq (Normalize-Version $ver)) { $action = 'current' }
    if ($Force -and $action -eq 'current') { $action = 'reinstall' }
  }
  switch ($action) {
    'current'   { Write-Ok "already up to date ($ver); nothing to do.  -Force reinstalls." }
    'update'    { Write-Ok ("update: {0} -> {1}" -f $(if ($installed.Version) { $installed.Version } else { 'unknown' }), $ver) }
    'install'   { Write-Ok 'fresh install' }
    'reinstall' { Write-Ok "reinstall of $ver (-Force)" }
  }

  if ($DryRun) {
    Write-Step '--dry-run: nothing was changed (app)'
    if ($action -eq 'current') { Write-Host "  app is up to date: $asset" }
    else {
      Write-Host "  would $action  $asset   (sha256 $($hash.Substring(0, 12))...)"
      Write-Host "  from          $(Get-ReleaseUrl $Channel $asset)"
      Write-Host "  into          $target"
      if ($format -eq 'portable' -and -not $NoShortcut) { Write-Host '  and           a Start-menu shortcut' }
      if ($format -eq 'setup') { Write-Host "  running       setup.exe $SetupArgs" }
    }
    return 0
  }
  if ($action -eq 'current') { return 0 }

  $file = Join-Path $Work $asset
  Write-Note (Get-ReleaseUrl $Channel $asset)
  try { Receive-File (Get-ReleaseUrl $Channel $asset) $file } catch { Stop-Install "download of $asset failed." }
  $v = Test-FileHash $file $asset $Work
  if ($v -eq 'ok') { Write-Ok 'SHA-256 matches SHA256SUMS.txt' }
  else { Stop-Install "could not verify $asset ($v) -- nothing was installed." }

  if ($action -ne 'install' -and -not (Confirm-Step "  Replace the app at $($installed.Path)?")) { Stop-Install 'cancelled -- nothing changed.' }
  if (Get-Process -Name 'virtualjaguar' -ErrorAction SilentlyContinue) { Stop-Install 'Virtual Jaguar is running: close it and re-run.' }

  if ($format -eq 'setup') {
    Write-Step 'Running the installer'
    $p = Start-Process -FilePath $file -ArgumentList $SetupArgs -Wait -PassThru
    if ($p.ExitCode -ne 0) { Stop-Install "the installer exited with code $($p.ExitCode)." }
    Set-Receipt @{ channel = $Channel; kind = 'setup'; path = $dest; version = $ver; asset = $asset; sha256 = $hash }
    Write-Ok 'installed'
  } else {
    Write-Step 'Installing the app'
    $x = Join-Path $Work 'app-x'
    New-Item -ItemType Directory -Force -Path $x | Out-Null
    Expand-Archive -LiteralPath $file -DestinationPath $x -Force
    $exe = Get-ChildItem -LiteralPath $x -Recurse -Filter $script:AppExe | Select-Object -First 1
    if (-not $exe) { Stop-Install "no $($script:AppExe) inside $asset." }
    $srcRoot = $exe.DirectoryName
    $backup = ''
    try {
      New-Item -ItemType Directory -Force -Path (Split-Path $dest) | Out-Null
      if (Test-Path -LiteralPath $dest) {
        $backup = "$dest.bak"
        if (Test-Path -LiteralPath $backup) { Remove-Item -LiteralPath $backup -Recurse -Force }
        Move-Item -LiteralPath $dest -Destination $backup -ErrorAction Stop
        Write-Ok "old app kept as $(Split-Path $backup -Leaf)"
      }
      Copy-Item -LiteralPath $srcRoot -Destination $dest -Recurse -Force -ErrorAction Stop
    } catch {
      if ($backup -and -not (Test-Path -LiteralPath $dest)) { Move-Item -LiteralPath $backup -Destination $dest -ErrorAction SilentlyContinue }
      Stop-Install "could not install into $dest : $($_.Exception.Message)"
    }
    Write-Ok "installed -> $dest"
    if (-not $NoShortcut -and $env:OS -eq 'Windows_NT') {
      try {
        $lnkDir = Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs'
        $sh = New-Object -ComObject WScript.Shell
        $lnk = $sh.CreateShortcut((Join-Path $lnkDir "$($script:AppName).lnk"))
        $lnk.TargetPath = Join-Path $dest $script:AppExe
        $lnk.WorkingDirectory = $dest
        $lnk.Save()
        Write-Ok 'Start-menu shortcut created'
      } catch { Write-Warn2 "could not create the Start-menu shortcut: $($_.Exception.Message)" }
    }
    Set-Receipt @{ channel = $Channel; kind = 'portable'; path = $dest; version = $ver; asset = $asset; sha256 = $hash }
    Write-Step 'App installed'
    Write-Host "  app:      $dest"
    Write-Host "  version:  $ver   channel: $Channel"
    if ($backup) { Write-Host "  undo:     replace it with $backup" }
  }
  return 0
}

# ---------------------------------------------------------------------------
# main -- a function so `return`/`throw` never close the user's window under iex
# ---------------------------------------------------------------------------
function Invoke-VjInstall {
  if (-not (Test-WindowsHost)) { Stop-Install 'This installer is for Windows.  On macOS and Linux use tools/install.sh.' }
  try { [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12 } catch { }
  $script:Sums = $null
  if ($What -and $What -notin 'core', 'app', 'both') { Stop-Install "-What takes core, app or both (got '$What')." }
  if ($AppFormat -and $AppFormat -notin 'portable', 'setup') { Stop-Install "-AppFormat takes portable or setup (got '$AppFormat')." }

  $work = Join-Path ([IO.Path]::GetTempPath()) ('vjinstall-' + [Guid]::NewGuid().ToString('N'))
  New-Item -ItemType Directory -Force -Path $work | Out-Null
  try {
    if ($List) {
      $null = Install-Core $work $true
      Show-AppStatus
      return 0
    }

    $installed = Find-InstalledApp
    $rc = Get-Receipt
    $soft = $false
    $w = $What
    if (-not $w) {
      if (Test-Interactive) {
        $def = if ($installed) { '3' } else { '1' }
        Write-Step 'What do you want to install?'
        Write-Host '  1) the RetroArch core      for RetroArch'
        Write-Host '  2) the Virtual Jaguar app  standalone, no RetroArch needed (from v4.0)'
        Write-Host '  3) both'
        $w = switch (Read-Choice '  Number' $def) { '2' { 'app' } '3' { 'both' } default { 'core' } }
      } else {
        # Unattended runs keep doing what they always did -- the core -- and add the
        # app only when THIS script installed it.
        $w = 'core'
        if ($installed -and $rc['kind'] -eq $installed.Kind) { $w = 'both'; $soft = $true }
      }
    }

    if ($Channel -eq 'latest' -and (Test-Interactive)) {
      Write-Step 'Which build?'
      Write-Host '  1) latest release          stable, recommended'
      Write-Host '  2) nightly                 every push to develop; gated on compiling, not on tests'
      Write-Host '  3) a specific version      e.g. v4.0.0'
      switch (Read-Choice '  Number' '1') { '2' { $script:Channel = 'nightly' } '3' { $script:Channel = Read-Choice '  Tag' 'v4.0.0' } }
    }

    $coreRc = 0; $appRc = 0
    if ($w -eq 'core' -or $w -eq 'both') {
      try { $coreRc = Install-Core $work $soft }
      catch {
        if ($w -eq 'both') { Write-Warn2 "the core step failed: $($_.Exception.Message)"; Write-Warn2 'carrying on with the app.'; $coreRc = 1 }
        else { throw }
      }
    }
    if ($w -eq 'app' -or $w -eq 'both') {
      $appRc = Install-App $work
      if ($appRc -eq 3 -and $w -eq 'both') { $appRc = 0 }     # nothing published yet: not a failure when the core ran
    }
    if ($coreRc -ne 0) { return $coreRc }
    return $appRc
  } finally {
    Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
  }
}

$script:VjExit = 0
try {
  $r = @(Invoke-VjInstall)
  $script:VjExit = [int]$r[-1]
} catch {
  $script:VjExit = 1
  if ($_.Exception.Data -and $_.Exception.Data.Contains('VjExit')) { $script:VjExit = [int]$_.Exception.Data['VjExit'] }
  Write-Host ''
  Write-Host "ERROR $($_.Exception.Message)" -ForegroundColor Red
}
# Only a script FILE may set the process exit code; under `irm | iex` (no
# $PSCommandPath) `exit` would close the user's console.
if ($PSCommandPath) { exit $script:VjExit }
