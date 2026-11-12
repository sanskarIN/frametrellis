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
# Only bundle services actually required by FrameTrellis. Homebrew MLT
# contains optional plugins linked against unrelated toolkits, hardware and
# codec stacks. Copying all of them makes reliable dependency closure and
# licensing needlessly harder and can load incompatible Qt plugins.
for name in core avformat plus normalize; do
    # MLT's CMake MODULE libraries usually carry .so even on macOS, unlike
    # ordinary SHARED .dylib outputs. Probe both documented native suffixes.
    file=""
    for suffix in so dylib; do
        candidate="$modules/libmlt${name}.$suffix"
        if [[ -f "$candidate" ]]; then
            file="$candidate"
            break
        fi
    done
    if [[ -z "$file" ]]; then
        echo "Missing essential MLT module $name in $modules" >&2
        find "$modules" -maxdepth 1 -name 'libmlt*' -print >&2
        exit 1
    fi
    ditto "$file" "$contents/PlugIns/mlt-7/${file##*/}"
done
ditto "$data" "$contents/Resources/mlt-7"
mkdir -p "$contents/Resources/licenses"
cp "$root/LICENSE" "$root/THIRD_PARTY_NOTICES.md" "$contents/Resources/licenses/"

qtdeploy="$qt_prefix/bin/macdeployqt"
[[ -x "$qtdeploy" ]] || { echo "Missing $qtdeploy" >&2; exit 1; }
# -no-codesign: all native modules are still being copied and re-linked. Sign
# the final complete tree *after* dylibbundler has finished modifying Mach-O.
"$qtdeploy" "$app" "-executable=$contents/MacOS/frametrellis-render" \
    "-qmldir=$root/src/ui/qml" -always-overwrite -no-codesign

# macdeployqt copies optional Qt 3D/PDF/VirtualKeyboard/Timeline plugins even
# though the editor only imports QtQuick, Basic Controls, Dialogs and Layouts.
# Prune ONLY the known-unused modules after checking all project QML imports.
# This prevents unresolvable optional plugin dependencies from entering the
# app. The remaining dependency verifier still rejects missing linked code.
python3 "$root/packaging/macos/prune_unused_qt.py" "$app" "$root/src/ui/qml"

# macdeployqt defaults to Cocoa on macOS and does NOT install Qt's offscreen
# QPA plugin. Our renderer creates a QGuiApplication with an offscreen backend
# on headless CI and on supported unattended exports. A bundle containing only
# libqcocoa.dylib passes otool auditing yet fails at startup ("offscreen not
# found"). Resolve the plugin path from the SAME Homebrew Qt that compiled the
# app and ship the plugin inside Contents/PlugIns/platforms.
plugin_directory=""
qtbase_prefix="$(brew --prefix qtbase)"
for candidate in \
    "$("$qt_prefix/bin/qtpaths6" --plugin-dir 2>/dev/null || true)" \
    "$("$qt_prefix/bin/qtpaths" --plugin-dir 2>/dev/null || true)" \
    "$("$qt_prefix/bin/qmake6" -query QT_INSTALL_PLUGINS 2>/dev/null || true)" \
    "$("$qt_prefix/bin/qmake" -query QT_INSTALL_PLUGINS 2>/dev/null || true)" \
    "$qt_prefix/share/qt/plugins" \
    "$qtbase_prefix/share/qt/plugins"; do
    if [[ -n "$candidate" && -f "$candidate/platforms/libqoffscreen.dylib" ]]; then
        plugin_directory="$candidate"
        break
    fi
done
if [[ -z "$plugin_directory" ]]; then
    echo "Qt offscreen QPA plugin missing from Homebrew Qt; cannot verify bundled renderer" >&2
    exit 1
fi
mkdir -p "$contents/PlugIns/platforms"
ditto "$plugin_directory/platforms/libqoffscreen.dylib" \
    "$contents/PlugIns/platforms/libqoffscreen.dylib"
echo "Bundled Qt QPA platforms: cocoa, offscreen"

# Homebrew's SDL2 may be sdl2-compat, which dlopen()s SDL3 during native
# library initialization before main(). Neither otool -L nor dylibbundler
# can discover the dlopen() dependency, so deploy SDL3 explicitly next to
# SDL2 in Frameworks for the compat layer's @loader_path lookup.
sdl3_prefix="$(brew --prefix sdl3)"
sdl3_library="$sdl3_prefix/lib/libSDL3.dylib"
if [[ ! -f "$sdl3_library" ]]; then
    echo "Missing SDL3 runtime library: $sdl3_library" >&2
    exit 1
fi
ditto "$sdl3_library" "$contents/Frameworks/libSDL3.dylib"

