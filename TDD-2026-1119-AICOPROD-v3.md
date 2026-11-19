# Technical Design Document (TDD): "Lick-Lubin" Wired Hardware AI Co-Producer for FL Studio

| Metadata | Details |
| :--- | :--- |
| **Document ID** | `TDD-2026-1119-AICOPROD-v3` |
| **Status** | `LOCKED FOR BUILD (FINAL)` |
| **Target Event** | OpenAI for Startups — *You can just build things [with hardware]* (295 Lafayette St, NYC) |
| **Build Date** | Thursday, November 19, 2026 (1:30 PM – 6:30 PM EST) |
| **Hardware Target** | M5Stack CoreS3 / CoreS3 SE (ESP32-S3) + Focusrite Scarlett Interface + Condenser Mic |
| **Host Environment** | MacBook Pro 16" (2019, Intel Core i9, 16 GB RAM, macOS Tahoe 26.2) running Python 3.11+ & FL Studio |
| **Musical Blueprint** | **101 BPM**, **D Major** (Laid-back, warm hip-hop pocket) |
| **Transport Layer** | Wired USB-C CDC Serial (`921,600` baud) + macOS CoreMIDI IAC Bus (`IAC Driver Bus 1`) |

---

## 1. Executive Summary & North Star

### 1.1 North Star & 3-Act Hands-Free Demo Arc
Build **"Lick-Lubin"**—a deadpan, minimalist AI co-producer inspired by Rick Rubin—housed in a wired **M5Stack CoreS3** control surface tethered to **FL Studio** on macOS. Because the laptop screen is cast to the stage display while the performer stands up to produce and rap, **all three acts are 100% hands-free (zero trackpad touches, zero modal dialogs)**:

1. **Act I (`LOOP MODE` — Hands-Free Section Building):** Build an original `101 BPM` `D Major` beat by voice—generating chords (`Ch 1: Piano`), extracting a root-note bassline (`Ch 2: Bass 1`), laying down human-velocity drum grooves (`Ch 3: Drums`), and balancing levels via context-aware `Mix Gain`. Python streams live MIDI notes directly over `IAC Driver Bus 1` while exporting `.mid` files to `./AI_Drops/`.
2. **Act II (`ARRANGE MODE` — Hands-Free Timeline Stitching):** Stitch isolated sections (`Intro`, `Verse`, `Chorus`) into a linear 16-bar arrangement in Python memory (and export `Master_Arrangement_*.mid` to `./AI_Drops/`) while the active 4-bar loop continues playing uninterrupted.
3. **Act III (`PERFORMANCE MODE` — Atomic Zero-Touch Rap Finale):** One voice command (`"Lick-Lubin, performance mode"`) atomically:
   * Resets Python's drift-free `perf_counter` CoreMIDI sequencer to **Bar 1, Tick 0** in linear `SONG` playback mode (`Intro` -> `Verse` -> `Chorus` -> deterministic auto-stop + `CC 123` All Notes Off at Bar 16 / `38.02s`).
   * Mutes Lick-Lubin's hardware speaker (`LIVE SILENT`).
   * Unmutes the Scarlett condenser vocal chain (`CC 33 = 127`) on FL Studio Mixer Insert 4 so the performer raps live without touching the MacBook.

### 1.2 Architectural Simplifications in `v3`
* **100% Python `perf_counter` CoreMIDI Sequencer Across All 3 Acts:** Eliminates fragile `osascript` `Cmd+M` file dialogs, `open -a "FL Studio"` import modals, `0xF8` MIDI clock slave drift, FL Studio Song Mode (`CC 40`), and custom FL Studio MIDI Remote Scripts (`device_LickLubin.py`). FL Studio runs purely as a low-latency sound module and mixer.
* **Sub-Millisecond Timing Precision:** Ticks are scheduled against `time.perf_counter()` (`target_time = start_time + tick_index * (60.0 / 101.0 / 4.0)`) rather than cumulative `asyncio.sleep()`, guaranteeing zero drift across the entire 16-bar (`256`-tick, `38.02s`) rap finale.
* **Dual-Mic Acoustic Isolation:** M5Stack dual MEMS mics handle push-to-talk (`HOLD TO SPEAK`) producer commands; Focusrite Scarlett + condenser mic routes exclusively to FL Studio Mixer Insert 4 for the rap performance.

