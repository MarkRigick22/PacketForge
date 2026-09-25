#include "ipv4.h"
pf_result pf_parse_ipv4(const uint8_t* p, size_t n, pf_ipv4* o) {
    if (!p || !o || n < 20)
        return PF_TRUNCATED;
    if ((p[0] >> 4) != 4 || (p[0] & 15) < 5)
        return PF_MALFORMED;
    o->header_length = (uint8_t)((p[0] & 15) * 4);
    if (n < o->header_length)
        return PF_TRUNCATED;
    o->total_length = pf_be16(p + 2);
    if (o->total_length < o->header_length)
        return PF_MALFORMED;
    if (n < o->total_length)
        return PF_TRUNCATED;
    o->source = pf_be32(p + 12);
    o->destination = pf_be32(p + 16);
    o->protocol = p[9];
    o->ttl = p[8];
    o->fragment = pf_be16(p + 6);
    return PF_OK;
}
uint16_t pf_checksum(const uint8_t* p, size_t n) {
    uint32_t sum = 0;
    while (n >= 2) {
        sum += pf_be16(p);
        p += 2;
        n -= 2;
    }
    if (n)
        sum += (uint32_t)p[0] << 8;
    while (sum >> 16)
        sum = (sum & 65535) + (sum >> 16);
    return (uint16_t)~sum;
}
