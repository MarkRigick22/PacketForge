# Packet processing and invariants

## Pipeline contract

One host thread calls `submit()` and subsequently `finish()`; these operations may
not run concurrently. The host validates Ethernet/IPv4/TCP or UDP, chooses a worker
with `rss_worker(flow_key(packet))`, obtains that worker's supply descriptor, copies
once into its arena buffer and publishes RX. The worker validates again, observes
the flow, applies policy, queues allowed packets to simulated TX, completes TX and
recycles. Both engines run the same `Processor::accept()` in the same position.

Default rules and flow capacity are empty/zero, preserving forwarding-to-sink.
The optional `FrameSink` is called at completion before successful TX accounting.
It may neither throw nor retain the borrowed payload. Flow statistics count valid
worker arrivals, **including policy drops and subsequent TX failures**. `processed`
and `transmitted` retain their historical meaning: successfully completed packets.

## Filtering

`--rules FILE` loads at most 1024 ordered rules before workers start. Each line is:

```text
ACTION SRC DST SPORT DPORT PROTO
ALLOW 10.0.0.1 11.0.0.1 1025 443 6
DROP * * * * 17
```

ACTION is uppercase ALLOW/DROP; addresses are exact dotted IPv4; ports are decimal
0..65535; protocol is decimal 0..255; `*` is a wildcard. Whitespace separates fields,
`#` begins a comment. First matching line wins; unmatched valid packets are allowed.
Add `DROP * * * * *` as the last rule for default deny. No CIDR, ranges, checksum
verification, routing, NAT or connection tracking is implied. Each worker privately
owns an immutable rule vector copied at startup; live rule mutation is unsupported.
Rule evaluation is linear, bounded by the rule limit. There is no per-packet allocation.

Only complete IPv4 TCP/UDP packets reach policy. VLAN (including 802.1Q), IPv6, ARP,
other IP protocols and IPv4 fragments (MF or nonzero offset) are unsupported and
dropped before RSS/policy. Truncation/invalid lengths are malformed. IPv4/TCP options
are length-checked; checksums are not validated. UDP data can be shorter than the IP
payload as in the original parser. Ethernet padding is counted in frame bytes.

## Bounded flows

`--flow-capacity N` selects slots **per worker**, zero disables. Nonzero N must be a
multiple of four and <=1,048,576. Storage is `N * sizeof(FlowEntry)` per worker plus
vector metadata; final Stats snapshots duplicate it for inspection. Large capacities
multiply by worker count: callers must budget memory; construction failures propagate.

`FlowHash(key) % (N/4)` selects one four-slot set. Full keys resolve hash collisions.
A hit increments packet/byte totals and updates worker-local logical sequence.
A miss uses an empty slot, else evicts the least recently seen entry **in that set**.
Four probes bound packet-time work even for adversarial collisions. This is
set-associative LRU, not global LRU: eviction can occur while other sets are empty.
`flow_collisions` counts capacity conflicts requiring eviction (not every unequal
probe); `flow_evictions` counts replacements. No time-based expiration is claimed.
Counts for evicted entries are discarded; global packet counters remain cumulative.
Sequences are comparable only within one worker and reset with each run. They assume
fewer than 2^64 observations per worker lifetime. Directional tuples mean reverse
traffic is a separate flow. This is traffic accounting, not TCP connection tracking.

`Stats::flows[worker]` exposes immutable post-join snapshots with key, packet count,
byte count, last-seen sequence and occupancy. CLI reports aggregate occupancy and
eviction statistics; C++ consumers can inspect every entry. RSS keeps a given tuple
on one worker for a fixed worker count; changing worker count requires a new engine.

## Ownership, pressure and failure

| Event | Ownership transition / outcome |
|---|---|
| Pool acquisition | Free -> Host; worker publishes an empty descriptor on supply |
| Ingress fills buffer | Host -> DeviceRx, release-published on RX |
| Worker takes RX | DeviceRx -> Worker; acquire observes payload |
| Policy rejection | Worker -> Cached, no TX publication |
| Allowed TX | Worker -> DeviceTx; same descriptor and payload |
| Completion, forced TX rejection or sink error | DeviceTx -> Cached |
| Cache reuse | Cached -> Host -> supply |
| Shutdown | Drain RX and TX, pop unused Host supply, cache flush Cached -> Host -> Free |

Normal packet accounting is `received == transmitted + dropped` and final global
`buffers_in_use == 0`. `recycled` counts processed optimized descriptors, excluding
unused supply buffers. Policy, parse, unsupported and egress drops are separate.
Legacy rx_full/tx_full also include corresponding deterministic injected rejections;
actual pressure cannot be inferred from those counters when injection is enabled.
Baseline TX-copy allocation failures are accounted as `allocation_drops` rather than
escaping a worker thread. API misuse/invalid internal ownership transitions remain
programming errors, not recoverable hardware faults.

Batch size defaults to 32 and is configurable. When the TX ring fills inside a
batch, the same worker services completions before posting more packets. This fixes
avoidable losses when batch size exceeds ring capacity. No SPSC role or memory
ordering was weakened. `wait=false` can drop at ingress if supply/RX lacks space;
`wait=true` yields until progress. There is no concurrent cancellation of a blocked
submit; the peer worker must be healthy. TAP writes are nonblocking and bounded.

Workers are stopped and joined on partial startup failure. Destructors drain without
allocating reports. `finish()` joins first and then builds snapshots; report allocation
failure can throw after safe shutdown. There is no live worker-statistics reader.
The pool and baseline queues still use mutexes, and optional shared TAP TX serializes
writes. The entire application is not lock-free.
