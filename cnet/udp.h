#ifndef PF_UDP_H
#define PF_UDP_H
#include "network_types.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    uint16_t source_port, destination_port, length;
} pf_udp;
pf_result pf_parse_udp(const uint8_t* p, size_t n, pf_udp* out);
#ifdef __cplusplus
}
#endif
#endif
