#pragma once
#include "cnet/packet.h"
namespace pf {
struct FlowKey {
    uint32_t source, destination;
    uint16_t source_port, destination_port;
    uint8_t protocol;
    bool operator==(const FlowKey& o) const {
        return source == o.source && destination == o.destination && source_port == o.source_port &&
               destination_port == o.destination_port && protocol == o.protocol;
    }
};
inline FlowKey flow_key(const pf_packet& p) {
    return {p.ipv4.source, p.ipv4.destination, p.source_port, p.destination_port, p.ipv4.protocol};
}
struct FlowHash {
    size_t operator()(const FlowKey& f) const {
        uint64_t h = 14695981039346656037ull;
        // Explicit field serialization: no padding or host-endian dependence.
        auto add = [&](uint32_t x, int n) {
            for (int i = n - 1; i >= 0; --i) {
                h ^= (x >> (8 * i)) & 255;
                h *= 1099511628211ull;
            }
        };
        add(f.source, 4);
        add(f.destination, 4);
        add(f.source_port, 2);
        add(f.destination_port, 2);
        add(f.protocol, 1);
        h ^= h >> 33;
        h *= 0xff51afd7ed558ccdull;
        h ^= h >> 33;
        return static_cast<size_t>(h);
    }
};
} // namespace pf
