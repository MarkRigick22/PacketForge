#include "packetforge/pcap_reader.hpp"
#include "packetforge/stats.hpp"
#include "packetforge/traffic.hpp"
#include "test.hpp"
#include <filesystem>
struct TempFile {
    std::filesystem::path path = std::filesystem::temp_directory_path() /
                                 ("packetforge-test-" + std::to_string(pf::now_ns()) + ".pcap");
    ~TempFile() {
        std::error_code e;
        std::filesystem::remove(path, e);
    }
};
[[maybe_unused]] static void write_pcap(const std::filesystem::path& path, bool little, bool nano,
                                        int corruption = 0) {
    std::ofstream f(path, std::ios::binary);
    auto u16 = [&](uint16_t v) {
        for (int i = 0; i < 2; ++i)
            f.put(static_cast<char>((v >> (8 * (little ? i : 1 - i))) & 255));
    };
    auto u32 = [&](uint32_t v) {
        for (int i = 0; i < 4; ++i)
            f.put(static_cast<char>((v >> (8 * (little ? i : 3 - i))) & 255));
    };
    u32(nano ? 0xa1b23c4d : 0xa1b2c3d4);
    u16(2);
    u16(4);
    u32(0);
    u32(0);
    u32(65535);
    u32(corruption == 1 ? 101 : 1);
    pf::TrafficGenerator g;
    auto p = g.packet(0);
    u32(0);
    u32(0);
    u32(corruption == 2 ? 0xffffffff : static_cast<uint32_t>(p.size()));
    u32(static_cast<uint32_t>(p.size()));
    f.write(reinterpret_cast<char*>(p.data()), corruption == 3 ? 10 : p.size());
}
TEST(pcap_endian_and_nanosecond_replay) {
#if PF_PCAP
    for (bool little : {false, true})
        for (bool nano : {false, true}) {
            TempFile t;
            write_pcap(t.path, little, nano);
            pf::PcapReader r(t.path.string());
            std::vector<uint8_t> p;
            CHECK(r.next(p));
            pf_packet parsed{};
            CHECK(pf_parse_packet(p.data(), p.size(), &parsed) == PF_OK);
            CHECK(!r.next(p));
        }
#else
    THROWS(pf::PcapReader("disabled.pcap"));
#endif
}
TEST(pcap_invalid_and_truncated) {
#if PF_PCAP
    TempFile t;
    THROWS(pf::PcapReader(t.path.string()));
    write_pcap(t.path, true, false, 1);
    THROWS(pf::PcapReader(t.path.string()));
    for (int bad : {2, 3}) {
        write_pcap(t.path, true, false, bad);
        pf::PcapReader r(t.path.string());
        std::vector<uint8_t> p;
        THROWS(r.next(p));
    }
    {
        std::ofstream f(t.path, std::ios::binary);
        f << "bad";
    }
    THROWS(pf::PcapReader(t.path.string()));
#endif
}
