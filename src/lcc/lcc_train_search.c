#include "lcc/lcc_train_search.h"
#include "lcc/lcc_defs.h"
#include "lcc/lcc_node.h"
#include "lcc/lcc_traction.h"

#include <string.h>

static lcc_train_alloc_fn g_alloc;

bool lcc_train_search_is_event(uint64_t event_id)
{
    return (event_id & LCC_EVENT_TRAIN_SEARCH_MASK)
         == LCC_EVENT_TRAIN_SEARCH_PREFIX;
}

static void extract_digits(uint64_t event_id, uint8_t *digits)
{
    uint32_t lower = (uint32_t)(event_id & 0xFFFFFFFFu);
    digits[0] = (uint8_t)((lower >> 28) & 0x0F);
    digits[1] = (uint8_t)((lower >> 24) & 0x0F);
    digits[2] = (uint8_t)((lower >> 20) & 0x0F);
    digits[3] = (uint8_t)((lower >> 16) & 0x0F);
    digits[4] = (uint8_t)((lower >> 12) & 0x0F);
    digits[5] = (uint8_t)((lower >>  8) & 0x0F);
}

uint8_t lcc_train_search_flags(uint64_t event_id)
{
    return (uint8_t)(event_id & 0xFF);
}

uint16_t lcc_train_search_address(uint64_t event_id)
{
    uint8_t digits[6];
    extract_digits(event_id, digits);
    uint16_t addr = 0;
    for (int i = 0; i < 6; i++) {
        if (digits[i] <= 9)
            addr = (uint16_t)(addr * 10 + digits[i]);
    }
    return addr;
}

uint64_t lcc_train_search_create_event(uint16_t address, uint8_t flags)
{
    uint8_t digits[6] = {0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F};
    int i = 5;
    if (address == 0) {
        digits[i] = 0;
    } else {
        while (address > 0 && i >= 0) {
            digits[i--] = (uint8_t)(address % 10);
            address = (uint16_t)(address / 10);
        }
    }
    uint32_t lower = 0;
    lower |= (uint32_t)digits[0] << 28;
    lower |= (uint32_t)digits[1] << 24;
    lower |= (uint32_t)digits[2] << 20;
    lower |= (uint32_t)digits[3] << 16;
    lower |= (uint32_t)digits[4] << 12;
    lower |= (uint32_t)digits[5] <<  8;
    lower |= (uint32_t)flags;
    return LCC_EVENT_TRAIN_SEARCH_PREFIX | (uint64_t)lower;
}

void lcc_train_search_set_allocator(lcc_train_alloc_fn alloc)
{
    g_alloc = alloc;
}

/* Reject searches whose digits contain reserved nibbles (0xA..0xE) or
 * whose flags set the reserved bit (0x10), or whose flags set a DCC
 * sub-flag without the DCC selector itself (S-9.7.0.2 §6.2). */
static bool has_reserved_values(const uint8_t *digits, uint8_t flags)
{
    for (int i = 0; i < 6; i++) {
        if (digits[i] >= 0x0A && digits[i] <= 0x0E)
            return true;
    }
    if (flags & LCC_TRAIN_SEARCH_FLAG_RESERVED)
        return true;
    if (!(flags & LCC_TRAIN_SEARCH_FLAG_DCC) && (flags & 0x07))
        return true;
    return false;
}

/* Decimal digit match. Skip leading 0xF padding; then compare either
 * exactly (EXACT flag) or as a prefix against the train's address
 * digits. */
static bool does_address_match(uint16_t train_address,
                               const uint8_t *digits, uint8_t flags)
{
    uint8_t train_digits[6];
    int     train_n = 0;

    if (train_address == 0) {
        train_digits[0] = 0;
        train_n = 1;
    } else {
        uint8_t rev[6];
        uint16_t t = train_address;
        while (t > 0 && train_n < 6) {
            rev[train_n++] = (uint8_t)(t % 10);
            t = (uint16_t)(t / 10);
        }
        for (int i = 0; i < train_n; i++)
            train_digits[i] = rev[train_n - 1 - i];
    }

    uint8_t search[6];
    int     search_n = 0;
    for (int i = 0; i < 6; i++) {
        if (digits[i] <= 9)
            search[search_n++] = digits[i];
    }
    if (search_n == 0)
        return false;

    if (flags & LCC_TRAIN_SEARCH_FLAG_EXACT) {
        if (search_n != train_n)
            return false;
    } else {
        if (search_n > train_n)
            return false;
    }
    for (int i = 0; i < search_n; i++) {
        if (search[i] != train_digits[i])
            return false;
    }
    return true;
}

