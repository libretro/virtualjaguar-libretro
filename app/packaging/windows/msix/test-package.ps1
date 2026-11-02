# Hermetic staging tests. No Windows SDK, credentials or emulator build required.
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = Join-Path ([IO.Path]::GetTempPath()) ('vj-msix-test-' + [guid]::NewGuid())
$prefix = Join-Path $root 'install tree'
$bin = Join-Path $prefix 'bin'
$docs = Join-Path $prefix 'share/doc/virtualjaguar'
$null = New-Item -ItemType Directory -Path $bin, $docs, (Join-Path $bin 'cheats'), (Join-Path $bin 'shaders') -Force
$exe = [byte[]]::new(128)
$exe[0] = 0x4d; $exe[1] = 0x5a; $exe[0x3c] = 64
$exe[64] = 0x50; $exe[65] = 0x45; $exe[68] = 0x64; $exe[69] = 0x86
[IO.File]::WriteAllBytes((Join-Path $bin 'virtualjaguar.exe'), $exe)
Set-Content (Join-Path $bin 'cheats/example.cht') 'fixture cheat'
Set-Content (Join-Path $bin 'shaders/example.slang') 'fixture shader'
Set-Content (Join-Path $docs 'LICENSE.txt') 'fixture GPL notice'
Set-Content (Join-Path $docs 'THIRD-PARTY-NOTICES.txt') 'fixture notices'
Set-Content (Join-Path $docs 'extra-license.txt') 'retain extra notices'
$script:checks = 0
function Check([bool]$condition, [string]$message) {
    if (-not $condition) { throw "FAIL: $message" }
    $script:checks++
}
function Reject([string]$version, [string]$arch, [string]$output, [string]$reason) {
    try {
        & "$PSScriptRoot/package.ps1" -InstallPrefix $prefix -Version $version -Architecture $arch -OutputDirectory $output -StageOnly | Out-Null
    } catch {
        Check ($_.Exception.Message -like "*$reason*") "expected $reason, got $_"
        return
    }
    throw "FAIL: accepted invalid input ($version, $arch, $reason)"
}
try {
    $out = Join-Path $root 'valid output'
    $layout = & "$PSScriptRoot/package.ps1" -InstallPrefix $prefix -Version '3.10.0' -Architecture x64 -OutputDirectory $out -StageOnly
    [xml]$xml = Get-Content (Join-Path $layout 'AppxManifest.xml') -Raw
    Check ($xml.Package.Identity.Name -eq 'JosephMattello.VirtualJaguar') 'reserved package identity'
    Check ($xml.Package.Identity.Version -eq '3.10.0.0') 'Store numeric version'
    Check ($xml.Package.Identity.Publisher -eq 'CN=65735C6B-702F-4938-A980-C84C590F6DD9') 'reserved publisher'
    Check ($xml.Package.Identity.ProcessorArchitecture -eq 'x64') 'PE architecture'
    Check ($xml.Package.Dependencies.TargetDeviceFamily.Name -eq 'Windows.Desktop') 'desktop only'
    Check ($xml.Package.Capabilities.Capability.Name -eq 'runFullTrust') 'classic desktop capability'
    foreach ($relative in @('virtualjaguar.exe', 'cheats/example.cht', 'shaders/example.slang', 'docs/LICENSE.txt',
        'docs/THIRD-PARTY-NOTICES.txt', 'docs/extra-license.txt')) {
        Check ((Get-FileHash (Join-Path $layout $relative)).Hash -eq
            (Get-FileHash (Join-Path $(if ($relative.StartsWith('docs/')) { $docs } else { $bin }) ($relative -replace '^docs/', ''))).Hash) "payload retained: $relative"
    }
    Check (-not (Test-Path (Join-Path $layout 'portable.txt'))) 'non-portable package'
    foreach ($entry in @(@('Square44x44Logo.png',44), @('Square150x150Logo.png',150), @('StoreLogo.png',50))) {
        $png = [IO.File]::ReadAllBytes((Join-Path $layout "Assets/$($entry[0])"))
        $width = $png[16] * 16777216 + $png[17] * 65536 + $png[18] * 256 + $png[19]
        $height = $png[20] * 16777216 + $png[21] * 65536 + $png[22] * 256 + $png[23]
        Check ($width -eq $entry[1] -and $height -eq $entry[1]) "manifest PNG size: $($entry[0])"
    }
    foreach ($version in @('3.10.0-nightly','3.10.0.1','0.1.0','65536.1.0','03.1.0')) {
        Reject $version x64 (Join-Path $root 'bad') 'Version'
    }
    Reject '3.10.0' arm64 (Join-Path $root 'bad') 'PE machine'
    Reject '3.10.0' x64 $prefix 'must not overlap'
    Reject '3.10.0' x64 $root 'must not overlap'
    Reject '3.10.0' x64 $out 'Output already exists'
    Check (Test-Path (Join-Path $layout 'virtualjaguar.exe')) 'existing output preserved'
    Set-Content (Join-Path $bin 'portable.txt') 'portable fixture'
    Reject '3.10.0' x64 (Join-Path $root 'bad') 'portable.txt'
    Remove-Item -LiteralPath (Join-Path $bin 'portable.txt')
    $exe[68] = 0x64; $exe[69] = 0xaa
    [IO.File]::WriteAllBytes((Join-Path $bin 'virtualjaguar.exe'), $exe)
    $arm = & "$PSScriptRoot/package.ps1" -InstallPrefix $prefix -Version '3.10.0.0' -Architecture arm64 -OutputDirectory (Join-Path $root 'arm') -StageOnly
    [xml]$armXml = Get-Content (Join-Path $arm 'AppxManifest.xml') -Raw
    Check ($armXml.Package.Identity.ProcessorArchitecture -eq 'arm64') 'ARM64 input and manifest agree'
    [IO.File]::WriteAllBytes((Join-Path $bin 'virtualjaguar.exe'), [byte[]]::new(64))
    Reject '3.10.0' x64 (Join-Path $root 'bad') 'not a PE'
    Write-Output "PASS: $script:checks MSIX staging checks"
} finally {
    # Only our unique temporary fixture directory, never a checkout/input tree.
    Remove-Item -LiteralPath $root -Recurse -Force
}
