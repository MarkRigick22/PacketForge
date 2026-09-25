# PacketForge

PacketForge is a C11/C++17 software model of a high-performance NIC datapath,
exploring descriptor-based packet processing, preallocated memory management,
RSS-style flow distribution, concurrent worker queues, and zero-copy buffer ownership.

It is a **software NIC/dataplane simulator** with firmware-style behavior. It models
DMA-style ownership; it is **not physical NIC firmware and does not perform hardware
DMA**. Zero-copy applies between application dataplane stages after one ingress copy.

## Highlights

- C/C++ systems programming with a portable core and optional Linux networking integration.
- RX/TX descriptor rings, software device registers and doorbell counters.
- A contiguous, aligned packet arena with per-worker buffer caches and checked ownership.
- Lock-free SPSC rings using atomics, acquire/release ordering and cache-aware index layout.
- Bounds-checked Ethernet, IPv4, TCP and UDP parsers implemented in C.
- Five-tuple classification and deterministic RSS-style distribution across worker threads.
- Allocation/copy/mutex baseline and preallocated descriptor-based optimized datapath.
- Deterministic synthetic traffic, classic PCAP replay and optional Linux TAP input.
- Fault injection, packet accounting, draining shutdown, CTest and sanitizer support.
- Throughput, sampled latency, memory metrics and repeatable CSV benchmarks.

## Architecture

```text
       Synthetic traffic / classic PCAP / Linux TAP
                            |
                   Ingress parser + RSS
                            |
              Preposted buffers from aligned arena
                            |
                     One ingress copy
                            |
             +--------------+--------------+
             |              |              |
         RX ring 0      RX ring 1      RX ring N
             |              |              |
            W0             W1             WN
             |              |              |
         TX ring 0      TX ring 1      TX ring N
             |              |              |
       Simulated TX completions, serviced by each worker
             |              |              |
         Local cache    Local cache    Local cache
             +--------> Supply rings ------> ingress
```

Ingress parses source bytes to choose a queue; workers validate the arena frame
again. Both comparison modes perform this parsing work. TX completes in a software
sink, not on a physical network. See [architecture](docs/ARCHITECTURE.md).

## Memory / Ownership Model

```text
FREE -> HOST -> DEVICE_RX -> WORKER -> DEVICE_TX -> CACHED
          ^                                          |
          +------------- local reuse ----------------+

CACHED -> HOST -> FREE    (cache flush / pool release)
```

The arena is allocated at startup. Descriptors reference stable buffers rather than
containing packet copies. A buffer retains the same payload allocation through RX,
processing and TX; completion returns it to a worker-local cache. Atomic state checks
detect invalid ownership transitions, while ring acquire/release operations publish
data between threads. This is simulated DMA-style ownership, not pinned memory or
device DMA. See [memory](docs/MEMORY_MODEL.md) and [concurrency](docs/CONCURRENCY.md).

## Baseline vs Optimized

| Baseline | Optimized |
|---|---|
| Per-packet dynamic payload allocation | Preallocated, aligned packet arena |
| Bounded mutex/condition-variable queues | Bounded lock-free SPSC descriptor rings |
| Additional payload copy at TX | Same buffer through RX, worker and TX |
| Per-packet vector destruction | Worker-local recycling and supply rings |

The global pool uses a mutex; worker-local reuse avoids it during steady-state
processing. Only the SPSC rings are claimed to be lock-free. Performance depends
on packet size, flow distribution, worker count and scheduling; the benchmark tools
measure both implementations without assuming a particular speedup.

## Build

Requires CMake 3.16+, a C11/C++17 compiler and a threading runtime. Python 3 is
needed only for helper tools. There are no downloaded framework dependencies.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

The core is portable across macOS and Linux. TAP is compiled only on Linux when
enabled. Use `-DPACKETFORGE_TAP=OFF` to disable it. Classic PCAP has a built-in reader
with no libpcap dependency; disable it with `-DPACKETFORGE_PCAP=OFF`.

## Run

```sh
./build/packetforge --help
./build/packetforge --synthetic --packets 100000 --workers 4
./build/packetforge --synthetic --packets 100000 --workers 4 --baseline
./build/packetforge --synthetic --packets 100000 --workers 4 --optimized
./build/packetforge --synthetic --packets 100000 --size 1500 --flows 10000 --seed 42
python3 tools/generate_packets.py traffic.pcap --packets 10000
./build/packetforge --pcap traffic.pcap --workers 4
```

Default ingress waits for space. `--drop-on-pressure` exposes supply/RX pressure.
`--packets` limits every source (default 100000), including PCAP and TAP. Frames
exclude Ethernet FCS; use `--buffer-size` for frames larger than the default 2048
bytes. PCAP replay is unpaced and supports classic Ethernet PCAP, not pcapng.

On Linux, with an appropriately configured TAP interface and permissions:

```sh
sudo ./build/packetforge --tap tap0 --workers 4 --packets 10000
```

TAP feeds the same RX path and completes TX in a simulated sink; it does not echo
or bridge frames. Creating or attaching an interface may require root/CAP_NET_ADMIN.
See [isolated TAP setup and traffic examples](examples/README.md).

## Tests

