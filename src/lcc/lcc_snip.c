#include "lcc/lcc_snip.h"
#include "lcc/lcc_defs.h"

#include <string.h>

/* OpenLCB spec caps: mfg/model ≤ 41, hw/sw ≤ 21, user_name/desc ≤ 63 (all
 * +1 for NUL). Worst-case payload is ~260 bytes; 320 leaves headroom so
 * append_* calls can never scribble off the end of the buffer. */
#define SNIP_MAX_BYTES 320

static int append_byte(uint8_t *buf, int pos, uint8_t b, int cap)
{
    if (pos < cap)
        buf[pos++] = b;
    return pos;
}

static int append_str(uint8_t *buf, int pos, const char *s, int cap)
{
    if (!s) s = "";
    while (*s && pos < cap - 1)
        buf[pos++] = (uint8_t)*s++;
    if (pos < cap)
        buf[pos++] = 0;  /* null terminator */
    return pos;
}

void lcc_snip_handle(const lcc_node_t *node, const lcc_frame_t *frame)
{
    const lcc_snip_t *snip = node->snip;
    if (!snip) return;

    uint8_t buf[SNIP_MAX_BYTES];
    int pos = 0;

    pos = append_byte(buf, pos, 4, SNIP_MAX_BYTES);  /* mfg info version */
    pos = append_str(buf, pos, snip->mfg_name,   SNIP_MAX_BYTES);
    pos = append_str(buf, pos, snip->model_name, SNIP_MAX_BYTES);
    pos = append_str(buf, pos, snip->hw_version, SNIP_MAX_BYTES);
    pos = append_str(buf, pos, snip->sw_version, SNIP_MAX_BYTES);

    pos = append_byte(buf, pos, 2, SNIP_MAX_BYTES);  /* user info version */
    pos = append_str(buf, pos, snip->user_name, SNIP_MAX_BYTES);
    pos = append_str(buf, pos, snip->user_desc, SNIP_MAX_BYTES);

    uint16_t src = lcc_get_src(frame->id);
    lcc_node_send_addressed(node, LCC_MTI_IDENT_INFO_REPLY, src,
                            buf, (uint16_t)pos);
}
