#!/usr/bin/env pwsh
#Requires -Version 5.1
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 itsloopyo
<#
.SYNOPSIS
  Compare the installed Pinball FX EXE's PE fingerprint against the values
  committed in src/builds/steam_offsets.cpp.
.DESCRIPTION
  Same three-field check (TimeDateStamp / SizeOfImage / CheckSum) that
  builds::SelectProfile runs at mod load time. Run this first when a user
  reports the "staying dormant" log line, and after a Steam patch lands, to
  find out whether RVAs need rederiving before shipping a new mod version.

  A Steam buildid change is not proof the EXE changed - asset-only patches
  move the buildid and leave the shipping EXE untouched - so "MATCH" after a
  patch is a normal, no-work-needed outcome.

  Exit codes:
    0 = match (no rederivation needed)
    1 = mismatch (rederive + ship a new release)
    2 = could not locate the EXE
.PARAMETER ExePath
  Direct path to PinballFX-Win64-Shipping.exe. If omitted, falls back to
  cameraunlock-core's Find-GamePath (env var / Steam manifest / etc).
#>
param(
    [Parameter(Position=0, Mandatory=$false)]
    [string]$ExePath = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectDir = Split-Path -Parent $PSScriptRoot
Import-Module (Join-Path $projectDir 'cameraunlock-core/powershell/GamePathDetection.psm1') -Force

function Resolve-ExePath {
    param([string]$Provided = '')
    if ($Provided) {
        if (-not (Test-Path $Provided)) {
            throw "EXE not found at: $Provided"
        }
        return (Resolve-Path $Provided).Path
    }
    $config = Get-GameConfig -GameId 'pinball-fx'
    $gameRoot = Find-GamePath -GameId 'pinball-fx'
    if (-not $gameRoot) {
        throw "Could not locate Pinball FX. Pass the EXE path positionally or set `$env:PINBALL_FX_PATH."
    }
    $exe = Join-Path $gameRoot $config.Executable
    if (-not (Test-Path $exe)) {
        throw "EXE not found at expected location: $exe"
    }
    return $exe
}

function Read-PEFingerprint {
    param([string]$Path)
    $stream = [System.IO.File]::OpenRead($Path)
    try {
        $reader = New-Object System.IO.BinaryReader($stream)
        $stream.Position = 0x3c
        $e_lfanew = $reader.ReadUInt32()
        $stream.Position = $e_lfanew
        $sig = $reader.ReadUInt32()
        if ($sig -ne 0x00004550) {
            throw ("Not a PE file: signature 0x{0:x} at e_lfanew=0x{1:x}" -f $sig, $e_lfanew)
        }
        $stream.Position = $e_lfanew + 8
        $tds = $reader.ReadUInt32()
        $stream.Position = $e_lfanew + 4 + 20 + 0x38
        $size = $reader.ReadUInt32()
        $stream.Position = $e_lfanew + 4 + 20 + 0x40
        $csum = $reader.ReadUInt32()
        return [pscustomobject]@{
            TimeDateStamp = $tds
            SizeOfImage   = $size
            CheckSum      = $csum
        }
    } finally {
        $stream.Dispose()
    }
}

function Read-ExpectedFingerprints {
    param([string]$ProjectDir)
    # Each profile ships its Fingerprint as a struct initialiser literal:
    #   /* Fingerprint */ { 0x6a0e94f2u, 0x06703000u, 0x0616dbccu },
    # preceded by a /* Name */ "store-platform-YYYYMMDD" line. Collect every
    # profile, mirroring builds::SelectProfile.
    $profiles = @()
    $cppPath = Join-Path $ProjectDir 'src/builds/steam_offsets.cpp'
    if (-not (Test-Path $cppPath)) {
        throw "steam_offsets.cpp not found at $cppPath"
    }
    $cpp = Get-Content -Raw $cppPath
    $pattern = '(?s)Name\s*\*/\s*"([^"]+)".*?Fingerprint\s*\*/\s*\{\s*0x([0-9a-fA-F]+)u?\s*,\s*0x([0-9a-fA-F]+)u?\s*,\s*0x([0-9a-fA-F]+)u?\s*\}'
    $found = [regex]::Matches($cpp, $pattern)
    if ($found.Count -eq 0) {
        throw "No profile fingerprints found in steam_offsets.cpp"
    }
    foreach ($m in $found) {
        $profiles += [pscustomobject]@{
            Name          = $m.Groups[1].Value
            TimeDateStamp = [Convert]::ToUInt32($m.Groups[2].Value, 16)
            SizeOfImage   = [Convert]::ToUInt32($m.Groups[3].Value, 16)
            CheckSum      = [Convert]::ToUInt32($m.Groups[4].Value, 16)
        }
    }
    return $profiles
}

try {
    $exe = Resolve-ExePath -Provided $ExePath
} catch {
    Write-Host "ERROR: $_" -ForegroundColor Red
    exit 2
}

Write-Host "EXE:      $exe"
$running  = Read-PEFingerprint -Path $exe
$profiles = Read-ExpectedFingerprints -ProjectDir $projectDir

Write-Host ("Running:  ts=0x{0:x8} size=0x{1:x8} csum=0x{2:x8}" -f $running.TimeDateStamp, $running.SizeOfImage, $running.CheckSum)
foreach ($p in $profiles) {
    Write-Host ("Profile:  ts=0x{0:x8} size=0x{1:x8} csum=0x{2:x8}  {3}" -f $p.TimeDateStamp, $p.SizeOfImage, $p.CheckSum, $p.Name)
}

$match = $profiles | Where-Object {
    $running.TimeDateStamp -eq $_.TimeDateStamp `
    -and $running.SizeOfImage -eq $_.SizeOfImage `
    -and $running.CheckSum -eq $_.CheckSum
} | Select-Object -First 1

if ($match) {
    Write-Host ("MATCH - profile {0}, no rederivation needed." -f $match.Name) -ForegroundColor Green
    exit 0
}

Write-Host "MISMATCH: EXE matches no committed profile." -ForegroundColor Yellow
$newest = $profiles | Sort-Object TimeDateStamp -Descending | Select-Object -First 1
if ($running.TimeDateStamp -gt $newest.TimeDateStamp) {
    Write-Host "  The EXE is NEWER than every committed profile - the game was patched."
} elseif ($running.TimeDateStamp -lt $newest.TimeDateStamp) {
    Write-Host "  The EXE is OLDER than the newest profile - let Steam finish updating."
} else {
    Write-Host "  Same build date, different size/checksum - a repacked or modified EXE."
}
Write-Host ""
Write-Host "  Paste-ready profile stub for src/builds/steam_offsets.cpp:"
Write-Host ("    /* Fingerprint */ {{ 0x{0:X8}u, 0x{1:X8}u, 0x{2:X8}u }}," -f $running.TimeDateStamp, $running.SizeOfImage, $running.CheckSum)
Write-Host ("    /* Name        */ `"steam-win64-{0}`"," -f ([DateTimeOffset]::FromUnixTimeSeconds($running.TimeDateStamp).UtcDateTime.ToString('yyyyMMdd')))
Write-Host ""
Write-Host "  ADD a new build profile - never edit an existing one (AGENTS.md append-only policy)."
exit 1
