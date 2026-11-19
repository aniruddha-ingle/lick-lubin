# Lick-Lubin — Hands-Free Hardware AI Music Co-Producer

**Lick-Lubin** is a tactile hardware AI co-producer built for live beat-making and rap performance. Powered by an **M5Stack CoreS3 (ESP32-S3)**, **OpenAI Realtime API (`gpt-realtime`)**, and a drift-free **macOS CoreMIDI (`time.perf_counter()`) step-sequencer** driving **FL Studio**, it lets a performer build, arrange, and mix a track completely hands-free—then silence the AI with one tap (`LIVE SILENT`) to drop into a live rap finale.

---

## System Architecture

```text
┌─────────────────────────────┐   USB-C CDC (921,600 baud)   ┌──────────────────────────────────┐
│     M5Stack CoreS3 (S3)     │ ◄═══ 0xAA LTV Framing ════►  │   MacBook Pro (host_bridge.py)   │
│                             │  • 0x01: 24kHz Mic PCM16     │                                  │
│ • Dual-MEMS Mic (24 kHz)    │  • 0x02: 24kHz Spk PCM16     │ • OpenAI Realtime WebSocket      │
│ • Speaker + DMA Cutoff      │  • 0x03: JSON UI/PTT Events  │ • 7 Function-Calling Studio Tools│
│ • 320x240 IPS Touch UI      │  • 0x04: Heartbeat           │ • 101 BPM perf_counter Sequencer │
└─────────────────────────────┘                              └────────────────┬─────────────────┘
                                                                              │ CoreMIDI (IAC Bus 1)
                                                                              ▼
                                                             ┌──────────────────────────────────┐
                                                             │       FL Studio (Sound Module)   │
                                                             │ • Ch 1 (0x90): Piano (D Major)   │
                                                             │ • Ch 2 (0x91): Bass 1            │
                                                             │ • Ch 3 (0x92): FPC Drums         │
                                                             │ • Ch 16 (0xBF): Mixer CCs 20–33  │
                                                             │ • Insert 4: Scarlett Vocal Mic   │
                                                             └──────────────────────────────────┘
```

---

## Repository Map

```text
lick-lubin/
├── README.md                        # You are here
├── TDD-2026-1119-AICOPROD-v3.md     # Locked Technical Design Document (Single Source of Truth)
├── BDD-2026-1119-AICOPROD-v2.md     # 12 Gherkin BDD acceptance scenarios
├── aicoprod/
│   ├── firmware/
│   │   ├── platformio.ini           # ESP32-S3 build config (M5Unified + ArduinoJson, PSRAM)
│   │   └── src/main.cpp             # Half-duplex I2S audio, touch UI, 0xAA serial framing
│   └── host/
│       ├── requirements.txt         # Python dependencies (openai, pyserial, rtmidi, mido, pytest)
│       ├── host_bridge.py           # Realtime API bridge + 101 BPM CoreMIDI sequencer
│       └── tests/                   # 15 offline pytest verification tests (<3s runtime)
├── personas/                        # Agent prompts used to build & audit the system
└── stale_docs/                      # Archived v1 drafts
```

---

## Hardware & Software Prerequisites

1. **Hardware:**
   * **M5Stack CoreS3** (ESP32-S3) + USB-C data cable
   * **MacBook Pro** (macOS) + audio interface / mic for live vocals
2. **macOS Audio MIDI Setup:**
   * Open **Audio MIDI Setup** -> **Window -> Show MIDI Studio** -> double-click **IAC Driver** -> check **Device is online** (`Bus 1`).
3. **FL Studio Channel & Mixer Mapping (`LickLubin_Stage.flp`):**
   * **Channel 1 (`MIDI Ch 1`):** Piano / Keys (`Dmaj9 -> Bm7 -> Gmaj7 -> A6`)
   * **Channel 2 (`MIDI Ch 2`):** Bass 1 (auto-extracted 1 octave below chord roots)
   * **Channel 3 (`MIDI Ch 3`):** FPC Drum Kit (`Kick=C5/60`, `Snare=D5/62`, `ClosedHat=F#5/66`, `OpenHat=A#5/70`)
   * **Mixer Inserts 1–4 (`MIDI Ch 16`):**
     * Faders: `CC 20` (Piano), `CC 21` (Bass, capped at `92` / `-4.5 dB`), `CC 22` (Drums), `CC 23` (Vocal)
     * Mutes: `CC 30` (Piano), `CC 31` (Bass), `CC 32` (Drums), `CC 33` (Vocal — unmuted automatically in `PERFORMANCE` mode)

---

## Quickstart Commands

### 1. Run Offline Verification Suite (`< 3 seconds`)
```bash
cd aicoprod/host
source ../../.venv-lick-lubin/bin/activate
pytest -v
```

### 2. Flash M5Stack CoreS3 Firmware
```bash
cd aicoprod/firmware
pio run -t upload
```

### 3. Start the Live Host Bridge
```bash
cd aicoprod/host
export OPENAI_API_KEY="sk-..."
python host_bridge.py
```
*(Auto-discovers the M5Stack CoreS3 USB-C port via Espressif VID `0x303A` at `921,600` baud and binds to `IAC Driver Bus 1`.)*

---

## CoreS3 Touchscreen Controls & 3-Act Flow

* **Bottom Zone (`HOLD TO SPEAK`):** Press and hold to talk to Lick-Lubin (switches CoreS3 to `24 kHz` Mic mode); release to commit turn and hear a 1-line producer response + immediate beat update.
* **Top-Right Pill (`TALKBACK` / `LIVE SILENT`):** Tap anytime for `< 50 ms` acoustic speaker cutoff + `response.cancel()`.
* **Act I — `LOOP` Mode:** Builds a 4-bar loop at **101 BPM** in **D Major** (chords, bassline, boom-bap/trap drums, mix balance).
* **Act II — `ARRANGE` Mode:** Stitches `Intro (4 bars) -> Verse (8 bars) -> Chorus (4 bars)` into a 16-bar timeline while exporting archival `.mid` stems to `./AI_Drops/`.
* **Act III — `PERFORMANCE` Mode:** Atomically resets playhead to Bar 1, engages `LIVE SILENT`, unmutes your vocal mic (`CC 33 = 127`), plays the full 16-bar arrangement (`38.02s`), and sends `CC 123` (`All Notes Off`) on the final downbeat.
