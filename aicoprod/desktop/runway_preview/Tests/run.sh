#!/bin/bash
set -eu
cd "$(dirname "$0")/.."
qa=$(mktemp -d /tmp/orchid-story-XXXXXX)
trap 'rm -rf "$qa"' EXIT
swiftc -target "$(uname -m)-apple-macosx15.0" -D RUNWAY_TEST -parse-as-library ./*.swift Tests/Story.swift -o "$qa/story"
mkdir -p "$qa/Home"
CFFIXED_USER_HOME="$qa/Home" ORCHID_REHEARSAL=1 "$qa/story"
