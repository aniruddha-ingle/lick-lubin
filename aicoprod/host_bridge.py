#!/usr/bin/env python3
"""
Lick-Lubin AI Co-Producer Host Bridge (macOS)
Implements TDD-2026-1119-AICOPROD-v3 & BDD-2026-1119-AICOPROD-v2:
- 0xAA LTV Framing over non-blocking USB CDC Serial (921,600 baud, VID 0x303A)
- OpenAI Realtime API (gpt-realtime) with Rick Rubin Lick-Lubin persona & 7 tools
- Immediate response.cancel() & audio drop on LIVE SILENT / PERFORMANCE cutoff
- Drift-free 101.0 BPM CoreMIDI Sequencer (IAC Driver Bus 1) using time.perf_counter()
- D Major voicings, octave-down bass extraction, human-velocity FPC drums, .mid export
- Channel 16 Mixer CCs (Unity=102, Bass Ceiling=92, Mutes, Act III Vocal Unmute)
"""

import asyncio
import base64
import json
import logging
import os
import random
import signal
import struct
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Callable, Dict, List, Optional, Set, Tuple

import mido
import rtmidi
import serial.tools.list_ports
import serial_asyncio_fast
from dotenv import load_dotenv
from openai import AsyncOpenAI

load_dotenv()

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
)
log = logging.getLogger("lick-lubin")

# ---------------------------------------------------------------------------
# Protocol Constants (TDD Section 4.1)
# ---------------------------------------------------------------------------
SYNC_BYTE = 0xAA
MSG_MIC_PCM = 0x01
MSG_SPK_PCM = 0x02
MSG_JSON_UI = 0x03
MSG_HEARTBEAT = 0x04

MAX_PAYLOAD_LEN = 4096
SAMPLE_RATE = 24000
SERIAL_BAUD = 921600
ESPRESSIF_VID = 0x303A

# ---------------------------------------------------------------------------
# Musical & CoreMIDI Constants (TDD Sections 5 & 6)
# ---------------------------------------------------------------------------
BPM = 101.0
TICKS_PER_BEAT = 4  # 16th-note grid
SECONDS_PER_TICK = 60.0 / BPM / TICKS_PER_BEAT  # 0.1485148514851485s
PATTERN_TICKS = 64   # 4 bars * 16 ticks
SONG_TICKS = 256     # 16 bars * 16 ticks (38.02s)

MIDI_PORT_NAME = "IAC Driver Bus 1"
CH_PIANO = 0   # MIDI Channel 1
CH_BASS = 1    # MIDI Channel 2
CH_DRUMS = 2   # MIDI Channel 3
CH_MIXER = 15  # MIDI Channel 16

# FL Studio FPC Pad Note Numbers
FPC_KICK = 60        # C5
FPC_SNARE = 62       # D5
FPC_CLOSED_HAT = 66  # F#5
FPC_OPEN_HAT = 70    # A#5

# Channel 16 Mixer CCs
CC_VOL_MAP = {"PIANO": 20, "BASS": 21, "DRUMS": 22, "VOCAL": 23}
CC_MUTE_MAP = {"PIANO": 30, "BASS": 31, "DRUMS": 32, "VOCAL": 33}
CHANNEL_TO_MIDI_CH = {"PIANO": CH_PIANO, "BASS": CH_BASS, "DRUMS": CH_DRUMS}

UNITY_CC = 102
BASS_CEILING_CC = 92  # -4.5 dB ceiling on Bass 1
CC_ALL_NOTES_OFF = 123

# D Major Voicings (TDD Section 5.2)
CHORD_VOICINGS: Dict[str, List[Tuple[str, int, List[int]]]] = {
    "VERSE": [
        ("Dmaj9", 50, [50, 54, 57, 61, 64]),   # Root D3 = 50 -> Bass D2 = 38
        ("Bm7",   47, [47, 50, 54, 57, 61]),   # Root B2 = 47 -> Bass B1 = 35
        ("Gmaj7", 43, [43, 47, 50, 54, 59]),   # Root G2 = 43 -> Bass G1 = 31
        ("A6",    45, [45, 49, 52, 54, 57]),   # Root A2 = 45 -> Bass A1 = 33
    ],
    "CHORUS": [
        ("Em9",   52, [52, 55, 59, 62, 66]),   # Root E3 = 52 -> Bass E2 = 40
        ("A13",   45, [45, 49, 55, 59, 66]),   # Root A2 = 45 -> Bass A1 = 33
        ("Dmaj9", 50, [50, 54, 57, 61, 64]),   # Root D3 = 50 -> Bass D2 = 38
        ("B7#9",  47, [47, 51, 57, 62, 66]),   # Root B2 = 47 -> Bass B1 = 35
    ],
    "INTRO": [
        ("Dmaj9", 50, [50, 54, 57, 61, 64]),
        ("Gmaj7", 43, [43, 47, 50, 54, 59]),
        ("Dmaj9", 50, [50, 54, 57, 61, 64]),
        ("Gmaj7", 43, [43, 47, 50, 54, 59]),
    ],
}

