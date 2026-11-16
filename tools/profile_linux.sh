#!/usr/bin/env bash
set -euo pipefail
if [[ $(uname -s) != Linux ]]; then
    echo 'Linux perf profiling requires a Linux host.' >&2
    exit 1
fi
command -v perf >/dev/null || { echo 'Install the perf package matching your Linux kernel.' >&2; exit 1; }
build=${1:-build}
perf stat -x, -o perf-stat.csv -r 3 -e task-clock,cpu-migrations,cycles,instructions,cache-references,cache-misses,context-switches,branches,branch-misses \
    "$build/packetforge" --synthetic --packets 1000000 --workers 4 --optimized
# No sysctl changes or elevated permissions are applied automatically.
# Record CPU call stacks separately; failure is visible and never ignored.
perf record -g --call-graph fp -o perf.data -- \
    "$build/packetforge" --packets 3000000 --workers 4 --optimized --prebuilt 10000
perf report --stdio -i perf.data > perf-report.txt
# Scheduler tracepoints can require extra kernel permissions; this is an explicit opt-in.
if [[ ${SCHED:-0} == 1 ]]; then
    perf sched record -o perf-sched.data -- "$build/packetforge" --packets 1000000 --workers 4 --optimized
    perf sched latency -i perf-sched.data > perf-sched.txt
fi
