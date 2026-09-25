#include "packetforge/dataplane.hpp"
namespace pf {
Baseline::Baseline(Config c) : config_(c), start_(now_ns()) {
    if (!c.workers || c.workers > 256 || !c.ring_capacity)
        throw std::invalid_argument("invalid baseline configuration");
    stats_.queue_high_water.resize(c.workers);
    for (size_t i = 0; i < c.workers; ++i)
        queues_.push_back(std::make_unique<Queue>());
    try {
        for (auto& qp : queues_) {
            auto* q = qp.get();
            q->thread = std::thread([q] {
                for (;;) {
                    Item item;
                    {
                        std::unique_lock<std::mutex> lock(q->mutex);
                        q->ready.wait(lock, [&] { return q->stop || !q->items.empty(); });
                        if (q->items.empty())
                            break;
                        item = std::move(q->items.front());
                        q->items.pop_front();
                        q->space.notify_one();
                    }
                    pf_packet parsed{};
                    if (pf_parse_packet(item.payload.data(), item.payload.size(), &parsed) !=
                        PF_OK) {
                        ++q->stats.drops;
                        continue;
                    }
                    std::vector<uint8_t> tx(item.payload); // Deliberate baseline TX payload copy.
                    ++q->stats.packets;
                    q->stats.bytes += tx.size();
                    q->stats.digest += tx.size() + tx.back();
                    if (item.started)
                        q->stats.latency.add(now_ns() - item.started);
                }
            });
        }
    } catch (...) {
        for (auto& q : queues_) {
            {
                std::lock_guard<std::mutex> lock(q->mutex);
                q->stop = true;
            }
            q->ready.notify_one();
        }
        for (auto& q : queues_)
            if (q->thread.joinable())
                q->thread.join();
        throw;
    }
}
Baseline::~Baseline() {
    if (!finished_)
        finish();
}
bool Baseline::submit(const uint8_t* p, size_t n, bool wait) {
    if (finished_)
        throw std::logic_error("submit after finish");
    uint64_t stamp = (stats_.received % 64 == 0) ? now_ns() : 0;
    ++stats_.received;
    pf_packet parsed{};
    if (!classify(p, n, parsed, stats_))
        return false;
    if (n > config_.buffer_size) {
        ++stats_.dropped;
        return false;
    }
    auto index = rss_worker(flow_key(parsed), queues_.size());
    auto& q = *queues_[index];
    Item item{std::vector<uint8_t>(p, p + n), stamp};
    std::unique_lock<std::mutex> lock(q.mutex);
    if (!wait && q.items.size() >= config_.ring_capacity) {
        ++stats_.rx_full;
        ++stats_.dropped;
        return false;
    }
    q.space.wait(lock, [&] { return q.items.size() < config_.ring_capacity; });
    q.items.push_back(std::move(item));
    stats_.queue_high_water[index] =
        std::max<uint64_t>(stats_.queue_high_water[index], q.items.size());
    q.ready.notify_one();
    return true;
}
Stats Baseline::finish() {
    if (finished_)
        return stats_;
    for (auto& q : queues_) {
        {
            std::lock_guard<std::mutex> lock(q->mutex);
            q->stop = true;
        }
        q->ready.notify_one();
    }
    std::vector<uint64_t> samples;
    for (auto& q : queues_) {
        if (q->thread.joinable())
            q->thread.join();
        auto& s = q->stats;
        stats_.processed += s.packets;
        stats_.transmitted += s.packets;
        stats_.bytes += s.bytes;
        stats_.digest += s.digest;
        stats_.dropped += s.drops;
        stats_.per_worker.push_back(s.packets);
        stats_.worker_bytes.push_back(s.bytes);
        stats_.worker_drops.push_back(s.drops);
        s.latency.append(samples);
    }
    stats_.seconds = (now_ns() - start_) / 1e9;
    stats_.percentiles(std::move(samples));
    finished_ = true;
    return stats_;
}
} // namespace pf
