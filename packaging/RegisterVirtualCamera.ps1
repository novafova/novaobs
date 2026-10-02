param(
    [Parameter(Mandatory = $true)][string]$Root,
    [switch]$Unregister,
    [switch]$Elevated
)

$ErrorActionPreference = 'Stop'
$clsid = '{A3FCE0F5-3493-419F-958A-ABA1250EC20B}'
$cameraDir = Join-Path $Root 'data/obs-plugins/win-dshow'
$dll64 = Join-Path $cameraDir 'obs-virtualcam-module64.dll'
$dll32 = Join-Path $cameraDir 'obs-virtualcam-module32.dll'

function Get-CameraPath([Microsoft.Win32.RegistryView]$view) {
    $base = [Microsoft.Win32.RegistryKey]::OpenBaseKey('LocalMachine', $view)
    try {
        $key = $base.OpenSubKey("SOFTWARE\Classes\CLSID\$clsid\InprocServer32")
        if (-not $key) { return $null }
        try { return [string]$key.GetValue('') } finally { $key.Close() }
    } finally { $base.Close() }
}

$current64 = Get-CameraPath ([Microsoft.Win32.RegistryView]::Registry64)
$current32 = Get-CameraPath ([Microsoft.Win32.RegistryView]::Registry32)
$our64 = $current64 -and [string]::Equals($current64, $dll64, [StringComparison]::OrdinalIgnoreCase)
$our32 = $current32 -and [string]::Equals($current32, $dll32, [StringComparison]::OrdinalIgnoreCase)

if ($Unregister) {
    if (-not $our64 -and -not $our32) { exit 0 }
} elseif ($current64 -and $current32 -and (Test-Path -LiteralPath $current64) -and (Test-Path -LiteralPath $current32)) {
    # A normal OBS installation already supplies the system-wide virtual camera.
    exit 0
}

if (-not $Elevated) {
    $args = '-NoProfile -ExecutionPolicy Bypass -File "{0}" -Root "{1}" -Elevated' -f $PSCommandPath, $Root
    if ($Unregister) { $args += ' -Unregister' }
    try {
        $child = Start-Process -FilePath (Join-Path $PSHOME 'powershell.exe') -ArgumentList $args -Verb RunAs -Wait -PassThru
        exit $child.ExitCode
    } catch {
        Write-Error "Virtual camera registration was not approved: $_"
        exit 1
    }
}

$reg64 = if ([Environment]::Is64BitProcess) {
    Join-Path $env:WINDIR 'System32/regsvr32.exe'
} else {
    Join-Path $env:WINDIR 'Sysnative/regsvr32.exe'
}
$reg32 = Join-Path $env:WINDIR 'SysWOW64/regsvr32.exe'

if ($Unregister) {
    if ($our32) { & $reg32 /u /s $dll32; if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE } }
    if ($our64) { & $reg64 /u /s $dll64; if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE } }
} else {
    if (-not (Test-Path -LiteralPath $dll32) -or -not (Test-Path -LiteralPath $dll64)) { exit 2 }
    & $reg32 /i /s $dll32
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & $reg64 /i /s $dll64
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
exit 0
