# SPDX-License-Identifier: GPL-3.0-or-later
"""Run native macOS packaging smoke with a hard per-command deadline.

Unlike a CI job-level timeout, this identifies which exact bundled program
stalled and kills its complete process group. Never shell-evaluates arguments.
"""
from __future__ import annotations

import argparse
import os
import signal
from contextlib import suppress
import subprocess
import sys
from pathlib import Path


def capture_stalled_process(pid: int, diagnostics_dir: str | None) -> None:
    """Capture a native stack sample before terminating a timed-out CI GUI.

    No user project/media is sampled: this only runs on an opt-in CI-host
    diagnostics folder, with temporary synthetic package test inputs.
    """
    if not diagnostics_dir or sys.platform != "darwin":
        return
    try:
        folder = Path(diagnostics_dir)
        folder.mkdir(parents=True, exist_ok=True)
        output = folder / f"timeout-sample-{pid}.txt"
        result = subprocess.run(
            ["/usr/bin/sample", str(pid), "3", "-file", str(output)],
            capture_output=True,
            text=True,
            timeout=15,
            check=False,
        )
        if result.returncode:
            print(
                f"WARNING: macOS process sampling returned {result.returncode}: "
                f"{result.stderr[-500:]}",
                file=sys.stderr,
                flush=True,
            )
        else:
            print(f"macOS stalled-process stack sample: {output}", flush=True)
    except (OSError, subprocess.TimeoutExpired) as exc:
        # A missing/denied sampler must never bypass process termination.
        print(f"WARNING: cannot sample stalled macOS process: {exc}", file=sys.stderr)


def execute(argv: list[str], seconds: int, diagnostics_dir: str | None = None) -> int:
    if not argv or seconds < 1:
        raise ValueError("expected command and positive timeout")
    try:
        process = subprocess.Popen(argv, start_new_session=True)
    except OSError as exc:
        print(f"FAIL: could not start {argv[0]}: {exc}", file=sys.stderr, flush=True)
        return 127
    try:
        return process.wait(timeout=seconds)
    except subprocess.TimeoutExpired:
        print(
            f"FAIL: timed out after {seconds}s: {argv[0]} "
            "(stopping the entire test process group)",
            file=sys.stderr,
            flush=True,
        )
        capture_stalled_process(process.pid, diagnostics_dir)
        with suppress(ProcessLookupError):
            os.killpg(process.pid, signal.SIGTERM)
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            with suppress(ProcessLookupError):
                os.killpg(process.pid, signal.SIGKILL)
            process.wait()
        return 124


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--seconds", type=int, required=True, help="hard execution deadline")
    parser.add_argument(
        "--diagnostics-dir",
        help="optional CI-only directory for native macOS stack samples on timeout",
    )
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command
    if command and command[0] == "--":
        command = command[1:]
    if args.seconds < 1 or not command:
        parser.error("supply --seconds > 0 followed by -- command [arguments]")
    return execute(command, args.seconds, args.diagnostics_dir)


if __name__ == "__main__":
    raise SystemExit(main())
