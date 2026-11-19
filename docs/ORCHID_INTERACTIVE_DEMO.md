# Orchid Interactive Demo Plan

## Direction

Build a small standalone JUCE desktop instrument. JUCE owns the authoritative arrangement, transport, tempo, sample voices, mixing, and audio output. Python remains the voice/AI controller and sends validated musical commands to JUCE over a local user-only IPC endpoint. Python does not schedule playback events or maintain a second timeline.

The proposed demo target is **102 BPM, F minor**. Use the Orchid **New Flow** MIDI as a starting motif, not a finished arrangement: it is one track with 22 note-on events, and its tempo/key are represented in the filename rather than embedded tempo metadata. Keep the first arrangement small, with Intro 4 bars, Verse 8 bars, and Chorus 4 bars (16 bars total), unless arranging the motif reveals a reason to revise that form. At 102 BPM, 16 bars of 4/4 lasts about 37.65 seconds.

## First proof: one audio slice

Before revising every spec or building the full UI, make the smallest JUCE app that:

1. Loads the curated `Clean` kick WAV through a manifest entry and an external sample-root setting.
2. Plays it on a beat at 102 BPM using JUCE's audio callback/timing path.
3. Stops transport, cancels pending triggers, and releases/stops the active voice without a click or a stuck voice.

Keep this slice independent of the CoreS3 and OpenAI. Verify it first with an offline render/test harness, then audition it through the target Mac audio device. This proves sample loading, callback safety, timing, and cleanup before investing in a complete arrangement editor.

## Sample manifest and aliases

Keep the sample pack outside the repository. A user-curated manifest stores stable sample IDs, spoken aliases, roles, relative paths, and source/root-note metadata where known. Configure the local sample root in a user preference or environment setting; never copy the full pack into the repo. Missing or unsupported files must be reported clearly.

Initial spoken aliases:

| Alias | Relative path |
| --- | --- |
| `Perfect Donk` | `808s & Basses/Cymatics - Orchid BASS Perfect Donk (C).wav` |
| `Heavyweight` | `808s & Basses/Cymatics - Orchid 808 Heavyweight (C).wav` |
| `Clean` | `Drum One Shots/Cymatics - Orchid Kick - Clean (F).wav` |
| `Dancehall` | `Drum One Shots/Cymatics - Orchid Kick - Dancehall (A#).wav` |
| `Bunker` | `Drum One Shots/Cymatics - Orchid Rimshot - Bunker.wav` |
| `Roll` | `Drum One Shots/Cymatics - Orchid Hihat - Roll 1.wav` (default; Roll 2/3 may be added) |
| `Closed` | `Drum One Shots/Cymatics - Orchid Hihat - Closed 1.wav` |
| `Open` | `Drum One Shots/Cymatics - Orchid Hihat - Open 3.wav` |
| `Flam` | `Drum One Shots/Cymatics - Orchid Clap - Flam.wav` |
| `Pop` | `Drum One Shots/Cymatics - Orchid Clap - Pop.wav` |

Preserve these spoken aliases even where the pack's label differs. Do not assume sample pitch, duration, or format beyond what the manifest/file inspection confirms.

## Cut Itself and side-chain

Image-Line documents FL Studio Cut Itself as sending an all-notes-off/release command to the same channel's instrument; the voice enters its release phase and may remain audible according to its envelope. It is not equivalent to an immediate audio mute. In this app, define per-track **Retrigger: release previous voice**. A new hit sends the prior voice into that track's release ramp, then starts the new voice. For one-shots, use a short configurable release (initial default 10 ms to avoid clicks); the old sample may briefly overlap during that release. A zero-release hard cut can be a separate explicit option. This behavior is scoped to the same track. Cross-track cut groups are a separate feature and are out of the MVP.

For **Side Chain A to B**, implement **trigger-based ducking** in the MVP: a kick event on A drives a gain envelope on B with a short attack, configurable depth, and release. This is deterministic and avoids audio-envelope detection latency and lookahead. It is not a full compressor or dynamic EQ. Add true audio-envelope detection only if the musical requirement is to follow the rendered kick waveform rather than its trigger event.

