#requires -Version 5.1
# Manual dev action: refresh the vendored Ultimate ASI Loader, strip the
# third-party DLLs it carries as resources, and rewrite the vendor README.
# Never run by CI or the build - bumping the bundled loader is a deliberate
# commit. See doctrine "Vendoring Third-Party Dependencies".
#
# The extracted DLL is NOT vendored as it comes: the x86 build embeds
# binkw32.dll (RAD Game Tools, proprietary), wndmode.dll (VEG / menopem, no
# licence) and vorbisfile.dll (Xiph.Org) as RCDATA resources, and the installer
# ZIP would redistribute all three. strip-loader-payload.ps1 zeroes them
# before the copy is hashed and committed. Never skip that step.
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

$upstreamSha = (Get-FileHash -Path $dllPath -Algorithm SHA256).Hash.ToLower()

Write-Host "Stripping the loader's embedded third-party DLLs..." -ForegroundColor Cyan
$strip = Join-Path $PSScriptRoot 'strip-loader-payload.ps1'
& $strip -Path $dllPath
& $strip -Path $dllPath -VerifyOnly   # throws if anything survived

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
    "- Upstream dinput8.dll SHA-256: ``$upstreamSha``",
    "- Vendored dinput8.dll SHA-256: ``$sha`` (after the strip below)",
    "- Fetched at: $($meta.FetchedAt)",
    '',
    "``dinput8.dll`` is extracted from the upstream x86 zip. install.cmd copies it into the",
    "Mirror's Edge exe dir as the ASI proxy slot the game loads.",
    '',
    '## Modified: third-party payload stripped',
    '',
    'The upstream x86 loader carries three complete third-party DLLs as RCDATA resources,',
    'so that a user who renames it over one of those libraries still gets the original',
    'exports, plus the ini template one of them reads:',
    '',
    '- `binkw32.dll` - RAD Game Tools, Inc., Bink and Smacker 1.994i. Proprietary',
    '  middleware licensed per title; we have no right to redistribute it.',
    '- `wndmode.dll` - DirectX Windower Embedded v2.3, (C) 2008 VEG, (C) 2004 menopem.',
    '  No licence accompanies it.',
    '- `vorbisfile.dll` - Xiph.Org, BSD-3-Clause. Redistributable only with its notice.',
    '',
    '`scripts/strip-loader-payload.ps1` zeroes all three, and the windower ini template,',
    'before the file is committed. Only the `.rsrc` section changes: the loader code, its',
    'imports, relocations and appended PDB are byte-identical to upstream. Nothing in this',
    'mod can reach the stripped resources - the two library payloads are keyed off the',
    "loader's own filename, and we deploy it as `dinput8.dll`, while the windower needs a",
    '`wndmode.ini` we never ship. MIT permits the modification; it is recorded here and in',
    'THIRD-PARTY-NOTICES.md so this copy is not mistaken for stock upstream.'
) -join "`n"
Set-Content -Path (Join-Path $vendorDir 'README.md') -Value $readme -Encoding UTF8

Write-Host ("Vendored Ultimate ASI Loader {0} -> dinput8.dll (sha256 {1})" -f $meta.Tag, $sha.Substring(0,16)) -ForegroundColor Green
