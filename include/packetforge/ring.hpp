#pragma once
#include <atomic>
#include <cstddef>
#include <stdexcept>
#include <type_traits>
#include <vector>
namespace pf {
// Exactly one producer and one consumer; the same thread may play both roles.
template <class T> class SpscRing {
    static_assert(std::is_trivially_copyable<T>::value, "Descriptors must be trivial");
    static_assert(std::atomic<size_t>::is_always_lock_free, "This target lacks lock-free indices");
    std::vector<T> slots_;
    alignas(64) std::atomic<size_t> head_{0};
    alignas(64) std::atomic<size_t> tail_{0};

  public:
    explicit SpscRing(size_t capacity) : slots_(checked(capacity)) {}
    static size_t checked(size_t n) {
        if (n < 1 || n > (size_t(1) << 28))
            throw std::invalid_argument("ring capacity out of range");
        return n + 1;
    }
    size_t capacity() const {
        return slots_.size() - 1;
    }
    bool push(const T& v) {
        auto h = head_.load(std::memory_order_relaxed);
        auto next = (h + 1) % slots_.size();
        if (next == tail_.load(std::memory_order_acquire))
            return false;
        slots_[h] = v;
        head_.store(next, std::memory_order_release);
        return true;
    }
    bool pop(T& v) {
        auto t = tail_.load(std::memory_order_relaxed);
        if (t == head_.load(std::memory_order_acquire))
            return false;
        v = slots_[t];
        tail_.store((t + 1) % slots_.size(), std::memory_order_release);
        return true;
    }
    bool empty() const {
        return head_.load(std::memory_order_acquire) == tail_.load(std::memory_order_acquire);
    }
    // Advisory only: independent snapshots are not a synchronization primitive.
    size_t approximate_size() const {
        auto h = head_.load(std::memory_order_acquire);
        auto t = tail_.load(std::memory_order_acquire);
        return (h + slots_.size() - t) % slots_.size();
    }
};
} // namespace pf
