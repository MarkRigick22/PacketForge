#include "packetforge/dataplane.hpp"
namespace pf {
//“The baseline creates one worker thread and one bounded mutex-protected queue per worker. On submission I parse the packet, derive its 5-tuple, select a worker using RSS, copy the packet into an owned vector and enqueue it. The worker sleeps on a condition variable until work arrives, pops the item under the mutex, releases the lock before processing, parses it, performs a deliberate TX payload copy and updates private statistics. On shutdown I signal every worker, let each worker drain its queue, join all threads, then aggregate throughput, drop, per-worker and sampled latency statistics.”
Baseline::Baseline(Config c) : config_(c), start_(now_ns()) {//in the baseline version the packets are COPIED from each layer , we use the baseline model to show that the optimized method really make better performance
    if (!c.workers || c.workers > 256 || !c.ring_capacity)
        throw std::invalid_argument("invalid baseline configuration");
    stats_.queue_high_water.resize(c.workers); // the timestamp starts in the construction moment therefore we have extra time for threads initiliaztion overhead
    // high water mark is the maximum capacity of packets for one worker
    for (size_t i = 0; i < c.workers; ++i)
        queues_.push_back(std::make_unique<Queue>());
    try {
        for (auto& qp : queues_) {
            auto* q = qp.get();// qp is unique ptr
            q->thread = std::thread([q] {// creats worker thread
                for (;;) {
                    Item item; //Queue -> deque<item> , this is the payload
                    {
                        std::unique_lock<std::mutex> lock(q->mutex);// without lock there might be race conditions SCSP
                        //unique lock works with time(wait()) , unique guard no .,
                        q->ready.wait(lock, [&] { return q->stop || !q->items.empty(); }); // this line is crusial it avoids busy waiting
                        if (q->items.empty()) // if its time to stop " stop = true " and there still packets needs to proccess , first finish them then quit
                            break;
                        item = std::move(q->items.front()); // we use move beacues items contains vector<uin_8t> so we avoid deep copy
                        q->items.pop_front();
                        q->space.notify_one();
                    }//unique lock destroyed automatic
                    pf_packet parsed{};
                    if (pf_parse_packet(item.payload.data(), item.payload.size(), &parsed) !=
                        PF_OK) {
                        ++q->stats.drops; //if parcing failed we drop it
                        continue;
                    } // in the baseline model we parce twice : in submit and in when worker finishes , this performs calssification and adds overhead
                    std::vector<uint8_t> tx(item.payload); // Deliberate baseline TX payload copy. : copy from RX to TX vector
                    ++q->stats.packets;
                    q->stats.bytes += tx.size();
                    q->stats.digest += tx.size() + tx.back();//The digest is a lightweight checksum-like accumulator used mainly so benchmark work has an observable result; it is not a cryptographic hash.”
                    if (item.started)
                        q->stats.latency.add(now_ns() - item.started);//latency
                }
            });
        }
    } catch (...) {// if we created 8 workers and worker 4 didnt allocate succesfly we cleanup all the rest
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
