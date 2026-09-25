#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
namespace pf {
struct TrafficConfig {
    size_t size = 128, flows = 1000;
    uint64_t seed = 1;
    unsigned tcp_percent = 50;
};
class TrafficGenerator {
    TrafficConfig config_;
    std::vector<uint8_t> packet_;

  public:
    explicit TrafficGenerator(TrafficConfig c = {});
    const std::vector<uint8_t>& packet(uint64_t index);
};
} // namespace pf
