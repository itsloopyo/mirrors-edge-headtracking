[CmdletBinding()]
param([switch]$AllowDirty)
$ErrorActionPreference = 'Stop'
$ProjectRoot = Resolve-Path (Join-Path $PSScriptRoot '..')
Import-Module (Join-Path $ProjectRoot 'cameraunlock-core\powershell\NightlyRelease.psm1') -Force

$verLine = Select-String -Path (Join-Path $ProjectRoot 'src/version.h') -Pattern '#define MEHT_VERSION "([^"]+)"'
$version = $verLine.Matches[0].Groups[1].Value

Publish-NightlyBuild `
    -ModId 'mirrors-edge' `
    -ModName 'MirrorsEdgeHeadTracking' `
    -Version $version `
    -ProjectRoot $ProjectRoot `
    -BuildCommand 'pixi run build' `
    -AllowDirty:$AllowDirty
