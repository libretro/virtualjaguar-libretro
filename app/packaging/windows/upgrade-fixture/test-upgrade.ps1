param(
    [Parameter(Mandatory)][ValidateSet('x64','arm64')][string]$Arch,
    [Parameter(Mandatory)][string]$Directory,
    [Parameter(Mandatory)][ValidateSet('azure','fixture')][string]$Signing
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class UpgradeFixtureUi {
    [DllImport("user32.dll", SetLastError = true)]
    public static extern bool PostMessage(IntPtr window, uint message, IntPtr wParam, IntPtr lParam);
}
'@
$native = $env:RUNNER_ARCH
if ($native -ne $Arch) { throw "Expected native $Arch, runner reports $native" }
$root = (Resolve-Path -LiteralPath $Directory).Path
$oldSetup = Join-Path $root "VirtualJaguar-nightly-101-windows-$Arch-direct-setup.exe"
$newSetup = Join-Path $root "VirtualJaguar-nightly-102-windows-$Arch-direct-setup.exe"
$feed = Join-Path $root "nightly-$Arch.xml"
$logs = Join-Path $env:RUNNER_TEMP "vj-upgrade-$Arch"
New-Item -ItemType Directory -Path $logs -Force | Out-Null
$openssl = Join-Path $env:ProgramFiles 'Git\usr\bin\openssl.exe'
$certPem = Join-Path $logs 'tls-cert.pem'
$certDer = Join-Path $logs 'tls-cert.cer'
$tlsKey = Join-Path $env:RUNNER_TEMP "vj-upgrade-$Arch-tls.key"
$trusted = @()
$server = $null
$appProcess = $null

function Get-AppControls {
    if (!$script:appProcess -or $script:appProcess.HasExited) { return }
    $condition = [System.Windows.Automation.PropertyCondition]::new(
        [System.Windows.Automation.AutomationElement]::ProcessIdProperty, $script:appProcess.Id)
    $windows = [System.Windows.Automation.AutomationElement]::RootElement.FindAll(
        [System.Windows.Automation.TreeScope]::Children, $condition)
    foreach ($window in $windows) {
        try {
            if ($window.Current.IsOffscreen) { continue }
            $window
            $window.FindAll([System.Windows.Automation.TreeScope]::Descendants,
                [System.Windows.Automation.Condition]::TrueCondition) |
                Where-Object { !$_.Current.IsOffscreen }
        } catch [System.Windows.Automation.ElementNotAvailableException] {}
    }
}

function Wait-For($Label, [scriptblock]$Condition, [int]$Seconds = 75) {
    $deadline = (Get-Date).AddSeconds($Seconds)
    do {
        $result = & $Condition
        if ($result) { return $result }
        Start-Sleep -Milliseconds 250
    } while ((Get-Date) -lt $deadline)
    Get-AppControls | ForEach-Object { Write-Host "UI: $($_.Current.Name)" }
    throw "Timed out waiting for $Label"
}

function Find-Control([string]$Pattern, [switch]$Button) {
    foreach ($element in (Get-AppControls)) {
        try {
            if ($element.Current.Name -notmatch $Pattern) { continue }
            if ($Button -and (!$element.Current.IsEnabled -or
                $element.Current.ControlType -ne [System.Windows.Automation.ControlType]::Button)) { continue }
            return $element
        } catch [System.Windows.Automation.ElementNotAvailableException] {}
    }
}

function Click([string]$Name) {
    $element = Wait-For "button $Name" { Find-Control "^$Name$" -Button }
    # Posting avoids blocking the test when a click opens a modal dialog.
    $handle = [IntPtr]::new($element.Current.NativeWindowHandle)
    if ($handle -eq [IntPtr]::Zero -or ![UpgradeFixtureUi]::PostMessage($handle, 0x00f5, [IntPtr]::Zero, [IntPtr]::Zero)) {
        throw "Could not click native button $Name"
    }
}

function Assert-Version([string]$Executable, [string]$Expected) {
    $version = (& $Executable --version | Out-String).Trim()
    if ($LASTEXITCODE -ne 0 -or $version -ne $Expected) { throw "Expected build $Expected, got '$version'" }
    $bytes = [IO.File]::ReadAllBytes($Executable)
    $machine = [BitConverter]::ToUInt16($bytes, [BitConverter]::ToInt32($bytes, 0x3c) + 4)
    $expectedMachine = if ($Arch -eq 'arm64') { 0xaa64 } else { 0x8664 }
    if ($machine -ne $expectedMachine) { throw "Installed program machine $machine does not match $Arch" }
}

try {
    if ($Signing -eq 'fixture') {
        $trusted += Import-Certificate -FilePath (Join-Path $root 'fixture-authenticode.cer') -CertStoreLocation 'Cert:\CurrentUser\Root'
    }
    foreach ($setup in @($oldSetup, $newSetup)) {
        $signature = Get-AuthenticodeSignature -LiteralPath $setup
        if ($signature.Status -ne 'Valid') { throw "Invalid Authenticode: $setup ($($signature.Status))" }
        if ($Signing -eq 'azure' -and !$signature.TimeStamperCertificate) { throw "Missing Azure timestamp: $setup" }
        Write-Host "Authenticode valid ($Signing): $setup"
    }
    & $openssl req -x509 -newkey rsa:2048 -noenc -days 1 -subj '/CN=127.0.0.1' -addext 'subjectAltName=IP:127.0.0.1' -out $certPem -keyout $tlsKey
    if ($LASTEXITCODE -ne 0) { throw 'Could not create loopback TLS certificate' }
    & $openssl x509 -in $certPem -outform DER -out $certDer
    if ($LASTEXITCODE -ne 0) { throw 'Could not encode loopback TLS certificate' }
    $trusted += Import-Certificate -FilePath $certDer -CertStoreLocation 'Cert:\CurrentUser\Root'
    $script = Join-Path $PSScriptRoot 'feed.py'
    $serverArgs = @(('"{0}"' -f $script), 'serve', ('"{0}"' -f $root), ('"{0}"' -f $tlsKey), '--cert', ('"{0}"' -f $certPem))
    $server = Start-Process -FilePath (Get-Command python).Source -ArgumentList $serverArgs -RedirectStandardError (Join-Path $logs 'https.log') -PassThru

    $data = Join-Path $env:APPDATA 'VirtualJaguar'
    $save = Join-Path $data 'saves\upgrade-fixture.srm'
    $config = Join-Path $data 'config.ini'
    if ((Test-Path $config) -or (Test-Path $save)) { throw 'Fixture requires a fresh runner profile' }
    New-Item -ItemType Directory -Path (Split-Path -Parent $save) -Force | Out-Null
    'fixture save sentinel' | Set-Content -LiteralPath $save
    'fixture.config=preserve' | Set-Content -LiteralPath $config
    $originalSave = (Get-FileHash -LiteralPath $save).Hash
    $originalConfig = (Get-FileHash -LiteralPath $config).Hash

    foreach ($scope in @('currentuser', 'allusers')) {
        $install = Join-Path $logs "custom $scope install"
        $exe = Join-Path $install 'virtualjaguar.exe'
        $marker = Join-Path $install 'direct-install.ini'
        $playing = Join-Path $install 'fixture-playing'
        $args = @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/SP-', "/$scope",
            ('/DIR="{0}"' -f $install), ('/LOG="{0}\setup-{1}.log"' -f $logs, $scope))
        $setup = Start-Process -FilePath $oldSetup -ArgumentList $args -PassThru -Wait
        if ($setup.ExitCode -ne 0) { throw "Initial $scope install exited $($setup.ExitCode)" }
        Assert-Version $exe '101'
        $previousHash = (Get-FileHash -LiteralPath $exe).Hash

        if ($scope -eq 'currentuser') {
            Copy-Item -LiteralPath (Join-Path $root 'tampered.xml') -Destination $feed -Force
            $null = Wait-For 'loopback HTTPS' {
                try { (Invoke-WebRequest -Uri "https://127.0.0.1:8443/nightly-$Arch.xml" -TimeoutSec 3 -UseBasicParsing).StatusCode -eq 200 }
                catch { $false }
            }
            $appProcess = Start-Process -FilePath $exe -WorkingDirectory $install -ArgumentList '--check' -PassThru
            Click 'Install update'
            $null = Wait-For 'WinSparkle signature rejection' { Find-Control 'improperly signed' }
            if ($appProcess.HasExited -or (Get-FileHash -LiteralPath $exe).Hash -ne $previousHash) {
                throw 'Tampered update changed or exited the installed program'
            }
            Write-Host "PASS $Arch rejects a modified installer with the original EdDSA signature"
            Stop-Process -Id $appProcess.Id
            $appProcess.WaitForExit()
        }

        Copy-Item -LiteralPath (Join-Path $root 'valid.xml') -Destination $feed -Force
        if ($scope -eq 'currentuser') { New-Item -ItemType File -Path $playing | Out-Null }
        $appProcess = Start-Process -FilePath $exe -WorkingDirectory $install -ArgumentList '--check' -PassThru
        Click 'Install update'
        $null = Wait-For 'downloaded update prompt' { Find-Control 'ready to|downloaded' }
        Click 'Install update'
        if ($scope -eq 'currentuser') {
            $null = Wait-For 'running-game shutdown denial' { Find-Control 'cannot be restarted' }
            if ($appProcess.HasExited -or (Get-FileHash -LiteralPath $exe).Hash -ne $previousHash) {
                throw 'Installer started while the fixture marked a game as running'
            }
            Click 'OK'
            Remove-Item -LiteralPath $playing
            Start-Sleep -Milliseconds 500
            Click 'Install update'
        }
        if (!$appProcess.WaitForExit(75000) -or $appProcess.ExitCode -ne 0) {
            throw "Old $scope instance did not exit normally after the approved update"
        }
        $null = Wait-For "installer replacing the $scope executable" {
            try { (Get-FileHash -LiteralPath $exe -ErrorAction Stop).Hash -ne $previousHash }
            catch { $false }
        }
        $setupName = "VirtualJaguar-nightly-102-windows-$Arch-direct-setup"
        $null = Wait-For 'updated installer to finish' {
            !(Get-Process -Name "$setupName*" -ErrorAction SilentlyContinue)
        }
        Assert-Version $exe '102'
        if ((Get-Content -LiteralPath $marker) -notcontains "Scope=$scope") { throw "Install scope changed: $marker" }
        $hive = if ($scope -eq 'currentuser') { 'HKCU:' } else { 'HKLM:' }
        $uninstall = Join-Path $hive "Software\Microsoft\Windows\CurrentVersion\Uninstall\Virtual Jaguar Direct Nightly ${Arch}_is1"
        $location = (Get-ItemProperty -LiteralPath $uninstall).InstallLocation
        if ($location.TrimEnd('\') -ne $install) { throw "Install path changed: $location" }
        if ((Get-FileHash -LiteralPath $save).Hash -ne $originalSave -or
            (Get-FileHash -LiteralPath $config).Hash -ne $originalConfig) { throw 'User data changed' }
        Write-Host "PASS $Arch $scope 101 -> 102 with scope, custom path, saves and settings preserved"
        $removed = Start-Process -FilePath (Join-Path $install 'unins000.exe') -ArgumentList '/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART' -Wait -PassThru
        if ($removed.ExitCode -ne 0) { throw "Uninstall exited $($removed.ExitCode)" }
    }
} finally {
    if ($appProcess -and !$appProcess.HasExited) { Stop-Process -Id $appProcess.Id -ErrorAction SilentlyContinue }
    if ($server -and !$server.HasExited) { Stop-Process -Id $server.Id -ErrorAction SilentlyContinue }
    foreach ($cert in $trusted) {
        Remove-Item -LiteralPath "Cert:\CurrentUser\Root\$($cert.Thumbprint)" -ErrorAction SilentlyContinue
    }
    Remove-Item -LiteralPath $tlsKey -ErrorAction SilentlyContinue
}
