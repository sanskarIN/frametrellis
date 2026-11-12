# SPDX-License-Identifier: GPL-3.0-or-later
"""A stalled macOS package never consumes the entire CI matrix timeout."""
from __future__ import annotations

import importlib.util
import subprocess
import unittest
from pathlib import Path
from unittest import mock

SCRIPT = Path(__file__).resolve().parents[1] / "macos" / "timeout_exec.py"
SPEC = importlib.util.spec_from_file_location("ft_macos_timeout", SCRIPT)
assert SPEC and SPEC.loader
timeout = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(timeout)


class MacTimeoutTests(unittest.TestCase):
    def test_success_passes_command_as_argv_and_uses_new_session(self):
        fake = mock.Mock()
        fake.wait.return_value = 0
        with mock.patch.object(timeout.subprocess, "Popen", return_value=fake) as spawn:
            self.assertEqual(timeout.execute(["/tmp/a file", "--arg", "with spaces"], 90), 0)
            spawn.assert_called_once_with(
                ["/tmp/a file", "--arg", "with spaces"], start_new_session=True
            )
            fake.wait.assert_called_once_with(timeout=90)

    def test_nonzero_child_exit_is_preserved(self):
        fake = mock.Mock()
        fake.wait.return_value = 17
        with mock.patch.object(timeout.subprocess, "Popen", return_value=fake):
            self.assertEqual(timeout.execute(["/tmp/fails"], 90), 17)

    def test_timeout_terminates_group_and_returns_124(self):
        fake = mock.Mock(pid=8192)
        fake.wait.side_effect = [subprocess.TimeoutExpired("command", 10), 0]
        with (
            mock.patch.object(timeout.subprocess, "Popen", return_value=fake),
            mock.patch.object(timeout.os, "killpg") as kill,
        ):
            self.assertEqual(timeout.execute(["/tmp/stuck"], 10), 124)
            kill.assert_called_once_with(8192, timeout.signal.SIGTERM)

    def test_uncooperative_group_is_force_killed(self):
        fake = mock.Mock(pid=8193)
        fake.wait.side_effect = [
            subprocess.TimeoutExpired("command", 10),
            subprocess.TimeoutExpired("command", 5),
            0,
        ]
        with (
            mock.patch.object(timeout.subprocess, "Popen", return_value=fake),
            mock.patch.object(timeout.os, "killpg") as kill,
        ):
            self.assertEqual(timeout.execute(["/tmp/stuck"], 10), 124)
            self.assertEqual(kill.call_args_list, [
                mock.call(8193, timeout.signal.SIGTERM),
                mock.call(8193, timeout.signal.SIGKILL),
            ])

    def test_missing_executable_is_diagnostic_failure(self):
        with mock.patch.object(
            timeout.subprocess, "Popen", side_effect=OSError("missing file")
        ):
            self.assertEqual(timeout.execute(["/not-present"], 5), 127)

    def test_sampling_uses_native_mac_tool_with_bounded_timeout(self):
        import tempfile

        with tempfile.TemporaryDirectory() as directory:
            with (
                mock.patch.object(timeout.sys, "platform", "darwin"),
                mock.patch.object(timeout.subprocess, "run") as invoke,
            ):
                invoke.return_value.returncode = 0
                timeout.capture_stalled_process(8192, directory)
                args, kwargs = invoke.call_args
                self.assertEqual(args[0][:3], ["/usr/bin/sample", "8192", "3"])
                self.assertEqual(args[0][-2], "-file")
                self.assertTrue(args[0][-1].endswith("timeout-sample-8192.txt"))
                self.assertEqual(kwargs["timeout"], 15)
                self.assertFalse(kwargs["check"])

    def test_sampling_disabled_without_opt_in(self):
        with mock.patch.object(timeout.subprocess, "run") as invoke:
            timeout.capture_stalled_process(8192, None)
            invoke.assert_not_called()

    def test_sampler_failure_does_not_skip_group_cleanup(self):
        fake = mock.Mock(pid=8192)
        fake.wait.side_effect = [subprocess.TimeoutExpired("command", 10), 0]
        with (
            mock.patch.object(timeout.subprocess, "Popen", return_value=fake),
            mock.patch.object(timeout.sys, "platform", "darwin"),
            mock.patch.object(timeout.subprocess, "run",
                              side_effect=OSError("sampling denied")),
            mock.patch.object(timeout.os, "killpg") as kill,
        ):
            # A denied sampler must not prevent the actual child termination.
            self.assertEqual(timeout.execute(["/tmp/stuck"], 10, "/tmp/diagnostics"), 124)
            kill.assert_called_once_with(8192, timeout.signal.SIGTERM)

    def test_rejects_empty_argv_or_unbounded_deadline(self):
        with self.assertRaises(ValueError):
            timeout.execute([], 30)
        with self.assertRaises(ValueError):
            timeout.execute(["/tmp/app"], 0)


if __name__ == "__main__":
    unittest.main()
