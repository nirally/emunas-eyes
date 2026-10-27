# Runner's Sight - a VR mod for Mirror's Edge Catalyst. Installs the mod into the game folder.
# Usage: double-click Install.bat, or: powershell -ExecutionPolicy Bypass -File install.ps1 [-GameDir <folder>]
# The game's own NvCameraSDK64.dll is kept in mecvr\ and put back by uninstall.ps1. Your settings
# (mecvr\config.json) and profiles (mecvr\profiles\) are kept when installing over an older version.
param([string]$GameDir)
$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
$targetVersion = '1.0.3.47248'

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
    throw "Couldn't find Mirror's Edge Catalyst. Run again with -GameDir `"<the game's folder>`" (the one with MirrorsEdgeCatalyst.exe)."
}
if (Get-Process MirrorsEdgeCatalyst -ErrorAction SilentlyContinue) { throw 'Close the game first.' }
$exeVersion = (Get-Item (Join-Path $GameDir 'MirrorsEdgeCatalyst.exe')).VersionInfo.FileVersion -replace ',', '.' -replace ' ', ''
if ($exeVersion -ne $targetVersion) {
    Write-Warning "The game is version $exeVersion; Runner's Sight was made for $targetVersion. It will start in safe mode (flat, mod inactive) and say so in mecvr\mecvr.log."
}

$files = Join-Path $here 'files'
foreach ($f in 'NvCameraSDK64.dll', 'mecvr\schema.json', 'mecvr\openxr_loader.dll', 'mecvr\defaults.json') {
    if (-not (Test-Path (Join-Path $files $f))) { throw "The package is incomplete: files\$f is missing. Extract the whole zip and try again." }
}

# Never overwrite another mod's proxy DLLs.
foreach ($name in 'version.dll', 'dinput8.dll') {
    $t = Join-Path $GameDir $name
    if (Test-Path $t) {
        if (Test-Ours $t) { Remove-Item $t } else { throw "$t belongs to another mod or tool. Remove it first: two mods hooking the game will fight." }
    }
}

$modDir = Join-Path $GameDir 'mecvr'
New-Item -ItemType Directory -Force $modDir | Out-Null
$nv = Join-Path $GameDir 'NvCameraSDK64.dll'
$nvOrig = Join-Path $modDir 'NvCameraSDK64_orig.dll'
if ((Test-Path $nv) -and -not (Test-Ours $nv)) { Move-Item $nv $nvOrig -Force }  # the game's own: kept to forward to
if (-not (Test-Path $nvOrig)) { throw "The game's NvCameraSDK64.dll is missing. In Steam: right-click the game > Properties > Installed Files > Verify integrity, then install again." }

Copy-Item (Join-Path $files 'NvCameraSDK64.dll') $nv -Force
Copy-Item (Join-Path $files 'mecvr\schema.json') $modDir -Force
Copy-Item (Join-Path $files 'mecvr\openxr_loader.dll') $modDir -Force
$config = Join-Path $modDir 'config.json'
if (-not (Test-Path $config)) { Copy-Item (Join-Path $files 'mecvr\defaults.json') $config }
Remove-Item (Join-Path $modDir 'SAFE_MODE') -ErrorAction SilentlyContinue

"Runner's Sight installed in $GameDir"
'Start SteamVR, put the headset on, then start the game from Steam. Settings in VR: click both sticks in and hold 1 s.'
