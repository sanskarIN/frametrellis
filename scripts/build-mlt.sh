#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Builds the MLT modules FrameTrellis needs (core, avformat, plus, normalize) against the system
# FFmpeg and installs them to a prefix. Used by CI and for systems whose packaged MLT is too old.
#
#   scripts/build-mlt.sh <git-tag> <install-prefix>
set -euo pipefail

tag="${1:-v7.22.0}"
prefix="${2:-$HOME/.local/mlt}"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

git clone --quiet --depth 1 --branch "$tag" https://github.com/mltframework/mlt "$work/src"
cmake -S "$work/src" -B "$work/build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$prefix" \
  -DGPL=ON -DGPL3=ON -DCLANG_FORMAT=OFF -DBUILD_TESTING=OFF -DBUILD_TESTS_WITH_QT6=OFF \
  -DMOD_AVFORMAT=ON -DUSE_AVDEVICE=OFF -DMOD_PLUS=ON -DMOD_NORMALIZE=ON -DMOD_KDENLIVE=ON \
  -DMOD_DECKLINK=OFF -DMOD_FREI0R=OFF -DMOD_GDK=OFF -DMOD_JACKRACK=OFF -DMOD_MOVIT=OFF \
  -DMOD_OLDFILM=OFF -DMOD_OPENCV=OFF -DMOD_PLUSGPL=OFF -DMOD_QT=OFF -DMOD_QT6=OFF \
  -DMOD_RESAMPLE=OFF -DMOD_RTAUDIO=OFF -DMOD_RUBBERBAND=OFF -DMOD_SDL1=OFF -DMOD_SDL2=OFF \
  -DMOD_SOX=OFF -DMOD_VIDSTAB=OFF -DMOD_VORBIS=OFF -DMOD_XINE=OFF -DMOD_XML=OFF \
  -DMOD_GLAXNIMATE=OFF -DMOD_GLAXNIMATE_QT6=OFF -DMOD_OPENFX=OFF -DMOD_PLACEBO=OFF -DMOD_RNNOISE=OFF
cmake --build "$work/build"
cmake --install "$work/build"
echo "MLT $tag installed to $prefix"
