# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
<#
.SYNOPSIS
    Verifies the FrameTrellis Windows packages on a machine without development tools.

.DESCRIPTION
    Runs with nothing but Windows on the PATH and checks that
      * the portable ZIP contains the application, its license files and the component list,
      * the engine self-test passes from the extracted folder (sample media, editing, export),
      * the editor starts, opens the self-test project and renders its window, while loading
        libraries only from the package and from Windows itself,
      * the installer installs for the current user, registers the uninstaller, the Start menu
        shortcut and the .ftproj association, the installed copy passes the self-test, and the
        uninstaller removes it again.

    Every check prints PASS or FAIL; the script exits with 1 if any check failed.

.EXAMPLE
    pwsh packaging/windows/verify.ps1 -Zip dist/FrameTrellis-1.0.0-windows-x64.zip `
        -Installer dist/FrameTrellis-1.0.0-windows-x64-setup.exe -WorkDir "$env:RUNNER_TEMP\verify"
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string] $Zip,
    [string] $Installer,
    [Parameter(Mandatory)] [string] $WorkDir,
    [string] $Label = 'windows',
    [string] $Artifacts,
    [ValidateRange(1, 5)] [int] $PortableGuiRuns = 1
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$AppId = '{653A63AF-5BF8-4BD3-95A9-1B95BB5C7E26}'
$Zip = (Resolve-Path $Zip).Path
if ($Installer) { $Installer = (Resolve-Path $Installer).Path }
# A space in the path catches quoting mistakes.
$WorkDir = Join-Path $WorkDir 'verify packages'
if (-not $Artifacts) { $Artifacts = Join-Path $WorkDir 'artifacts' }
New-Item -ItemType Directory -Force -Path $WorkDir, $Artifacts | Out-Null
Write-Host "Verification host OS architecture: $([System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture)"
Write-Host "Verification process architecture: $([System.Runtime.InteropServices.RuntimeInformation]::ProcessArchitecture)"
Write-Host "Independent portable GUI launches required: $PortableGuiRuns"

# Only Windows itself may be found on the PATH: the package has to bring everything else.
$env:PATH = @("$env:SystemRoot\System32", $env:SystemRoot, "$env:SystemRoot\System32\Wbem",
    "$env:SystemRoot\System32\WindowsPowerShell\v1.0") -join ';'
foreach ($name in 'QT_PLUGIN_PATH', 'QML_IMPORT_PATH', 'QML2_IMPORT_PATH', 'QT_QPA_PLATFORM',
    'MLT_REPOSITORY', 'MLT_DATA', 'FRAMETRELLIS_MLT_REPOSITORY') {
    Remove-Item "Env:$name" -ErrorAction SilentlyContinue
}

$failures = [System.Collections.Generic.List[string]]::new()
function Check([bool] $Condition, [string] $What) {
    if ($Condition) {
        Write-Host "PASS $What"
    } else {
        Write-Host "FAIL $What"
        $failures.Add($What)
    }
}

function Invoke-Console([string] $Exe, [string[]] $Arguments, [int] $TimeoutSeconds = 600) {
    $info = [System.Diagnostics.ProcessStartInfo]::new($Exe)
    foreach ($argument in $Arguments) { $info.ArgumentList.Add($argument) }
    $info.UseShellExecute = $false
    # Without a console, Qt sends log messages to the debugger; keep them in the job log.
    $info.Environment['QT_FORCE_STDERR_LOGGING'] = '1'
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $process = [System.Diagnostics.Process]::Start($info)
    $out = $process.StandardOutput.ReadToEndAsync()
    $err = $process.StandardError.ReadToEndAsync()
    if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
        $process.Kill($true)
        throw "$Exe did not finish within $TimeoutSeconds seconds"
    }
    $process.WaitForExit()
    [pscustomobject]@{ ExitCode = $process.ExitCode; Output = $out.Result; Errors = $err.Result }
}

function Test-SelfTest([string] $Folder, [string] $What) {
    $render = Join-Path $Folder 'frametrellis-render.exe'
    $work = Join-Path $WorkDir "self-test $What"
    $result = Invoke-Console $render @('--self-test', $work)
    Write-Host $result.Output
    if ($result.ExitCode -ne 0 -and $result.Errors) { Write-Host $result.Errors }
    Check ($result.ExitCode -eq 0 -and $result.Output -match 'Self-test passed') "$What engine self-test"
    return (Join-Path $work 'self-test.ftproj')
}

function Get-ForeignModules([System.Diagnostics.Process] $Process, [string] $Root) {
    $allowed = @($Root, $env:SystemRoot, "$env:ProgramData\Microsoft\Windows Defender",
        "$env:ProgramFiles\Windows Defender")
    $Process.Refresh()
    $Process.Modules | ForEach-Object FileName | Where-Object {
        $path = $_
        -not ($allowed | Where-Object { $path.StartsWith($_, [System.StringComparison]::OrdinalIgnoreCase) })
    }
}

function Test-Editor([string] $Folder, [string] $Project, [string] $What) {
    $exe = Join-Path $Folder 'frametrellis.exe'
    $shot = Join-Path $Artifacts "screenshot-$Label-$What.png"
    $arguments = @('--screenshot', "`"$shot`"", '--screenshot-delay', '15000', "`"$Project`"")
    $process = Start-Process -FilePath $exe -ArgumentList $arguments -PassThru
    $null = $process.Handle # keeps the exit code available after the process ends
    Start-Sleep -Seconds 10
    if (-not $process.HasExited) {
        try {
            $foreign = @(Get-ForeignModules $process $Folder)
            $count = @($process.Modules).Count
            Write-Host "The editor loaded $count modules."
            if ($foreign.Count -gt 0) { $foreign | ForEach-Object { Write-Host "  outside the package: $_" } }
            Check ($foreign.Count -eq 0) "$What editor loads libraries only from the package and Windows"
        } catch {
            Write-Host "Could not list the editor's modules: $_"
        }
    }
    if (-not $process.WaitForExit(180000)) {
        Stop-Process -Id $process.Id -Force
        Check $false "$What editor finished within three minutes"
        return
    }
    Check ($process.ExitCode -eq 0) "$What editor started, opened the project and rendered its window (exit code $($process.ExitCode))"
    Check ((Test-Path $shot) -and (Get-Item $shot).Length -gt 20000) "$What editor screenshot saved"
    if ($process.ExitCode -ne 0) { Write-CrashReport $process.StartTime }
}

