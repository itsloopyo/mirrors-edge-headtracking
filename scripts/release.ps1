#!/usr/bin/env pwsh
#Requires -Version 5.1
# Fully unattended release workflow for MirrorsEdgeHeadTracking.
# Usage: pixi run release <major|minor|patch|nightly|X.Y.Z>
#
# Running this command IS the authorization. There is no second gate: the
# release runs end to end with zero prompts. The preconditions below (clean
# tree, on main, tag absent, valid semver) are the safety net in place of
# any interactive confirmation - each fails fast with a non-zero exit.

[CmdletBinding()]
param(
    [Parameter(Position=0)]
    [string]$Version,
    [switch]$AllowDirty,
    # Ship a release even when there are no user-facing commits since the
    # last tag (writes a maintenance changelog entry instead of aborting).
    [switch]$Force
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path

if (-not $Version) {
    Write-Error "Usage: pixi run release <major|minor|patch|nightly|X.Y.Z>"
    exit 1
}

if ($Version -eq 'nightly') {
    & (Join-Path $PSScriptRoot 'release-nightly.ps1') -AllowDirty:$AllowDirty
    exit $LASTEXITCODE
}

Import-Module (Join-Path $ProjectRoot 'cameraunlock-core/powershell/ReleaseWorkflow.psm1') -Force

# Mirrors New-ChangelogFromCommits' insertion so a -Force maintenance entry
# lands in the same place with the same shape.
function Add-MaintenanceChangelogEntry {
    param([string]$Path, [string]$NewVersion)
    $date = Get-Date -Format 'yyyy-MM-dd'
    $entry = "## [$NewVersion] - $date`n`n### Changed`n`n- Maintenance release (no user-facing changes).`n`n"
    $changelog = Get-Content $Path -Raw
    if ($changelog -match '(?s)(# Changelog.*?)(## \[)') {
        $changelog = $changelog -replace '(?s)(# Changelog.*?\n\n)', "`$1$entry"
    } else {
        $changelog = $changelog -replace '(?s)(# Changelog.*?\n)', "`$1$entry"
    }
    $changelog = $changelog.TrimEnd() + "`n"
    Set-Content $Path $changelog -NoNewline
}

function Write-NoBom {
    param([string]$Path, [string]$Text)
    [System.IO.File]::WriteAllText($Path, $Text, (New-Object System.Text.UTF8Encoding $false))
}

# --- 1. Resolve and validate the target version ------------------------
# src/version.h (MEHT_VERSION) is the canonical version the build and
# packager read; CMakeLists.txt's project() version is kept in lockstep.
$versionHeaderPath = Join-Path $ProjectRoot 'src/version.h'
$versionHeaderText = Get-Content $versionHeaderPath -Raw
if ($versionHeaderText -notmatch '#define MEHT_VERSION "(\d+\.\d+\.\d+)"') {
    Write-Error "Could not parse current version from src/version.h"
    exit 1
}
$current = $Matches[1]

try {
    $target = Resolve-ReleaseVersion -Argument $Version -CurrentVersion $current
} catch {
    Write-Error $_.Exception.Message
    exit 1
}

# --- 2. Preconditions (these stand in for interactive confirmation) ----
$branch = (git -C $ProjectRoot rev-parse --abbrev-ref HEAD).Trim()
if ($branch -ne 'main') {
    Write-Error "Releases must run on 'main' (currently on '$branch')."
    exit 1
}

if (-not $AllowDirty) {
    $status = git -C $ProjectRoot status --porcelain
    if ($status) {
        Write-Error "Working tree is not clean. Commit or stash changes before releasing."
        exit 1
    }
}

$tag = "v$target"
if (git -C $ProjectRoot tag --list $tag) {
    Write-Error "Tag $tag already exists."
    exit 1
}

Write-Host "Releasing $current -> $target" -ForegroundColor Cyan

# --- 3. Changelog from commits since the last tag ----------------------
# This is the gate that aborts when there are no user-facing commits, so run
# it BEFORE mutating any version files or building - a failure here then
# leaves a clean tree instead of stranding a half-applied version bump with
# no tag.
$changelogPath = Join-Path $ProjectRoot 'CHANGELOG.md'
Write-Host "Generating CHANGELOG from commits..." -ForegroundColor Cyan
$hasTags = git -C $ProjectRoot tag -l 2>$null
if (-not $hasTags) {
    # First release - ensure a baseline CHANGELOG exists.
    if (-not (Test-Path $changelogPath)) {
        $date = Get-Date -Format 'yyyy-MM-dd'
        Write-NoBom -Path $changelogPath -Text "# Changelog`n`n## [$target] - $date`n`nFirst release.`n"
    }
} else {
    try {
        $changelogArgs = @{
            ChangelogPath = $changelogPath
            Version       = $target
            ArtifactPaths = @('src/', 'cameraunlock-core', 'scripts/')
        }
        New-ChangelogFromCommits @changelogArgs | Out-Null
    } catch {
        if (-not $Force) {
            Write-Error $_.Exception.Message
            Write-Host 'No user-facing changes to release. Re-run with -Force for a maintenance release.' -ForegroundColor Yellow
            exit 1
        }
        Write-Host 'No user-facing commits since last tag - writing maintenance entry (-Force).' -ForegroundColor Yellow
        Add-MaintenanceChangelogEntry -Path $changelogPath -NewVersion $target
    }
}

# --- 4. Bump the canonical version (src/version.h) + keep CMakeLists.txt in sync
$versionHeaderText = $versionHeaderText -replace '#define MEHT_VERSION "\d+\.\d+\.\d+"', "#define MEHT_VERSION `"$target`""
Write-NoBom -Path $versionHeaderPath -Text $versionHeaderText

$cmakePath = Join-Path $ProjectRoot 'CMakeLists.txt'
$cmakeText = Get-Content $cmakePath -Raw
$cmakeText = $cmakeText -replace 'project\(MirrorsEdgeHeadTracking VERSION \d+\.\d+\.\d+', "project(MirrorsEdgeHeadTracking VERSION $target"
Write-NoBom -Path $cmakePath -Text $cmakeText

# --- 5. Release-config build -------------------------------------------
Write-Host "Building release configuration..." -ForegroundColor Cyan
pixi run build
if ($LASTEXITCODE -ne 0) {
    Write-Error "Release build failed."
    exit 1
}

# --- 6. Commit the version bump + changelog ----------------------------
git -C $ProjectRoot add src/version.h CMakeLists.txt CHANGELOG.md
git -C $ProjectRoot commit -m "Release v$target"
if ($LASTEXITCODE -ne 0) { Write-Error "git commit failed."; exit 1 }

# --- 7. Annotated tag --------------------------------------------------
git -C $ProjectRoot tag -a $tag -m "Release v$target"
if ($LASTEXITCODE -ne 0) { Write-Error "git tag failed."; exit 1 }

# --- 8. Push commits + tag (triggers .github/workflows/release.yml) ----
git -C $ProjectRoot push origin HEAD
if ($LASTEXITCODE -ne 0) { Write-Error "git push (commits) failed."; exit 1 }
git -C $ProjectRoot push origin $tag
if ($LASTEXITCODE -ne 0) { Write-Error "git push (tag) failed."; exit 1 }

Write-Host "Released $tag" -ForegroundColor Green