# ---------------------------------------------------------------------------
# Lick-Lubin System Prompt (TDD Section 4.6)
# ---------------------------------------------------------------------------
LICK_LUBIN_SYSTEM_PROMPT = """You are LICK-LUBIN, a minimalist, zen-like AI music co-producer modeled after Rick Rubin's studio philosophy, paired with an M5Stack CoreS3 tactile hardware controller and FL Studio.

CORE MUSICAL LOCK:
- Key: D Major
- Tempo: 101.0 BPM (locked 16th-note grid)
- Arrangement: 3-Act Structure (Act I: PATTERN loop -> Act II: SONG 16-bar linear arrangement -> Act III: PERFORMANCE silent stage handoff)

PHILOSOPHY & VOICE:
- Speak sparsely, calmly, and with deep musical intentionality. Keep spoken replies concise (1-2 sentences max).
- Strip away clutter. Focus on pocket, space, groove, and emotional truth.
- Never mention technical jargon, MIDI numbers, CCs, or JSON in speech.
- When the user asks to create or adjust music, invoke the production tools immediately and confirm briefly.
- In PERFORMANCE mode, remain completely silent (speak_back=false) so the artist has full acoustic stage control.
"""

# ---------------------------------------------------------------------------
# OpenAI Realtime Tool Definitions (TDD Section 5)
# ---------------------------------------------------------------------------
REALTIME_TOOLS: List[Dict[str, Any]] = [
    {
        "type": "function",
        "name": "set_production_mode",
        "description": "Switch the 3-Act production mode between PATTERN (4-bar loop), SONG (16-bar arrangement), and PERFORMANCE (live silent stage handoff).",
        "parameters": {
            "type": "object",
            "properties": {
                "mode": {
                    "type": "string",
                    "enum": ["PATTERN", "SONG", "PERFORMANCE"],
                }
            },
            "required": ["mode"],
        },
    },
    {
        "type": "function",
        "name": "write_chord_progression",
        "description": "Write a D Major chord progression on Channel 1 (Piano) for a given section.",
        "parameters": {
            "type": "object",
            "properties": {
                "section": {
                    "type": "string",
                    "enum": ["INTRO", "VERSE", "CHORUS"],
                    "default": "VERSE",
                },
                "voicing_style": {
                    "type": "string",
                    "default": "neo_soul",
                },
            },
            "required": ["section"],
        },
    },
    {
        "type": "function",
        "name": "extract_bassline",
        "description": "Extract a monophonic octave-down bassline on Channel 2 (Bass 1) from the current chord roots.",
        "parameters": {
            "type": "object",
            "properties": {
                "groove": {
                    "type": "string",
                    "enum": ["root_pocket", "syncopated", "sustained"],
                    "default": "root_pocket",
                }
            },
        },
    },
    {
        "type": "function",
        "name": "write_drum_pattern",
        "description": "Write a human-velocity FL Studio FPC drum groove on Channel 3.",
        "parameters": {
            "type": "object",
            "properties": {
                "style": {
                    "type": "string",
                    "enum": ["boom_bap", "minimal_pocket", "half_time"],
                    "default": "boom_bap",
                },
                "swing": {
                    "type": "number",
                    "default": 0.15,
                },
            },
        },
    },
    {
        "type": "function",
        "name": "set_channel_gain",
        "description": "Adjust mixer fader gain (dB offset from Unity=102) or mute state on Channel 16.",
        "parameters": {
            "type": "object",
            "properties": {
                "channel": {
                    "type": "string",
                    "enum": ["PIANO", "BASS", "DRUMS", "VOCAL"],
                },
                "db_offset": {
                    "type": "number",
                    "default": 0.0,
                },
                "mute": {
                    "type": "boolean",
                    "default": False,
                },
            },
            "required": ["channel"],
        },
    },
    {
        "type": "function",
        "name": "arrange_sections",
        "description": "Arrange a 16-bar linear SONG timeline (4 bars Intro, 8 bars Verse, 4 bars Chorus).",
        "parameters": {
            "type": "object",
            "properties": {
                "structure": {
                    "type": "array",
                    "items": {"type": "string"},
                    "default": ["INTRO", "VERSE", "VERSE", "CHORUS"],
                }
            },
        },
    },
    {
        "type": "function",
        "name": "set_talkback",
        "description": "Enable or disable AI spoken talkback audio to the CoreS3 speaker.",
        "parameters": {
            "type": "object",
            "properties": {
                "enabled": {"type": "boolean"},
            },
            "required": ["enabled"],
        },
    },
]


