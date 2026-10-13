# Installs the current build into the game folder.
# Usage: powershell -File tools\deploy.ps1 [-As nvcamera|version|dinput8] [-Safe]
#   nvcamera (default): replaces NvCameraSDK64.dll; the game's original is moved to mecvr\NvCameraSDK64_orig.dll
param([ValidateSet('nvcamera','version','dinput8')][string]$As = 'nvcamera', [switch]$Safe)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$game = 'E:\SteamLibrary\steamapps\common\Mirrors Edge Catalyst'
if (Get-Process MirrorsEdgeCatalyst -ErrorAction SilentlyContinue) { throw 'Close the game first.' }
$dll = Join-Path $root 'build\Release\mecvr.dll'
if (-not (Test-Path $dll)) { throw "Build first: $dll not found" }

function Test-Ours($path) { (Test-Path $path) -and (Select-String -Path $path -Pattern 'MEC VR' -SimpleMatch -Quiet) }

$modDir = Join-Path $game 'mecvr'
New-Item -ItemType Directory -Force $modDir | Out-Null

# Remove any previous door; never touch files that aren't ours.
foreach ($name in 'version.dll','dinput8.dll') {
    $t = Join-Path $game $name
    if (Test-Path $t) { if (Test-Ours $t) { Remove-Item $t } else { throw "$t exists and is not MEC VR - refusing to touch it." } }
}
$nv = Join-Path $game 'NvCameraSDK64.dll'
$nvOrig = Join-Path $modDir 'NvCameraSDK64_orig.dll'
if ((Test-Path $nv) -and -not (Test-Ours $nv)) {
    # The game's own file: keep it as the original we forward to.
    Move-Item $nv $nvOrig -Force
}

switch ($As) {
    'nvcamera' {
        if (-not (Test-Path $nvOrig)) { throw "Original NvCameraSDK64.dll missing - run Steam 'Verify integrity' first." }
        Copy-Item $dll $nv -Force
    }
    default {
        # Put the game's original back if a previous deploy used the nvcamera door.
        if (Test-Ours $nv) { Remove-Item $nv }
        if ((Test-Path $nvOrig) -and -not (Test-Path $nv)) { Move-Item $nvOrig $nv }
        Copy-Item $dll (Join-Path $game "$As.dll")
    }
}

Copy-Item (Join-Path $root 'config\schema.json') $modDir -Force
Copy-Item (Join-Path $root 'third_party\openxr\bin\openxr_loader.dll') $modDir -Force
if (-not (Test-Path (Join-Path $modDir 'config.json'))) { Copy-Item (Join-Path $root 'config\defaults.json') (Join-Path $modDir 'config.json') }
$safeFile = Join-Path $modDir 'SAFE_MODE'
if ($Safe) { New-Item -ItemType File -Force $safeFile | Out-Null } else { Remove-Item $safeFile -ErrorAction SilentlyContinue }
"Deployed via $As door$(if ($Safe) {' (SAFE MODE)'}) -> $game"
