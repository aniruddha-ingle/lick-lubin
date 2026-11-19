#!/bin/bash
set -eu
cd "$(dirname "$0")"
build_dir=$(mktemp -d)
trap 'rm -rf "$build_dir"' EXIT
cat > "$build_dir/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<plist version="1.0"><dict>
<key>CFBundleIdentifier</key><string>org.lick-lubin.orchid-dictate</string>
<key>NSMicrophoneUsageDescription</key><string>Capture a short spoken arrangement command.</string>
<key>NSSpeechRecognitionUsageDescription</key><string>Transcribe arrangement commands on this Mac.</string>
</dict></plist>
PLIST
swiftc orchid_dictate.swift -Xlinker -sectcreate -Xlinker __TEXT -Xlinker __info_plist -Xlinker "$build_dir/Info.plist" -o "$build_dir/orchid-dictate"
codesign --force --sign - "$build_dir/orchid-dictate"
mv "$build_dir/orchid-dictate" ./orchid-dictate
