#!/usr/bin/env bash
set -euo pipefail
[[ $(uname -s) == Linux && $EUID == 0 ]] || { echo 'Requires Linux root for isolated namespace setup' >&2; exit 1; }
binary=$(realpath "${1:-build-release/packetforge}")
namespace="pf-test-$$"
cleanup() { ip netns del "$namespace"; }
ip netns add "$namespace"
trap cleanup EXIT
ip -n "$namespace" link set lo up
for interface in pf-in pf-out; do
    ip netns exec "$namespace" ip tuntap add dev "$interface" mode tap
    ip netns exec "$namespace" sh -c "echo 1 > /proc/sys/net/ipv6/conf/$interface/disable_ipv6"
    ip -n "$namespace" link set "$interface" up
done
ip netns exec "$namespace" python3 tools/test_tap_frames.py "$binary"
