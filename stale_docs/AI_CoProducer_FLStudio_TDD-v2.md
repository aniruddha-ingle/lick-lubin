# Technical Design Document (TDD): "Lick-Lubin" Wired Hardware AI Co-Producer for FL Studio

| Metadata | Details |
| :--- | :--- |
| **Document ID** | `TDD-2026-1119-AICOPROD-v2` |
| **Status** | `LOCKED FOR BUILD` |
| **Target Event** | OpenAI for Startups — *You can just build things [with hardware]* (295 Lafayette St, NYC) |
| **Build Date** | Thursday, November 19, 2026 (1:30 PM – 7:30 PM EST) |
| **Hardware Target** | M5Stack CoreS3 / CoreS3 SE (ESP32-S3) + Focusrite Scarlett Interface + Condenser Mic |
| **Host Environment** | MacBook Pro 16" (2019, Intel Core i9, 16 GB RAM, macOS Tahoe 26.2) running Python 3.11+ & FL Studio |
| **Transport Layer** | Wired USB-C CDC Serial (`921,600` baud) + macOS CoreMIDI IAC Bus + AppleScript / FL MIDI Script |

---

## 1. Executive Summary & Problem Statement

### 1.1 North Star & Demo Arc
Build **"Lick-Lubin"**—a deadpan, minimalist AI co-producer inspired by Rick Rubin—housed in a wired **M5Stack CoreS3** control surface tethered to **FL Studio** on macOS. The live showcase follows a three-act production-to-performance arc:
1. **Act I (`LOOP MODE`):** Build an original beat from scratch by voice—generating diatonic chords, extracting a root-note bassline, laying down a drum groove, and balancing levels via context-aware `Mix Gain`.
2. **Act II (`ARRANGE MODE`):** Stitch isolated sections (`Intro`, `Verse`, `Chorus`) into a master song timeline while the active loop continues playing uninterrupted.
3. **Act III (`PERFORMANCE MODE` + Live Rap Finale):** Trigger an atomic state transition (`PERFORMANCE MODE`) that mutes Lick-Lubin's speaker (`LIVE SILENT`), switches FL Studio from Pattern to Song mode, and unmutes the Scarlett condenser vocal chain so the performer can rap live over the finished track.

### 1.2 Problem Statement & Solution Overview
Digital Audio Workstations (DAWs) such as FL Studio depend heavily on physical instrument proficiency or high-friction mouse-and-keyboard manipulation inside Piano Rolls and Mixer racks. Existing voice assistants fail on stage due to uncontrolled acoustic bleed, wireless jitter/latency, and lack of musical context.

We solve this with a **deterministic, zero-wireless-latency Hardware AI Co-Producer** built on an **M5Stack CoreS3** tethered via a single **wired USB-C cable** to a local Python orchestration bridge and **FL Studio** on macOS:
* **Zero Wireless Jitter:** Eliminates venue Wi-Fi/Bluetooth latency and drop-outs via a single `921,600` baud USB-C serial link.
* **Hands-Free Stage Flow:** Replaces manual trackpad file drags with macOS `osascript` window-focus automation + `./AI_Drops/` filesystem exports (with a pinned Browser fallback).
* **Dual-Mic Acoustic Isolation:** Keeps the M5Stack dual MEMS mics strictly for push-to-talk (`HOLD TO SPEAK`) producer commands while routing the Scarlett condenser mic exclusively to FL Studio Mixer Insert 4 for the live rap finale.

---

## 2. Goals, Non-Goals & Design Principles

### 2.1 Core Engineering Principles
* **Deterministic Wired Transport:** Zero reliance on local Wi-Fi LANs, captive portals, or Bluetooth audio. All hardware-to-host communication runs over a single USB-C cable.
* **Non-Destructive Musical State:** Musical sections are isolated in memory and on disk so building a new section never overwrites or contaminates a previously approved section.
* **Acoustic Safety in Performance:** Speaker talkback must be instantaneously killable at the hardware layer (`LIVE SILENT` mode) and automatically silenced upon entering `PERFORMANCE MODE`.

