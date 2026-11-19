#!/usr/bin/env bash
set -e
cd "$(dirname "$0")"

echo "=== LICK-LUBIN STAGE PRE-FLIGHT ==="

# 1. Check .env and API key
if [ ! -f .env ] || ! grep -q "OPENAI_API_KEY=sk-" .env; then
  echo "❌ ERROR: Missing valid OPENAI_API_KEY in host/.env"
  exit 1
fi
echo "✅ OpenAI API Key found"

# 2. Verify IAC Driver Bus 1 & CoreS3 Hardware
python3 - <<'EOF'
import sys, rtmidi, serial.tools.list_ports

midi_out = rtmidi.MidiOut()
ports = midi_out.get_ports()
if not any("IAC Driver Bus 1" in p for p in ports):
    print(f"❌ ERROR: 'IAC Driver Bus 1' not found. Available MIDI ports: {ports}")
    sys.exit(1)
print("✅ CoreMIDI 'IAC Driver Bus 1' active")

cores3 = [p.device for p in serial.tools.list_ports.comports() if p.vid == 0x303A]
if not cores3:
    print("⚠️  WARNING: M5Stack CoreS3 (VID 0x303A) not detected yet. Plug in USB-C.")
else:
    print(f"✅ M5Stack CoreS3 detected at {cores3}")
EOF

echo "🚀 Launching Lick-Lubin Host Bridge (101 BPM / D Major)..."
exec python3 host_bridge.py
