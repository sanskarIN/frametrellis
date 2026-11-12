# SPDX-License-Identifier: GPL-3.0-or-later
"""Portable regression tests for the independent video/audio release gate."""
from __future__ import annotations

import importlib.util
import json
import subprocess
import tempfile
import unittest
from fractions import Fraction
from pathlib import Path
from unittest import mock

SCRIPT = Path(__file__).resolve().parents[1] / "verify_export_media.py"
SPEC = importlib.util.spec_from_file_location("verify_export_media", SCRIPT)
assert SPEC and SPEC.loader
checker = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(checker)


def synthetic_probe() -> dict:
    return {
        "streams": [
            {
                "codec_type": "video",
                "codec_name": "h264",
                "width": 640,
                "height": 360,
                "avg_frame_rate": "25/1",
                "r_frame_rate": "25/1",
            },
            {
                "codec_type": "audio",
                "codec_name": "aac",
                "sample_rate": "48000",
                "channels": 2,
            },
        ],
        "format": {
            "format_name": "mov,mp4,m4a,3gp,3g2,mj2",
            "duration": "2.04",
        },
    }


class IndependentExportVerifierTests(unittest.TestCase):
    def inspect(self, info: object, fps: Fraction = Fraction(25, 1)) -> list[str]:
        return checker.probe_problems(info, fps, 640, 360, 2.0)

    def test_accepts_valid_h264_aac_mp4(self):
        self.assertEqual(self.inspect(synthetic_probe()), [])

    def test_accepts_mpeg4_fallback_and_rational_fps(self):
        info = synthetic_probe()
        info["streams"][0]["codec_name"] = "mpeg4"
        info["streams"][0]["avg_frame_rate"] = "30000/1001"
        self.assertEqual(self.inspect(info, Fraction(30000, 1001)), [])

    def test_falls_back_to_r_frame_rate_when_avg_unknown(self):
        info = synthetic_probe()
        info["streams"][0]["avg_frame_rate"] = "0/0"
        self.assertEqual(self.inspect(info), [])

    def test_invalid_frame_rate_does_not_crash(self):
        for rate in ("0/0", "-1/2", "nonsense", None, ""):
            self.assertIsNone(checker.parse_rate(rate))

    def test_missing_audio_rejected(self):
        info = synthetic_probe()
        info["streams"].pop()
        self.assertTrue(any("exactly one audio stream" in p for p in self.inspect(info)))

    def test_multiple_video_streams_rejected(self):
        info = synthetic_probe()
        info["streams"].append(dict(info["streams"][0]))
        self.assertTrue(any("exactly one video stream" in p for p in self.inspect(info)))

    def test_wrong_frame_rate_rejected(self):
        info = synthetic_probe()
        info["streams"][0]["avg_frame_rate"] = "24/1"
        self.assertTrue(any("frame rate" in p for p in self.inspect(info)))

    def test_wrong_size_rejected(self):
        info = synthetic_probe()
        info["streams"][0]["width"] = 1280
        self.assertTrue(any("dimensions" in p for p in self.inspect(info)))

    def test_non_aac_rejected(self):
        info = synthetic_probe()
        info["streams"][1]["codec_name"] = "mp3"
        self.assertTrue(any("expected AAC" in p for p in self.inspect(info)))

    def test_non_mp4_rejected(self):
        info = synthetic_probe()
        info["format"]["format_name"] = "matroska,webm"
        self.assertTrue(any("not an MP4" in p for p in self.inspect(info)))

    def test_missing_invalid_duration_rejected(self):
        for duration in ("nan", "inf", "-2", "2.5", None):
            info = synthetic_probe()
            info["format"]["duration"] = duration
            self.assertTrue(
                any("container duration" in p for p in self.inspect(info)),
                f"invalid duration={duration}",
            )

    def test_invalid_audio_channels_rejected(self):
        info = synthetic_probe()
        info["streams"][1]["channels"] = 0
        self.assertTrue(any("audio layout" in p for p in self.inspect(info)))

    def test_missing_json_metadata_rejected(self):
        self.assertTrue(self.inspect(None))
        self.assertTrue(self.inspect({}))

    def test_volume_detect_accepts_signal_and_rejects_silence(self):
        self.assertEqual(
            checker.mean_volume_dbfs("[Parsed_volumedetect_0] mean_volume: -20.5 dB"),
            -20.5,
        )
        self.assertEqual(
            checker.mean_volume_dbfs("[Parsed_volumedetect_0] mean_volume: -inf dB"),
            float("-inf"),
        )
        self.assertIsNone(checker.mean_volume_dbfs("no volume"))

    def test_missing_media_is_rejected_without_executing_tools(self):
        with tempfile.TemporaryDirectory() as tmp:
            with mock.patch.object(checker, "run_command") as run:
                self.assertTrue(checker.verify(Path(tmp) / "missing.mp4", Fraction(25), 640, 360, 2.0, -60))
                run.assert_not_called()

    def test_independent_bitstream_checks_execute_and_accept_real_signal(self):
        with tempfile.TemporaryDirectory() as tmp:
            media = Path(tmp) / "sample.mp4"
            media.write_bytes(b"x" * 2048)
            reports = [
                subprocess.CompletedProcess([], 0, stdout=json.dumps(synthetic_probe()), stderr=""),
                subprocess.CompletedProcess([], 0, stdout="", stderr=""),
                subprocess.CompletedProcess([], 0, stdout="", stderr="mean_volume: -22.0 dB"),
            ]
            with mock.patch.object(checker, "run_command", side_effect=reports) as run:
                self.assertEqual(checker.verify(media, Fraction(25), 640, 360, 2.0, -60), [])
                self.assertEqual(run.call_count, 3)
                self.assertEqual(run.call_args_list[1].args[0][0], "ffmpeg")

    def test_silent_audio_rejected_even_when_probe_has_aac(self):
        with tempfile.TemporaryDirectory() as tmp:
            media = Path(tmp) / "sample.mp4"
            media.write_bytes(b"x" * 2048)
            reports = [
                subprocess.CompletedProcess([], 0, stdout=json.dumps(synthetic_probe()), stderr=""),
                subprocess.CompletedProcess([], 0, stdout="", stderr=""),
                subprocess.CompletedProcess([], 0, stdout="", stderr="mean_volume: -inf dB"),
            ]
            with mock.patch.object(checker, "run_command", side_effect=reports):
                self.assertTrue(
                    any("silent or too quiet" in p for p in checker.verify(
                        media, Fraction(25), 640, 360, 2.0, -60
                    ))
                )

    def test_corrupt_video_decode_fails_despite_valid_metadata(self):
        with tempfile.TemporaryDirectory() as tmp:
            media = Path(tmp) / "sample.mp4"
            media.write_bytes(b"x" * 2048)
            reports = [
                subprocess.CompletedProcess([], 0, stdout=json.dumps(synthetic_probe()), stderr=""),
                subprocess.CompletedProcess([], 1, stdout="", stderr="Invalid NAL unit"),
                subprocess.CompletedProcess([], 0, stdout="", stderr="mean_volume: -18.0 dB"),
            ]
            with mock.patch.object(checker, "run_command", side_effect=reports):
                self.assertTrue(any("decode video" in p for p in checker.verify(
                    media, Fraction(25), 640, 360, 2.0, -60
                )))


if __name__ == "__main__":
    unittest.main()
