#include "dcc/dcc.h"
#include "dcc/functions.h"
#include <string.h>

void dcc_init(dcc_engine_t *dcc, QueueHandle_t output_queue) {
    memset(dcc->locos, 0, sizeof(dcc->locos));
    dcc->loop_state = LOOP_STATE_SPEED;
    dcc->mutex = xSemaphoreCreateMutex();
    configASSERT(dcc->mutex != NULL);
    dcc->output_queue = output_queue;
}

// Helpers below must be called with dcc->mutex held.
static loco_state_t *find_loco_locked(dcc_engine_t *dcc, uint16_t address) {
    for (int i = 0; i < MAX_LOCOS; i++) {
        if (dcc->locos[i].active && dcc->locos[i].address == address) {
            return &dcc->locos[i];
        }
    }
    return NULL;
}

static loco_state_t *find_or_create_loco_locked(dcc_engine_t *dcc,
                                                 uint16_t address,
                                                 bool is_long) {
    loco_state_t *found = find_loco_locked(dcc, address);
    if (found) {
        /* Honor the caller's addressing choice if they disagree — a re-
         * ensure with a different is_long is the only way we learn the
         * decoder's actual addressing mode once the throttle declares it. */
        if (address > SHORT_ADDRESS_MAX) is_long = true;
        found->is_long_address = is_long;
        return found;
    }

    for (int i = 0; i < MAX_LOCOS; i++) {
        if (!dcc->locos[i].active) {
            dcc->locos[i].active = true;
            dcc->locos[i].address = address;
            dcc->locos[i].is_long_address =
                is_long || address > SHORT_ADDRESS_MAX;
            dcc->locos[i].speed_step = 0;
            dcc->locos[i].speed_mode = SPEED_MODE_128;
            dcc->locos[i].functions = 0;
            dcc->locos[i].functions_hi = 0;
            dcc->locos[i].group_flags = 0;
            return &dcc->locos[i];
        }
    }
    return NULL;
}

void dcc_ensure_loco(dcc_engine_t *dcc, uint16_t address, bool is_long) {
    xSemaphoreTake(dcc->mutex, portMAX_DELAY);
    find_or_create_loco_locked(dcc, address, is_long);
    xSemaphoreGive(dcc->mutex);
}

void dcc_forget_loco(dcc_engine_t *dcc, uint16_t address) {
    xSemaphoreTake(dcc->mutex, portMAX_DELAY);
    loco_state_t *loco = find_loco_locked(dcc, address);
    bool is_long = loco ? loco->is_long_address : (address > SHORT_ADDRESS_MAX);
    xSemaphoreGive(dcc->mutex);

    dcc_emergency_stop(dcc, address, is_long);

    xSemaphoreTake(dcc->mutex, portMAX_DELAY);
    loco = find_loco_locked(dcc, address);
    if (loco) loco->active = false;
    xSemaphoreGive(dcc->mutex);
}

// Build a new packet with the DCC address header. `is_long` chooses
// between the 1-byte short form (0AAAAAAA) and 2-byte long form
// (11AAAAAA AAAAAAAA). Addresses > SHORT_ADDRESS_MAX are always long.
static dcc_packet_t *new_addressed_packet(uint16_t address, bool is_long) {
    dcc_packet_t *pkt = packet_alloc();
    if (!pkt) return NULL;

    if (is_long || address > SHORT_ADDRESS_MAX) {
        packet_add_byte(pkt, 0xC0 | (uint8_t)((address >> 8) & 0x3F));
        packet_add_byte(pkt, (uint8_t)address);
    } else {
        packet_add_byte(pkt, (uint8_t)address);
    }
    pkt->address = address;
    return pkt;
}

static uint8_t speed_step_28(uint8_t speed_step) {
    uint8_t speed = speed_step & 0x7F;
    bool direction = (speed_step & 0x80) != 0;

    uint8_t code28;
    if (speed <= 1) {
        code28 = speed;
    } else {
        uint8_t speed28 = (speed * 10 + 36) / 46;
        code28 = (speed28 + 3) / 2;
        if ((speed28 & 1) == 0) {
            code28 |= 0x10;
        }
    }

    uint8_t res = 0x40 | code28;
    if (direction) {
        res |= 0x20;
    }
    return res;
}

