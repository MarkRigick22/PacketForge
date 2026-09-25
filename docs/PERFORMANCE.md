# Performance and measurement

## What is being compared

| Aspect | Baseline | Optimized |
|---|---|---|
| Ingress storage | A new vector per packet | Preallocated arena buffer |
| Handoff | Bounded mutex/condition queue | SPSC descriptor ring |
| TX | New vector and payload copy | Same buffer, TX descriptor |
| Reclaim | Vector destruction | Worker cache, supply ring |
| Queue steering | C parse + deterministic hash | Same |
| Worker work | C parse + completion digest | Same |
| Wait strategy | Condition variables | Poll/yield |
| Instrumentation | Sampled latency, private counters | Same plus ownership/register model |

Baseline is correct and intentionally simple. It is not a deliberately broken
competitor: bounded queues avoid runaway memory use and preserve order. The models
have different scheduler behavior and the optimized one does extra device-model
bookkeeping. Report the measurements; neither design is guaranteed to win.

## Reproducible commands

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
PACKETS=1000000 REPEATS=5 ./tools/run_benchmarks.sh build benchmark_results.csv
python3 tools/analyze_results.py benchmark_results.csv
./build/benchmark_scaling 100000 > benchmark_results_scaling.csv
```

The script runs two short warmup processes, then separate measured processes per
mode and worker count. It alternates mode order by trial. Separate processes mean
`ru_maxrss` represents each run; the standalone scaling executable runs sequentially
inside one process, so its peak RSS column is a cumulative process high-water mark.
Keep machine load, CPU power state, compiler, packet size, flow count, buffer count,
ring capacity and repetition count with every result. Use long runs and repeated
measurements for conclusions. Do not benchmark sanitizers or run CPU-heavy tasks
alongside measurements.

Reported time begins immediately before worker startup and ends after draining.
It includes source generation, parsing, ingress copy, scheduling, processing and
shutdown; it excludes arena allocation and final percentile sorting. Thus it is
an end-to-end workload benchmark, not an isolated forwarding capacity or a wire
line-rate test. For large payloads, source generation/checksumming can dominate.

## Metrics

- Packets/s = successful completions / elapsed seconds.
- Gb/s = completed Ethernet bytes * 8 / seconds / 1e9. It excludes wire overhead
  such as FCS, preamble and inter-frame gap, so it is approximate payload throughput.
- Latency starts at submit entry before parse/backpressure and ends at software TX
  completion. It is not wire, kernel or round-trip latency.
- Every 64th received packet is timed with a monotonic clock. Each worker keeps at
  most 8192 samples, replacing its oldest samples circularly. Percentiles aggregate
  these bounded samples, sort after join and use nearest-rank selection.
- p50 describes the sample median; p95/p99 describe slower sampled packets. Only
  successful, selected packets contribute. Recent per-worker capped windows can
  bias long/skewed runs; this is not an unbiased full-run quantile estimator.
- Peak RSS is process resident memory high-water from `getrusage` (Darwin bytes,
  Linux KiB converted to bytes). Arena bytes are reserved payload capacity and
  exclude metadata, rings, stacks, cached buffers' metadata and allocator overhead.
  Demand paging means arena reservation and resident memory are not interchangeable.

`processed` and `transmitted` both mean successful simulated completions. Protocol
counters describe successfully classified ingress, even if later dropped. Drops
partition into malformed, unsupported, oversized (derived), supply pressure,
RX rejection/full and TX rejection/full. Per-worker drops are TX-side drops; host
rejections have not entered a worker. High-water marks are approximate host samples.

## Cache locality and contention

Arena buffers avoid repeated payload allocation and tend to reuse a bounded
working set. Worker caches remove repeated shared-free-list locking. Moving short
descriptors saves copying large payloads. Separate index lines reduce unrelated
producer/consumer writes sharing a cache line. These mechanisms can reduce costs,
but should not be presented as measured cache-miss reductions without counters.

Real costs remain: one ingress copy, two parses, per-packet ownership CAS, atomic
register updates, head/tail coherence traffic, hashing, source checksums and idle
worker polling. A single ingress thread can saturate while extra workers add
scheduling overhead. A busy flow stays on one worker. On heterogeneous CPUs and
shared machines, more workers may be slower. Quotas can leave unused buffers tied
to idle queues. These tradeoffs can limit scaling even when per-packet allocation is reduced.

## Linux perf

No Linux profiling result is implied by the existence of these commands.

```sh
cmake -S . -B build-profile -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_CXX_FLAGS=-fno-omit-frame-pointer
cmake --build build-profile -j
perf stat -r 5 -e cycles,instructions,cache-references,cache-misses,context-switches,branches,branch-misses \
  ./build-profile/packetforge --synthetic --packets 1000000 --workers 4 --optimized
perf stat -r 5 -e cycles,instructions,cache-references,cache-misses,context-switches,branches,branch-misses \
  ./build-profile/packetforge --synthetic --packets 1000000 --workers 4 --baseline
perf record -g -- ./build-profile/packetforge --synthetic --packets 1000000 --workers 4 --optimized
perf report
```

Cycles/instructions help estimate instructions per cycle; cache/branch events can
highlight locality and prediction costs. Context switches distinguish blocking
from busy polling. Counter availability, virtualization, kernel permissions and
multiplexing affect interpretation. Normalize counters per completed packet and
compare equal workloads. If perf is denied, ask the machine administrator about
policy; the helper never changes `perf_event_paranoid` or other system settings.

On macOS, `/usr/bin/time -l` supplies runtime, RSS, page-fault and context-switch
observations. It cannot substitute for Linux hardware cache-counter evidence.

## Interpreting comparisons

The optimized datapath replaces per-packet allocations, copies and mutex queues
with preallocated descriptor transport and local buffer reuse. Benchmark results
quantify that tradeoff for a particular workload; they do not establish a universal
speedup, physical NIC throughput or reduced cache misses without hardware-counter
evidence. Preserve the workload configuration and repeated measurements when
comparing changes. Higher reserved or resident memory can be a deliberate cost of
preallocation even when throughput improves.
