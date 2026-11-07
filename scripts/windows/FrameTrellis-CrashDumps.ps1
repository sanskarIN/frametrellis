# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#
# Opt-in Windows Error Reporting (WER) minidumps for FrameTrellis only.
# Run in an elevated PowerShell console:
#   .\scripts\windows\FrameTrellis-CrashDumps.ps1 -Enable
#   .\scripts\windows\FrameTrellis-CrashDumps.ps1 -Status
#   .\scripts\windows\FrameTrellis-CrashDumps.ps1 -Disable
# No dumps are uploaded or inspected by this script.
[CmdletBinding(DefaultParameterSetName = 'Status')]
param(
    [Parameter(Mandatory = $true, ParameterSetName = 'Enable')]
    [switch] $Enable,

    [Parameter(Mandatory = $true, ParameterSetName = 'Disable')]
    [switch] $Disable,

    [Parameter(ParameterSetName = 'Status')]
    [switch] $Status,

    [Parameter(ParameterSetName = 'Enable')]
    [ValidateRange(1, 10)]
    [int] $DumpCount = 3,

    [Parameter(ParameterSetName = 'Enable')]
    [string] $DumpFolder = (Join-Path $env:LOCALAPPDATA 'FrameTrellis\CrashDumps')
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not [Environment]::OSVersion.Platform.ToString().Equals('Win32NT')) {
    throw 'This diagnostic helper can only run on Windows.'
}

$werBase = 'HKLM:\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps'
$appNames = @('frametrellis.exe', 'frametrellis-render.exe')
$markerName = 'FrameTrellisDiagnosticManaged'

function Assert-Administrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw 'Open PowerShell using Run as administrator for -Enable or -Disable.'
    }
}

function Get-ManagedState([string] $key) {
    if (-not (Test-Path -LiteralPath $key)) {
        return $false
    }
    $values = Get-ItemProperty -LiteralPath $key
    return ($null -ne $values.PSObject.Properties[$markerName] -and
            [string]$values.$markerName -eq '1')
}

if ($PSCmdlet.ParameterSetName -eq 'Status') {
    foreach ($appName in $appNames) {
        $key = Join-Path $werBase $appName
        if (-not (Test-Path -LiteralPath $key)) {
            Write-Output "$appName : no application-specific WER configuration"
            continue
        }
        $values = Get-ItemProperty -LiteralPath $key
        $managed = Get-ManagedState $key
        Write-Output "$appName : present (managed by FrameTrellis: $managed)"
        $dumpType = if ($null -ne $values.PSObject.Properties['DumpType']) {
            $values.DumpType
        } else { 'default' }
        $dumpCount = if ($null -ne $values.PSObject.Properties['DumpCount']) {
            $values.DumpCount
        } else { 'default' }
        $dumpFolder = if ($null -ne $values.PSObject.Properties['DumpFolder']) {
            $values.DumpFolder
        } else { 'default' }
        Write-Output "  DumpType: $dumpType; DumpCount: $dumpCount"
        Write-Output "  DumpFolder: $dumpFolder"
    }
    Write-Output 'No configuration was modified. Existing system-wide WER rules may also apply.'
    return
}

Assert-Administrator

if ($Enable) {
    # Do not silently replace existing WER settings created by users or other tools.
    foreach ($appName in $appNames) {
        $key = Join-Path $werBase $appName
        if ((Test-Path -LiteralPath $key) -and -not (Get-ManagedState $key)) {
            throw "Existing WER settings for $appName were not created by this tool; refusing to overwrite."
        }
    }

    $absoluteFolder = [IO.Path]::GetFullPath(
        [Environment]::ExpandEnvironmentVariables($DumpFolder))
    if ($absoluteFolder.StartsWith('\\')) {
        throw 'Use a local folder for crash dumps, not a network share.'
    }
    New-Item -ItemType Directory -Path $absoluteFolder -Force | Out-Null
    New-Item -Path $werBase -Force | Out-Null
    foreach ($appName in $appNames) {
        $key = Join-Path $werBase $appName
        New-Item -Path $key -Force | Out-Null
        New-ItemProperty -LiteralPath $key -Name 'DumpFolder' -PropertyType ExpandString -Value $absoluteFolder -Force | Out-Null
        New-ItemProperty -LiteralPath $key -Name 'DumpType' -PropertyType DWord -Value 1 -Force | Out-Null
        New-ItemProperty -LiteralPath $key -Name 'DumpCount' -PropertyType DWord -Value $DumpCount -Force | Out-Null
        New-ItemProperty -LiteralPath $key -Name $markerName -PropertyType String -Value '1' -Force | Out-Null
        Write-Output "Enabled local minidumps for $appName"
    }
    Write-Output "Dump folder: $absoluteFolder"
    Write-Warning 'Crash dumps can contain private paths and program data. Keep them local and review before sharing.'
    Write-Output 'No crash dump will exist until a crash occurs. Run -Disable afterward.'
}
elseif ($Disable) {
    foreach ($appName in $appNames) {
        $key = Join-Path $werBase $appName
        if (Get-ManagedState $key) {
            Remove-Item -LiteralPath $key -Recurse -Force
            Write-Output "Removed FrameTrellis WER settings for $appName"
        }
        elseif (Test-Path -LiteralPath $key) {
            Write-Warning "Preserved unrelated existing WER settings for $appName"
        }
        else {
            Write-Output "No FrameTrellis WER settings for $appName"
        }
    }
    Write-Output 'Crash dump files were not deleted. Remove them manually after investigation.'
}
