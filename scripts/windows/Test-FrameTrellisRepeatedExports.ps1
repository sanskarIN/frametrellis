# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
# Windows PowerShell 5.1+; intentionally local-only, never uploads diagnostics.
# Run from a normal PowerShell session against a local packaged renderer.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $Renderer,
    [Parameter(Mandatory = $true)]
    [string] $OutputDirectory,
    [string] $Project = "",
    [string] $Fps = "match",
    [ValidateRange(1, 25)]
    [int] $Runs = 3,
    [ValidateRange(30, 7200)]
    [int] $TimeoutSeconds = 1800,
    [switch] $SampleResources,
    [ValidateRange(100, 5000)]
    [int] $SampleIntervalMs = 500
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if (-not [Environment]::OSVersion.Platform.ToString().StartsWith("Win")) {
    throw "This test harness targets the packaged Windows renderer."
}
if (-not (Test-Path -LiteralPath $Renderer -PathType Leaf)) {
    throw "Renderer executable not found: $Renderer"
}
$rendererPath = [IO.Path]::GetFullPath($Renderer)
if (-not $rendererPath.EndsWith("frametrellis-render.exe",
                                [StringComparison]::OrdinalIgnoreCase)) {
    throw "Choose frametrellis-render.exe from a FrameTrellis installation or ZIP."
}
if (-not (Test-Path -LiteralPath $OutputDirectory)) {
    [void](New-Item -ItemType Directory -Path $OutputDirectory -Force)
}
$outputRoot = [IO.Path]::GetFullPath($OutputDirectory)
$projectPath = ""
if (-not [string]::IsNullOrWhiteSpace($Project)) {
    if (-not (Test-Path -LiteralPath $Project -PathType Leaf)) {
        throw "Project file not found: $Project"
    }
    $projectPath = [IO.Path]::GetFullPath($Project)
    if (-not $projectPath.EndsWith(".ftproj", [StringComparison]::OrdinalIgnoreCase)) {
        throw "Choose a FrameTrellis .ftproj project."
    }
}
if ([string]::IsNullOrWhiteSpace($Fps)) {
    throw "FPS must be 'match' or a valid positive rate."
}

$mode = if ($projectPath) { "existing-project" } else { "synthetic-self-test" }
$results = New-Object System.Collections.Generic.List[object]
Write-Host "FrameTrellis repeated export test ($mode), $Runs runs"
Write-Host "Renderer: $rendererPath"
Write-Host "Results are kept only on this computer, under $outputRoot"
Write-Host "No user project, footage, logs, or crash dumps are uploaded."

