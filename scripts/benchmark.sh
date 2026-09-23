#!/usr/bin/env bash
# Measure live throughput and latency over vcan0 (these are the resume numbers).
set -euo pipefail
cd "$(dirname "$0")/.."
N="${1:-200000}"
scripts/setup_vcan.sh vcan0 >/dev/null
build/j1939_monitor --iface vcan0 > /tmp/bench.txt &
MON=$!
sleep 0.5
build/j1939_sim --iface vcan0 --flood "$N"
sleep 1
kill -INT "$MON"; wait "$MON" || true
tail -6 /tmp/bench.txt