# ---------------------------------------------------------------------------
# 0xAA LTV Binary Framing Codec (TDD Section 4.1)
# ---------------------------------------------------------------------------
def encode_ltv_frame(msg_type: int, payload: bytes = b"") -> bytes:
    if len(payload) > MAX_PAYLOAD_LEN:
        raise ValueError(f"Payload length {len(payload)} exceeds MAX_PAYLOAD_LEN ({MAX_PAYLOAD_LEN})")
    header = struct.pack(">BBH", SYNC_BYTE, msg_type & 0xFF, len(payload))
    return header + payload


class LtvStreamDecoder:
    """Deterministic byte-stream parser with immediate WAIT_SYNC recovery on errors."""

    WAIT_SYNC = 0
    READ_TYPE = 1
    READ_LEN_HI = 2
    READ_LEN_LO = 3
    READ_PAYLOAD = 4

    def __init__(self) -> None:
        self.state: int = self.WAIT_SYNC
        self.msg_type: int = 0
        self.expected_len: int = 0
        self.payload_buf: bytearray = bytearray()
        self.error_count: int = 0

    @property
    def _payload(self) -> bytearray:
        return self.payload_buf

    def reset(self) -> None:
        self.state = self.WAIT_SYNC
        self.msg_type = 0
        self.expected_len = 0
        self.payload_buf.clear()

    def feed(self, data: bytes) -> List[Tuple[int, bytes]]:
        frames: List[Tuple[int, bytes]] = []
        for byte in data:
            if self.state == self.WAIT_SYNC:
                if byte == SYNC_BYTE:
                    self.state = self.READ_TYPE
            elif self.state == self.READ_TYPE:
                if byte in (MSG_MIC_PCM, MSG_SPK_PCM, MSG_JSON_UI, MSG_HEARTBEAT):
                    self.msg_type = byte
                    self.state = self.READ_LEN_HI
                else:
                    self.error_count += 1
                    self.reset()
                    if byte == SYNC_BYTE:
                        self.state = self.READ_TYPE
            elif self.state == self.READ_LEN_HI:
                self.expected_len = byte << 8
                self.state = self.READ_LEN_LO
            elif self.state == self.READ_LEN_LO:
                self.expected_len |= byte
                if self.expected_len > MAX_PAYLOAD_LEN:
                    self.error_count += 1
                    self.reset()
                elif self.expected_len == 0:
                    frames.append((self.msg_type, b""))
                    self.reset()
                else:
                    self.payload_buf.clear()
                    self.state = self.READ_PAYLOAD
            elif self.state == self.READ_PAYLOAD:
                self.payload_buf.append(byte)
                if len(self.payload_buf) >= self.expected_len:
                    frames.append((self.msg_type, bytes(self.payload_buf)))
                    self.reset()
        return frames


# ---------------------------------------------------------------------------
# Serial Port Auto-Discovery (Espressif VID 0x303A)
# ---------------------------------------------------------------------------
def find_cores3_port() -> Optional[str]:
    override = os.environ.get("CORES3_PORT")
    if override:
        return override
    for port in serial.tools.list_ports.comports():
        if port.vid == ESPRESSIF_VID:
            return port.device
    return None


# ---------------------------------------------------------------------------
# Bridge State & Sequencer Data Structures
# ---------------------------------------------------------------------------
@dataclass
class BridgeState:
    mode: str = "PATTERN"          # PATTERN | SONG | PERFORMANCE
    speak_back: bool = True
    ptt_active: bool = False
    is_playing: bool = False
    tick_index: int = 0
    active_section: str = "VERSE"
    chord_progression: List[Tuple[str, int, List[int]]] = field(default_factory=list)
    bass_notes: List[int] = field(default_factory=list)
    drum_events: Dict[int, List[Tuple[int, int]]] = field(default_factory=dict)
    song_sections: List[str] = field(default_factory=lambda: ["INTRO", "VERSE", "VERSE", "CHORUS"])
    channel_gains: Dict[str, int] = field(
        default_factory=lambda: {"PIANO": UNITY_CC, "BASS": BASS_CEILING_CC, "DRUMS": UNITY_CC, "VOCAL": UNITY_CC}
    )
    channel_mutes: Dict[str, bool] = field(
        default_factory=lambda: {"PIANO": False, "BASS": False, "DRUMS": False, "VOCAL": False}
    )


