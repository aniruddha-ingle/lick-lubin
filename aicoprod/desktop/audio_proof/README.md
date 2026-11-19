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

The 16-bar arrangement grid covers INTRO (4 bars), VERSE (8 bars), and CHORUS
(4 bars). Clean starts enabled in bar 1. While stopped, click cells to choose
which samples play in each bar. Play starts at bar 1 and triggers each active
cell on all four beats of its bar at 102 BPM. The highlighted column follows
playback, which stops automatically after bar 16.

Select a track in **Track effects** to change its settings. **Cut itself**
releases the track's previous hit when a new hit starts. **Echo** adds an
eighth-note repeat. Select a room or hall reverb, or choose a sidechain source
to lower this track's volume when the source plays.

The app loads the ten curated Orchid samples before playback and lists any
unavailable tracks. It also imports the New Flow MIDI into an in-memory motif
without adding it to the grid. Keep the sample pack and MIDI files outside the
repository; the manifest contains their relative paths.
