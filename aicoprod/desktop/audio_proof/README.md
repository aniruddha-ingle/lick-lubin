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
(4 bars). The intro opens with a four-bar groove. The upper grid chooses which tracks
play in each bar. The 16-step editor below it sets the rhythm for the selected
track across its enabled bars. Each step is a sixteenth note. For a backbeat,
enable steps 5 and 13 on Pop. Edit while stopped, then press Play to hear the
changes at 102 BPM. The highlighted column follows playback through bar 16.

Select Heavyweight under **Track effects** and choose **Clean ducks Heavyweight** to lower the bass on kick hits. Cut Itself releases the selected track’s previous hit on retrigger. Echo adds tempo-synced eighth-note repeats, and the reverb menu offers room and hall presets. Effects can change while playing.

The app loads the ten curated Orchid samples before playback and lists any
unavailable tracks. It also imports the New Flow MIDI into an in-memory motif
without adding it to the grid. Keep the sample pack and MIDI files outside the
repository; the manifest contains their relative paths.
