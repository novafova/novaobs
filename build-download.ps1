param([switch]$SkipOverlayBuild)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$buildRoot = Join-Path $root '.nova-build'
$stage = Join-Path $buildRoot ('release-stage-' + [guid]::NewGuid().ToString('N'))
$nsis = Join-Path $buildRoot 'nsis-3.13/nsis-3.13/makensis.exe'
$redist = Join-Path $buildRoot 'redist'
$outputDir = Join-Path $root 'dist'
$output = Join-Path $outputDir 'NovaOBS-32.2.2-Windows-Setup.exe'

if (-not (Test-Path -LiteralPath $nsis)) {
    throw 'NSIS 3.13 is needed on the build machine. Download the portable nsis-3.13.zip from https://sourceforge.net/projects/nsis/files/NSIS%203/3.13/ and unpack it into .nova-build/nsis-3.13.'
}
foreach ($arch in @('x64', 'x86')) {
    $file = Join-Path $redist "vc_redist.$arch.exe"
    if (-not (Test-Path -LiteralPath $file)) {
        throw "The Microsoft Visual C++ $arch runtime is missing on the build machine: $file. Download it from https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist."
    }
    $signature = Get-AuthenticodeSignature -LiteralPath $file
    if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'Microsoft Corporation') {
        throw "The Microsoft runtime signature is invalid: $file"
    }
}

& (Join-Path $root 'package-portable-preview.ps1') -Destination $stage -SeedScene -SkipOverlayBuild:$SkipOverlayBuild
if ($LASTEXITCODE -and $LASTEXITCODE -ne 0) { throw 'Nova packaging failed.' }
New-Item -ItemType Directory -Force $outputDir | Out-Null
& $nsis "/DSTAGE=$stage" "/DREDIST=$redist" "/DOUTPUT=$output" (Join-Path $root 'packaging/NovaOBS.nsi')
if ($LASTEXITCODE -ne 0) { throw 'The Nova OBS installer build failed.' }
if (-not (Test-Path -LiteralPath $output)) { throw 'NSIS reported success without creating an installer.' }
$artifact = Get-Item -LiteralPath $output
$hash = Get-FileHash -LiteralPath $output -Algorithm SHA256
Write-Host "Installer: $($artifact.FullName)"
Write-Host "Size: $([math]::Round($artifact.Length / 1MB, 1)) MB"
Write-Host "SHA256: $($hash.Hash)"
