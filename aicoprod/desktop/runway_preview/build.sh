#!/bin/bash
set -eu
cd "$(dirname "$0")"
bundle="${1:-$PWD/build/Lick-Lubin Runway.app}"
mkdir -p "$bundle/Contents/MacOS" "$bundle/Contents/Resources"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
swiftc -parse-as-library -O -target "$(uname -m)-apple-macosx15.0" ./*.swift -o "$tmp/LickLubinRunway"
cp Info.plist "$bundle/Contents/Info.plist"
cp ../audio_proof/manifest.json "$bundle/Contents/Resources/manifest.json"
if [ ! -x ../../orchid-dictate ]; then bash ../../build_orchid_dictate.sh; fi
cp ../../orchid-dictate "$bundle/Contents/Resources/orchid-dictate"
install -m 755 "$tmp/LickLubinRunway" "$bundle/Contents/MacOS/LickLubinRunway.new"
mv -f "$bundle/Contents/MacOS/LickLubinRunway.new" "$bundle/Contents/MacOS/LickLubinRunway"
codesign --force --sign - "$bundle"
echo "$bundle"
