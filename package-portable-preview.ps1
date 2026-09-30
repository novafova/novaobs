param(
    [switch]$SkipOverlayBuild,
    [string]$Destination = (Join-Path $PSScriptRoot '.nova-build/NovaOBS-preview'),
    [switch]$SeedScene
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$installed = Join-Path $env:ProgramFiles 'obs-studio'
$obsExe = Join-Path $installed 'bin/64bit/obs64.exe'
if (-not (Test-Path $obsExe)) { throw "OBS Studio is not installed at $installed" }
$version = (Get-Item $obsExe).VersionInfo.ProductVersion
if ($version -ne '32.2.2') { throw "Expected installed OBS Studio 32.2.2; found $version" }

if (-not $SkipOverlayBuild) {
    & (Join-Path $root 'src/build.bat')
    if ($LASTEXITCODE -ne 0) { throw 'The Nova overlay build failed.' }
}

$destination = [IO.Path]::GetFullPath($Destination)
$buildRoot = [IO.Path]::GetFullPath((Join-Path $root '.nova-build')).TrimEnd('\') + '\'
if (-not $destination.StartsWith($buildRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Destination must be inside $buildRoot"
}
if (Test-Path -LiteralPath $destination) {
    throw "Destination already exists: $destination. Use a fresh directory to prevent shipping local OBS settings."
}
New-Item -ItemType Directory -Force $destination | Out-Null
Copy-Item -Path (Join-Path $installed '*') -Destination $destination -Recurse -Force

# Debug symbols and the installed copy's uninstaller do not serve a portable app.
$safeRoot = [IO.Path]::GetFullPath($destination).TrimEnd('\') + '\'
foreach ($symbol in Get-ChildItem -LiteralPath $destination -Recurse -File -Filter '*.pdb') {
    $fullPath = [IO.Path]::GetFullPath($symbol.FullName)
    if (-not $fullPath.StartsWith($safeRoot, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Unexpected package file path: $fullPath"
    }
    Remove-Item -LiteralPath $fullPath -Force
}
$uninstaller = Join-Path $destination 'uninstall.exe'
if (Test-Path $uninstaller) { Remove-Item -LiteralPath $uninstaller -Force }

$themeDir = Join-Path $destination 'data/obs-studio/themes'
$scriptDir = Join-Path $destination 'data/obs-plugins/frontend-tools/scripts'
New-Item -ItemType Directory -Force $themeDir,$scriptDir | Out-Null
Copy-Item (Join-Path $root 'nova-theme/Nova_Gaming.ovt') $themeDir -Force
Copy-Item (Join-Path $root 'NovaOBS-Setup.exe') $destination -Force
Copy-Item (Join-Path $root 'nova_clip_notify.lua') $scriptDir -Force
Copy-Item (Join-Path $root 'NovaOverlay.exe') $scriptDir -Force
Copy-Item (Join-Path $root 'packaging/Launch Nova OBS.cmd') $destination -Force
Copy-Item (Join-Path $root 'packaging/RegisterVirtualCamera.ps1') $destination -Force
New-Item -ItemType File -Force (Join-Path $destination 'obs_portable_mode.txt') | Out-Null

$configDir = Join-Path $destination 'config/obs-studio'
New-Item -ItemType Directory -Force $configDir | Out-Null
$userIni = Join-Path $configDir 'user.ini'
if (-not (Test-Path $userIni)) {
    $userValues = @(
        '[Appearance]',
        'Theme=dev.novaobs.Gaming',
        '',
        '[BasicWindow]',
        'PreviewEnabled=false'
    )
    if ($SeedScene) {
        $userValues += @(
            '', '[General]', 'FirstRun=true',
            '', '[Basic]',
            'Profile=Nova Gaming', 'ProfileDir=Nova Gaming',
            'SceneCollection=Nova Gaming', 'SceneCollectionFile=Nova Gaming.json'
        )
    }
    Set-Content -LiteralPath $userIni -Encoding utf8 -Value $userValues
}

if ($SeedScene) {
    $scenesDir = Join-Path $configDir 'basic/scenes'
    New-Item -ItemType Directory -Force $scenesDir | Out-Null
    Copy-Item (Join-Path $root 'packaging/Nova Gaming.json') (Join-Path $scenesDir 'Nova Gaming.json')
    $profileDir = Join-Path $configDir 'basic/profiles/Nova Gaming'
    New-Item -ItemType Directory -Force $profileDir | Out-Null
    Set-Content -LiteralPath (Join-Path $profileDir 'basic.ini') -Encoding utf8 -Value @(
        '[General]', 'Name=Nova Gaming',
        '', '[Video]',
        'FPSType=0', 'FPSCommon=60', 'FPSInt=60', 'FPSNum=60', 'FPSDen=1',
        '', '[SimpleOutput]',
        'RecRB=true', 'RecRBTime=20'
    )
}

$sourceDir = Join-Path $destination 'nova-source'
New-Item -ItemType Directory -Force $sourceDir | Out-Null
Copy-Item (Join-Path $root 'LICENSE') $sourceDir
Copy-Item (Join-Path $root 'build-nova.ps1') $sourceDir
Copy-Item (Join-Path $root 'nova_clip_notify.lua') $sourceDir
Copy-Item (Join-Path $root 'nova-theme/Nova_Gaming.ovt') $sourceDir
Copy-Item (Join-Path $root 'src/nova_overlay.cpp') $sourceDir
Copy-Item (Join-Path $root 'src/setup.cpp') $sourceDir
Copy-Item (Join-Path $root 'patches/obs-32.2.2.patch') $sourceDir
Copy-Item (Join-Path $root 'packaging/RegisterVirtualCamera.ps1') $sourceDir

Set-Content -LiteralPath (Join-Path $destination 'NOVA-SOURCE.txt') -Encoding utf8 -Value @(
    'This package bundles an installed, unmodified OBS Studio 32.2.2 binary.',
    'Nova theme and clip notification source: https://github.com/novafova/novaobs',
    'The Nova theme, script, overlay, setup, build recipe, and OBS patch source are in nova-source/.',
    'OBS Studio source: https://github.com/obsproject/obs-studio/tree/32.2.2',
    'The native Nova source build is produced by build-nova.ps1.'
)
Write-Host "Portable Nova OBS package is ready at $destination"
if (-not $SeedScene) { Write-Host 'Without -SeedScene, the clip script loads after the second launch.' }
