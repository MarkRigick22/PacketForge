#include "packetforge/dataplane.hpp"
#include "packetforge/traffic.hpp"
#include "test.hpp"
TEST(dataplane_end_to_end_and_baseline_equivalence) {
    pf::Config c;
    c.workers = 4;
    pf::Dataplane optimized(c);
    pf::Baseline baseline(c);
    pf::TrafficGenerator gen;
    for (size_t i = 0; i < 10000; ++i) {
        auto& p = gen.packet(i);
        CHECK(optimized.submit(p.data(), p.size()));
        CHECK(baseline.submit(p.data(), p.size()));
    }
    auto a = optimized.finish(), b = baseline.finish();
    CHECK(a.processed == 10000);
    CHECK(a.received == a.transmitted);
    CHECK(a.dropped == 0);
    CHECK(a.buffers_in_use == 0);
    CHECK(a.bytes == 1280000);
    CHECK(a.tcp == 5000 && a.udp == 5000);
    CHECK(a.digest == b.digest);
    CHECK(a.per_worker == b.per_worker);
    CHECK(a.processed == b.processed);
    CHECK(a.p99_us >= a.p50_us);
    CHECK(optimized.finish().processed == 10000);
    THROWS(optimized.submit(nullptr, 0));
}
TEST(descriptor_zero_copy_identity_and_registers) {
    pf::MemoryPool pool(1, 128);
    pf::LocalCache cache(pool);
    pf::Device device(1, 4);
    device.start();
    auto& q = device.queue(0);
    auto* b = cache.acquire();
    auto* storage = b->data;
    b->length = 64;
    b->data[63] = 42;
    b->transition(pf::Ownership::Host, pf::Ownership::DeviceRx);
    CHECK(q.post_rx({b, 64, 0, 0, pf::Ownership::DeviceRx}));
    pf::Descriptor d;
    CHECK(q.take_rx(d));
    CHECK(d.buffer == b && d.buffer->data == storage);
    d.buffer->transition(pf::Ownership::DeviceRx, pf::Ownership::Worker);
    d.buffer->transition(pf::Ownership::Worker, pf::Ownership::DeviceTx);
    d.ownership = pf::Ownership::DeviceTx;
    CHECK(q.post_tx(d));
    CHECK(q.complete_tx(d));
    CHECK(d.buffer == b && d.buffer->data == storage && d.buffer->data[63] == 42);
    cache.recycle(d.buffer);
    cache.flush();
    CHECK(pool.stats().in_use == 0);
    CHECK(q.registers.rx_doorbell == 1);
    CHECK(q.registers.tx_doorbell == 1);
    CHECK(q.registers.rx_head == q.registers.rx_tail);
    CHECK(q.registers.tx_head == q.registers.tx_tail);
    device.stop();
    CHECK(q.registers.status == pf::DeviceStatus::Stopped);
}
TEST(dataplane_single_flow_affinity) {
    pf::Dataplane d;
    pf::TrafficGenerator gen({128, 1, 1, 100});
    for (size_t i = 0; i < 1000; ++i) {
        auto& p = gen.packet(i);
        d.submit(p.data(), p.size());
    }
    auto s = d.finish();
    size_t active = 0;
    for (auto n : s.per_worker)
        active += n != 0;
    CHECK(active == 1);
    CHECK(s.transmitted == 1000);
}
TEST(dataplane_invalid_configuration) {
    pf::Config c;
    c.workers = 0;
    THROWS(pf::Dataplane{c});
    c.workers = 4;
    c.buffers = 2;
    THROWS(pf::Dataplane{c});
}

TEST(worker_zero_copy_round_trip) {
    pf::MemoryPool pool(1, 128);
    pf::QueuePair q(2);
    pf::Worker worker(q, pool, 1, {});
    pf::TrafficGenerator gen;
    auto packet = gen.packet(0);
    worker.start();
    pf::Descriptor first;
    while (!q.supply.pop(first))
        std::this_thread::yield();
    auto* buffer = first.buffer;
    auto* storage = buffer->data;
    std::copy(packet.begin(), packet.end(), storage);
    buffer->length = packet.size();
    first.length = packet.size();
    first.ownership = pf::Ownership::DeviceRx;
    buffer->transition(pf::Ownership::Host, pf::Ownership::DeviceRx);
    CHECK(q.post_rx(first));
    pf::Descriptor recycled;
    while (!q.supply.pop(recycled))
        std::this_thread::yield();
    bool same = recycled.buffer == buffer && recycled.buffer->data == storage &&
                std::equal(packet.begin(), packet.end(), storage);
    recycled.length = packet.size();
    recycled.buffer->length = packet.size();
    recycled.ownership = pf::Ownership::DeviceRx;
    recycled.buffer->transition(pf::Ownership::Host, pf::Ownership::DeviceRx);
    CHECK(q.post_rx(recycled));
    worker.stop();
    worker.join();
    CHECK(same);
    CHECK(worker.stats.packets == 2);
    CHECK(pool.stats().in_use == 0);
}
