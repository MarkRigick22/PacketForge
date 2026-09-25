#pragma once
#include <cstdint>
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
    bool read(std::vector<uint8_t>& packet, int timeout_ms = 100);
};
} // namespace pf