---

## 2. Goals & Non-Goals

### 2.1 Explicit Goals
* **G1 (Sub-Millisecond Local Link):** Bidirectional `0xAA`-framed USB-C serial (`921,600` baud) carrying 16 kHz mono PCM audio and UTF-8 JSON telemetry.
* **G2 (Hands-Free Beat Generation @ 101 BPM, D Major):** Generate diatonic `D Major` chord progressions (`Piano`), derived octave-down basslines (`Bass 1`), and human-velocity drum grooves (`Drums`) streamed live over CoreMIDI and archived to `./AI_Drops/*.mid`.
* **G3 (Hybrid "Lick-Lubin" Agency):** Execute explicit commands immediately with zero confirmation questions, followed (only when `TALKBACK: ON`) by a single deadpan, Rick Rubin-inspired arrangement critique.
* **G4 (Zero-Touch `LOOP` -> `ARRANGE` -> `PERFORMANCE` Flow):** Seamless transition from 4-bar section looping to linear 16-bar song playback + live vocal unmute with zero mouse/trackpad interaction.

### 2.2 Explicit Non-Goals
* **NG1:** Real-time pitch-to-MIDI tracking of hummed melodies.
* **NG2:** Native C++ VST3/AU plugin compilation or custom FL Studio MIDI Python scripts.
* **NG3:** Wireless Wi-Fi or Bluetooth transport fallback.

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
|     local `speak_back` flag)      |                               |  [Drift-Free CoreMIDI Sequencer]         |
|   - 2.0" IPS LCD (320x240):       |                               |   - 101 BPM `time.perf_counter()` clock  |
|     * Top Bar: Mode + Silent Pill |                               |   - LOOP (4-bar) & SONG (16-bar) modes   |
|     * Main Body: 3-Line Telemetry |                               |   - `mido` archival export (`./AI_Drops`) |
+-----------------------------------+                               +--------------------+---------------------+
                                                                                         |
                                                                           macOS CoreMIDI IAC Bus
                                                                           (`IAC Driver Bus 1`)
                                                                           - Ch 1: Piano Notes
                                                                           - Ch 2: Bass 1 Notes
                                                                           - Ch 3: Drums Notes (FPC)
                                                                           - Ch 16: Mixer CCs (20-23, 30-33)
                                                                                         |
                                                                                         v
+-----------------------------------+  +---------------------------------------------------------------------------------------------------+
|   Focusrite Scarlett + Condenser  |  |                                             FL Studio                                             |
|   - Direct Monitor: ON (zero lat) |=>|  - Channel Rack (Omni/Multi-Channel Receive):                                                     |
|   - Routed to Mixer Insert 4      |  |      * Ch 1 -> `Piano` (FL Keys / Rhodes) -> Mixer Insert 1 (CC 20 Vol / CC 30 Mute)              |
+-----------------------------------+  |      * Ch 2 -> `Bass 1` (FLEX / 3x Osc)   -> Mixer Insert 2 (CC 21 Vol / CC 31 Mute)              |
                                       |      * Ch 3 -> `Drums` (FPC Hip-Hop Kit)  -> Mixer Insert 3 (CC 22 Vol / CC 32 Mute)              |
                                       |  - Mixer Insert 4: `Vocal` (Pitch + Comp + Reverb) -> CC 23 Vol / CC 33 Mute (Muted until Act III) |
                                       +---------------------------------------------------------------------------------------------------+
