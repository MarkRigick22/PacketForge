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
        workers_.push_back(std::make_unique<Worker>(device_.queue(i), pool_, quota, c.faults, c.rules, c.flow_capacity, c.batch_size, c.sink));
    device_.start();
    try {
        size_t starting = 0;
        for (auto& w : workers_) {
            if (++starting == c.faults.fail_start_worker) throw std::runtime_error("injected worker start failure");
            w->start();
        }
    } catch (...) {
        for (auto& w : workers_)
            w->stop();
        for (auto& w : workers_)
            w->join();
        throw;
    }
}
Dataplane::~Dataplane() {
    if (!finished_) {
        for (auto& w : workers_) w->stop();
        for (auto& w : workers_) w->join();
        device_.stop();
    }
}
bool Dataplane::submit(const uint8_t* data, size_t n, bool wait) {
    if (finished_)
        throw std::logic_error("submit after finish");
    uint64_t stamp = (stats_.received % 64 == 0) ? now_ns() : 0;//check time for every 64 packet , because it costs CPU time
    ++stats_.received;
    pf_packet parsed{};
    if (!classify(data, n, parsed, stats_))
        return false;
    if (n > config_.buffer_size) {
        ++stats_.dropped;
        return false;
    }
    auto index = rss_worker(flow_key(parsed), workers_.size());//do hash : RSS - Recieve Side Scaling
    auto& q = device_.queue(index);// queue of worker i
    if (reject_nth(stats_.received, config_.faults.reject_rx_every)) {// simulation to failures in example every 100 packet drops intentianoly to check that the system works in failed conditions
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
    while (!q.supply.pop(d)) {// give me Descriptor that points to free Buffer
        if (!wait) { // no free buffer
            ++stats_.pool_exhaustion;
            ++stats_.dropped;
            return false;
        }
        std::this_thread::yield();//threads gives the CPU "i dont have work to do i am waiting"
    }
    std::memcpy(d.buffer->data, data, n); // One ingress copy; no copies between dataplane stages. we only do 1 copy in the initliztion from input packet to pre allocated Buffer - no copies later between stages .
    d.buffer->length = n;
    d.length = n;
    d.started_ns = stamp;
    d.ownership = Ownership::DeviceRx;// Ownership : " who control the buffer now ? Host/Rx/thread/Tx
    d.buffer->transition(Ownership::Host, Ownership::DeviceRx);
    while (!q.post_rx(d)) {// insert the descriptor into RX ring
        std::this_thread::yield();// this is the SPSC part : single producer (DataPlane/device ) single consumer (thread)
    }
    stats_.queue_high_water[index] =
        std::max<uint64_t>(stats_.queue_high_water[index], q.rx.approximate_size());//update new limit of RX queue if needed
    return true;
}
Stats Dataplane::finish() {
    if (finished_)
        return stats_;
    for (auto& w : workers_)
        w->stop();
    for (auto& w : workers_)
        w->join();
    finished_ = true;
    device_.stop();
    std::vector<uint64_t> samples;
    for (auto& w : workers_) {
        auto& s = w->stats;
        stats_.processed += s.packets;
        stats_.transmitted += s.packets;
        stats_.bytes += s.bytes;
        stats_.digest += s.digest;
        stats_.dropped += s.drops;
        stats_.allocation_drops += s.allocation_drops;
        stats_.policy_drops += s.policy_drops;
        stats_.egress_drops += s.egress_drops;
        stats_.malformed += s.malformed;
        stats_.recycled += s.recycled;
        const auto& table = (w->processor).flows;
        stats_.flow_capacity += table.entries().size();
        stats_.active_flows += table.active();
        stats_.flow_evictions += table.evictions();
        stats_.flow_collisions += table.collisions();
        stats_.flows.push_back(table.entries());
        stats_.tx_full += w->tx_full;
        stats_.tx_high_water.push_back(s.tx_high_water);
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
