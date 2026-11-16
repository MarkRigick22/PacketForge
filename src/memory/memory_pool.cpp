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
    // Clarification: this rounds capacity UP to the next multiple of 64.
    stride_ = (capacity + 63) & ~size_t(63);//floor the capapcity to be multiplication of 64
    if (count > std::numeric_limits<size_t>::max() / stride_)
        throw std::overflow_error("arena size overflow");
    arena_.reset(static_cast<uint8_t*>(::operator new(count * stride_, std::align_val_t(64))));
    buffers_ = std::make_unique<PacketBuffer[]>(count);
    free_.reserve(count);
    for (size_t i = 0; i < count; ++i) {//connect the buffer to the arena , the arena have the real packets data
        auto& b = buffers_[i];
        b.data = arena_.get() + i * stride_;
        b.capacity = capacity;
        b.id = i;
        b.pool = this;// every buffer saves his memory pool of where he came from , this is crucial so buffer always go back to the right memory pool
        free_.push_back(&b);
    }
}
PacketBuffer* MemoryPool::acquire() {// give me free buffer
    std::lock_guard<std::mutex> lock(mutex_);// sync
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
    if (free_.empty())// if cache is empty , give me buffer from the memory pool
        return pool_.acquire();
    auto* b = free_.back();// if there is buffer in the cache give it , no need for mutex , why ?
    free_.pop_back();
    b->transition(Ownership::Cached, Ownership::Host);
    b->length = 0;
    return b;
}
void LocalCache::recycle(PacketBuffer* b) {// after packet transmission complete
    if (!b || b->pool != &pool_)
        throw std::logic_error("foreign cache buffer");
    auto old = b->state.load(std::memory_order_relaxed); // get the current ownership
    b->transition(old, Ownership::Cached);
    if (free_.size() == limit_) {// cache is full
        auto* spill = free_.back();// free buffer from cache
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
