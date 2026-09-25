#pragma once
#include "descriptor.hpp"
#include "ring.hpp"
#include <memory>
namespace pf {
enum class DeviceStatus { Reset, Running, Stopped };
struct Registers {
    std::atomic<DeviceStatus> status{DeviceStatus::Reset};
    std::atomic<uint64_t> control{0}, rx_head{0}, rx_tail{0}, tx_head{0}, tx_tail{0};
    std::atomic<uint64_t> rx_doorbell{0}, tx_doorbell{0};
};
struct QueuePair {
    SpscRing<Descriptor> supply, rx, tx;
    Registers registers;
    QueuePair(size_t n) : supply(n), rx(n), tx(n) {}
    bool post_rx(Descriptor d);
    bool take_rx(Descriptor& d);
    bool post_tx(Descriptor d);
    bool complete_tx(Descriptor& d);
};
class Device {
    std::vector<std::unique_ptr<QueuePair>> queues_;

  public:
    Device(size_t workers, size_t capacity);
    QueuePair& queue(size_t i) {
        return *queues_.at(i);
    }
    void start();
    void stop();
};
} // namespace pf
