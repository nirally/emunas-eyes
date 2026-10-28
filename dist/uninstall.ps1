# Emuna's Eyes - removes the mod and puts the game's own NvCameraSDK64.dll back.
# Usage: double-click Uninstall.bat, or: powershell -ExecutionPolicy Bypass -File uninstall.ps1 [-GameDir <folder>] [-KeepSettings]
# -KeepSettings keeps mecvr\config.json and mecvr\profiles\ for a later reinstall. Afterwards Steam's
# "Verify integrity of game files" confirms the game is back to stock.
param([string]$GameDir, [switch]$KeepSettings)
$ErrorActionPreference = 'Stop'

function Find-Game {
    $steam = (Get-ItemProperty 'HKCU:\Software\Valve\Steam' -ErrorAction SilentlyContinue).SteamPath
    if (-not $steam) { $steam = (Get-ItemProperty 'HKLM:\SOFTWARE\WOW6432Node\Valve\Steam' -ErrorAction SilentlyContinue).InstallPath }
    if (-not $steam) { return $null }
    $libs = @($steam)
    $vdf = Join-Path $steam 'steamapps\libraryfolders.vdf'
    if (Test-Path $vdf) {
        foreach ($m in [regex]::Matches((Get-Content $vdf -Raw), '"path"\s+"([^"]+)"')) { $libs += $m.Groups[1].Value -replace '\\\\', '\' }
    }
    foreach ($lib in $libs | Select-Object -Unique) {
        $dir = Join-Path $lib 'steamapps\common\Mirrors Edge Catalyst'
        if (Test-Path (Join-Path $dir 'MirrorsEdgeCatalyst.exe')) { return $dir }
    }
    return $null
}

function Test-Ours($path) { (Test-Path $path) -and (Select-String -Path $path -Pattern 'MEC VR' -SimpleMatch -Quiet) }

if (-not $GameDir) { $GameDir = Find-Game }
if (-not $GameDir -or -not (Test-Path (Join-Path $GameDir 'MirrorsEdgeCatalyst.exe'))) {
    throw "Couldn't find Mirror's Edge Catalyst. Run again with -GameDir `"<the game's folder>`"."
}
if (Get-Process MirrorsEdgeCatalyst -ErrorAction SilentlyContinue) { throw 'Close the game first.' }

foreach ($name in 'version.dll', 'dinput8.dll', 'NvCameraSDK64.dll') {
    $t = Join-Path $GameDir $name
    if (Test-Ours $t) { Remove-Item $t; "removed the mod's $name" }
}
$modDir = Join-Path $GameDir 'mecvr'
$nv = Join-Path $GameDir 'NvCameraSDK64.dll'
$nvOrig = Join-Path $modDir 'NvCameraSDK64_orig.dll'
if ((Test-Path $nvOrig) -and -not (Test-Path $nv)) { Move-Item $nvOrig $nv; "put the game's NvCameraSDK64.dll back" }
if (-not (Test-Path $nv)) { Write-Warning "NvCameraSDK64.dll is missing - in Steam run Verify integrity of game files." }

if (Test-Path $modDir) {
    if ($KeepSettings) {
        Get-ChildItem $modDir | Where-Object { $_.Name -ne 'config.json' -and $_.Name -ne 'profiles' } | Remove-Item -Recurse -Force
        'removed mecvr\ (kept config.json and profiles\)'
    } else {
        Remove-Item $modDir -Recurse -Force
        'removed mecvr\'
    }
}
"Emuna's Eyes is uninstalled. The game folder has only the game's own files again$(if ($KeepSettings) {' (plus your saved settings in mecvr\)'})."
