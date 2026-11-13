# SPDX-License-Identifier: GPL-3.0-or-later
# No Event Viewer access or privileges. Test the collector's actual helpers.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '../../scripts/windows/FrameTrellisCrashTime.ps1')

$crash = ConvertTo-FrameTrellisEventInstant '2026-11-07T04:00:00.0000000Z'
$same = ConvertTo-FrameTrellisEventInstant '2026-11-07T09:30:00+05:30'
if ($crash.UtcDateTime.Ticks -ne $same.UtcDateTime.Ticks) {
    throw 'An equivalent non-UTC timestamp must represent the same instant'
}
if (-not (Test-FrameTrellisCrashProximity -EventInstant $same -CrashInstant $crash)) {
    throw 'Exact matching crash and storage event instants must correlate'
}
$within = ConvertTo-FrameTrellisEventInstant '2026-11-07T09:34:59+05:30'
if (-not (Test-FrameTrellisCrashProximity -EventInstant $within -CrashInstant $crash)) {
    throw 'A 4m59s delta in another timezone must correlate'
}
$outside = ConvertTo-FrameTrellisEventInstant '2026-11-07T09:35:01+05:30'
if (Test-FrameTrellisCrashProximity -EventInstant $outside -CrashInstant $crash) {
    throw 'A 5m01s delta must be rejected'
}
$previousDay = ConvertTo-FrameTrellisEventInstant '2026-11-06T23:59:00-04:00'
if (-not (Test-FrameTrellisCrashProximity -EventInstant $previousDay -CrashInstant $crash)) {
    throw 'Comparison across calendar days and UTC offsets failed'
}
$hoursApart = ConvertTo-FrameTrellisEventInstant '2026-11-07T09:30:00Z'
if (Test-FrameTrellisCrashProximity -EventInstant $hoursApart -CrashInstant $crash) {
    throw 'Wall-clock time was confused with the true UTC instant'
}
Write-Host 'PASS: crash event UTC/offset correlation regression'
