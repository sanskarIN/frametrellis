#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# EXPERIMENTAL x86_64 Linux AppImage: not an approved stable release.
# Usage: bash packaging/linux/build-appimage.sh build/linux dist/linux "$HOME/mlt"
set -euo pipefail

if [[ $(uname -s) != Linux || $(uname -m) != x86_64 ]]; then
    echo "This AppImage builder requires native Linux x86_64" >&2
    exit 2
fi
if [[ $# -ne 3 ]]; then
    echo "Usage: $0 <cmake-build-dir> <output-dir> <mlt-prefix>" >&2
    exit 2
fi
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
build="$(cd "$1" && pwd)"
mkdir -p "$2"
output="$(cd "$2" && pwd)"
mlt="$(cd "$3" && pwd)"
appdir="$output/AppDir"
if [[ -e "$appdir" ]]; then
    echo "Refusing to overwrite an existing AppDir: $appdir" >&2
    exit 1
fi

cmake --install "$build" --prefix "$appdir/usr"
for required in \
    "$appdir/usr/bin/frametrellis" \
    "$appdir/usr/bin/frametrellis-render" \
    "$appdir/usr/share/applications/frametrellis.desktop" \
    "$appdir/usr/share/icons/hicolor/scalable/apps/frametrellis.svg" \
    "$appdir/usr/share/mime/packages/frametrellis.xml"; do
    [[ -s "$required" ]] || { echo "Missing installed file $required" >&2; exit 1; }
done
modules="$mlt/lib/mlt-7"
data="$mlt/share/mlt-7"
[[ -d "$modules" && -d "$data" ]] || {
    echo "Missing MLT service modules or data under $mlt" >&2
    exit 1
}
mkdir -p "$appdir/usr/lib/mlt-7" "$appdir/usr/share/mlt-7"
cp -a "$data/." "$appdir/usr/share/mlt-7/"
mlt_modules=()
for name in core avformat plus normalize; do
    candidate="$modules/libmlt${name}.so"
    if [[ ! -f "$candidate" ]]; then
        echo "Required MLT service missing: $candidate" >&2
        exit 1
    fi
    cp -L "$candidate" "$appdir/usr/lib/mlt-7/"
    mlt_modules+=(--library "$candidate")
done
shopt -s nullglob
mlt_binaries=("$mlt/lib/libmlt-7.so."* "$mlt/lib/libmlt++-7.so."*)
if [[ ${#mlt_binaries[@]} -lt 2 ]]; then
    echo "Expected libmlt++ and libmlt shared runtimes in $mlt/lib" >&2
    exit 1
fi

toolsdir="$output/bundle-tools"
mkdir -p "$toolsdir"
linuxdeploy="$toolsdir/linuxdeploy-x86_64.AppImage"
qtdeploy="$toolsdir/linuxdeploy-plugin-qt-x86_64.AppImage"
# linuxdeploy has a publisher-provided digest. The older Qt plug-in release
# does NOT publish a digest via GitHub's API. Two independent successful
# FrameTrellis Linux AppImage CI runs recorded the SAME plug-in SHA-256.
# Pin that observed baseline to fail closed if the downloaded artifact drifts.
# This is reproducibility evidence, NOT independent upstream provenance;
# publisher/source verification remains a stable-release requirement.
linuxdeploy_url="https://github.com/linuxdeploy/linuxdeploy/releases/download/1-alpha-20251107-1/linuxdeploy-x86_64.AppImage"
linuxdeploy_sha="c20cd71e3a4e3b80c3483cef793cda3f4e990aca14014d23c544ca3ce1270b4d"
qtdeploy_url="https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/1-alpha-20250213-1/linuxdeploy-plugin-qt-x86_64.AppImage"
qtdeploy_sha="15106be885c1c48a021198e7e1e9a48ce9d02a86dd0a1848f00bdbf3c1c92724"
curl -fLsS --retry 3 "$linuxdeploy_url" -o "$linuxdeploy"
echo "$linuxdeploy_sha  $linuxdeploy" | sha256sum -c -
curl -fLsS --retry 3 "$qtdeploy_url" -o "$qtdeploy"
# Enforce the known successful CI baseline before EVER executing this tool.
# An upstream replacement or unexpected download fails this build; do not
# derive the expected value from the file just downloaded.
echo "$qtdeploy_sha  $qtdeploy" | sha256sum -c -
# Also publish a basename-only digest for downstream offline review. The
# observed digest is NOT an authenticated third-party signature.
(cd "$toolsdir" && sha256sum "$(basename "$qtdeploy")") \
    | tee "$output/linuxdeploy-plugin-qt-downloaded.sha256"
chmod +x "$linuxdeploy" "$qtdeploy"

export ARCH=x86_64 APPIMAGE_EXTRACT_AND_RUN=1
export LD_LIBRARY_PATH="$mlt/lib:${LD_LIBRARY_PATH:-}"
export QML_SOURCES_PATHS="$root/src/ui/qml"
export EXTRA_QT_MODULES='svg'
export EXTRA_PLATFORM_PLUGINS='libqoffscreen.so'
if [[ -z "${QMAKE:-}" ]]; then
    if [[ -n "${QT_ROOT_DIR:-}" && -x "$QT_ROOT_DIR/bin/qmake6" ]]; then
        export QMAKE="$QT_ROOT_DIR/bin/qmake6"
    elif command -v qmake6 >/dev/null; then
        export QMAKE="$(command -v qmake6)"
    else
        echo "Qt 6 qmake is required by linuxdeploy-plugin-qt" >&2
        exit 1
    fi
fi
# Preserve actual MLT module locations. The --library inputs also instruct
# linuxdeploy to inspect transitive dependencies of services loaded by name,
# which cannot be found from the main executable's direct ELF dependencies.
"$linuxdeploy" \
    --appdir "$appdir" \
    --executable "$appdir/usr/bin/frametrellis" \
    --executable "$appdir/usr/bin/frametrellis-render" \
    --desktop-file "$appdir/usr/share/applications/frametrellis.desktop" \
    --icon-file "$appdir/usr/share/icons/hicolor/scalable/apps/frametrellis.svg" \
    "${mlt_modules[@]}" \
    --plugin qt
# MLT's upstream MODULE binaries retain an absolute build-prefix RPATH
# (/home/runner/mlt/lib) and must instead search only the packaged usr/lib.
# Do this before AppImage compression, then let the dependency audit fail if
# anything still points at the CI machine.
for module in "$appdir"/usr/lib/mlt-7/libmlt*.so; do
    patchelf --set-rpath '$ORIGIN/..:$ORIGIN' "$module"
done
"$linuxdeploy" --appdir "$appdir" --output appimage
# Verify we have actually copied Qt/MLT/codec closure into AppDir. The
# production release must also pass clean-distro tests and license audit.
python3 "$root/packaging/linux/verify_appdir.py" "$appdir" \
    | tee "$output/AppDir-dependency-audit.txt"
# linuxdeploy writes AppImage to the current directory, not necessarily $out.
shopt -s nullglob
results=(./FrameTrellis-*.AppImage ./frametrellis-*.AppImage)
if [[ ${#results[@]} -ne 1 ]]; then
    echo "Expected one AppImage generated by linuxdeploy; found ${#results[@]}" >&2
    exit 1
fi
dest="$output/FrameTrellis-linux-x86_64-EXPERIMENTAL.AppImage"
[[ ! -e "$dest" ]] || { echo "Refusing overwrite: $dest" >&2; exit 1; }
mv "${results[0]}" "$dest"
chmod +x "$dest"
test -s "$dest"
# Emit a filename-only checksum so it still verifies outside GitHub Actions.
(cd "$output" && sha256sum "$(basename "$dest")" > "$(basename "$dest").sha256")

{
    echo "Experimental AppImage (not the stable v1.5.0 release)"
    echo "Qt version: ${QT_VERSION:-from-build-environment}"
    echo "MLT prefix: $mlt"
    echo "linuxdeploy release: 1-alpha-20251107-1 (pinned SHA256)"
    echo "Qt deploy plugin: 1-alpha-20250213-1 (CI-baseline SHA256 pinned; independent upstream verification pending)"
    echo "This file has NOT passed independent clean-distro QA, full third-party"
    echo "license/source provenance, or broad glibc and audio-device testing."
} > "$output/DEVELOPMENT-NOTICES.txt"
echo "Prepared experimental AppImage: $dest"
