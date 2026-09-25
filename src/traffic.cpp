#include "packetforge/traffic.hpp"
#include "cnet/network_types.h"
#include <algorithm>
#include <stdexcept>
namespace pf {
static void be16(uint8_t* p, uint16_t x) {
    p[0] = x >> 8;
    p[1] = x & 255;
}
static void be32(uint8_t* p, uint32_t x) {
    p[0] = x >> 24;
    p[1] = (x >> 16) & 255;
    p[2] = (x >> 8) & 255;
    p[3] = x & 255;
}
TrafficGenerator::TrafficGenerator(TrafficConfig c) : config_(c) {
    if (c.size < 60 || c.size > 65549 || !c.flows || c.flows > 0xffffffffull || c.tcp_percent > 100)
        throw std::invalid_argument(
            "traffic requires size 60..65549, flows 1..2^32-1, TCP percent 0..100");
    packet_.resize(c.size);
}
const std::vector<uint8_t>& TrafficGenerator::packet(uint64_t index) {
    std::fill(packet_.begin(), packet_.end(), 0);
    auto* p = packet_.data();
    uint32_t flow = static_cast<uint32_t>(index % config_.flows);
    // Protocol is a property of the flow, not the packet ordinal.
    bool tcp = (flow % 100) < config_.tcp_percent;
    p[0] = 2;
    p[5] = 1;
    p[6] = 2;
    p[11] = 2;
    be16(p + 12, 0x0800);
    auto* ip = p + 14;
    ip[0] = 0x45;
    be16(ip + 2, static_cast<uint16_t>(packet_.size() - 14));
    be16(ip + 4, static_cast<uint16_t>(index));
    ip[8] = 64;
    ip[9] = tcp ? 6 : 17;
    be32(ip + 12, 0x0a000000u + (flow & 0xffffff));
    be32(ip + 16, 0x0b000001u);
    auto* l4 = ip + 20;
    size_t len = packet_.size() - 34;
    be16(l4, static_cast<uint16_t>(1024 + flow % 60000));
    be16(l4 + 2, static_cast<uint16_t>(tcp ? 443 : 53));
    size_t header = tcp ? 20 : 8;
    if (tcp) {
        be32(l4 + 4, static_cast<uint32_t>(index));
        l4[12] = 0x50;
        l4[13] = 0x18;
        be16(l4 + 14, 65535);
    } else
        be16(l4 + 4, static_cast<uint16_t>(len));
    uint64_t random = config_.seed ^ (uint64_t(flow) * 0x9e3779b97f4a7c15ull);
    for (size_t i = header; i < len; ++i) {
        random = random * 6364136223846793005ull + 1;
        l4[i] = static_cast<uint8_t>(random >> 56);
    }
    // IPv4 pseudo-header checksum plus transport segment; no temporary payload allocation.
    uint32_t sum = 0;
    for (size_t i = 12; i < 20; i += 2)
        sum += pf_be16(ip + i);
    sum += ip[9];
    sum += static_cast<uint16_t>(len);
    for (size_t i = 0; i + 1 < len; i += 2)
        sum += pf_be16(l4 + i);
    if (len & 1)
        sum += uint32_t(l4[len - 1]) << 8;
    while (sum >> 16)
        sum = (sum & 65535) + (sum >> 16);
    uint16_t checksum = static_cast<uint16_t>(~sum);
    if (!tcp && !checksum)
        checksum = 65535;
    be16(l4 + (tcp ? 16 : 6), checksum);
    be16(ip + 10, pf_checksum(ip, 20));
    return packet_;
}
} // namespace pf
