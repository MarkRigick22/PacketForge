#pragma once
#include <cstdint>
#include <fstream>
#include <vector>
namespace pf {
// Classic PCAP, Ethernet link type; timestamps are read but replay is unpaced.
class PcapReader {
    std::ifstream file_;
    bool little_ = true, nanoseconds_ = false;
    uint32_t snaplen_ = 0;
    uint32_t u32(const uint8_t* p) const;
    uint16_t u16(const uint8_t* p) const;

  public:
    explicit PcapReader(const std::string& path);
    bool next(std::vector<uint8_t>& packet);
};
} // namespace pf
