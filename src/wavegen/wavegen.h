#ifndef WAVEGEN_H
#define WAVEGEN_H

#include <stdbool.h>
#include <stdint.h>
#include "hardware/pio.h"
#include "dcc/packet.h"

typedef enum {
    WAVEGEN_NORMAL = 0,
    WAVEGEN_NO_CUTOUT,
    WAVEGEN_SERVICE_MODE,
} wavegen_mode_t;

typedef struct {
    PIO             wave_pio;
    uint            wave_sm;
    uint            wave_offset;
    PIO             cutout_pio;
    uint            cutout_sm;
    uint            cutout_offset;
    wavegen_mode_t  mode;
    bool            initialized;
} wavegen_t;

bool wavegen_init(wavegen_t *wg, wavegen_mode_t mode,
                  uint signal_pin, uint signal_pin_count, uint brake_pin);
void wavegen_enable(wavegen_t *wg, bool enabled);
void wavegen_send(wavegen_t *wg, dcc_packet_t *pkt);

// FreeRTOS task: blocks on a queue, feeds packets to PIO
void task_wavegen(void *params);

#endif // WAVEGEN_H
