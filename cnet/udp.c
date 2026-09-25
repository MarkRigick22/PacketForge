#include "udp.h"
pf_result pf_parse_udp(const uint8_t* p, size_t n, pf_udp* o) {
    if (!p || !o || n < 8)
        return PF_TRUNCATED;
    o->length = pf_be16(p + 4);
    if (o->length < 8)
        return PF_MALFORMED;
    if (o->length > n)
        return PF_TRUNCATED;
    o->source_port = pf_be16(p);
    o->destination_port = pf_be16(p + 2);
    return PF_OK;
}
