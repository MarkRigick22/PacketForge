#include "packetforge/ring.hpp"
#include "test.hpp"
TEST(ring_empty_full_fifo_wrap) {
    pf::SpscRing<unsigned> r(3);
    unsigned x = 99;
    CHECK(r.empty());
    CHECK(!r.pop(x));
    CHECK(r.capacity() == 3);
    for (unsigned cycle = 0; cycle < 10000; ++cycle) {
        for (unsigned i = 0; i < 3; ++i)
            CHECK(r.push(cycle * 3 + i));
        CHECK(!r.push(99));
        CHECK(r.approximate_size() == 3);
        for (unsigned i = 0; i < 3; ++i) {
            CHECK(r.pop(x));
            CHECK(x == cycle * 3 + i);
        }
        CHECK(r.empty());
    }
}
TEST(ring_one_slot) {
    pf::SpscRing<int> r(1);
    int x;
    CHECK(r.push(4));
    CHECK(!r.push(5));
    CHECK(r.pop(x));
    CHECK(x == 4);
    CHECK(!r.pop(x));
    THROWS(pf::SpscRing<int>(0));
}
