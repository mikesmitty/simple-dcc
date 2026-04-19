#ifndef LCC_GC_H
#define LCC_GC_H

#include <stdint.h>
#include <stdbool.h>
#include "lcc/lcc_frame.h"

/*
 * GridConnect ASCII CAN frame codec.
 * Wire format: :X<8 hex CAN ID>N<data hex>;\n
 * Reference: OpenMRN GridConnect.hxx.
 */

/* Maximum encoded frame size (":X" + 8 id + "N" + 16 data + ";\n" + NUL). */
#define LCC_GC_MAX_LEN  30

/* Byte-by-byte parser state. Zero-initialize before first use. */
typedef struct {
    uint8_t  state;
    uint32_t id;
    uint8_t  data[8];
    uint8_t  dlc;
    uint8_t  nibble;
} lcc_gc_parser_t;

/* Encode `frame` as ASCII into `buf`. Returns bytes written (excluding the
 * NUL terminator), or -1 if buf_size < LCC_GC_MAX_LEN. */
int lcc_gc_encode(const lcc_frame_t *frame, char *buf, int buf_size);

/* Feed one character to the parser. Returns true when a complete frame has
 * been parsed into *frame; the parser is then ready for the next frame. */
bool lcc_gc_parse_byte(lcc_gc_parser_t *p, char c, lcc_frame_t *frame);

#endif /* LCC_GC_H */
