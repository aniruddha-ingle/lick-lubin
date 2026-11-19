import json
import pytest

from host_bridge import (
    BASS_CEILING_CC,
    CH_BASS,
    CH_DRUMS,
    CH_MIXER,
    CH_PIANO,
    MSG_JSON_UI,
    UNITY_CC,
)


def test_scenario_3_1_bass_ceiling_clamped_at_92(bridge, fake_midi) -> None:
    """Scenario 3.1: BASS gain at 0.0 dB offset clamps to CC 92 (-4.5 dB ceiling) while Unity is 102."""
    assert UNITY_CC == 102
    assert BASS_CEILING_CC == 92

    res = bridge.set_channel_gain(channel="BASS", db_offset=0.0)
    cc_val = res.get("cc_gain", res.get("cc_value", res.get("value")))
    assert cc_val == 92

    raw_midi = fake_midi.raw_bytes()
    assert [0xB0 | CH_MIXER, 21, 92] in raw_midi


def test_scenario_3_2_channel_mute_emits_cc31_0_and_cc123(bridge, fake_midi) -> None:
    """Scenario 3.2: Muting BASS emits CC 31 = 0 on Channel 16 and CC 123 (All Notes Off) on Channel 2."""
    bridge.active_pitches[CH_BASS].add(38)
    fake_midi.clear()

    bridge.set_channel_gain(channel="BASS", mute=True)

    raw_midi = fake_midi.raw_bytes()
    assert [0xB0 | CH_MIXER, 31, 0] in raw_midi
    assert [0xB0 | CH_BASS, 123, 0] in raw_midi
    assert len(bridge.active_pitches[CH_BASS]) == 0


def test_scenario_3_3_pattern_overwrite_flushes_notes_before_new_note_ons(bridge, fake_midi) -> None:
    """Scenario 3.3: Overwriting an active chord pattern emits note_off + CC 123 before new note_on events."""
    bridge.write_chord_progression(section="VERSE")
    bridge.process_tick(0)
    fake_midi.clear()

    bridge.write_chord_progression(section="CHORUS")
    bridge.process_tick(0)

    raw_midi = fake_midi.raw_bytes()
    cc123_idx = raw_midi.index([0xB0 | CH_PIANO, 123, 0])
    note_off_indices = [i for i, m in enumerate(raw_midi) if (m[0] & 0xF0) == 0x80 and (m[0] & 0x0F) == CH_PIANO]
    note_on_indices = [i for i, m in enumerate(raw_midi) if (m[0] & 0xF0) == 0x90 and (m[0] & 0x0F) == CH_PIANO]

    assert len(note_off_indices) > 0
    assert len(note_on_indices) > 0
    assert max(note_off_indices) < min(note_on_indices)
    assert cc123_idx < min(note_on_indices)


def test_scenario_4_1_pattern_mode_wraps_tick_63_to_0(bridge) -> None:
    """Scenario 4.1: PATTERN mode wraps 4-bar loop from tick 63 back to tick 0."""
    bridge.state.mode = "PATTERN"
    bridge.state.is_playing = True
    bridge.state.tick_index = 63

    bridge.step_sequencer_tick()
    assert bridge.state.tick_index == 0
    assert bridge.state.is_playing is True


@pytest.mark.asyncio
async def test_scenario_4_2_performance_mode_atomic_cutoff_and_vocal_unmute(bridge, fake_midi, fake_serial) -> None:
    """Scenario 4.2: PERFORMANCE mode sets speak_back=False, resets tick=0, sends JSON, and unmutes Vocal CC 33=127."""
    bridge.state.tick_index = 42
    bridge.state.speak_back = True

    await bridge.set_production_mode(mode="PERFORMANCE")

    assert bridge.state.mode == "PERFORMANCE"
    assert bridge.state.speak_back is False
    assert bridge.state.tick_index == 0

    raw_midi = fake_midi.raw_bytes()
    assert [0xB0 | CH_MIXER, 33, 127] in raw_midi

    frames = fake_serial.writer.sent_frames()
    json_frames = [json.loads(p.decode("utf-8")) for t, p in frames if t == MSG_JSON_UI]
    assert any(f.get("mode") == "PERFORMANCE" and f.get("speak_back") is False for f in json_frames)


def test_scenario_4_3_song_mode_auto_stops_at_tick_256_bar_16(bridge, fake_midi) -> None:
    """Scenario 4.3: SONG mode stops automatically at tick 256 (Bar 16) and flushes CC 123 on Channels 1-3."""
    bridge.state.mode = "SONG"
    bridge.state.is_playing = True
    bridge.state.tick_index = 255
    bridge.active_pitches[CH_PIANO].add(50)
    bridge.active_pitches[CH_BASS].add(38)
    bridge.active_pitches[CH_DRUMS].add(60)
    fake_midi.clear()

    bridge.step_sequencer_tick()

    assert bridge.state.is_playing is False
    assert bridge.state.tick_index in (0, 256)
    raw_midi = fake_midi.raw_bytes()
    assert [0xB0 | CH_PIANO, 123, 0] in raw_midi
    assert [0xB0 | CH_BASS, 123, 0] in raw_midi
    assert [0xB0 | CH_DRUMS, 123, 0] in raw_midi
