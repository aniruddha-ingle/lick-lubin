# BDD Test Suite Specification: Lick-Lubin AI Co-Producer
**Document ID:** `BDD-2026-1119-AICOPROD-v1`  
**Target System:** `TDD-2026-1119-AICOPROD-v3` (101 BPM, D Major, M5Stack CoreS3 + macOS CoreMIDI Host Bridge)  
**Test Framework:** `pytest` + `pytest-asyncio` (Host Python) & Unity / Serial Loopback Smoke Harness (ESP32-S3)  
**Purpose:** Automated sanity check and pre-stage regression suite to verify serial framing, CoreMIDI timing precision, stuck-note prevention, tool execution, and `<50 ms` `LIVE SILENT` cutoff without requiring FL Studio or live OpenAI API credits.

---

## 1. Test Architecture & Mock Isolation Rules

To run deterministically in `< 5 seconds` on macOS before Thursday's hackathon without external network or hardware dependencies:
1. **Virtual MIDI Loopback (`rtmidi`):** Tests open `IAC Driver Bus 1` (or a mock `MidiOut` spy object recording `(timestamp_perf, status, data1, data2)`) to assert exact MIDI bytes and `time.perf_counter()` intervals.
2. **Mock Serial Transport (`asyncio` Stream / `ByteArray` Pipe):** Tests feed framed `0xAA` packets (`[0xAA, MSG_TYPE, LEN_HI, LEN_LO, PAYLOAD]`) directly into the host bridge parser to verify boundary fragmentation, checksum/length corruption recovery, and outgoing `0x02`/`0x03` frames.
3. **Mock OpenAI Realtime Connection:** Tests stub `client.realtime.connect(...)` to emit deterministic `response.function_call_arguments.done` events and assert that all 7 Lick-Lubin tools execute with zero confirmation prompts.
4. **Temporary `./AI_Drops` Fixture (`tmp_path`):** All `.mid` export tests write into `pytest`'s isolated `tmp_path` so your stage `./AI_Drops/` directory stays clean.

---

## 2. Gherkin Feature Scenarios

### Feature 1: 0xAA Binary Serial Framing & Desync Recovery
> *Verifies that fragmented USB-C CDC chunks and corrupted bytes never crash the parser or stall audio.*

```gherkin
Feature: 0xAA Serial Protocol Framing & Resynchronization

  Scenario: Parse valid fragmented 24 kHz PCM uplink packet (0x01)
    Given the host serial parser is initialized and idle
    When the serial transport receives a 960-byte 0x01 PCM frame split across three uneven TCP/USB chunks
    Then the parser reassembles exactly one 0x01 frame of 960 bytes
    And forwards the raw 24 kHz mono pcm16 payload to the Realtime audio uplink buffer

  Scenario: Recover cleanly from garbage bytes preceding a 0xAA sync header
    Given the serial buffer contains 17 random non-0xAA noise bytes
    When a valid 0x03 JSON state frame '{"event":"ptt_start","speak_back":true}' arrives immediately after
    Then the parser discards the 17 noise bytes without raising an exception
    And emits the parsed JSON dictionary {"event": "ptt_start", "speak_back": True}

  Scenario: Reject oversized malformed length header safely
    Given the serial parser receives [0xAA, 0x01, 0xFF, 0xFF] claiming a 65535-byte payload
    When the frame length exceeds MAX_FRAME_SIZE (4096 bytes)
    Then the parser resets its state machine to HUNT_SYNC
    And continues parsing subsequent valid 0x03 frames normally
```

---

### Feature 2: 101 BPM Drift-Free `time.perf_counter()` CoreMIDI Sequencer
> *Verifies sub-millisecond timing accuracy at 101 BPM (`148.51485 ms` per 16th-note tick) and D Major channel routing.*

