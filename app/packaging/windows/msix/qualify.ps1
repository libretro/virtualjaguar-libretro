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
    $sdk = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\bin\*\x64\signtool.exe" | Sort-Object FullName -Descending | Select-Object -First 1
    if (!$sdk) { throw 'Windows SDK SignTool missing' }
    & $sdk.FullName sign /fd SHA256 /s My /sha1 $certificate.Thumbprint $signedCopy
    if ($LASTEXITCODE -ne 0) { throw 'Test signing failed' }
    Add-AppxPackage -Path $signedCopy
    $installed = Get-AppxPackage -Name $identity.name
    if (!$installed) { throw 'Package was not registered' }
    $installed | Select-Object Name, Version, PackageFamilyName, InstallLocation | Format-List
    $exe = Join-Path $installed.InstallLocation 'virtualjaguar.exe'
    $rom = (Resolve-Path "$PSScriptRoot/../../../../test/roms/yarc.j64").Path
    $data = Join-Path $env:RUNNER_TEMP 'Jaguar qualification data'
    New-Item -ItemType Directory -Path $data -Force | Out-Null
    "window.w=1600`nwindow.h=1000`n" | Set-Content (Join-Path $data 'config.ini') -Encoding ASCII
    $script:invocationIndex = 0
    function Run-Candidate([string[]]$Arguments) {
        $script:invocationIndex++
        $stdout = Join-Path $out "run-$script:invocationIndex.stdout.log"
        $stderr = Join-Path $out "run-$script:invocationIndex.stderr.log"
        $process = Start-Process -FilePath $exe -ArgumentList $Arguments -PassThru `
            -RedirectStandardOutput $stdout -RedirectStandardError $stderr
        if (!$process.WaitForExit(90000)) { $process.Kill(); throw 'Installed app timed out' }
        Get-Content $stdout, $stderr | Write-Output
        if ($process.ExitCode -ne 0) { throw "Installed app exited $($process.ExitCode)" }
    }
    Run-Candidate @('--headless', '--frames', '300', '--expect-hash', 'f4c30ea3', "`"$rom`"")
    # Software OpenGL is supplied by the test host, never inserted into the package.
    $graphicsBin = Join-Path $env:VJ_TEST_MSYS2 'mingw64\bin'
    $env:PATH = "$graphicsBin;$env:PATH"
    $env:SDL_OPENGL_LIBRARY = Join-Path $graphicsBin 'opengl32.dll'
    if (!(Test-Path $env:SDL_OPENGL_LIBRARY)) { throw 'Host Mesa OpenGL library missing' }
    $env:GALLIUM_DRIVER = 'llvmpipe'
    foreach ($view in @('keypad', 'settings-input', 'settings-video')) {
        $png = Join-Path $out "$view.png"
        Run-Candidate @('--data-dir', "`"$data`"", '--screenshot', "`"$png`"", '--screenshot-frame', '120', '--show-window', $view, "`"$rom`"")
        if (!(Test-Path $png) -or (Get-Item $png).Length -lt 10000) { throw "Missing or empty screenshot: $view" }
    }
    if ((Get-FileHash $original -Algorithm SHA256).Hash -ne $metadata.sha256) { throw 'Original Store candidate changed' }
    'PASS: test-signed installation, installed headless hash and three Windows UI captures. Manual controller/audio/upgrade checks remain.' | Set-Content (Join-Path $out 'RESULT.txt')
} finally {
    if ($installed) { Remove-AppxPackage -Package $installed.PackageFullName }
    if ($trusted) { Remove-Item "Cert:\LocalMachine\TrustedPeople\$($trusted.Thumbprint)" }
    if ($certificate) { Remove-Item "Cert:\CurrentUser\My\$($certificate.Thumbprint)" }
    # Retain diagnostics and PNGs only; never distribute test-signed packages/certs.
    Remove-Item (Join-Path $out 'test-only.msix'), (Join-Path $out 'test-only.cer') -Force -ErrorAction SilentlyContinue
    Stop-Transcript
}
