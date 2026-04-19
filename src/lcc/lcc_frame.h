#ifndef LCC_FRAME_H
#define LCC_FRAME_H

#include <stdint.h>
#include <stdbool.h>
#include "lcc/lcc_defs.h"

/*
 * Raw CAN frame representation used throughout the LCC stack + helpers for
 * the addressed-message fragmentation header, datagram CAN-ID construction,
 * and CID-frame detection.
 */

typedef struct {
    uint32_t id;        /* 29-bit CAN ID (always extended, no EFF flag) */
    uint8_t  dlc;
    uint8_t  data[8];
} lcc_frame_t;

/* Addressed-message fragmentation header (first 2 bytes of payload): upper
 * nibble of data[0] is flags (LCC_NOT_FIRST_FRAME / LCC_NOT_LAST_FRAME),
 * low nibble of data[0] + all of data[1] is the 12-bit destination alias. */
static inline uint16_t lcc_frame_addressed_dst(const lcc_frame_t *f)
{
    return (uint16_t)(((uint16_t)(f->data[0] & 0x0F) << 8) | f->data[1]);
}

static inline uint8_t lcc_frame_addressed_flags(const lcc_frame_t *f)
{
    return (uint8_t)(f->data[0] & 0xF0);
}

static inline bool lcc_frame_addressed_is_first(const lcc_frame_t *f)
{
    return (lcc_frame_addressed_flags(f) & LCC_NOT_FIRST_FRAME) == 0;
}

static inline bool lcc_frame_addressed_is_last(const lcc_frame_t *f)
{
    return (lcc_frame_addressed_flags(f) & LCC_NOT_LAST_FRAME) == 0;
}

/* Build the 2-byte addressed header in `out` for the given destination
 * alias and continuation flags. */
static inline void lcc_frame_build_addressed_hdr(uint8_t *out,
                                                 uint16_t dst_alias,
                                                 uint8_t flags)
{
    out[0] = (uint8_t)(((dst_alias >> 8) & 0x0F) | (flags & 0xF0));
    out[1] = (uint8_t)(dst_alias & 0xFF);
}

/* Build a datagram CAN ID. `frame_type` is one of LCC_FRAME_DATAGRAM_*. */
static inline uint32_t lcc_frame_datagram_id(uint8_t frame_type,
                                             uint16_t dst_alias,
                                             uint16_t src_alias)
{
    return (src_alias & 0xFFF)
         | ((uint32_t)(dst_alias & 0xFFF) << 12)
         | ((uint32_t)frame_type << LCC_CAN_FRAME_TYPE_SHIFT)
         | ((uint32_t)LCC_FRAME_TYPE_OPENLCB << LCC_FRAME_TYPE_SHIFT)
         | ((uint32_t)LCC_PRIORITY_NORMAL << LCC_PRIORITY_SHIFT);
}

/* CID frames are control frames whose 3-bit can_frame_type equals 4..7
 * (i.e. the top two bits of the field are 01). Tests 0x1C mask & 0x14. */
static inline bool lcc_frame_is_cid(uint32_t can_id)
{
    if (!lcc_is_control_frame(can_id))
        return false;
    return ((can_id >> LCC_CAN_FRAME_TYPE_SHIFT) & 0x1C) == 0x14;
}

#endif /* LCC_FRAME_H */
