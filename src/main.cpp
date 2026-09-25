#include "packetforge/dataplane.hpp"
#include "packetforge/pcap_reader.hpp"
#include "packetforge/report.hpp"
#include "packetforge/tap.hpp"
#include "packetforge/traffic.hpp"
#include <csignal>
#include <iostream>
#include <limits>
#include <string>
namespace {
volatile std::sig_atomic_t interrupted = 0;
void on_signal(int) {
    interrupted = 1;
}
size_t number(const std::string& s) {
    if (s.empty() || s[0] == '-')
        throw std::invalid_argument("expected nonnegative integer");
    size_t used = 0;
    auto v = std::stoull(s, &used);
    if (used != s.size() || v > std::numeric_limits<size_t>::max())
        throw std::invalid_argument("invalid integer: " + s);
    return static_cast<size_t>(v);
}
} // namespace
int main(int argc, char** argv) {
    try {
        pf::Config c;
        pf::TrafficConfig traffic;
        size_t packets = 100000;
        bool baseline = false, csv = false, wait = true;
        std::string source = "synthetic", path;
        bool source_set = false;
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            auto value = [&]() {
                if (++i >= argc)
                    throw std::invalid_argument("missing value for " + arg);
                return std::string(argv[i]);
            };
            auto select = [&](std::string kind) {
                if (source_set)
                    throw std::invalid_argument("choose exactly one traffic source");
                source_set = true;
                source = kind;
            };
            if (arg == "--help") {
                std::cout << R"(PacketForge — firmware-style software NIC datapath (no physical DMA)
Usage: packetforge [--synthetic | --pcap FILE | --tap NAME] [options]
  --packets N         Synthetic count / replay or TAP limit (default 100000)
  --workers N         Worker threads, 1..256 (default 4)
  --flows N           Synthetic flow count (default 1000)
  --size N            Ethernet frame bytes without FCS, 60..65549 (default 128)
  --seed N            Deterministic payload seed (default 1)
  --tcp-percent N     TCP percentage of flow IDs (default 50)
  --buffers N         Shared arena buffer count (default 4096)
  --buffer-size N     Each buffer capacity (default 2048)
  --ring-size N       Descriptors per queue (default 256)
  --baseline          Allocations, copies and mutex queues
  --optimized         Arena, descriptor references and SPSC queues (default)
  --drop-on-pressure  Drop instead of waiting at ingress
  --fault-rx-every N  Reject every Nth RX attempt (0 disables)
  --fault-tx-every N  Reject every Nth worker TX attempt (0 disables)
  --csv               Machine-readable header and one result row
  --help              Show this help
TAP is Linux-only, RX-to-simulated-TX sink; Ctrl-C drains pending work.
PCAP: classic Ethernet PCAP, unpaced; no external library required.
)";
                return 0;
            } else if (arg == "--synthetic")
                select("synthetic");
            else if (arg == "--pcap") {
                select("pcap");
                path = value();
            } else if (arg == "--tap") {
                select("tap");
                path = value();
            } else if (arg == "--packets")
                packets = number(value());
            else if (arg == "--workers")
                c.workers = number(value());
            else if (arg == "--flows")
                traffic.flows = number(value());
            else if (arg == "--size")
                traffic.size = number(value());
            else if (arg == "--seed")
                traffic.seed = number(value());
            else if (arg == "--tcp-percent") {
                auto n = number(value());
                if (n > 100)
                    throw std::invalid_argument("TCP percentage exceeds 100");
                traffic.tcp_percent = static_cast<unsigned>(n);
            } else if (arg == "--buffers")
                c.buffers = number(value());
            else if (arg == "--buffer-size")
                c.buffer_size = number(value());
            else if (arg == "--ring-size")
                c.ring_capacity = number(value());
            else if (arg == "--fault-rx-every")
                c.faults.reject_rx_every = number(value());
            else if (arg == "--fault-tx-every")
                c.faults.reject_tx_every = number(value());
            else if (arg == "--baseline")
                baseline = true;
            else if (arg == "--optimized")
                baseline = false;
            else if (arg == "--csv")
                csv = true;
            else if (arg == "--drop-on-pressure")
                wait = false;
            else
                throw std::invalid_argument("unknown option: " + arg);
        }
        if (baseline && (c.faults.reject_rx_every || c.faults.reject_tx_every))
            throw std::invalid_argument("device fault injection requires optimized mode");
        std::signal(SIGINT, on_signal);
        std::signal(SIGTERM, on_signal);
        auto run = [&](auto& engine) {
            if (source == "synthetic") {
                pf::TrafficGenerator gen(traffic);
                for (size_t i = 0; i < packets && !interrupted; ++i) {
                    auto& p = gen.packet(i);
                    engine.submit(p.data(), p.size(), wait);
                }
            } else if (source == "pcap") {
                pf::PcapReader reader(path);
                std::vector<uint8_t> p;
                for (size_t i = 0; i < packets && !interrupted && reader.next(p); ++i)
                    engine.submit(p.data(), p.size(), wait);
            } else {
                pf::Tap tap(path);
                std::vector<uint8_t> p;
                size_t i = 0;
                while (i < packets && !interrupted)
                    if (tap.read(p)) {
                        engine.submit(p.data(), p.size(), wait);
                        ++i;
                    }
            }
            return engine.finish();
        };
        pf::Stats stats;
        if (baseline) {
            pf::Baseline engine(c);
            stats = run(engine);
        } else {
            pf::Dataplane engine(c);
            stats = run(engine);
        }
        const char* mode = baseline ? "baseline" : "optimized";
        if (csv) {
            pf::csv_header(std::cout);
            pf::csv_row(std::cout, stats, mode, c.workers);
        } else
            pf::human_report(std::cout, stats, mode);
        if (stats.received != stats.transmitted + stats.dropped || stats.buffers_in_use)
            throw std::runtime_error("packet/buffer accounting invariant failed");
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "packetforge: " << e.what() << '\n';
        return 1;
    }
}
