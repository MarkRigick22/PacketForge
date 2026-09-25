#!/usr/bin/env bash
set -euo pipefail
# Run from the project root. Each measured row gets a fresh process.
build=${1:-build}
output=${2:-benchmark_results.csv}
packets=${PACKETS:-100000}
repeats=${REPEATS:-3}
temporary=$(mktemp)
trap 'rm -f "$temporary"' EXIT
"$build/packetforge" --synthetic --packets 10000 --optimized >/dev/null
"$build/packetforge" --synthetic --packets 10000 --baseline >/dev/null
: > "$output"
first=1
for workers in 1 2 4 8; do
    for ((trial=0; trial<repeats; trial++)); do
        # Alternate order to limit systematic warm-cache/order bias.
        modes=(baseline optimized)
        if ((trial % 2)); then modes=(optimized baseline); fi
        for mode in "${modes[@]}"; do
            "$build/packetforge" --synthetic --packets "$packets" --workers "$workers" --"$mode" --csv > "$temporary"
            if ((first)); then head -n 1 "$temporary" >> "$output"; first=0; fi
            tail -n 1 "$temporary" >> "$output"
        done
    done
done
python3 tools/analyze_results.py "$output"
