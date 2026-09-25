#include "packetforge/rss.hpp"
#include "test.hpp"
#include <array>
TEST(rss_determinism_and_distribution) {
    std::array<unsigned, 8> count{};
    for (uint32_t i = 0; i < 10000; ++i) {
        pf::FlowKey f{i, 0x0b000001, static_cast<uint16_t>(i), 443, 6};
        auto worker = pf::rss_worker(f, 8);
        CHECK(worker == pf::rss_worker(f, 8));
        CHECK(f == f);
        ++count[worker];
    }
    for (auto n : count)
        CHECK(n > 950 && n < 1550);
    THROWS(pf::rss_worker({1, 2, 3, 4, 6}, 0));
}
