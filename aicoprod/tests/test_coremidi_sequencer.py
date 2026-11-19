import time
import pytest
from host_bridge import (
    BPM,
    SECONDS_PER_TICK,
    CH_PIANO,
    CH_BASS,
    CH_DRUMS,
    FPC_KICK,
    FPC_SNARE,
    FPC_CLOSED_HAT,
    FPC_OPEN_HAT,
)


@pytest.mark.asyncio
async def test_scenario_2_1_101bpm_perf_counter_drift_under_10ms(bridge) -> None:
    """Scenario 2.1: 16-tick bar at 101.0 BPM maintains < 10 ms cumulative drift."""
    assert BPM == 101.0
    assert abs(SECONDS_PER_TICK - 0.1485148515) < 1e-7

    tick_timestamps = []
    bridge.on_tick = lambda tick, ts: tick_timestamps.append(ts)
    bridge.state.is_playing = True

    await bridge.sequencer_loop(max_ticks=16)

    assert len(tick_timestamps) >= 16
    elapsed = tick_timestamps[15] - tick_timestamps[0]
    expected = 15 * SECONDS_PER_TICK  # 2.22772277s across 16 ticks
    drift_ms = abs(elapsed - expected) * 1000.0
    assert drift_ms < 10.0, f"Cumulative sequencer drift {drift_ms:.2f} ms exceeded 10 ms"


def test_scenario_2_2_d_major_verse_chords_and_octave_down_bass(bridge, fake_midi) -> None:
    """Scenario 2.2: D Major Verse voicings & 1-octave-down bass extraction."""
    chord_res = bridge.write_chord_progression(section="VERSE")
    assert chord_res["chords"] == ["Dmaj9", "Bm7", "Gmaj7", "A6"]

    bass_res = bridge.extract_bassline()
    assert bass_res["notes"] == [38, 35, 31, 33]  # D2=38, B1=35, G1=31, A1=33

    # Step bar 1 tick 0 and verify Channel 1 (0x90) and Channel 2 (0x91) note_on events
    fake_midi.clear()
    bridge.step_sequencer_tick(0)

    raw = fake_midi.raw_bytes()
    ch1_notes = [msg[1] for msg in raw if msg[0] == (0x90 | CH_PIANO)]
    ch2_notes = [msg[1] for msg in raw if msg[0] == (0x90 | CH_BASS)]

    assert ch1_notes == [50, 54, 57, 61, 64]
    assert ch2_notes == [38]
    assert ch2_notes[0] == ch1_notes[0] - 12


def test_scenario_2_3_fpc_drum_mapping_and_human_velocity(bridge, fake_midi) -> None:
    """Scenario 2.3: FL Studio FPC pad mapping (60, 62, 66, 70) & human velocity variation."""
    bridge.write_drum_pattern(style="boom_bap")
    fake_midi.clear()

    for t in range(16):
        bridge.step_sequencer_tick(t)

    drum_note_ons = [msg for msg in fake_midi.raw_bytes() if msg[0] == (0x90 | CH_DRUMS)]
    emitted_notes = {msg[1] for msg in drum_note_ons}
    emitted_velocities = [msg[2] for msg in drum_note_ons]

    assert emitted_notes == {FPC_KICK, FPC_SNARE, FPC_CLOSED_HAT, FPC_OPEN_HAT}
    assert min(emitted_velocities) != max(emitted_velocities)
