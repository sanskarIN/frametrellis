# syntax=docker/dockerfile:1
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#
# Container image of frametrellis-render, the headless renderer of FrameTrellis: it renders a
# project (.ftproj) to an H.264/AAC MP4 file without a display. Build it from the root of a
# FrameTrellis source tree:
#
#   docker build -f packaging/container/Dockerfile -t frametrellis-render .
#   docker run --rm --user "$(id -u):$(id -g)" -v "$PWD:/work" frametrellis-render \
#       --project /work/my-project.ftproj --output /work/my-video.mp4
#
# Qt, MLT and FFmpeg come from Debian 13. Only the four MLT modules the engine uses are
# loaded, so Debian's Qt 5 based modules never end up in the Qt 6 process.

ARG DEBIAN_IMAGE=debian:13-slim

FROM ${DEBIAN_IMAGE} AS build
RUN apt-get update \
    && apt-get install --yes --no-install-recommends \
        ca-certificates cmake g++ libmlt++-dev libmlt-dev ninja-build pkgconf qt6-base-dev \
    && rm -rf /var/lib/apt/lists/*
COPY . /src
RUN cmake -S /src -B /build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DFT_BUILD_APP=OFF \
        -DFT_BUILD_TESTS=OFF \
        -DFT_WITH_SDL2=OFF \
    && cmake --build /build --target frametrellis-render \
    && install -D -m 0755 /build/bin/frametrellis-render /out/usr/bin/frametrellis-render \
    && install -D -m 0644 /src/LICENSE /out/usr/share/doc/frametrellis/LICENSE \
    && install -D -m 0644 /src/THIRD_PARTY_NOTICES.md \
        /out/usr/share/doc/frametrellis/THIRD_PARTY_NOTICES.md

FROM ${DEBIAN_IMAGE}
ARG VERSION=unknown
ARG REVISION=unknown
LABEL org.opencontainers.image.title="frametrellis-render" \
      org.opencontainers.image.description="Headless renderer of the FrameTrellis video editor: renders .ftproj projects to H.264/AAC MP4." \
      org.opencontainers.image.source="https://github.com/sanskarIN/frametrellis" \
      org.opencontainers.image.url="https://sanskarin.github.io/frametrellis/" \
      org.opencontainers.image.documentation="https://github.com/sanskarIN/frametrellis/blob/main/docs/user-guide/container.md" \
      org.opencontainers.image.licenses="GPL-3.0-or-later" \
      org.opencontainers.image.version="${VERSION}" \
      org.opencontainers.image.revision="${REVISION}"
RUN apt-get update \
    && apt-get install --yes --no-install-recommends \
        libmlt++7 libmlt-data libmlt7 libqt6concurrent6 libqt6core6t64 libqt6gui6 \
        qt6-qpa-plugins fonts-dejavu-core fonts-noto-core \
    && rm -rf /var/lib/apt/lists/* \
    && mkdir -p /usr/lib/frametrellis/mlt-7 \
    && for module in core avformat plus normalize; do \
           ln -s "$(ls /usr/lib/*/mlt-7/libmlt${module}.so)" /usr/lib/frametrellis/mlt-7/; \
       done \
    && mkdir -p /usr/share/doc/frametrellis \
    && dpkg-query --show --showformat='${Package} ${Version}\n' \
        > /usr/share/doc/frametrellis/debian-packages.txt
COPY --from=build /out/ /
COPY <<"EOF" /usr/share/doc/frametrellis/README.txt
frametrellis-render
===================

The headless renderer of FrameTrellis, a free and open-source video editor:
https://github.com/sanskarIN/frametrellis

Render a project whose folder is mounted at /work (`frametrellis-render --version` prints the
version of this image):

    docker run --rm --user "$(id -u):$(id -g)" -v "$PWD:/work" \
        ghcr.io/sanskarin/frametrellis-render \
        --project /work/my-project.ftproj --output /work/my-video.mp4

Check the image with: --self-test /tmp/self-test

License: FrameTrellis is free software under the GNU General Public License, version 3 or
later (LICENSE in this folder). It comes with ABSOLUTELY NO WARRANTY. Its source code is at
https://github.com/sanskarIN/frametrellis, with one tag per version.

The image is based on Debian 13. Qt, MLT, FFmpeg and the other libraries are Debian packages;
their versions are listed in debian-packages.txt and their license files are in
/usr/share/doc/<package>/copyright. The source code of every Debian package at the listed
version is available from the Debian archive and https://snapshot.debian.org/.
EOF
ENV FRAMETRELLIS_MLT_REPOSITORY=/usr/lib/frametrellis/mlt-7 \
    MLT_DATA=/usr/share/mlt-7 \
    HOME=/tmp \
    QT_QPA_PLATFORM=offscreen
WORKDIR /work
ENTRYPOINT ["frametrellis-render"]
CMD ["--help"]
