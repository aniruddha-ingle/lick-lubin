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

Set `ORCHID_SAMPLE_ROOT` to the Orchid Sample Pack folder before launching the
app. For the default iCloud Drive location shown in the project setup:

```sh
export ORCHID_SAMPLE_ROOT="$HOME/Library/Mobile Documents/com~apple~CloudDocs/Documents/Image-Line/FL Studio/Presets/Clipboard files/Samples/Cymatics - Orchid - Sample Pack"
open "aicoprod/desktop/audio_proof/build/LickLubinAudioProof_artefacts/Release/Lick-Lubin Audio Proof.app"
```

The app's one-row manifest is embedded into the executable using JUCE
`BinaryData`; only the sample pack itself remains outside the repository.
