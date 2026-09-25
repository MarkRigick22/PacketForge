#include "packetforge/worker.hpp"
namespace pf {
void Worker::run() {
    size_t owned = 0, ordinal = 0;
    // Quotas keep one worker from hoarding the entire shared arena.
    auto refill = [&] {
        while (q_.supply.approximate_size() < q_.supply.capacity() && owned < quota_) {
            auto* b = cache_.acquire();
            if (!b)
                break;
            if (!q_.supply.push({b, 0, 0, 0, Ownership::Host})) {
                cache_.recycle(b);
                break;
            }
            ++owned;
        }
    };
    auto complete = [&] {
        Descriptor d;
        while (q_.complete_tx(d)) {
            if (d.started_ns)
                stats.latency.add(now_ns() - d.started_ns);
            ++stats.packets;
            stats.bytes += d.length;
            stats.digest += d.length + d.buffer->data[d.length - 1];
            cache_.recycle(d.buffer);
            --owned;
        }
    };
    refill();
    for (;;) {
        Descriptor d;
        bool work = false;
        // Process in bounded batches; the TX device model is serviced by this thread.
        for (size_t batch = 0; batch < 32 && q_.take_rx(d); ++batch) {
            work = true;
            ++ordinal;
            d.buffer->transition(Ownership::DeviceRx, Ownership::Worker);
            pf_packet parsed{};
            const bool valid = pf_parse_packet(d.buffer->data, d.length, &parsed) == PF_OK;
            d.buffer->transition(Ownership::Worker, Ownership::DeviceTx);
            d.ownership = Ownership::DeviceTx;
            if (!valid || reject_nth(ordinal, faults_.reject_tx_every) || !q_.post_tx(d)) {
                ++stats.drops;
                if (valid)
                    ++tx_full;
                cache_.recycle(d.buffer);
                --owned;
            }
        }
        complete();
        if (stop_.load(std::memory_order_acquire) && q_.rx.empty())
            break;
        refill();
        if (!work)
            std::this_thread::yield();
    }
    Descriptor d;
    while (q_.supply.pop(d)) {
        cache_.recycle(d.buffer);
        --owned;
    }
    cache_.flush();
}
} // namespace pf