# ---------------------------------------------------------------------------
# Lick-Lubin Host Bridge
# ---------------------------------------------------------------------------
class LickLubinBridge:
    def __init__(
        self,
        midi_out: Optional[Any] = None,
        ai_drops_dir: Optional[Path] = None,
        drops_dir: Optional[Path] = None,
        on_tick: Optional[Callable[[int, float], None]] = None,
    ) -> None:
        self.state = BridgeState()
        self.decoder = LtvStreamDecoder()
        self.writer: Optional[asyncio.StreamWriter] = None
        self.reader: Optional[asyncio.StreamReader] = None
        self.conn: Optional[Any] = None

        target_drops = drops_dir or ai_drops_dir or (Path(__file__).resolve().parent.parent / "AI_Drops")
        self.ai_drops_dir = Path(target_drops)
        self.drops_dir = self.ai_drops_dir
        self.ai_drops_dir.mkdir(parents=True, exist_ok=True)

        self.on_tick = on_tick

        # CoreMIDI setup
        if midi_out is not None:
            self.midi_out = midi_out
        else:
            self.midi_out = rtmidi.MidiOut()
            self._open_iac_port()

        # Active note tracking per MIDI channel (0..15) for guaranteed flush
        self.active_pitches: Dict[int, Set[int]] = {ch: set() for ch in range(16)}

        # Realtime response tracking for immediate cutoff
        self.active_response_id: Optional[str] = None
        self.canceled_response_ids: Set[str] = set()
        self.audio_buffer_bytes: int = 0

        # Background tasks
        self._tasks: List[asyncio.Task] = []
        self._running = False
        self._rng = random.Random(101)  # Deterministic humanization seed

    def _open_iac_port(self) -> None:
        ports = self.midi_out.get_ports()
        for idx, name in enumerate(ports):
            if MIDI_PORT_NAME in name:
                self.midi_out.open_port(idx)
                log.info("Opened CoreMIDI port: %s", name)
                return
        log.warning("'%s' not found in ports %s; opening virtual port", MIDI_PORT_NAME, ports)
        self.midi_out.open_virtual_port(MIDI_PORT_NAME)

    def attach_serial(self, reader: asyncio.StreamReader, writer: asyncio.StreamWriter) -> None:
        self.reader = reader
        self.writer = writer

    def attach_realtime(self, conn: Any) -> None:
        self.conn = conn

    # -----------------------------------------------------------------------
    # Serial TX Helpers
    # -----------------------------------------------------------------------
    def send_serial_frame(self, msg_type: int, payload: bytes = b"") -> None:
        if self.writer is None:
            return
        frame = encode_ltv_frame(msg_type, payload)
        self.writer.write(frame)

    def send_ui_json(self, obj: Dict[str, Any]) -> None:
        raw = json.dumps(obj, separators=(",", ":")).encode("utf-8")
        self.send_serial_frame(MSG_JSON_UI, raw)

    # -----------------------------------------------------------------------
    # MIDI TX & Stuck-Note Prevention (CC 123 + note_off)
    # -----------------------------------------------------------------------
    def _send_midi(self, msg: List[int]) -> None:
        if self.midi_out is not None:
            self.midi_out.send_message(msg)

    def note_on(self, channel: int, note: int, velocity: int) -> None:
        note = max(0, min(127, int(note)))
        velocity = max(1, min(127, int(velocity)))
        self._send_midi([0x90 | (channel & 0x0F), note, velocity])
        self.active_pitches[channel].add(note)

    def note_off(self, channel: int, note: int) -> None:
        note = max(0, min(127, int(note)))
        self._send_midi([0x80 | (channel & 0x0F), note, 0])
        self.active_pitches[channel].discard(note)

    def flush_channel(self, channel: int) -> None:
        """Emit explicit note_off for every active pitch plus CC 123 (All Notes Off)."""
        for pitch in sorted(self.active_pitches[channel]):
            self._send_midi([0x80 | (channel & 0x0F), pitch, 0])
        self.active_pitches[channel].clear()
        self._send_midi([0xB0 | (channel & 0x0F), CC_ALL_NOTES_OFF, 0])

    def flush_all_instrument_channels(self) -> None:
        for ch in (CH_PIANO, CH_BASS, CH_DRUMS):
            self.flush_channel(ch)

    def init_mixer_unity(self) -> None:
        for ch_name, cc_num in CC_VOL_MAP.items():
            val = BASS_CEILING_CC if ch_name == "BASS" else UNITY_CC
            self.state.channel_gains[ch_name] = val
            self._send_midi([0xB0 | CH_MIXER, cc_num, val])
        for ch_name, cc_num in CC_MUTE_MAP.items():
            self.state.channel_mutes[ch_name] = False
            self._send_midi([0xB0 | CH_MIXER, cc_num, 127])

    # -----------------------------------------------------------------------
    # Immediate Acoustic & Realtime Response Cutoff
    # -----------------------------------------------------------------------
    async def enforce_silent_cutoff(self, mode_override: Optional[str] = None) -> None:
        self.state.speak_back = False
        if self.active_response_id:
            self.canceled_response_ids.add(self.active_response_id)
        if self.conn is not None:
            try:
                await self.conn.response.cancel()
            except Exception as exc:
                log.debug("response.cancel() ignored or no active response: %s", exc)
        payload: Dict[str, Any] = {"speak_back": False}
        if mode_override is not None:
            payload["mode"] = mode_override
        self.send_ui_json(payload)

    # -----------------------------------------------------------------------
    # Lick-Lubin Tool Implementations (TDD Section 5)
    # -----------------------------------------------------------------------
    async def set_production_mode(self, mode: str) -> Dict[str, Any]:
        mode = mode.upper()
        if mode not in ("PATTERN", "SONG", "PERFORMANCE"):
            raise ValueError(f"Invalid production mode: {mode}")

        self.flush_all_instrument_channels()
        self.state.mode = mode
        self.state.tick_index = 0

        if mode == "PERFORMANCE":
            # Act III atomic transition: silent speak_back, reset tick 0, unmute Vocal (CC 33 = 127)
            self.state.is_playing = False
            self.state.channel_mutes["VOCAL"] = False
            self._send_midi([0xB0 | CH_MIXER, CC_MUTE_MAP["VOCAL"], 127])
            await self.enforce_silent_cutoff(mode_override="PERFORMANCE")
        else:
            self.state.is_playing = True
            self.send_ui_json({"mode": mode, "speak_back": self.state.speak_back})

        return {"status": "ok", "mode": self.state.mode, "speak_back": self.state.speak_back, "tick": self.state.tick_index}

    def write_chord_progression(self, section: str = "VERSE", voicing_style: str = "neo_soul") -> Dict[str, Any]:
        sec = section.upper()
        if sec not in CHORD_VOICINGS:
            sec = "VERSE"

        # Flush existing notes before overwriting pattern (Scenario 3.3)
        self.flush_channel(CH_PIANO)

        self.state.active_section = sec
        self.state.chord_progression = list(CHORD_VOICINGS[sec])
        self.state.is_playing = True
        self._export_midi_drop(f"chords_{sec.lower()}")

        chord_names = [c[0] for c in self.state.chord_progression]
        return {"status": "ok", "section": sec, "chords": chord_names}

    def extract_bassline(self, groove: str = "root_pocket") -> Dict[str, Any]:
        if not self.state.chord_progression:
            self.state.chord_progression = list(CHORD_VOICINGS[self.state.active_section])

        self.flush_channel(CH_BASS)
        # 1 octave (-12 semitones) below each chord root
        self.state.bass_notes = [root - 12 for (_, root, _) in self.state.chord_progression]
        self.state.is_playing = True
        self._export_midi_drop(f"bass_{self.state.active_section.lower()}")

        return {
            "status": "ok",
            "groove": groove,
            "notes": list(self.state.bass_notes),
            "bass_notes": list(self.state.bass_notes),
        }

    def write_drum_pattern(self, style: str = "boom_bap", swing: float = 0.15) -> Dict[str, Any]:
        self.flush_channel(CH_DRUMS)
        events: Dict[int, List[Tuple[int, int]]] = {t: [] for t in range(16)}

        # 16-tick (1-bar) pattern with human velocity variation
        kick_ticks = [0, 7, 10] if style == "boom_bap" else [0, 8]
        snare_ticks = [4, 12]
        open_hat_ticks = [14]

        for t in range(16):
            if t in kick_ticks:
                vel = self._rng.randint(96, 114)
                events[t].append((FPC_KICK, vel))
            if t in snare_ticks:
                vel = self._rng.randint(100, 118)
                events[t].append((FPC_SNARE, vel))
            if t in open_hat_ticks:
                vel = self._rng.randint(82, 98)
                events[t].append((FPC_OPEN_HAT, vel))
            elif t % 2 == 0:
                vel = self._rng.randint(68, 94)
                events[t].append((FPC_CLOSED_HAT, vel))

        self.state.drum_events = events
        self.state.is_playing = True
        self._export_midi_drop(f"drums_{style}")

        return {"status": "ok", "style": style, "ticks": 16}

    def set_channel_gain(self, channel: str, db_offset: float = 0.0, mute: Optional[bool] = None) -> Dict[str, Any]:
        ch = channel.upper()
        if ch not in CC_VOL_MAP:
            raise ValueError(f"Unknown channel: {channel}")

        # ~1.5 CC steps per 0.5 dB -> ~3 steps per dB around Unity=102
        raw_cc = int(round(UNITY_CC + (db_offset * 3.0)))
        raw_cc = max(0, min(127, raw_cc))
        if ch == "BASS":
            raw_cc = min(BASS_CEILING_CC, raw_cc)

        self.state.channel_gains[ch] = raw_cc
        self._send_midi([0xB0 | CH_MIXER, CC_VOL_MAP[ch], raw_cc])

        if mute is not None:
            self.state.channel_mutes[ch] = bool(mute)
            mute_val = 0 if mute else 127
            self._send_midi([0xB0 | CH_MIXER, CC_MUTE_MAP[ch], mute_val])
            if mute and ch in CHANNEL_TO_MIDI_CH:
                self.flush_channel(CHANNEL_TO_MIDI_CH[ch])

        return {
            "status": "ok",
            "channel": ch,
            "cc_gain": raw_cc,
            "cc_value": raw_cc,
            "muted": self.state.channel_mutes[ch],
        }

    def arrange_sections(self, structure: Optional[List[str]] = None) -> Dict[str, Any]:
        sections = [s.upper() for s in (structure or ["INTRO", "VERSE", "VERSE", "CHORUS"])]
        self.state.song_sections = sections
        return {"status": "ok", "structure": sections, "bars": len(sections) * 4}

    async def set_talkback(self, enabled: bool) -> Dict[str, Any]:
        if not enabled:
            await self.enforce_silent_cutoff()
        else:
            self.state.speak_back = True
            self.send_ui_json({"speak_back": True})
        return {"status": "ok", "speak_back": self.state.speak_back}

    async def dispatch_tool_call(self, name: str, args: Dict[str, Any]) -> Dict[str, Any]:
        if name == "set_production_mode":
            return await self.set_production_mode(args["mode"])
        if name == "write_chord_progression":
            return self.write_chord_progression(args.get("section", "VERSE"), args.get("voicing_style", "neo_soul"))
        if name == "extract_bassline":
            return self.extract_bassline(args.get("groove", "root_pocket"))
        if name == "write_drum_pattern":
            return self.write_drum_pattern(args.get("style", "boom_bap"), args.get("swing", 0.15))
        if name == "set_channel_gain":
            return self.set_channel_gain(args["channel"], args.get("db_offset", 0.0), args.get("mute"))
        if name == "arrange_sections":
            return self.arrange_sections(args.get("structure"))
        if name == "set_talkback":
            return await self.set_talkback(bool(args["enabled"]))
        return {"status": "error", "message": f"Unknown tool: {name}"}

    # -----------------------------------------------------------------------
    # .mid File Export to ../AI_Drops/
    # -----------------------------------------------------------------------
    def _export_midi_drop(self, prefix: str) -> Path:
        mid = mido.MidiFile(ticks_per_beat=480)
        track = mido.MidiTrack()
        mid.tracks.append(track)
        track.append(mido.MetaMessage("set_tempo", tempo=mido.bpm2tempo(BPM), time=0))

        if self.state.chord_progression:
            for _, _, notes in self.state.chord_progression:
                for i, n in enumerate(notes):
                    track.append(mido.Message("note_on", channel=CH_PIANO, note=n, velocity=84, time=0))
                for i, n in enumerate(notes):
                    track.append(mido.Message("note_off", channel=CH_PIANO, note=n, velocity=0, time=1920 if i == 0 else 0))

        out_path = self.ai_drops_dir / f"{prefix}_{int(BPM)}bpm.mid"
        mid.save(str(out_path))
        return out_path

    # -----------------------------------------------------------------------
    # Drift-Free 101.0 BPM Sequencer Engine (TDD Section 6)
    # -----------------------------------------------------------------------
    def process_tick(self, tick: Optional[int] = None) -> None:
        if tick is None:
            tick = self.state.tick_index
        else:
            self.state.tick_index = tick

        if not self.state.is_playing:
            return

        # Linear SONG mode auto-stop at Bar 16 / Tick 256 (38.02s)
        if self.state.mode == "SONG" and tick >= SONG_TICKS:
            self.flush_all_instrument_channels()
            self.state.is_playing = False
            self.state.tick_index = 0
            return

        bar_in_loop = (tick // 16) % 4
        tick_in_bar = tick % 16

        # Select active section for SONG mode (4 bars per section block)
        if self.state.mode == "SONG" and self.state.song_sections:
            sec_idx = min((tick // 64), len(self.state.song_sections) - 1)
            sec_name = self.state.song_sections[sec_idx]
            chords = CHORD_VOICINGS.get(sec_name, self.state.chord_progression)
            bass = [r - 12 for (_, r, _) in chords]
        else:
            chords = self.state.chord_progression
            bass = self.state.bass_notes

        # Channel 1: Chords (trigger on beat 1 of each bar, release at tick 15)
        if chords and not self.state.channel_mutes["PIANO"]:
            _, _, notes = chords[bar_in_loop % len(chords)]
            if tick_in_bar == 0:
                self.flush_channel(CH_PIANO)
                for n in notes:
                    self.note_on(CH_PIANO, n, 84)
            elif tick_in_bar == 15:
                for n in notes:
                    self.note_off(CH_PIANO, n)

        # Channel 2: Octave-down Bassline
        if bass and not self.state.channel_mutes["BASS"]:
            b_note = bass[bar_in_loop % len(bass)]
            if tick_in_bar in (0, 6, 10):
                self.flush_channel(CH_BASS)
                self.note_on(CH_BASS, b_note, 96)
            elif tick_in_bar in (5, 9, 15):
                self.note_off(CH_BASS, b_note)

        # Channel 3: Human-Velocity FPC Drums
        if self.state.drum_events and not self.state.channel_mutes["DRUMS"]:
            # Release previous 16th-note drum hits
            for active_drum in list(self.active_pitches[CH_DRUMS]):
                self.note_off(CH_DRUMS, active_drum)
            for drum_note, vel in self.state.drum_events.get(tick_in_bar, []):
                self.note_on(CH_DRUMS, drum_note, vel)

        # Advance tick counter with mode-specific wrap/stop
        next_tick = tick + 1
        if self.state.mode == "PATTERN":
            self.state.tick_index = next_tick % PATTERN_TICKS
        elif self.state.mode == "SONG":
            if next_tick >= SONG_TICKS:
                self.flush_all_instrument_channels()
                self.state.is_playing = False
                self.state.tick_index = 0
            else:
                self.state.tick_index = next_tick
        else:
            self.state.tick_index = 0

    def step_sequencer_tick(self, tick: Optional[int] = None) -> None:
        """Explicit single-tick step helper for deterministic testing & manual advance."""
        if not self.state.is_playing:
            self.state.is_playing = True
        self.process_tick(tick)

    async def sequencer_loop(self, max_ticks: Optional[int] = None) -> List[float]:
        """Drift-free tick scheduler anchored strictly to time.perf_counter()."""
        timestamps: List[float] = []
        start_time = time.perf_counter()
        step = 0

        while self._running or max_ticks is not None:
            if max_ticks is not None and step >= max_ticks:
                break

            target_time = start_time + (step * SECONDS_PER_TICK)
            now = time.perf_counter()
            sleep_dur = target_time - now
            if sleep_dur > 0:
                await asyncio.sleep(sleep_dur)

            actual_now = time.perf_counter()
            timestamps.append(actual_now)
            if self.on_tick is not None:
                self.on_tick(self.state.tick_index, actual_now)

            self.process_tick(self.state.tick_index)
            step += 1

        return timestamps

    # -----------------------------------------------------------------------
    # Serial Uplink & Realtime Event Handlers
    # -----------------------------------------------------------------------
    async def handle_serial_bytes(self, raw_bytes: bytes) -> None:
        frames = self.decoder.feed(raw_bytes)
        for msg_type, payload in frames:
            await self.handle_serial_frame(msg_type, payload)

    async def handle_serial_frame(self, msg_type: int, payload: bytes) -> None:
        if msg_type == MSG_MIC_PCM:
            if self.state.ptt_active and self.conn is not None:
                self.audio_buffer_bytes += len(payload)
                b64_audio = base64.b64encode(payload).decode("ascii")
                await self.conn.input_audio_buffer.append(audio=b64_audio)

        elif msg_type == MSG_JSON_UI:
            try:
                data = json.loads(payload.decode("utf-8"))
            except (UnicodeDecodeError, json.JSONDecodeError) as exc:
                log.warning("Malformed JSON frame ignored: %s", exc)
                return

            event = data.get("event")

            if event == "ptt_start" or (event is None and data.get("ptt") is True):
                self.state.ptt_active = True
                if "speak_back" in data:
                    self.state.speak_back = bool(data["speak_back"])

            elif event == "ptt_release" or (event is None and data.get("ptt") is False):
                self.state.ptt_active = False
                if self.conn is not None:
                    await self.conn.input_audio_buffer.commit()
                    await self.conn.response.create()

            elif event == "toggle_silent" or (event is None and "speak_back" in data):
                new_speak_back = bool(data.get("speak_back", not self.state.speak_back))
                if not new_speak_back:
                    await self.enforce_silent_cutoff()
                else:
                    self.state.speak_back = True
                    self.send_ui_json({"speak_back": True})

        elif msg_type == MSG_HEARTBEAT:
            self.send_serial_frame(MSG_HEARTBEAT, b"")

    async def handle_realtime_event(self, event: Any) -> None:
        ev_type = getattr(event, "type", None) or (event.get("type") if isinstance(event, dict) else None)

        if ev_type == "response.created":
            resp = getattr(event, "response", None) or (event.get("response") if isinstance(event, dict) else None)
            resp_id = getattr(resp, "id", None) or (resp.get("id") if isinstance(resp, dict) else None)
            if resp_id:
                self.active_response_id = resp_id

        elif ev_type in ("response.audio.delta", "response.output_audio.delta"):
            resp_id = getattr(event, "response_id", None) or (event.get("response_id") if isinstance(event, dict) else None)
            if resp_id:
                self.active_response_id = resp_id

            # Drop audio immediately if speak_back is muted or response was canceled
            if not self.state.speak_back:
                return
            if resp_id and resp_id in self.canceled_response_ids:
                return

            delta_b64 = getattr(event, "delta", None) or (event.get("delta") if isinstance(event, dict) else "")
            if delta_b64:
                pcm_bytes = base64.b64decode(delta_b64)
                for offset in range(0, len(pcm_bytes), MAX_PAYLOAD_LEN):
                    chunk = pcm_bytes[offset : offset + MAX_PAYLOAD_LEN]
                    self.send_serial_frame(MSG_SPK_PCM, chunk)

        elif ev_type == "response.done":
            resp = getattr(event, "response", None) or (event.get("response") if isinstance(event, dict) else None)
            resp_id = getattr(resp, "id", None) or (resp.get("id") if isinstance(resp, dict) else None)
            if resp_id and resp_id == self.active_response_id:
                self.active_response_id = None

        elif ev_type == "response.function_call_arguments.done":
            call_id = getattr(event, "call_id", None) or (event.get("call_id") if isinstance(event, dict) else "")
            name = getattr(event, "name", None) or (event.get("name") if isinstance(event, dict) else "")
            raw_args = getattr(event, "arguments", None) or (event.get("arguments") if isinstance(event, dict) else "{}")
            args = json.loads(raw_args) if isinstance(raw_args, str) else raw_args
            result = await self.dispatch_tool_call(name, args)
            if self.conn is not None:
                await self.conn.conversation.item.create(
                    item={
                        "type": "function_call_output",
                        "call_id": call_id,
                        "output": json.dumps(result),
                    }
                )
                await self.conn.response.create()

    # -----------------------------------------------------------------------
    # Background Loops & Lifecycle
    # -----------------------------------------------------------------------
    async def serial_read_loop(self) -> None:
        assert self.reader is not None
        while self._running:
            chunk = await self.reader.read(4096)
            if not chunk:
                break
            await self.handle_serial_bytes(chunk)

    async def heartbeat_loop(self) -> None:
        while self._running:
            self.send_serial_frame(MSG_HEARTBEAT, b"")
            await asyncio.sleep(1.0)

    async def realtime_read_loop(self) -> None:
        assert self.conn is not None
        async for event in self.conn:
            if not self._running:
                break
            await self.handle_realtime_event(event)

    async def start_background_tasks(self) -> None:
        self._running = True
        self._tasks = [asyncio.create_task(self.sequencer_loop())]
        if self.reader is not None:
            self._tasks.append(asyncio.create_task(self.serial_read_loop()))
        if self.writer is not None:
            self._tasks.append(asyncio.create_task(self.heartbeat_loop()))
        if self.conn is not None:
            self._tasks.append(asyncio.create_task(self.realtime_read_loop()))

    async def shutdown(self) -> None:
        self._running = False
        for task in self._tasks:
            task.cancel()
        if self._tasks:
            await asyncio.gather(*self._tasks, return_exceptions=True)
        self._tasks.clear()

        # Guaranteed stuck-note flush across Channels 1-3 before port closure
        self.flush_all_instrument_channels()

        if self.midi_out is not None:
            try:
                self.midi_out.close_port()
            except Exception:
                pass

        if self.writer is not None:
            try:
                self.writer.close()
                await self.writer.wait_closed()
            except Exception:
                pass


async def main() -> None:
    port = find_cores3_port()
    if not port:
        raise RuntimeError("M5Stack CoreS3 (VID 0x303A) not found. Set CORES3_PORT or connect USB-C.")

    bridge = LickLubinBridge()
    reader, writer = await serial_asyncio_fast.open_serial_connection(url=port, baudrate=SERIAL_BAUD)
    bridge.attach_serial(reader, writer)
    bridge.init_mixer_unity()

    loop = asyncio.get_running_loop()
    stop_event = asyncio.Event()

    for sig in (signal.SIGINT, signal.SIGTERM):
        loop.add_signal_handler(sig, stop_event.set)

    client = AsyncOpenAI()
    async with client.realtime.connect(model="gpt-realtime") as conn:
        bridge.attach_realtime(conn)
        await conn.session.update(
            session={
                "instructions": LICK_LUBIN_SYSTEM_PROMPT,
                "voice": "ash",
                "input_audio_format": "pcm16",
                "output_audio_format": "pcm16",
                "turn_detection": None,
                "tools": REALTIME_TOOLS,
            }
        )
        await bridge.start_background_tasks()
        log.info("Lick-Lubin Bridge online at %.1f BPM (D Major)", BPM)
        await stop_event.wait()

    await bridge.shutdown()


if __name__ == "__main__":
    asyncio.run(main())
