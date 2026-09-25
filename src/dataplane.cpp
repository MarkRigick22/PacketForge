#include "packetforge/dataplane.hpp"
#include <cstring>
namespace pf {
Dataplane::Dataplane(Config c)
    : config_(c), pool_(c.buffers, c.buffer_size), device_(c.workers, c.ring_capacity),
      start_(now_ns()) {
    if (c.buffers < c.workers)
        throw std::invalid_argument("need at least one buffer per worker");
    stats_.queue_high_water.resize(c.workers);
    size_t quota = std::min(c.buffers / c.workers, c.ring_capacity * 2);
    for (size_t i = 0; i < c.workers; ++i)
        workers_.push_back(std::make_unique<Worker>(device_.queue(i), pool_, quota, c.faults));
    device_.start();
    try {
        for (auto& w : workers_)
            w->start();
    } catch (...) {
        for (auto& w : workers_)
            w->stop();
        for (auto& w : workers_)
            w->join();
        throw;
    }
}
Dataplane::~Dataplane() {
    if (!finished_)
        finish();
}
bool Dataplane::submit(const uint8_t* data, size_t n, bool wait) {
    if (finished_)
        throw std::logic_error("submit after finish");
    uint64_t stamp = (stats_.received % 64 == 0) ? now_ns() : 0;
    ++stats_.received;
    pf_packet parsed{};
    if (!classify(data, n, parsed, stats_))
        return false;
    if (n > config_.buffer_size) {
        ++stats_.dropped;
        return false;
    }
    auto index = rss_worker(flow_key(parsed), workers_.size());
    auto& q = device_.queue(index);
    if (reject_nth(stats_.received, config_.faults.reject_rx_every)) {
        ++stats_.rx_full;
        ++stats_.dropped;
        return false;
    }
    if (!wait && q.rx.approximate_size() == q.rx.capacity()) {
        ++stats_.rx_full;
        ++stats_.dropped;
        return false;
    }
    Descriptor d;
    while (!q.supply.pop(d)) {
        if (!wait) {
            ++stats_.pool_exhaustion;
            ++stats_.dropped;
            return false;
        }
        std::this_thread::yield();
    }
    std::memcpy(d.buffer->data, data, n); // One ingress copy; no copies between dataplane stages.
    d.buffer->length = n;
    d.length = n;
    d.started_ns = stamp;
    d.ownership = Ownership::DeviceRx;
    d.buffer->transition(Ownership::Host, Ownership::DeviceRx);
    while (!q.post_rx(d)) {
        std::this_thread::yield();
    }
    stats_.queue_high_water[index] =
        std::max<uint64_t>(stats_.queue_high_water[index], q.rx.approximate_size());
    return true;
}
Stats Dataplane::finish() {
    if (finished_)
        return stats_;
    for (auto& w : workers_)
        w->stop();
    for (auto& w : workers_)
        w->join();
    std::vector<uint64_t> samples;
    for (auto& w : workers_) {
        auto& s = w->stats;
        stats_.processed += s.packets;
        stats_.transmitted += s.packets;
        stats_.bytes += s.bytes;
        stats_.digest += s.digest;
        stats_.dropped += s.drops;
        stats_.tx_full += w->tx_full;
        stats_.per_worker.push_back(s.packets);
        stats_.worker_bytes.push_back(s.bytes);
        stats_.worker_drops.push_back(s.drops);
        s.latency.append(samples);
    }
    stats_.seconds = (now_ns() - start_) / 1e9;
    stats_.percentiles(std::move(samples));
    auto p = pool_.stats();
    stats_.buffers_total = p.total;
    stats_.buffers_in_use = p.in_use;
    stats_.pool_failures = p.failures;
    stats_.arena_bytes = p.arena_bytes;
    device_.stop();
    finished_ = true;
    return stats_;
}
} // namespace pf