### 2.2 Explicit Goals
* **G1 (Sub-Millisecond Local Link):** Establish bidirectional framed serial transport (`921,600` baud) between the M5Stack CoreS3 and MacBook Pro Python bridge for 16 kHz PCM audio and JSON UI state packets.
* **G2 (Full Beat Generation):** Generate named MIDI chord progressions (`Piano`), octave-transposed root-note basslines (`Bass 1`), and style-based drum grooves (`Drums`) into isolated `[Section]_[Channel].mid` files with hands-free macOS import.
* **G3 (Hybrid "Lick-Lubin" Agency):** Execute explicit commands immediately with zero confirmation dialogs, followed (only when `TALKBACK: ON`) by a single deadpan, Rick Rubin-inspired arrangement critique.
* **G4 (Dual-Mode Production Workflow):** Support `LOOP MODE` (isolated 4/8-bar section building) and `ARRANGE MODE` (concatenating saved sections into a master timeline while the current loop continues playing uninterrupted).
* **G5 (Context-Aware Gain Staging):** Differentiate between `Absolute Gain` (raw fader percentage) and `Mix Gain` (dynamic headroom ceiling calculated from active mixer channels).
* **G6 (Atomic `PERFORMANCE MODE`):** Provide a single voice or touch trigger that simultaneously enables `LIVE SILENT`, switches FL Studio to Song Mode (`CC 40`), and unmutes the `Vocal` mixer insert (`CC 33`).

### 2.3 Explicit Non-Goals
* **NG1:** Real-time monophonic/polyphonic pitch tracking of hummed or sung audio (out of scope for a single-afternoon build).
* **NG2:** Custom native FL Studio C++ SDK / VST3 plugin compilation.
* **NG3:** Wireless Wi-Fi or Bluetooth fallback modes (intentionally excluded to eliminate venue RF interference and latency).

---

## 3. System Architecture & Data Flow

