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
    size_t count_, stride_;
    std::unique_ptr<uint8_t, AlignedDelete> arena_;
    std::unique_ptr<PacketBuffer[]> buffers_;
    mutable std::mutex mutex_;
    std::vector<PacketBuffer*> free_;
    size_t failures_ = 0;

  public:
    MemoryPool(size_t count, size_t capacity);
    PacketBuffer* acquire();
    void release(PacketBuffer* buffer);
    PoolStats stats() const;
};
// Thread-confined cache. Objects belong to one worker until flushed to the pool.
class LocalCache {
    MemoryPool& pool_;
    size_t limit_;
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
