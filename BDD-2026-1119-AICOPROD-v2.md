# BDD Test Suite Specification: Lick-Lubin (`BDD-2026-1119-AICOPROD-v2`)

**Document ID:** `BDD-2026-1119-AICOPROD-v2`  
**Parent Spec:** `TDD-2026-1119-AICOPROD-v3.md`  
**Target Codebase:** `host/host_bridge.py`, `firmware/src/main.cpp`  
**Execution Target:** Offline Deterministic `pytest` Suite (`< 4.0 seconds` total wall-clock runtime; zero live hardware or OpenAI API calls required)

---

## 1. Test Architecture & Mock Harness Contract

To guarantee deterministic, offline sanity checking before flashing the M5Stack CoreS3 or opening FL Studio, `SWE-AGENT` MUST implement the test suite inside `host/tests/` using `pytest` and `pytest-asyncio`:

```text
host/
├── requirements.txt                # Must include pytest and pytest-asyncio
├── host_bridge.py
└── tests/
    ├── conftest.py                 # FakeSerialTransport, FakeRtMidiOut, FakeRealtimeConn
    ├── test_serial_framing.py      # Feature 1 (Scenarios 1.1 – 1.3)
    ├── test_coremidi_sequencer.py  # Feature 2 (Scenarios 2.1 – 2.3) & Feature 3 (Scenarios 3.1 – 3.2)
    ├── test_arrangement_modes.py   # Feature 4 (Scenarios 4.1 – 4.3)
    └── test_realtime_interrupts.py # Feature 5 (Scenarios 5.1 – 5.2)
```

### Hardware & MIDI Contract Lock-In (v2 Alignment)
1. **FL Studio FPC Drum Pad Mappings (MIDI Channel 3 / Status `0x92`):**
   * `Kick` = `60` (`C5`)
   * `Snare` = `62` (`D5`)
   * `ClosedHat` = `66` (`F#5`)
   * `OpenHat` = `70` (`A#5`)
2. **FL Studio Mixer Control Channel (MIDI Channel 16 / Status `0xBF`):**
   * Volume Faders: `CC 20` (Piano), `CC 21` (Bass 1), `CC 22` (Drums), `CC 23` (Vocal Insert 4)
   * Track Mutes: `CC 30` (Piano), `CC 31` (Bass 1), `CC 32` (Drums), `CC 33` (Vocal Insert 4 — `0` = Muted, `127` = Unmuted)
3. **Standardized JSON Uplink Events (`0x03` Frame):**
   * PTT Press: `{"event": "ptt_start", "ptt": true, "speak_back": <bool>}`
   * PTT Release: `{"event": "ptt_release", "ptt": false, "speak_back": <bool>}`
   * Talkback Toggle: `{"event": "toggle_silent", "speak_back": <bool>}`
4. **Wall-Clock Budget:** All timing tests must execute in `< 4.0 seconds` total (e.g., 16 ticks = `2.376s` for `time.perf_counter()` drift verification).

---

## 2. Gherkin Feature Scenarios (12 Scenarios)

### Feature 1: `0xAA` Binary Serial Framing & Desync Recovery (`test_serial_framing.py`)

```gherkin
Feature: 0xAA Binary Serial Framing & Desync Recovery
  As the Python Host Bridge communicating at 921,600 baud over USB-C CDC
  I want to parse framed [0xAA, MSG_TYPE, LEN_HI, LEN_LO, PAYLOAD] packets reliably
  So that fragmented reads or noisy serial bytes never crash the audio or MIDI loops.

  Scenario 1.1: Reassemble fragmented 24 kHz PCM16 audio frames cleanly
    Given an initialized 0xAA serial frame parser
    When a valid 0x02 (MIC_PCM) packet containing 960 bytes of 24 kHz mono PCM16 audio arrives split across 3 separate serial read chunks
    Then the parser buffers the partial chunks without raising an exception
    And emits exactly one complete 960-byte PCM16 payload once the final byte arrives.

  Scenario 1.2: Recover from leading garbage bytes before 0xAA sync header
    Given an initialized 0xAA serial frame parser
    When 7 random non-0xAA noise bytes arrive followed immediately by a valid 0x03 JSON state frame '{"event":"ptt_start","ptt":true,"speak_back":true}'
    Then the parser discards the 7 leading noise bytes
    And cleanly decodes the JSON object with event == "ptt_start" and ptt is True.

  Scenario 1.3: Reject oversized malformed length headers safely
    Given an initialized 0xAA serial frame parser
    When a frame header [0xAA, 0x02, 0xFF, 0xFF] declares a 65,535-byte payload exceeding MAX_FRAME_BYTES (8192)
    Then the parser rejects the invalid header, drops the corrupt sync byte, and resynchronizes on the next valid 0xAA packet without memory exhaustion.
```

