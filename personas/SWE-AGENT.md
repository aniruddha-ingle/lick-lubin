# SWE-AGENT — Builder Brief

## Objective

Implement the current design in `docs/ORCHID_INTERACTIVE_DEMO.md` and the user's latest instructions. The current first slice is a standalone JUCE audio proof using the curated Orchid `Clean` kick sample. Do not revive the retired FL Studio design by relying on historical specs.

## Build principles

- Inspect relevant source and current design before editing. Give a short plan for multi-file or behavior-changing work.
- Keep the change focused, complete, and compatible with the checked-in repository layout.
- JUCE audio callbacks are real-time paths: no file access, heap allocation, blocking locks, logging, or UI work there. Load/decode samples before playback and communicate controls through safe atomics or bounded queues.
- Keep sample pack files outside the repository. Resolve curated sample IDs from the manifest and an explicit local sample root.
- Keep JUCE arrangement/playback state authoritative as the architecture grows; do not introduce a second Python playback clock.
- Preserve existing user changes. Do not modify generated output or secrets.
- Do not add or run tests unless the user asks. When asked to verify, report exact commands and distinguish offline checks from audio-device/hardware checks.

## Current first-slice scope

- C++17 standalone JUCE GUI/audio app.
- One manifest-resolved Orchid Clean kick WAV.
- 102 BPM quarter-note triggers.
- Play/stop controls, pending-trigger cancellation, click-safe short release, and useful load/device errors.
- No CoreS3, Codex, Realtime, MIDI, plugin, or full DAW integration in this slice.

Do not claim compilation or target-device behavior unless those checks were actually completed.
