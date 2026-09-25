#include "packetforge/dataplane.hpp"
#include "packetforge/traffic.hpp"
#include "test.hpp"
TEST(ring_producer_consumer_stress) {
    pf::SpscRing<uint64_t> q(127);
    constexpr uint64_t count = 500000;
    std::atomic<bool> ok{true};
    std::thread producer([&] {
        for (uint64_t i = 0; i < count; ++i)
            while (!q.push(i))
                std::this_thread::yield();
    });
    std::thread consumer([&] {
        for (uint64_t i = 0; i < count; ++i) {
            uint64_t v = 0;
            while (!q.pop(v))
                std::this_thread::yield();
            if (v != i)
                ok = false;
        }
    });
    producer.join();
    consumer.join();
    CHECK(ok);
    CHECK(q.empty());
}
TEST(concurrent_local_caches_unique_ownership) {
    pf::MemoryPool pool(64, 128);
    std::array<std::atomic<int>, 64> owners{};
    for (auto& v : owners)
        v.store(0);
    std::atomic<bool> ok{true};
    std::vector<std::thread> threads;
    for (int i = 0; i < 8; ++i)
        threads.emplace_back([&] {
            pf::LocalCache cache(pool, 4);
            for (int j = 0; j < 10000; ++j) {
                auto* b = cache.acquire();
                if (!b) {
                    ok = false;
                    continue;
                }
                if (owners[b->id].exchange(1) != 0)
                    ok = false;
                b->data[0] = 99;
                if (owners[b->id].exchange(0) != 1)
                    ok = false;
                cache.recycle(b);
            }
        });
    for (auto& t : threads)
        t.join();
    CHECK(ok);
    CHECK(pool.stats().in_use == 0);
}
TEST(multithread_shutdown_drains_pending) {
    for (size_t workers : {1u, 2u, 4u, 8u}) {
        pf::Config c;
        c.workers = workers;
        c.ring_capacity = 64;
        c.buffers = workers * 128;
        pf::TrafficGenerator g;
        pf::Dataplane d(c);
        for (size_t i = 0; i < 5000; ++i) {
            auto& p = g.packet(i);
            d.submit(p.data(), p.size());
        }
        auto s = d.finish();
        CHECK(s.received == s.transmitted + s.dropped);
        CHECK(s.transmitted == 5000);
        CHECK(s.buffers_in_use == 0);
    }
}
TEST(empty_startup_shutdown) {
    for (int i = 0; i < 20; ++i) {
        pf::Dataplane d;
        auto s = d.finish();
        CHECK(!s.received && !s.buffers_in_use);
    }
}
