#!/usr/bin/env bash
set -euo pipefail
if [[ $(uname -s) != Linux ]]; then
    echo 'Linux perf profiling requires a Linux host.' >&2
    exit 1
fi
command -v perf >/dev/null || { echo 'Install the perf package matching your Linux kernel.' >&2; exit 1; }
build=${1:-build}
perf stat -r 3 -e cycles,instructions,cache-references,cache-misses,context-switches,branches,branch-misses \
    "$build/packetforge" --synthetic --packets 1000000 --workers 4 --optimized
# No sysctl changes or elevated permissions are applied automatically.
