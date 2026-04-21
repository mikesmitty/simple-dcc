#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdbool.h>
#include <stdint.h>
#include "motor/motor.h"
#include "dcc/dcc.h"

// Initialize the 128x64 SSD1306 OLED on the given I2C pins. Returns false
// and skips all I2C traffic if `enabled` is false, if the pin pair is not
// a valid RP2350 I2C mapping, or if the controller does not acknowledge.
//
// The referenced motor/dcc/version pointers must remain valid for the
// lifetime of task_display. `node_id` is the 48-bit CS node id (low bits
// are displayed); pass 0 if not yet known.
bool display_init(uint8_t sda_pin, uint8_t scl_pin, uint8_t i2c_addr,
                  bool enabled,
                  motor_t *motor_main, motor_t *motor_prog,
                  dcc_engine_t *dcc,
                  const char *version,
                  uint64_t node_id);

// FreeRTOS task: drives the boot splash, then the live status view.
void task_display(void *params);

#endif // DISPLAY_H
