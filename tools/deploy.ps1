# Installs the current build into the game folder as version.dll.
# Usage: powershell -File tools\deploy.ps1 [-As dinput8] [-Safe]
param([ValidateSet('version','dinput8')][string]$As = 'version', [switch]$Safe)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$game = 'E:\SteamLibrary\steamapps\common\Mirrors Edge Catalyst'
if (Get-Process MirrorsEdgeCatalyst -ErrorAction SilentlyContinue) { throw 'Close the game first.' }
$dll = Join-Path $root 'build\Release\mecvr.dll'
if (-not (Test-Path $dll)) { throw "Build first: $dll not found" }

# Never overwrite a file that isn't ours.
foreach ($name in 'version.dll','dinput8.dll') {
    $target = Join-Path $game $name
    if ((Test-Path $target) -and -not (Select-String -Path $target -Pattern 'MEC VR' -SimpleMatch -Quiet)) {
        throw "$target exists and is not MEC VR - refusing to overwrite."
    }
}
# Only one door at a time.
Remove-Item (Join-Path $game 'version.dll'), (Join-Path $game 'dinput8.dll') -ErrorAction SilentlyContinue

$modDir = Join-Path $game 'mecvr'
New-Item -ItemType Directory -Force $modDir | Out-Null
Copy-Item $dll (Join-Path $game "$As.dll")
Copy-Item (Join-Path $root 'config\schema.json') $modDir -Force
if (-not (Test-Path (Join-Path $modDir 'config.json'))) { Copy-Item (Join-Path $root 'config\defaults.json') (Join-Path $modDir 'config.json') }
$safeFile = Join-Path $modDir 'SAFE_MODE'
if ($Safe) { New-Item -ItemType File -Force $safeFile | Out-Null } else { Remove-Item $safeFile -ErrorAction SilentlyContinue }
"Deployed as $As.dll$(if ($Safe) {' (SAFE MODE)'}) -> $game"
