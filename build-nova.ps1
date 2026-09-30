param(
    [switch]$PrepareOnly,
    [switch]$SkipOverlayBuild
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$work = Join-Path $root '.nova-build'
$source = Join-Path $work 'upstream'
$build = Join-Path $work 'build'
$package = Join-Path $work 'NovaOBS'
$tag = '32.2.2'
$commit = 'ba2f32bdf791005443988a4955e963663e16b1ed'

function Invoke-Checked([string]$File, [string[]]$Arguments) {
    & $File @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$File failed with exit code $LASTEXITCODE"
    }
}

if (-not (Test-Path (Join-Path $source '.git'))) {
    New-Item -ItemType Directory -Force $work | Out-Null
    Invoke-Checked 'git' @('clone', '--depth', '1', '--branch', $tag, '--filter=blob:none',
        'https://github.com/obsproject/obs-studio.git', $source)
}

$actual = (& git -C $source rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $actual -ne $commit) {
    throw "Expected OBS $tag at $commit; found $actual. Use a fresh .nova-build/upstream checkout."
}
Invoke-Checked 'git' @('-C', $source, 'submodule', 'update', '--init', '--recursive', '--depth', '1')

$patch = Join-Path $root 'patches/obs-32.2.2.patch'
& git -C $source apply --reverse --check $patch 2>$null
if ($LASTEXITCODE -ne 0) {
    Invoke-Checked 'git' @('-C', $source, 'apply', '--check', $patch)
    Invoke-Checked 'git' @('-C', $source, 'apply', $patch)
}

if (-not $SkipOverlayBuild) {
    Invoke-Checked 'cmd.exe' @('/c', (Join-Path $root 'src/build.bat'))
}
$overlay = Join-Path $root 'NovaOverlay.exe'
if (-not (Test-Path $overlay)) { throw 'NovaOverlay.exe is missing. Build it or omit -SkipOverlayBuild.' }

$themeDestination = Join-Path $source 'frontend/data/themes/Nova_Gaming.ovt'
$scriptsDestination = Join-Path $source 'plugins/frontend-tools/data/scripts'
New-Item -ItemType Directory -Force $scriptsDestination | Out-Null
Copy-Item -LiteralPath (Join-Path $root 'nova-theme/Nova_Gaming.ovt') -Destination $themeDestination -Force
Copy-Item -LiteralPath (Join-Path $root 'nova_clip_notify.lua') -Destination $scriptsDestination -Force
Copy-Item -LiteralPath $overlay -Destination $scriptsDestination -Force

Write-Host "Prepared Nova OBS from OBS Studio $tag ($commit)."
if ($PrepareOnly) { return }

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
if (-not (Test-Path $vswhere)) { throw 'Visual Studio Build Tools not found.' }
$vs = (& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath).Trim()
if (-not $vs) { throw 'Visual Studio C++ Build Tools not found.' }
$atl = Get-ChildItem (Join-Path $vs 'VC/Tools/MSVC') -Directory |
    Where-Object { Test-Path (Join-Path $_.FullName 'atlmfc/include/atlbase.h') } |
    Select-Object -First 1
if (-not $atl) {
    throw 'C++ ATL is required for the full OBS build. Add "C++ ATL for latest v143 build tools" in Visual Studio Installer, then rerun this script.'
}

$cmake = (Get-Command cmake -ErrorAction SilentlyContinue).Source
if (-not $cmake) {
    $cmake = Join-Path $vs 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
}
if (-not (Test-Path $cmake)) { throw "CMake not found at $cmake" }

# Keep upstream's full Windows feature set. CEF is required for browser sources.
$configure = @(
    '-S', $source, '-B', $build, '-G', 'Visual Studio 17 2022', '-A', 'x64',
    '-DENABLE_FRONTEND=ON', '-DENABLE_PLUGINS=ON', '-DENABLE_SCRIPTING=ON',
    '-DENABLE_BROWSER=ON', '-DENABLE_WEBSOCKET=ON', '-DENABLE_VIRTUALCAM=ON',
    '-DENABLE_NVENC=ON', '-DENABLE_QSV11=ON', '-DENABLE_VLC=ON',
    '-DENABLE_VST=ON', '-DENABLE_WEBRTC=ON', '-DENABLE_WHATSNEW=ON',
    '-DVIRTUALCAM_GUID=1B581224-5953-4831-BD7B-9A9779F52184'
)
Invoke-Checked $cmake $configure
Invoke-Checked $cmake @('--build', $build, '--config', 'Release', '--parallel', '8')
Invoke-Checked $cmake @('--install', $build, '--config', 'Release', '--component', 'Runtime', '--prefix', $package)

New-Item -ItemType File -Force (Join-Path $package 'obs_portable_mode.txt') | Out-Null
Copy-Item -LiteralPath (Join-Path $source 'COPYING') -Destination (Join-Path $package 'OBS-GPL-2.0.txt') -Force
Copy-Item -LiteralPath (Join-Path $root 'packaging/Launch Nova OBS.cmd') -Destination $package -Force
Set-Content -LiteralPath (Join-Path $package 'NOVA-SOURCE.txt') -Encoding utf8 -Value @(
    'Nova OBS is a modified OBS Studio build.',
    "Upstream source: https://github.com/obsproject/obs-studio/tree/$tag",
    "Upstream commit: $commit",
    'Nova changes and complete build recipe: https://github.com/novafova/novaobs',
    'OBS Studio is GPL-2.0-or-later; see OBS-GPL-2.0.txt.'
)
Write-Host "Nova OBS is ready at $package"
