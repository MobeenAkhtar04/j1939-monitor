#!/usr/bin/env bash
# Create a virtual CAN interface (no hardware needed). Requires root.
set -euo pipefail
IFACE="${1:-vcan0}"
sudo modprobe vcan
if ! ip link show "$IFACE" >/dev/null 2>&1; then
  sudo ip link add dev "$IFACE" type vcan
fi
sudo ip link set "$IFACE" txqueuelen 1000   # default of 10 drops frames under load
sudo ip link set up "$IFACE"
ip -details link show "$IFACE"
