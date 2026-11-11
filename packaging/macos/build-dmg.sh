#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Candidate macOS desktop packaging (NOT a signed/notarized stable release).
# Usage: bash packaging/macos/build-dmg.sh build/macos dist/macos
set -euo pipefail

if [[ $(uname -s) != Darwin ]]; then
    echo "macOS packaging requires a native Mac runner" >&2
    exit 2
fi
if [[ $# -ne 2 ]]; then
    echo "Usage: $0 <cmake-build-directory> <output-directory>" >&2
    exit 2
fi
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
build="$(cd "$1" && pwd)"
mkdir -p "$2"
output="$(cd "$2" && pwd)"
source_app="$build/bin/frametrellis.app"
if [[ ! -x "$source_app/Contents/MacOS/frametrellis" ||
      ! -x "$source_app/Contents/MacOS/frametrellis-render" ]]; then
    echo "Expected the complete editor and renderer in $source_app/Contents/MacOS" >&2
    exit 1
fi
for tool in brew dylibbundler otool install_name_tool hdiutil codesign ditto; do
    command -v "$tool" >/dev/null || { echo "Missing $tool" >&2; exit 1; }
done
arch="$(uname -m)"
case "$arch" in arm64|x86_64) ;; *) echo "Unsupported Mac architecture $arch" >&2; exit 1 ;; esac
app="$output/FrameTrellis-macos-$arch-development.app"
dmg="$output/FrameTrellis-macos-$arch-UNNOTARIZED-development.dmg"
if [[ -e "$app" || -e "$dmg" ]]; then
    echo "Refusing to overwrite an existing Mac artifact" >&2
    exit 1
fi
ditto "$source_app" "$app"
contents="$app/Contents"
mkdir -p "$contents/Frameworks" "$contents/PlugIns/mlt-7" "$contents/Resources/mlt-7"
mlt_prefix="$(brew --prefix mlt)"
qt_prefix="$(brew --prefix qt)"
modules="$(pkg-config --variable=moduledir mlt-framework-7 2>/dev/null || true)"
data="$(pkg-config --variable=mltdatadir mlt-framework-7 2>/dev/null || true)"
[[ -d "$modules" ]] || modules="$mlt_prefix/lib/mlt-7"
[[ -d "$data" ]] || data="$mlt_prefix/share/mlt-7"
if [[ ! -d "$modules" || ! -d "$data" ]]; then
    echo "MLT modules and data missing: $modules ; $data" >&2
    exit 1
fi
ditto "$modules" "$contents/PlugIns/mlt-7"
ditto "$data" "$contents/Resources/mlt-7"
mkdir -p "$contents/Resources/licenses"
cp "$root/LICENSE" "$root/THIRD_PARTY_NOTICES.md" "$contents/Resources/licenses/"

qtdeploy="$qt_prefix/bin/macdeployqt"
[[ -x "$qtdeploy" ]] || { echo "Missing $qtdeploy" >&2; exit 1; }
# -no-codesign: all native modules are still being copied and re-linked. Sign
# the final complete tree *after* dylibbundler has finished modifying Mach-O.
"$qtdeploy" "$app" "-executable=$contents/MacOS/frametrellis-render" \
    "-qmldir=$root/src/ui/qml" -always-overwrite -no-codesign

# MLT loads service plug-ins dynamically. Include the plug-ins explicitly as
# dylibbundler inputs so its dependency closure covers FFmpeg and other codecs.
# Qt frameworks are deployed by macdeployqt (and must not be copied twice).
inputs=(-x "$contents/MacOS/frametrellis" -x "$contents/MacOS/frametrellis-render")
while IFS= read -r -d '' module; do
    inputs+=(-x "$module")
done < <(find "$contents/PlugIns/mlt-7" -type f \( -name '*.dylib' -o -name '*.so' \) -print0)
dylibbundler -b -cd -ns -d "$contents/Frameworks" \
    -p '@executable_path/../Frameworks/' -i "$qt_prefix/lib" \
    -s "$mlt_prefix/lib" -s "$(brew --prefix ffmpeg)/lib" \
    -s "$(brew --prefix sdl2)/lib" "${inputs[@]}"

# Rebuilding/copying native libraries invalidates their previous signatures.
# Ad-hoc signing makes it possible to check code integrity in CI but does not
# provide Developer ID identity or Apple notarization.
codesign --force --deep --sign - --timestamp=none "$app"
codesign --verify --deep --strict "$app"

python3 "$root/packaging/macos/verify_bundle.py" "$app"

{
    echo "FrameTrellis development packaging dependencies"
    echo "Build architecture: $arch"
    brew list --versions qt mlt ffmpeg sdl2 dylibbundler || true
    echo "This unsigned Developer ID / unnotarized test artifact is NOT"
    echo "approved as a v1.5.0 stable release and may be rejected by Gatekeeper."
    echo "Third-party license closure and clean-Mac QA remain release blockers."
} > "$output/FrameTrellis-macos-$arch-DEVELOPMENT-NOTICES.txt"

# Produce a development DMG for smoke and future clean-host testing; never
# publish it as the stable Mac binary without identity signing/notarization.
hdiutil create -quiet -volname 'FrameTrellis Development' \
    -srcfolder "$app" -format UDZO -ov "$dmg"
test -s "$dmg"
shasum -a 256 "$dmg" > "$dmg.sha256"
echo "Prepared development-only macOS disk image: $dmg"
