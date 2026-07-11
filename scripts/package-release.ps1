#requires -Version 5.1
# Offline packager: assemble the installer + Nexus ZIPs from committed inputs and
# the freshly built .asi. Consumes only what is in the repo (vendor/ included);
# never hits the network. Mirrors the doctrine release layout.
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$root = Resolve-Path (Join-Path $PSScriptRoot '..')

# Version is the single source of truth in src/version.h.
$verLine = Select-String -Path (Join-Path $root 'src/version.h') -Pattern '#define MEHT_VERSION "([^"]+)"'
if (-not $verLine) { throw "MEHT_VERSION not found in src/version.h" }
$version = $verLine.Matches[0].Groups[1].Value
Write-Host "Packaging MirrorsEdgeHeadTracking v$version" -ForegroundColor Cyan

$asi = Join-Path $root 'build/Release/MirrorsEdgeHeadTracking.asi'
if (-not (Test-Path $asi)) { throw "Build output missing: $asi (run pixi run build first)" }
$loaderDll = Join-Path $root 'vendor/ultimate-asi-loader/dinput8.dll'
if (-not (Test-Path $loaderDll)) { throw "Vendored ASI loader missing (run pixi run update-deps)" }

$rel = Join-Path $root 'release'
$stage = Join-Path $rel 'artifact-contents'
Remove-Item $rel -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Path $stage -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $stage 'plugins') -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $stage 'vendor/ultimate-asi-loader') -Force | Out-Null

# Mod payload.
Copy-Item $asi (Join-Path $stage 'plugins/MirrorsEdgeHeadTracking.asi') -Force
Copy-Item (Join-Path $root 'assets/MirrorsEdgeHeadTracking.ini') (Join-Path $stage 'plugins/MirrorsEdgeHeadTracking.ini') -Force

# Vendored loader (zip root: loader DLL + its LICENSE + README only).
Copy-Item $loaderDll (Join-Path $stage 'vendor/ultimate-asi-loader/dinput8.dll') -Force
Copy-Item (Join-Path $root 'vendor/ultimate-asi-loader/LICENSE') (Join-Path $stage 'vendor/ultimate-asi-loader/LICENSE') -Force
Copy-Item (Join-Path $root 'vendor/ultimate-asi-loader/README.md') (Join-Path $stage 'vendor/ultimate-asi-loader/README.md') -Force

# Scripts + docs. install.cmd / uninstall.cmd resolve the game via
# shared/find-game.ps1 even when a GAME_PATH is passed explicitly (the shim
# validates it and emits GAME_EXE / GAME_EXE_RELPATH), so the shared bundle
# must ship or the scripts fail on startup.
Copy-Item (Join-Path $root 'scripts/install.cmd') $stage -Force
Copy-Item (Join-Path $root 'scripts/uninstall.cmd') $stage -Force
Import-Module (Join-Path $root 'cameraunlock-core/powershell/ReleaseWorkflow.psm1') -Force
Copy-SharedBundle -StagingDir $stage
foreach ($doc in 'README.md','LICENSE','CHANGELOG.md','THIRD-PARTY-NOTICES.md') {
    Copy-Item (Join-Path $root $doc) $stage -Force
}

# launcher-manifest.json (manifest delivery mode). The default INI is seeded only
# when absent so user config is never clobbered; loader archives/detect are empty
# (the ASI loader DLL is deployed via files[], renamed to the dinput8 proxy).
$iniB64 = [Convert]::ToBase64String([IO.File]::ReadAllBytes((Join-Path $root 'assets/MirrorsEdgeHeadTracking.ini')))
$manifest = [ordered]@{
    schema_version = 2
    mod_info = [ordered]@{ name = 'MirrorsEdgeHeadTracking'; version = $version; game_id = 'mirrors-edge' }
    strategy = 'AsiLoader'
    delivery_mode = 'manifest'
    loader = [ordered]@{
        archives = @()
        detect = @()
        seed = @(@{ target = 'Binaries/MirrorsEdgeHeadTracking.ini'; content_b64 = $iniB64 })
    }
    files = @(
        [ordered]@{ source = 'vendor/ultimate-asi-loader/dinput8.dll'; target = 'Binaries/dinput8.dll' }
        [ordered]@{ source = 'plugins/MirrorsEdgeHeadTracking.asi'; target = 'Binaries/MirrorsEdgeHeadTracking.asi' }
    )
    runtime_requirements = @()
    dependencies = @()
}
$manifest | ConvertTo-Json -Depth 8 | Set-Content (Join-Path $stage 'launcher-manifest.json') -Encoding UTF8

# Zip: installer (full tree) + Nexus (deploy subtree only).
$installerZip = Join-Path $rel "MirrorsEdgeHeadTracking-v$version-installer.zip"
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $installerZip -Force
Write-Host "installer -> $installerZip" -ForegroundColor Green

$nexusStage = Join-Path $rel 'nexus'
New-Item -ItemType Directory -Path $nexusStage -Force | Out-Null
Copy-Item (Join-Path $stage 'plugins/MirrorsEdgeHeadTracking.asi') $nexusStage -Force
Copy-Item (Join-Path $stage 'plugins/MirrorsEdgeHeadTracking.ini') $nexusStage -Force
$nexusZip = Join-Path $rel "MirrorsEdgeHeadTracking-v$version-nexus.zip"
Compress-Archive -Path (Join-Path $nexusStage '*') -DestinationPath $nexusZip -Force
Remove-Item $nexusStage -Recurse -Force
Write-Host "nexus -> $nexusZip" -ForegroundColor Green
