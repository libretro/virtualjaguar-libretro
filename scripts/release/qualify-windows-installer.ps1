param(
  [Parameter(Mandatory = $true)][string]$ReleaseDirectory,
  [Parameter(Mandatory = $true)][string]$Tag,
  [Parameter(Mandatory = $true)][ValidateSet('x64', 'arm64')][string]$Architecture,
  [Parameter(Mandatory = $true)][string]$Content,
  [Parameter(Mandatory = $true)][string]$OutputDirectory
)

$ErrorActionPreference = 'Stop'
$release = (Resolve-Path -LiteralPath $ReleaseDirectory).Path
$contentPath = (Resolve-Path -LiteralPath $Content).Path
$out = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force -Path $out | Out-Null
$label = $Tag -replace '^v', ''
$name = "VirtualJaguar-$label-windows-$Architecture-setup.exe"
$installer = Join-Path $release $name
$sumFile = Join-Path $release 'SHA256SUMS.txt'
$entry = Get-Content -LiteralPath $sumFile | Where-Object { $_ -match ('^[0-9a-fA-F]{64}\s+\*?' + [regex]::Escape($name) + '$') }
if (@($entry).Count -ne 1) { throw "Expected one checksum entry for staged installer $name" }
$expected = ([regex]::Match($entry, '^([0-9a-fA-F]{64})')).Groups[1].Value.ToLowerInvariant()
$actual = (Get-FileHash -LiteralPath $installer -Algorithm SHA256).Hash.ToLowerInvariant()
if ($actual -ne $expected) { throw "Staged installer checksum mismatch for $name" }
"PASS checksum $name $actual" | Set-Content -LiteralPath (Join-Path $out 'RESULT.txt')

$install = Join-Path $env:RUNNER_TEMP "vj-release-$Architecture-install"
$uninstaller = Join-Path $install 'unins000.exe'
$installed = $false
try {
  $p = Start-Process -FilePath $installer -ArgumentList @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/CURRENTUSER', "/DIR=$install", "/LOG=$(Join-Path $out 'installer.log')") -Wait -PassThru
  if ($p.ExitCode -ne 0) { throw "Silent installer exited with $($p.ExitCode)" }
  $installed = $true
  $exe = Join-Path $install 'virtualjaguar.exe'
  $shaders = Join-Path $install 'shaders'
  if (!(Test-Path -LiteralPath $exe -PathType Leaf)) { throw 'Installed application executable is missing' }
  if (!(Test-Path -LiteralPath $shaders -PathType Container)) { throw 'Installed shader assets are missing' }
  Add-Content -LiteralPath (Join-Path $out 'RESULT.txt') -Value 'PASS silent install'

  $qualifier = Join-Path (Get-Location).Path 'app/test/gl-qualification/gl_qualify.py'
  $glOut = Join-Path $out 'windowed-yarc'
  & python -I $qualifier --exe $exe --shaders $shaders --out $glOut --os windows --content $contentPath --only vj-sharp-bilinear.slangp --expect-renderer '(llvmpipe|D3D12)' --timeout 180
  if ($LASTEXITCODE -ne 0) { throw "Windowed YARC GL qualification failed with exit code $LASTEXITCODE" }
  Add-Content -LiteralPath (Join-Path $out 'RESULT.txt') -Value 'PASS real-window YARC boot and bundled GL shader render'
}
finally {
  if ($installed) {
    if (!(Test-Path -LiteralPath $uninstaller -PathType Leaf)) { throw 'Installed package has no uninstaller' }
    $u = Start-Process -FilePath $uninstaller -ArgumentList @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART') -Wait -PassThru
    if ($u.ExitCode -ne 0) { throw "Silent uninstaller exited with $($u.ExitCode)" }
    Start-Sleep -Seconds 2
    if (Test-Path -LiteralPath (Join-Path $install 'virtualjaguar.exe')) { throw 'Silent uninstall left the application executable behind' }
    Add-Content -LiteralPath (Join-Path $out 'RESULT.txt') -Value 'PASS silent uninstall'
  }
}
