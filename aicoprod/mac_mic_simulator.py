#!/usr/bin/env python3
"""Exercise the Realtime host flow with the Mac's microphone and speakers.

This development-only path replaces the CoreS3 serial audio endpoint. It uses
the same host bridge PCM and PTT handlers, but does not verify firmware,
touchscreen, USB serial, or CoreS3 I2S behavior.
"""

import argparse
import asyncio
import json
import logging
import os
import queue
import threading
from typing import Any, Optional

try:
    import sounddevice as sd
except ImportError as exc:
    raise SystemExit(
        "The Mac mic simulator requires sounddevice. Install project dependencies with "
        "`python3 -m pip install -r requirements.txt`."
    ) from exc

from host_bridge import (
    LICK_LUBIN_SYSTEM_PROMPT,
    MSG_JSON_UI,
    MSG_MIC_PCM,
    MSG_SPK_PCM,
    REALTIME_TOOLS,
    SAMPLE_RATE,
    LtvStreamDecoder,
    LickLubinBridge,
)
from openai import AsyncOpenAI


log = logging.getLogger("lick-lubin.mac-mic")
INPUT_BLOCK_SAMPLES = 480  # 20 ms at 24 kHz
INPUT_QUEUE_BLOCKS = 100   # Bound queued capture to about two seconds
OUTPUT_QUEUE_CHUNKS = 24   # Bound playback latency if the output device falls behind


def list_input_devices() -> None:
    devices = sd.query_devices()
    for index, device in enumerate(devices):
        if int(device.get("max_input_channels", 0)) > 0:
            print(f"{index}: {device['name']} ({device['max_input_channels']} input channels)")


class NullMidiOut:
    """Keep mic testing independent of an IAC bus or FL Studio."""

    def send_message(self, _message: list[int]) -> None:
        return

    def close_port(self) -> None:
        return


class MacSpeakerSink:
    """Accept the bridge's framed speaker PCM and play it on the default output."""

    def __init__(self, sounddevice: Any) -> None:
        self._sounddevice = sounddevice
        self._decoder = LtvStreamDecoder()
        self._chunks: queue.Queue[bytes] = queue.Queue(maxsize=OUTPUT_QUEUE_CHUNKS)
        self._pending = bytearray()
        self._flush_requested = threading.Event()
        self._closed = False
        self._stream = sounddevice.RawOutputStream(
            samplerate=SAMPLE_RATE,
            channels=1,
            dtype="int16",
            blocksize=INPUT_BLOCK_SAMPLES,
            callback=self._callback,
        )
        self._stream.start()

    def write(self, data: bytes) -> None:
        if self._closed:
            return
        for msg_type, payload in self._decoder.feed(data):
            if msg_type == MSG_JSON_UI:
                try:
                    state = json.loads(payload.decode("utf-8"))
                except (UnicodeDecodeError, json.JSONDecodeError):
                    continue
                if state.get("speak_back") is False:
                    self._flush_requested.set()
                    while True:
                        try:
                            self._chunks.get_nowait()
                        except queue.Empty:
                            break
                continue
            if msg_type != MSG_SPK_PCM or not payload:
                continue
            try:
                self._chunks.put_nowait(payload)
            except queue.Full:
                # Favor current talkback over stale queued speech.
                try:
                    self._chunks.get_nowait()
                except queue.Empty:
                    pass
                try:
                    self._chunks.put_nowait(payload)
                except queue.Full:
                    log.warning("Dropping a speaker chunk because playback is behind")

    def _callback(self, outdata: Any, frames: int, _time_info: Any, _status: Any) -> None:
        if self._flush_requested.is_set():
            self._pending.clear()
            self._flush_requested.clear()
        needed = frames * 2  # mono int16
        while len(self._pending) < needed:
            try:
                self._pending.extend(self._chunks.get_nowait())
            except queue.Empty:
                break

        available = min(needed, len(self._pending))
        if available:
            outdata[:available] = self._pending[:available]
            del self._pending[:available]
        if available < needed:
            outdata[available:needed] = bytes(needed - available)

    def close(self) -> None:
        if self._closed:
            return
        self._closed = True
        self._stream.stop()
        self._stream.close()

    async def wait_closed(self) -> None:
        return


