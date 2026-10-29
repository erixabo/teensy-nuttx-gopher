#!/bin/sh
set -eu

if [ "$#" -ne 1 ]; then
  echo "Usage: $0 firmware.hex" >&2
  exit 2
fi

hex=$1
port=${TEENSY_PORT:-/dev/ttyACM0}
loader=${TEENSY_LOADER:-teensy_loader_cli}

if [ ! -r "$hex" ]; then
  echo "Cannot read firmware: $hex" >&2
  exit 1
fi

if [ ! -e "$port" ]; then
  echo "Teensy CDC serial port not found: $port" >&2
  exit 1
fi

echo "Requesting Teensy 4.1 HalfKay through USB CDC (134 baud)..."
sudo -n stty -F "$port" 134

echo "Programming $hex..."
if sudo -n "$loader" --mcu=TEENSY41 -w -v "$hex"; then
  exit 0
fi

echo "First loader attempt failed; retrying once while HalfKay remains active..." >&2
exec sudo -n "$loader" --mcu=TEENSY41 -w -v "$hex"
