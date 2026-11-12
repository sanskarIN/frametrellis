#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Independently inspect a rendered MP4 with FFprobe and decode it with FFmpeg.

This is a release *regression gate*: FrameTrellis's own media probe cannot
substitute for a second implementation checking the output bitstream. Tests
use synthetic, locally generated media only; they never upload user footage.
"""
from __future__ import annotations

import argparse
import json
import math
import re
import subprocess
import sys
from fractions import Fraction
from pathlib import Path

VIDEO_CODECS = {"h264", "mpeg4"}
AUDIO_CODEC = "aac"
VOLUME = re.compile(r"mean_volume:\s*(-?(?:inf|\d+(?:\.\d+)?))\s*dB", re.I)


def parse_rate(value: object) -> Fraction | None:
    """Parse FFprobe rational FPS, including an absent/unknown 0/0 rate."""
    try:
        rate = Fraction(str(value))
    except (ValueError, ZeroDivisionError, TypeError):
        return None
    return rate if rate > 0 else None


def probe_problems(
    info: object, expected_fps: Fraction, width: int, height: int, duration: float
) -> list[str]:
    """Check exact streams/ratio, expected visual shape and final file duration."""
    if not isinstance(info, dict):
        return ["FFprobe did not return a JSON object"]
    streams = info.get("streams")
    container = info.get("format")
    if not isinstance(streams, list) or not isinstance(container, dict):
        return ["FFprobe omitted streams or container metadata"]
    videos = [s for s in streams if isinstance(s, dict) and s.get("codec_type") == "video"]
    audios = [s for s in streams if isinstance(s, dict) and s.get("codec_type") == "audio"]
    problems: list[str] = []
    if len(videos) != 1:
        problems.append(f"expected exactly one video stream, found {len(videos)}")
    if len(audios) != 1:
        problems.append(f"expected exactly one audio stream, found {len(audios)}")
    if "mp4" not in str(container.get("format_name", "")):
        problems.append("output is not an MP4 container")
    if len(videos) == 1:
        video = videos[0]
        if video.get("codec_name") not in VIDEO_CODECS:
            problems.append(f"unexpected video codec: {video.get('codec_name')}")
        if video.get("width") != width or video.get("height") != height:
            problems.append(
                f"video dimensions {video.get('width')}x{video.get('height')}; expected {width}x{height}"
            )
        rate = parse_rate(video.get("avg_frame_rate")) or parse_rate(video.get("r_frame_rate"))
        if rate != expected_fps:
            problems.append(f"video frame rate {rate}; expected {expected_fps}")
    if len(audios) == 1:
        audio = audios[0]
        if audio.get("codec_name") != AUDIO_CODEC:
            problems.append(f"unexpected audio codec: {audio.get('codec_name')}; expected AAC")
        try:
            sample_rate = int(audio.get("sample_rate", 0))
            channels = int(audio.get("channels", 0))
        except (ValueError, TypeError):
            sample_rate, channels = 0, 0
        if sample_rate < 8000 or not 1 <= channels <= 8:
            problems.append(f"invalid decoded audio layout: {sample_rate} Hz, {channels} channels")
    try:
        actual_duration = float(container.get("duration"))
    except (ValueError, TypeError):
        actual_duration = float("nan")
    tolerance = max(0.2, duration * 0.10)
    if not math.isfinite(actual_duration) or abs(actual_duration - duration) > tolerance:
        problems.append(
            f"container duration {actual_duration} s; expected {duration:.3f} +/- {tolerance:.3f} s"
        )
    return problems


def mean_volume_dbfs(ffmpeg_output: str) -> float | None:
    """Return None when the decode did not emit a volumedetect summary."""
    matches = VOLUME.findall(ffmpeg_output)
    if not matches:
        return None
    try:
        return float(matches[-1])
    except ValueError:
        return None


def run_command(arguments: list[str], timeout: int = 60) -> subprocess.CompletedProcess[str]:
    try:
        return subprocess.run(
            arguments, capture_output=True, text=True, errors="replace",
            timeout=timeout, check=False,
        )
    except (OSError, subprocess.TimeoutExpired) as exc:
        raise RuntimeError(f"Cannot execute {arguments[0]}: {exc}") from exc


def verify(
    media: Path,
    fps: Fraction,
    width: int,
    height: int,
    duration: float,
    min_mean_dbfs: float,
) -> list[str]:
    problems: list[str] = []
    if not media.is_file() or media.stat().st_size < 1024:
        return ["rendered media is missing or too small"]
    probe = run_command([
        "ffprobe", "-v", "error", "-i", str(media),
        "-show_entries",
        "stream=codec_type,codec_name,width,height,avg_frame_rate,r_frame_rate,"
        "sample_rate,channels:format=format_name,duration",
        "-of", "json",
    ])
    if probe.returncode:
        return [f"FFprobe could not read the MP4: {probe.stderr.strip()[-400:]}"]
    try:
        info = json.loads(probe.stdout)
    except json.JSONDecodeError as exc:
        return [f"FFprobe returned malformed JSON: {exc}"]
    problems.extend(probe_problems(info, fps, width, height, duration))
    if problems:
        return problems

    # This decodes the actual video, catching corrupt packets that headers
    # alone cannot find. It never writes output or accesses a network URL.
    video = run_command([
        "ffmpeg", "-hide_banner", "-nostdin", "-v", "error",
        "-i", str(media), "-map", "0:v:0", "-an", "-f", "null", "-",
    ])
    if video.returncode:
        problems.append(f"FFmpeg could not decode video: {video.stderr.strip()[-400:]}")

    # Independently decode audio samples and prove that the encoded AAC is not
    # complete silence. -inf dBFS and missing volumedetect summaries both fail.
    audio = run_command([
        "ffmpeg", "-hide_banner", "-nostdin", "-v", "info",
        "-i", str(media), "-map", "0:a:0", "-vn",
        "-af", "volumedetect", "-f", "null", "-",
    ])
    if audio.returncode:
        problems.append(f"FFmpeg could not decode audio: {audio.stderr.strip()[-400:]}")
    else:
        volume = mean_volume_dbfs(audio.stderr)
        if volume is None or not math.isfinite(volume) or volume < min_mean_dbfs:
            problems.append(
                f"decoded AAC is silent or too quiet: mean {volume} dBFS"
                f" (minimum {min_mean_dbfs} dBFS)"
            )
    return problems


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("media", type=Path, help="locally generated MP4 output")
    parser.add_argument("--fps", default="25", help="expected exact rational FPS")
    parser.add_argument("--width", type=int, default=640)
    parser.add_argument("--height", type=int, default=360)
    parser.add_argument("--duration", type=float, default=2.0, help="seconds")
    parser.add_argument("--min-mean-dbfs", type=float, default=-60.0)
    args = parser.parse_args()
    fps = parse_rate(args.fps)
    if fps is None or args.width <= 0 or args.height <= 0 or (
        not math.isfinite(args.duration) or args.duration <= 0
    ) or not math.isfinite(args.min_mean_dbfs):
        parser.error("invalid expected FPS, dimensions, duration or audio threshold")
    try:
        problems = verify(
            args.media, fps, args.width, args.height, args.duration, args.min_mean_dbfs
        )
    except RuntimeError as exc:
        problems = [str(exc)]
    if problems:
        for problem in problems:
            print(f"FAIL: {problem}", file=sys.stderr)
        return 1
    print(
        f"PASS: independent FFprobe/FFmpeg video and non-silent AAC decode: "
        f"{args.width}x{args.height} @ {fps} FPS"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
