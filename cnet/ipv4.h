#ifndef PF_IPV4_H
#define PF_IPV4_H
#include "network_types.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    uint32_t source, destination;
    uint16_t total_length, fragment;
    uint8_t protocol, header_length, ttl;
} pf_ipv4;
pf_result pf_parse_ipv4(const uint8_t* p, size_t n, pf_ipv4* out);
#ifdef __cplusplus
}
#endif
#endif
