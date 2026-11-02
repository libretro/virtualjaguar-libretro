# Run only on a disposable Windows CI machine. Never exports a private key.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$CandidateDirectory,
    [Parameter(Mandatory = $true)][string]$OutputDirectory
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
    if ($metadata.architecture -ne 'x64') { throw 'This qualification job requires x64' }
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
    $rom = (Resolve-Path "$PSScriptRoot/../../../../test/roms/yarc.j64").Path
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
    if ((Get-FileHash $original -Algorithm SHA256).Hash -ne $metadata.sha256) { throw 'Original Store candidate changed' }
    'PASS: test-signed installation and installed headless hash. Manual GUI/controller/audio/upgrade checks remain.' | Set-Content (Join-Path $out 'RESULT.txt')
} finally {
    if ($installed) { Remove-AppxPackage -Package $installed.PackageFullName }
    if ($trusted) { Remove-Item "Cert:\LocalMachine\TrustedPeople\$($trusted.Thumbprint)" }
    if ($certificate) { Remove-Item "Cert:\CurrentUser\My\$($certificate.Thumbprint)" }
    # Retain diagnostics only; never distribute test-signed packages/certs.
    Remove-Item (Join-Path $out 'test-only.msix'), (Join-Path $out 'test-only.cer') -Force -ErrorAction SilentlyContinue
    Stop-Transcript
}
