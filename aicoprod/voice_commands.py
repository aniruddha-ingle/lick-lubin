"""Translate a single Orchid transcript into one JSON-lines command for JUCE."""

import argparse
import json
import re
from pathlib import Path
from uuid import uuid4

MANIFEST = Path(__file__).parent / "desktop/audio_proof/manifest.json"
BAR_WORDS = "one two three four five six seven eight nine ten eleven twelve thirteen fourteen fifteen sixteen".split()
BARS = {name: number for number, name in enumerate(BAR_WORDS, 1)}
BARS.update({str(number): number for number in range(1, 17)})


def load_aliases(path=MANIFEST):
    aliases = {}
    for sample in json.loads(path.read_text(encoding="utf-8"))["samples"]:
        alias = " ".join(sample["alias"].casefold().split())
        if alias in aliases:
            raise ValueError(f"Ambiguous sample alias: {alias}")
        aliases[alias] = sample["id"]
    return aliases


def parse_command(transcript, aliases, expected_revision, command_id=None):
    """Bars in the wire format are numbered 1–16, as displayed in the grid."""
    if type(expected_revision) is not int or expected_revision < 0:
        raise ValueError("Expected revision must be a nonnegative integer.")
    if command_id is not None and (not isinstance(command_id, str) or not command_id.strip()):
        raise ValueError("Command ID must be a nonempty string.")
    text = " ".join(transcript.strip().rstrip(".!").casefold().split())
    request = {"command_id": command_id or str(uuid4()), "expected_revision": expected_revision}
    if text in ("play", "stop"):
        return {**request, "op": text}

    match = re.fullmatch(r"(put|place|add) (.+) (?:in|on|at) bar (\w+)", text)
    enabled = match is not None
    if not enabled:
        match = re.fullmatch(r"(remove) (.+) from bar (\w+)", text)
    if not match:
        raise ValueError("Use play, stop, put <sample> in bar <number>, or remove <sample> from bar <number>.")
    _, alias, number = match.groups()
    if alias not in aliases:
        raise ValueError(f"Unknown sample alias: {alias}")
    if number not in BARS:
        raise ValueError("Choose one bar from 1 to 16.")
    return {**request, "op": "set_cell", "track_id": aliases[alias], "bar": BARS[number], "enabled": enabled}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("transcript", help='For example, "put Perfect Donk in bar one"')
    parser.add_argument("--expected-revision", type=int, required=True)
    parser.add_argument("--command-id", help="Reuse the original ID when retrying a request")
    args = parser.parse_args()
    try:
        command = parse_command(args.transcript, load_aliases(), args.expected_revision, args.command_id)
    except (OSError, ValueError, KeyError) as error:
        parser.exit(2, f"{error}\n")
    print(json.dumps(command))


if __name__ == "__main__":
    main()
