# Memory model

## Arena and metadata

At startup `MemoryPool` allocates one contiguous payload arena with C++17 aligned
allocation. The stride rounds configured capacity up to a multiple of 64 bytes.
Each stable `PacketBuffer` metadata record has a payload pointer, capacity, length,
id, owning-pool pointer and atomic ownership. Metadata records are also aligned
to 64 bytes. This is a chosen cache-aware alignment, not an assertion that every
CPU's hardware cache line is 64 bytes. On machines with larger cache lines the
padding may not fully prevent false sharing.

The arena is virtual process memory; it is not pinned, physically contiguous,
registered with a device, or mapped by an IOMMU. Buffer identifiers are metadata
indices, not physical addresses. Payload storage is never allocated or freed per
packet in the optimized engine.

## Ownership state machine

```text
 FREE --pool.acquire--> HOST --RX post--> DEVICE_RX
                          ^                  |
                          |                  v
                     supply ring           WORKER
                          |                  |
                          |                  v
                       CACHED <----------- DEVICE_TX
                          |
                          +--cache flush--> HOST --pool.release--> FREE
```

An acquired but unused `HOST` buffer can be returned to the pool or cached.
`DEVICE_RX -> HOST` is an explicit rollback transition reserved for canceled
publication; normal ingress does not need it. Duplicate release, cross-pool
release and transitions outside the allowed graph throw in all build modes.
A cache also rejects duplicate recycling. These checks assume the supplied
pointer refers to a live `PacketBuffer`; they cannot make arbitrary stale/raw
pointers safe to dereference.

The state CAS is relaxed: it checks ownership, but does not publish payload
writes. Ring release/acquire operations perform inter-thread publication. The
pool mutex orders global free-list reuse. Confusing state checks with memory
publication would leave a race even if every state label looked correct.

## Worker-local caches

Each worker exclusively accesses its `LocalCache`. Cache recycle/acquire needs no
global lock; startup/refill or overflow/flush calls the synchronized global pool.
The worker preposts empty buffers on its supply ring. The host temporarily owns
those buffers while filling and posting RX; it never touches the worker's cache
vector directly. The worker later consumes RX, completes TX and regains the buffer.

Quota counts include buffers in supply, held by the host, RX and TX. Cached buffers
remain assigned to the worker. Together outstanding plus cached buffers never
exceed its quota. Normally the same buffers cycle repeatedly without another pool
lock. The shared pool remains useful for initial allocation, other cache users,
spill and final reclamation; it is deliberately not claimed to be lock-free.

Pool `in_use` means every buffer outside the **global** free list, including cached
or preposted empty buffers. It is not the number of live packets. At a completed
shutdown, in-use must be zero. Pool allocation failures count actual empty global
free-list acquisitions; ingress `pool_exhaustion` counts empty worker supply in
nonblocking mode and can happen even when the global pool has free space. Human
output labels that counter `pool_pressure` to make the distinction explicit.

## Zero-copy scope and lifetime

The traffic source owns scratch storage. The host copies once into the acquired
arena buffer. RX, worker and TX descriptors all reference the same `PacketBuffer`
and payload address. No packet-sized vector is created between those stages.
The source may immediately reuse its scratch vector after submit returns.

This project models DMA-style ownership and descriptor-based communication in
software. Real DMA lets a device access system memory without the CPU copying
those bytes. PacketForge does not do that. Its zero-copy claim is confined to
application dataplane stage transfers. PCAP file I/O and TAP kernel I/O may copy.

`worker_zero_copy_round_trip` checks payload address, buffer identity, contents,
completion count and final global reclamation through an actual running worker.
Concurrent cache tests independently check that no buffer is leased to two workers
at once. ASAN/UBSAN and TSAN builds complement, but cannot prove, lifetime safety.
