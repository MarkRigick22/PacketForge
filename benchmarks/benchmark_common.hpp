#pragma once
#include "packetforge/dataplane.hpp"
#include "packetforge/report.hpp"
#include "packetforge/traffic.hpp"
#include <iostream>
namespace pf {
inline void benchmark(bool baseline, size_t workers, size_t count) {
    Config c;
    c.workers = workers;
    TrafficGenerator gen;
    auto run = [&](auto& engine) {
        for (size_t i = 0; i < count; ++i) {
            auto& p = gen.packet(i);
            engine.submit(p.data(), p.size());
        }
        return engine.finish();
    };
    Stats stats;
    if (baseline) {
        Baseline e(c);
        stats = run(e);
    } else {
        Dataplane e(c);
        stats = run(e);
    }
    if (stats.received != stats.transmitted + stats.dropped || stats.buffers_in_use)
        throw std::runtime_error("benchmark accounting failure");
    csv_row(std::cout, stats, baseline ? "baseline" : "optimized", workers);
}
inline size_t benchmark_count(int argc, char** argv) {
    if (argc > 2)
        throw std::invalid_argument("usage: benchmark [packet_count]");
    if (argc < 2)
        return 100000;
    std::string s = argv[1];
    size_t pos = 0;
    auto n = std::stoull(s, &pos);
    if (s.empty() || s[0] == '-' || pos != s.size())
        throw std::invalid_argument("invalid packet count");
    return static_cast<size_t>(n);
}
} // namespace pf
