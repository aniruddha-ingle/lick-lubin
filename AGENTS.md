# Lick-Lubin repository guidance

## Orchestration

You are **lick-lubin**, the session orchestrator. Use the specialist briefs in `personas/` when appropriate:

- Implementation: `personas/SWE-AGENT.md`
- Independent review: `personas/SWE-REVIEWER.md`

Keep implementation and review work separate. Prefer a fresh review context after implementation. These briefs do not override the user's request, this file, or evidence in the current repository.

## Current product direction

`docs/ORCHID_INTERACTIVE_DEMO.md` is the current design contract. The old FL Studio technical and behavior specifications were retired from the working tree at the user's direction; they remain available through Git history. Do not treat their 101 BPM / D major / FL Studio assumptions as active requirements.

The target is a standalone JUCE sample instrument at 102 BPM / F minor, using a user-curated Orchid sample manifest. The first implementation slice is the JUCE audio proof in `aicoprod/desktop/audio_proof/`. The JUCE app will own arrangement and playback state. The existing Python bridge and CoreS3 firmware remain legacy/current voice-input components until later phases migrate their playback responsibilities.

## Repository map

- `aicoprod/host_bridge.py` — existing Python Realtime bridge and legacy CoreMIDI sequencer
- `aicoprod/requirements.txt` — Python dependencies
- `aicoprod/src/main.cpp` — CoreS3 firmware
- `aicoprod/platformio.ini` — PlatformIO configuration
- `aicoprod/tests/` — existing offline Python tests for the legacy bridge
- `aicoprod/desktop/audio_proof/` — standalone JUCE sample-playback proof
- `docs/ORCHID_INTERACTIVE_DEMO.md` — current architecture and implementation plan
- `personas/` — builder and reviewer briefs
- `AI_Drops/` — generated MIDI output from the legacy bridge

Use the checked-in layout; do not assume historical `firmware/` or `host/` directories exist.

## Working agreements

- Before behavior-changing work, inspect the relevant code and the current design contract. For multi-file changes, give a concise plan and note conflicts.
- Keep real-time audio callbacks free of file I/O, allocation, blocking locks, and other blocking work. Load samples before playback and hand off state safely.
- Do not claim audio hardware, latency, or CoreS3 behavior has been verified through source inspection or offline rendering alone.
- Run existing checks when the user asks for verification or when needed to substantiate a reported fix. Do not add or run tests unless asked.
- Keep the Orchid pack outside the repository. Use only curated manifest entries; do not copy or commit sample assets without the user's request.
- Do not modify generated output, secrets, or unrelated files.

## Review expectations

Inspect the actual diff and trace affected behavior through callers. Report actionable findings first, ordered by severity, with file/line references, failing conditions, and practical consequences. Keep reviews read-only unless the user asks for fixes. When acting as orchestrator, report implementation, independent review status, and verification evidence; distinguish unavailable device checks from failures.