```sh
ctest --test-dir build --output-on-failure
./build/packetforge_tests
./build/packetforge --synthetic --packets 10000 --fault-rx-every 7 --fault-tx-every 5
```

The local harness covers buffer exhaustion and double release, cache ownership,
SPSC FIFO/wraparound/concurrency, parser bounds and malformed frames, RSS distribution,
actual worker buffer reuse, draining shutdown, baseline equivalence and PCAP records.
CTest also runs optimized and baseline CLI smoke tests. Checks remain active in
Release builds; no online testing dependency is required.

## Benchmark Snapshot

Representative local results, measured on 2026-09-25. Each value is the median
of three separate Release-build runs; these are not cross-machine performance claims.

**Environment:** Apple silicon (arm64), macOS 26.6.2, Apple Clang 21.0.0.
**Workload:** 500,000 packets per run, 128-byte frames, 1,000 flows, 50/50 TCP/UDP,
seed 1; 256-entry queues and 4,096 × 2,048-byte buffers in the optimized arena.

| Datapath | Workers | Throughput (Mpps) | p50 (µs) | p95 (µs) | p99 (µs) | Drops |
|---|---:|---:|---:|---:|---:|---:|
| Baseline | 1 | 2.69 | 7.50 | 29.83 | 50.62 | 0 |
| Optimized | 1 | 5.16 | 0.25 | 3.29 | 37.79 | 0 |
| Baseline | 4 | 1.76 | 3.46 | 19.38 | 33.21 | 0 |
| Optimized | 4 | 4.29 | 0.75 | 5.96 | 26.83 | 0 |

- Optimized median throughput was **1.92×** baseline with one worker and
  **2.43×** with four workers, using the same traffic workload.
- Four workers had lower median throughput than one in both modes for this workload.
- All 12 runs completed all packets with zero drops and no unreclaimed buffers.

Timings include traffic generation, startup and drain. Latency is sampled from
submit to simulated completion; short runs showed throughput and tail-latency variability.
See [measurement methodology](docs/PERFORMANCE.md). These local software-simulation
results do not represent physical NIC or hardware-DMA performance.

## Benchmarks

```sh
./build/benchmark_baseline 100000
./build/benchmark_optimized 100000
./build/benchmark_scaling 100000 > benchmark_results_scaling.csv
PACKETS=100000 REPEATS=3 ./tools/run_benchmarks.sh build benchmark_results.csv
python3 tools/analyze_results.py benchmark_results.csv
```

The script compares 1/2/4/8 workers with separate measured processes and alternating
mode order. Results include packets/s, approximate Gb/s, sampled p50/p95/p99 latency,
drops, arena capacity and process peak RSS. Timings include source generation,
worker startup and drain, but exclude arena allocation. See
[performance methodology](docs/PERFORMANCE.md) for sampling and measurement limits.

## Profiling

On Linux with perf installed and counters permitted:

```sh
cmake -S . -B build-profile -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_CXX_FLAGS=-fno-omit-frame-pointer
cmake --build build-profile -j
./tools/profile_linux.sh build-profile
perf record -g -- ./build-profile/packetforge --synthetic --packets 1000000 --workers 4 --optimized
perf report
```

The helper collects cycles, instructions, cache references/misses, context switches
and branch statistics. Counter availability and permissions depend on the host;
the script does not change system profiling policy.

## Sanitizers

```sh
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DPACKETFORGE_ASAN=ON -DPACKETFORGE_UBSAN=ON
cmake --build build-asan -j
ctest --test-dir build-asan --output-on-failure

cmake -S . -B build-tsan -DCMAKE_BUILD_TYPE=Debug -DPACKETFORGE_TSAN=ON
cmake --build build-tsan -j
ctest --test-dir build-tsan --output-on-failure
```

TSAN uses a separate build and cannot be combined with ASAN/UBSAN here. Runtime
support depends on the platform. Use Release builds for performance comparisons.

## Repository Structure

| Directory | Purpose |
|---|---|
| `cnet/` | C protocol parsers and checksum helpers |
| `include/packetforge/` | C++ interfaces, descriptors, rings and statistics |
| `src/` | Memory, device, workers, dataplanes, traffic sources and CLI |
| `tests/` | Offline correctness, fault and concurrency tests |
| `benchmarks/` | Baseline, optimized and scaling benchmark drivers |
| `tools/` | Synthetic PCAP generation, result analysis and Linux profiling |
| `examples/` | Reproducible traffic and isolated TAP examples |
| `docs/` | Architecture, memory, concurrency, networking and performance |

## Limitations

- Software simulation: no physical NIC, real hardware DMA, IOMMU mappings or interrupts.
- RSS-style directional software hashing, not a vendor-specific RSS implementation.
- One ingress thread; additional workers do not guarantee linear scaling.
- Ethernet II / IPv4 / TCP / UDP only. VLAN, IPv6, IPv4 fragments, ARP and other
  protocols are unsupported; there is no routing, reassembly or TCP connection state.
- Header lengths are checked; received checksums, FCS and TTL policy are not enforced.
- TX completion is a software sink. TAP is optional, Linux-specific and not a bridge.
- Latency percentiles use bounded samples of successful packets, not full-run wire latency.
- Queue operations are lock-free; the entire application is not.

Licensed under the [MIT License](LICENSE).
