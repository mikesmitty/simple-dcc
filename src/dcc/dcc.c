#include "dcc/dcc.h"
#include "dcc/functions.h"
#include <string.h>

void dcc_init(dcc_engine_t *dcc, QueueHandle_t output_queue) {
    memset(dcc->locos, 0, sizeof(dcc->locos));
    dcc->loop_state = LOOP_STATE_SPEED;
    dcc->mutex = xSemaphoreCreateMutex();
    dcc->output_queue = output_queue;
}

loco_state_t *dcc_get_or_create_loco(dcc_engine_t *dcc, uint16_t address) {
    xSemaphoreTake(dcc->mutex, portMAX_DELAY);

    // Look for existing
    for (int i = 0; i < MAX_LOCOS; i++) {
        if (dcc->locos[i].active && dcc->locos[i].address == address) {
            xSemaphoreGive(dcc->mutex);
            return &dcc->locos[i];
        }
    }

    // Create new
    for (int i = 0; i < MAX_LOCOS; i++) {
        if (!dcc->locos[i].active) {
            dcc->locos[i].active = true;
            dcc->locos[i].address = address;
            dcc->locos[i].speed_step = 0;
            dcc->locos[i].speed_mode = SPEED_MODE_128;
            dcc->locos[i].functions = 0;
            dcc->locos[i].group_flags = 0;
            xSemaphoreGive(dcc->mutex);
            return &dcc->locos[i];
        }
    }

    xSemaphoreGive(dcc->mutex);
    return NULL;
}

loco_state_t *dcc_get_loco(dcc_engine_t *dcc, uint16_t address) {
    xSemaphoreTake(dcc->mutex, portMAX_DELAY);
    for (int i = 0; i < MAX_LOCOS; i++) {
        if (dcc->locos[i].active && dcc->locos[i].address == address) {
            xSemaphoreGive(dcc->mutex);
            return &dcc->locos[i];
        }
    }
    xSemaphoreGive(dcc->mutex);
    return NULL;
}

void dcc_forget_loco(dcc_engine_t *dcc, uint16_t address) {
    dcc_emergency_stop(dcc, address);
    xSemaphoreTake(dcc->mutex, portMAX_DELAY);
    for (int i = 0; i < MAX_LOCOS; i++) {
        if (dcc->locos[i].active && dcc->locos[i].address == address) {
            dcc->locos[i].active = false;
            break;
        }
    }
    xSemaphoreGive(dcc->mutex);
}

// Build a new packet with the DCC address header
static dcc_packet_t *new_addressed_packet(uint16_t address) {
    dcc_packet_t *pkt = packet_alloc();
    if (!pkt) return NULL;

    if (address > SHORT_ADDRESS_MAX) {
        packet_add_byte(pkt, 0xC0 | (uint8_t)(address >> 8));
    }
    packet_add_byte(pkt, (uint8_t)address);
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
                                            uint8_t speed_step,
                                            speed_mode_t mode) {
    dcc_packet_t *pkt = new_addressed_packet(address);
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

void dcc_set_throttle(dcc_engine_t *dcc, uint16_t address,
                      uint8_t speed, bool direction) {
    loco_state_t *loco = dcc_get_or_create_loco(dcc, address);
    if (!loco) return;

    uint8_t speed_step = speed & 0x7F;
    if (direction) speed_step |= 0x80;

    loco->speed_step = speed_step;

    dcc_packet_t *pkt = build_throttle_packet(address, speed_step, loco->speed_mode);
    send_packet(dcc, pkt);
}

void dcc_set_function(dcc_engine_t *dcc, uint16_t address,
                      uint16_t fn_number, bool on) {
    loco_state_t *loco = dcc_get_or_create_loco(dcc, address);
    if (!loco) return;

    uint32_t prev = loco->functions;
    if (on) {
        loco->functions |= (1u << fn_number);
    } else {
        loco->functions &= ~(1u << fn_number);
    }

    if (loco->functions != prev && fn_number <= 28) {
        loco->group_flags = fn_update_group_flags(loco->group_flags, fn_number);
    }
}

void dcc_emergency_stop(dcc_engine_t *dcc, uint16_t address) {
    loco_state_t *loco = dcc_get_loco(dcc, address);
    speed_mode_t mode = loco ? loco->speed_mode : SPEED_MODE_128;
    dcc_packet_t *pkt = build_throttle_packet(address, 1, mode); // speed_step 1 = estop
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
        pkt = build_throttle_packet(loco->address, loco->speed_step, loco->speed_mode);
        break;
    case LOOP_STATE_FN_GROUP1:
        if (loco->group_flags & FN_GROUP1) {
            pkt = new_addressed_packet(loco->address);
            if (pkt) packet_add_byte(pkt, fn_encode_group1(loco->functions));
        }
        break;
    case LOOP_STATE_FN_GROUP2:
        if (loco->group_flags & FN_GROUP2) {
            pkt = new_addressed_packet(loco->address);
            if (pkt) packet_add_byte(pkt, fn_encode_group2(loco->functions));
        }
        break;
    case LOOP_STATE_FN_GROUP3:
        if (loco->group_flags & FN_GROUP3) {
            pkt = new_addressed_packet(loco->address);
            if (pkt) packet_add_byte(pkt, fn_encode_group3(loco->functions));
        }
        break;
    case LOOP_STATE_FN_GROUP4:
        if (loco->group_flags & FN_GROUP4) {
            pkt = new_addressed_packet(loco->address);
            if (pkt) {
                packet_add_byte(pkt, 0xDE);
                packet_add_byte(pkt, (uint8_t)(loco->functions >> 13));
            }
        }
        break;
    case LOOP_STATE_FN_GROUP5:
        if (loco->group_flags & FN_GROUP5) {
            pkt = new_addressed_packet(loco->address);
            if (pkt) {
                packet_add_byte(pkt, 0xDF);
                packet_add_byte(pkt, (uint8_t)(loco->functions >> 21));
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
