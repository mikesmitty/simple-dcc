#ifndef SERIAL_H
#define SERIAL_H

#include <stdbool.h>
#include <stdint.h>

void serial_init(void);
void serial_write(const uint8_t *data, uint16_t len);
bool serial_write_ready(void);

// FreeRTOS task: reads USB CDC, parses GridConnect, feeds CAN RX
void task_serial(void *params);

#endif // SERIAL_H
