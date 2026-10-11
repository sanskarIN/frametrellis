#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Captures screenshots of the real application for the documentation and website.
# Generates synthetic demo media with FFmpeg, drives the GUI test with it and copies the
# results to docs/images/.
#
#   scripts/make-screenshots.sh [build-dir]
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
build="${1:-$root/build/linux}"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

ffmpeg -v error -y -f lavfi -i "mandelbrot=size=1280x720:rate=30" \
  -f lavfi -i "sine=frequency=220:sample_rate=48000" -t 8 \
  -c:v libx264 -preset veryfast -pix_fmt yuv420p -c:a aac "$work/mandelbrot-zoom.mp4"
ffmpeg -v error -y -f lavfi -i "aevalsrc=0.25*sin(2*PI*220*t)+0.2*sin(2*PI*277*t)+0.15*sin(2*PI*330*t):s=48000:d=10" \
  -ac 2 "$work/ambient-chord.wav"
ffmpeg -v error -y -f lavfi -i "gradients=size=1280x720:c0=0x1f6f63:c1=0x353d78:x0=0:y0=0:x1=1280:y1=720:duration=1" \
  -frames:v 1 "$work/title-card.png"

export FT_DEMO_MEDIA="$work/mandelbrot-zoom.mp4;$work/ambient-chord.wav;$work/title-card.png"
export FT_SCREENSHOT_DIR="$work/shots"
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software SDL_AUDIODRIVER=dummy \
  QT_LOGGING_RULES="ft.*.info=false" "$build/tests/gui/tst_gui"

mkdir -p "$root/docs/images"
cp "$work/shots/04-clip-selected.png" "$root/docs/images/workspace.png"
cp "$work/shots/01-welcome.png" "$root/docs/images/welcome.png"
cp "$work/shots/05-export-dialog.png" "$root/docs/images/export.png"
cp "$work/shots/06-export-finished.png" "$root/docs/images/export-finished.png"
cp "$work/shots/02-empty-workspace.png" "$root/docs/images/empty-workspace.png"
echo "Screenshots written to docs/images/"