## Voice and Codex command path

The default path must work without an OpenAI API key or Realtime API credits. Keep CoreS3 as the push-to-talk microphone: on PTT press/release it sends its existing event frames, and while held it streams microphone PCM over the existing USB serial framing to the Python host. The host buffers each utterance and runs a local speech-to-text engine on the laptop (for example, benchmark a small whisper.cpp or Vosk model for latency and recognition of the sample aliases). Audio stays local for transcription.

Deliver the recognized text to Codex explicitly through the Codex app-server's text RPC input. Codex interprets that transcript and can invoke one dedicated local MCP tool, such as `submit_music_command`, whose schema only accepts the allowed arrangement operations. That MCP tool calls the same local command adapter used by JUCE IPC. The path is:

`CoreS3 PTT PCM/events -> framed USB serial -> Python local STT -> transcript -> Codex app-server text input -> allowlisted local MCP command -> validated adapter -> JUCE IPC -> ack/reject -> display or local spoken feedback`

The ChatGPT-plan app-server route is a text path, not an audio/transcription path: current documentation excludes audio/video input and the transcription API there. It can avoid API-key/Realtime usage, but only eligible **ChatGPT Plus and Pro** accounts can use their plan for requests in this open-source app flow. A Free account may not qualify. The app must implement its own **Sign in with ChatGPT** OAuth authorization/consent flow for Responses API access and explicitly pass the resulting access token to its app-server child process; do not assume the currently open Codex session supplies credentials automatically. Network access and plan usage limits still apply, so this is not fully offline inference. If eligibility, authorization, or an inference turn fails, use the no-cost fallback: local STT followed by a fixed local phrase-to-command grammar for a small set of allowlisted commands. Natural-language Codex interpretation then requires an eligible plan or API credits.

Realtime remains an opt-in alternative for users who have API access/credits. It can process the CoreS3 audio and produce the same typed command request, but it is not the no-credits default. Do not assume that CoreS3 audio or transcripts automatically enter the current Codex chat. Never use shell commands, source editing, or unconstrained text execution as the live music-control interface. An ambiguous transcript or rejected command must produce a clear clarification/rejection and no partial changes.

Use a local user-only JSON-lines socket with typed, allowlisted commands. Include `command_id` for idempotency and `expected_revision` for stale-state protection. JUCE validates the full request atomically and returns accepted/rejected status, reason, and current revision. Invalid commands have no partial effects. Stop is always allowed and high priority. Mute/level changes may apply immediately with a short ramp; tempo changes apply at the next bar; structural edits and sample reassignment are rejected while playing in the MVP.

## Minimal full demo after the audio proof

- Sample tracks and one-shot clips, with clip placement across sections.
- Transport: play, stop, playhead, and tempo.
- Per-track mute, level, and release-previous-voice retrigger.
- Kick-triggered side-chain routing from one track to another.
- A simple arrangement grid: track rows, bar columns, colored clip blocks, section labels, and a moving playhead.
- The New Flow phrase available as an editable motif; it is not silently expanded into a complete song.

Keep arrangement state authoritative in JUCE. Python requests actions and reads acknowledgements/state snapshots; it never sends time-critical note events.

## Contract revisions

The retired FL Studio TDD locked 101 BPM / D major and assumed Python/CoreMIDI drove FL Studio. Those contracts were removed from the working tree at the user's direction. Future acceptance scenarios should cover sample load/play/stop, manifest resolution, motif import, arrangement transitions, retrigger release, side-chain envelope, and command rejection/atomicity. Use one arrangement input schema: `sequence: [{"section_name": "INTRO"|"VERSE"|"CHORUS", "bars": integer}]`, totaling 16 bars as 4/8/4. Keep the README aligned with the app, local sample-root setup, and actual repository paths. Retain the CoreS3 serial/PTT contract as the voice input surface; revise only the host's playback responsibilities.

