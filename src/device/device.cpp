#include "packetforge/device.hpp"
namespace pf {
bool QueuePair::post_rx(Descriptor d) {
    if (!rx.push(d))
        return false;
    registers.rx_tail.fetch_add(1, std::memory_order_relaxed);
    registers.rx_doorbell.fetch_add(1, std::memory_order_relaxed);
    return true;
}
bool QueuePair::take_rx(Descriptor& d) {
    if (!rx.pop(d))
        return false;
    registers.rx_head.fetch_add(1, std::memory_order_relaxed);
    return true;
}
bool QueuePair::post_tx(Descriptor d) {
    if (!tx.push(d))
        return false;
    registers.tx_tail.fetch_add(1, std::memory_order_relaxed);
    registers.tx_doorbell.fetch_add(1, std::memory_order_relaxed);
    return true;
}
bool QueuePair::complete_tx(Descriptor& d) {
    if (!tx.pop(d))
        return false;
    registers.tx_head.fetch_add(1, std::memory_order_relaxed);
    return true;
}
Device::Device(size_t n, size_t capacity) {
    if (n < 1 || n > 256)
        throw std::invalid_argument("workers must be 1..256");
    for (size_t i = 0; i < n; ++i)
        queues_.push_back(std::make_unique<QueuePair>(capacity));
}
void Device::start() {
    for (auto& q : queues_) {
        q->registers.control.store(1);
        q->registers.status.store(DeviceStatus::Running);
    }
}
void Device::stop() {
    for (auto& q : queues_) {
        q->registers.control.store(0);
        q->registers.status.store(DeviceStatus::Stopped);
    }
}
} // namespace pf
