#include "packetforge/memory_pool.hpp"
#include <limits>
#include <new>
namespace pf {
void MemoryPool::AlignedDelete::operator()(uint8_t* p) const {
    ::operator delete(p, std::align_val_t(64));
}
MemoryPool::MemoryPool(size_t count, size_t capacity) : count_(count), stride_(0) {
    if (!count || capacity < 64 || capacity > 65549 || count > 10000000)
        throw std::invalid_argument("invalid pool dimensions");
    stride_ = (capacity + 63) & ~size_t(63);
    if (count > std::numeric_limits<size_t>::max() / stride_)
        throw std::overflow_error("arena size overflow");
    arena_.reset(static_cast<uint8_t*>(::operator new(count * stride_, std::align_val_t(64))));
    buffers_ = std::make_unique<PacketBuffer[]>(count);
    free_.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        auto& b = buffers_[i];
        b.data = arena_.get() + i * stride_;
        b.capacity = capacity;
        b.id = i;
        b.pool = this;
        free_.push_back(&b);
    }
}
PacketBuffer* MemoryPool::acquire() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (free_.empty()) {
        ++failures_;
        return nullptr;
    }
    auto* b = free_.back();
    free_.pop_back();
    b->transition(Ownership::Free, Ownership::Host);
    b->length = 0;
    return b;
}
void MemoryPool::release(PacketBuffer* b) {
    if (!b || b->pool != this)
        throw std::logic_error("foreign or null buffer");
    std::lock_guard<std::mutex> lock(mutex_);
    b->transition(Ownership::Host, Ownership::Free);
    b->length = 0;
    free_.push_back(b);
}
PoolStats MemoryPool::stats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return {count_, free_.size(), count_ - free_.size(), failures_, count_ * stride_};
}
PacketBuffer* LocalCache::acquire() {
    if (free_.empty())
        return pool_.acquire();
    auto* b = free_.back();
    free_.pop_back();
    b->transition(Ownership::Cached, Ownership::Host);
    b->length = 0;
    return b;
}
void LocalCache::recycle(PacketBuffer* b) {
    if (!b || b->pool != &pool_)
        throw std::logic_error("foreign cache buffer");
    auto old = b->state.load(std::memory_order_relaxed);
    b->transition(old, Ownership::Cached);
    if (free_.size() == limit_) {
        auto* spill = free_.back();
        free_.pop_back();
        spill->transition(Ownership::Cached, Ownership::Host);
        pool_.release(spill);
    }
    free_.push_back(b);
}
void LocalCache::flush() {
    for (auto* b : free_) {
        b->transition(Ownership::Cached, Ownership::Host);
        pool_.release(b);
    }
    free_.clear();
}
} // namespace pf
