# JUCE Audio Proof — TDD Draft

**Status:** Draft derived from `docs/ORCHID_INTERACTIVE_DEMO.md`. This document defines the first audio-proof slice only; it is not a specification for the later interactive demo.

## 1. Purpose and scope

The proof is a standalone C++17 JUCE desktop application that demonstrates loading and playing one curated Orchid sample through JUCE's audio callback. Its fixed musical behavior is a `clean_kick` sample, triggered on quarter notes at 102 BPM. The application owns its timing and audio output.

In scope:

- Resolve `clean_kick` from the checked-in manifest and locate its WAV under an explicit local Orchid sample root.
- Load and decode the sample before audio playback.
- Provide Play and Stop controls and show useful sample/device failure status.
- Trigger the sample immediately when playback starts, then once per quarter note at 102 BPM.
- Stop transport, cancel future hits, and release an active hit with a short ramp so Stop does not create a discontinuity.
- Keep file access, allocation, blocking locks, logging, and UI work out of the audio callback.
- Verify behavior with offline rendered-buffer or equivalent deterministic tests before auditioning on the target Mac audio device.

Out of scope: CoreS3, Python playback scheduling, OpenAI/Codex, Realtime, MIDI, plug-in hosting, arrangement editing, other samples, tempo controls, the full 16-bar song, vocal processing, side-chain, and cross-track retrigger behavior. The later demo plan remains useful context, but does not expand this slice.

## 2. Contract

### 2.1 Sample resolution and loading

- The manifest contains the stable ID `clean_kick`, alias `Clean`, and relative path `Drum One Shots/Cymatics - Orchid Kick - Clean (F).wav`.
- The sample root is supplied locally through the configured environment/preference mechanism; the current proof setup uses `ORCHID_SAMPLE_ROOT`.
- The resolved file must remain within the configured root. The pack is external and must not be copied into the repository.
- A missing root, invalid root, absent/malformed manifest entry, path escape, missing file, unsupported/corrupt WAV, or invalid sample properties must leave playback unavailable and provide a useful status. No invalid sample may be partially loaded or played.
- A successful load fully decodes the sample into memory before playback. Mono and stereo WAV are supported by the current proof; do not assume additional formats or sample metadata.

### 2.2 Transport and timing

- Play is available only when the sample and audio output are ready.
- Starting playback begins with a hit at the start of the next rendered audio frame, then schedules subsequent hits one quarter note apart: `sample_rate * 60 / 102` frames, using the active device sample rate.
- The callback must correctly handle a beat boundary occurring within an audio block and retain fractional timing across blocks.
- Stop takes effect in the audio path, cancels future triggers, and starts the defined short release for active voices. Once stopped, no later beat may trigger until Play is requested again.
- The first proof does not specify pause, seek, count-in, tempo change, loop editing, or arrangement state.

### 2.3 Audio safety and voice lifecycle

- The callback clears output buffers and mixes only already-loaded sample data and preallocated voice state.
- Stop release is short and click-avoiding; the current implementation uses 12 ms. Treat that value as the proof's initial implementation value, not a later product-wide retrigger default.
- Active voices must terminate at sample end or after the stop release. Voice exhaustion must not allocate or block; if the fixed voice capacity is reached, the skipped hit should be surfaced through status.
- Device shutdown/error must prevent new playback and leave transport in a stopped state.
- Audio callback code must not read files, allocate memory, take blocking locks, log, or touch UI objects.

## 3. Test-first sequence

Write focused deterministic checks in this order when test implementation is authorized:

1. **Manifest/root resolution:** valid entry resolves the expected relative path; absent ID/path and malformed input fail clearly; traversal outside the sample root is rejected.
2. **Sample loading:** valid mono and stereo WAV load completely; missing, unsupported, empty, over-limit, or invalid-rate input fails without marking the sample ready.
3. **Initial trigger:** a rendered buffer beginning playback contains a hit at its first frame; no hit is emitted before Play or when sample/device readiness is false.
4. **Beat interval:** at multiple supported sample rates, hit onset spacing matches one quarter note at 102 BPM within the renderer's sample-frame quantization; test boundaries across differently sized render blocks.
5. **Stop cancellation:** after Stop is requested, no subsequent beat starts; a later Play restarts with an immediate first hit.
6. **Rapid Play/Stop requests:** queue Play and Stop in both orders before the next callback, including repeated requests; verify that the implemented ordering policy is deterministic and matches the documented product decision. Until that policy is decided, do not assume that separate boolean flags preserve every request or its order.
7. **Release and cleanup:** stopping an active voice produces a bounded ramp to silence over the configured release, has no discontinuity at the stop boundary, and leaves no stuck voice.
8. **Device lifecycle:** device unavailable/error/stopped states disable or reject Play, clear running status, and do not produce nonzero output.
9. **Callback constraints:** review or instrument the render path to confirm it uses preloaded data and fixed-capacity state with no file I/O, allocation, blocking synchronization, logging, or UI calls.

Tests should use generated in-memory or temporary fixture audio where possible; never commit Orchid pack assets. Keep fixtures and render setup outside the real-time callback. Do not make live API calls or require CoreS3 hardware for this proof.

## 4. Deferred full-demo scope

These behaviors belong to later phases and must not be inferred from this audio-proof contract: New Flow motif import; 4/8/4 arrangement sections; track grid and playhead; multiple samples/tracks; mute/level; retrigger semantics; trigger-based side-chain ducking; local IPC; Python/STT/Codex command flow; stale revision/idempotency handling; and CoreS3 rehearsal. They require later contracts and tests.

## 5. Verification evidence

Offline tests/rendering establish deterministic load, timing, cancellation, release, and cleanup behavior only. A successful build is not an audio-device check. The target-Mac audition separately checks audible sample quality, perceived timing/latency, clicks, and underruns using the selected output device. Record the device and observed conditions when that audition is performed; do not claim it from source inspection or offline rendering.

## 6. Open decisions

- Whether `ORCHID_SAMPLE_ROOT` remains the final user-facing configuration or is replaced by a user preference in a later UI pass.
- Whether 12 ms remains the release duration after target-device audition; the demo plan only requires a short click-safe release.
- The final test framework and whether audio rendering is extracted behind a deterministic harness or exercised through the JUCE device callback abstraction.
- Whether the audio proof needs an explicit behavior for repeated Play clicks while already playing. The present plan does not define toggle/restart semantics.
- How rapid Play/Stop requests are ordered, including the resulting playback state shown to the user. The demo plan does not settle whether Stop wins, the last action wins, or every action is preserved.
- Exact supported sample-rate/channel configurations beyond the currently described mono/stereo input and active-device output.

Resolve these choices from implementation evidence or explicit product direction; do not import retired FL Studio behavior.

## 7. BDD companion

The companion [BDD draft](JUCE_AUDIO_PROOF_BDD.md) contains user-visible scenarios for the first audio proof. Keep it limited to scenario behavior and its pointer to this document's verification evidence; implementation details, deferred scope, and unresolved product decisions belong here.