class MacMicSimulator:
    def __init__(self, bridge: LickLubinBridge, loop: asyncio.AbstractEventLoop) -> None:
        self.bridge = bridge
        self.loop = loop
        self._capture_enabled = threading.Event()
        self._capture_queue: asyncio.Queue[bytes] = asyncio.Queue(maxsize=INPUT_QUEUE_BLOCKS)
        self._capture_stream: Optional[Any] = None
        self._capture_task: Optional[asyncio.Task[None]] = None
        self._dropped_capture_blocks = 0

        # The bridge still emits normal 0x02 framed output, which this sink
        # turns into PCM for the Mac's default speaker/headphone device.
        self.speaker_sink = MacSpeakerSink(sd)
        self.bridge.writer = self.speaker_sink  # type: ignore[assignment]

    def start(self, device: Optional[int]) -> None:
        self._capture_task = asyncio.create_task(self._capture_loop())
        self._capture_stream = sd.RawInputStream(
            device=device,
            samplerate=SAMPLE_RATE,
            channels=1,
            dtype="int16",
            blocksize=INPUT_BLOCK_SAMPLES,
            callback=self._input_callback,
        )

    def _input_callback(self, indata: Any, _frames: int, _time_info: Any, status: Any) -> None:
        if status:
            # Keep callback work bounded; the detailed warning is emitted on
            # the asyncio thread by the scheduled callback.
            self.loop.call_soon_threadsafe(log.warning, "Microphone stream status: %s", status)
        if not self._capture_enabled.is_set():
            return
        pcm = bytes(indata)
        self.loop.call_soon_threadsafe(self._enqueue_capture, pcm)

    def _enqueue_capture(self, pcm: bytes) -> None:
        try:
            self._capture_queue.put_nowait(pcm)
        except asyncio.QueueFull:
            self._dropped_capture_blocks += 1

    async def _capture_loop(self) -> None:
        while True:
            pcm = await self._capture_queue.get()
            try:
                if self.bridge.state.ptt_active:
                    await self.bridge.handle_serial_frame(MSG_MIC_PCM, pcm)
            except Exception:
                log.exception("Could not forward a captured microphone block")
            finally:
                self._capture_queue.task_done()

    async def _send_ui_event(self, event: str, *, ptt: bool, speak_back: bool) -> None:
        payload = json.dumps(
            {"event": event, "ptt": ptt, "speak_back": speak_back},
            separators=(",", ":"),
        ).encode("utf-8")
        await self.bridge.handle_serial_frame(MSG_JSON_UI, payload)

    async def run_console_ptt(self) -> None:
        print("Mac mic simulator ready. Use headphones to avoid speaker-to-mic echo.")
        print("Press Enter to start a turn; press Enter again to send it. Ctrl-C exits.")
        while True:
            await asyncio.to_thread(input, "[Enter] start speaking > ")
            await self._send_ui_event("ptt_start", ptt=True, speak_back=self.bridge.state.speak_back)
            self._capture_enabled.set()
            assert self._capture_stream is not None
            await asyncio.to_thread(self._capture_stream.start)
            print("Listening…")

            await asyncio.to_thread(input, "[Enter] send turn > ")
            self._capture_enabled.clear()
            # Stop waits for PortAudio callbacks to finish posting captured
            # blocks. Then a loop barrier ensures their queue callbacks ran.
            await asyncio.to_thread(self._capture_stream.stop)
            barrier = self.loop.create_future()
            self.loop.call_soon(barrier.set_result, None)
            await barrier
            await self._capture_queue.join()
            await self._send_ui_event("ptt_release", ptt=False, speak_back=self.bridge.state.speak_back)
            print("Turn submitted; waiting for the Realtime response…")

    async def close(self) -> None:
        self._capture_enabled.clear()
        if self._capture_stream is not None:
            if self._capture_stream.active:
                await asyncio.to_thread(self._capture_stream.stop)
            self._capture_stream.close()
            self._capture_stream = None
        if self._capture_task is not None:
            self._capture_task.cancel()
            await asyncio.gather(self._capture_task, return_exceptions=True)
            self._capture_task = None
        self.speaker_sink.close()
        if self._dropped_capture_blocks:
            log.warning("Dropped %d microphone blocks because the host was behind", self._dropped_capture_blocks)


async def run_simulator(device: Optional[int]) -> None:
    if not os.environ.get("OPENAI_API_KEY"):
        raise RuntimeError("Set OPENAI_API_KEY in the environment or aicoprod/.env before starting the mic simulator.")

    bridge = LickLubinBridge(midi_out=NullMidiOut())
    simulator = MacMicSimulator(bridge, asyncio.get_running_loop())
    client = AsyncOpenAI()

    try:
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
            simulator.start(device)
            log.info("Realtime Mac mic simulator connected at %d Hz mono PCM16", SAMPLE_RATE)
            await simulator.run_console_ptt()
    finally:
        await simulator.close()
        await bridge.shutdown()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--device", type=int, help="sounddevice input device index; defaults to the macOS default input")
    parser.add_argument("--list-devices", action="store_true", help="list available microphone input devices and exit")
    return parser.parse_args()


async def main() -> None:
    args = parse_args()
    if args.list_devices:
        list_input_devices()
        return
    await run_simulator(args.device)


if __name__ == "__main__":
    logging.basicConfig(level=logging.INFO, format="%(asctime)s [%(levelname)s] %(name)s: %(message)s")
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\nMac mic simulator stopped.")
