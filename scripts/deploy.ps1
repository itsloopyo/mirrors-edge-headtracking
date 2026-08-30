#requires -Version 5.1
# Dev convenience: build output -> game Binaries folder for testing. Copies the
# .asi, the vendored ASI loader (as dinput8.dll), and the default INI (only if
# absent, so local tweaks survive). Resolves the game path from games.json.
[CmdletBinding()]
param([string]$GamePath)
$ErrorActionPreference = 'Stop'
$root = Resolve-Path (Join-Path $PSScriptRoot '..')

if (-not $GamePath) {
    # Find-GamePath walks every Steam library, the GOG registry and the
    # MIRRORS_EDGE_PATH override. Reading steam_folder out of games.json and
    # gluing it onto the default Steam directory found only the one install
    # that happens to live on C:.
    Import-Module (Join-Path $root 'cameraunlock-core/powershell/GamePathDetection.psm1') -Force
    $GamePath = Find-GamePath -GameId 'mirrors-edge'
}
if (-not $GamePath -or -not (Test-Path $GamePath)) {
    throw "Could not resolve Mirror's Edge install. Pass -GamePath."
}

$binDir = Join-Path $GamePath 'Binaries'
$asi = Join-Path $root 'build/Release/MirrorsEdgeHeadTracking.asi'
$loader = Join-Path $root 'vendor/ultimate-asi-loader/dinput8.dll'
$ini = Join-Path $root 'assets/MirrorsEdgeHeadTracking.ini'

if (-not (Test-Path $asi)) { throw "Build output missing: $asi (run pixi run build)" }

Copy-Item $asi $binDir -Force
Write-Host "deployed MirrorsEdgeHeadTracking.asi -> $binDir" -ForegroundColor Green
Copy-Item $loader (Join-Path $binDir 'dinput8.dll') -Force
Write-Host "deployed dinput8.dll (ASI loader)" -ForegroundColor Green
$iniDst = Join-Path $binDir 'MirrorsEdgeHeadTracking.ini'
if (-not (Test-Path $iniDst)) {
    Copy-Item $ini $iniDst -Force
    Write-Host "deployed default INI" -ForegroundColor Green
} else {
    Write-Host "INI already present, left as-is" -ForegroundColor DarkGray
}
