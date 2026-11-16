# Actual macOS measurements — 2026-11-16

These are new local **macOS**, not Linux, measurements. Platform: macOS 26.6.2,
arm64, Darwin 25.6.0, eight reported logical CPUs. CPU model/physical core queries
were denied by the environment and are recorded as UNAVAILABLE. Compiler:
AppleClang 21.0.0.21000099; Release, warnings as errors; no sanitizers.

Primary corpus: 3,000,000 packets/trial, 128-byte frames, 1,000 flows, ring 256,
batch 32, processing off, five independent repetitions per case with separate
warm-up processes and alternating engine order. No other build/test jobs were
run concurrently with these final trials. Measured engine intervals ranged from
0.349 to 3.174 seconds, so these remain short desktop experiments with meaningful
scheduler noise; use longer trials and controlled Linux hardware for strong conclusions.

| Source | Engine | Workers | Median Mpps | Min–max Mpps | Sample SD |
|---|---|---:|---:|---:|---:|
| Inline | baseline | 1 | 2.987 | 2.578–3.063 | 0.201 |
| Inline | baseline | 2 | 2.347 | 2.135–2.684 | 0.246 |
| Inline | baseline | 4 | 1.708 | 1.525–1.835 | 0.140 |
| Inline | baseline | 8 | 0.962 | 0.945–1.015 | 0.027 |
| Inline | optimized | 1 | 5.100 | 5.076–5.217 | 0.071 |
| Inline | optimized | 2 | 5.548 | 4.263–5.681 | 0.586 |
| Inline | optimized | 4 | 3.957 | 3.178–4.130 | 0.404 |
| Inline | optimized | 8 | 4.488 | 4.398–4.555 | 0.071 |
| Prebuilt 10,000 | baseline | 1 | 5.001 | 4.289–5.165 | 0.345 |
| Prebuilt 10,000 | baseline | 2 | 3.272 | 2.983–3.373 | 0.148 |
| Prebuilt 10,000 | baseline | 4 | 2.770 | 2.495–2.869 | 0.152 |
| Prebuilt 10,000 | baseline | 8 | 1.129 | 1.098–1.162 | 0.025 |
| Prebuilt 10,000 | optimized | 1 | 6.844 | 6.753–7.061 | 0.116 |
| Prebuilt 10,000 | optimized | 2 | 8.269 | 8.058–8.593 | 0.237 |
| Prebuilt 10,000 | optimized | 4 | 6.331 | 5.830–6.485 | 0.271 |
| Prebuilt 10,000 | optimized | 8 | 7.766 | 6.162–7.962 | 0.731 |

## Evidence and interpretation

* Four workers remain slower than one: optimized inline 5.100 -> 3.957 Mpps
  (about 22.4% lower). This upgrade does not claim to have solved scaling.
* Removing timed packet generation improves optimized four-worker throughput
  to 6.331 Mpps (about 60.0% higher than inline), while one worker reaches
  6.844 Mpps. This is a workload isolation experiment, **not a faster algorithm**.
* At four workers, successful-packet RSS imbalance is only 1.06 (max/mean),
  so severe flow skew is not the explanation for this uniform workload.
* Median involuntary context switches for optimized inline 1/2/4/8 workers:
  1269 / 2619 / 24244 / 482970 per process. Median CPU seconds likewise rise
  1.156 / 1.610 / 3.547 / 4.520. Eight workers plus ingress oversubscribe the
  eight logical CPUs. Scheduling overhead is consistent with the data, not
  uniquely proven as the causal mechanism.
* No cycles/cache-miss/PMU data were collected. Register false sharing, atomic
  costs and supply-ring coherence are code-based hypotheses awaiting Linux perf.
* Flow caches remove routine pool locking after refill; that observation comes
  from the code, not a measured global allocator-contention profile.
* The TX servicing correction is a correctness change backed by the small-ring
  regression, not a claimed performance speedup. No memory ordering was weakened.

## Additional policy workload

The 48 rows in `macos-policy.csv` cover four workers, 64/1500-byte frames,
1/1000 flows, ring 64, batch 8, 0/90% skew, processing enabled, prebuilt corpus
10,000, one million packets and three trials per engine/case. Both engines run
drop-UDP rules plus 1024 flow slots per worker. One-flow default traffic is all
TCP and intentionally produces no policy drops; 1000-flow cases exercise drops.
Compare offered_pps as well as successful pps under skew/filtering. These cases
exercise size, affinity and workload changes; they are not an exhaustive factor
sweep. The reproducible driver supports the full sweep, which was NOT EXECUTED.

All 128 recorded runs satisfy packet accounting with zero final buffers in use.
The primary 80 legacy-workload rows have zero drops. See raw CSV for p50/p95/p99,
per-worker counts, high-water marks, CPU seconds, switches and RSS. No current
result is represented as the historical macOS measurement or a Linux result.

## Reproduction and provenance

Raw files: `macos-scaling.csv`, `macos-policy.csv`; paired `.metadata.json` records
parameters, CMake cache, host and revision. Datapath measurement revision:
`0ce6998`. A subsequent egress-only latency fix excludes failed sink writes from
latency samples; sink-disabled workloads above exercise neither failure path.

```sh
python3 tools/analyze_results.py docs/upgrade/macos-scaling.csv
python3 tools/analyze_results.py docs/upgrade/macos-policy.csv
```

See [methodology](../benchmark-methodology.md) for timing/sampling limitations.
There is no isolated steady-state measurement, CPU pinning or Linux perf result.
Historical and current numbers cannot establish a before/after speedup without
rebuilding both revisions on the same host with identical factor controls.
