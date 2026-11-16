# Optional isolated Linux TAP output

Default TX remains a simulated sink. Explicit `--tap pf-in --tap-out pf-out` writes
allowed frames through a second TAP descriptor. This is virtual Ethernet I/O, not
physical hardware transmission, driver code, DMA, or end-to-end kernel zero-copy.
Both baseline and optimized engines use the same optional FrameSink.

Safety gates require distinct pf-* names and a network namespace different from
PID 1's namespace. This gate is a guardrail, not a security boundary against an
administrator deliberately wiring namespaces together. Use a fresh isolated namespace;
never bridge either interface to a physical/production network. No automatic routing,
IP forwarding or bridge configuration is performed. A read sees kernel-originated
frames on pf-in; a write injects frames into the kernel at pf-out, so separate,
unbridged interfaces prevent direct reinjection into the ingress reader.

```sh
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --parallel
sudo bash tools/test_tap_namespace.sh build-release/packetforge
```

**Linux namespace demo: NOT EXECUTED on the macOS development host.** The script
creates unique disposable namespace pf-test-PID, two TAPs, disables IPv6 autotraffic,
and uses AF_PACKET sockets to send 100 frames into ingress. With drop-UDP policy,
it asserts 50 byte-identical TCP frames at output, 50 policy drops and zero leaked
buffers. It has deadlines, checks child exit status and deletes the namespace on exit.
No privileged TAP job is part of ordinary CI. Use an isolated Linux VM for this test.

`TapSink::write` serializes worker writes with a mutex; descriptor rings remain SPSC.
`Tap::write_frame` uses nonblocking write, retries EINTR at most three attempts and
counts EAGAIN/errors/short writes as egress drops. A short frame is never retried as a
suffix (TAP is packet-oriented). There is no retained packet queue or unbounded output
retry, so shutdown cannot hang waiting for a writable TAP. Writes copy across the
kernel boundary. No additional application payload copy is introduced in the optimized
path; baseline deliberately retains its existing TX vector copy.

Portable fake-sink tests verify failure accounting/recycling. The namespace test
covers the real successful Linux path, but forced EAGAIN/short-write syscalls have
not been exercised here. Further Linux validation is required before relying on egress.
