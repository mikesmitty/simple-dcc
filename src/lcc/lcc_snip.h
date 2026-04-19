#ifndef LCC_SNIP_H
#define LCC_SNIP_H

#include <stdint.h>
#include "lcc/lcc_node.h"
#include "lcc/lcc_frame.h"

/*
 * Simple Node Info (SNIP) content + reply handler.
 *
 * Wire layout for a v4/v2 SNIP reply:
 *   [1] mfg_version (=4)
 *   [N] mfg_name      (null-terminated ASCII)
 *   [N] model_name
 *   [N] hw_version
 *   [N] sw_version
 *   [1] user_version  (=2)
 *   [N] user_name     (from ACDI)
 *   [N] user_desc     (from ACDI)
 *
 * The manufacturer half is static per build; the user half is pulled
 * from the ACDI config at reply time (stubbed in M3, wired to the real
 * config memory in M4 with the ACDI 0xFB space).
 */

typedef struct lcc_snip {
    const char *mfg_name;
    const char *model_name;
    const char *hw_version;
    const char *sw_version;
    /* Pointer to user name + description in the ACDI region. May be NULL
     * before M4 wires the config-memory handlers; the reply will emit
     * empty strings in that case. */
    const char *user_name;
    const char *user_desc;
} lcc_snip_t;

/* Handle an Ident Info Request (MTI 0x0DE8). `frame` is assumed
 * addressed to this node; the reply fragments into 6-byte payload
 * chunks with NOT_FIRST_FRAME / NOT_LAST_FRAME continuation flags. */
void lcc_snip_handle(const lcc_node_t *node, const lcc_frame_t *frame);

#endif /* LCC_SNIP_H */
