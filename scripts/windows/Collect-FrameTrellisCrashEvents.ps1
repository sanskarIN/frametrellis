# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
# Read-only, local Windows event-timeline correlation for native export crashes.
# This script does NOT modify event logs, configure WER or upload private data.
[CmdletBinding()]
param(
    [datetime] $Since = (Get-Date).AddDays(-7),
    [datetime] $Until = (Get-Date),
    [ValidateRange(1, 30)] [int] $CorrelationMinutes = 5,
    [string] $OutputDirectory = (Join-Path $env:LOCALAPPDATA 'FrameTrellis\Diagnostics')
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) {
    throw 'Windows event correlation requires a Windows computer.'
}
if ($Since -ge $Until) {
    throw 'Since must be earlier than Until.'
}
if (($Until - $Since).TotalDays -gt 31) {
    throw 'Limit one report to 31 days to keep event queries bounded.'
}

function Read-EventWindow([hashtable] $Filter, [int] $MaxEvents) {
    try {
        return @(Get-WinEvent -FilterHashtable $Filter -MaxEvents $MaxEvents -ErrorAction Stop)
    }
    catch {
        # An empty Windows event query is not a failure, but permission errors,
        # unavailable event logs and other failures must remain visible.
        if ($_.FullyQualifiedErrorId -match '^NoMatchingEventsFound') {
            return @()
        }
        throw
    }
}

$application = @(Read-EventWindow @{
    LogName = 'Application'
    Id = @(1000, 1001)
    StartTime = $Since
    EndTime = $Until
} 3000)

$crashes = @(
    foreach ($event in $application) {
        $message = [string] $event.Message
        $program = [regex]::Match(
            $message, '(?i)frametrellis(?:-render)?\.exe')
        if (-not $program.Success) {
            continue
        }
        $exception = [regex]::Match(
            $message, '(?i)Exception code:\s*(0x[0-9a-f]{8})')
        $module = [regex]::Match(
            $message, '(?i)Faulting module name:\s*([^\s,]+)')
        # Only emit limited structured metadata. Windows raw event messages
        # may include usernames, full executable paths and media filenames.
        [pscustomobject]@{
            timeUtc = $event.TimeCreated.ToUniversalTime().ToString('o')
            process = $program.Value.ToLowerInvariant()
            provider = [string] $event.ProviderName
            eventId = [int] $event.Id
            exceptionCode = if ($exception.Success) {
                $exception.Groups[1].Value.ToLowerInvariant()
            } else { $null }
            faultModule = if ($module.Success) {
                # Never persist a full drive/user path if WER emits one
                # instead of the usual faulting DLL basename.
                [IO.Path]::GetFileName($module.Groups[1].Value)
            } else { $null }
        }
    }
)

# These system records are possible I/O, disk, file-system, storage-controller
# and hardware warnings. A nearby event does NOT establish crash causation.
$system = @(Read-EventWindow @{
    LogName = 'System'
    Id = @(7, 11, 15, 46, 51, 55, 57, 129, 153, 157, 161)
    StartTime = $Since
    EndTime = $Until
} 3000)
$providerNames = @(
    'disk', 'Ntfs', 'Microsoft-Windows-Ntfs', 'storahci', 'stornvme',
    'volmgr', 'Microsoft-Windows-StorPort', 'Microsoft-Windows-WHEA-Logger'
)
# Compare offset-aware instants rather than local wall-clock ticks.
# These pure helpers are exercised by the Windows package CI regression.
. (Join-Path $PSScriptRoot 'FrameTrellisCrashTime.ps1')
$crashInstants = @(
    foreach ($crash in $crashes) {
        ConvertTo-FrameTrellisEventInstant -Timestamp $crash.timeUtc
    }
)
$signals = @(
    foreach ($event in $system) {
        if ([string] $event.ProviderName -notin $providerNames) {
            continue
        }
        $near = $false
        $eventInstant = [datetimeoffset]::new($event.TimeCreated)
        foreach ($crashInstant in $crashInstants) {
            if (Test-FrameTrellisCrashProximity -EventInstant $eventInstant -CrashInstant $crashInstant -Minutes $CorrelationMinutes) {
                $near = $true
                break
            }
        }
        [pscustomobject]@{
            timeUtc = $event.TimeCreated.ToUniversalTime().ToString('o')
            provider = [string] $event.ProviderName
            eventId = [int] $event.Id
            level = [int] $event.Level
            nearFrameTrellisCrash = $near
        }
    }
)

$report = [pscustomobject]@{
    schemaVersion = 1
    generatedAtUtc = (Get-Date).ToUniversalTime().ToString('o')
    sinceUtc = $Since.ToUniversalTime().ToString('o')
    untilUtc = $Until.ToUniversalTime().ToString('o')
    correlationMinutes = $CorrelationMinutes
    applicationEventsMatched = @($crashes).Count
    systemSignalsMatched = @($signals).Count
    applicationEventsTruncated = $application.Count -ge 3000
    systemQueryTruncated = $system.Count -ge 3000
    crashes = @($crashes)
    storageSignals = @($signals)
    limitations = @(
        'No native call stack or source-media investigation is performed.',
        'Missing events and nearby disk warnings do not prove or exclude a storage fault.',
        'Windows non-English event wording may leave exceptionCode or faultModule null.',
        'Event metadata is stored only locally; review it before sharing.'
    )
}
[void](New-Item -ItemType Directory -Path $OutputDirectory -Force)
$stamp = (Get-Date).ToUniversalTime().ToString('yyyyMMdd-HHmmss')
$filename = "frametrellis-crash-events-$stamp-$([guid]::NewGuid().ToString('N')).json"
$destination = Join-Path $OutputDirectory $filename
$json = $report | ConvertTo-Json -Depth 6
# CreateNew prevents overwriting any existing evidence, including during a
# simultaneous collection. Never send the report to a network endpoint.
$stream = [IO.File]::Open($destination, [IO.FileMode]::CreateNew,
                          [IO.FileAccess]::Write, [IO.FileShare]::None)
try {
    $bytes = [Text.UTF8Encoding]::new($false).GetBytes($json + "`n")
    $stream.Write($bytes, 0, $bytes.Length)
}
finally {
    $stream.Dispose()
}
Write-Host "FrameTrellis application events: $(@($crashes).Count)"
Write-Host "Storage/system signals in time window: $(@($signals).Count)"
Write-Host "Local metadata report: $destination"
Write-Warning 'A nearby storage event is a correlation, not proof of native crash causation.'
Write-Warning 'Keep crash dumps and raw Windows event messages private; this script uploads nothing.'
