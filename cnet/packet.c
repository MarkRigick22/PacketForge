#include "packet.h"
#include <string.h>
pf_result pf_parse_packet(const uint8_t* p, size_t n, pf_packet* o) {
    pf_result r;
    if (!o)
        return PF_MALFORMED;
    memset(o, 0, sizeof(*o));
    r = pf_parse_ethernet(p, n, &o->ethernet);
    if (r != PF_OK)
        return r;
    o->has_ethernet = 1;
    if (o->ethernet.ethertype != 0x0800)
        return PF_UNSUPPORTED;
    r = pf_parse_ipv4(p + 14, n - 14, &o->ipv4);
    if (r != PF_OK)
        return r;
    o->has_ipv4 = 1;
    /* Fragmented transport headers cannot safely form a complete five-tuple. */
    if (o->ipv4.fragment & 0x3fff)
        return PF_UNSUPPORTED;
    p += 14 + o->ipv4.header_length;
    n = o->ipv4.total_length - o->ipv4.header_length;
    if (o->ipv4.protocol == 6) {
        pf_tcp t;
        r = pf_parse_tcp(p, n, &t);
        if (r != PF_OK)
            return r;
        o->source_port = t.source_port;
        o->destination_port = t.destination_port;
    } else if (o->ipv4.protocol == 17) {
        pf_udp u;
        r = pf_parse_udp(p, n, &u);
        if (r != PF_OK)
            return r;
        o->source_port = u.source_port;
        o->destination_port = u.destination_port;
    } else
        return PF_UNSUPPORTED;
    return PF_OK;
}
