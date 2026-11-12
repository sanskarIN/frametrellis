# SPDX-License-Identifier: GPL-3.0-or-later
"""Run native macOS packaging smoke with a hard per-command deadline.

Unlike a CI job-level timeout, this identifies which exact bundled program
stalled and kills its complete process group. Never shell-evaluates arguments.
"""
from __future__ import annotations

import argparse
import os
import signal
import subprocess
import sys


def execute(argv: list[str], seconds: int) -> int:
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
        try:
            os.killpg(process.pid, signal.SIGTERM)
        except ProcessLookupError:
            pass
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            process.wait()
        return 124


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--seconds", type=int, required=True, help="hard execution deadline")
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command
    if command and command[0] == "--":
        command = command[1:]
    if args.seconds < 1 or not command:
        parser.error("supply --seconds > 0 followed by -- command [arguments]")
    return execute(command, args.seconds)


if __name__ == "__main__":
    raise SystemExit(main())