# MLT loads service plug-ins dynamically. Include the plug-ins explicitly as
# dylibbundler inputs so its dependency closure covers FFmpeg and other codecs.
# Qt frameworks are deployed by macdeployqt (and must not be copied twice).
# The copied SDL3 is also an input: close its transitive linked dependencies.
inputs=(-x "$contents/MacOS/frametrellis" -x "$contents/MacOS/frametrellis-render"
        -x "$contents/Frameworks/libSDL3.dylib")
while IFS= read -r -d '' module; do
    inputs+=(-x "$module")
done < <(find "$contents/PlugIns/mlt-7" -type f \( -name '*.dylib' -o -name '*.so' \) -print0)
dylibbundler -b -cd -ns -d "$contents/Frameworks" \
    -p '@executable_path/../Frameworks/' -i "$qt_prefix/lib" \
    -s "$mlt_prefix/lib" -s "$(brew --prefix ffmpeg)/lib" \
    -s "$(brew --prefix sdl2)/lib" -s "$sdl3_prefix/lib" "${inputs[@]}"


# Homebrew Qt's copied framework binaries can retain absolute LC_ID_DYLIB
# install names. Relocate their identities to the bundle for future links.
for framework in "$contents"/Frameworks/*.framework; do
    [[ -d "$framework" ]] || continue
    framework_name="${framework##*/}"
    framework_name="${framework_name%.framework}"
    framework_binary="$framework/Versions/A/$framework_name"
    [[ -f "$framework_binary" ]] || continue
    install_id="$(otool -D "$framework_binary" | sed -n '2p' | xargs)"
    case "$install_id" in
        /opt/homebrew/*|/usr/local/*)
            install_name_tool -id \
                "@rpath/$framework_name.framework/Versions/A/$framework_name" \
                "$framework_binary"
            ;;
    esac
done

# Some Homebrew dylibbundler copies keep their original install-name
# identity, particularly Intel brotli runtime libraries. Normalize copied
# dylib identities as well as the Qt framework identities above. This does
# not mask missing dependencies: verify_bundle.py audits their actual links.
for library in "$contents"/Frameworks/*.dylib; do
    [[ -f "$library" && ! -L "$library" ]] || continue
    install_id="$(otool -D "$library" | sed -n '2p' | xargs)"
    case "$install_id" in
        /opt/homebrew/*|/usr/local/*)
            install_name_tool -id "@rpath/${library##*/}" "$library"
            ;;
    esac
done

# Remove build-machine LC_RPATH entries only after deploying dependencies.
# The bundle verifier remains responsible for rejecting unresolved libraries.
# Both steps precede the final ad-hoc code signature.
while IFS= read -r -d '' native_file; do
    if ! /usr/bin/file -b "$native_file" | grep -Fq 'Mach-O'; then
        continue
    fi
    while IFS= read -r runpath; do
        case "$runpath" in
            /opt/homebrew/*|/usr/local/*)
                install_name_tool -delete_rpath "$runpath" "$native_file"
                ;;
        esac
    done < <(otool -l "$native_file" | awk '
        $1 == "cmd" && $2 == "LC_RPATH" { found = 1; next }
        found && $1 == "path" { print $2; found = 0 }
    ')
done < <(find "$contents" -type f -print0)

# Rebuilding/copying native libraries invalidates their previous signatures.
# Ad-hoc signing makes it possible to check code integrity in CI but does not
# provide Developer ID identity or Apple notarization.
codesign --force --deep --sign - --timestamp=none "$app"
codesign --verify --deep --strict "$app"

python3 "$root/packaging/macos/verify_bundle.py" "$app"

{
    echo "FrameTrellis development packaging dependencies"
    echo "Build architecture: $arch"
    brew list --versions qt mlt ffmpeg sdl2 sdl3 dylibbundler || true
    echo "This unsigned Developer ID / unnotarized test artifact is NOT"
    echo "approved as a v1.5.0 stable release and may be rejected by Gatekeeper."
    echo "Third-party license closure and clean-Mac QA remain release blockers."
} > "$output/FrameTrellis-macos-$arch-DEVELOPMENT-NOTICES.txt"

# Produce a development DMG for smoke and future clean-host testing; never
# publish it as the stable Mac binary without identity signing/notarization.
# An opaque -quiet hdiutil failure previously hid an Intel-only DMG error.
# Report available free space and staged bytes, but do not expose user paths
# beyond the short CI-owned package directory already present in build logs.
echo "macOS candidate staging size and free-space evidence:"
du -sh "$app"
df -h "$output"
hdiutil create -volname 'FrameTrellis Development' \
    -srcfolder "$app" -format UDZO "$dmg"
test -s "$dmg"
hdiutil verify "$dmg"
# Record only the DMG filename, never the temporary CI staging path.
# Release-artifact checksums must remain valid after users move the files.
(cd "$output" && shasum -a 256 "$(basename "$dmg")" > "$(basename "$dmg").sha256")
echo "Prepared development-only macOS disk image: $dmg"
