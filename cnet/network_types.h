#ifndef PF_NETWORK_TYPES_H
#define PF_NETWORK_TYPES_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef enum { PF_OK = 0, PF_TRUNCATED, PF_MALFORMED, PF_UNSUPPORTED } pf_result;
static inline uint16_t pf_be16(const uint8_t* p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}
static inline uint32_t pf_be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
uint16_t pf_checksum(const uint8_t* p, size_t n);
#ifdef __cplusplus
}
#endif
#endif
