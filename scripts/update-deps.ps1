#requires -Version 5.1
# Manual dev action: refresh the vendored Ultimate ASI Loader. Never run by CI
# or the build - bumping the bundled loader is a deliberate commit. See doctrine
# "Vendoring Third-Party Dependencies".
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'

$projectDir = Resolve-Path (Join-Path $PSScriptRoot '..')
Import-Module (Join-Path $projectDir 'cameraunlock-core/powershell/ModLoaderSetup.psm1') -Force

$vendorDir = Join-Path $projectDir 'vendor/ultimate-asi-loader'

# Fetch the x86 wrapper zip (Mirror's Edge is 32-bit). Pinned to v9.x.
$meta = Update-VendoredLoader `
    -Name 'ultimate-asi-loader' `
    -OutputDir $vendorDir `
    -OutputFileName 'Ultimate-ASI-Loader.zip' `
    -Owner 'ThirteenAG' -Repo 'Ultimate-ASI-Loader' `
    -VersionPrefix 'v9.' `
    -AssetPattern '^Ultimate-ASI-Loader\.zip$'

# Doctrine ASI wiring vendors the raw dinput8.dll, not the wrapper zip: extract
# it and drop the zip so install.cmd / the manifest reference a single DLL.
$zipPath = Join-Path $vendorDir 'Ultimate-ASI-Loader.zip'
$dllPath = Join-Path $vendorDir 'dinput8.dll'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip = [System.IO.Compression.ZipFile]::OpenRead($zipPath)
try {
    $entry = $zip.Entries | Where-Object { $_.Name -ieq 'dinput8.dll' } | Select-Object -First 1
    if (-not $entry) { throw "dinput8.dll not found in $($meta.AssetName)" }
    if (Test-Path $dllPath) { Remove-Item $dllPath -Force }
    [System.IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $dllPath, $true)
} finally {
    $zip.Dispose()
}
Remove-Item $zipPath -Force

# Update-VendoredLoader writes a README referencing the wrapper zip's hash, but
# the committed artifact is the extracted dinput8.dll. Rewrite the snapshot so
# the recorded SHA-256 matches the DLL actually on disk (see existing ASI mods).
$sha = (Get-FileHash -Path $dllPath -Algorithm SHA256).Hash.ToLower()
$readme = @(
    '# Ultimate ASI Loader (vendored)',
    '',
    "Bundled copy of Ultimate ASI Loader for Mirror's Edge, the install-time source of truth.",
    'Refresh manually with `pixi run update-deps`, then commit.',
    '',
    '## Snapshot',
    '',
    '- Upstream: https://github.com/ThirteenAG/Ultimate-ASI-Loader',
    "- Tag: ``$($meta.Tag)``",
    "- Commit: ``$($meta.CommitSha)``",
    "- Asset: ``$($meta.AssetName)``",
    "- dinput8.dll SHA-256: ``$sha``",
    "- Fetched at: $($meta.FetchedAt)",
    '',
    "``dinput8.dll`` is extracted from the upstream x86 zip untouched. install.cmd copies it",
    "into the Mirror's Edge exe dir as the ASI proxy slot the game loads."
) -join "`n"
Set-Content -Path (Join-Path $vendorDir 'README.md') -Value $readme -Encoding UTF8

Write-Host ("Vendored Ultimate ASI Loader {0} -> dinput8.dll (sha256 {1})" -f $meta.Tag, $sha.Substring(0,16)) -ForegroundColor Green
