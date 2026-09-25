#pragma once
#include "rss.hpp"
#include "worker.hpp"
#include <condition_variable>
#include <deque>
namespace pf {
struct Config {
    size_t workers = 4, ring_capacity = 256, buffers = 4096, buffer_size = 2048;
    FaultInjection faults;
};
class Dataplane {
    Config config_;
    MemoryPool pool_;
    Device device_;
    std::vector<std::unique_ptr<Worker>> workers_;
    Stats stats_;
    uint64_t start_;
    bool finished_ = false;

  public:
    explicit Dataplane(Config config = {});
    ~Dataplane();
    // One host thread only. wait=false converts supply/ring pressure into drops.
    bool submit(const uint8_t* data, size_t length, bool wait = true);
    Stats finish();
    PoolStats pool_stats() const {
        return pool_.stats();
    }
};
class Baseline {
    struct Item {
        std::vector<uint8_t> payload;
        uint64_t started;
    };
    struct Queue {
        std::mutex mutex;
        std::condition_variable ready, space;
        std::deque<Item> items;
        bool stop = false;
        WorkerStats stats;
        std::thread thread;
    };
    Config config_;
    std::vector<std::unique_ptr<Queue>> queues_;
    Stats stats_;
    uint64_t start_;
    bool finished_ = false;

  public:
    explicit Baseline(Config config = {});
    ~Baseline();
    bool submit(const uint8_t* data, size_t n, bool wait = true);
    Stats finish();
};
} // namespace pf
