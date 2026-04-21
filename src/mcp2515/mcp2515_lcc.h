#ifndef MCP2515_LCC_H
#define MCP2515_LCC_H

#include <stdbool.h>
#include "mcp2515/mcp2515.h"

/*
 * Wires the MCP2515 driver into the LCC transport layer:
 *   - Registers an egress callback with lcc_if so outbound LCC frames
 *     are transmitted on CAN in addition to USB.
 *   - Spawns an RX task that waits on the MCP2515 INT notification and
 *     drains frames into lcc_if_enqueue_frame().
 *
 * No-op if cfg is NULL. Returns false if the MCP2515 fails to respond
 * on SPI or if the egress slot is full.
 */
bool mcp2515_lcc_start(const mcp2515_config_t *cfg);

#endif /* MCP2515_LCC_H */
