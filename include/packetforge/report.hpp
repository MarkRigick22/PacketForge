#pragma once
#include "stats.hpp"
#include <iomanip>
#include <ostream>
#if defined(__unix__) || defined(__APPLE__)
#include <sys/resource.h>
#endif
namespace pf {
inline uint64_t peak_rss_bytes() {
#if defined(__unix__) || defined(__APPLE__)
    struct rusage r{};
    if (getrusage(RUSAGE_SELF, &r) != 0)
        return 0;
#ifdef __APPLE__
    return static_cast<uint64_t>(r.ru_maxrss);
#else
    return static_cast<uint64_t>(r.ru_maxrss) * 1024;
#endif
#else
    return 0;
#endif
}
inline void csv_header(std::ostream& o) {
    o << "mode,workers,received,processed,transmitted,bytes,drops,seconds,pps,gbps,p50_us,p95_us,"
         "p99_us,arena_bytes,peak_rss_bytes,buffers_in_use,malformed,unsupported,ethernet,ipv4,tcp,"
         "udp,rx_full,tx_full,pool_exhaustion,pool_failures,digest\n";
}
inline void csv_row(std::ostream& o, const Stats& s, const char* mode, size_t workers) {
    o << std::setprecision(10) << mode << ',' << workers << ',' << s.received << ',' << s.processed
      << ',' << s.transmitted << ',' << s.bytes << ',' << s.dropped << ',' << s.seconds << ','
      << s.pps() << ',' << (s.seconds ? s.bytes * 8 / s.seconds / 1e9 : 0) << ',' << s.p50_us << ','
      << s.p95_us << ',' << s.p99_us << ',' << s.arena_bytes << ',' << peak_rss_bytes() << ','
      << s.buffers_in_use << ',' << s.malformed << ',' << s.unsupported << ',' << s.ethernet << ','
      << s.ipv4 << ',' << s.tcp << ',' << s.udp << ',' << s.rx_full << ',' << s.tx_full << ','
      << s.pool_exhaustion << ',' << s.pool_failures << ',' << s.digest << '\n';
}
inline void human_report(std::ostream& o, const Stats& s, const char* mode) {
    o << "PacketForge (" << mode << ", software NIC model)\nreceived=" << s.received
      << " processed=" << s.processed << " transmitted=" << s.transmitted << " drops=" << s.dropped
      << " bytes=" << s.bytes << "\nEthernet=" << s.ethernet << " IPv4=" << s.ipv4
      << " TCP=" << s.tcp << " UDP=" << s.udp << " malformed=" << s.malformed
      << " unsupported=" << s.unsupported << "\nrx_full=" << s.rx_full << " tx_full=" << s.tx_full
      << " pool_pressure=" << s.pool_exhaustion << " allocation_failures=" << s.pool_failures
      << " buffers_total=" << s.buffers_total << " buffers_in_use=" << s.buffers_in_use
      << " arena_bytes=" << s.arena_bytes << "\nseconds=" << s.seconds << " packets/s=" << s.pps()
      << " Gb/s=" << (s.seconds ? s.bytes * 8 / s.seconds / 1e9 : 0)
      << " p50/p95/p99_us=" << s.p50_us << '/' << s.p95_us << '/' << s.p99_us << '\n';
    for (size_t i = 0; i < s.per_worker.size(); ++i)
        o << "worker[" << i << "] packets=" << s.per_worker[i] << " bytes=" << s.worker_bytes[i]
          << " drops=" << s.worker_drops[i] << " approximate_high_water=" << s.queue_high_water[i]
          << '\n';
}
} // namespace pf
