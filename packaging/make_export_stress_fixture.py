# SPDX-License-Identifier: GPL-3.0-or-later
"""Safely stretch the *generated FrameTrellis self-test* into an extended MP4 fixture.

Do not use personal media or arbitrary projects. Repeat the existing short
synthetic clips on their original video/image/audio tracks without extending
source In/Out points; render via the normal FrameTrellis CLI afterward.
Print exact expected duration in seconds for independent FFprobe verification.
"""
from __future__ import annotations

import argparse
import copy
import json
import sys
import uuid
from fractions import Fraction
from pathlib import Path


def stretch(document: object, repeats: int) -> tuple[dict, Fraction]:
    if not 2 <= repeats <= 50:
        raise ValueError("Repeated clip count must be between 2 and 50")
    if not isinstance(document, dict) or document.get("format") != "org.frametrellis.project":
        raise ValueError("Input is not a FrameTrellis project")
    project = document.get("project")
    if not isinstance(project, dict) or project.get("name") != "FrameTrellis self-test":
        raise ValueError("Only the disposable FrameTrellis generated self-test is accepted")
    settings = project.get("settings")
    tracks = project.get("tracks")
    if not isinstance(settings, dict) or not isinstance(tracks, list):
        raise ValueError("Missing project settings or tracks")
    try:
        rate = Fraction(settings.get("frameRate"))
    except (TypeError, ValueError, ZeroDivisionError) as exc:
        raise ValueError("Invalid self-test project FPS") from exc
    if rate != 25 or len(tracks) != 4:
        raise ValueError("Expected the standard four-track 25 FPS synthetic test project")
    active = [t for t in tracks if isinstance(t, dict) and t.get("clips")]
    if len(active) != 3 or {t.get("kind") for t in active} != {"video", "audio"}:
        raise ValueError("Expected two picture clips and one audio clip")
    lengths = set()
    for track in tracks:
        if not isinstance(track, dict) or not isinstance(track.get("clips"), list):
            raise ValueError("Invalid self-test track")
        clips = track["clips"]
        if len(clips) > 1 or track.get("transitions"):
            raise ValueError("Unexpected synthetic source edits/transitions")
        if clips:
            clip = clips[0]
            if not isinstance(clip, dict) or clip.get("start") != 0:
                raise ValueError("Self-test clip must begin at frame zero")
            length = clip.get("duration")
            if not isinstance(length, int) or isinstance(length, bool) or not 25 <= length <= 50:
                raise ValueError("Unexpected synthetic source clip duration")
            lengths.add(length)
    if len(lengths) != 1:
        raise ValueError("Synthetic picture/audio clips must share a single duration")
    frames = lengths.pop()
    result = copy.deepcopy(document)
    project_copy = result["project"]
    project_copy["name"] = "FrameTrellis generated export stress test"
    for track in project_copy["tracks"]:
        clips = track["clips"]
        if not clips:
            continue
        template = clips[0]
        for index in range(1, repeats):
            clone = copy.deepcopy(template)
            clone["id"] = uuid.uuid4().hex
            clone["start"] = frames * index
            clips.append(clone)
    return result, Fraction(frames * repeats, 1) / rate


def make_fixture(source: Path, destination: Path, repeats: int) -> Fraction:
    if source.is_symlink() or not source.is_file():
        raise ValueError("Source must be a real generated project file")
    if destination.is_symlink() or destination.exists():
        raise ValueError("Refusing to overwrite an existing output project")
    if not destination.parent.is_dir() or destination.parent.is_symlink():
        raise ValueError("Output parent must already exist and not be a symlink")
    if source.resolve() == destination.resolve(strict=False):
        raise ValueError("Output cannot be the input project")
    document = json.loads(source.read_text(encoding="utf-8"))
    result, duration = stretch(document, repeats)
    with destination.open("x", encoding="utf-8", newline="\n") as stream:
        json.dump(result, stream, ensure_ascii=False, indent=2)
        stream.write("\n")
    return duration


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--repeats", type=int, default=30)
    args = parser.parse_args()
    try:
        seconds = make_fixture(args.input, args.output, args.repeats)
    except (OSError, ValueError, TypeError, json.JSONDecodeError) as exc:
        print(f"Extended synthetic fixture FAILED: {exc}", file=sys.stderr)
        return 1
    # Machine-readable stdout only; used by Windows shell in --duration.
    print(f"{float(seconds):.6f}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
