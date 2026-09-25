#ifndef PF_PACKET_H
#define PF_PACKET_H
#include "ethernet.h"
#include "ipv4.h"
#include "tcp.h"
#include "udp.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    pf_ethernet ethernet;
    pf_ipv4 ipv4;
    uint16_t source_port, destination_port;
    int has_ethernet, has_ipv4;
} pf_packet;
pf_result pf_parse_packet(const uint8_t* p, size_t n, pf_packet* out);
#ifdef __cplusplus
}
#endif
#endif
