#include "packetforge/dataplane.hpp"
#include "packetforge/traffic.hpp"
#include "test.hpp"
#include <fstream>
#include <filesystem>
TEST(rule_priority_and_all_fields) {
    pf::Rule drop; drop.action = pf::Action::Drop;
    drop.source = 0x0a000000; drop.destination = 0x0b000001;
    drop.source_port = 1024; drop.destination_port = 443; drop.protocol = 6;
    pf::TrafficGenerator gen;
    auto p = gen.packet(0); pf_packet parsed{};
    CHECK(pf_parse_packet(p.data(), p.size(), &parsed) == PF_OK);
    pf::Processor a({drop}); CHECK(!a.accept(parsed, p.size()));
    for (int field = 0; field < 5; ++field) {
        auto other = parsed;
        if (field == 0) ++other.ipv4.source;
        if (field == 1) ++other.ipv4.destination;
        if (field == 2) ++other.source_port;
        if (field == 3) ++other.destination_port;
        if (field == 4) other.ipv4.protocol = 17;
        CHECK(a.accept(other, p.size()));
    }
    pf::Rule allow;
    pf::Processor b({allow, drop}); CHECK(b.accept(parsed, p.size()));
    pf::Processor c({drop, allow}); CHECK(!c.accept(parsed, p.size()));
    pf::Processor disabled; CHECK(disabled.accept(parsed, p.size()));
}
TEST(flow_collision_lru_and_capacity) {
    pf::FlowTable table(4);
    std::vector<pf::FlowKey> keys;
    for (uint32_t i = 0; i < 5; ++i) keys.push_back({i, 2, 3, 4, 17});
    for (size_t i = 0; i < 4; ++i) table.observe(keys[i], 60);
    table.observe(keys[0], 128);
    table.observe(keys[4], 100);
    CHECK(table.active() == 4 && table.evictions() == 1 && table.collisions() == 1);
    bool found = false;
    for (const auto& e : table.entries()) {
        CHECK(!(e.key == keys[1]));
        if (e.key == keys[0]) { found = true; CHECK(e.packets == 2 && e.bytes == 188 && e.last_seen == 5); }
    }
    CHECK(found);
    pf::FlowTable disabled; disabled.observe(keys[0], 1); CHECK(disabled.entries().empty());
    THROWS(pf::FlowTable(3)); THROWS(pf::FlowTable(1048580));
}
TEST(policy_drop_recycling_and_equivalence) {
    for (size_t workers : {size_t(1), size_t(2), size_t(4), size_t(8)}) {
        pf::Config c; c.workers = workers; c.buffers = workers * 2; c.ring_capacity = 1;
        c.flow_capacity = 16; c.batch_size = 64;
        pf::Rule rule; rule.action = pf::Action::Drop; rule.protocol = 17;
        c.rules = {rule}; pf::Dataplane a(c); pf::Baseline b(c);
        pf::TrafficGenerator gen({128, 100, 1, 50});
        for (size_t i = 0; i < 2000; ++i) {
            auto& p = gen.packet(i); CHECK(a.submit(p.data(), p.size())); CHECK(b.submit(p.data(), p.size()));
        }
        auto x = a.finish(), y = b.finish();
        CHECK(x.transmitted == 1000 && x.policy_drops == 1000);
        CHECK(x.recycled == 2000 && !x.buffers_in_use && !x.tx_full);
        CHECK(x.received == x.transmitted + x.dropped);
        CHECK(x.digest == y.digest && x.per_worker == y.per_worker);
        CHECK(x.policy_drops == y.policy_drops && x.flow_evictions == y.flow_evictions);
        CHECK(x.flow_capacity == workers * 16 && x.flows.size() == workers);
        for (size_t w = 0; w < workers; ++w) {
            CHECK(x.flows[w].size() == y.flows[w].size());
            for (size_t i = 0; i < x.flows[w].size(); ++i) {
                const auto& l = x.flows[w][i]; const auto& r = y.flows[w][i];
                CHECK(l.key == r.key && l.packets == r.packets && l.bytes == r.bytes && l.last_seen == r.last_seen);
            }
        }
    }
}
TEST(drop_all_with_pressure_and_outstanding_shutdown) {
    pf::Config c; c.workers = 4; c.buffers = 8; c.ring_capacity = 1;
    pf::Rule rule; rule.action = pf::Action::Drop; c.rules = {rule};
    pf::Dataplane a(c); pf::TrafficGenerator gen;
    for (size_t i = 0; i < 10000; ++i) { auto& p = gen.packet(i); a.submit(p.data(), p.size(), false); }
    auto s = a.finish();
    CHECK(s.received == s.dropped && !s.transmitted && !s.buffers_in_use);
    CHECK(s.dropped == s.policy_drops + s.rx_full + s.pool_exhaustion);
    CHECK(s.recycled == s.policy_drops);
}
TEST(rules_file_validation) {
    const auto path = std::filesystem::temp_directory_path() / ("pf-rules-" + std::to_string(pf::now_ns()));
    struct Cleanup { std::filesystem::path p; ~Cleanup() { std::error_code e; std::filesystem::remove(p, e); } } cleanup{path};
    for (const auto* line : {"DROP * * * * 17", "ALLOW 10.0.0.1 11.0.0.1 0 65535 6 # comment"}) {
        { std::ofstream f(path); f << line; }
        CHECK(pf::load_rules(path.string()).size() == 1);
    }
    for (const auto* line : {"drop * * * * 17", "DROP 1.2.3.256 * * * *", "DROP 1.2.3.4. * * * *",
                             "DROP * * -1 * *", "DROP * * 65536 * *", "DROP * * * * 256", "DROP * * *", "DROP * * * * * extra"}) {
        { std::ofstream f(path); f << line; }
        THROWS(pf::load_rules(path.string()));
    }
}
struct FailingSink : pf::FrameSink {
    bool write(const uint8_t*, size_t) noexcept override { return false; }
};
TEST(egress_failure_is_accounted_and_reclaimed) {
    pf::Config c; c.sink = std::make_shared<FailingSink>();
    pf::Dataplane a(c); pf::Baseline b(c); pf::TrafficGenerator gen;
    for (size_t i = 0; i < 1000; ++i) { auto& p = gen.packet(i); a.submit(p.data(), p.size()); b.submit(p.data(), p.size()); }
    auto x = a.finish(), y = b.finish();
    CHECK(x.egress_drops == 1000 && y.egress_drops == 1000);
    CHECK(!x.transmitted && !x.buffers_in_use && x.recycled == 1000);
}
TEST(batch_larger_than_tx_ring_is_lossless) {
    pf::Config c; c.workers = 1; c.ring_capacity = 2; c.buffers = 4; c.batch_size = 64;
    pf::Dataplane a(c); pf::TrafficGenerator gen;
    for (size_t i = 0; i < 10000; ++i) { auto& p = gen.packet(i); a.submit(p.data(), p.size()); }
    auto s = a.finish(); CHECK(s.transmitted == 10000 && !s.dropped && !s.buffers_in_use);
}

TEST(partial_startup_failure_and_destructor_drain) {
    for (size_t worker : {size_t(1), size_t(2), size_t(4)}) {
        pf::Config c; c.workers = 4; c.faults.fail_start_worker = worker;
        THROWS(pf::Dataplane{c}); THROWS(pf::Baseline{c});
    }
    for (size_t i = 0; i < 20; ++i) {
        pf::Config c; c.workers = 2; c.buffers = 4; c.ring_capacity = 1;
        pf::Dataplane a(c); pf::Baseline b(c); pf::TrafficGenerator gen;
        for (size_t j = 0; j < 20; ++j) { auto& p = gen.packet(j); a.submit(p.data(), p.size()); b.submit(p.data(), p.size()); }
        // Destructors must drain even when finish() is never explicitly called.
    }
}