```

---

## 4. Detailed Component Specifications

### 4.1 USB-C Framed Serial Protocol (Hardware <-> Host)
All traffic uses a deterministic **Length-Prefixed Type-Value (LTV)** framing format:

| Byte Offset | Field | Size | Description |
| :--- | :--- | :--- | :--- |
| `0` | `SYNC_BYTE` | 1 byte | Fixed magic byte `0xAA` for instant frame alignment recovery. |
| `1` | `MSG_TYPE` | 1 byte | `0x01` = Mic PCM Audio (Dev->Host)<br>`0x02` = Speaker PCM Audio (Host->Dev)<br>`0x03` = UTF-8 JSON State/UI Update (Bidirectional)<br>`0x04` = Heartbeat / ACK |
| `2..3` | `PAYLOAD_LEN` | 2 bytes | Unsigned 16-bit big-endian integer (`0` to `4096`). |
| `4..N` | `PAYLOAD` | `N` bytes | Raw 16-bit signed little-endian PCM (16 kHz mono) or UTF-8 JSON string. |

### 4.2 Firmware Specification (`M5Stack CoreS3`)
* **Display Layout (`320x240` IPS):**
  * **Header Bar (`y: 0..44`):**
    * Left (`x: 0..179`): Mode Badge — `[LOOP: VERSE]` (Blue), `[ARRANGE: 16B]` (Purple), or `[PERFORM: LIVE]` (Gold).
    * Right (`x: 180..320`): Acoustic Gate — `[TALKBACK: ON]` (Green) or `[LIVE SILENT]` (Crimson Red).
  * **Telemetry Body (`y: 45..179`):** High-contrast 3-line readout on `#111111`:
    * **Line 1 (Action):** `DRUMS: BOOM_BAP (101 BPM)`
    * **Line 2 (Mixer):** `GAIN: 85% MIX (-1.4 dB)`
    * **Line 3 (Export):** `SAVED: Verse_Drums.mid`
  * **PTT Trigger (`y: 180..240`):** Full-width tactile button (`HOLD TO SPEAK` / `LISTENING...`).
* **Hardware Acoustic Cutoff:** Entering `PERFORMANCE` mode (or tapping `[LIVE SILENT]`) immediately sets `speak_back = false`, flushes the I2S speaker DMA buffer in `<50ms`, and sends `{"event": "set_mode", "speak_back": false}` over `0x03`.

### 4.3 Drift-Free `perf_counter` CoreMIDI Step-Sequencer (`Python Bridge`)
* **Timing Math (`101 BPM`, 16th-Note Resolution):**
  * `SECONDS_PER_BEAT = 60.0 / 101.0` (`0.594059s`)
  * `SECONDS_PER_TICK = SECONDS_PER_BEAT / 4.0` (`0.148515s` per 16th note, `16 ticks/bar`)
  * `target_time = start_time + tick_index * SECONDS_PER_TICK`
* **Playback Modes:**
  * `LOOP`: Continuously wraps `tick_index % (active_section_bars * 16)` so any newly generated chords, basslines, or drums join seamlessly on the very next bar/step.
  * `SONG` (Activated by `PERFORMANCE MODE`): Resets `start_time = time.perf_counter()` and `tick_index = 0`, plays the linear stitched timeline (`Intro` [4 bars] -> `Verse` [8 bars] -> `Chorus` [4 bars] = `256` ticks = `38.02s`), and automatically sends `CC 123` (`All Notes Off`) on Channels `1, 2, 3` at `tick_index == total_ticks` for a crisp final hit.
* **Stuck-Note Prevention:** Any section overwrite, channel mute (`CC 30–32`), or transport stop immediately transmits `CC 123` (`All Notes Off`) on the affected MIDI channel.

### 4.4 Musical Blueprint (`101 BPM`, `D Major`) & Human-Velocity Templates
* **Chord Voicings (`D Major` Warm Neo-Soul / Hip-Hop Pocket):**
  * `Verse` (4 bars): `Dmaj9` (`D3, F#3, A3, C#4, E4`) -> `Bm7` (`B2, D3, F#3, A3`) -> `Gmaj7` (`G2, B2, D3, F#3`) -> `A6` (`A2, C#3, E3, F#3`).
  * `Chorus` (4 bars): `Em9` (`E3, G3, B3, D4, F#4`) -> `A13` (`A2, G3, C#4, F#4`) -> `Dmaj9` (`D3, F#3, A3, C#4, E4`) -> `B7#9` (`B2, D#3, A3, D4`).
  * `Intro` (4 bars): `Dmaj9` -> `Gmaj7` (sparse half-note Rhodes/Piano swell).