---

### Feature 2: `101 BPM` `time.perf_counter()` CoreMIDI Sequencer (`test_coremidi_sequencer.py`)

```gherkin
Feature: 101 BPM Drift-Free CoreMIDI Step-Sequencer
  As the live stage beat engine driving FL Studio over IAC Driver Bus 1
  I want sub-millisecond 16th-note scheduling at 101 BPM in D Major
  So that chords, bass, and FPC drums lock tightly into pocket with zero clock drift.

  Scenario 2.1: Sub-millisecond tick precision across bar boundaries
    Given the CoreMIDI sequencer is configured at 101.0 BPM (148.51485 ms per 16th-note tick)
    When the sequencer runs for 16 consecutive 16th-note ticks (1 full bar) against FakeRtMidiOut
    Then the elapsed wall-clock duration between Tick 0 and Tick 15 is 2.2277 seconds within a tolerance of ±10.0 ms
    And individual tick jitter does not accumulate across steps.

  Scenario 2.2: D Major chord progression voicings & archival .mid export
    Given the session is in LOOP mode at 101 BPM in D Major
    When the tool `write_chord_progression` is invoked with progression ["Dmaj9", "Bm7", "Gmaj7", "A6"]
    Then MIDI Note-On events (status 0x90) are scheduled on MIDI Channel 1 for D Major scale degrees (D, F#, A, C#, E / B, D, F#, A / G, B, D, F# / A, C#, E, F#)
    And a valid Standard MIDI File is written to `./AI_Drops/` matching the generated chord progression.

  Scenario 2.3: FL Studio FPC human-velocity drum groove on Channel 3
    Given the session is in LOOP mode at 101 BPM
    When the tool `write_drum_pattern` is invoked with style "laid_back_boom_bap"
    Then MIDI Note-On events (status 0x92) are emitted on MIDI Channel 3 using FL Studio FPC pad notes: Kick = 60, Snare = 62, ClosedHat = 66, and OpenHat = 70
    And hi-hat velocities exhibit human groove variation (accented downbeats > off-beat ghost hits).
```

---

### Feature 3: Zero-Stuck-Note & Mixer Control Guarantee (`test_coremidi_sequencer.py`)

```gherkin
Feature: Zero-Stuck-Note & Mixer Control Guarantee
  As a live performer on stage
  I want immediate CC 123 (All Notes Off) cleanup and isolated Channel 16 Mixer CCs
  So that overwriting patterns or muting stems never leaves hanging notes or collides with note channels.

  Scenario 3.1: Overwriting an active track flushes CC 123 before new notes play
    Given Piano (Channel 1) is actively playing a looping chord progression
    When `write_chord_progression` is called again with a new voicing
    Then `host_bridge.py` immediately sends `[0xB0, 123, 0]` (CC 123 All Notes Off on Channel 1) before scheduling the replacement pattern.

  Scenario 3.2: Muting Bass 1 sends CC 123 on Channel 2 and Mixer Mute on Channel 16
    Given Bass 1 (Channel 2) is actively playing
    When `set_channel_gain` is called for channel "bass" with mute=True
    Then `host_bridge.py` sends `[0xB1, 123, 0]` (All Notes Off on Channel 2)
    And sends `[0xBF, 31, 0]` (Mixer Bass Mute CC 31 = 0 on MIDI Channel 16).
```

---

### Feature 4: Production Modes & Atomic Rap Finale Handoff (`test_arrangement_modes.py`)