for ($i = 1; $i -le $Runs; ++$i) {
    $caseDir = Join-Path $outputRoot ("run-{0:D3}" -f $i)
    if (Test-Path -LiteralPath $caseDir) {
        throw "Refusing to overwrite an existing run folder: $caseDir"
    }
    [void](New-Item -ItemType Directory -Path $caseDir)
    $stdoutFile = Join-Path $caseDir "renderer.stdout.log"
    $stderrFile = Join-Path $caseDir "renderer.stderr.log"
    $outputFile = Join-Path $caseDir "export.mp4"
    if ($projectPath) {
        $arguments = @("--project", $projectPath, "--output", $outputFile,
                       "--fps", $Fps)
    } else {
        $arguments = @("--self-test", $caseDir)
    }
    # Windows file paths cannot contain double quotes. Quote each argument to
    # preserve whitespace without calling a shell or evaluating user input.
    $quoted = ($arguments | ForEach-Object { '"' + $_ + '"' }) -join ' '

    $startedAt = Get-Date
    $timedOut = $false
    $launchError = ""
    $exitCode = $null
    $resourceSamples = 0
    $peakWorkingSetBytes = [int64] 0
    $peakPrivateBytes = [int64] 0
    try {
        $process = Start-Process -FilePath $rendererPath -ArgumentList $quoted `
            -PassThru -NoNewWindow -RedirectStandardOutput $stdoutFile `
            -RedirectStandardError $stderrFile -ErrorAction Stop
        # PowerShell can discard a process's handle after Start-Process
        # -NoNewWindow / redirected streams, then silently return $null for
        # ExitCode even after WaitForExit. Capture the handle before waiting:
        # https://github.com/PowerShell/PowerShell/issues/20400
        $processHandle = $process.Handle
        if ($SampleResources) {
            # Poll only our own child process. No per-process paths, environment,
            # media names or system-wide process data are recorded.
            $timer = [Diagnostics.Stopwatch]::StartNew()
            $finished = $false
            while (-not $finished) {
                try {
                    $process.Refresh()
                    if (-not $process.HasExited) {
                        $peakWorkingSetBytes = [math]::Max(
                            $peakWorkingSetBytes, [int64] $process.WorkingSet64)
                        $peakPrivateBytes = [math]::Max(
                            $peakPrivateBytes, [int64] $process.PrivateMemorySize64)
                        ++$resourceSamples
                    }
                }
                catch {
                    # A process can exit between the refresh and the sample.
                    # Missing samples are reported, not fabricated as zero.
                }
                $remaining = [int64] $TimeoutSeconds * 1000 - $timer.ElapsedMilliseconds
                if ($remaining -le 0) {
                    break
                }
                $wait = [int] [math]::Min([int64] $SampleIntervalMs, $remaining)
                $finished = $process.WaitForExit($wait)
            }
            if (-not $finished -and -not $process.HasExited) {
                $timedOut = $true
                $process.Kill()
                $process.WaitForExit()
            }
        }
        elseif (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
            $timedOut = $true
            # Kill only our own launched test renderer, never the editor.
            $process.Kill()
            $process.WaitForExit()
        }
        $exitCode = $process.ExitCode
    }
    catch {
        $launchError = $_.Exception.Message
    }

    $seconds = [math]::Round(((Get-Date) - $startedAt).TotalSeconds, 2)
    # A zero renderer exit code alone is not proof that the requested MP4 was
    # published. Check only this run's dedicated output; leave it untouched
    # for independent media probing and privacy-conscious crash analysis.
    $outputInfo = if (Test-Path -LiteralPath $outputFile -PathType Leaf) {
        Get-Item -LiteralPath $outputFile
    } else {
        $null
    }
    $outputExists = $null -ne $outputInfo
    $outputBytes = if ($outputExists) { [int64] $outputInfo.Length } else { [int64] 0 }
    $status = "success"
    if ($launchError) {
        $status = "launch-failed"
    } elseif ($timedOut) {
        $status = "timeout"
    } elseif ($exitCode -ne 0) {
        $status = "renderer-failed"
    } elseif ($projectPath -and -not $outputExists) {
        $status = "missing-output"
    } elseif ($projectPath -and $outputBytes -eq 0) {
        $status = "empty-output"
    }
    $exitHex = if ($null -eq $exitCode) {
        ""
    } else {
        "0x" + [Convert]::ToString([int]$exitCode, 16).PadLeft(8, '0')
    }
    $reportedPeakWorkingSet = $null
    $reportedPeakPrivateBytes = $null
    if ($resourceSamples -gt 0) {
        $reportedPeakWorkingSet = [int64] $peakWorkingSetBytes
        $reportedPeakPrivateBytes = [int64] $peakPrivateBytes
    }
    $result = [pscustomobject]@{
        run = $i
        status = $status
        exitCode = $exitCode
        exitHex = $exitHex
        elapsedSeconds = $seconds
        timedOut = $timedOut
        launchError = $launchError
        outputExists = $outputExists
        outputBytes = $outputBytes
        resourceSamplingEnabled = [bool] $SampleResources
        resourceSamples = $resourceSamples
        peakWorkingSetBytes = $reportedPeakWorkingSet
        peakPrivateBytes = $reportedPeakPrivateBytes
        stdout = $stdoutFile
        stderr = $stderrFile
        output = $outputFile
    }
    [void]$results.Add($result)
    Write-Host ("Run {0}: {1}; exit {2}; {3} seconds" -f
        $i, $status, $exitHex, $seconds)
}

$summaryPath = Join-Path $outputRoot "frametrellis-render-repro-summary.json"
$reportedSampleInterval = $null
if ($SampleResources) {
    $reportedSampleInterval = $SampleIntervalMs
}
$summary = [pscustomobject]@{
    schemaVersion = 1
    generatedAt = (Get-Date).ToString("o")
    mode = $mode
    renderer = $rendererPath
    project = $projectPath
    fps = $Fps
    timeoutSeconds = $TimeoutSeconds
    resourceSamplingEnabled = [bool] $SampleResources
    resourceSampleIntervalMs = $reportedSampleInterval
    runs = @($results.ToArray())
}
$summary | ConvertTo-Json -Depth 5 |
    Set-Content -LiteralPath $summaryPath -Encoding UTF8
Write-Host "Local summary: $summaryPath"
Write-Host "Review and redact project paths, media names, and logs before sharing."
if (@($results | Where-Object { $_.status -ne "success" }).Count -gt 0) {
    exit 1
}
exit 0
