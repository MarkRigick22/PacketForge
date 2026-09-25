#include "cnet/packet.h"
#include "packetforge/traffic.hpp"
#include "test.hpp"
#include <random>
TEST(parse_valid_tcp_udp_and_checksums) {
    for (unsigned ratio : {0u, 100u}) {
        pf::TrafficGenerator gen({129, 100, 42, ratio});
        auto p = gen.packet(3);
        pf_packet out{};
        CHECK(pf_parse_packet(p.data(), p.size(), &out) == PF_OK);
        CHECK(out.has_ethernet && out.has_ipv4);
        CHECK(out.ipv4.header_length == 20);
        CHECK(out.ipv4.total_length == 115);
        CHECK(out.ipv4.ttl == 64);
        CHECK(out.ipv4.protocol == (ratio ? 6 : 17));
        CHECK(out.source_port == 1027);
        CHECK(pf_checksum(p.data() + 14, 20) == 0);
        std::vector<uint8_t> pseudo(p.begin() + 26, p.begin() + 34);
        pseudo.push_back(0);
        pseudo.push_back(out.ipv4.protocol);
        auto len = p.size() - 34;
        pseudo.push_back(len >> 8);
        pseudo.push_back(len & 255);
        pseudo.insert(pseudo.end(), p.begin() + 34, p.end());
        CHECK(pf_checksum(pseudo.data(), pseudo.size()) == 0);
    }
}
TEST(parse_every_truncation) {
    for (unsigned ratio : {0u, 100u}) {
        pf::TrafficGenerator gen({128, 1, 1, ratio});
        auto p = gen.packet(0);
        pf_packet out{};
        for (size_t n = 0; n < p.size(); ++n)
            CHECK(pf_parse_packet(p.data(), n, &out) != PF_OK);
    }
}
TEST(parse_malformed_network_headers) {
    pf::TrafficGenerator gen;
    auto good = gen.packet(0);
    pf_packet out{};
    auto p = good;
    p[14] = 0x44;
    CHECK(pf_parse_packet(p.data(), p.size(), &out) == PF_MALFORMED);
    p = good;
    p[14] = 0x65;
    CHECK(pf_parse_packet(p.data(), p.size(), &out) == PF_MALFORMED);
    p = good;
    p[16] = 0;
    p[17] = 19;
    CHECK(pf_parse_packet(p.data(), p.size(), &out) == PF_MALFORMED);
    p = good;
    p[16] = 0xff;
    p[17] = 0xff;
    CHECK(pf_parse_packet(p.data(), p.size(), &out) == PF_TRUNCATED);
    p = good;
    p[12] = 0x86;
    p[13] = 0xdd;
    CHECK(pf_parse_packet(p.data(), p.size(), &out) == PF_UNSUPPORTED);
    p = good;
    p[20] = 0x20;
    CHECK(pf_parse_packet(p.data(), p.size(), &out) == PF_UNSUPPORTED);
    p = good;
    p[21] = 1;
    CHECK(pf_parse_packet(p.data(), p.size(), &out) == PF_UNSUPPORTED);
    p = good;
    p[23] = 1;
    CHECK(pf_parse_packet(p.data(), p.size(), &out) == PF_UNSUPPORTED);
    p = good;
    p[46] = 0x40;
    CHECK(pf_parse_packet(p.data(), p.size(), &out) == PF_MALFORMED);
    p = good;
    p.resize(54);
    p[17] = 40;
    p[46] = 0xf0;
    CHECK(pf_parse_packet(p.data(), p.size(), &out) == PF_TRUNCATED);
}
TEST(parse_udp_lengths_and_padding) {
    pf::TrafficGenerator gen({128, 1, 1, 0});
    auto good = gen.packet(0);
    pf_packet out{};
    auto p = good;
    p[38] = 0;
    p[39] = 7;
    CHECK(pf_parse_packet(p.data(), p.size(), &out) == PF_MALFORMED);
    p = good;
    p[38] = 255;
    CHECK(pf_parse_packet(p.data(), p.size(), &out) == PF_TRUNCATED);
    p = good;
    p.resize(160, 0xff);
    CHECK(pf_parse_packet(p.data(), p.size(), &out) == PF_OK);
    pf_udp u{};
    CHECK(pf_parse_udp(good.data() + 34, 7, &u) == PF_TRUNCATED);
    pf_tcp t{};
    CHECK(pf_parse_tcp(good.data() + 34, 19, &t) == PF_TRUNCATED);
}
TEST(parse_ipv4_tcp_options) {
    pf::TrafficGenerator gen;
    auto p = gen.packet(0);
    p.insert(p.begin() + 34, 4, 0);
    p[14] = 0x46;
    p[17] += 4;
    pf_packet out{};
    CHECK(pf_parse_packet(p.data(), p.size(), &out) == PF_OK);
    CHECK(out.ipv4.header_length == 24);
    p[50] = 0x60;
    CHECK(pf_parse_packet(p.data(), p.size(), &out) == PF_OK);
}
TEST(parse_random_bytes_safely) {
    std::mt19937 random(42);
    std::vector<uint8_t> p(512);
    pf_packet out{};
    for (int i = 0; i < 30000; ++i) {
        size_t n = random() % p.size();
        for (size_t j = 0; j < n; ++j)
            p[j] = random() & 255;
        auto r = pf_parse_packet(p.data(), n, &out);
        CHECK(r >= PF_OK && r <= PF_UNSUPPORTED);
    }
}
TEST(generator_deterministic) {
    pf::TrafficGenerator a({128, 1000, 9, 50}), b({128, 1000, 9, 50}), c({128, 1000, 10, 50});
    CHECK(a.packet(123) == b.packet(123));
    CHECK(a.packet(123) != c.packet(123));
    THROWS(pf::TrafficGenerator({10, 1, 1, 50}));
}
