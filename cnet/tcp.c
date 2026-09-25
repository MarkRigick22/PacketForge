#include "tcp.h"
pf_result pf_parse_tcp(const uint8_t* p, size_t n, pf_tcp* o) {
    if (!p || !o || n < 20)
        return PF_TRUNCATED;
    o->header_length = (uint8_t)((p[12] >> 4) * 4);
    if (o->header_length < 20)
        return PF_MALFORMED;
    if (o->header_length > n)
        return PF_TRUNCATED;
    o->source_port = pf_be16(p);
    o->destination_port = pf_be16(p + 2);
    o->flags = p[13];
    return PF_OK;
}
