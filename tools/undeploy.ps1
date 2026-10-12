# Removes every MEC VR file from the game folder and puts the game's own
# NvCameraSDK64.dll back. Afterwards, Steam -> Verify integrity confirms stock.
# Usage: powershell -File tools\undeploy.ps1 [-KeepLogs]
param([switch]$KeepLogs)
$ErrorActionPreference = 'Stop'
$game = 'E:\SteamLibrary\steamapps\common\Mirrors Edge Catalyst'
if (Get-Process MirrorsEdgeCatalyst -ErrorAction SilentlyContinue) { throw 'Close the game first.' }
function Test-Ours($path) { (Test-Path $path) -and (Select-String -Path $path -Pattern 'MEC VR' -SimpleMatch -Quiet) }

foreach ($name in 'version.dll','dinput8.dll','NvCameraSDK64.dll') {
    $t = Join-Path $game $name
    if (Test-Ours $t) { Remove-Item $t; "removed our $name" }
}
$modDir = Join-Path $game 'mecvr'
$nv = Join-Path $game 'NvCameraSDK64.dll'
$nvOrig = Join-Path $modDir 'NvCameraSDK64_orig.dll'
if ((Test-Path $nvOrig) -and -not (Test-Path $nv)) { Move-Item $nvOrig $nv; 'restored the game''s NvCameraSDK64.dll' }
if (-not (Test-Path $nv)) { 'WARNING: NvCameraSDK64.dll missing - run Steam "Verify integrity of game files".' }

if (Test-Path $modDir) {
    if ($KeepLogs) { Get-ChildItem $modDir -Exclude *.log | Remove-Item -Recurse; 'removed mecvr\ (kept logs)' }
    else { Remove-Item $modDir -Recurse; 'removed mecvr\' }
}
'Game folder is back to stock files.'
