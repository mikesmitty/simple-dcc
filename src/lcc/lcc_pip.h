#ifndef LCC_PIP_H
#define LCC_PIP_H

#include "lcc/lcc_node.h"
#include "lcc/lcc_frame.h"

/*
 * Protocol Identification Protocol (PIP) handler.
 * Reply layout: the 48-bit PIP bitmap packed big-endian into a 6-byte
 * payload returned as an addressed Protocol Support Reply (MTI 0x0668)
 * to the node that issued the Protocol Support Inquiry (MTI 0x0828).
 */

/* Handle an incoming PIP request. `frame` is assumed to be addressed
 * and already verified to target `node->alias`. */
void lcc_pip_handle(const lcc_node_t *node, const lcc_frame_t *frame);

#endif /* LCC_PIP_H */
