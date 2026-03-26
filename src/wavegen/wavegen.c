#include "wavegen/wavegen.h"
#include "board_config.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "wavegen.pio.h"
#include "wavegen_nocutout.pio.h"
#include "wavegen_servicemode.pio.h"
#include "cutout.pio.h"

// Queue for packets to be sent to the PIO
QueueHandle_t wavegen_queue;

static bool init_wave_sm(wavegen_t *wg, wavegen_mode_t mode,
                         uint signal_pin, uint signal_pin_count) {
    switch (mode) {
    case WAVEGEN_NORMAL:
        if (!pio_can_add_program(pio0, &wavegen_program)) return false;
        wg->wave_offset = pio_add_program(pio0, &wavegen_program);
        wg->wave_sm = pio_claim_unused_sm(pio0, true);
        wavegen_program_init(pio0, wg->wave_sm, wg->wave_offset,
                             signal_pin, signal_pin_count);
        break;
    case WAVEGEN_NO_CUTOUT:
        if (!pio_can_add_program(pio0, &wavegen_nocutout_program)) return false;
        wg->wave_offset = pio_add_program(pio0, &wavegen_nocutout_program);
        wg->wave_sm = pio_claim_unused_sm(pio0, true);
        wavegen_nocutout_program_init(pio0, wg->wave_sm, wg->wave_offset,
                                      signal_pin, signal_pin_count);
        break;
    case WAVEGEN_SERVICE_MODE:
        if (!pio_can_add_program(pio0, &wavegen_servicemode_program)) return false;
        wg->wave_offset = pio_add_program(pio0, &wavegen_servicemode_program);
        wg->wave_sm = pio_claim_unused_sm(pio0, true);
        wavegen_servicemode_program_init(pio0, wg->wave_sm, wg->wave_offset,
                                         signal_pin);
        break;
    }
    wg->wave_pio = pio0;
    return true;
}

static bool init_cutout_sm(wavegen_t *wg, uint brake_pin) {
    if (!pio_can_add_program(pio1, &cutout_program)) return false;
    wg->cutout_offset = pio_add_program(pio1, &cutout_program);
    wg->cutout_sm = pio_claim_unused_sm(pio1, true);
    cutout_program_init(pio1, wg->cutout_sm, wg->cutout_offset, brake_pin);
    wg->cutout_pio = pio1;
    return true;
}

bool wavegen_init(wavegen_t *wg, wavegen_mode_t mode,
                  uint signal_pin, uint signal_pin_count, uint brake_pin) {
    wg->mode = mode;
    wg->initialized = false;

    if (!init_wave_sm(wg, mode, signal_pin, signal_pin_count)) {
        return false;
    }

    if (mode == WAVEGEN_NORMAL) {
        if (!init_cutout_sm(wg, brake_pin)) {
            return false;
        }
    }

    wg->initialized = true;
    wavegen_enable(wg, true);
    return true;
}

void wavegen_enable(wavegen_t *wg, bool enabled) {
    if (!wg->initialized) return;

    if (wg->mode == WAVEGEN_NORMAL) {
        pio_sm_set_enabled(wg->cutout_pio, wg->cutout_sm, enabled);
    }
    pio_sm_set_enabled(wg->wave_pio, wg->wave_sm, enabled);
}

void wavegen_send(wavegen_t *wg, dcc_packet_t *pkt) {
    if (!wg->initialized || pkt == NULL || packet_is_invalid(pkt)) {
        return;
    }

    encoded_packet_t enc = packet_encode(pkt);
    for (uint8_t i = 0; i < enc.count; i++) {
        while (pio_sm_is_tx_fifo_full(wg->wave_pio, wg->wave_sm)) {
            vTaskDelay(1);
        }
        pio_sm_put(wg->wave_pio, wg->wave_sm, enc.words[i]);
    }
}

void task_wavegen(void *params) {
    wavegen_t *wg = (wavegen_t *)params;
    dcc_packet_t *pkt;

    for (;;) {
        if (xQueueReceive(wavegen_queue, &pkt, portMAX_DELAY) == pdTRUE) {
            wavegen_send(wg, pkt);
            packet_free(pkt);
        }
    }
}
