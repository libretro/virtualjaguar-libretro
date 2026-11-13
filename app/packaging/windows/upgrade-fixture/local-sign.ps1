# Disposable Authenticode fallback. Only the public certificate leaves this job.
param([Parameter(Mandatory)][string]$Directory)
$ErrorActionPreference = 'Stop'
$certificate = New-SelfSignedCertificate -Type CodeSigningCert -Subject 'CN=Virtual Jaguar Upgrade Fixture' `
    -CertStoreLocation 'Cert:\CurrentUser\My' -HashAlgorithm SHA256 -NotAfter (Get-Date).AddDays(2)
try {
    Export-Certificate -Cert $certificate -FilePath (Join-Path $Directory 'fixture-authenticode.cer') | Out-Null
    Import-Certificate -FilePath (Join-Path $Directory 'fixture-authenticode.cer') -CertStoreLocation 'Cert:\CurrentUser\Root' | Out-Null
    $installers = @(Get-ChildItem -LiteralPath $Directory -Filter '*-direct-setup.exe' -File)
    if ($installers.Count -ne 4) { throw "Expected four direct fixture installers, found $($installers.Count)" }
    foreach ($installer in $installers) {
        $signed = Set-AuthenticodeSignature -LiteralPath $installer.FullName -Certificate $certificate -HashAlgorithm SHA256
        if ($signed.Status -ne 'Valid') { throw "Fixture Authenticode failed for $($installer.Name): $($signed.StatusMessage)" }
    }
} finally {
    Remove-Item -LiteralPath "Cert:\CurrentUser\Root\$($certificate.Thumbprint)" -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath "Cert:\CurrentUser\My\$($certificate.Thumbprint)" -DeleteKey -ErrorAction SilentlyContinue
}
