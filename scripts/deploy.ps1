#!/usr/bin/env pwsh
#Requires -Version 5.1
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 itsloopyo
# Deploy the built PinballFXHeadTracking.asi into the game's exe directory for
# local testing.
#
# Usage: deploy.ps1 [GAME_PATH] [-Configuration Debug|Release]
# Game detection order matches install.cmd: explicit path -> PINBALL_FX_PATH
# env var -> Steam registry / library folders -> games.json.

param(
    [Parameter(Position = 0)]
    [string]$GamePath,
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectDir = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path

$asi = Join-Path $projectDir "build/$Configuration/PinballFXHeadTracking.asi"
if (-not (Test-Path $asi)) {
    Write-Error "Build output not found: $asi. Run 'pixi run build' first."
    exit 1
}

if (-not $GamePath) {
    Import-Module (Join-Path $projectDir 'cameraunlock-core/powershell/GamePathDetection.psm1') -Force
    $GamePath = Find-GamePath -GameId 'pinball-fx'
}

if (-not $GamePath -or -not (Test-Path $GamePath)) {
    Write-Error "Could not locate Pinball FX. Set PINBALL_FX_PATH or pass the install path as the first argument."
    exit 1
}

# The install root only holds a BootstrapPackagedGame shim (PinballFX.exe); the
# module the mod hooks - and so the directory the loader has to sit in - is the
# real shipping EXE's.
$exeDir = Join-Path $GamePath 'PinballFX\Binaries\Win64'
if (-not (Test-Path $exeDir)) {
    Write-Error "Expected exe directory not found: $exeDir"
    exit 1
}

Copy-Item $asi -Destination $exeDir -Force
Write-Host "Deployed: $asi -> $exeDir" -ForegroundColor Green

# PinballFX-Win64-Shipping.exe imports WINMM.dll statically and nothing else
# claims it, so that is the proxy slot. Matches ASI_LOADER_NAME in install.cmd.
$loaderTarget = Join-Path $exeDir 'winmm.dll'
if (-not (Test-Path $loaderTarget)) {
    $vendorLoader = Join-Path $projectDir 'vendor/ultimate-asi-loader/dinput8.dll'
    if (-not (Test-Path $vendorLoader)) {
        Write-Error "Vendored ASI loader missing: $vendorLoader. Run 'pixi run update-deps' and commit the result."
        exit 1
    }
    Copy-Item $vendorLoader -Destination $loaderTarget -Force
    Write-Host "Installed Ultimate ASI Loader as winmm.dll" -ForegroundColor Green
}
