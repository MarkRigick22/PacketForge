#ifndef PF_ETHERNET_H
#define PF_ETHERNET_H
#include "network_types.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    uint8_t source[6], destination[6];
    uint16_t ethertype;
} pf_ethernet;
pf_result pf_parse_ethernet(const uint8_t* p, size_t n, pf_ethernet* out);
#ifdef __cplusplus
}
#endif
#endif
