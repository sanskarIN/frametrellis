#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Optional *future* macOS production-signing candidate. Does not publish a release.
# Requires a real Developer ID cert and locally stored notarytool profile.
# Usage: FT_DEVELOPER_ID_APPLICATION='Developer ID Application: ...' \
#        FT_NOTARYTOOL_PROFILE='local-keychain-profile' \
#        bash packaging/macos/notarize-candidate.sh <audited-app> <output-dir> <version>
set -euo pipefail

if [[ $(uname -s) != Darwin || $# -ne 3 ]]; then
    echo "Usage on native macOS: $0 <audited-app> <output-dir> <x.y.z>" >&2
    exit 2
fi
src="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"
out="$2"
version="$3"
arch="$(uname -m)"
case "$arch" in
    arm64|x86_64) ;;
    *) echo "Unsupported macOS architecture: $arch" >&2; exit 2 ;;
esac
if [[ ! "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "Version must match x.y.z" >&2
    exit 2
fi
identity="${FT_DEVELOPER_ID_APPLICATION:-}"
profile="${FT_NOTARYTOOL_PROFILE:-}"
if [[ -z "$identity" || -z "$profile" ]]; then
    echo "Developer ID identity and notarytool keychain profile are required" >&2
    exit 2
fi
for tool in ditto codesign xcrun hdiutil lipo shasum python3; do
    command -v "$tool" >/dev/null || { echo "Required tool not found: $tool" >&2; exit 1; }
done
if [[ ! -d "$src/Contents" || ! -x "$src/Contents/MacOS/frametrellis" ||
      ! -x "$src/Contents/MacOS/frametrellis-render" ]]; then
    echo "Input is not a complete FrameTrellis .app (with adjacent renderer)" >&2
    exit 1
fi
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
project_version="$(python3 - "$root/CMakeLists.txt" <<'PY'
import re
import sys
from pathlib import Path

source = Path(sys.argv[1]).read_text(encoding="utf-8")
found = re.search(r"project\(FrameTrellis\s+VERSION\s+([0-9.]+)", source)
print(found.group(1) if found else "")
PY
)"
if [[ "$version" != "$project_version" ]]; then
    echo "Refusing to sign a different version (requested $version; CMake $project_version)" >&2
    exit 1
fi
# This is a hard dependency and local-path audit, NOT licensing certification.
python3 "$root/packaging/macos/verify_bundle.py" "$src"
for exe in frametrellis frametrellis-render; do
    if ! lipo -archs "$src/Contents/MacOS/$exe" | tr ' ' '\n' | grep -Fxq "$arch"; then
        echo "Bundled $exe is missing required native architecture $arch" >&2
        exit 1
    fi
done
mkdir -p "$out"
out="$(cd "$out" && pwd)"
filename="FrameTrellis-$version-macos-$arch.dmg"
if [[ -e "$out/$filename" || -e "$out/$filename.sha256" ]]; then
    echo "Refusing to overwrite existing production candidate: $filename" >&2
    exit 1
fi
staging="$(mktemp -d "$out/.ft-notarize.XXXXXXXX")"
app="$staging/FrameTrellis.app"
dmg="$staging/$filename"
# The Apple notary service can take minutes. Another signer might create the
# same candidate while this run waits. Track exactly which final output *we*
# created, so a failed checksum handoff cannot overwrite or delete its file.
linked_dmg=false
published=false
cleanup() {
    if [[ "$linked_dmg" == true && "$published" != true &&
          -f "$out/$filename" && "$out/$filename" -ef "$dmg" ]]; then
        rm -f -- "$out/$filename"
    fi
    rm -rf -- "$staging"
}
trap cleanup EXIT
ditto "$src" "$app"

# Code-sign from the deepest native components outward. A single --deep
# signature on the outer app is not proof that dynamic Qt/MLT services and
# the independent render-helper executable have valid hardened signatures.
while IFS= read -r -d '' file; do
    if /usr/bin/file -b "$file" | grep -Fq 'Mach-O'; then
        codesign --force --options runtime --timestamp --sign "$identity" "$file"
    fi
done < <(find "$app/Contents/Frameworks" "$app/Contents/PlugIns" -type f -print0)
while IFS= read -r -d '' framework; do
    codesign --force --options runtime --timestamp --sign "$identity" "$framework"
done < <(find "$app/Contents/Frameworks" -type d -name '*.framework' -print0)
codesign --force --options runtime --timestamp --sign "$identity" \
    "$app/Contents/MacOS/frametrellis-render"
codesign --force --options runtime --timestamp --sign "$identity" "$app"
codesign --verify --deep --strict --verbose=2 "$app"

hdiutil create -quiet -volname 'FrameTrellis' -srcfolder "$app" -format UDZO "$dmg"
hdiutil verify "$dmg"
# No Apple account passwords, API keys or .p8 material appear in command
# arguments or CI logs. The keychain profile must already exist on this Mac.
xcrun notarytool submit "$dmg" --keychain-profile "$profile" --wait \
    --output-format json > "$staging/notary-result.json"
python3 - "$staging/notary-result.json" <<'PY'
import json
import sys
from pathlib import Path

result = json.loads(Path(sys.argv[1]).read_text(encoding="utf-8"))
if result.get("status") != "Accepted":
    raise SystemExit("Apple notarization did not return Accepted; refusing production filename")
print("Apple notarization status: Accepted")
PY
xcrun stapler staple "$dmg"
xcrun stapler validate "$dmg"
hdiutil verify "$dmg"
# Never expose a production-style DMG until signing/notarization and staple
# verification succeeded. Gatekeeper testing on a quarantined clean Mac is
# STILL required separately. Staging is on the SAME filesystem as output.
# Unlike mv, hard-link creation fails atomically if another signer already
# produced this version/arch, even if it appeared during notarization.
if ! ln "$dmg" "$out/$filename"; then
    echo "Refusing to replace another notarized candidate: $filename" >&2
    exit 1
fi
linked_dmg=true
(cd "$staging" && shasum -a 256 "$filename" > "$filename.sha256")
# Likewise never replace someone else's checksum. The EXIT trap removes only
# our own hard-linked DMG if this second exclusive publication step fails.
if ! ln "$staging/$filename.sha256" "$out/$filename.sha256"; then
    echo "Refusing to replace another notarized checksum: $filename.sha256" >&2
    exit 1
fi
published=true
echo "Created notarized *candidate* DMG: $out/$filename"
echo "Manual clean-Mac Gatekeeper, license and source-SHA checks remain mandatory."
