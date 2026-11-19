# Technical Design Document (TDD): Deterministic Wired Hardware AI Co-Producer for FL Studio

| Metadata | Details |
| :--- | :--- |
| **Document ID** | `TDD-2026-1119-AICOPROD` |
| **Status** | `READY FOR AGENT REVIEW` |
| **Target Event** | OpenAI for Startups — *You can just build things [with hardware]* (295 Lafayette St, NYC) |
| **Build Date** | Thursday, November 19, 2026 (1:30 PM – 7:30 PM EST) |
| **Hardware Target** | M5Stack CoreS3 / CoreS3 SE (ESP32-S3) |
| **Host Environment** | Laptop (macOS / Windows) running Python 3.11+ & FL Studio |
| **Transport Layer** | Wired USB-C CDC Serial (`921,600` baud) + Virtual MIDI Bus + Local Filesystem `.mid` |

---

## 1. Executive Summary & Problem Statement

### 1.1 Problem Statement
Digital Audio Workstations (DAWs) such as FL Studio offer deep composition and mixing capabilities, yet their primary interaction model depends heavily on traditional musical instrument proficiency or high-friction mouse-and-keyboard manipulation inside Piano Rolls and Mixer racks. For producers who compose without playing physical instruments—or who perform live sets where interrupting audio playback to navigate menus is unacceptable—there is a substantial latency gap between musical intent and audible execution.

Existing voice assistants fail in studio and live performance environments for three reasons:
1. **Uncontrolled Acoustic Bleed:** Unsolicited spoken voice responses bleed into live microphones and disrupt active playback.
2. **Wireless Latency & Jitter:** Consumer wireless protocols (Bluetooth, congested 2.4 GHz Wi-Fi) introduce unacceptable latency, packet loss, and local network isolation issues in live venues.
3. **Lack of Musical Context:** Generic voice commands lack awareness of harmonic state, song structure (loops vs. full arrangements), and active gain staging headroom.

### 1.2 Solution Overview
We propose a **deterministic, zero-wireless-latency Hardware AI Co-Producer** built on an **M5Stack CoreS3** tethered via a single **wired USB-C cable** to a local Python orchestration bridge and **FL Studio**. 

Acting as a dedicated studio control surface, the hardware unit captures push-to-talk (PTT) voice commands and streams raw PCM audio over high-speed USB Serial to the OpenAI Realtime API. The system executes structured musical composition, root-note bassline extraction, context-aware mixing (`Mix Gain` vs. `Absolute Gain`), and non-destructive **`LOOP` / `ARRANGE`** song assembly—while providing a physical and voice-controlled **`LIVE SILENT`** toggle that mutes hardware speaker talkback in favor of high-contrast visual telemetry.

---

## 2. Goals, Non-Goals & Design Principles

### 2.1 Core Engineering Principles
* **Deterministic Wired Transport:** Zero reliance on local Wi-Fi LANs, captive portals, or Bluetooth audio. All hardware-to-host communication runs over a single USB-C cable.
* **Non-Destructive Musical State:** Musical sections are isolated in memory and on disk so building a new section never overwrites or contaminates a previously approved section.
* **Acoustic Safety in Performance:** Speaker talkback must be instantaneously killable at the hardware layer (`LIVE SILENT` mode) without interrupting command execution or visual status telemetry.

### 2.2 Explicit Goals
* **G1 (Sub-Millisecond Local Link):** Establish bidirectional framed serial transport (`921,600` baud) between the M5Stack CoreS3 and laptop Python bridge for 16 kHz PCM audio and JSON UI state packets.
* **G2 (Harmonic & Derived Composition):** Generate named MIDI chord progressions and automatically derive octave-transposed root-note basslines into isolated `[Section]_[Channel].mid` files.
* **G3 (Dual-Mode Production Workflow):** Support `LOOP MODE` (isolated 4/8-bar section building) and `ARRANGE MODE` (concatenating saved sections into a master timeline while the current loop continues playing uninterrupted).
* **G4 (Context-Aware Gain Staging):** Differentiate between `Absolute Gain` (raw fader percentage) and `Mix Gain` (dynamic headroom ceiling calculated from active mixer channels).
* **G5 (Dual-Control Performance Toggle):** Support muting speaker talkback via both a physical touchscreen button and a Realtime API tool call (`set_talkback`).

