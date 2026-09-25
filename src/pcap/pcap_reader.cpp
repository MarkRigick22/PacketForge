#include "packetforge/pcap_reader.hpp"
#include <stdexcept>
namespace pf {
uint32_t PcapReader::u32(const uint8_t* p) const {
    if (little_)
        return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) |
               (uint32_t(p[3]) << 24);
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}
uint16_t PcapReader::u16(const uint8_t* p) const {
    return little_ ? uint16_t(p[0] | (p[1] << 8)) : uint16_t((p[0] << 8) | p[1]);
}
PcapReader::PcapReader(const std::string& path) {
#if PF_PCAP
    file_.open(path, std::ios::binary);
    if (!file_)
        throw std::runtime_error("cannot open PCAP: " + path);
    uint8_t h[24];
    if (!file_.read(reinterpret_cast<char*>(h), 24))
        throw std::runtime_error("truncated PCAP global header");
    if (h[0] == 0xd4 && h[1] == 0xc3 && h[2] == 0xb2 && h[3] == 0xa1)
        little_ = true;
    else if (h[0] == 0xa1 && h[1] == 0xb2 && h[2] == 0xc3 && h[3] == 0xd4)
        little_ = false;
    else if (h[0] == 0x4d && h[1] == 0x3c && h[2] == 0xb2 && h[3] == 0xa1) {
        little_ = true;
        nanoseconds_ = true;
    } else if (h[0] == 0xa1 && h[1] == 0xb2 && h[2] == 0x3c && h[3] == 0x4d) {
        little_ = false;
        nanoseconds_ = true;
    } else
        throw std::runtime_error("unsupported PCAP magic (pcapng is not classic PCAP)");
    if (u16(h + 4) != 2 || u16(h + 6) != 4 || u32(h + 20) != 1)
        throw std::runtime_error("requires PCAP 2.4 Ethernet link type 1");
    snaplen_ = u32(h + 16);
    if (!snaplen_ || snaplen_ > 16 * 1024 * 1024)
        throw std::runtime_error("invalid PCAP snaplen");
#else
    (void)path;
    throw std::runtime_error(
        "PCAP disabled; configure -DPACKETFORGE_PCAP=ON (no external dependency)");
#endif
}
bool PcapReader::next(std::vector<uint8_t>& packet) {
    uint8_t h[16];
    file_.read(reinterpret_cast<char*>(h), 16);
    if (file_.gcount() == 0 && file_.eof())
        return false;
    if (file_.gcount() != 16)
        throw std::runtime_error("truncated PCAP record header");
    uint32_t captured = u32(h + 8), original = u32(h + 12);
    if (captured > snaplen_ || captured > original ||
        u32(h + 4) >= (nanoseconds_ ? 1000000000u : 1000000u))
        throw std::runtime_error("invalid PCAP record lengths/timestamp");
    packet.resize(captured);
    if (captured && !file_.read(reinterpret_cast<char*>(packet.data()), captured))
        throw std::runtime_error("truncated PCAP packet data");
    return true;
}
} // namespace pf
