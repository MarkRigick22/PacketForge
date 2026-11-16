# PacketForge

**High-Performance NIC Firmware & Packet Processing Engine — a software model.**

PacketForge is a C11/C++17 systems project for studying descriptor-based packet
processing, explicit buffer ownership, queue concurrency and measurable tradeoffs.
The baseline and optimized implementations process the same Ethernet/IPv4/TCP/UDP
traffic and expose packet accounting, throughput, sampled latency and memory metrics.

## What It Is

A software NIC/dataplane simulator with a firmware-style descriptor model. It is
**not physical NIC firmware, a kernel driver, or actual hardware DMA**.

> This project models DMA-style ownership and descriptor-based communication in software.

The optimized path makes one ingress copy into an arena, then moves descriptors,
not payloads, through RX, worker processing and simulated TX completion. This is
zero-copy **between application dataplane stages**, not kernel or hardware zero-copy.

## Architecture

```text
Synthetic / classic PCAP / optional Linux TAP
                      |
           bounds-checked C parser
           five-tuple / RSS-style hash
                      |
        shared, aligned packet arena
        worker-preposted empty buffers
                      |
              one ingress copy
                      |
       +--------------+--------------+
       |              |              |
   RX ring 0      RX ring 1      RX ring N
       |              |              |
      W0             W1             WN
       |              |              |
   TX ring 0      TX ring 1      TX ring N
       |              |              |
   simulated device completions (serviced by each worker)
       |              |              |
   local cache    local cache    local cache
       +------> empty-buffer supply rings ------> ingress
```

Ingress parses the source bytes to select the queue; each worker validates the
arena frame again. Both benchmark modes do this same parsing work. TX is a
software sink, not transmission onto a wire. See [architecture](docs/ARCHITECTURE.md).

## Major Features

- Contiguous, 64-byte-aligned, fixed-buffer arena; configurable count and capacity.
- Thread-confined worker caches backed by a synchronized global pool.
- Bounded SPSC supply/RX/TX rings, acquire/release publication, separated indices.
- Checked buffer state transitions and software registers/doorbell counters.
- Portable C parsers, five-tuple equality/hash and deterministic RSS-style routing.
- Configurable worker threads, bounded queues, draining shutdown and pressure drops.
- Baseline using per-packet allocations, an extra TX copy and mutex/condition queues.
- Deterministic valid synthetic traffic, classic PCAP replay and optional Linux TAP.
- Counters, per-worker totals, bounded latency sampling, CSV benchmarks and Linux perf commands.
- Offline CTest harness, ownership/concurrency stress tests and deterministic fault injection.

## Build

Requires CMake 3.16+, a C11/C++17 compiler and a threading runtime. Python 3 is
needed only for the helper tools. No downloaded frameworks or libraries.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

The portable core builds on macOS and Linux. Linux TAP is selected at configure
time. Built-in PCAP can be disabled with `-DPACKETFORGE_PCAP=OFF`; it never
requires libpcap. Disable TAP explicitly with `-DPACKETFORGE_TAP=OFF`.

## Run

```sh
./build/packetforge --help
./build/packetforge --synthetic --packets 10000 --workers 4
./build/packetforge --synthetic --packets 1000000 --workers 4 --flows 10000
./build/packetforge --synthetic --workers 4 --baseline
./build/packetforge --synthetic --workers 4 --optimized --csv
./build/packetforge --synthetic --size 1500 --tcp-percent 25 --seed 42
python3 tools/generate_packets.py traffic.pcap --packets 10000
./build/packetforge --pcap traffic.pcap --workers 4
```

Default ingress applies backpressure; `--drop-on-pressure` exposes supply/RX
pressure. `--packets` limits every source (default 100000), including PCAP/TAP.
Synthetic minimum size is 60 bytes, excluding Ethernet FCS. Use `--buffer-size`
when replaying or generating frames larger than the default 2048-byte capacity.

On Linux only, with an appropriately configured TAP:

```sh
sudo ./build/packetforge --tap tap0 --workers 4 --packets 10000
```

This reads Ethernet frames into the same RX path and completes TX in the simulated
sink; it does **not** echo frames into the kernel. See [safe manual TAP setup](examples/README.md).
No network configuration or elevated operation is performed by the build/tests.

## Tests

```sh
ctest --test-dir build --output-on-failure
./build/packetforge_tests
./build/packetforge --synthetic --fault-rx-every 7 --fault-tx-every 5
./build/packetforge --synthetic --buffers 4 --workers 4 --ring-size 1 --drop-on-pressure
```

