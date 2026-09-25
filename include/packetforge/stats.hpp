#pragma once
#include "cnet/packet.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <vector>
namespace pf {
inline uint64_t now_ns() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                     std::chrono::steady_clock::now().time_since_epoch())
                                     .count());
}
struct Latencies {
    std::array<uint64_t, 8192> samples{};
    size_t seen = 0;
    void add(uint64_t ns) {
        samples[seen++ % samples.size()] = ns;
    }
    void append(std::vector<uint64_t>& v) const {
        v.insert(v.end(), samples.begin(), samples.begin() + std::min(seen, samples.size()));
    }
};
struct alignas(64) WorkerStats {
    uint64_t packets = 0, bytes = 0, drops = 0, digest = 0;
    Latencies latency;
};
struct Stats {
    uint64_t received = 0, processed = 0, transmitted = 0, bytes = 0, dropped = 0, malformed = 0,
             unsupported = 0;
    uint64_t ethernet = 0, ipv4 = 0, tcp = 0, udp = 0, rx_full = 0, tx_full = 0,
             pool_exhaustion = 0, digest = 0;
    double seconds = 0, p50_us = 0, p95_us = 0, p99_us = 0;
    size_t buffers_total = 0, buffers_in_use = 0, pool_failures = 0, arena_bytes = 0;
    std::vector<uint64_t> per_worker, worker_bytes, worker_drops, queue_high_water;
    double pps() const {
        return seconds > 0 ? processed / seconds : 0;
    }
    void percentiles(std::vector<uint64_t> v) {
        if (v.empty())
            return;
        std::sort(v.begin(), v.end());
        auto p = [&](size_t n) { return v[(v.size() * n + 99) / 100 - 1] / 1000.0; };
        p50_us = p(50);
        p95_us = p(95);
        p99_us = p(99);
    }
};
inline bool classify(const uint8_t* p, size_t n, pf_packet& parsed, Stats& s) {
    auto r = pf_parse_packet(p, n, &parsed);
    s.ethernet += parsed.has_ethernet;
    s.ipv4 += parsed.has_ipv4;
    if (r != PF_OK) {
        ++s.dropped;
        if (r == PF_UNSUPPORTED)
            ++s.unsupported;
        else
            ++s.malformed;
        return false;
    }
    if (parsed.ipv4.protocol == 6)
        ++s.tcp;
    else
        ++s.udp;
    return true;
}
} // namespace pf
