#include "packetforge/dataplane.hpp"
#include "packetforge/traffic.hpp"
#include "test.hpp"
TEST(device_rx_tx_full) {
    pf::QueuePair q(1);
    pf::Descriptor a{}, b{};
    CHECK(q.post_rx(a));
    CHECK(!q.post_rx(a));
    CHECK(q.take_rx(b));
    CHECK(!q.take_rx(b));
    CHECK(q.post_tx(a));
    CHECK(!q.post_tx(a));
    CHECK(q.complete_tx(b));
    CHECK(!q.complete_tx(b));
}
TEST(fault_injection_accounting) {
    pf::Config c;
    c.workers = 2;
    c.faults = {7, 5};
    pf::Dataplane d(c);
    pf::TrafficGenerator gen;
    for (size_t i = 0; i < 2000; ++i) {
        auto& p = gen.packet(i);
        d.submit(p.data(), p.size());
    }
    auto s = d.finish();
    CHECK(s.rx_full == 2000 / 7);
    CHECK(s.tx_full > 0);
    CHECK(s.dropped == s.rx_full + s.tx_full);
    CHECK(s.received == s.transmitted + s.dropped);
    CHECK(!s.buffers_in_use);
}
TEST(malformed_and_unsupported_counters) {
    pf::Dataplane d;
    uint8_t tiny[5]{};
    CHECK(!d.submit(tiny, 5));
    pf::TrafficGenerator gen;
    auto p = gen.packet(0);
    p[12] = 0x86;
    p[13] = 0xdd;
    CHECK(!d.submit(p.data(), p.size()));
    auto s = d.finish();
    CHECK(s.malformed == 1);
    CHECK(s.unsupported == 1);
    CHECK(s.dropped == 2);
    CHECK(s.ethernet == 1);
    CHECK(!s.buffers_in_use);
}
TEST(queue_pressure_tiny_pool_no_leaks) {
    pf::Config c;
    c.workers = 2;
    c.buffers = 2;
    c.ring_capacity = 1;
    pf::Dataplane d(c);
    pf::TrafficGenerator g;
    for (size_t i = 0; i < 10000; ++i) {
        auto& p = g.packet(i);
        d.submit(p.data(), p.size(), false);
    }
    auto s = d.finish();
    CHECK(s.received == 10000);
    CHECK(s.received == s.transmitted + s.dropped);
    CHECK(!s.buffers_in_use);
    CHECK(s.dropped == s.pool_exhaustion + s.rx_full + s.tx_full);
}
TEST(oversized_packet_drop) {
    pf::Config c;
    c.buffer_size = 64;
    pf::Dataplane d(c);
    pf::TrafficGenerator g;
    auto& p = g.packet(0);
    CHECK(!d.submit(p.data(), p.size()));
    auto s = d.finish();
    CHECK(s.dropped == 1 && !s.malformed && !s.buffers_in_use);
}
