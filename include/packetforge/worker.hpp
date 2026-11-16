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
    size_t batch_size_;
    std::shared_ptr<FrameSink> sink_;
    std::atomic<bool> stop_{false};
    std::thread thread_;
    void run();

  public:
    WorkerStats stats;
    Processor processor;
    uint64_t tx_full = 0;
    Worker(QueuePair& q, MemoryPool& p, size_t quota, FaultInjection faults,
           const std::vector<Rule>& rules = {}, size_t flow_capacity = 0, size_t batch = 32,
           std::shared_ptr<FrameSink> sink = {})
        : q_(q), cache_(p, quota), quota_(quota), faults_(faults), batch_size_(batch),
          sink_(std::move(sink)), processor(rules, flow_capacity) {
        if (!batch || batch > 65536) throw std::invalid_argument("batch must be 1..65536");
    }
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
