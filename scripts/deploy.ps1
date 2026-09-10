#requires -Version 5.1
# Dev convenience: build output -> the game's Binaries folder for testing. Copies
# the .asi, the vendored ASI loader (as dinput8.dll), and the default INI (only if
# absent, so local tweaks survive).
#
# Deploys to EVERY installed copy, not the first one found: Mirror's Edge ships on
# Steam, GOG, the EA app and Game Pass, and a deploy that picks one silently leaves
# the others running whatever build was last dropped in them. Pass -GamePath to
# target a single install.
[CmdletBinding()]
param([string]$GamePath)
$ErrorActionPreference = 'Stop'
$root = Resolve-Path (Join-Path $PSScriptRoot '..')

Import-Module (Join-Path $root 'cameraunlock-core/powershell/GamePathDetection.psm1')
Import-Module (Join-Path $root 'cameraunlock-core/powershell/DevDeploy.psm1')

$forward = @{}
if ($GamePath) { $forward['GivenPath'] = $GamePath }

# No -ConfigFile: the orchestrator copies it unconditionally, which would wipe the
# tuning in an INI already sitting next to the game between test runs.
$null = Invoke-DevDeployASILoader @forward `
    -GameId 'mirrors-edge' `
    -GameDisplayName "Mirror's Edge" `
    -BuildOutputPath (Join-Path $root 'build/Release') `
    -ModDllName 'MirrorsEdgeHeadTracking.asi' `
    -VendorLoaderDll (Join-Path $root 'vendor/ultimate-asi-loader/dinput8.dll') `
    -AsiLoaderName 'dinput8.dll'

$targets = if ($GamePath) { @($GamePath) } else { @(Find-AllGamePaths -GameId 'mirrors-edge') }
foreach ($target in $targets) {
    $exeDir = Resolve-DevExeDir -GamePath $target -GameId 'mirrors-edge'
    $iniDst = Join-Path $exeDir 'MirrorsEdgeHeadTracking.ini'
    if (-not (Test-Path -LiteralPath $iniDst)) {
        Copy-Item -LiteralPath (Join-Path $root 'assets/MirrorsEdgeHeadTracking.ini') -Destination $iniDst -Force
        Write-Host "Deployed default INI to $exeDir" -ForegroundColor Green
    } else {
        Write-Host "INI already present at $exeDir, left as-is" -ForegroundColor DarkGray
    }
}
