#include "cnet/packet.h"
#include <cstddef>
#include <cstdint>
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    pf_packet result{};
    pf_parse_packet(data, size, &result);
    return 0;
}
