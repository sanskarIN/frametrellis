# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#
# Windows package smoke-test helpers. Keep these functions independent of the
# verifier's side effects so that their directory-boundary rules can be tested.

function Test-PathWithinDirectory {
    param(
        [Parameter(Mandatory)] [string] $Candidate,
        [Parameter(Mandatory)] [string] $Directory
    )

    # GetFullPath resolves ".." segments without requiring files to exist.
    # Comparing with the directory separator prevents accepting a sibling such
    # as "FrameTrellis-unsafe" as part of "FrameTrellis".
    $path = [System.IO.Path]::GetFullPath($Candidate)
    $root = [System.IO.Path]::GetFullPath($Directory).TrimEnd(
        [System.IO.Path]::DirectorySeparatorChar, [System.IO.Path]::AltDirectorySeparatorChar)
    $comparison = [System.StringComparison]::OrdinalIgnoreCase
    return $path.Equals($root, $comparison) -or
        $path.StartsWith($root + [System.IO.Path]::DirectorySeparatorChar, $comparison)
}

function Test-ModulePathAllowed {
    param(
        [Parameter(Mandatory)] [string] $ModulePath,
        [Parameter(Mandatory)] [string[]] $AllowedRoots
    )

    foreach ($root in $AllowedRoots) {
        if (Test-PathWithinDirectory -Candidate $ModulePath -Directory $root) {
            return $true
        }
    }
    return $false
}

function Get-ForeignModules {
    param(
        [Parameter(Mandatory)] [System.Diagnostics.Process] $Process,
        [Parameter(Mandatory)] [string] $PackageRoot
    )

    $allowed = @($PackageRoot, $env:SystemRoot,
        "$env:ProgramData\Microsoft\Windows Defender",
        "$env:ProgramFiles\Windows Defender")
    $Process.Refresh()
    foreach ($module in $Process.Modules) {
        $path = $module.FileName
        if (-not (Test-ModulePathAllowed -ModulePath $path -AllowedRoots $allowed)) {
            Write-Output $path
        }
    }
}