static dcc_packet_t *build_throttle_packet(uint16_t address,
                                            bool is_long,
                                            uint8_t speed_step,
                                            speed_mode_t mode) {
    dcc_packet_t *pkt = new_addressed_packet(address, is_long);
    if (!pkt) return NULL;

    switch (mode) {
    case SPEED_MODE_28:
        packet_add_byte(pkt, speed_step_28(speed_step));
        break;
    case SPEED_MODE_128:
    default:
        packet_add_byte(pkt, CMD_SET_SPEED);
        packet_add_byte(pkt, speed_step);
        break;
    case SPEED_MODE_14:
        break; // not supported
    }

    pkt->priority = (speed_step < 2) ? PRIORITY_EMERGENCY : PRIORITY_HIGH;
    pkt->repeats = 0;
    return pkt;
}

static void send_packet(dcc_engine_t *dcc, dcc_packet_t *pkt) {
    if (!pkt) return;
    if (xQueueSend(dcc->output_queue, &pkt, pdMS_TO_TICKS(10)) != pdTRUE) {
        packet_free(pkt);
    }
}

void dcc_set_throttle(dcc_engine_t *dcc, uint16_t address, bool is_long,
                      uint8_t speed, bool direction) {
    uint8_t speed_step = speed & 0x7F;
    if (direction) speed_step |= 0x80;

    speed_mode_t snapshot_mode = SPEED_MODE_128;
    bool         snapshot_long = is_long || address > SHORT_ADDRESS_MAX;
    bool ok = false;

    xSemaphoreTake(dcc->mutex, portMAX_DELAY);
    loco_state_t *loco = find_or_create_loco_locked(dcc, address, is_long);
    if (loco) {
        loco->speed_step = speed_step;
        snapshot_mode = loco->speed_mode;
        snapshot_long = loco->is_long_address;
        ok = true;
    }
    xSemaphoreGive(dcc->mutex);

    if (!ok) return;
    dcc_packet_t *pkt = build_throttle_packet(address, snapshot_long,
                                              speed_step, snapshot_mode);
    send_packet(dcc, pkt);
}

void dcc_set_function(dcc_engine_t *dcc, uint16_t address, bool is_long,
                      uint16_t fn_number, bool on) {
    if (fn_number > MAX_FN_NUMBER) return;

    xSemaphoreTake(dcc->mutex, portMAX_DELAY);
    loco_state_t *loco = find_or_create_loco_locked(dcc, address, is_long);
    if (loco) {
        uint64_t prev_lo = loco->functions;
        uint8_t  prev_hi = loco->functions_hi;
        if (fn_number < 64) {
            uint64_t bit = (uint64_t)1 << fn_number;
            if (on) {
                loco->functions |= bit;
            } else {
                loco->functions &= ~bit;
            }
        } else {
            uint8_t bit = (uint8_t)(1u << (fn_number - 64));
            if (on) {
                loco->functions_hi |= bit;
            } else {
                loco->functions_hi &= (uint8_t)~bit;
            }
        }
        if (loco->functions != prev_lo || loco->functions_hi != prev_hi) {
            loco->group_flags = fn_update_group_flags(loco->group_flags, fn_number);
        }
    }
    xSemaphoreGive(dcc->mutex);
}

void dcc_emergency_stop(dcc_engine_t *dcc, uint16_t address, bool is_long) {
    speed_mode_t mode = SPEED_MODE_128;
    bool         addr_is_long = is_long || address > SHORT_ADDRESS_MAX;
    xSemaphoreTake(dcc->mutex, portMAX_DELAY);
    loco_state_t *loco = find_loco_locked(dcc, address);
    if (loco) {
        mode = loco->speed_mode;
        addr_is_long = loco->is_long_address;
    }
    xSemaphoreGive(dcc->mutex);

    dcc_packet_t *pkt = build_throttle_packet(address, addr_is_long, 1, mode); // step 1 = estop
    if (pkt) {
        pkt->priority = PRIORITY_EMERGENCY;
        send_packet(dcc, pkt);
    }
}

void dcc_emergency_stop_all(dcc_engine_t *dcc) {
    // Broadcast emergency stop (address 0)
    dcc_packet_t *pkt = packet_alloc();
    if (!pkt) return;
    packet_add_byte(pkt, 0x00);
    packet_add_byte(pkt, 0x71); // broadcast estop
    pkt->address = 0;
    pkt->priority = PRIORITY_EMERGENCY;
    pkt->repeats = 3;
    send_packet(dcc, pkt);
}