# Prints what Windows recorded about a crash (faulting module and offset) and the end of the
# editor's own log, so that a failure can be diagnosed from the job log alone.
function Write-CrashReport([datetime] $Since) {
    Start-Sleep -Seconds 3 # Windows Error Reporting writes its event shortly after the crash
    try {
        Get-WinEvent -FilterHashtable @{ LogName = 'Application'; ProviderName = 'Application Error'; StartTime = $Since } -ErrorAction Stop |
            Select-Object -First 3 | ForEach-Object { Write-Host $_.Message }
    } catch {
        Write-Host "No crash event was recorded: $_"
    }
    $logs = Join-Path $env:APPDATA 'FrameTrellis\FrameTrellis\logs'
    $latest = Get-ChildItem $logs -Filter *.log -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if ($latest) {
        Write-Host "Last lines of $($latest.FullName):"
        Get-Content $latest.FullName -Tail 40 | ForEach-Object { Write-Host "  $_" }
    }
}

# --- Portable ZIP ------------------------------------------------------------------------------
$portableRoot = Join-Path $WorkDir 'portable'
Expand-Archive -Path $Zip -DestinationPath $portableRoot -Force
$portable = @(Get-ChildItem $portableRoot -Directory)[0].FullName
Write-Host "Extracted $Zip to $portable"
foreach ($file in 'frametrellis.exe', 'frametrellis-render.exe', 'qt.conf', 'LICENSE.txt',
    'README.txt', 'BUNDLED_COMPONENTS.txt', 'SOURCES.txt', 'THIRD_PARTY_NOTICES.md',
    'lib\mlt\libmltcore.dll', 'lib\mlt\libmltavformat.dll', 'share\mlt\profiles',
    'plugins\platforms\qwindows.dll', 'plugins\imageformats\qjpeg.dll',
    'plugins\imageformats\qtiff.dll', 'plugins\imageformats\qwebp.dll', 'qml\QtQuick') {
    Check (Test-Path (Join-Path $portable $file)) "ZIP contains $file"
}
$licenseCount = @(Get-ChildItem (Join-Path $portable 'licenses') -Recurse -File).Count
Check ($licenseCount -gt 10) "ZIP contains license files ($licenseCount)"
$components = @(Get-Content (Join-Path $portable 'BUNDLED_COMPONENTS.txt') |
    ForEach-Object { if ($_ -match '^(mingw-w64-\S+)\s') { $Matches[1] } })