* **Drum Pattern Mapping (FL Studio FPC Standard Notes):**
  * Kick = `C5` (`MIDI 60`), Snare/Clap = `D5` (`MIDI 62`), Closed Hat = `F#5` (`MIDI 66`), Open Hat = `A#5` (`MIDI 70`).
  * **Human Velocity Curve:** Downbeat kicks at velocity `114`, syncopated ghost kicks at `84`, backbeat snares (beats 2 & 4) at `118`, and 8th/16th hi-hats alternating `88 -> 58 -> 76 -> 52` for natural head-nod swing at `101 BPM`.

### 4.5 Gain Staging & Headroom Mathematics
Unity Gain (`0.0 dB`) in FL Studio's mixer corresponds to `CC = 102` on MIDI Channel 16 (`index 15`):
* **`absolute` Mode:** `CC_abs = round((P / 100) * 127)`
* **`mix` Mode (Context-Aware Headroom):**
  * When `Piano` is active (`current_cc >= 95`), `Bass 1` receives a `-4.5 dB` ceiling (`CC_ceiling = 92`) so sub-bass leaves headroom for the kick and vocal.
  * `Drums` receive a `-1.5 dB` punch ceiling (`CC_ceiling = 99`), and `Vocal` defaults to Unity (`CC_ceiling = 102`) when armed in `PERFORMANCE MODE`.

### 4.6 "Lick-Lubin" System Prompt (Hybrid Agency)
```text
You are Lick-Lubin, a legendary, deadpan minimalist music producer inspired by Rick Rubin.
RULES:
1. OBEY FIRST: Immediately invoke the requested tool(s) with zero confirmation questions.
2. BREVITY: Keep spoken responses under 15 words. Calm, low-ego, decisive tone.
3. ONE-LINE RUBIN CRITIQUE: After executing a composition or arrangement tool, add ONE short, tasteful production suggestion (e.g., "Chords are in. Give it a laid-back boom-bap groove so the pocket breathes." or "Arrangement is locked. Let's take it to Performance Mode and put you on the mic.").
4. SILENT MODE: When entering PERFORMANCE mode or when talkback is disabled, return minimal text only.
```

---

## 5. OpenAI Realtime API Tool Schemas

