#!/usr/bin/env python3
"""Generate checksum-valid classic Ethernet PCAP without third-party packages."""
import argparse
import random
import struct


def checksum(data):
    if len(data) % 2:
        data += b'\0'
    value = sum(struct.unpack('!%dH' % (len(data) // 2), data))
    while value >> 16:
        value = (value & 65535) + (value >> 16)
    return (~value) & 65535


def frame(index, size, flows, seed, tcp_percent):
    flow = index % flows
    tcp = flow % 100 < tcp_percent
    protocol = 6 if tcp else 17
    src, dst = 0x0a000000 + (flow & 0xffffff), 0x0b000001
    sport, dport = 1024 + flow % 60000, 443 if tcp else 53
    length = size - 34
    rng = random.Random(seed ^ flow)
    payload = bytes(rng.randrange(256) for _ in range(length - (20 if tcp else 8)))
    if tcp:
        transport = struct.pack('!HHIIBBHHH', sport, dport, index & 0xffffffff, 0, 0x50, 0x18, 65535, 0, 0) + payload
    else:
        transport = struct.pack('!HHHH', sport, dport, length, 0) + payload
    pseudo = struct.pack('!IIBBH', src, dst, 0, protocol, length)
    check = checksum(pseudo + transport)
    if not tcp and check == 0:
        check = 65535
    offset = 16 if tcp else 6
    transport = transport[:offset] + struct.pack('!H', check) + transport[offset + 2:]
    ip = struct.pack('!BBHHHBBHII', 0x45, 0, size - 14, index & 65535, 0, 64, protocol, 0, src, dst)
    ip = ip[:10] + struct.pack('!H', checksum(ip)) + ip[12:]
    ethernet = bytes.fromhex('0200000000010200000000020800')
    return ethernet + ip + transport


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output')
    parser.add_argument('--packets', type=int, default=10000)
    parser.add_argument('--size', type=int, default=128)
    parser.add_argument('--flows', type=int, default=1000)
    parser.add_argument('--seed', type=int, default=1)
    parser.add_argument('--tcp-percent', type=int, default=50)
    parser.add_argument('--truncate-every', type=int, default=0, help='Capture only 10 bytes every Nth packet for fault testing')
    args = parser.parse_args()
    if not (60 <= args.size <= 65549 and 1 <= args.flows <= 0xffffffff and 0 <= args.tcp_percent <= 100 and args.packets >= 0 and args.truncate_every >= 0):
        parser.error('invalid size, flows, ratio, packet count or fault period')
    with open(args.output, 'wb') as stream:
        stream.write(struct.pack('<IHHIIII', 0xa1b2c3d4, 2, 4, 0, 0, 65549, 1))
        for i in range(args.packets):
            packet = frame(i, args.size, args.flows, args.seed, args.tcp_percent)
            captured = packet[:10] if args.truncate_every and (i + 1) % args.truncate_every == 0 else packet
            stream.write(struct.pack('<IIII', i // 1000000, i % 1000000, len(captured), len(packet)))
            stream.write(captured)
    print(f'Wrote {args.packets} Ethernet frames to {args.output}')


if __name__ == '__main__':
    main()
