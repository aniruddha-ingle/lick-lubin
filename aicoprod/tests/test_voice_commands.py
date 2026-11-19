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
