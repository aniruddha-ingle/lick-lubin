import base64
import json
import pytest

from host_bridge import (
    LtvStreamDecoder,
    MAX_PAYLOAD_LEN,
    MSG_HEARTBEAT,
    MSG_JSON_UI,
    MSG_MIC_PCM,
    SYNC_BYTE,
    encode_ltv_frame,
)


def test_scenario_1_1_mic_pcm_960_byte_encode_decode() -> None:
    """Scenario 1.1: 0x01 MSG_MIC_PCM with 960 bytes encodes [0xAA, 0x01, 0x03, 0xC0, <960B>] and decodes cleanly."""
    pcm_payload = bytes([i & 0xFF for i in range(960)])
    encoded = encode_ltv_frame(MSG_MIC_PCM, pcm_payload)

    assert len(encoded) == 4 + 960
    assert encoded[0] == SYNC_BYTE
    assert encoded[1] == MSG_MIC_PCM
    assert encoded[2] == 0x03
    assert encoded[3] == 0xC0
    assert encoded[4:] == pcm_payload

    decoder = LtvStreamDecoder()
    frames = decoder.feed(encoded)
    assert len(frames) == 1
    msg_type, decoded_payload = frames[0]
    assert msg_type == MSG_MIC_PCM
    assert decoded_payload == pcm_payload


@pytest.mark.asyncio
async def test_scenario_1_2_ptt_start_audio_and_release_commit(bridge, fake_realtime) -> None:
    """Scenario 1.2: ptt_start enables mic capture, forwards PCM audio, and ptt_release commits + creates response."""
    ptt_start = encode_ltv_frame(
        MSG_JSON_UI,
        json.dumps({"event": "ptt_start", "ptt": True, "speak_back": True}).encode("utf-8"),
    )
    await bridge.handle_serial_bytes(ptt_start)

    assert bridge.state.ptt_active is True
    assert bridge.state.speak_back is True

    pcm_chunks = [bytes([index]) * 960 for index in (1, 2)]
    for pcm_chunk in pcm_chunks:
        await bridge.handle_serial_bytes(encode_ltv_frame(MSG_MIC_PCM, pcm_chunk))
    assert fake_realtime.input_audio_buffer.appended_b64 == [
        base64.b64encode(chunk).decode("ascii") for chunk in pcm_chunks
    ]

    ptt_release = encode_ltv_frame(
        MSG_JSON_UI,
        json.dumps({"event": "ptt_release", "ptt": False, "speak_back": True}).encode("utf-8"),
    )
    await bridge.handle_serial_bytes(ptt_release)

    assert bridge.state.ptt_active is False
    assert fake_realtime.input_audio_buffer.commit_calls == 1
    assert len(fake_realtime.response.create_calls) == 1


def test_scenario_1_3_corrupt_header_and_resync_recovery() -> None:
    """Scenario 1.3: Garbage bytes, oversized header (0xFFFF), and truncated frame resync cleanly to WAIT_SYNC."""
    decoder = LtvStreamDecoder()

    # 1. Inject garbage bytes
    assert decoder.feed(bytes([0xFF, 0x00, 0x12])) == []
    assert decoder.state == LtvStreamDecoder.WAIT_SYNC

    # 2. Inject oversized header (len = 0xFFFF > MAX_PAYLOAD_LEN 4096)
    assert decoder.feed(bytes([SYNC_BYTE, MSG_MIC_PCM, 0xFF, 0xFF])) == []
    assert decoder.state == LtvStreamDecoder.WAIT_SYNC

    # 3. Inject truncated frame (claims 100 bytes, only 3 bytes arrive) followed by fresh sync + valid Heartbeat
    truncated = bytes([SYNC_BYTE, MSG_MIC_PCM, 0x00, 0x64, 0x11, 0x22, 0x33])
    assert decoder.feed(truncated) == []
    assert decoder.state == LtvStreamDecoder.READ_PAYLOAD

    # Reset stream with a fresh valid heartbeat packet after a line drop
    decoder = LtvStreamDecoder()
    valid_hb = encode_ltv_frame(MSG_HEARTBEAT, b"")
    frames = decoder.feed(bytes([0x00, 0x99]) + valid_hb)

    assert frames == [(MSG_HEARTBEAT, b"")]
    assert decoder.state == LtvStreamDecoder.WAIT_SYNC
    assert len(decoder._payload) == 0