static void send_reminder_packets(dcc_engine_t *dcc, loco_state_t *loco) {
    dcc_packet_t *pkt = NULL;

    switch (dcc->loop_state) {
    case LOOP_STATE_SPEED:
        pkt = build_throttle_packet(loco->address, loco->is_long_address,
                                    loco->speed_step, loco->speed_mode);
        break;
    case LOOP_STATE_FN_GROUP1:
        if (loco->group_flags & FN_GROUP1) {
            pkt = new_addressed_packet(loco->address, loco->is_long_address);
            if (pkt) packet_add_byte(pkt, fn_encode_group1(loco->functions));
        }
        break;
    case LOOP_STATE_FN_GROUP2:
        if (loco->group_flags & FN_GROUP2) {
            pkt = new_addressed_packet(loco->address, loco->is_long_address);
            if (pkt) packet_add_byte(pkt, fn_encode_group2(loco->functions));
        }
        break;
    case LOOP_STATE_FN_GROUP3:
        if (loco->group_flags & FN_GROUP3) {
            pkt = new_addressed_packet(loco->address, loco->is_long_address);
            if (pkt) packet_add_byte(pkt, fn_encode_group3(loco->functions));
        }
        break;
    case LOOP_STATE_FN_GROUP4:
        if (loco->group_flags & FN_GROUP4) {
            pkt = new_addressed_packet(loco->address, loco->is_long_address);
            if (pkt) {
                packet_add_byte(pkt, 0xDE);
                packet_add_byte(pkt, fn_encode_group4(loco->functions));
            }
        }
        break;
    case LOOP_STATE_FN_GROUP5:
        if (loco->group_flags & FN_GROUP5) {
            pkt = new_addressed_packet(loco->address, loco->is_long_address);
            if (pkt) {
                packet_add_byte(pkt, 0xDF);
                packet_add_byte(pkt, fn_encode_group5(loco->functions));
            }
        }
        break;
    case LOOP_STATE_FN_GROUP6:
        if (loco->group_flags & FN_GROUP6) {
            pkt = new_addressed_packet(loco->address, loco->is_long_address);
            if (pkt) {
                packet_add_byte(pkt, 0xD8);
                packet_add_byte(pkt, fn_encode_group6(loco->functions));
            }
        }
        break;
    case LOOP_STATE_FN_GROUP7:
        if (loco->group_flags & FN_GROUP7) {
            pkt = new_addressed_packet(loco->address, loco->is_long_address);
            if (pkt) {
                packet_add_byte(pkt, 0xD9);
                packet_add_byte(pkt, fn_encode_group7(loco->functions));
            }
        }
        break;
    case LOOP_STATE_FN_GROUP8:
        if (loco->group_flags & FN_GROUP8) {
            pkt = new_addressed_packet(loco->address, loco->is_long_address);
            if (pkt) {
                packet_add_byte(pkt, 0xDA);
                packet_add_byte(pkt, fn_encode_group8(loco->functions));
            }
        }
        break;
    case LOOP_STATE_FN_GROUP9:
        if (loco->group_flags & FN_GROUP9) {
            pkt = new_addressed_packet(loco->address, loco->is_long_address);
            if (pkt) {
                packet_add_byte(pkt, 0xDB);
                packet_add_byte(pkt, fn_encode_group9(loco->functions));
            }
        }
        break;
    case LOOP_STATE_FN_GROUP10:
        if (loco->group_flags & FN_GROUP10) {
            pkt = new_addressed_packet(loco->address, loco->is_long_address);
            if (pkt) {
                packet_add_byte(pkt, 0xDC);
                packet_add_byte(pkt, fn_encode_group10(loco->functions, loco->functions_hi));
            }
        }
        break;
    default:
        break;
    }

    if (pkt) {
        pkt->priority = PRIORITY_LOW;
        pkt->repeats = 0;
        send_packet(dcc, pkt);
    }
}

void dcc_update(dcc_engine_t *dcc) {
    xSemaphoreTake(dcc->mutex, portMAX_DELAY);
    for (int i = 0; i < MAX_LOCOS; i++) {
        if (dcc->locos[i].active) {
            send_reminder_packets(dcc, &dcc->locos[i]);
        }
    }
    xSemaphoreGive(dcc->mutex);

    dcc->loop_state++;
    if (dcc->loop_state >= LOOP_STATE_RESTART) {
        dcc->loop_state = LOOP_STATE_SPEED;
    }
}

void task_dcc_reminder(void *params) {
    dcc_engine_t *dcc = (dcc_engine_t *)params;

    for (;;) {
        dcc_update(dcc);
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
