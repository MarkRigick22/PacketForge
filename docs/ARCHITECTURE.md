# Architecture

PacketForge has one host/ingress thread and a configurable number of worker
threads. All stages are ordinary software. The C parser is stateless; the C++
components own resources and orchestrate descriptors.

## Components and responsibilities

`MemoryPool` allocates an arena and stable metadata at construction. Its mutex
protects the global free list. `LocalCache` is owned by a single worker and holds
reusable buffers without synchronization. Each worker has a quota of
`min(total_buffers / workers, 2 * ring_capacity)` outstanding buffers. Leftover
buffers remain global; a quiet flow's queue does not lend its reservation to a
busy queue. Quotas deliberately trade utilization for predictable ownership.

`Device` owns a `QueuePair` per worker. Each pair has three rings: empty-buffer
supply, RX and TX. Registers track status/control, monotonically accumulated
head/tail operation counts, and doorbell counts. These are software diagnostics,
not memory-mapped device registers. Head/tail registers count successful queue
operations; they are not the modulo indices used internally by `SpscRing`.

`Dataplane` is called by a single host thread. It selects a queue with the C parser
and RSS hash, obtains one preposted buffer, copies bytes into it once and posts RX.
`Worker` owns its cache, consumes RX and services simulated TX. `Baseline` implements
the same routing and parsing workload using allocated vectors and bounded mutex queues.

## Complete packet lifecycle

```text
Global free list
  | worker acquires (occasional global mutex)
  v
Worker cache -> Host-owned descriptor on supply ring
                                      |
Traffic source -> parse -> RSS -> host pops empty buffer
                                      |
                             copy ingress frame once
                                      |
                      HOST -> DEVICE_RX; RX publication
                                      |
                         worker pops RX descriptor
                                      |
                      DEVICE_RX -> WORKER; parse/check
                                      |
                     WORKER -> DEVICE_TX; TX publication
                                      |
                    same thread services simulated device
                                      |
                    completion stats + latency + cache recycle
                                      |
                      Cached -> Host -> supply ring again
```

The preclassification step models receive steering. It reads the source's bytes
before the arena ingress copy; the worker parses again to model processing after
RX delivery. Invalid or unsupported input is dropped at preclassification and
never consumes an arena buffer. A descriptor contains a stable buffer pointer,
length, optional timestamp, flags and a snapshot ownership label; it has no payload.
The buffer's checked state is authoritative.

## RX and TX behavior

Ingress defaults to waiting for supply/ring space and yields while waiting. With
`--drop-on-pressure`, it checks RX capacity and attempts to pop a supply descriptor,
then drops if unavailable. Only the host produces RX, so after it observes space,
no competing producer can consume that space. Worker RX processing uses batches
of up to 32. A full TX ring drops the current packet and recycles it. This is
observable with very small rings; no loss-free claim is made for that configuration.

Successful TX publication increments a software doorbell. Completion consumes
that same descriptor and same payload allocation. Because the worker both posts
and completes TX in this model, TX is a bounded single-thread device queue; it
still uses the same genuine SPSC implementation tested across threads for RX and
supply. Doorbells are counters, not interrupts or wakeup mechanisms.

## Shutdown and failures

`finish()` is called only after the host stops submitting. It release-publishes
stop to every worker, joins them, aggregates private statistics and stops the
device. Workers finish all queued RX/TX, drain unused supply buffers and flush
caches to the global pool. `finish()` is idempotent; submit after finish throws.
Destructors also drain. Worker creation failure stops and joins already-started
threads. Ownership violations throw (including Release), so invariant failures
inside a worker are fail-fast programming errors, not silently ignored packets.

Malformed packets, unsupported protocols, pressure and injected device rejections
are normal input outcomes with counters. Invalid configuration, corrupt PCAP files,
missing files and unavailable adapters are descriptive exceptions caught by the CLI.
Packet accounting after drain is `received == transmitted + dropped`. Processed
and transmitted count successful software completions, not wire transmissions.

## Resource and threading boundaries

Sources reuse a scratch vector; the optimized engine never retains its pointer.
PCAP record scratch allocation and the initial ingress copy are outside the
interstage zero-copy claim. The baseline creates a vector per packet and another
at TX. Both feed the same C parser, hash and completion digest workload.

The CLI handles SIGINT/SIGTERM using a signal-safe flag. TAP poll wakes every
100 ms to check it. Current accepted work drains before exit. An offline source
is unpaced and never changes network interfaces.
