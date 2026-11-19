import base64
import json
from types import SimpleNamespace
import pytest

from host_bridge import (
    CH_BASS,
    CH_DRUMS,
    CH_PIANO,
    MSG_JSON_UI,
    MSG_SPK_PCM,
    encode_ltv_frame,
)


@pytest.mark.asyncio
async def test_scenario_5_1_toggle_silent_false_cancels_response_and_drops_audio(
    bridge, fake_serial, fake_realtime
) -> None:
    """Scenario 5.1: toggle_silent(false) immediately awaits response.cancel() and drops subsequent audio."""
    bridge.state.speak_back = True

    await bridge.handle_realtime_event(
        SimpleNamespace(type="response.created", response=SimpleNamespace(id="resp_001"))
    )

    pcm_chunk = b"\x01\x02" * 240
    b64_chunk = base64.b64encode(pcm_chunk).decode("ascii")

    # First delta while speak_back=True reaches serial
    await bridge.handle_realtime_event(
        SimpleNamespace(type="response.audio.delta", response_id="resp_001", delta=b64_chunk)
    )
    frames_before = [t for t, _ in fake_serial.writer.sent_frames() if t == MSG_SPK_PCM]
    assert len(frames_before) == 1

    # CoreS3 taps [LIVE SILENT] -> toggle_silent(false)
    mute_payload = json.dumps({"event": "toggle_silent", "speak_back": False}).encode("utf-8")
    await bridge.handle_serial_bytes(encode_ltv_frame(MSG_JSON_UI, mute_payload))

    assert bridge.state.speak_back is False
    assert fake_realtime.response.cancel_calls == 1
    canceled = getattr(bridge.state, "canceled_response_ids", getattr(bridge, "canceled_response_ids", set()))
    assert "resp_001" in canceled

    # Subsequent in-flight deltas for resp_001 must be dropped completely
    await bridge.handle_realtime_event(
        SimpleNamespace(type="response.audio.delta", response_id="resp_001", delta=b64_chunk)
    )
    frames_after = [t for t, _ in fake_serial.writer.sent_frames() if t == MSG_SPK_PCM]
    assert len(frames_after) == 1


@pytest.mark.asyncio
async def test_scenario_5_2_toggle_silent_true_resumes_audio_forwarding(
    bridge, fake_serial
) -> None:
    """Scenario 5.2: toggle_silent(true) re-enables speak_back and forwards subsequent audio deltas."""
    bridge.state.speak_back = False

    unmute_payload = json.dumps({"event": "toggle_silent", "speak_back": True}).encode("utf-8")
    await bridge.handle_serial_bytes(encode_ltv_frame(MSG_JSON_UI, unmute_payload))
    assert bridge.state.speak_back is True

    await bridge.handle_realtime_event(
        SimpleNamespace(type="response.created", response=SimpleNamespace(id="resp_002"))
    )
    pcm_chunk = b"\x10\x20" * 240
    b64_chunk = base64.b64encode(pcm_chunk).decode("ascii")
    await bridge.handle_realtime_event(
        SimpleNamespace(type="response.audio.delta", response_id="resp_002", delta=b64_chunk)
    )

    spk_frames = [p for t, p in fake_serial.writer.sent_frames() if t == MSG_SPK_PCM]
    assert len(spk_frames) == 1
    assert spk_frames[0] == pcm_chunk


@pytest.mark.asyncio
async def test_scenario_5_3_shutdown_flushes_cc123_across_channels_1_to_3(
    bridge, fake_midi, fake_serial
) -> None:
    """Scenario 5.3: Graceful shutdown flushes CC 123 (All Notes Off) on Channels 1-3 before closing ports."""
    bridge.write_chord_progression(section="VERSE")
    bridge.process_tick(0)
    fake_midi.messages.clear()

    await bridge.shutdown()

    raw_midi = fake_midi.raw_bytes()
    assert [0xB0 | CH_PIANO, 123, 0] in raw_midi
    assert [0xB0 | CH_BASS, 123, 0] in raw_midi
    assert [0xB0 | CH_DRUMS, 123, 0] in raw_midi
    assert fake_midi.closed is True
    assert fake_serial.writer.closed is True