```text
+-----------------------------------+       USB-C CDC Serial        +------------------------------------------+
|         M5Stack CoreS3            |       (921,600 Baud)          |       MacBook Pro (macOS Tahoe 26.2)     |
|       (ESP32-S3 Hardware)         | <===========================> |           Python 3.11+ Bridge            |
|                                   |                               |                                          |
|  [Hardware Inputs]                |   Framed Binary Protocol:     |  [Serial Mux/Demux Engine]               |
|   - Dual MEMS Mics (16kHz PCM)    |   - 0x01: Mic PCM (Dev->Host) |   - Framed 0xAA packet parser            |
|   - Touch Zone 1: Hold-to-Talk    |   - 0x02: Spk PCM (Host->Dev) |                                          |
|   - Touch Zone 2: SILENT Toggle   |   - 0x03: JSON UI/State       |  [OpenAI Realtime API Client]            |
|                                   |   - 0x04: Heartbeat / ACK     |   - WebSocket + Lick-Lubin Persona       |
|  [Hardware Outputs]               |                               |   - Immediate Function Execution         |
|   - 1W Speaker (Gated by          |                               |                                          |
|     local `speak_back` flag)      |                               |  [Musical State & macOS Automation]      |
|   - 2.0" IPS LCD (320x240):       |                               |   - Section Store + Mix Gain DSP         |
|     * Top Bar: Mode + Silent Pill |                               |   - `mido` MIDI Generator (`./AI_Drops`) |
|     * Main Body: 3-Line Telemetry |                               |   - `osascript` FL Focus + Auto-Import   |
+-----------------------------------+                               +--------------------+---------------------+
                                                                                         |
                                                      +----------------------------------+----------------------------------+
                                                      |                                                                     |
                                       Filesystem + AppleScript Drop                                         macOS CoreMIDI IAC Bus
                                       `./AI_Drops/*.mid`                                                    (`IAC Driver Bus 1`)
                                                      |                                                                     |
                                                      v                                                                     v
+-----------------------------------+  +---------------------------------------------------------------------------------------------------+
|   Focusrite Scarlett + Condenser  |  |                                             FL Studio                                             |
|   - Direct Monitor: ON (zero lat) |=>|  - Channel Rack: `Piano` (Ch 1), `Bass 1` (Ch 2), `Drums` (Ch 3 FPC)                           |
|   - Routed to Mixer Insert 4      |  |  - Custom MIDI Script (`device_LickLubin.py`): Maps CC 40 -> Pattern/Song Mode (`setLoopMode`)   |
+-----------------------------------+  |  - Mixer Inserts 1–4: Linked to CCs 20–23 (Vol) & CCs 30–33 (Mute)                            |
                                       +---------------------------------------------------------------------------------------------------+
```

---

## 4. Detailed Component Specifications

### 4.1 USB-C Framed Serial Protocol (Hardware <-> Host)
To prevent bidirectional 16 kHz audio streams from colliding with control messages over a single UART/USB-CDC pipe, all traffic uses a deterministic **Length-Prefixed Type-Value (LTV) framing protocol**:

| Byte Offset | Field | Size | Description |
| :--- | :--- | :--- | :--- |
| `0` | `SYNC_BYTE` | 1 byte | Fixed magic byte `0xAA` for instant frame alignment recovery. |
| `1` | `MSG_TYPE` | 1 byte | `0x01` = Mic PCM Audio (Dev->Host)<br>`0x02` = Speaker PCM Audio (Host->Dev)<br>`0x03` = UTF-8 JSON State/UI Update (Bidirectional)<br>`0x04` = Heartbeat / ACK |
| `2..3` | `PAYLOAD_LEN` | 2 bytes | Unsigned 16-bit big-endian integer (`0` to `4096`). |
| `4..N` | `PAYLOAD` | `N` bytes | Raw 16-bit signed little-endian PCM (16 kHz mono) or UTF-8 JSON string. |

* **Bandwidth Budget:** 16 kHz, 16-bit mono PCM requires `32,000 bytes/sec` (`256 kbps`). At `921,600 baud` (~`92,160 bytes/sec` effective throughput), audio consumes **~34.7% bandwidth**, leaving >65% headroom for instantaneous JSON state updates with zero buffer bloat.

### 4.2 Firmware Specification (`M5Stack CoreS3`)
* **Display Layout (`320x240` IPS):**
  * **Header Bar (`y: 0..44`):** Split into two high-contrast touch targets:
    * Left (`x: 0..179`): Mode Indicator — `[LOOP: CHORUS]` (Blue), `[ARRANGE: 20B]` (Purple), or `[PERFORM: LIVE]` (Gold).
    * Right (`x: 180..320`): Acoustic Gate Toggle — `[TALKBACK: ON]` (Green) or `[LIVE SILENT]` (Crimson Red).
  * **Telemetry Readout (`y: 45..179`):** Dark background (`#111111`) with crisp monospaced/sans-serif 3-line status confirmation:
    * **Line 1 (Action):** `DRUMS: WRITTEN (TRAP)`
    * **Line 2 (Mixer):** `GAIN: 85% MIX (-1.4 dB)`
    * **Line 3 (File):** `DROP: Chorus_Drums.mid`
  * **PTT Trigger Zone (`y: 180..240`):** Full-width tactile button (`HOLD TO SPEAK` / `LISTENING...`).
* **Local Acoustic Gate Enforcement:**
  * Tapping `[LIVE SILENT]` (or entering `PERFORMANCE` mode) immediately flips the local boolean `speak_back = false`, flushes the hardware I2S speaker DMA ring buffer in `<50ms`, and transmits `{"event": "toggle_talkback", "enabled": false}` over `MSG_TYPE 0x03` so the host bridge stops streaming `0x02` audio frames.

### 4.3 Production State Machine (`Python Bridge`)
The Python host maintains a strict, deterministic state dictionary:

```python
state = {
    "mode": "LOOP",                 # Enum: "LOOP" | "ARRANGE" | "PERFORMANCE"
    "active_section": "Verse",      # String key into `sections`
    "speak_back": True,             # Mirrors hardware toggle state
    "bpm": 140,
    "sections": {
        # Isolated per-section memory prevents cross-section contamination:
        # "Chorus": {
        #     "key": "G",
        #     "scale": "minor",
        #     "bars": 4,
        #     "chords": [ {"bar": 0, "notes": [55, 58, 62], "duration_beats": 4}, ... ],
        #     "bass":   [ {"bar": 0, "notes": [43],         "duration_beats": 4}, ... ],
        #     "drums":  [ {"bar": 0, "notes": [36, 42],     "duration_beats": 0.5}, ... ]
        # }
    },
    "mixer": {
        "Piano":  {"active": True,  "cc_vol": 20, "cc_mute": 30, "current_cc": 102, "db": 0.0},
        "Bass 1": {"active": False, "cc_vol": 21, "cc_mute": 31, "current_cc": 0,   "db": -99.0},
        "Drums":  {"active": False, "cc_vol": 22, "cc_mute": 32, "current_cc": 0,   "db": -99.0},
        "Vocal":  {"active": False, "cc_vol": 23, "cc_mute": 33, "current_cc": 0,   "db": -99.0}
    }
}
```

#### State Transition Rules
1. **Entering `LOOP MODE` with a New Section Name:**
   * Command: *"Enter Loop Mode and call this section Chorus."*
   * Action: Sets `mode = "LOOP"`, creates a clean entry `state["sections"]["Chorus"] = {}`, and updates the CoreS3 header to `[LOOP: CHORUS]`. Subsequent composition tools operate exclusively on `Chorus` and export `Chorus_Piano.mid`, `Chorus_Bass1.mid`, and `Chorus_Drums.mid`.
2. **Entering `ARRANGE MODE` (Live Background Playback):**
   * Command: *"Switch to Arrange Mode. Lay out Intro for 4 bars, Verse for 8 bars, and Chorus for 8 bars."*
   * Action: Sets `mode = "ARRANGE"` without sending a MIDI Stop message—FL Studio continues looping the active pattern to the room. The bridge reads saved note arrays from `state["sections"]`, calculates cumulative tick offsets (`ticks_per_beat = 480`), and writes `Master_Arrangement_Piano.mid`, `Master_Arrangement_Bass1.mid`, and `Master_Arrangement_Drums.mid` into `./AI_Drops/`.
3. **Entering `PERFORMANCE MODE` (Atomic Live Rap Transition):**
   * Command: *"Turn on Performance Mode"* or *"Lick-Lubin, put me on the mic."*
   * Action: Atomically executes three operations:
     1. Sets `speak_back = False` (`LIVE SILENT`) on both host and M5Stack CoreS3.
     2. Sends `CC 40 = 127` over `IAC Driver Bus 1` to switch FL Studio from Pattern Mode to Song Mode.
     3. Unmutes Mixer Insert 4 (`Vocal`, `CC 33 = 127`) and sets its gain to Unity (`CC 23 = 102`, `0.0 dB`).

### 4.4 macOS Hands-Free MIDI Loading & FL Studio Remote Script
To guarantee zero trackpad fumbling on stage without relying on fragile global hotkeys:
1. **macOS AppleScript Auto-Load (`osascript`):**
   * Requires granting **Accessibility** permission to Terminal/IDE once during setup.
   * When a `.mid` file is written to `./AI_Drops/`, Python focuses `FL Studio`, selects the target Channel (`1`, `2`, or `3`), and triggers MIDI file import via `osascript`—while `./AI_Drops/` remains pinned at the top of FL Studio's Browser as an instant 1-drag backup.
2. **FL Studio Python MIDI Controller Script (`device_LickLubin.py`):**
   * Placed in `~/Documents/Image-Line/FL Studio/Settings/Hardware/LickLubin/device_LickLubin.py` and assigned to `IAC Driver Bus 1`:
   ```python
   # name=LickLubin
   import transport

   def OnControlChange(event):
       if event.data1 == 40:  # CC 40: Pattern (0) vs Song Mode (127)
           target_song_mode = 1 if event.data2 >= 64 else 0
           if transport.getLoopMode() != target_song_mode:
               transport.setLoopMode()
           event.handled = True
   ```

### 4.5 Gain Staging & Headroom Mathematics
FL Studio's mixer fader law maps incoming 7-bit MIDI CC (`0..127`) non-linearly from `-∞ dB` to `+5.6 dB`, with **Unity Gain (`0.0 dB`) calibrated at `CC = 102` (~80.3% fader travel)**.

The `set_channel_gain(channel_name, percent, mode, muted)` tool implements two distinct curves:

1. **`absolute` Gain Mode:**
   Directly scales the user's requested percentage `P ∈ [0, 100]` across the entire physical fader travel (`0..127`):
   ```text
   CC_abs = round((P / 100) * 127)
   ```

2. **`mix` Gain Mode (Context-Aware Headroom Ceiling):**
   Inspects `state["mixer"]` to evaluate active competing tracks before computing output CC:
   * **Rule A (Harmonic Bed Active -> Bass Ceiling):** If `Piano` is unmuted (`active == True`) at or near unity (`current_cc >= 95`), `Bass 1` receives a protective headroom ceiling of **`-4.5 dB` (`CC_ceiling = 92`)** so sub-bass does not clip the master bus.
   * **Rule B (Drums Punch Ceiling):** `Drums` receive a `-1.5 dB` punch ceiling (`CC_ceiling = 99`) when paired with melodic tracks.
   * **Rule C (Solo / Vocal Unity):** If no competing tracks are active—or when setting `Vocal`—`CC_ceiling` defaults to Unity (`102`, `0.0 dB`).
   ```text
   CC_mix = round((P / 100) * CC_ceiling)
   ```

### 4.6 "Lick-Lubin" System Prompt (Hybrid Agency)
```text
You are Lick-Lubin, a legendary, deadpan minimalist music producer inspired by Rick Rubin.
RULES:
1. OBEY FIRST: Immediately invoke the requested tool(s) with zero confirmation questions.
2. BREVITY: Keep spoken responses under 15 words. Calm, low-ego, decisive tone.
3. ONE-LINE RUBIN CRITIQUE: After executing a composition or arrangement tool, add ONE short, tasteful production suggestion (e.g., "Chords are in. Give it a sparse trap groove so the pocket breathes." or "Arrangement is locked. Strip the piano on the verse so your voice owns the room.").
4. SILENT MODE: When entering PERFORMANCE mode or when talkback is disabled, return minimal text only.
```

---

## 5. OpenAI Realtime API Tool Interface

The Python bridge registers the following JSON tool schemas with the OpenAI Realtime API session (`session.update`):

```json
{
  "tools": [
    {
      "type": "function",
      "name": "set_production_mode",
      "description": "Switch between LOOP (isolated section building), ARRANGE (timeline stitching while playing), and PERFORMANCE (atomic live rap mode: enables LIVE SILENT, switches FL Studio to Song Mode via CC 40, and unmutes Vocal channel).",
      "parameters": {
        "type": "object",
        "properties": {
          "mode": { "type": "string", "enum": ["LOOP", "ARRANGE", "PERFORMANCE"] },
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
      "name": "write_drum_pattern",
      "description": "Generate a MIDI drum pattern (Kick C5, Clap/Snare D5, Closed Hat F#5) mapped to FL Studio FPC in the active LOOP section.",
      "parameters": {
        "type": "object",
        "properties": {
          "style": { "type": "string", "enum": ["trap", "boom_bap", "minimal_808"] },
          "bars": { "type": "integer", "default": 4, "minimum": 1, "maximum": 16 },
          "channel_name": { "type": "string", "default": "Drums" }
        },
        "required": ["style", "channel_name"]
      }
    },
    {
      "type": "function",
      "name": "set_channel_gain",
      "description": "Unmute or mute a mixer channel and set its volume using either context-aware 'mix' gain or raw 'absolute' fader gain.",
      "parameters": {
        "type": "object",
        "properties": {
          "channel_name": { "type": "string", "enum": ["Piano", "Bass 1", "Drums", "Vocal"] },
          "percent": { "type": "number", "minimum": 0, "maximum": 100 },
          "mode": { "type": "string", "enum": ["mix", "absolute"] },
          "muted": { "type": "boolean", "default": false }
        },
        "required": ["channel_name", "percent", "mode"]
      }
    },
    {
      "type": "function",
      "name": "arrange_sections",
      "description": "Concatenate saved LOOP sections into master arrangement .mid files for Piano, Bass 1, and Drums while current loop playback continues.",
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
| **Stage Monitor Feedback into Condenser Mic** | High-pitched acoustic feedback loop during the live rap finale. | Keep venue/monitor speakers low or wear closed-back headphones during Act III; keep Scarlett Direct Monitoring `ON` and Mixer Insert 4 muted (`CC 33 = 0`) until `PERFORMANCE MODE` activates. |
| **2019 Intel MacBook Pro Thermal Throttling** | Audio buffer underruns (crackles) under simultaneous Realtime API + FL Studio load. | Set FL Studio Audio Buffer to `256` or `512` samples (`44.1 kHz`), close Chrome/background apps, and run on AC power. |
| **macOS Accessibility / Focus Misfire** | AppleScript MIDI import fails if a system dialog steals focus. | Keep `./AI_Drops/` pinned to the top of FL Studio's Browser panel for a zero-latency 1-drag fallback. |
| **Serial Framing Desync** | Garbled PCM audio static or dropped JSON UI updates if a byte drops during USB plug/unplug. | State-machine parser in both C++ firmware and Python scans strictly for `0xAA` sync byte and validates `PAYLOAD_LEN <= 4096` before reading payload; invalid headers immediately reset parser state. |
| **Internet Drop on Laptop** | OpenAI Realtime API WebSocket disconnects during live showcase. | Python bridge catches `websockets.ConnectionClosed`, updates CoreS3 Line 3 to `NET: RECONNECTING...`, and automatically reconnects with exponential backoff (`0.5s -> 2.0s`) while preserving local `state["sections"]` in memory. |
| **Bass Extraction Before Chords** | User asks to extract bassline in a fresh section that has no chord progression yet. | Tool handler checks `if "chords" not in section`, aborts MIDI write, and sends `ERR: NO CHORDS IN SECTION` to the CoreS3 screen. |

---

## 7. Hackathon Implementation & Verification Plan

### 7.1 Pre-Event macOS Setup (15 Minutes)
1. **Virtual MIDI Bus:** Open `Audio MIDI Setup.app` -> Show MIDI Studio -> Enable **IAC Driver** (`Device is online`, `Bus 1`).
2. **macOS Permissions:** `System Settings -> Privacy & Security -> Accessibility` -> Enable **Terminal** (or VS Code/Cursor).
3. **FL Studio Controller Script:** Save `device_LickLubin.py` into `~/Documents/Image-Line/FL Studio/Settings/Hardware/LickLubin/` and assign `LickLubin` as the Controller Type for `IAC Driver Bus 1` in FL Studio MIDI Settings.
4. **FL Studio Template (`LickLubin_Stage.flp`):**
   * Channel 1: `Piano` (FL Keys) -> Mixer Insert 1 (Volume `CC 20`, Mute `CC 30`).
   * Channel 2: `Bass 1` (3x Osc / FLEX) -> Mixer Insert 2 (Volume `CC 21`, Mute `CC 31`).
   * Channel 3: `Drums` (FPC Hip-Hop/Trap Kit) -> Mixer Insert 3 (Volume `CC 22`, Mute `CC 32`).
   * Mixer Insert 4: `Vocal` (Scarlett Input 1 -> Pitch Correction + Compressor + Reverb, muted by default, Volume `CC 23`, Mute `CC 33`).
   * Pin `./AI_Drops/` to the top of FL Studio's left Browser sidebar for one-drag pattern fallback.

### 7.2 On-Site Build Milestones (1:30 PM – 6:30 PM)
* **Milestone 1 (1:30 PM – 2:15 PM):** Flash M5Stack CoreS3 firmware; verify `921,600` baud `0xAA` framed loopback and touch UI rendering (`[LOOP]` + `[TALKBACK: ON]`).
* **Milestone 2 (2:15 PM – 3:30 PM):** Connect Python bridge to OpenAI Realtime API with Lick-Lubin system prompt; verify bidirectional voice streaming and `<50ms` `LIVE SILENT` DMA cutoff.
* **Milestone 3 (3:30 PM – 4:45 PM):** Implement `write_chord_progression`, `extract_bassline`, `write_drum_pattern`, and `set_channel_gain` (`mix` vs. `absolute`) + AppleScript auto-import.
* **Milestone 4 (4:45 PM – 5:45 PM):** Implement `set_production_mode` (`LOOP`, `ARRANGE`, and atomic `PERFORMANCE` mode with `CC 40` Song Mode + `Vocal` unmute) and `arrange_sections`.
* **Milestone 5 (5:45 PM – 6:30 PM):** End-to-end live performance rehearsal of the 3-minute stage demo + rap verse.

### 7.3 Acceptance Test Criteria (Definition of Done)
* [ ] **AT-1 (Zero-Jitter Serial Link):** 60 seconds of continuous voice commands and screen telemetry execute over USB-C without a single framing error or audio glitch.
* [ ] **AT-2 (Full Beat Pipeline):** Generating chords (`Piano`), extracting root notes (`Bass 1`), and writing a drum groove (`Drums`) produces three valid `.mid` files in `./AI_Drops/`, auto-loads into FL Studio, and updates all 3 telemetry lines on the CoreS3.
* [ ] **AT-3 (Gain Staging Accuracy):** *"50% mix gain"* sets `Bass 1` to `CC 46` (when `Piano` is active at `0 dB`), whereas *"50% absolute gain"* sets `Bass 1` to `CC 64`.
* [ ] **AT-4 (Non-Destructive Loop/Arrange):** Building `Verse` then `Chorus` in `LOOP MODE`, switching to `ARRANGE MODE`, and stitching `Verse (4) + Chorus (8)` exports 12-bar arrangement files without interrupting FL Studio playback.
* [ ] **AT-5 (Atomic Performance Mode & Acoustic Gate):** Triggering `PERFORMANCE MODE` simultaneously mutes CoreS3 talkback (`<50ms`), switches FL Studio to Song Mode (`CC 40`), and unmutes Mixer Insert 4 (`Vocal`) for the live rap finale.
