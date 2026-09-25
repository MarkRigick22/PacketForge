# Concurrency and ordering

## SPSC contract

SPSC means single producer, single consumer. PacketForge uses this topology:

| Ring | Producer | Consumer |
|---|---|---|
| Empty-buffer supply for worker i | Worker i | Host ingress |
| RX for worker i | Host ingress | Worker i |
| TX for worker i | Worker i (processor role) | Worker i (device completion role) |

Only RX and supply transfer data between threads today. Sharing one ring among
multiple producers would violate the contract; changing indices to atomics alone
would not make that safe. Public submit/finish calls must come from one host thread
and must not run concurrently with each other.

## Head, tail, wrap and capacity

Head identifies the next producer slot; tail identifies the next consumer slot.
The ring allocates `capacity + 1` slots and deliberately leaves one empty, so
head==tail means empty and next(head)==tail means full. Increment uses modulo the
storage length. No power-of-two capacity is required, no unbounded counter can
overflow, and the API's capacity is fully usable.

Producer and consumer frequently written indices are aligned separately to 64
bytes. Slots are allocated before processing. `approximate_size()` takes two
independent atomic snapshots: it is diagnostic, not a globally consistent size.
The host's nonblocking capacity check is safe under the single-producer contract
because only that host can fill an empty RX slot; the consumer can only free more.

## Why acquire/release is necessary

Producer push:

1. Load its own head with relaxed ordering.
2. Acquire-load consumer tail before deciding the next slot is free.
3. Write the ordinary descriptor slot (and finish earlier payload writes).
4. Release-store the new head to publish the descriptor and prior writes.

Consumer pop:

1. Load its own tail relaxed.
2. Acquire-load producer head to discover published entries.
3. Read the ordinary descriptor and later access its payload.
4. Release-store the next tail, allowing the producer to reuse the descriptor slot.

A release store that is observed by an acquire load establishes a happens-before
edge. Head publication protects consumer reads; tail publication protects the
producer from overwriting a descriptor before the consumer copied it. Neither
`volatile` nor compiler ordering alone supplies these guarantees. Separate manual
fences are unnecessary because the acquire/release operations carry the required
ordering. Actual instructions vary by CPU (x86 and ARM need not emit identical
barriers).

The payload can outlive the descriptor slot: pop copies the descriptor value and
frees that slot, but the consumer still owns the referenced buffer. The buffer
cannot be reused until it travels through TX completion and the supply ring.
That separate lifecycle is what prevents payload overwrite races.

## Lock-free scope

The queue has no locks, allocation, blocking syscalls or retry loops inside
push/pop. Index atomics must be always lock-free; a static assertion rejects
unsupported targets. Each try operation does bounded work. An external caller
that loops waiting for space can still wait indefinitely if its peer stops.
Do not describe blocking submit as wait-free or the whole engine as lock-free.

The global pool has a mutex and baseline queues use mutexes/condition variables.
OS scheduling, memory allocation at construction and thread joins can block.
These facts do not invalidate the narrower SPSC claim.

## Statistics, registers and sharing

Host counters are host-only. Worker counters and bounded latency arrays are
worker-only and read only after thread join. Join synchronizes completed worker
writes with aggregation; they do not need per-packet atomic increments. Worker
statistics and ring indices are padded to limit false sharing. False sharing
occurs when unrelated writes share a coherence line and force ownership transfers.
64-byte alignment is a practical choice, not a portable cache-line discovery API.

Software registers use atomics because host and worker touch their counters.
Doorbells are relaxed diagnostic increments: they do not publish packets and are
not used as wakeups. Ring ordering supplies synchronization. The atomic ownership
CAS catches misuse; its relaxed ordering is not a replacement for ring barriers.

## Stop and drain

After the last submit, the host release-stores stop. Each worker acquire-loads it,
continues processing until RX is empty, completes TX, reclaims unused supply,
flushes its cache and exits. The host joins every worker before reading statistics
or destroying the arena. Startup exceptions stop already-started threads.
The worker checks stop after consuming a batch; this keeps draining predictable.

Tests stress 500,000 ordered queue transfers, eight concurrent cache users,
1/2/4/8-worker packet accounting and immediate shutdown. Run TSAN separately;
passing tests and race detection provide evidence, not a proof for all schedules.
