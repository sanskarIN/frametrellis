# SPDX-License-Identifier: GPL-3.0-or-later
# Pure, locale-independent crash/storage event time comparison.
# Dot-source this file from the collector and its network-free regression test.
function ConvertTo-FrameTrellisEventInstant {
    [CmdletBinding()]
    param([Parameter(Mandatory)] [string] $Timestamp)
    # DateTimeOffset preserves the UTC offset; DateTime.Parse can silently
    # convert an ISO Z timestamp into a local wall-clock value.
    return [datetimeoffset]::Parse(
        $Timestamp, [Globalization.CultureInfo]::InvariantCulture,
        [Globalization.DateTimeStyles]::None
    )
}

function Test-FrameTrellisCrashProximity {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [datetimeoffset] $EventInstant,
        [Parameter(Mandatory)] [datetimeoffset] $CrashInstant,
        [ValidateRange(1, 30)] [int] $Minutes = 5
    )
    # DateTimeOffset subtraction normalizes the offsets of both instants.
    return [math]::Abs(($EventInstant - $CrashInstant).TotalMinutes) -le $Minutes
}
