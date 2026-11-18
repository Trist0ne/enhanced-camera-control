/* SYNC's vehicle-data service (VS_CANShadow): the subscription request, as the service's own struct for SYNC
 * 3.4.23188. A subscription names messages as bus << 16 | CAN id and how often to send each. */
#ifndef CANSHADOW_H
#define CANSHADOW_H

#include <stdint.h>

#define CS_SERVICE "VS_CANShadow"
#define CS_CMD_SUBSCRIBE 0x543f
#define CS_MAX_MSGS 50
#define CS_MAX_SIGS 15

struct cs_entry {
    uint32_t msg;      /* bus << 16 | CAN id */
    uint32_t mode;     /* 0 every frame, 1 the whole message when it changes, 2 a list of signals when they change */
    uint8_t nsig;
    uint8_t pad[3];
    uint32_t sig[CS_MAX_SIGS];
};
struct cs_request {
    uint8_t count;
    uint8_t pad[3];
    struct cs_entry e[CS_MAX_MSGS];
};

#endif
