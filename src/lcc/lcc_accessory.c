#include "lcc/lcc_accessory.h"
#include "lcc/lcc_defs.h"
#include "lcc/lcc_events.h"

#include <string.h>

#define ACCY_OUTPUTS    2048
#define ACCY_BASIC_BITS 12
#define ACCY_EXT_BITS   19

static const lcc_accessory_hooks_t *g_hooks;

/* One bit per output: has an event set its state, and is it normal. */
static uint32_t g_known[ACCY_OUTPUTS / 32];
static uint32_t g_normal[ACCY_OUTPUTS / 32];

/* Activate and deactivate events both set the output's state, as
 * OpenMRN's WellKnownEventRangeConsumer does. */
static void on_basic(lcc_node_t *node, uint64_t event_id)
{
    (void)node;
    uint16_t addr     = (uint16_t)((event_id >> 1) & (ACCY_OUTPUTS - 1));
    bool     normal   = event_id & 1;
    bool     activate = (event_id >> ACCY_BASIC_BITS)
                     == (LCC_EVENT_ACCY_ACTIVATE_BASE >> ACCY_BASIC_BITS);
    uint32_t bit = 1u << (addr & 31);

    g_known[addr / 32] |= bit;
    if (normal)
        g_normal[addr / 32] |= bit;
    else
        g_normal[addr / 32] &= ~bit;

    if (g_hooks && g_hooks->basic)
        g_hooks->basic(addr, normal, activate);
}

/* Valid if the event names the output's last state, Invalid if it names
 * the other one, Unknown until some event has set it. */
static lcc_event_state_t basic_state(const lcc_node_t *node, uint64_t event_id)
{
    (void)node;
    uint16_t addr = (uint16_t)((event_id >> 1) & (ACCY_OUTPUTS - 1));
    uint32_t bit  = 1u << (addr & 31);

    if (!(g_known[addr / 32] & bit))
        return LCC_EVENT_STATE_UNKNOWN;
    bool normal = (g_normal[addr / 32] & bit) != 0;
    return normal == (bool)(event_id & 1) ? LCC_EVENT_STATE_VALID
                                          : LCC_EVENT_STATE_INVALID;
}

static void on_extended(lcc_node_t *node, uint64_t event_id)
{
    (void)node;
    if (g_hooks && g_hooks->extended)
        g_hooks->extended((uint16_t)((event_id >> 8) & (ACCY_OUTPUTS - 1)),
                          (uint8_t)event_id);
}

void lcc_accessory_init(lcc_node_t *node, const lcc_accessory_hooks_t *hooks)
{
    g_hooks = hooks;
    memset(g_known, 0, sizeof(g_known));
    memset(g_normal, 0, sizeof(g_normal));

    lcc_events_register_consumer_range(node, LCC_EVENT_ACCY_ACTIVATE_BASE,
                                       ACCY_BASIC_BITS, on_basic, basic_state);
    lcc_events_register_consumer_range(node, LCC_EVENT_ACCY_DEACTIVATE_BASE,
                                       ACCY_BASIC_BITS, on_basic, basic_state);
    lcc_events_register_consumer_range(node, LCC_EVENT_ACCY_EXT_BASE,
                                       ACCY_EXT_BITS, on_extended, NULL);
}
