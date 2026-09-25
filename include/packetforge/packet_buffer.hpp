#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
namespace pf {
class MemoryPool;
enum class Ownership { Free, Host, DeviceRx, Worker, DeviceTx, Cached };
struct alignas(64) PacketBuffer {
    uint8_t* data = nullptr;
    size_t capacity = 0, length = 0, id = 0;
    MemoryPool* pool = nullptr;
    std::atomic<Ownership> state{Ownership::Free};
    void transition(Ownership from, Ownership to) {
        const bool allowed =
            (from == Ownership::Free && to == Ownership::Host) ||
            (from == Ownership::Host &&
             (to == Ownership::DeviceRx || to == Ownership::Free || to == Ownership::Cached)) ||
            (from == Ownership::DeviceRx && (to == Ownership::Worker || to == Ownership::Host)) ||
            (from == Ownership::Worker && to == Ownership::DeviceTx) ||
            (from == Ownership::DeviceTx && to == Ownership::Cached) ||
            (from == Ownership::Cached && to == Ownership::Host);
        if (!allowed || !state.compare_exchange_strong(from, to, std::memory_order_relaxed))
            throw std::logic_error("invalid buffer ownership transition");
    }
};
} // namespace pf
