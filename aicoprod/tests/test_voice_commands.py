import unittest
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from voice_commands import BAR_WORDS, load_aliases, parse_command


class VoiceCommands(unittest.TestCase):
    def setUp(self):
        self.aliases = load_aliases()

    def parse(self, phrase):
        return parse_command(phrase, self.aliases, 3, "demo-1")

    def test_transport(self):
        for op in ("play", "stop"):
            self.assertEqual(self.parse(op.upper() + "!"), {
                "op": op, "command_id": "demo-1", "expected_revision": 3})

    def test_all_curated_aliases_and_bars(self):
        for alias, track in self.aliases.items():
            for bar, word in enumerate(BAR_WORDS, 1):
                for phrase, enabled in ((f"put {alias} in bar {word}.", True),
                                        (f"remove {alias} from bar {bar}", False)):
                    self.assertEqual(self.parse(phrase), {
                        "op": "set_cell", "track_id": track, "bar": bar, "enabled": enabled,
                        "expected_revision": 3, "command_id": "demo-1"})

    def test_reject_whole_ambiguous_or_invalid_request(self):
        for phrase in ("play and stop", "do not play", "put Clean and Pop in bar one",
                       "put Clean in bar 0", "remove Pop from bar 17", "put Snare in bar 2",
                       "put New Flow in bar 1", "put Clean in bar 1 and 2"):
            with self.subTest(phrase=phrase), self.assertRaises(ValueError):
                self.parse(phrase)

    def test_request_identity_and_revision(self):
        self.assertNotEqual(parse_command("stop", self.aliases, 1)["command_id"],
                            parse_command("stop", self.aliases, 1)["command_id"])
        for rev in (-1, True, "1"):
            with self.assertRaises(ValueError):
                parse_command("play", self.aliases, rev)

    def test_track_effects_and_sidechain(self):
        for phrase, fields in (
            ("cut itself on for Perfect Donk", {"op": "set_cut_itself", "track_id": "perfect_donk", "enabled": True}),
            ("add echo to Bunker", {"op": "set_echo", "track_id": "bunker_rimshot", "enabled": True}),
            ("put Pop in a big room", {"op": "set_reverb", "track_id": "pop_clap", "mode": "hall"}),
            ("echo off for Open", {"op": "set_echo", "track_id": "open_hihat", "enabled": False}),
            ("room reverb on for Pop", {"op": "set_reverb", "track_id": "pop_clap", "mode": "room"}),
            ("hall reverb on for Pop", {"op": "set_reverb", "track_id": "pop_clap", "mode": "hall"}),
            ("reverb off for Pop", {"op": "set_reverb", "track_id": "pop_clap", "mode": "off"}),
            ("side chain Clean to Heavyweight", {"op": "set_sidechain", "source_id": "clean_kick",
                                                   "target_id": "heavyweight", "enabled": True}),
            ("remove sidechain Clean to Heavyweight", {"op": "set_sidechain", "source_id": "clean_kick",
                                                         "target_id": "heavyweight", "enabled": False}),
        ):
            self.assertEqual(self.parse(phrase), {**fields, "expected_revision": 3, "command_id": "demo-1"})
        for phrase in ("sidechain Clean to Clean", "reverb on for Pop", "echo off for New Flow"):
            with self.assertRaises(ValueError):
                self.parse(phrase)

    def test_cli_socket_revision_lookup_and_delivery(self):
        import json
        import socket
        import subprocess
        import tempfile
        import threading

        with tempfile.TemporaryDirectory(dir="/tmp") as tmp, socket.socket(socket.AF_UNIX) as server:
            address = str(Path(tmp) / "control.sock")
            server.bind(address)
            server.listen()
            server.settimeout(5)
            received = []

            def receive():
                for revision in (7, 8):
                    connection, _ = server.accept()
                    with connection, connection.makefile("rb") as reader:
                        received.append(json.loads(reader.readline()))
                        connection.sendall((json.dumps({"accepted": True, "revision": revision}) + "\n").encode())

            thread = threading.Thread(target=receive)
            thread.start()
            cli = Path(__file__).resolve().parents[1] / "voice_commands.py"
            result = subprocess.run([sys.executable, str(cli), "echo on for Pop", "--socket", address],
                                    check=True, capture_output=True, text=True, timeout=8)
            thread.join(5)
            self.assertEqual(json.loads(result.stdout), {"accepted": True, "revision": 8})
            self.assertEqual(received[0]["op"], "status")
            self.assertEqual(received[1]["expected_revision"], 7)
