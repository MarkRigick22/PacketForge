# Networking

## Ethernet and IPv4

Ethernet II begins with destination MAC (6 bytes), source MAC (6) and EtherType
(2). `0x0800` identifies IPv4. The parser extracts both MAC addresses and type only
after checking all 14 bytes are present. Other EtherTypes, including VLAN and
IPv6, are classified unsupported. Frames exclude Ethernet FCS in the generator.

IPv4 starts with version and IHL. IHL is measured in 32-bit words, so 5 means a
20-byte header. The parser requires version 4, IHL >= 5, the full declared header,
total length >= header length and enough captured bytes for total length. It
extracts source/destination addresses, protocol, TTL and fragment flags/offset.
IPv4 options are skipped by IHL. Ethernet padding beyond IPv4 total length is
ignored when determining transport bounds.

MF or a nonzero fragment offset marks unsupported fragmented input. A fragment
can lack transport ports; hashing it as though it contained a complete TCP/UDP
header would be wrong. DF alone is allowed. Reassembly and reserved-bit policy
are outside this model.

## TCP and UDP

TCP requires at least 20 bytes. Its data offset is also in 32-bit words. It must
be >= 5 and fit in the IP-bounded transport bytes. Ports, header length and flags
are extracted; options are skipped safely. There is no TCP connection state.

UDP requires 8 bytes. Its declared length must be >= 8 and no greater than the
available IP payload. The parser accepts extra IP payload beyond the UDP length;
it does not assume all captured bytes belong to UDP. Ports and length are extracted.
Protocol identifiers are 6 for TCP and 17 for UDP; other IPv4 protocols are unsupported.

## Byte order and safety

Network fields are big-endian. The C helpers assemble unsigned integers byte by
byte; they never cast unaligned bytes to packed structs. Every layer bounds the
next layer using validated lengths, rather than trusting a packet's claim or
using the whole Ethernet capture as transport length. No pointer is advanced
before its header is known to be present. Null data is treated as truncated.

Malformed means internally invalid lengths/version; truncated means insufficient
bytes. Both increment the CLI's malformed counter. Unsupported means a recognized
boundary with a protocol this engine does not handle. The parser does not verify
received IPv4/TCP/UDP checksums, Ethernet FCS, TTL expiry, or every protocol semantic.
The generators nevertheless produce valid IPv4 and transport checksums, including
the IPv4 pseudo-header for TCP/UDP and odd-byte padding for checksum arithmetic.

## Five-tuple and RSS-style distribution

The key is `(source IPv4, destination IPv4, source port, destination port, protocol)`.
Equality compares fields explicitly. Hashing serializes their network-order bytes
and applies 64-bit FNV-1a followed by an avalanche mix. It never hashes struct padding.
The queue is `hash(key) % workers`. Direction matters: reversing the tuple need not
select the same worker. Changing the worker count can remap a flow.

Keeping a directional flow on one FIFO worker preserves order among accepted
packets and keeps any future flow state local. There is no per-flow state table in
this implementation. Drops can create sequence gaps. This is RSS-style distribution,
not exact NIC Toeplitz hashing or an indirection-table implementation. Tests verify
same-key mapping and approximate balance over 10,000 distinct keys.

The synthetic generator assigns protocol by flow id (`flow % 100 < TCP percent`).
Thus the ratio is exact over full groups of 100 flow ids, approximate otherwise;
a configuration with one flow necessarily chooses one transport protocol. Payloads
are deterministic for a seed, flow id and frame size. PCAP timestamps are validated
but replay is as fast as the host allows, without inter-packet pacing.
