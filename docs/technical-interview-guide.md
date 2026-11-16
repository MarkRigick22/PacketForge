# Technical interview guide: defend the actual implementation

1. **What publishes payload writes to another thread?**
   `SpscRing::push/pop` (`include/packetforge/ring.hpp`) release-publishes head and
   acquire-observes it. Tail release/acquire protects slot reuse. Relaxed ownership
   CAS validates states; it does not publish payload. Explain happens-before and
   why a descriptor slot can be reused while its referenced payload remains owned.

2. **What exactly is zero-copy here?**
   `Dataplane::submit` (`src/dataplane.cpp`) copies once from source into arena.
   RX/worker/TX exchange descriptor references; no packet-sized copy between them.
   `Baseline` deliberately adds TX vector copying. TAP and file/kernel boundaries
   can copy; no physical DMA, pinned memory or NIC registration exists.

3. **Who owns a packet rejected by policy?**
   `Worker::run` (`src/concurrency/worker.cpp`) owns it after DeviceRx -> Worker.
   It transitions Worker -> Cached via recycle, decrements outstanding count and
   never publishes TX. Explain the new edge in `PacketBuffer::transition` and why
   no thread can reuse the payload until the supply release/acquire handoff.

4. **Can changing atomics make this MPMC?**
   No. `QueuePair` has one ingress and one worker per supply/RX ring; TX producer
   and consumer are the same worker. MPMC requires slot reservation/publication
   protocols beyond atomic indices. `finish` cannot race with public `submit`.

5. **Why four-way flow LRU instead of unordered_map plus a linked list?**
   `FlowTable::observe` (`include/packetforge/processing.hpp`) uses one preallocated
   array and at most four probes; no allocation, rehash or pointer chasing per packet.
   The tradeoff is set-local eviction even when another set is empty. Full tuple
   equality resolves collisions; replacement loses the evicted flow's history.

6. **Is flow accounting connection tracking?**
   No. `Processor::accept` counts valid worker arrivals, including policy rejects;
   tuples are directional. It does not track TCP sequence state, handshake, FIN,
   retransmissions, expiration timers or reverse-flow relationships. RSS ensures
   affinity only for a fixed worker count.

7. **How do policy priority and reconfiguration work?**
   `load_rules` (`src/processing.cpp`) validates a bounded ordered list. First match
   wins, no match allows. Rules are copied to private const vectors before launch;
   there is no live reload. A future update would need versioned publication and
   lifetime management, not mutation of worker-owned storage.

8. **What stops fragmented traffic bypassing a port rule?**
   `pf_parse_packet` (`cnet/packet.c`) rejects MF or nonzero fragment offset before
   constructing a five-tuple. VLAN/IPv6/ARP and non-TCP/UDP are unsupported.
   Explain captured-length versus IPv4 total-length versus transport-header checks,
   and why bounds checks are not checksum verification or reassembly.

9. **Why did batching cause drops with a tiny ring?**
   The old `Worker::run` serviced TX after up to 32 RX packets. A smaller TX ring
   could fill before servicing despite available synchronous completions. It now
   completes before posting further work to a full ring. See regression
   `batch_larger_than_tx_ring_is_lossless` in `tests/test_processing.cpp`.

10. **Is the memory pool lock-free? What is bounded?**
    `MemoryPool::acquire/release` (`src/memory/memory_pool.cpp`) uses a mutex.
    `LocalCache` is thread-confined, reserved before launch and recycles within quota.
    Explain global-free versus cached/preposted buffers, alignment versus physical
    contiguity, and why pool `in_use` must become zero only after complete drain.

11. **How does shutdown handle outstanding packets and partial startup?**
    `Dataplane` stops every created worker then joins. Workers drain RX/TX, reclaim
    unused supply, flush and exit. Constructor catch handles failed starts; the
    injected `fail_start_worker` tests cover positions 1/2/4. Destructors do not
    allocate a report. A stopped peer during blocking submit is outside the API's
    single-host healthy-worker contract; there is no asynchronous cancellation API.

12. **What would a worker exception do?**
    Baseline TX allocation failure is caught and counted in `Baseline`'s worker loop.
    Optimized packet processing preallocates storage and sink writes are noexcept.
    Illegal ownership transitions remain fatal invariant violations if they escape
    a thread; this is not fault-tolerant firmware recovery. Explain which failures
    are expected operational drops versus programmer errors.

13. **Why isn't four workers faster than one?**
    Use `docs/upgrade/performance-results.md`: inline generation, one ingress thread,
    synchronization, scheduler cost and workload size all matter. Prebuilt replay
    experimentally removes one producer cost; near-balanced RSS rules out severe
    uniform-workload imbalance. Context switches increase, but that alone does not
    prove cache contention or a unique cause. No Linux hardware counters were run.

14. **Are the latency percentiles and speedups representative?**
    `Latencies` (`include/packetforge/stats.hpp`) keeps a bounded tail of every 64th
    offered packet that completes. Dropped packets are absent; it is not unbiased
    sampling of all arrivals. CSV throughput includes startup/drain and timed source
    generation unless prebuilt. Compare identical factor tuples and report trial
    variation; never call this wire rate or guaranteed production performance.

15. **How is TAP egress bounded, and what does hardware still require?**
    `Tap::write_frame`/`TapSink` (`src/tap/tap.cpp`, `include/packetforge/tap.hpp`) use
    nonblocking frame writes, bounded EINTR retries, no suffix writes and explicit
    drop accounting. Two distinct TAPs in an isolated namespace avoid direct loops;
    a mutex serializes output. Real hardware would add MMIO/barriers, coherent or
    explicitly synchronized DMA memory, IOMMU mappings, interrupts, reset recovery,
    queue capabilities, offloads and device-specific validation. None is implemented.
