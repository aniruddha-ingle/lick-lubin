# Lick-Lubin

Lick-Lubin is a voice-controlled, sample-based music co-producer with a small standalone desktop instrument and a tactile M5Stack CoreS3 controller. The demo is moving away from FL Studio toward a purpose-built JUCE app that owns the arrangement, transport, sample playback, and audio output.

The working musical target is **102 BPM, F minor**. The selected library is the user's curated **Cymatics Orchid Sample Pack**. Keep that pack outside the repository and configure its location locally; do not commit sample files.

## Current status

The first implementation slice is a standalone JUCE audio proof: load the Orchid Clean kick, play it on a 102 BPM beat, and stop cleanly. It is intentionally independent of the CoreS3 and AI services. Voice control, the arrangement UI, and hardware integration come later.

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

The planned no-API route is CoreS3 push-to-talk audio, local speech-to-text, then either an explicitly authorized Codex app-server text turn or a fixed local phrase-to-command grammar. Codex app-server access is an early gate: it requires eligible ChatGPT plan access and its own Sign in with ChatGPT authorization; an existing Codex login does not automatically provide the app with credentials. Realtime API support remains optional and requires separate API access/credits.

## Verification boundaries

An offline render can check sample loading, beat scheduling, trigger cancellation, and release behavior. It cannot establish actual device latency, audio-buffer stability, feedback safety, or CoreS3 touch behavior. Report those only after testing on the target Mac and hardware.
