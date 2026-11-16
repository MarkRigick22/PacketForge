#pragma once
#include "packet_buffer.hpp"
#include <memory>
#include <mutex>
#include <vector>
namespace pf {
struct PoolStats {
    size_t total, available, in_use, failures, arena_bytes;
};
class MemoryPool {
    struct AlignedDelete {
        void operator()(uint8_t* p) const;
    };
    size_t count_, stride_;// stride is the distance between each buffer
    std::unique_ptr<uint8_t, AlignedDelete> arena_;//all the packets bytes goes here
    std::unique_ptr<PacketBuffer[]> buffers_;//meta data + pointer to areana
    mutable std::mutex mutex_;//memory pool is shared between workers , this is not SPSC ring
    std::vector<PacketBuffer*> free_;// list of free buffers
    size_t failures_ = 0;

  public:
    MemoryPool(size_t count, size_t capacity);//the only part we allocate memory in the whole system instead of malloc/free
    PacketBuffer* acquire();
    void release(PacketBuffer* buffer);
    PoolStats stats() const;
};
// Thread-confined cache. Objects belong to one worker until flushed to the pool.
class LocalCache {
    MemoryPool& pool_;
    size_t limit_;// how many buffers i can maintain from the memory pool 
    std::vector<PacketBuffer*> free_;

  public:
    explicit LocalCache(MemoryPool& p, size_t limit = 32) : pool_(p), limit_(limit) {
        if (!limit)
            throw std::invalid_argument("cache limit is zero");
        free_.reserve(limit);
    }
    ~LocalCache() {
        flush();
    }
    LocalCache(const LocalCache&) = delete;
    LocalCache& operator=(const LocalCache&) = delete;
    PacketBuffer* acquire();
    void recycle(PacketBuffer* b);
    void flush();
};
} // namespace pf
