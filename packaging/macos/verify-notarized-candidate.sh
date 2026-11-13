#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Read-only on the original DMG. Run on a clean native Mac after notarization;
# never signs, staples, publishes, uploads or modifies the distribution file.
# Usage: bash packaging/macos/verify-notarized-candidate.sh <candidate.dmg>
set -euo pipefail

if [[ $(uname -s) != Darwin || $# -ne 1 ]]; then
    echo "Usage on native macOS: $0 <FrameTrellis-x.y.z-macos-arch.dmg>" >&2
    exit 2
fi
for command in hdiutil codesign spctl xcrun ditto shasum xattr awk grep; do
    command -v "$command" >/dev/null || {
        echo "Required native macOS command not found: $command" >&2
        exit 1
    }
done

candidate="$1"
if [[ ! -f "$candidate" || -L "$candidate" ]]; then
    echo "Candidate must be a real regular DMG file" >&2
    exit 1
fi
file="$(basename "$candidate")"
if [[ ! "$file" =~ ^FrameTrellis-([0-9]+\.[0-9]+\.[0-9]+)-macos-(arm64|x86_64)\.dmg$ ]]; then
    echo "Refusing a development, unnotarized or wrongly named DMG: $file" >&2
    exit 1
fi
version="${BASH_REMATCH[1]}"
arch="${BASH_REMATCH[2]}"
if [[ "$(uname -m)" != "$arch" ]]; then
    echo "Run the $arch candidate on a native $arch Mac; no cross-architecture claims" >&2
    exit 1
fi
parent="$(cd "$(dirname "$candidate")" && pwd -P)"
candidate="$parent/$file"
checksum="$candidate.sha256"
if [[ ! -f "$checksum" || -L "$checksum" ]]; then
    echo "Missing regular candidate checksum: $checksum" >&2
    exit 1
fi
# Require ONE basename-only checksum, never an absolute CI/developer path.
expected="$(awk -v name="$file" '
    NF == 2 && $2 == name && length($1) == 64 && $1 ~ /^[0-9a-fA-F]+$/ { valid++ }
    END { if (NR != 1 || valid != 1) exit 1; print name }
' "$checksum")" || {
    echo "Checksum manifest must contain exactly one candidate basename and SHA-256" >&2
    exit 1
}
[[ "$expected" == "$file" ]] || exit 1
(cd "$parent" && shasum -a 256 -c "$(basename "$checksum")")

hdiutil verify "$candidate"
xcrun stapler validate "$candidate"

scratch="$(mktemp -d "${TMPDIR:-/tmp}/ft-gatekeeper.XXXXXXXX")"
mounted=false
cleanup() {
    if [[ "$mounted" == true ]]; then
        hdiutil detach -quiet "$scratch/mount" >/dev/null 2>&1 || {
            echo "WARNING: could not detach temporary mounted DMG: $scratch/mount" >&2
            return
        }
    fi
    rm -rf -- "$scratch"
}
trap cleanup EXIT
mkdir -p "$scratch/mount"
hdiutil attach -quiet -readonly -nobrowse -mountpoint "$scratch/mount" "$candidate"
mounted=true

app="$scratch/mount/FrameTrellis.app"
if [[ ! -d "$app/Contents" ||
      ! -x "$app/Contents/MacOS/frametrellis" ||
      ! -x "$app/Contents/MacOS/frametrellis-render" ]]; then
    echo "Final mounted DMG does not contain the complete editor and renderer" >&2
    exit 1
fi

# Run code-sign verification against the FINAL read-only mounted app, not a
# pre-notarization source or separately staged binary.
codesign --verify --deep --strict "$app"
signature="$(codesign --display --verbose=4 "$app" 2>&1)"
if ! grep -Fq 'Authority=Developer ID Application:' <<< "$signature"; then
    echo "Missing production Developer ID Application certificate" >&2
    exit 1
fi
team="$(grep '^TeamIdentifier=' <<< "$signature" | head -n 1)"
if [[ -z "$team" || "$team" == "TeamIdentifier=not set" ]]; then
    echo "Production signature has no Apple Developer team identity" >&2
    exit 1
fi

# Verify Gatekeeper as downloaded: quarantine a COPY only, never modify the
# DMG or mounted read-only app. Finder GUI, user audio and real media QA are
# still a separate manual acceptance step.
copy="$scratch/FrameTrellis.app"
ditto "$app" "$copy"
quarantine="0081;$(printf '%x' "$(date +%s)");FrameTrellis-QA;https://github.com/sanskarIN/frametrellis"
xattr -r -w com.apple.quarantine "$quarantine" "$copy"
codesign --verify --deep --strict "$copy"
spctl --assess --type execute --verbose=4 "$copy"

echo "PASS: native macOS $arch FrameTrellis $version notarized candidate"
echo "PASS: candidate checksum, DMG structure, stapled ticket, Developer ID and Gatekeeper"
echo "LIMIT: Finder launch, user audio, real footage, clean-runtime compatibility,"
echo "       complete source/licensing inventory and owner release approval remain mandatory."
