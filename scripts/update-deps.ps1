#!/usr/bin/env pwsh
#Requires -Version 5.1
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 itsloopyo
# Manually refresh the vendored Ultimate ASI Loader. See AGENTS.md "Vendoring".
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$ProgressPreference    = 'SilentlyContinue'

$scriptDir  = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectDir = Split-Path -Parent $scriptDir
$vendorDir  = Join-Path $projectDir 'vendor/ultimate-asi-loader'

$module = Join-Path $projectDir 'cameraunlock-core/powershell/ModLoaderSetup.psm1'
if (-not (Test-Path $module)) {
    throw "ModLoaderSetup.psm1 not found at $module. Run 'git submodule update --init --recursive'."
}
Import-Module $module -Force

# Pinball FX is a 64-bit game, so we need the x64 Ultimate ASI Loader. The
# upstream asset is a zip; install.cmd and deploy.ps1 both copy a bare
# dinput8.dll (renamed to winmm.dll at the game), so we extract the single
# proxy DLL the zip contains rather than vendoring the zip itself.
$meta = Update-VendoredLoader `
    -Name 'ultimate-asi-loader' `
    -OutputDir $vendorDir `
    -OutputFileName 'Ultimate-ASI-Loader_x64.zip' `
    -Owner 'ThirteenAG' -Repo 'Ultimate-ASI-Loader' `
    -VersionPrefix 'v9.' `
    -AssetPattern '^Ultimate-ASI-Loader_x64\.zip$' `
    -LicenseName 'license'

$zipPath = Join-Path $vendorDir 'Ultimate-ASI-Loader_x64.zip'
$dllPath = Join-Path $vendorDir 'dinput8.dll'

Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip = [System.IO.Compression.ZipFile]::OpenRead($zipPath)
try {
    $entry = $zip.Entries | Where-Object { $_.Name -ieq 'dinput8.dll' } | Select-Object -First 1
    if (-not $entry) { throw "x64 Ultimate ASI Loader zip did not contain dinput8.dll" }
    $out = [IO.File]::Create($dllPath)
    try {
        $in = $entry.Open()
        try { $in.CopyTo($out) } finally { $in.Dispose() }
    } finally { $out.Dispose() }
} finally { $zip.Dispose() }

# Fail fast if we somehow extracted the wrong architecture - copying an x86 (or
# zip) proxy into an x64 game crashes the game on launch before our code runs.
$bytes = [IO.File]::ReadAllBytes($dllPath)
$peOffset = [BitConverter]::ToInt32($bytes, 0x3C)
$machine = [BitConverter]::ToUInt16($bytes, $peOffset + 4)
if ($machine -ne 0x8664) {
    throw ("Extracted ASI Loader is not x64 (machine=0x{0:X4}); refusing to vendor it." -f $machine)
}

# Record the hash of the artifact we actually commit, so its provenance can be
# checked without keeping the upstream zip around.
$dllHash = (Get-FileHash -Path $dllPath -Algorithm SHA256).Hash.ToLowerInvariant()
$readmePath = Join-Path $vendorDir 'README.md'
Add-Content -Path $readmePath -Encoding utf8 -Value @"

## Committed artifact

Only ``dinput8.dll`` is committed; the upstream zip is a download intermediate
and is deleted after extraction.

- File: ``dinput8.dll`` (extracted from the asset above, unmodified)
- SHA-256: ``$dllHash``

It is deployed to ``<game>/PinballFX/Binaries/Win64/winmm.dll`` as the ASI hook
slot: that is the directory holding the real shipping EXE (the install root only
carries a BootstrapPackagedGame shim), and PinballFX-Win64-Shipping.exe imports
WINMM.dll directly, so the proxy loads without any launch-option changes.
"@

# The zip is not shipped and not committed - install.cmd and the packager both
# consume the bare DLL.
Remove-Item $zipPath -Force

$licensePath = Join-Path $vendorDir 'LICENSE'
if (-not (Test-Path $licensePath)) {
    throw "Upstream LICENSE was not written to $vendorDir - refusing to vendor a loader binary without its licence."
}

Write-Host "Vendored x64 Ultimate ASI Loader ($($meta.Tag)) extracted to vendor/ultimate-asi-loader/dinput8.dll" -ForegroundColor Green