$unlicensed = @($components | Where-Object {
    -not (Get-ChildItem (Join-Path $portable "licenses\$_") -Recurse -File -ErrorAction SilentlyContinue) })
Check ($components.Count -gt 0 -and $unlicensed.Count -eq 0) `
    "every bundled component has license texts ($($components.Count) components$(if ($unlicensed) { '; missing: ' + ($unlicensed -join ', ') }))"
$version = (Get-Item (Join-Path $portable 'frametrellis.exe')).VersionInfo.ProductVersion
Write-Host "frametrellis.exe product version: $version"
Check ($Zip -match [regex]::Escape("FrameTrellis-$version-")) 'version resource matches the package name'

$help = Invoke-Console (Join-Path $portable 'frametrellis-render.exe') @('--version')
Check ($help.ExitCode -eq 0 -and $help.Output -match [regex]::Escape($version)) 'render helper reports its version'
$project = Test-SelfTest $portable 'portable'
if (Test-Path $project) {
    for ($run = 1; $run -le $PortableGuiRuns; ++$run) {
        $name = if ($PortableGuiRuns -eq 1) { 'portable' } else { "portable-$run" }
        # Every launch has its own screenshot and results. A later success never
        # cancels out an earlier failure in the check accumulator.
        Test-Editor $portable $project $name
    }
}

# --- Installer ---------------------------------------------------------------------------------
if ($Installer) {
    $log = Join-Path $Artifacts "install-$Label.log"
    $setup = Start-Process -FilePath $Installer -Wait -PassThru -ArgumentList @(
        '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/CURRENTUSER', "/LOG=`"$log`"")
    Check ($setup.ExitCode -eq 0) "installer finished (exit code $($setup.ExitCode))"
    $installDir = Join-Path $env:LOCALAPPDATA 'Programs\FrameTrellis'
    Check (Test-Path (Join-Path $installDir 'frametrellis.exe')) 'installed for the current user'
    $shortcut = Join-Path ([Environment]::GetFolderPath('Programs')) 'FrameTrellis.lnk'
    Check (Test-Path $shortcut) 'Start menu shortcut created'
    $uninstallKey = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\${AppId}_is1"
    Check (Test-Path $uninstallKey) 'uninstaller registered'
    $association = (Get-ItemProperty 'HKCU:\Software\Classes\.ftproj' -ErrorAction SilentlyContinue).'(default)'
    Check ($association -eq 'FrameTrellis.Project') '.ftproj files open with FrameTrellis'

    $installedProject = Test-SelfTest $installDir 'installed'
    if (Test-Path $installedProject) { Test-Editor $installDir $installedProject 'installed' }

    if (Test-Path $uninstallKey) {
        $uninstaller = (Get-ItemProperty $uninstallKey).UninstallString.Trim('"')
        Start-Process -FilePath $uninstaller -Wait -ArgumentList @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART')
        # The uninstaller continues from a temporary copy, so wait for it to finish its work.
        $deadline = (Get-Date).AddMinutes(2)
        while ((Test-Path (Join-Path $installDir 'frametrellis.exe')) -and (Get-Date) -lt $deadline) {
            Start-Sleep -Seconds 1
        }
        Start-Sleep -Seconds 2
        Check (-not (Test-Path (Join-Path $installDir 'frametrellis.exe'))) 'uninstaller removed the program'
        Check (-not (Test-Path $shortcut)) 'uninstaller removed the Start menu shortcut'
        Check (-not (Test-Path $uninstallKey)) 'uninstaller removed its registry entry'
        Check (-not (Test-Path 'HKCU:\Software\Classes\FrameTrellis.Project')) 'uninstaller removed the file association'
    }
}

if ($failures.Count -gt 0) {
    Write-Host "`n$($failures.Count) check(s) failed:"
    $failures | ForEach-Object { Write-Host "  - $_" }
    exit 1
}
Write-Host "`nAll package checks passed."
