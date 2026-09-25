#ifndef PF_TCP_H
#define PF_TCP_H
#include "network_types.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    uint16_t source_port, destination_port;
    uint8_t header_length, flags;
} pf_tcp;
pf_result pf_parse_tcp(const uint8_t* p, size_t n, pf_tcp* out);
#ifdef __cplusplus
}
#endif
#endif
