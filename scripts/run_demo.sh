#!/usr/bin/env bash
# Full live demo: vcan0 + virtual serial port + simulator + monitor + Qt dashboard.
set -euo pipefail
cd "$(dirname "$0")/.."
B=build
IFACE=vcan0

scripts/setup_vcan.sh "$IFACE" >/dev/null

# Virtual null-modem cable: the monitor writes to ttyMON, you can read ttyREAD.
socat pty,raw,echo=0,link=/tmp/ttyMON pty,raw,echo=0,link=/tmp/ttyREAD &
PIDS=($!)
sleep 0.5
cat /tmp/ttyREAD > serial_capture.log &
PIDS+=($!)

[ -x "$B/j1939_dashboard" ] && { "$B/j1939_dashboard" & PIDS+=($!); }
"$B/j1939_monitor" --iface "$IFACE" --serial /tmp/ttyMON --udp 127.0.0.1:9000 &
PIDS+=($!)
trap 'kill "${PIDS[@]}" 2>/dev/null || true' EXIT

sleep 1
"$B/j1939_sim" --iface "$IFACE"     # one 55 s drive cycle
sleep 2
echo "serial output captured in serial_capture.log"
