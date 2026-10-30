# Build an unsigned Microsoft Store submission package from `cmake --install`.
# -StageOnly is cross-platform; packing requires the Windows SDK MakeAppx tool.
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$InstallPrefix,
    [Parameter(Mandatory)][string]$Version,
    [Parameter(Mandatory)][ValidateSet('x64', 'arm64')][string]$Architecture,
    [Parameter(Mandatory)][string]$OutputDirectory,
    [string]$MakeAppx,
    [switch]$StageOnly
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

# Store versions use a numeric quad; the Store owns the final (zero) component.
if ($Version -notmatch '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(\.0)?$') {
    throw 'Version must be major.minor.patch or major.minor.patch.0 (no suffix).'
}
$parts = $Version.Split('.')
if ($parts[0] -eq '0' -or @($parts | Where-Object { [decimal]$_ -gt 65535 }).Count) {
    throw 'Version major must be positive and all components must be <= 65535.'
}
$packageVersion = ($parts[0..2] -join '.') + '.0'
$prefix = (Resolve-Path -LiteralPath $InstallPrefix).Path
$out = [IO.Path]::GetFullPath($OutputDirectory)
# Never overwrite/delete an input tree, including a caller passing an ancestor as output.
$sep = [IO.Path]::DirectorySeparatorChar
$prefixRoot = $prefix.TrimEnd($sep) + $sep
$outRoot = $out.TrimEnd($sep) + $sep
if ($outRoot.StartsWith($prefixRoot, [StringComparison]::OrdinalIgnoreCase) -or
    $prefixRoot.StartsWith($outRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'InstallPrefix and OutputDirectory must not overlap.'
}
$bin = Join-Path $prefix 'bin'
$docs = Join-Path $prefix 'share/doc/virtualjaguar'
$exe = Join-Path $bin 'virtualjaguar.exe'
foreach ($required in @($exe, (Join-Path $docs 'LICENSE.txt'), (Join-Path $docs 'THIRD-PARTY-NOTICES.txt'))) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) { throw "Missing required input: $required" }
}
if (Test-Path -LiteralPath (Join-Path $bin 'portable.txt')) {
    throw 'Store input must not contain portable.txt; use a CMake install tree, not a portable ZIP.'
}
foreach ($dir in @($bin, $docs)) {
    if (@(Get-ChildItem -LiteralPath $dir -Recurse -Force | Where-Object {
        $_.Attributes -band [IO.FileAttributes]::ReparsePoint
    }).Count) { throw "Input contains a symlink/reparse point: $dir" }
}
# Check PE machine independently of the filename/toolchain label.
$bytes = [IO.File]::ReadAllBytes($exe)
if ($bytes.Length -lt 64 -or $bytes[0] -ne 0x4d -or $bytes[1] -ne 0x5a) { throw 'Input is not a PE executable.' }
$pe = [BitConverter]::ToInt32($bytes, 0x3c)
if ($pe -lt 0 -or $pe -gt $bytes.Length - 6 -or [BitConverter]::ToUInt32($bytes, $pe) -ne 0x00004550) {
    throw 'Invalid PE header.'
}
$machine = [BitConverter]::ToUInt16($bytes, $pe + 4)
$expected = if ($Architecture -eq 'x64') { 0x8664 } else { 0xaa64 }
if ($machine -ne $expected) { throw "PE machine does not match $Architecture." }

$identity = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'identity.json') -Raw | ConvertFrom-Json
$layout = Join-Path $out "layout-$Architecture"
$package = Join-Path $out "VirtualJaguar-$packageVersion-windows-$Architecture.msix"
if ((Test-Path -LiteralPath $layout) -or (Test-Path -LiteralPath $package)) {
    throw 'Output already exists; choose a fresh OutputDirectory.'
}
$null = New-Item -ItemType Directory -Path $layout -Force
Get-ChildItem -LiteralPath $bin -Force | Copy-Item -Destination $layout -Recurse -Force
Copy-Item -LiteralPath $docs -Destination (Join-Path $layout 'docs') -Recurse
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Assets') -Destination $layout -Recurse
$manifest = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'AppxManifest.xml.in') -Raw
$values = @{
    NAME = $identity.name; PUBLISHER = $identity.publisher; VERSION = $packageVersion
    ARCH = $Architecture; DISPLAY_NAME = $identity.displayName
    PUBLISHER_DISPLAY_NAME = $identity.publisherDisplayName; APP_ID = $identity.applicationId
}
foreach ($key in $values.Keys) {
    $manifest = $manifest.Replace("@$key@", [Security.SecurityElement]::Escape([string]$values[$key]))
}
if ($manifest -match '@[A-Z_]+@') { throw 'Unexpanded manifest placeholder.' }
$manifestPath = Join-Path $layout 'AppxManifest.xml'
[IO.File]::WriteAllText($manifestPath, $manifest, [Text.UTF8Encoding]::new($false))
[xml]$xml = $manifest
# A manifest edit must not accidentally point to missing resources.
foreach ($file in @($xml.Package.Properties.Logo,
    $xml.Package.Applications.Application.Executable,
    $xml.Package.Applications.Application.VisualElements.Square150x150Logo,
    $xml.Package.Applications.Application.VisualElements.Square44x44Logo)) {
    if (-not (Test-Path -LiteralPath (Join-Path $layout ($file.Replace('\', '/'))) -PathType Leaf)) {
        throw "Manifest references missing file: $file"
    }
}
if ($StageOnly) { Write-Output $layout; return }
if (-not $IsWindows) { throw 'Packing requires Windows; use -StageOnly to inspect a layout on other hosts.' }
if (-not $MakeAppx) {
    $sdk = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits/10/bin'
    $tools = @(Get-ChildItem -Path "$sdk/*/x64/makeappx.exe" | Sort-Object { [version]$_.Directory.Parent.Name } -Descending)
    if (-not $tools.Count) { throw 'Install the Windows SDK or pass -MakeAppx.' }
    $MakeAppx = $tools[0].FullName
}
# Never use /nv: MakeAppx schema/content validation is part of the build gate.
& $MakeAppx pack /d $layout /p $package /o
if ($LASTEXITCODE -ne 0) { throw "MakeAppx pack failed ($LASTEXITCODE)." }
$unpacked = Join-Path $out "unpacked-$Architecture"
& $MakeAppx unpack /p $package /d $unpacked /o
if ($LASTEXITCODE -ne 0) { throw "MakeAppx unpack failed ($LASTEXITCODE)." }
foreach ($source in Get-ChildItem -LiteralPath $layout -File -Recurse) {
    $relative = [IO.Path]::GetRelativePath($layout, $source.FullName)
    $copy = Join-Path $unpacked $relative
    if (-not (Test-Path -LiteralPath $copy) -or
        (Get-FileHash -LiteralPath $copy).Hash -ne (Get-FileHash -LiteralPath $source.FullName).Hash) {
        throw "MSIX round trip lost or changed $relative"
    }
}
$metadata = [ordered]@{
    storeId = $identity.storeId; identityName = $identity.name; publisher = $identity.publisher
    version = $packageVersion; architecture = $Architecture
    package = [IO.Path]::GetFileName($package); sha256 = (Get-FileHash -LiteralPath $package -Algorithm SHA256).Hash.ToLowerInvariant()
    signed = $false; intendedDistribution = 'Microsoft Store submission only'
}
$metadata | ConvertTo-Json | Set-Content -LiteralPath "$package.json" -Encoding utf8
Write-Output "Unsigned Store submission package: $package"
