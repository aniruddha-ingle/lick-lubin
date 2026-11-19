import asyncio
import inspect
from pathlib import Path
from typing import Any, Callable, Dict, List, Optional, Tuple
import pytest
import pytest_asyncio

import host_bridge
from host_bridge import LickLubinBridge, encode_ltv_frame


class FakeStreamWriter:
    def __init__(self) -> None:
        self.buffer = bytearray()
        self.closed = False

    def write(self, data: bytes) -> None:
        if not self.closed:
            self.buffer.extend(data)

    async def drain(self) -> None:
        await asyncio.sleep(0)

    def close(self) -> None:
        self.closed = True

    def is_closing(self) -> bool:
        return self.closed

    async def wait_closed(self) -> None:
        await asyncio.sleep(0)

    def sent_frames(self) -> List[Tuple[int, bytes]]:
        frames: List[Tuple[int, bytes]] = []
        idx = 0
        buf = bytes(self.buffer)
        while idx + 4 <= len(buf):
            if buf[idx] != 0xAA:
                idx += 1
                continue
            msg_type = buf[idx + 1]
            length = (buf[idx + 2] << 8) | buf[idx + 3]
            if idx + 4 + length > len(buf):
                break
            payload = buf[idx + 4 : idx + 4 + length]
            frames.append((msg_type, payload))
            idx += 4 + length
        return frames


class FakeSerialTransport:
    def __init__(self, loop: asyncio.AbstractEventLoop) -> None:
        self.reader = asyncio.StreamReader(loop=loop)
        self.writer = FakeStreamWriter()

    def feed_bytes(self, data: bytes) -> None:
        self.reader.feed_data(data)

    def feed_frame(self, msg_type: int, payload: bytes = b"") -> None:
        self.reader.feed_data(encode_ltv_frame(msg_type, payload))

    def feed_eof(self) -> None:
        self.reader.feed_eof()


class FakeMidiOut:
    def __init__(self) -> None:
        self.messages: List[Tuple[float, List[int]]] = []
        self.ports = ["IAC Driver Bus 1"]
        self.opened_port: Optional[int] = None
        self.virtual_port: Optional[str] = None
        self.closed = False

    def get_ports(self) -> List[str]:
        return list(self.ports)

    def open_port(self, idx: int) -> None:
        self.opened_port = idx

    def open_virtual_port(self, name: str) -> None:
        self.virtual_port = name

    def send_message(self, msg: List[int]) -> None:
        import time
        self.messages.append((time.perf_counter(), list(msg)))

    def close_port(self) -> None:
        self.closed = True

    def raw_bytes(self) -> List[List[int]]:
        return [m for _, m in self.messages]

    def clear(self) -> None:
        self.messages.clear()


class _FakeInputAudioBufferResource:
    def __init__(self) -> None:
        self.appended_b64: List[str] = []
        self.commit_calls: int = 0
        self.clear_calls: int = 0

    async def append(self, *, audio: str) -> None:
        self.appended_b64.append(audio)

    async def commit(self) -> None:
        self.commit_calls += 1

    async def clear(self) -> None:
        self.clear_calls += 1


class _FakeResponseResource:
    def __init__(self) -> None:
        self.create_calls: List[Dict[str, Any]] = []
        self.cancel_calls: int = 0

    async def create(self, **kwargs: Any) -> None:
        self.create_calls.append(kwargs)

    async def cancel(self) -> None:
        self.cancel_calls += 1


class _FakeConversationItemResource:
    def __init__(self) -> None:
        self.created_items: List[Dict[str, Any]] = []

    async def create(self, **kwargs: Any) -> None:
        self.created_items.append(kwargs)


class _FakeConversationResource:
    def __init__(self) -> None:
        self.item = _FakeConversationItemResource()


class _FakeSessionResource:
    def __init__(self) -> None:
        self.updates: List[Dict[str, Any]] = []

    async def update(self, **kwargs: Any) -> None:
        self.updates.append(kwargs)


class FakeRealtimeConnection:
    def __init__(self) -> None:
        self.input_audio_buffer = _FakeInputAudioBufferResource()
        self.response = _FakeResponseResource()
        self.conversation = _FakeConversationResource()
        self.session = _FakeSessionResource()
        self._queue: asyncio.Queue[Any] = asyncio.Queue()

    def push_event(self, event: Any) -> None:
        self._queue.put_nowait(event)

    def __aiter__(self):
        return self

    async def __anext__(self) -> Any:
        item = await self._queue.get()
        if item is StopAsyncIteration:
            raise StopAsyncIteration
        return item


@pytest_asyncio.fixture
async def fake_serial() -> FakeSerialTransport:
    loop = asyncio.get_running_loop()
    return FakeSerialTransport(loop=loop)


@pytest.fixture
def fake_midi() -> FakeMidiOut:
    return FakeMidiOut()


@pytest.fixture
def fake_realtime() -> FakeRealtimeConnection:
    return FakeRealtimeConnection()


@pytest_asyncio.fixture
async def bridge(
    tmp_path: Path,
    fake_serial: FakeSerialTransport,
    fake_midi: FakeMidiOut,
    fake_realtime: FakeRealtimeConnection,
):
    drops_path = tmp_path / "AI_Drops"
    drops_path.mkdir(parents=True, exist_ok=True)

    sig = inspect.signature(LickLubinBridge.__init__)
    kwargs: Dict[str, Any] = {}
    if "midi_out" in sig.parameters:
        kwargs["midi_out"] = fake_midi
    if "drops_dir" in sig.parameters:
        kwargs["drops_dir"] = drops_path
    if "ai_drops_dir" in sig.parameters:
        kwargs["ai_drops_dir"] = drops_path

    b = LickLubinBridge(**kwargs)

    # Ensure MIDI spy and Path drops_dir are wired regardless of constructor signature
    if hasattr(b, "seq") and hasattr(b.seq, "midi_out"):
        b.seq.midi_out = fake_midi
    if hasattr(b, "midi_out"):
        b.midi_out = fake_midi
    b.drops_dir = drops_path
    b.ai_drops_dir = drops_path

    # Attach serial transport and realtime connection across all attribute conventions
    if hasattr(b, "attach_serial"):
        b.attach_serial(fake_serial.reader, fake_serial.writer)
    else:
        b.serial_reader = fake_serial.reader
        b.serial_writer = fake_serial.writer

    if hasattr(b, "attach_realtime"):
        b.attach_realtime(fake_realtime)
    else:
        b.realtime_conn = fake_realtime
        b.conn = fake_realtime

    yield b

    # Teardown: cancel all background tasks and flush MIDI cleanly
    for task_attr in ("_serial_task", "_realtime_task", "_sequencer_task", "_heartbeat_task"):
        t = getattr(b, task_attr, None)
        if t is not None and not t.done():
            t.cancel()
            try:
                await t
            except asyncio.CancelledError:
                pass
    if hasattr(b, "seq") and getattr(b.seq, "_task", None) is not None:
        t = b.seq._task
        if not t.done():
            t.cancel()
            try:
                await t
            except asyncio.CancelledError:
                pass
    if hasattr(b, "shutdown"):
        await b.shutdown()
    elif hasattr(b, "seq") and hasattr(b.seq, "panic_all"):
        b.seq.panic_all()