Vocal monitoring needs its own explicit scope because FL Studio currently supplies the vocal insert/effects. For the first demo, use audio-interface direct monitoring and leave effects out of scope. Add JUCE mic input processing only if it is required for the demo and after its latency/feedback path is specified.

## Phases and verification

0. **Codex access gate (before the full JUCE build):** On the actual account, verify Plus/Pro eligibility and complete the app's Sign in with ChatGPT OAuth/consent flow for Responses API access. Pass the authorized token to a minimal Codex app-server process as documented. Send one transcript as text, receive a completed turn, invoke one harmless local MCP tool (for example, `get_demo_status`), and confirm its result reaches the client. Do not use an API key. Treat a successful inference turn as the access check; a model catalog listing alone is not proof of entitlement. If the account is Free/ineligible or sign-in/inference fails, select the local phrase grammar as the no-cost fallback before building around Codex. This smoke test is independent of the first JUCE audio proof and may run alongside it.
1. **Audio proof:** one sample, one beat, clean stop. Offline rendered-buffer checks for loading, trigger timing, cancellation, and release; then target-device audition for latency, clicks, and underruns. This slice does not depend on Codex or Realtime availability.
2. **Manifest and motif:** validate aliases/paths and import New Flow. Offline tests cover missing files, filename metadata handling, note count, and timeline representation; no pack assets are committed.
3. **Tracks and arrangement:** implement the simple grid, 4/8/4 sections, transport, tempo, mute/level, retrigger, and trigger-based ducking. Offline tests cover bar boundaries, voice cleanup, no cross-track cut, release behavior, and trigger-envelope values.
4. **No-credits voice bridge:** integrate CoreS3 PTT buffering, local STT, app-server text delivery, and a dedicated local MCP command tool through the allowlisted adapter when the access gate passes. If it fails, route local STT to the fixed local grammar instead. Offline fake-STT/app-server/MCP/socket tests cover transcript handoff, schema validation, idempotency, stale revisions, mid-playback rejection/queue rules, and acknowledgements. Do not make live OpenAI API calls in tests.
5. **Optional Realtime path:** if API access/credits are available, add Realtime audio/tool handling as an alternative front end that submits the same typed commands. Keep it out of the required demo path and do not make live API calls in tests.
6. **Hardware/audio rehearsal:** verify CoreS3 PTT start/release, audio framing, offline local transcription latency/alias accuracy, JUCE CoreAudio output, sample playback, stop behavior, and the selected vocal-monitor route. Only physical-device/audio checks establish touch behavior, actual latency, feedback safety, or hardware buffer stability.

Key risks: audio-thread safety and underruns; sample-rate conversion and pitch/time behavior; filename-derived metadata; local STT latency and alias recognition; passing transcript text into the correct Codex app-server thread; stale commands; and audible release/crossfade behavior on retrigger. Keep offline evidence distinct from hardware/audio verification.

## Workspace note

Implementation of the first JUCE audio slice has been authorized. Keep later spec changes and tests aligned with explicit user direction. Preserve all pre-existing worktree changes and inspect the current Git diff before changing documentation.

## Reference

- [Image-Line: Miscellaneous Channel Settings — Cut Groups / Cut Itself](https://www.image-line.com/fl-studio-learning-content/fl-studio-online-manual/html/chansettings_misc.htm): Cut sends an all-notes-off/release command to the instrument; voices can remain audible through their release phase. The feature is scoped to a channel; cross-channel Cut/Cut by groups are separate.
- [OpenAI: Sign in with ChatGPT quickstart](https://developers.openai.com/siwc/quickstart): ChatGPT-plan usage in open-source apps is for eligible Plus and Pro users and requires an explicit OAuth authorization flow.
- [OpenAI: Codex app-server with ChatGPT plan usage](https://developers.openai.com/siwc/token-sharing-open-source/codex-app-server): pass the authorized access token to the app-server child process and use its text RPCs.
- [OpenAI: Preview limitations](https://developers.openai.com/siwc/token-sharing-open-source/preview-limitations): the documented app-server path supports text inputs but not audio/video input or transcription API use.
