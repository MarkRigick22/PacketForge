# Reproducible benchmark methodology

Build Release without sanitizers; use RelWithDebInfo + PACKETFORGE_PROFILE for perf.
Do not compare sanitizer runs with Release. Do not run benchmarks alongside builds,
tests or other compute jobs. Shared laptops/runners are noisy; report variation.

```sh
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DPACKETFORGE_WARNINGS_AS_ERRORS=ON
cmake --build build-release --parallel
python3 tools/benchmark_matrix.py --binary build-release/packetforge --output scaling.csv --packets 3000000 --repeats 5 --sizes 128 --flows 1000 --rings 256 --batches 32 --skews 0 --policies off --prebuilt 0,10000
python3 tools/analyze_results.py scaling.csv
```

The full driver supports workers 1/2/4/8, sizes 64/128/512/1500, flows 1/1000/10000,
rings 64/256, batches 8/32/128, skew 0/90 percent, processing off/on, inline or
prebuilt traffic. Pass comma-separated subsets to isolate factors. Defaults form a
large Cartesian product; reduced example above is intentional. Oversubscribed worker
counts are allowed and must be identified relative to metadata's logical core count.
One ingress thread also consumes CPU; eight workers on eight cores oversubscribes.

Every case has a separate unrecorded warm-up process for each engine. Each repetition
starts a fresh process, alternating engine order. This warms OS/code caches but does
not exclude cold per-process startup. Median, minimum, maximum and sample standard
deviation describe independent trials. The analyzer groups ALL experimental factors;
it never mixes sizes, policies, skew or prebuilt modes in a reported speedup.

## What is timed

`seconds` runs from engine startup bookkeeping through drain, aggregation and joins;
some member allocations occur before the constructor's start timestamp. It is an
**engine lifetime** measurement, not whole process wall time, wire rate, or isolated
steady-state service rate. Inline traffic generation is timed. `--prebuilt N`
constructs a corpus before engine creation, removing generation from the interval.
It still includes startup, ingress parsing/copy, RSS, synchronization and drain, and
is NOT a steady-state metric. No steady-state throughput is reported in this upgrade.
Increase packet count and compare different durations to assess fixed startup costs.

Prebuilt replay repeats a corpus: choose N >= requested flow count to cover all
flows. `--skew-percent 90` maps 90 out of every 100 packet ordinals to flow zero,
with the remainder drawn from the normal sequence (not a probabilistic Zipf model).
Hash collisions and that deterministic remainder can produce additional skew.

`pps` and Mpps count successful TX. `offered_pps` counts received frames/time and
must also be considered when policy intentionally rejects traffic. `gbps` is
successful Ethernet frame bytes * 8 / time / 1e9, excluding FCS, preamble and IFG.
This is software byte throughput, not NIC line rate. Both engines receive identical
rules/capacity and run the same parser + flow/policy work. Baseline's deliberate
payload copy, allocation and mutex queues remain the controlled implementation
contrast. Egress/TAP benchmarks must be separated from simulated sink runs.

## Metrics and interpretation

Latency samples every 64th offered packet, from before ingress parsing/wait to TX
completion. Only successful packets contribute. Each worker retains the most recent
8192 samples; this is a bounded tail window, NOT an unbiased whole-run reservoir.
P50/P95/P99 merge retained samples; pressure or policy rejection can bias coverage.

Per-worker transmitted counts and RX advisory/TX high-water values use semicolon
lists in CSV. Imbalance is max successful worker count / mean, zero for no successes.
Policy drops can affect this metric; `Stats::worker_drops` complements it in C++.
RX independent snapshots may underestimate the true peak. TX is worker-local.

Peak RSS, user+system CPU seconds and voluntary/involuntary context switches are
**whole-process** getrusage values, including setup and prebuilt corpus construction.
CPU seconds divided by engine seconds therefore is not exact utilization; use Linux
perf task-clock/elapsed or an external process monitor for utilization. Counters do
not establish cache-line contention causes without hardware profiling.

## Controlled hypotheses

* Inline vs prebuilt: tests generation cost in the single producer, not multi-ingress scalability.
* Uniform vs one-flow/90% skew: tests affinity and load imbalance, not lock contention alone.
* Batch/ring sweep: tests servicing/backpressure and batching; retain accounting checks.
* Worker sweep: exposes scheduling/coherence/producer limits; no required monotonic speedup.
* Rule/flow on/off: estimates added workload cost; compare like with like between engines.

Pool caches eliminate routine arena allocation in steady state, but supply/RX still
transfer cache-line ownership. Diagnostic register increments and ownership CAS cost
CPU. Producer and worker counters in `Registers` may share lines; ring head/tail are
64-byte aligned (not universal hardware cache-line size). These are testable hypotheses,
not claimed causes. No speculative memory-order relaxation or fabricated speedup was made.