/* Full train match per S-9.7.0.2 §6.3: DCC long/short filtering +
 * address match. Name match (flags without ADDR_ONLY) is not wired
 * up — train SNIPs don't carry a user-visible number today. */
static bool does_train_match(const lcc_train_state_t *train,
                             const uint8_t *digits,
                             uint16_t search_addr,
                             uint8_t flags)
{
    if (flags & LCC_TRAIN_SEARCH_FLAG_DCC) {
        if (flags & LCC_TRAIN_SEARCH_FLAG_LONG_ADDR) {
            if (!train->is_long_address)
                return false;
        } else if (!(flags & LCC_TRAIN_SEARCH_FLAG_ALLOCATE)) {
            /* Auto-pick from address range: short query wants a short
             * train, long query wants a long one. ALLOCATE skips this
             * gate so the throttle-chosen address range wins. */
            bool short_query = search_addr <= LCC_DCC_SHORT_ADDR_MAX;
            if (short_query && train->is_long_address)
                return false;
            if (!short_query && !train->is_long_address)
                return false;
        }
    }
    return does_address_match(train->dcc_address, digits, flags);
}

static void send_producer_identified_valid(const lcc_node_t *node,
                                           uint64_t event_id)
{
    uint8_t buf[8];
    lcc_event_to_buf(event_id, buf);
    lcc_node_send_global(node, LCC_MTI_PRODUCER_IDENTIFIED_VALID, buf, 8);
}

void lcc_train_search_dispatch(uint16_t src_alias, uint64_t event_id)
{
    (void)src_alias;

    if (!lcc_train_search_is_event(event_id))
        return;

    uint8_t digits[6];
    extract_digits(event_id, digits);
    uint8_t flags = lcc_train_search_flags(event_id);

    if (has_reserved_values(digits, flags))
        return;

    uint16_t search_addr = lcc_train_search_address(event_id);

    bool matched = false;
    int  n = lcc_node_count();
    for (int i = 0; i < n; i++) {
        lcc_node_t *node = lcc_node_at(i);
        if (!node || node->role != LCC_ROLE_TRAIN || !node->train)
            continue;
        if (!does_train_match(node->train, digits, search_addr, flags))
            continue;

        matched = true;
        if (node->alias_state == LCC_A_CONFIRMED)
            send_producer_identified_valid(node, event_id);
        else
            node->train->pending_search_event = event_id;
    }

    if (matched || !(flags & LCC_TRAIN_SEARCH_FLAG_ALLOCATE))
        return;
    if (!g_alloc)
        return;

    /* Allocator registers the new node and stashes event_id on
     * pending_search_event; flush_pending sends the reply once the
     * alias state machine reaches CONFIRMED. */
    (void)g_alloc(search_addr, flags, event_id);
}

void lcc_train_search_ensure_by_node_id(uint64_t node_id)
{
    /* DCC-proxy range is 06.01.00.00.00.00 + addr. Anything else is
     * a real node and not our business. */
    if ((node_id & 0xFFFFFFFF0000ULL) != LCC_TRAIN_NODE_ID_BASE)
        return;
    uint32_t addr32 = (uint32_t)(node_id & 0xFFFFULL);
    if (addr32 == 0 || addr32 > 0x3FFF)
        return;
    if (!g_alloc)
        return;

    uint8_t flags = LCC_TRAIN_SEARCH_FLAG_DCC;
    if (addr32 > LCC_DCC_SHORT_ADDR_MAX)
        flags |= LCC_TRAIN_SEARCH_FLAG_LONG_ADDR;

    /* event_id=0 — no deferred PRODUCER_IDENTIFIED_VALID owed; the VNI
     * path relies on AMD + Verified Node ID for discovery. */
    (void)g_alloc((uint16_t)addr32, flags, 0);
}

void lcc_train_search_flush_pending(void)
{
    int n = lcc_node_count();
    for (int i = 0; i < n; i++) {
        lcc_node_t *node = lcc_node_at(i);
        if (!node || node->role != LCC_ROLE_TRAIN || !node->train)
            continue;
        if (node->alias_state != LCC_A_CONFIRMED)
            continue;
        if (node->train->pending_search_event == 0)
            continue;

        send_producer_identified_valid(node,
                                       node->train->pending_search_event);
        node->train->pending_search_event = 0;
    }
}
