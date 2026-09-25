# Examples

Run from the PacketForge directory after a Release build.

## Synthetic and pressure

```sh
./build/packetforge --synthetic --packets 10000 --workers 4
./build/packetforge --synthetic --packets 10000 --workers 4 --baseline
./build/packetforge --synthetic --packets 10000 --workers 4 --optimized
./build/packetforge --synthetic --packets 10000 --flows 1 --tcp-percent 100
./build/packetforge --synthetic --packets 10000 --workers 4 --fault-rx-every 7 --fault-tx-every 5
./build/packetforge --synthetic --packets 10000 --workers 4 --buffers 4 --ring-size 1 --drop-on-pressure
```

The last example deliberately offers more traffic than tiny supply queues can
accept. Drops are expected. After shutdown, received = transmitted + drops and
buffers-in-use = 0. Exact pressure drops depend on scheduling.

## Classic PCAP replay

```sh
python3 tools/generate_packets.py traffic.pcap --packets 10000 --flows 1000 --seed 42
./build/packetforge --pcap traffic.pcap --workers 4
python3 tools/generate_packets.py broken.pcap --packets 1000 --truncate-every 10
./build/packetforge --pcap broken.pcap --workers 2
```

The truncated capture has exactly 100 malformed frames and 900 valid completions
at default queue sizes. PCAP record corruption itself is a file error rather than
an input packet drop. Use `--packets` larger than the capture count to read to EOF.

## Optional Linux TAP, manual only

Use an isolated development machine or network namespace. These commands are
provided for you to run; PacketForge does not run setup commands or alter routes.
An existing TAP can be owned by your user. Creating/attaching interfaces otherwise
requires root or CAP_NET_ADMIN. Do not attach this sink to a production bridge.

One isolated setup (run only on Linux, with permission to create a namespace):

```sh
sudo ip netns add pf-demo
sudo ip netns exec pf-demo ip tuntap add dev tap0 mode tap
sudo ip netns exec pf-demo ip link set tap0 up
sudo ip netns exec pf-demo ip addr add 192.0.2.1/24 dev tap0
# A static neighbor permits the kernel to send UDP without needing an ARP reply.
sudo ip netns exec pf-demo ip neigh replace 192.0.2.2 lladdr 02:00:00:00:00:02 dev tap0 nud permanent
sudo ip netns exec pf-demo ./build/packetforge --tap tap0 --workers 2 --packets 10
```

While PacketForge is running, in another terminal at the same project directory:

```sh
sudo ip netns exec pf-demo python3 -c 'import socket; s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM); [s.sendto(b"PacketForge",("192.0.2.2",9000)) for _ in range(10)]'
```

The virtual interface may also emit unsupported protocols; counters make these
visible and `--packets` counts all received frames. Ctrl-C stops and drains. The
engine reads kernel-emitted frames and completes them in a simulated TX sink;
it does not return replies or bridge traffic. This avoids echo loops.

Manual cleanup after stopping PacketForge:

```sh
sudo ip netns del pf-demo
```

The TAP adapter requires a Linux build and appropriate interface permissions.
Non-Linux builds provide an explicit unsupported-platform error.
