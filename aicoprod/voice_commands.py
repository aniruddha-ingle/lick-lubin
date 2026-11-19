"""Translate a single Orchid transcript into one JSON-lines command for JUCE."""

import argparse
import json
import re
import socket
import subprocess
import sys
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
    """Bars and steps in the wire format are numbered 1–16, as displayed in the grid."""
    if type(expected_revision) is not int or expected_revision < 0:
        raise ValueError("Expected revision must be a nonnegative integer.")
    if command_id is not None and (not isinstance(command_id, str) or not command_id.strip()):
        raise ValueError("Command ID must be a nonempty string.")
    text = " ".join(transcript.strip().rstrip(".!").casefold().split())
    request = {"command_id": command_id or str(uuid4()), "expected_revision": expected_revision}
    if text in ("play", "stop"):
        return {**request, "op": text}

    echo = re.fullmatch(r"(add echo to|remove echo from) (.+)", text)
    room = re.fullmatch(r"put (.+) in (?:a|the) (big room|small room|hall|room)", text)
    if echo:
        state = "on" if echo[1].startswith("add") else "off"
        text = f"echo {state} for {echo[2]}"
    elif room:
        preset = "hall" if room[2] in ("hall", "big room") else "room"
        text = f"{preset} reverb on for {room[1]}"

    effect = re.fullmatch(r"(cut itself|echo|room reverb|hall reverb|reverb) (on|off) (?:for |on )?(.+)", text)
    if effect:
        name, state, alias = effect.groups()
        if alias not in aliases:
            raise ValueError(f"Unknown sample alias: {alias}")
        request["track_id"] = aliases[alias]
        if name.endswith("reverb"):
            if name == "reverb" and state == "on":
                raise ValueError("Choose room reverb or hall reverb.")
            return {**request, "op": "set_reverb", "mode": name.split()[0] if state == "on" else "off"}
        return {**request, "op": "set_echo" if name == "echo" else "set_cut_itself", "enabled": state == "on"}

    route = re.fullmatch(r"(sidechain|side chain|remove sidechain|remove side chain) (.+) to (.+)", text)
    if route:
        action, source, target = route.groups()
        if source not in aliases or target not in aliases or source == target:
            raise ValueError("Choose two distinct curated sample names.")
        return {**request, "op": "set_sidechain", "source_id": aliases[source], "target_id": aliases[target],
                "enabled": not action.startswith("remove")}

    match = re.fullmatch(r"(put|place|add) (.+) (?:in|on|at) (bar|step) (\w+)", text)
    enabled = match is not None
    if not enabled:
        match = re.fullmatch(r"(remove) (.+) from (bar|step) (\w+)", text)
    if not match:
        raise ValueError("Use play, stop, put <sample> on bar/step <number>, or remove <sample> from bar/step <number>.")
    _, alias, unit, number = match.groups()
    if alias not in aliases:
        raise ValueError(f"Unknown sample alias: {alias}")
    if number not in BARS:
        raise ValueError(f"Choose one {unit} from 1 to 16.")
    op = "set_step" if unit == "step" else "set_cell"
    return {**request, "op": op, "track_id": aliases[alias], unit: BARS[number], "enabled": enabled}


def deliver(command, socket_path):
    """Send one request and read the receiver's acknowledgement."""
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection:
        connection.settimeout(5)
        connection.connect(str(socket_path))
        connection.sendall((json.dumps(command) + "\n").encode())
        with connection.makefile("rb") as stream:
            line = stream.readline(65537)
    if len(line) > 65536 or not line.endswith(b"\n"):
        raise ValueError("Invalid or incomplete acknowledgement from JUCE.")
    ack = json.loads(line)
    if not isinstance(ack, dict) or type(ack.get("accepted")) is not bool or type(ack.get("revision")) is not int:
        raise ValueError("Invalid JUCE acknowledgement fields.")
    return ack


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("transcript", nargs="?", help='For example, "put Perfect Donk in bar one"')
    parser.add_argument("--expected-revision", type=int, help="Defaults to the revision returned by JUCE when using a socket")
    parser.add_argument("--command-id", help="Reuse the original ID when retrying a request")
    parser.add_argument("--socket", nargs="?", const=Path.home() / ".lick-lubin/control.sock", type=Path, help="Send to this JUCE Unix socket and print its acknowledgement")
    parser.add_argument("--mic", action="store_true", help="Record a four-second phrase on the default Mac input")
    args = parser.parse_args()
    if args.mic == (args.transcript is not None):
        parser.error("Supply a transcript or --mic.")
    try:
        aliases = load_aliases()
        transcript = args.transcript
        if args.mic:
            helper = Path(__file__).with_name("orchid-dictate")
            if not helper.is_file():
                raise ValueError("Build the microphone helper with bash aicoprod/build_orchid_dictate.sh.")
            transcript = subprocess.check_output([str(helper), *aliases], text=True).strip()
        if args.mic:
            print("Heard " + transcript, file=sys.stderr)
        revision = args.expected_revision
        if revision is None and args.socket:
            snapshot = deliver({"op": "status", "command_id": str(uuid4())}, args.socket)
            if not snapshot["accepted"]:
                raise ValueError(snapshot.get("reason", "Could not obtain arrangement revision."))
            revision = snapshot["revision"]
        if revision is None:
            raise ValueError("Supply --expected-revision to preview a command without a socket.")
        command = parse_command(transcript, aliases, revision, args.command_id)
        if args.socket:
            ack = deliver(command, args.socket)
            print(json.dumps(ack))
            return 0 if ack["accepted"] else 1
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        parser.exit(2, f"{error}\n")
    print(json.dumps(command))


if __name__ == "__main__":
    raise SystemExit(main())
