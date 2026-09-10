#requires -Version 5.1
# Dev convenience: run uninstall.cmd against EVERY installed copy of the game.
#
# uninstall.cmd itself takes one install (the launcher passes the path it managed),
# so `pixi run uninstall` would otherwise clean whichever copy sorts first and leave
# the mod loaded in the others. Pass -GamePath to target a single install.
[CmdletBinding()]
param([string]$GamePath, [switch]$Force)
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

$script = Join-Path $PSScriptRoot 'uninstall.cmd'
$failed = @()
foreach ($target in $targets) {
    Write-Host ""
    Write-Host "--- $target" -ForegroundColor Cyan
    $args = @($target, '/y')
    if ($Force) { $args += '/force' }
    & cmd.exe /c $script @args
    if ($LASTEXITCODE -ne 0) { $failed += "$target (exit $LASTEXITCODE)" }
}
if ($failed.Count -gt 0) {
    throw "uninstall failed for: $($failed -join '; ')"
}
