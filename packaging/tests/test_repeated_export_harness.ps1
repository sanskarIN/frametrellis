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
        return 0;
    }
}
'@
    Add-Type -TypeDefinition $fixture -OutputAssembly $renderer -OutputType ConsoleApplication

    $project = Join-Path $root "disposable.ftproj"
    Set-Content -LiteralPath $project -Value "{}" -Encoding UTF8
    foreach ($case in @(
        @{ mode = "missing"; expected = "missing-output"; code = 1; bytes = 0; exists = $false },
        @{ mode = "empty"; expected = "empty-output"; code = 1; bytes = 0; exists = $true },
        @{ mode = "nonempty"; expected = "success"; code = 0; bytes = 3; exists = $true }
    )) {
        $env:FT_REPRO_FIXTURE_MODE = $case.mode
        $output = Join-Path $root ("outputs-" + $case.mode)
        # The harness deliberately calls exit, so run it in a child PowerShell
        # rather than allowing it to terminate this assertion script.
        & powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File $harness `
            -Renderer $renderer -OutputDirectory $output -Project $project -Runs 1
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
            throw "$($case.mode): unexpected run result $($run | ConvertTo-Json -Compress)"
        }
    }
    Write-Host "Repeated-export output integrity harness tests passed."
}
finally {
    Remove-Item Env:FT_REPRO_FIXTURE_MODE -ErrorAction SilentlyContinue
    if (Test-Path -LiteralPath $root) {
        Remove-Item -LiteralPath $root -Recurse -Force
    }
}
