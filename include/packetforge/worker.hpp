#pragma once
#include "device.hpp"
#include "fault_injection.hpp"
#include "memory_pool.hpp"
#include "stats.hpp"
#include <thread>
namespace pf {
class Worker {
    QueuePair& q_;
    LocalCache cache_;
    size_t quota_;
    FaultInjection faults_;
    std::atomic<bool> stop_{false};
    std::thread thread_;
    void run();

  public:
    WorkerStats stats;
    uint64_t tx_full = 0;
    Worker(QueuePair& q, MemoryPool& p, size_t quota, FaultInjection faults)
        : q_(q), cache_(p, quota), quota_(quota), faults_(faults) {}
    ~Worker() {
        stop();
        join();
    }
    void start() {
        thread_ = std::thread(&Worker::run, this);
    }
    void stop() {
        stop_.store(true, std::memory_order_release);
    }
    void join() {
        if (thread_.joinable())
            thread_.join();
    }
};
} // namespace pf