```gherkin
Feature: 101 BPM Drift-Free CoreMIDI Step-Sequencer

  Scenario: Verify 16th-note tick interval precision over 4 bars (64 ticks)
    Given the sequencer is configured at 101.0 BPM (0.14851485s per 16th-note tick)
    And a 4-bar D Major chord progression is loaded on Channel 1 (MIDI ch 0)
    When the sequencer runs for 64 ticks in LOOP mode
    Then the cumulative wall-clock duration measured via time.perf_counter() is 9.505s within ±15 ms
    And individual tick jitter never accumulates across bar boundaries

  Scenario: Verify D Major chord progression voicings and MIDI channel mapping
    Given write_chord_progression is called with progression="Dmaj9-Bm7-Gmaj7-A6" and bars=4
    When the sequencer triggers Bar 1 Beat 1 (Tick 0)
    Then MIDI Note-On messages (0x90) are sent on Channel 1 for Dmaj9 [62, 66, 69, 73, 76]
    And a valid Type-0/1 MIDI file is written to ./AI_Drops/Chords_Dmaj9.mid

  Scenario: Verify human-velocity FPC drum template on Channel 3
    Given write_drum_pattern is called with style="boom_bap_101" and bars=4
    When the drum pattern plays across 16 ticks (1 bar)
    Then Kick (MIDI note 36 / C5) fires on ticks 0 and 10 with velocity >= 110 on Channel 3 (0x92)
    And Snare (MIDI note 38 / D5) fires on ticks 4 and 12 with velocity >= 105 on Channel 3 (0x92)
    And Closed Hi-Hat (MIDI note 42 / F#5) alternates accented (vel ~95) and ghost (vel ~55) hits
```

---

### Feature 3: Zero-Stuck-Note Guarantee (`CC 123` + Explicit `Note-Off`)
> *Verifies that muting, overwriting, stopping, or finishing a song never leaves hanging MIDI notes in FL Studio.*

```gherkin
Feature: Zero-Stuck-Note MIDI Cleanup

  Scenario: Overwriting an active bassline flushes previous notes immediately
    Given extract_bassline is actively playing a D2 (note 38) Note-On on Channel 2 (0x91)
    When extract_bassline is invoked again with a new octave variation
    Then the sequencer immediately transmits MIDI CC 123 (0xB1, 123, 0) on Channel 2
    And no notes from the prior bassline remain active past the transition tick

  Scenario: Muting a channel sends CC 123 and sets FL Studio Mixer Mute CC
    Given all 3 instrument channels (Piano, Bass 1, Drums) are actively looping
    When set_channel_gain is called with channel="bass", gain=0.0, mute=True
    Then MIDI CC 31 (0xB0, 31, 0) is transmitted to mute Mixer Insert 2
    And MIDI CC 123 (0xB1, 123, 0) is transmitted on MIDI Channel 2
```

---

### Feature 4: Production Modes & Atomic `PERFORMANCE MODE` Rap Finale Handoff
> *Verifies seamless transition from `LOOP` -> `ARRANGE` -> `PERFORMANCE` mode with deterministic 16-bar auto-stop.*

```gherkin
Feature: Production Modes & Atomic Performance Finale Handoff

  Scenario: Stitch 16-bar Master Arrangement without interrupting active loop
    Given 4-bar patterns exist for Intro, Verse, and Chorus in LOOP mode
    When arrange_sections is called with ["Intro:4", "Verse:8", "Chorus:4"]
    Then a linear 256-tick (16-bar) timeline is compiled in memory
    And Master_Arrangement_Piano.mid, Master_Arrangement_Bass1.mid, and Master_Arrangement_Drums.mid are saved to ./AI_Drops/
    And the active 4-bar LOOP playback continues uninterrupted until PERFORMANCE mode is triggered

  Scenario: Atomic transition into PERFORMANCE mode for live rap finale
    Given a 16-bar Master Arrangement is staged and speak_back is True
    When set_production_mode is called with mode="PERFORMANCE"
    Then speak_back is immediately forced to False
    And a 0x03 JSON packet with {"mode": "PERFORMANCE", "speak_back": false} is sent to the M5Stack CoreS3
    And MIDI CC 33 (0xB0, 33, 127) is sent to unmute the Scarlett condenser vocal mic (Insert 4)
    And the sequencer playhead resets to Bar 1, Tick 0 in linear SONG playback mode

  Scenario: Deterministic auto-stop at end of Bar 16 (38.02 seconds)
    Given the sequencer is running in PERFORMANCE mode on a 16-bar arrangement
    When Tick 256 (end of Bar 16) is reached
    Then the sequencer transitions to STOPPED state without wrapping to Bar 1
    And MIDI CC 123 (All Notes Off) is broadcast across Channels 1, 2, and 3
```

