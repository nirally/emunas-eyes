# Builds the release zip: out\EmunasEyes-<version>.zip with only our own files (no game files).
# Usage: powershell -File tools\package.ps1 [-Version x.y.z]   (build Release first; the version is CMakeLists.txt's
# MECVR_VERSION, which the mod logs - a -Version that differs is refused)
param([string]$Version)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$built = (Select-String -Path (Join-Path $root 'CMakeLists.txt') -Pattern 'set\(MECVR_VERSION "([^"]+)"\)').Matches[0].Groups[1].Value
if (-not $built) { throw 'MECVR_VERSION not found in CMakeLists.txt' }
if (-not $Version) { $Version = $built }
if ($Version -ne $built) { throw "-Version $Version but the mod is $built (CMakeLists.txt MECVR_VERSION)" }
$dll = Join-Path $root 'build\Release\mecvr.dll'
if (-not (Test-Path $dll)) { throw "Build first: $dll not found" }
if (-not (Select-String -Path $dll -Pattern 'MEC VR' -SimpleMatch -Quiet)) { throw 'mecvr.dll lacks the MEC VR marker the install scripts look for.' }

$name = "EmunasEyes-$Version"
$stage = Join-Path $root "out\$name"
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
$files = Join-Path $stage 'files'
New-Item -ItemType Directory -Force (Join-Path $files 'mecvr') | Out-Null

Copy-Item $dll (Join-Path $files 'NvCameraSDK64.dll')
Copy-Item (Join-Path $root 'config\schema.json') (Join-Path $files 'mecvr')
Copy-Item (Join-Path $root 'config\defaults.json') (Join-Path $files 'mecvr')
Copy-Item (Join-Path $root 'third_party\openxr\bin\openxr_loader.dll') (Join-Path $files 'mecvr')
foreach ($f in 'install.ps1', 'uninstall.ps1', 'Install.bat', 'Uninstall.bat', 'README.md') { Copy-Item (Join-Path $root "dist\$f") $stage }

# Third-party notices: every bundled or compiled-in component's licence.
$notices = Join-Path $stage 'THIRD_PARTY_NOTICES.txt'
$parts = @(
    @('MinHook 1.3.4 (compiled in) - BSD-2-Clause', 'third_party\minhook\LICENSE.txt'),
    @('nlohmann/json 3.12.0 (compiled in) - MIT', 'third_party\nlohmann\LICENSE.MIT'),
    @('OpenXR SDK loader 1.1.63 (mecvr\openxr_loader.dll) - Apache-2.0, Copyright The Khronos Group Inc.', 'third_party\openxr\LICENSE.txt')
)
$text = "Emuna's Eyes includes the following third-party software.`r`n"
foreach ($p in $parts) { $text += "`r`n==== $($p[0]) ====`r`n`r`n" + (Get-Content (Join-Path $root $p[1]) -Raw) + "`r`n" }
Set-Content -Path $notices -Value $text -Encoding utf8

$zip = Join-Path $root "out\$name.zip"
if (Test-Path $zip) { Remove-Item $zip }
# Not Compress-Archive: in Windows PowerShell 5.1 it writes backslashes into entry names, which other unzip tools
# extract as flat file names.
Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
$archive = [System.IO.Compression.ZipFile]::Open($zip, 'Create')
try {
    foreach ($f in Get-ChildItem $stage -Recurse -File) {
        $entry = $f.FullName.Substring($stage.Length + 1) -replace '\\', '/'
        [void][System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $f.FullName, $entry, 'Optimal')
    }
} finally { $archive.Dispose() }
Get-ChildItem $stage -Recurse -File | ForEach-Object { $_.FullName.Substring($stage.Length + 1) }
"-> $zip ($([math]::Round((Get-Item $zip).Length / 1MB, 2)) MB)"
