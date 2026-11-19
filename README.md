# Lick-Lubin

Lick-Lubin is a voice-controlled, sample-based music co-producer with a small standalone desktop instrument and a tactile M5Stack CoreS3 controller. The demo is moving away from FL Studio toward a purpose-built JUCE app that owns the arrangement, transport, sample playback, and audio output.

The working musical target is **102 BPM, F minor**. The selected library is the user's curated **Cymatics Orchid Sample Pack**. Keep that pack outside the repository and configure its location locally; do not commit sample files.

## Runway Studio

The native Mac preview adds projects, 16-bar loops, a 64-bar runway, live pitched step editing and a project mixer. [Build and rehearse the demo](aicoprod/desktop/runway_preview/README.md).

## Current status

The standalone Mac app plays a programmable 16-bar grid at 102 BPM. Each active bar plays the selected track’s 16-step rhythm. Click a track and edit its sixteenth notes while stopped. Steps 1, 5, 9, and 13 are the four beat starts. During playback you can change Cut Itself, kick-triggered ducking, eighth-note echo, and room or hall reverb.

The current Python host bridge and CoreS3 firmware are retained as legacy voice-input work while the playback engine is replaced. The old FL Studio specifications have been removed from the working tree at the user's direction and remain in Git history.

## Design and implementation plan

See [the Orchid interactive demo plan](docs/ORCHID_INTERACTIVE_DEMO.md) for the architecture, sample aliases, voice-command options, milestones, and verification boundaries.

## Repository map

```text
AGENTS.md
README.md
docs/
  ORCHID_INTERACTIVE_DEMO.md
personas/
  SWE-AGENT.md
  SWE-REVIEWER.md
aicoprod/
  desktop/audio_proof/     # JUCE standalone sample-playback proof
  src/main.cpp             # CoreS3 firmware
  host_bridge.py           # Existing Python voice/MIDI bridge
  mac_mic_simulator.py     # Mac microphone Realtime test utility
  requirements.txt
  platformio.ini
  tests/                   # Existing offline tests for the legacy bridge
```

## JUCE audio proof

The first proof's build and local sample-root instructions are in [`aicoprod/desktop/audio_proof/README.md`](aicoprod/desktop/audio_proof/README.md). It requires CMake 3.22 or newer, Xcode Command Line Tools, Git, and network access for CMake to fetch the pinned JUCE source on first configure.

Set `ORCHID_SAMPLE_ROOT` to the local `Cymatics - Orchid - Sample Pack` folder. The app reads only the curated files listed in its manifest.

## Voice control without API credits

With the JUCE app open, run these commands from the repository root. Place both sounds in bar one to hear them together. Turn echo on and off during playback to compare the rimshot.

```sh
python3 aicoprod/voice_commands.py "put Clean in bar one" --socket
python3 aicoprod/voice_commands.py "put Bunker in bar one" --socket
python3 aicoprod/voice_commands.py "add echo to Bunker" --socket
python3 aicoprod/voice_commands.py "play" --socket
```

Try “remove Clean from step seven” to hear the kick without its pickup between beats two and three. Say “put Clean on step seven” to bring it back. Each edit applies to that track across its active bars.

Other phrases include “cut itself on for Perfect Donk”, “sidechain Clean to Heavyweight”, “put Pop in a big room”, and “reverb off for Pop”. A sidechain is audible when the source triggers while the target is sounding. Cut Itself affects overlapping hits on the same track. Select Roll in the grid to use the recorded hat roll.

For a spoken command, build the Mac helper once and invoke the client for each phrase. Grant macOS microphone and speech recognition permission when prompted, then speak after the listening cue. This uses on-device English recognition on the default Mac input. The typed path above also works when on-device recognition is unavailable.

```sh
bash aicoprod/build_orchid_dictate.sh
python3 aicoprod/voice_commands.py --mic --socket
```

JUCE owns the grid and serves a user-only Unix socket at `~/.lick-lubin/control.sock`. Each accepted edit advances a revision. The client reads the revision before sending a command and prints JUCE's acknowledgement. The CoreS3 bridge and optional Codex interpretation path are described in the design plan.

## Verification boundaries

An offline render can check sample loading, beat scheduling, trigger cancellation, and release behavior. It cannot establish actual device latency, audio-buffer stability, feedback safety, or CoreS3 touch behavior. Report those only after testing on the target Mac and hardware.
