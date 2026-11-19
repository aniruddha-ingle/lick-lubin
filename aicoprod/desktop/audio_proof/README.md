# Lick-Lubin Audio Proof

A small JUCE standalone app for the first Orchid sample playback slice. It is
separate from the CoreS3 firmware and Python host bridge.

## Prerequisites

- macOS with Xcode Command Line Tools
- CMake 3.22 or newer
- Git
- Network access on the first CMake configure so FetchContent can retrieve
  JUCE 9.0.5

If CMake is missing and Homebrew is installed, install it with
`brew install cmake`. This project does not install tools automatically.

## Configure and build

From the repository root:

```sh
cmake -S aicoprod/desktop/audio_proof \
  -B aicoprod/desktop/audio_proof/build \
  -DCMAKE_BUILD_TYPE=Release
cmake --build aicoprod/desktop/audio_proof/build --config Release
```

The app uses `ORCHID_SAMPLE_ROOT` when it is set. Otherwise, it looks for the
local pack at `$HOME/Music/Lick-Lubin/Orchid Sample Pack`. If neither location
is available, it reports the setup error and the path to the expected local
folder. To use a different location, set the environment variable before
launching:

```sh
export ORCHID_SAMPLE_ROOT="/path/to/Cymatics - Orchid - Sample Pack"
open "aicoprod/desktop/audio_proof/build/LickLubinAudioProof_artefacts/Release/Lick-Lubin Audio Proof.app"
```

The embedded manifest catalogs the ten curated Orchid aliases and New Flow MIDI
metadata. The app still plays the Clean kick as the audio proof and imports the
MIDI notes into an in-memory editable motif model; importing does not place the
motif into an arrangement. A separate 16-bar grid holds editable one-bar clip
cells for the ten aliases across INTRO (4 bars), VERSE (8 bars), and CHORUS (4
bars). Grid edits currently change arrangement state only; they do not affect
the Clean kick audio proof. Keep the sample pack and MIDI files outside the
repository; only their manifest-relative paths are checked in.
