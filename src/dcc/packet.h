#ifndef PACKET_H
#define PACKET_H

#include <stdbool.h>
#include <stdint.h>

#define MAX_PACKET_DATA   6
#define MAX_ENCODED_WORDS 3
#define PACKET_POOL_SIZE  64

#define BROADCAST_ADDRESS 0xFFFF

typedef enum {
    PRIORITY_BEST_EFFORT = 0,
    PRIORITY_LOW         = 25,
    PRIORITY_NORMAL      = 50,
    PRIORITY_HIGH        = 75,
    PRIORITY_EMERGENCY   = 100,
} dcc_priority_t;

typedef struct {
    uint8_t        data[MAX_PACKET_DATA];
    uint8_t        len;
    uint16_t       address;
    dcc_priority_t priority;
    int8_t         repeats;
    bool           in_use;
} dcc_packet_t;

typedef struct {
    uint32_t words[MAX_ENCODED_WORDS];
    uint8_t  count;
} encoded_packet_t;

void           packet_pool_init(void);
dcc_packet_t  *packet_alloc(void);
void           packet_free(dcc_packet_t *pkt);

void           packet_reset(dcc_packet_t *pkt);
void           packet_fill(dcc_packet_t *pkt, const uint8_t *data, uint8_t len,
                            uint16_t address, dcc_priority_t priority, int8_t repeats);
void           packet_add_byte(dcc_packet_t *pkt, uint8_t b);
bool           packet_is_invalid(const dcc_packet_t *pkt);
encoded_packet_t packet_encode(dcc_packet_t *pkt);

void           packet_make_idle(dcc_packet_t *pkt);

#endif // PACKET_H