### 2.3 Explicit Non-Goals
* **NG1:** Real-time monophonic/polyphonic pitch tracking of hummed or sung audio (out of scope for a single-afternoon build).
* **NG2:** Custom native FL Studio C++ SDK / VST3 plugin compilation.
* **NG3:** Wireless Wi-Fi or Bluetooth fallback modes (intentionally excluded to eliminate venue RF interference and latency).

---

## 3. System Architecture & Data Flow

```text
+-----------------------------------+       USB-C CDC Serial        +------------------------------------------+
|         M5Stack CoreS3            |       (921,600 Baud)          |          Laptop Python Bridge            |
|       (ESP32-S3 Hardware)         | <===========================> |           (macOS / Windows)              |
|                                   |                               |                                          |
|  [Hardware Inputs]                |   Framed Binary Protocol:     |  [Serial Mux/Demux Engine]               |
|   - Dual MEMS Mics (16kHz PCM)    |   - 0x01: Audio PCM Frames    |  - Reads/Writes framed USB packets      |
|   - Touch Zone 1: Hold-to-Talk    |   - 0x02: UTF-8 JSON State    |                                          |
|   - Touch Zone 2: SILENT Toggle   |                               |  [OpenAI Realtime API Client]            |
|                                   |                               |   - WebSocket over Laptop Internet       |
|  [Hardware Outputs]               |                               |   - Function Calling Dispatcher          |
|   - 1W Speaker (Gated by          |                               |                                          |
|     local `speak_back` flag)      |                               |  [Musical State & DSP Logic]             |
|   - 2.0" IPS LCD (320x240):       |                               |   - Loop / Arrange Section Store         |
|     * Top Bar: Mode + Silent Pill |                               |   - Mix vs. Absolute Gain Math           |
|     * Main Body: 3-Line Telemetry |                               |   - `mido` MIDI File Synthesizer         |
+-----------------------------------+                               +--------------------+---------------------+
                                                                                         |
                                                                    +--------------------+---------------------+
                                                                    |                                          |
                                                     Filesystem (.mid Drop)                     Virtual MIDI Bus (CC)
                                                     `./AI_Drops/*.mid`                         (IAC Driver / loopMIDI)
                                                                    |                                          |
                                                                    v                                          v
                                                    +----------------------------------------------------------+
                                                    |                        FL Studio                         |
                                                    |  - Browser Panel: Pinned `AI_Drops` Directory            |
                                                    |  - Channel Rack: `Piano` (Ch 1), `Bass 1` (Ch 2)         |
                                                    |  - Playlist / Pattern Transport: Pattern vs. Song Mode   |
                                                    |  - Mixer Inserts 1 & 2: Linked to MIDI CCs 20-21, 30-31  |
                                                    +----------------------------------------------------------+
```

---

## 4. Detailed Component Specifications

### 4.1 USB-C Framed Serial Protocol (Hardware <-> Host)
To prevent bidirectional 16 kHz audio streams from colliding with control messages over a single UART/USB-CDC pipe, all traffic uses a deterministic **Length-Prefixed Type-Value (LTV) framing protocol**:

| Byte Offset | Field | Size | Description |
| :--- | :--- | :--- | :--- |
| `0` | `SYNC_BYTE` | 1 byte | Fixed magic byte `0xAA` for frame alignment recovery. |
| `1` | `MSG_TYPE` | 1 byte | `0x01` = Mic PCM Audio (Dev->Host)<br>`0x02` = Speaker PCM Audio (Host->Dev)<br>`0x03` = JSON UI/State Update (Bidirectional)<br>`0x04` = Heartbeat / ACK |
| `2..3` | `PAYLOAD_LEN` | 2 bytes | Unsigned 16-bit big-endian integer (`0` to `4096`). |
| `4..N` | `PAYLOAD` | `N` bytes | Raw 16-bit signed little-endian PCM (16 kHz mono) or UTF-8 JSON string. |

* **Bandwidth Budget:** 16 kHz, 16-bit mono PCM requires `32,000 bytes/sec` (`256 kbps`). At `921,600 baud` (~`92,160 bytes/sec` effective throughput), audio consumes **~34.7% bandwidth**, leaving >65% headroom for instantaneous JSON state updates with zero buffer bloat.

### 4.2 Firmware Specification (`M5Stack CoreS3`)
* **Display Layout (`320x240` IPS):**
  * **Header Bar (`y: 0..44`):** Split into two high-contrast touch targets:
    * Left (`x: 0..179`): Mode Indicator — e.g., `[LOOP: CHORUS]` (Blue) or `[ARRANGE: 20 BARS]` (Purple).
    * Right (`x: 180..320`): Acoustic Gate Toggle — `[TALKBACK: ON]` (Green) or `[LIVE SILENT]` (Crimson Red).
  * **Telemetry Readout (`y: 45..179`):** Dark background (`#111111`) with crisp monospaced/sans-serif 3-line status confirmation:
    * **Line 1 (Action):** `BASS 1: WRITTEN (G MIN)`
    * **Line 2 (Mixer):** `GAIN: 50% MIX (-7.2 dB)`
    * **Line 3 (File):** `DROP: Chorus_Bass1.mid`
  * **PTT Trigger Zone (`y: 180..240`):** Full-width tactile button (`HOLD TO SPEAK` / `LISTENING...`).
* **Local Acoustic Gate Enforcement:**
  * Tapping `[LIVE SILENT]` immediately flips the local boolean `speak_back = false`, flushes the hardware I2S speaker DMA ring buffer (cutting off any mid-sentence speech instantly), and transmits `{"event": "toggle_talkback", "enabled": false}` over `MSG_TYPE 0x03` so the host bridge stops streaming `0x02` audio frames.

### 4.3 Production State Machine (`Python Bridge`)
The Python host maintains a strict, deterministic state dictionary:

```python
state = {
    "mode": "LOOP",                 # Enum: "LOOP" | "ARRANGE"
    "active_section": "Section_1",  # String key into `sections`
    "speak_back": True,             # Mirrors hardware toggle state
    "bpm": 140,
    "sections": {
        # Isolated per-section memory prevents cross-section contamination:
        # "Chorus": {
        #     "key": "G",
        #     "scale": "minor",
        #     "bars": 4,
        #     "chords": [ {"bar": 0, "notes": [55, 58, 62], "duration_beats": 4}, ... ],
        #     "bass":   [ {"bar": 0, "notes": [43],         "duration_beats": 4}, ... ]
        # }
    },
    "mixer": {
        "Piano":  {"active": True,  "cc_vol": 20, "cc_mute": 30, "current_cc": 102, "db": 0.0},
        "Bass 1": {"active": False, "cc_vol": 21, "cc_mute": 31, "current_cc": 0,   "db": -99.0}
    }
}
```

#### State Transition Rules
1. **Entering `LOOP MODE` with a New Section Name:**
   * Command: *"Enter Loop Mode and call this section Bridge."*
   * Action: Sets `mode = "LOOP"`, creates a clean entry `state["sections"]["Bridge"] = {}`, and updates the CoreS3 header to `[LOOP: BRIDGE]`. Subsequent composition tools (`write_chord_progression`, `extract_bassline`) operate exclusively on `Bridge` and export `Bridge_Piano.mid` and `Bridge_Bass1.mid`.
2. **Entering `ARRANGE MODE` (Live Background Playback):**
   * Command: *"Switch to Arrange Mode. Lay out Intro for 4 bars, Verse for 8 bars, and Chorus for 8 bars."*
   * Action: Sets `mode = "ARRANGE"` without sending a MIDI Stop message—FL Studio continues looping the active pattern to the room. The bridge reads the saved note arrays from `state["sections"]`, calculates cumulative tick offsets (`ticks_per_beat = 480`), and writes `Master_Arrangement_Piano.mid` and `Master_Arrangement_Bass1.mid` into `./AI_Drops/`.

### 4.4 Gain Staging & Headroom Mathematics
FL Studio's mixer fader law maps incoming 7-bit MIDI CC (`0..127`) non-linearly from `-∞ dB` to `+5.6 dB`, with **Unity Gain (`0.0 dB`) calibrated at `CC = 102` (~80.3% fader travel)**.

The `set_channel_gain(channel_name, percent, mode)` tool implements two distinct curves:

1. **`absolute` Gain Mode:**
   Directly scales the user's requested percentage `P ∈ [0, 100]` across the entire physical fader travel (`0..127`):
   ```text
   CC_abs = round((P / 100) * 127)
   ```
   * *Behavior:* `50% absolute gain` yields `CC = 64` (raw midpoint of physical fader travel, regardless of other tracks).

2. **`mix` Gain Mode (Context-Aware Headroom Ceiling):**
   Inspects `state["mixer"]` to evaluate active competing tracks before computing output CC:
   * **Rule A (Harmonic Bed Active):** If `Piano` is unmuted (`active == True`) at or near unity (`current_cc >= 95`), the engine assigns `Bass 1` a protective headroom ceiling of **`-4.5 dB` (`CC_ceiling = 92`)** so low-end sub frequencies sit cleanly beneath the chords without clipping the master bus.
   * **Rule B (Solo / No Competing Bed):** If no other tracks are active, `CC_ceiling` defaults to Unity (`102`, `0.0 dB`).
   ```text
   CC_mix = round((P / 100) * CC_ceiling)
   ```
   * *Behavior:* `50% mix gain` on `Bass 1` (while `Piano` plays) yields `CC = 46` (scaled to the `-4.5 dB` mix ceiling), and automatically sends the unmute CC (`127` on `cc_mute`) so the channel comes in cleanly on cue.

---

## 5. OpenAI Realtime API Tool Interface

The Python bridge registers the following JSON tool schemas with the OpenAI Realtime API session (`session.update`):

```json
{
  "tools": [
    {
      "type": "function",
      "name": "set_production_mode",
      "description": "Switch between LOOP mode (building an isolated named section) and ARRANGE mode (assembling sections while active loop plays).",
      "parameters": {
        "type": "object",
        "properties": {
          "mode": { "type": "string", "enum": ["LOOP", "ARRANGE"] },
          "section_name": {
            "type": "string",
            "description": "Target section name when entering LOOP mode (e.g., Intro, Verse, Chorus, Bridge)."
          }
        },
        "required": ["mode"]
      }
    },
    {
      "type": "function",
      "name": "write_chord_progression",
      "description": "Generate a diatonic chord progression in the active LOOP section and write a .mid file to AI_Drops.",
      "parameters": {
        "type": "object",
        "properties": {
          "key": { "type": "string", "description": "Musical root note (e.g., G, F#, C, Bb)." },
          "scale": { "type": "string", "enum": ["major", "minor", "dorian", "phrygian", "mixolydian"] },
          "bars": { "type": "integer", "default": 4, "minimum": 1, "maximum": 16 },
          "channel_name": { "type": "string", "default": "Piano" }
        },
        "required": ["key", "scale", "channel_name"]
      }
    },
    {
      "type": "function",
      "name": "extract_bassline",
      "description": "Isolate the lowest pitch of each chord in the active section, transpose down 1 octave (-12 semitones), and export a bass .mid file.",
      "parameters": {
        "type": "object",
        "properties": {
          "source_channel": { "type": "string", "default": "Piano" },
          "target_channel": { "type": "string", "default": "Bass 1" }
        },
        "required": ["source_channel", "target_channel"]
      }
    },
    {
      "type": "function",
      "name": "set_channel_gain",
      "description": "Unmute a mixer channel and set its volume using either context-aware 'mix' gain or raw 'absolute' fader gain.",
      "parameters": {
        "type": "object",
        "properties": {
          "channel_name": { "type": "string", "enum": ["Piano", "Bass 1"] },
          "percent": { "type": "number", "minimum": 0, "maximum": 100 },
          "mode": { "type": "string", "enum": ["mix", "absolute"] }
        },
        "required": ["channel_name", "percent", "mode"]
      }
    },
    {
      "type": "function",
      "name": "arrange_sections",
      "description": "Concatenate saved LOOP sections into master arrangement .mid files while current loop playback continues.",
      "parameters": {
        "type": "object",
        "properties": {
          "sequence": {
            "type": "array",
            "items": {
              "type": "object",
              "properties": {
                "section_name": { "type": "string" },
                "bars": { "type": "integer", "minimum": 1 }
              },
              "required": ["section_name", "bars"]
            }
          }
        },
        "required": ["sequence"]
      }
    },
    {
      "type": "function",
      "name": "set_talkback",
      "description": "Turn spoken voice feedback on or off on the M5Stack hardware unit.",
      "parameters": {
        "type": "object",
        "properties": {
          "enabled": { "type": "boolean" }
        },
        "required": ["enabled"]
      }
    }
  ]
}
```

---

## 6. Failure Modes, Edge Cases & Mitigations

| Failure Mode | Impact | Mitigation Strategy |
| :--- | :--- | :--- |
| **Serial Framing Desync** | Garbled PCM audio static or dropped JSON UI updates if a byte drops during USB plug/unplug. | State-machine parser in both C++ firmware and Python scans strictly for `0xAA` sync byte and validates `PAYLOAD_LEN <= 4096` before reading payload; invalid headers immediately reset parser state. |
| **Internet Drop on Laptop** | OpenAI Realtime API WebSocket disconnects during live showcase. | Python bridge catches `websockets.ConnectionClosed`, updates CoreS3 Line 3 to `NET: RECONNECTING...`, and automatically reconnects with exponential backoff (`0.5s -> 2.0s`) while preserving local `state["sections"]` in memory. |
| **Bass Extraction Before Chords** | User asks to extract bassline in a fresh section that has no chord progression yet. | Tool handler checks `if "chords" not in section`, aborts MIDI write, and sends `ERR: NO CHORDS IN SECTION` to the CoreS3 screen with a graceful spoken/visual prompt. |
| **Arrangement References Unknown Section** | User asks to arrange `"Drop"` when only `"Verse"` and `"Chorus"` exist. | `arrange_sections` validates all requested `section_name` keys against `state["sections"]` before writing files, returning a clear list of available sections if a mismatch occurs. |
| **Acoustic Feedback Loop** | CoreS3 speaker output re-enters CoreS3 microphone if user releases PTT early. | Half-duplex gating: firmware disables I2S speaker output whenever PTT button is held (`is_recording == true`) and enforces a `150ms` post-playback tail mute before re-arming mic buffers. |

---

## 7. Hackathon Implementation & Verification Plan

### 7.1 Pre-Event Host Setup (15 Minutes)
1. **Virtual MIDI Bus:** Enable **IAC Driver** (macOS) or create **`AI_Producer`** port in **loopMIDI** (Windows).
2. **FL Studio Template (`Hackathon_Demo.flp`):**
   * Channel 1: `Piano` (FL Keys) -> Mixer Insert 1 (Volume linked to `CC 20`, Mute linked to `CC 30`).
   * Channel 2: `Bass 1` (3x Osc / FLEX) -> Mixer Insert 2 (Volume linked to `CC 21`, Mute linked to `CC 31`).
   * Pin `./AI_Drops/` to the top of FL Studio's left Browser sidebar for one-drag pattern loading.

### 7.2 On-Site Build Milestones (1:30 PM – 6:30 PM)
* **Milestone 1 (1:30 PM – 2:15 PM):** Flash M5Stack CoreS3 firmware via Codex; verify `921,600` baud `0xAA` framed loopback and touch UI rendering (`[LOOP]` + `[TALKBACK: ON]`).
* **Milestone 2 (2:15 PM – 3:30 PM):** Connect Python bridge to OpenAI Realtime API; verify bidirectional voice streaming and instantaneous hardware audio cutoff when tapping `[LIVE SILENT]`.
* **Milestone 3 (3:30 PM – 4:45 PM):** Implement `write_chord_progression`, `extract_bassline`, and `set_channel_gain` (`mix` vs. `absolute`); verify live FL Studio fader movement and `.mid` drops.
* **Milestone 4 (4:45 PM – 5:45 PM):** Implement `set_production_mode` (`LOOP` vs. `ARRANGE`) and `arrange_sections` multi-section MIDI concatenation.
* **Milestone 5 (5:45 PM – 6:30 PM):** End-to-end live performance rehearsal using the scripted showcase flow.

### 7.3 Acceptance Test Criteria (Definition of Done)
* [ ] **AT-1 (Zero-Jitter Serial Link):** 60 seconds of continuous voice commands and screen telemetry execute over USB-C without a single framing error or audio glitch.
* [ ] **AT-2 (Chord -> Bass Pipeline):** Saying *"Write a G Minor chord progression"* followed by *"Extract the lowest notes to Bass 1"* produces two valid `.mid` files in `./AI_Drops/` and updates all 3 telemetry lines on the CoreS3.
* [ ] **AT-3 (Gain Staging Accuracy):** *"50% mix gain"* sets `Bass 1` to `CC 46` (when `Piano` is active at `0 dB`), whereas *"50% absolute gain"* sets `Bass 1` to `CC 64`.
* [ ] **AT-4 (Non-Destructive Loop/Arrange):** Building `Verse` then `Chorus` in `LOOP MODE`, switching to `ARRANGE MODE`, and stitching `Verse (4) + Chorus (8)` exports a 12-bar arrangement file without interrupting FL Studio playback.
* [ ] **AT-5 (Acoustic Performance Gate):** Tapping `[LIVE SILENT]` mid-response cuts speaker audio in `<50ms` while subsequent voice commands continue executing with visual-only confirmation.
