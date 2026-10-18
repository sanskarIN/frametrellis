# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#
# Self-contained Windows package boundary regressions; no Pester or built
# packages required. Run with pwsh -NoProfile -File packaging/tests/test_verify_module_paths.ps1.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot '../windows/module-paths.ps1')

$package = Join-Path ([System.IO.Path]::GetTempPath()) 'FrameTrellis smoke'
$windows = $env:SystemRoot
if (-not $windows) { throw 'This regression suite must run on Windows.' }
$allowed = @($package, $windows)
$checks = 0

function Assert-ModulePath([string] $Name, [string] $Candidate, [bool] $Expected) {
    $actual = Test-ModulePathAllowed -ModulePath $Candidate -AllowedRoots $allowed
    if ($actual -ne $Expected) {
        throw "$Name failed: expected $Expected, received $actual for '$Candidate'"
    }
    $script:checks++
    Write-Host "PASS $Name"
}

Assert-ModulePath 'package root DLL' (Join-Path $package 'Qt6Core.dll') $true
Assert-ModulePath 'nested package plugin' (Join-Path $package 'plugins/platforms/qwindows.dll') $true
Assert-ModulePath 'case insensitive package path' ((Join-Path $package 'BIN/QT6CORE.DLL').ToUpperInvariant()) $true
Assert-ModulePath 'Windows System32 DLL' (Join-Path $windows 'System32/kernel32.dll') $true
Assert-ModulePath 'package prefix spoofing' (Join-Path "${package}-other" 'Qt6Core.dll') $false
Assert-ModulePath 'Windows directory prefix spoofing' (Join-Path "${windows}-other" 'System32/kernel32.dll') $false
Assert-ModulePath 'traversal escaping package' (Join-Path $package '../outside.dll') $false
Assert-ModulePath 'DLL from unrelated directory' (Join-Path ([System.IO.Path]::GetTempPath()) 'unrelated.dll') $false

Write-Host "All $checks module-path boundary checks passed."
