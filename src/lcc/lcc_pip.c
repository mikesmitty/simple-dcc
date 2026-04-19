#include "lcc/lcc_pip.h"
#include "lcc/lcc_defs.h"

void lcc_pip_handle(const lcc_node_t *node, const lcc_frame_t *frame)
{
    uint8_t pip[6];
    uint64_t bits = node->pip_bits;
    pip[0] = (uint8_t)((bits >> 40) & 0xFF);
    pip[1] = (uint8_t)((bits >> 32) & 0xFF);
    pip[2] = (uint8_t)((bits >> 24) & 0xFF);
    pip[3] = (uint8_t)((bits >> 16) & 0xFF);
    pip[4] = (uint8_t)((bits >>  8) & 0xFF);
    pip[5] = (uint8_t)((bits >>  0) & 0xFF);

    uint16_t src = lcc_get_src(frame->id);
    lcc_node_send_addressed(node, LCC_MTI_PROTOCOL_SUPPORT_REPLY, src, pip, 6);
}