```gherkin
Feature: Production Modes & Atomic Rap Finale Handoff
  As the performer transitioning from beat-making (LOOP) to arrangement (ARRANGE) to live rap (PERFORMANCE)
  I want `arrange_sections` and `PERFORMANCE` mode to execute an atomic, zero-touch transition
  So that the 16-bar beat starts on Bar 1, Lick-Lubin goes silent, my mic unmutes, and the beat stops cleanly at Bar 16.

  Scenario 4.1: Stitching sections into a 16-bar linear Master Arrangement
    Given saved patterns exist for "Intro" (4 bars), "Verse" (8 bars), and "Chorus" (4 bars)
    When `arrange_sections` is called with `[{"section": "Intro", "bars": 4}, {"section": "Verse", "bars": 8}, {"section": "Chorus", "bars": 4}]`
    Then the sequencer builds a 256-tick (16-bar) linear timeline in memory
    And exports `Master_Arrangement_*.mid` into `./AI_Drops/` while `LOOP` mode continues uninterrupted.

  Scenario 4.2: Atomic handoff when switching to PERFORMANCE mode
    Given a 16-bar linear arrangement is loaded and `speak_back` is currently True
    When `set_production_mode(mode="PERFORMANCE")` is triggered
    Then the sequencer playhead resets atomically to Tick 0 (Bar 1, Beat 1) in linear non-looping playback mode
    And `speak_back` is set to False with `{"speak_back": false, "mode": "PERFORMANCE"}` pushed to the CoreS3
    And `[0xBF, 33, 127]` is sent on MIDI Channel 16 to unmute Vocal Insert 4 on the Scarlett interface.

  Scenario 4.3: Deterministic auto-stop at the end of Bar 16 (Tick 256)
    Given the sequencer is running in `PERFORMANCE` mode over a 16-bar (256-tick) arrangement
    When the playhead reaches Tick 256 (38.02 seconds at 101 BPM)
    Then the sequencer transitions to STOPPED without wrapping back to Bar 1
    And sends `CC 123` (`0xB0`, `0xB1`, `0xB2`) across Piano, Bass 1, and Drums so the finale ends on a crisp hit.
```

---

### Feature 5: `LIVE SILENT` Interrupt & Push-To-Talk Turn Flow (`test_realtime_interrupts.py`)

```gherkin
Feature: LIVE SILENT Interrupt & Push-To-Talk Turn Flow
  As a presenter speaking to the audience while Lick-Lubin talks
  I want tapping LIVE SILENT to cancel the active OpenAI Realtime response immediately
  And releasing HOLD TO SPEAK to commit my audio buffer cleanly.

  Scenario 5.1: Tapping LIVE SILENT cancels active Realtime response and drops trailing audio
    Given `speak_back` is True and an OpenAI Realtime response (`resp_123`) is actively streaming `response.audio.delta` events
    When the CoreS3 sends a `0x03` frame `{"event": "toggle_silent", "speak_back": false}`
    Then `host_bridge.py` immediately sets `speak_back = False`
    And calls `connection.response.cancel()` on the active Realtime session
    And drops all subsequent `response.audio.delta` packets for `resp_123` so zero `0x02` audio frames are written to serial.

  Scenario 5.2: Push-to-talk press and release commits audio turn to OpenAI Realtime API
    Given `ptt` is False and the Realtime WebSocket connection is open
    When the CoreS3 sends `{"event": "ptt_start", "ptt": true, "speak_back": true}` followed by two 960-byte `0x02` PCM16 frames, and then `{"event": "ptt_release", "ptt": false, "speak_back": true}`
    Then `host_bridge.py` forwards both base64-encoded PCM16 chunks via `input_audio_buffer.append`
    And upon `ptt_release`, calls `input_audio_buffer.commit()` followed by `response.create()`.
```

---

## 3. Copy-Paste Implementation Prompt for `SWE-AGENT`

```text
Read `BDD-2026-1119-AICOPROD-v2.md` and `TDD-2026-1119-AICOPROD-v3.md`.

1. Apply the two contract alignments in `firmware/src/main.cpp` and `host/host_bridge.py` required by `BDD-2026-1119-AICOPROD-v2.md`:
   - Emit and handle standardized `0x03` JSON events (`"event": "ptt_start"`, `"ptt_release"`, `"toggle_silent"`) alongside `"ptt"` and `"speak_back"`.
   - Track the active `response_id` on `response.created` / `response.audio.delta`, and immediately invoke `await connection.response.cancel()` whenever `speak_back` transitions to `False` (via `"toggle_silent"` or `set_talkback(enabled=False)`), dropping all trailing audio deltas for that `response_id`.
2. Add `pytest` and `pytest-asyncio` to `host/requirements.txt`.
3. Implement all 12 Gherkin scenarios from `BDD-2026-1119-AICOPROD-v2.md` across `host/tests/conftest.py`, `test_serial_framing.py`, `test_coremidi_sequencer.py`, `test_arrangement_modes.py`, and `test_realtime_interrupts.py` using fake serial, fake RtMidi (`IAC Driver Bus 1`), and fake OpenAI Realtime transports so `pytest -v` runs completely offline in `< 4.0 seconds`.
4. Output every modified/created file in full without stubs or truncation.
```
