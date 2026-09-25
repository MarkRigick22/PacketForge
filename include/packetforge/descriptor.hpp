#pragma once
#include "packet_buffer.hpp"
namespace pf {
struct Descriptor {
    PacketBuffer* buffer = nullptr;
    size_t length = 0;
    uint64_t started_ns = 0;
    uint32_t flags = 0;
    Ownership ownership = Ownership::Host;
};
} // namespace pf