Tests cover pool exhaustion/double release, cache ownership, all frame truncation
points, malformed headers, random parser inputs, RSS distribution, SPSC wrap and
500,000-element concurrent FIFO stress, full RX/TX rings, descriptor identity
through a real worker, baseline/optimized equivalence, draining shutdown, PCAP
endianness and malformed records. Assertions stay enabled in Release builds.

## Benchmarks

```sh
./build/benchmark_baseline 100000
./build/benchmark_optimized 100000
./build/benchmark_scaling 100000 > benchmark_results_scaling.csv
PACKETS=100000 REPEATS=3 ./tools/run_benchmarks.sh build benchmark_results.csv
python3 tools/analyze_results.py benchmark_results.csv
```

The script uses separate processes, alternates mode order, and tests 1/2/4/8 workers.
It reports actual results and does not assume the optimized mode wins. Measurements
include traffic generation, worker startup and draining, but exclude construction
of the arena itself. Latency samples include waiting at ingress. See
[measurement methodology](docs/PERFORMANCE.md) and [local validation](docs/VALIDATION.md).

## Profiling

On Linux with perf installed and counters permitted:

```sh
./tools/profile_linux.sh build
perf record -g -- ./build/packetforge --synthetic --packets 1000000 --workers 4 --optimized
perf report
```

For useful stacks, build `RelWithDebInfo` with `-DCMAKE_CXX_FLAGS=-fno-omit-frame-pointer`.
The documentation describes counter limitations; no cache data is fabricated.

## Sanitizers

```sh
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DPACKETFORGE_ASAN=ON -DPACKETFORGE_UBSAN=ON
cmake --build build-asan -j
ctest --test-dir build-asan --output-on-failure

cmake -S . -B build-tsan -DCMAKE_BUILD_TYPE=Debug -DPACKETFORGE_TSAN=ON
cmake --build build-tsan -j
ctest --test-dir build-tsan --output-on-failure
```

TSAN must use a separate build. Sanitizer runtime support depends on the host.
Do not use sanitizer builds for performance comparisons.

## Design Decisions

The global pool uses a mutex because its free list has multiple worker users;
local caches avoid this lock during steady-state reuse. Only the rings are claimed
to be lock-free. Worker quotas prevent one queue from taking all buffers. SPSC
queues have one explicitly assigned producer and consumer. TX completion runs in
the worker to keep this model small; it is not an independent hardware engine.

The parser reads bytes, never unaligned packed structs. Flow hashing serializes
fields explicitly. Software register atomics provide observable counters, while
the ring atomics—not the doorbells—publish descriptor data.

## Limitations

- Software simulation, no physical firmware, DMA addresses, IOMMU, MMIO or interrupts.
- RSS-style FNV-based directional hashing, not hardware Toeplitz/vendor RSS.
- One ingress thread; no claim of linear worker scaling or line-rate throughput.
- Ethernet II / IPv4 / TCP / UDP only; VLAN, IPv6, fragmented IPv4, ARP and other
  protocols are unsupported. No reassembly, routing, TCP state or retransmission.
- Header bounds are validated; received checksums, FCS and TTL policy are not enforced.
- PCAP is classic Ethernet PCAP (not pcapng), unpaced, with captured lengths preserved.
- TX completion is simulated; TAP receives into a sink and cannot serve as a bridge.
- Lock-free queues do not make the entire system lock-free. Statistics sampling,
  software doorbells, double parsing and polling carry measurable overhead.
- Benchmarks are short local observations; Linux perf and live TAP need Linux validation.

## Repository Structure

| Directory | Purpose |
|---|---|
| `cnet/` | C protocol parsing and checksum helpers |
| `include/packetforge/` | Public C++ structures and interfaces |
| `src/` | Arena, device, worker, baseline, traffic, PCAP, TAP and CLI |
| `tests/` | Local test harness; no online dependencies |
| `benchmarks/` | Baseline, optimized and scaling drivers |
| `tools/` | PCAP generation, benchmark analysis and Linux profiling |
| `examples/` | Reproducible traffic/fault/TAP walkthroughs |
| `docs/` | Architecture, memory, concurrency, networking, performance and interview guide |
| `validation/` | Generated local evidence, ignored by `.gitignore` |

MIT licensed. This delivery is entirely local; no repository or publication step is required.
