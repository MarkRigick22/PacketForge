#include "packetforge/worker.hpp"
namespace pf {
void Worker::run() {
    size_t owned = 0, ordinal = 0;
    // Quotas keep one worker from hoarding the entire shared arena.
    auto refill = [&] {//lamdpa expression help us use variables from enviromeent like: owned_ / quata/ cache
        
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
    auto complete = [&] { //if there still Tx packets process them
        Descriptor d;
        while (q_.complete_tx(d)) {
            if (sink_ && !sink_->write(d.buffer->data, d.length)) {
                ++stats.drops; ++stats.egress_drops;
                cache_.recycle(d.buffer); ++stats.recycled; --owned;
                continue;
            }
            if (d.started_ns)
                stats.latency.add(now_ns() - d.started_ns);
            ++stats.packets;
            stats.bytes += d.length;
            // Clarification: this accumulator is not a cryptographic hash.
            stats.digest += d.length + d.buffer->data[d.length - 1];//d length + d last byte - for cryptogtaphic hash correctness
            cache_.recycle(d.buffer);
            ++stats.recycled;
            --owned; // the life cycle : before Tx : worker -> Tx ring . after Tx : TX completion -> worker gets back descriptor
        }
    };
    // Batch size is configurable; the original 32-packet explanation below describes the default.
    refill();//fill the supply queue with empty buffers 
    for (;;) {
        Descriptor d;
        bool work = false;
        // Process in bounded batches; the TX device model is serviced by this thread.
        for (size_t batch = 0; batch < batch_size_ && q_.take_rx(d); ++batch) {//every worker process 32 packet in one cycle - bounded batches - we use it because we dont need the worker to only be stucj in RX part and ignore TX completion / refill / stop check .
            if (q_.tx.approximate_size() == q_.tx.capacity()) complete();
            work = true;
            ++ordinal;//packet counter
            d.buffer->transition(Ownership::DeviceRx, Ownership::Worker);//very important - change the owner of the buffer from RX to worker
            pf_packet parsed{}; // new structure that takes the results : sourcIP/destIP/IPv4 field....
            const bool valid = pf_parse_packet(d.buffer->data, d.length, &parsed) == PF_OK; // take the bytes of the packet and try to split them to fileds .
            if (valid && !processor.accept(parsed, d.length)) {
                ++stats.drops; ++stats.policy_drops;
                // Policy rejects never enter the TX ring; the worker still owns this buffer.
                cache_.recycle(d.buffer); ++stats.recycled; --owned;
                continue;
            }
            if (!valid) ++stats.malformed;
            d.buffer->transition(Ownership::Worker, Ownership::DeviceTx);
            d.ownership = Ownership::DeviceTx; // move the ownership to Tx
            if (!valid || reject_nth(ordinal, faults_.reject_tx_every) || !q_.post_tx(d)) {
                ++stats.drops;
                if (valid)
                    ++tx_full;
                cache_.recycle(d.buffer);// even when a packet drops , the buffer must return
                ++stats.recycled;
                --owned;
            }
            stats.tx_high_water = std::max<uint64_t>(stats.tx_high_water, q_.tx.approximate_size());
        }
        complete(); //check which Tx operations finished and return their buffers
        if (stop_.load(std::memory_order_acquire) && q_.rx.empty())//check the rx because we dont want to lose packets in RX
            // the stop_.load is atomic operation , memory order auquire job is to keep the memory oredering between threads -
            break;
        refill();
        if (!work)
            std::this_thread::yield();
    }
    Descriptor d;
    while (q_.supply.pop(d)) { // recycle every buffer who is in supply ring that havent been proccessed before exiting
        cache_.recycle(d.buffer);
        --owned;
    }
    cache_.flush();
}
} // namespace pf
