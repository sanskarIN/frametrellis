# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
# Windows PowerShell 5.1+; only disposable local fixtures, no real media.
[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$root = Join-Path ([IO.Path]::GetTempPath()) ("ft-repro-test-" + [Guid]::NewGuid().ToString("N"))
$harness = (Resolve-Path (Join-Path $PSScriptRoot "../../scripts/windows/Test-FrameTrellisRepeatedExports.ps1")).Path
try {
    [void](New-Item -ItemType Directory -Path $root)
    $renderer = Join-Path $root "frametrellis-render.exe"
    # Compile a harmless local stand-in that intentionally omits, empties or
    # writes the requested output while always reporting process exit code 0.
    # This is not a test of the actual encoder or MP4 playability.
    $fixture = @'
using System;
using System.IO;
public class RendererFixture
{
    public static int Main(string[] args)
    {
        string output = null;
        for (int i = 0; i < args.Length - 1; ++i)
            if (args[i] == "--output")
                output = args[i + 1];
        if (output == null) return 2;
        string mode = Environment.GetEnvironmentVariable("FT_REPRO_FIXTURE_MODE");
        if (mode == "empty") File.WriteAllBytes(output, new byte[0]);
        if (mode == "nonempty") File.WriteAllBytes(output, new byte[] { 1, 2, 3 });
        if (mode == "sample") {
            // Give the opt-in monitor time to observe this disposable child.
            byte[] memory = new byte[2 * 1024 * 1024];
            memory[0] = 1;
            System.Threading.Thread.Sleep(900);
            File.WriteAllBytes(output, new byte[] { memory[0], 2, 3 });
            GC.KeepAlive(memory);
        }
        return 0;
    }
}
'@
    Add-Type -TypeDefinition $fixture -OutputAssembly $renderer -OutputType ConsoleApplication

    $project = Join-Path $root "disposable.ftproj"
    Set-Content -LiteralPath $project -Value "{}" -Encoding UTF8
    foreach ($case in @(
        @{ mode = "missing"; expected = "missing-output"; code = 1; bytes = 0; exists = $false; sample = $false },
        @{ mode = "empty"; expected = "empty-output"; code = 1; bytes = 0; exists = $true; sample = $false },
        @{ mode = "nonempty"; expected = "success"; code = 0; bytes = 3; exists = $true; sample = $false },
        @{ mode = "sample"; expected = "success"; code = 0; bytes = 3; exists = $true; sample = $true }
    )) {
        $env:FT_REPRO_FIXTURE_MODE = $case.mode
        $output = Join-Path $root ("outputs-" + $case.mode)
        # The harness deliberately calls exit, so run it in a child PowerShell
        # rather than allowing it to terminate this assertion script.
        $extra = @()
        if ($case.sample) {
            $extra = @("-SampleResources", "-SampleIntervalMs", "100")
        }
        & powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File $harness `
            -Renderer $renderer -OutputDirectory $output -Project $project -Runs 1 @extra
        $actualCode = $LASTEXITCODE
        if ($actualCode -ne $case.code) {
            throw "$($case.mode): expected harness exit $($case.code), got $actualCode"
        }
        $summaryFile = Join-Path $output "frametrellis-render-repro-summary.json"
        $summary = Get-Content -LiteralPath $summaryFile -Raw | ConvertFrom-Json
        $run = @($summary.runs)[0]
        if ($run.status -ne $case.expected -or
            [int64] $run.outputBytes -ne $case.bytes -or
            [bool] $run.outputExists -ne $case.exists) {
            $stdout = if (Test-Path -LiteralPath $run.stdout) {
                Get-Content -LiteralPath $run.stdout -Raw
            } else { "<missing>" }
            $stderr = if (Test-Path -LiteralPath $run.stderr) {
                Get-Content -LiteralPath $run.stderr -Raw
            } else { "<missing>" }
            throw "$($case.mode): unexpected run result $($run | ConvertTo-Json -Compress); stdout=$stdout; stderr=$stderr"
        }
        if ($case.sample) {
            if (-not $summary.resourceSamplingEnabled -or
                -not $run.resourceSamplingEnabled -or
                [int] $run.resourceSamples -lt 1 -or
                [int64] $run.peakWorkingSetBytes -le 0 -or
                [int64] $run.peakPrivateBytes -le 0) {
                throw "Opt-in resource sampling did not capture the fixture: $($run | ConvertTo-Json -Compress)"
            }
        }
        elseif ($run.resourceSamplingEnabled -or [int] $run.resourceSamples -ne 0) {
            throw "Resource monitoring must stay disabled by default."
        }
    }
    Write-Host "Repeated-export output integrity and opt-in resource sampling tests passed."
}
finally {
    Remove-Item Env:FT_REPRO_FIXTURE_MODE -ErrorAction SilentlyContinue
    if (Test-Path -LiteralPath $root) {
        Remove-Item -LiteralPath $root -Recurse -Force
    }
}
