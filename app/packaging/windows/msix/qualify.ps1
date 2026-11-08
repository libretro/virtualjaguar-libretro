# Run only on a disposable Windows CI machine. Never exports a private key.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$CandidateDirectory,
    [Parameter(Mandatory = $true)][string]$OutputDirectory,
    [ValidateSet("x64", "arm64")][string]$Architecture = "x64"
)
$ErrorActionPreference = 'Stop'
$candidate = (Resolve-Path $CandidateDirectory).Path
$out = (New-Item -ItemType Directory -Path $OutputDirectory -Force).FullName
Start-Transcript -Path (Join-Path $out 'qualification.log')
$identity = Get-Content "$PSScriptRoot/identity.json" -Raw | ConvertFrom-Json
$certificate = $null
$trusted = $null
$installed = $null
try {
    if (Get-AppxPackage -Name $identity.name) { throw 'Refusing to replace an existing app installation' }
    $metadata = Get-Content (Join-Path $candidate '*.msix.json') -Raw | ConvertFrom-Json
    if ($metadata.architecture -ne $Architecture) { throw 'Candidate architecture does not match this job' }
    $original = Join-Path $candidate $metadata.package
    if ((Get-FileHash $original -Algorithm SHA256).Hash -ne $metadata.sha256) { throw 'Candidate checksum mismatch' }
    $signedCopy = Join-Path $out 'test-only.msix'
    Copy-Item $original $signedCopy
    $certificate = New-SelfSignedCertificate -Type Custom -Subject $identity.publisher `
        -KeyUsage DigitalSignature -FriendlyName 'Disposable Virtual Jaguar CI' `
        -CertStoreLocation Cert:\CurrentUser\My -KeyExportPolicy NonExportable `
        -TextExtension @('2.5.29.37={text}1.3.6.1.5.5.7.3.3', '2.5.29.19={text}')
    $publicCertificate = Join-Path $out 'test-only.cer'
    Export-Certificate -Cert $certificate -FilePath $publicCertificate | Out-Null
    $trusted = Import-Certificate -FilePath $publicCertificate -CertStoreLocation Cert:\LocalMachine\TrustedPeople
    $sdk = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\bin\*\x64\signtool.exe" -ErrorAction SilentlyContinue | Sort-Object { [version]$_.Directory.Parent.Name } -Descending | Select-Object -First 1
    if (!$sdk) { throw 'Windows SDK SignTool missing' }
    & $sdk.FullName sign /fd SHA256 /s My /sha1 $certificate.Thumbprint $signedCopy
    if ($LASTEXITCODE -ne 0) { throw 'Test signing failed' }
    Add-AppxPackage -Path $signedCopy
    $installed = Get-AppxPackage -Name $identity.name
    if (!$installed) { throw 'Package was not registered' }
    $installed | Select-Object Name, Version, PackageFamilyName, InstallLocation | Format-List
    $exe = Join-Path $installed.InstallLocation 'virtualjaguar.exe'
    $rom = Join-Path $out 'yarc.j64'
    $romUrl = 'https://raw.githubusercontent.com/42Bastian/JaguarDemos/1954a05fafd1489bcd3273012df6ca8fc731785d/yarc/yarc.j64'
    Invoke-WebRequest -UseBasicParsing -Uri $romUrl -OutFile $rom
    if ((Get-FileHash $rom -Algorithm SHA256).Hash.ToLowerInvariant() -ne 'ea24adaf33265727cdada00f6f2b9d3869d69bb9c86335a9f81152a540b7e1c5') {
        throw 'Author-hosted reviewer ROM checksum mismatch'
    }
    Copy-Item (Join-Path $candidate '*.msix.json') $out
    Copy-Item (Join-Path $candidate 'SOURCE_COMMIT.txt') $out
    Get-CimInstance Win32_OperatingSystem | Select-Object Caption, Version, BuildNumber, OSArchitecture | Format-List
    $script:invocationIndex = 0
    function Run-Candidate([string[]]$Arguments) {
        $script:invocationIndex++
        $stdout = Join-Path $out "run-$script:invocationIndex.stdout.log"
        $stderr = Join-Path $out "run-$script:invocationIndex.stderr.log"
        $start = New-Object System.Diagnostics.ProcessStartInfo
        $start.FileName = $exe
        $start.Arguments = $Arguments -join ' '
        $start.UseShellExecute = $false
        $start.RedirectStandardOutput = $true
        $start.RedirectStandardError = $true
        $process = New-Object System.Diagnostics.Process
        $process.StartInfo = $start
        if (!$process.Start()) { throw 'Could not start installed app' }
        $stdoutTask = $process.StandardOutput.ReadToEndAsync()
        $stderrTask = $process.StandardError.ReadToEndAsync()
        if (!$process.WaitForExit(90000)) { $process.Kill(); throw 'Installed app timed out' }
        $process.WaitForExit()
        $stdoutTask.GetAwaiter().GetResult() | Set-Content $stdout
        $stderrTask.GetAwaiter().GetResult() | Set-Content $stderr
        Get-Content $stdout, $stderr | Write-Output
        $exitCode = $process.ExitCode
        $process.Dispose()
        if ($exitCode -ne 0) { throw "Installed app exited $exitCode" }
    }
    Run-Candidate @('--headless', '--frames', '300', '--expect-hash', 'f4c30ea3', "`"$rom`"")
    # Exercise the installed payload's default startup, without --shader or a saved config.
    $defaultShot = Join-Path $out 'default.png'
    $defaultData = Join-Path $out 'default-data'
    Run-Candidate @('--data-dir', "`"$defaultData`"", '--screenshot', "`"$defaultShot`"",
        '--screenshot-frame', '20', '--screenshot-emu-frames', '60', "`"$rom`"")
    if (!(Test-Path $defaultShot)) { throw 'Default windowed launch produced no screenshot' }
    $defaultLog = Get-Content (Join-Path $out "run-$script:invocationIndex.stderr.log") -Raw
    if ($defaultLog -notmatch '\[shader\].*vj-scanlines\.slangp built:') {
        throw 'The fresh-install default shader did not render'
    }
    # Reuse the image decoder and plain-vs-filtered comparison, with no tolerated hangs/skips.
    $shaders = Join-Path $installed.InstallLocation 'shaders'
    if (!(Test-Path (Join-Path $shaders 'vj-scanlines.slangp'))) { throw 'Default preset missing from MSIX' }
    & python "$PSScriptRoot/../../../test/gl-qualification/gl_qualify.py" `
        --exe $exe --shaders $shaders --out (Join-Path $out 'gl') --os windows `
        --content $rom --frame 20 --emu-frames 60 --timeout 90 --only vj-scanlines.slangp
    if ($LASTEXITCODE -ne 0) { throw 'Installed windowed GL/shader qualification failed' }
    if ((Get-FileHash $original -Algorithm SHA256).Hash -ne $metadata.sha256) { throw 'Original Store candidate changed' }
    'PASS: test-signed installation, headless hash, fresh default window, and plain/filtered YARC rendering. Manual Start-menu/file-picker/controller/audio/upgrade checks remain.' | Set-Content (Join-Path $out 'RESULT.txt')
} finally {
    if ($installed) { Remove-AppxPackage -Package $installed.PackageFullName }
    if ($trusted) { Remove-Item "Cert:\LocalMachine\TrustedPeople\$($trusted.Thumbprint)" }
    if ($certificate) { Remove-Item "Cert:\CurrentUser\My\$($certificate.Thumbprint)" }
    # Retain diagnostics only; never distribute test-signed packages/certs.
    Remove-Item (Join-Path $out 'test-only.msix'), (Join-Path $out 'test-only.cer'), (Join-Path $out 'yarc.j64') -Force -ErrorAction SilentlyContinue
    Stop-Transcript
}