```json
{
  "tools": [
    {
      "type": "function",
      "name": "set_production_mode",
      "description": "Switch between LOOP (4-bar section building), ARRANGE (linear timeline stitching while playing), and PERFORMANCE (atomic live rap mode: resets CoreMIDI sequencer to Bar 1 of linear song arrangement, enables LIVE SILENT, and unmutes Vocal channel).",
      "parameters": {
        "type": "object",
        "properties": {
          "mode": { "type": "string", "enum": ["LOOP", "ARRANGE", "PERFORMANCE"] },
          "section_name": { "type": "string", "description": "Active section name when in LOOP mode (e.g., Intro, Verse, Chorus)." }
        },
        "required": ["mode"]
      }
    },
    {
      "type": "function",
      "name": "write_chord_progression",
      "description": "Generate a chord progression in the active LOOP section, queue it immediately into the 101 BPM CoreMIDI sequencer (Ch 1), and export a .mid file to AI_Drops.",
      "parameters": {
        "type": "object",
        "properties": {
          "key": { "type": "string", "default": "D" },
          "scale": { "type": "string", "enum": ["major", "minor", "dorian", "phrygian", "mixolydian"], "default": "major" },
          "bars": { "type": "integer", "default": 4, "minimum": 1, "maximum": 16 },
          "channel_name": { "type": "string", "default": "Piano" }
        },
        "required": ["key", "scale", "channel_name"]
      }
    },
    {
      "type": "function",
      "name": "extract_bassline",
      "description": "Isolate the lowest pitch of each chord in the active section, transpose -12 semitones, queue into CoreMIDI sequencer (Ch 2), and export a bass .mid file.",
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
      "description": "Generate a human-velocity MIDI drum pattern (Kick C5, Snare D5, Hat F#5) into the CoreMIDI sequencer (Ch 3) and export a .mid file.",
      "parameters": {
        "type": "object",
        "properties": {
          "style": { "type": "string", "enum": ["boom_bap", "trap", "minimal_808"], "default": "boom_bap" },
          "bars": { "type": "integer", "default": 4, "minimum": 1, "maximum": 16 },
          "channel_name": { "type": "string", "default": "Drums" }
        },
        "required": ["style", "channel_name"]
      }
    },
    {
      "type": "function",
      "name": "set_channel_gain",
      "description": "Unmute or mute a mixer channel via CoreMIDI CC and set its volume using 'mix' (headroom-aware) or 'absolute' gain.",
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
      "description": "Stitch saved LOOP sections into a linear song timeline in Python memory (and export Master_Arrangement_*.mid to AI_Drops) while LOOP playback continues uninterrupted.",
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
      "description": "Toggle spoken voice feedback on or off on the M5Stack CoreS3.",
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

## 6. Failure Modes & Hardware/macOS Mitigations

| Failure Mode | Impact | Mitigation Strategy |
| :--- | :--- | :--- |
| **Stage Monitor Feedback into Condenser Mic** | High-pitched acoustic feedback loop during Act III rap finale. | Keep venue speakers moderate or wear closed-back headphones during Act III; keep Scarlett Direct Monitoring `ON` and Insert 4 muted (`CC 33 = 0`) until `PERFORMANCE MODE` activates. |
| **2019 Intel MacBook Pro Thermal Throttling** | Audio buffer underruns (crackles) under simultaneous Realtime API + FL Studio load. | Set FL Studio Audio Buffer to `256` or `512` samples (`44.1 kHz`), close Chrome/background apps, and run on AC power. |
| **Stuck MIDI Notes on Section Change / Stop** | Sustained synth/piano note ringing over speech or after song ends. | Sequencer tracks active pitches per channel and sends explicit `note_off` + `CC 123` (`All Notes Off`) on every mute, stop, or song completion (`tick == total_ticks`). |
| **Serial Framing Desync** | Audio static or dropped JSON updates. | Strict `0xAA` sync-byte state machine + `PAYLOAD_LEN <= 4096` bounds check on both ESP32-S3 and Python. |

---

## 7. Pre-Event Setup & 5-Hour Build Schedule

### 7.1 Pre-Event macOS & FL Studio Setup (10 Minutes)
1. **Audio MIDI Setup:** Open `Audio MIDI Setup.app` -> Show MIDI Studio -> Enable **IAC Driver** (`Device is online`, `Bus 1`).
2. **FL Studio MIDI Settings:** Enable `IAC Driver Bus 1` as Input (`Generic Controller` — no custom script required).
3. **FL Studio Template (`LickLubin_Stage.flp` at `101 BPM`):**
   * Channel 1 (`Receive Notes From -> IAC Driver Bus 1 -> Channel 1`): `Piano` (FL Keys / Electric Piano) -> Mixer Insert 1 (`CC 20` Vol, `CC 30` Mute).
   * Channel 2 (`Receive Notes From -> IAC Driver Bus 1 -> Channel 2`): `Bass 1` (FLEX / 3x Osc) -> Mixer Insert 2 (`CC 21` Vol, `CC 31` Mute).
   * Channel 3 (`Receive Notes From -> IAC Driver Bus 1 -> Channel 3`): `Drums` (FPC Hip-Hop/Boom-Bap Kit) -> Mixer Insert 3 (`CC 22` Vol, `CC 32` Mute).
   * Mixer Insert 4: `Vocal` (Scarlett Input 1 -> Pitch Correction + Compressor + Reverb, muted by default, `CC 23` Vol, `CC 33` Mute).

### 7.2 On-Site Build Milestones (1:30 PM – 6:30 PM)
* **1:30 – 2:15 PM:** Flash M5Stack CoreS3 firmware (`921,600` baud `0xAA` serial + touch UI + `<50ms` `LIVE SILENT` DMA cutoff).
* **2:15 – 3:15 PM:** Connect Python bridge to OpenAI Realtime API with Lick-Lubin system prompt and verify voice round-trip.
* **3:15 – 4:30 PM:** Implement drift-free `time.perf_counter()` CoreMIDI sequencer (`101 BPM`, `D Major` chord/bass/drum generators + `CC 123` cleanup + `./AI_Drops` `.mid` export).
* **4:30 – 5:30 PM:** Wire `LOOP`, `ARRANGE`, and atomic `PERFORMANCE` mode (`Bar 1` linear song reset + `Vocal` unmute + `LIVE SILENT`).
* **5:30 – 6:30 PM:** Full dress rehearsal of the 3-minute stage demo + 16-bar (`38.02s`) rap finale.
