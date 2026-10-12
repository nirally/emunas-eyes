# Removes every MEC VR file from the game folder. Afterwards, Steam -> Verify integrity restores stock.
# Usage: powershell -File tools\undeploy.ps1 [-KeepLogs]
param([switch]$KeepLogs)
$ErrorActionPreference = 'Stop'
$game = 'E:\SteamLibrary\steamapps\common\Mirrors Edge Catalyst'
if (Get-Process MirrorsEdgeCatalyst -ErrorAction SilentlyContinue) { throw 'Close the game first.' }
foreach ($name in 'version.dll','dinput8.dll') {
    $target = Join-Path $game $name
    if (Test-Path $target) {
        if (Select-String -Path $target -Pattern 'MEC VR' -SimpleMatch -Quiet) { Remove-Item $target; "removed $name" }
        else { "left $name alone (not ours)" }
    }
}
$modDir = Join-Path $game 'mecvr'
if (Test-Path $modDir) {
    if ($KeepLogs) { Get-ChildItem $modDir -Exclude *.log | Remove-Item -Recurse; 'removed mecvr\ (kept logs)' }
    else { Remove-Item $modDir -Recurse; 'removed mecvr\' }
}
'Game folder is back to stock files.'
