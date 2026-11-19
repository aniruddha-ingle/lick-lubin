# Lick-Lubin — Hackathon Day-Of Runbook & Codex Context

**Event:** OpenAI for Startups — *"You can just build things [with hardware]"*  
**Date & Location:** Thursday, November 19, 2026 | OpenAI NYC HQ (1:30 PM – 6:00 PM+ ET)  
**Repository:** `https://github.com/aniruddha-ingle/lick-lubin` (Baseline Tag: `v1-rock-solid`)

---

## 1. Codex Agent Architecture & Hard Invariants

* **Hardware Controller:** M5Stack CoreS3 (ESP32-S3, `320x240` GT911 touchscreen, ES7210 mic, AW88298 speaker amp, battery base) with optional peripherals via USB-C CDC at **`921,600` baud** (`0x303A`).
* **Audio Constraints:** Half-duplex I2S bus requires toggling `M5.Mic` and `M5.Speaker` sequentially, locked at **`24,000 Hz`** mono 16-bit PCM. `< 50 ms` acoustic cutoff (`LIVE SILENT`) immediately flushes local audio buffers and cancels WebSocket responses.
* **Protocols & Sequencer:** Uses `0xAA` LTV serial framing and a drift-free `time.perf_counter()` sequencer running at **`101 BPM`** driving FL Studio via `IAC Driver Bus 1` (`python-rtmidi`), mapping Piano (Ch 1), Bass (Ch 2), FPC Drums (Ch 3), and Mixer CC automation (Ch 16).

---

## 2. Day-Of Schedule & Execution Checklist

* **Phase 1 (1:30–2:00 PM):** Unbox CoreS3, battery base, and verify serial connection (`ls /dev/cu.usbmodem*`).
* **Phase 2 (2:00–2:45 PM):** Run offline `pytest`, flash CoreS3 firmware (`pio run -t upload`), and configure FL Studio with `IAC Driver Bus 1`.
* **Phase 3 (2:45–3:15 PM):** Execute end-to-end smoke tests for voice-to-chords, bass/drum extraction, `LIVE SILENT` cutoff, and multi-section arrangements (`ARRANGE`/`PERFORMANCE`).
* **Phase 4 (3:15–4:45 PM):** Optionally map Grove/ToF sensor gesture sweeps (`CC 74`) on branch `feat/kit-sensor`, with a instant fallback to `v1-rock-solid`.
* **Phase 5 (4:45–6:00 PM):** Mount CoreS3 on battery base, run 3-minute dry runs, and prepare cellular hotspot redundancy.

---

## 3. Science-Fair Showcase Pitch (3-Minute Flow — 6:00 PM)

1. **The Hook (0:00–0:30):** Introduce Lick-Lubin as a tactile hardware AI co-producer on M5Stack CoreS3 and OpenAI Realtime API that builds and arranges FL Studio beats hands-free.
2. **Live Judge Co-Production (0:30–1:45):** Hand over the controller for voice-driven loop creation at `101 BPM` in D Major.
3. **Song Structure (1:45–2:15):** Command automated arrangements (intro/verse/chorus) with auto-stem `.mid` exports.
4. **Live Rap Finale (2:15–3:00):** Engage `PERFORMANCE` mode to cut AI voice via `LIVE SILENT`, unmute the vocal mic (`CC 33`), and sweep filters live.
