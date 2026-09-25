#include "ethernet.h"
#include <string.h>
pf_result pf_parse_ethernet(const uint8_t* p, size_t n, pf_ethernet* o) {
    if (!p || !o || n < 14)
        return PF_TRUNCATED;
    memcpy(o->destination, p, 6);
    memcpy(o->source, p + 6, 6);
    o->ethertype = pf_be16(p + 12);
    return PF_OK;
}
