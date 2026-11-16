#pragma once
#include <cstdint>
#include "processing.hpp"
#include <mutex>
#include <string>
#include <vector>
namespace pf {
class Tap {
#if PF_TAP
    int fd_ = -1;
#endif
  public:
    explicit Tap(const std::string& name);
    ~Tap();
    Tap(const Tap&) = delete;
    Tap& operator=(const Tap&) = delete;
    bool write_frame(const uint8_t* data, size_t length) noexcept;
    bool read(std::vector<uint8_t>& packet, int timeout_ms = 100);
};
// Explicit egress is restricted to named test TAPs in a separate network namespace.
class TapSink final : public FrameSink {
    std::mutex mutex_;
    Tap tap_;
    static std::string checked(const std::string& name);
  public:
    explicit TapSink(const std::string& name) : tap_(checked(name)) {}
    bool write(const uint8_t* data, size_t length) noexcept override {
        try {
            std::lock_guard<std::mutex> lock(mutex_);
            return tap_.write_frame(data, length);
        } catch (...) { return false; }
    }
};
} // namespace pf
