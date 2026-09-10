#requires -Version 5.1
<#
Dev-only: speed up the Mirror's Edge debug loop by disabling the Bink movies
(startup EA/DICE/Unreal/PhysX logos, menu attract loop, between-chapter
cutscenes, and loading-screen movies). UE3 skips any missing movie gracefully,
so this just renames each `<name>.bik` <-> `<name>.bik.disabled`.

  pixi run fast-debug-movies            # disable (default)
  powershell scripts/fast-debug-movies.ps1 -Restore   # put them all back

Applies to every installed copy (Steam, GOG, EA app, Game Pass) unless -GamePath
names one. This touches only the game's TdGame\Movies folder; it does not modify
game code or config. Fully reversible. Steam "Verify integrity" also restores them.
#>
[CmdletBinding()]
param(
    [switch]$Restore,
    [string]$GamePath
)
$ErrorActionPreference = 'Stop'
$root = Resolve-Path (Join-Path $PSScriptRoot '..')

if ($GamePath) {
    $targets = @($GamePath)
} else {
    Import-Module (Join-Path $root 'cameraunlock-core/powershell/GamePathDetection.psm1')
    $targets = @(Find-AllGamePaths -GameId 'mirrors-edge')
}
if ($targets.Count -eq 0) {
    throw "Could not resolve any Mirror's Edge install. Pass -GamePath, or set MIRRORS_EDGE_PATH."
}

foreach ($target in $targets) {
    Write-Host ""
    Write-Host "--- $target" -ForegroundColor Cyan
    $movies = Join-Path $target 'TdGame\Movies'
    if (-not (Test-Path -LiteralPath $movies)) { throw "Movies folder not found: $movies" }

    if ($Restore) {
        $disabled = Get-ChildItem $movies -Filter '*.bik.disabled'
        if (-not $disabled) { Write-Host "Nothing to restore (no *.bik.disabled)." -ForegroundColor Yellow; continue }
        foreach ($f in $disabled) {
            $orig = $f.FullName -replace '\.disabled$', ''
            Move-Item $f.FullName $orig -Force
            Write-Host "restored $($f.Name -replace '\.disabled$','')" -ForegroundColor Green
        }
        Write-Host "All movies restored." -ForegroundColor Cyan
        continue
    }

    $biks = Get-ChildItem $movies -Filter '*.bik'
    if (-not $biks) { Write-Host "No .bik files to disable (already disabled?)." -ForegroundColor Yellow; continue }
    foreach ($f in $biks) {
        Move-Item $f.FullName ($f.FullName + '.disabled') -Force
        Write-Host "disabled $($f.Name)" -ForegroundColor DarkGray
    }
    Write-Host "Disabled $($biks.Count) movie(s). Run with -Restore to undo." -ForegroundColor Cyan
}
