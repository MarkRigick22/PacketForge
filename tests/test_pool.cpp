#include "packetforge/memory_pool.hpp"
#include "test.hpp"
#include <set>
TEST(pool_acquire_exhaustion_reuse) {
    pf::MemoryPool pool(4, 128);
    std::vector<pf::PacketBuffer*> b;
    for (int i = 0; i < 4; ++i) {
        auto* p = pool.acquire();
        CHECK(p);
        CHECK(reinterpret_cast<uintptr_t>(p->data) % 64 == 0);
        b.push_back(p);
    }
    CHECK(!pool.acquire());
    CHECK(pool.stats().failures == 1);
    CHECK(pool.stats().in_use == 4);
    std::set<void*> unique;
    for (auto* p : b)
        unique.insert(p->data);
    CHECK(unique.size() == 4);
    auto* last = b.back();
    for (auto* p : b)
        pool.release(p);
    CHECK(pool.stats().available == 4);
    auto* again = pool.acquire();
    CHECK(again == last);
    pool.release(again);
    CHECK(pool.stats().in_use == 0);
}
TEST(pool_invalid_release_and_transitions) {
    pf::MemoryPool a(2, 128), b(1, 128);
    auto* p = a.acquire();
    THROWS(b.release(p));
    THROWS(p->transition(pf::Ownership::Host, pf::Ownership::Worker));
    a.release(p);
    THROWS(a.release(p));
    THROWS(a.release(nullptr));
    CHECK(a.stats().available == 2);
    THROWS(pf::MemoryPool(0, 128));
    THROWS(pf::MemoryPool(2, 4));
}
TEST(cache_recycles_and_flushes) {
    pf::MemoryPool p(4, 128);
    {
        pf::LocalCache cache(p, 2);
        auto* a = cache.acquire();
        auto* b = cache.acquire();
        auto* c = cache.acquire();
        cache.recycle(a);
        THROWS(cache.recycle(a));
        cache.recycle(b);
        cache.recycle(c);
        CHECK(p.stats().in_use == 2);
        auto* d = cache.acquire();
        CHECK(d == c);
        cache.recycle(d);
    }
    CHECK(p.stats().available == 4);
}