---

### Feature 5: Hardware `LIVE SILENT` Interrupt & Half-Duplex I2S State Machine
> *Verifies `<50 ms` talkback cutoff and CoreS3 I2S mic/speaker exclusivity.*

```gherkin
Feature: Hardware LIVE SILENT Interrupt & Half-Duplex Audio Handoff

  Scenario: Tapping LIVE SILENT during AI speech cancels Realtime response immediately
    Given Lick-Lubin is streaming 0x02 speaker audio chunks to the M5Stack CoreS3
    When the host receives a 0x03 JSON packet {"event": "toggle_silent", "speak_back": false}
    Then the host immediately flushes the outbound 0x02 speaker audio queue
    And sends a response.cancel event over the OpenAI Realtime WebSocket within 50 ms
    And suppresses all remaining audio deltas for the canceled response ID

  Scenario: Push-to-Talk (PTT) release commits audio buffer and requests response
    Given the user holds HOLD TO SPEAK and streams 1.5 seconds of 24 kHz PCM frames
    When the host receives 0x03 JSON {"event": "ptt_release"}
    Then the host sends input_audio_buffer.commit over the Realtime WebSocket
    And immediately sends response.create with tool_choice="auto"
```

---

## 3. Target Test File Structure for `SWE-AGENT`

Instruct `SWE-AGENT` to implement this BDD suite using `pytest` + `pytest-bdd` (or clean parameterized `pytest-asyncio` tests mapped 1-to-1 to the scenarios above) inside `host/tests/`:

```text
host/
├── host_bridge.py
├── requirements.txt
└── tests/
    ├── conftest.py                  # Mock MidiOut spy, Mock Serial pipe, Mock Realtime WS, tmp AI_Drops
    ├── test_serial_framing.py       # Feature 1: 0xAA framing, chunking, noise recovery, max-len guard
    ├── test_coremidi_sequencer.py   # Feature 2 & 3: 101 BPM perf_counter timing, D Major notes, CC 123 cleanup
    ├── test_arrangement_modes.py    # Feature 4: 16-bar stitching, atomic PERFORMANCE handoff, Bar 16 stop
    └── test_realtime_interrupts.py  # Feature 5: <50ms LIVE SILENT response.cancel, PTT commit flow
```

---

## 4. Prompt to Feed `SWE-AGENT` to Implement This BDD Suite

Copy and paste this prompt into your `SWE-AGENT` chat:

```text
Read `BDD-2026-1119-AICOPROD-v1.md` and `TDD-2026-1119-AICOPROD-v3.md`.
Implement the complete automated pytest suite inside `host/tests/` (`conftest.py`, `test_serial_framing.py`, `test_coremidi_sequencer.py`, `test_arrangement_modes.py`, `test_realtime_interrupts.py`) and add `pytest` and `pytest-asyncio` to `host/requirements.txt`.

Requirements:
1. Every Gherkin scenario in `BDD-2026-1119-AICOPROD-v1.md` must have a corresponding deterministic, zero-stub test function.
2. Use a mock `MidiOut` spy and `time.perf_counter()` assertions so tests run offline in <5 seconds without requiring FL Studio or an active OpenAI API key, while verifying exact MIDI status bytes, channels, notes, velocities, CC 123 flushes, and 101 BPM tick timing.
3. Ensure `pytest -v` passes 100% against `host/host_bridge.py`.
```
