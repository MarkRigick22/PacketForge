#pragma once
#include "cnet/packet.h"
namespace pf {
//We used an FNV-1a-style hash because it is simple, fast, deterministic, and easy to apply byte-by-byte to the 5-tuple. We don’t need a cryptographic hash here; we only need a stable hash that distributes flows reasonably well across workers. We also explicitly serialize the fields so the result doesn’t depend on struct padding or host endianness. Then we add an extra mixing step to improve the distribution before taking modulo by the number of workers.
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
    size_t operator()(const FlowKey& f) const {//this can help us use the hash object as function : hash(f);
        uint64_t h = 14695981039346656037ull; // this is known hash : FNV offset basis
        // we take byte by byte from each field and hash it , this avoids hashing also padding bytes , and is not affected from littile / big endian compiler translation ( we get the same hash anyway )
        auto add = [&](uint32_t x, int n) { // void add(uint32_t x , int n ) // x =value , n = bytes
            for (int i = n - 1; i >= 0; --i) {
                h ^= (x >> (8 * i)) & 255;// take one byte from the value = XOR 1 byte
                h *= 1099511628211ull;//constantof FNVcod
            }
        };
        add(f.source, 4);
        add(f.destination, 4);
        add(f.source_port, 2);
        add(f.destination_port, 2);
        add(f.protocol, 1);
        h ^= h >> 33;//mixing : h is xored with 33 bit shifted h - better distribution
        h *= 0xff51afd7ed558ccdull;
        h ^= h >> 33;
        return static_cast<size_t>(h);
    }
};
} // namespace pf
