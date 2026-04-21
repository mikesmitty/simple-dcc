#ifndef MCP2515_H
#define MCP2515_H

#include <stdint.h>
#include <stdbool.h>

#include "hardware/spi.h"
#include "FreeRTOS.h"
#include "task.h"

/*
 * Minimal MCP2515 CAN controller driver for the Pico SDK.
 *
 * Scope: OpenLCB-style 29-bit extended frames only. One TXB used (TXB0);
 * RXB0+RXB1 both drained on the RX path. All frames are accepted — the
 * mask + filter registers are loaded with zeros. No bus-off recovery
 * automation (if the device goes bus-off, re-init from the application).
 */

typedef enum {
    MCP2515_CRYSTAL_8MHZ  = 0,
    MCP2515_CRYSTAL_16MHZ = 1,
} mcp2515_crystal_t;

typedef enum {
    MCP2515_BITRATE_125K = 0,
    MCP2515_BITRATE_250K = 1,
    MCP2515_BITRATE_500K = 2,
    MCP2515_BITRATE_1M   = 3,
} mcp2515_bitrate_t;

typedef struct {
    spi_inst_t        *spi;        /* spi0 or spi1 */
    uint8_t            pin_miso;
    uint8_t            pin_mosi;
    uint8_t            pin_sck;
    uint8_t            pin_cs;
    uint8_t            pin_int;    /* active-low, falling-edge IRQ */
    mcp2515_crystal_t  crystal;
    mcp2515_bitrate_t  bitrate;
} mcp2515_config_t;

typedef struct {
    uint32_t id;      /* 29-bit extended CAN identifier */
    uint8_t  dlc;     /* 0..8 data bytes */
    uint8_t  data[8];
} mcp2515_frame_t;

/* Initialize SPI, configure the MCP2515 (reset → CONFIG mode → timing →
 * filters → NORMAL mode), and install the INT-pin ISR. Returns false if
 * the chip does not respond to register round-trips.
 *
 * The SPI bus is configured to 10 MHz; the caller owns the SPI instance
 * so multiple devices on the same bus is possible but the caller must
 * serialize access (this driver does not share mutexes with others). */
bool mcp2515_init(const mcp2515_config_t *cfg);

/* Register a FreeRTOS task to be notified when RX data becomes available.
 * The ISR calls xTaskNotifyGiveFromISR() on this task; the task should
 * drain frames via mcp2515_try_receive() until it returns false. */
void mcp2515_set_rx_task(TaskHandle_t task);

/* Try to pull one frame from RXB0 / RXB1. Returns true if `out` was
 * populated, false if no frames are pending. Extended frames only;
 * standard-ID frames are silently dropped. */
bool mcp2515_try_receive(mcp2515_frame_t *out);

/* Load a frame into TXB0 and request send. Returns false if TXB0 is
 * still holding an un-transmitted frame from a prior call (caller may
 * retry later or drop). */
bool mcp2515_send(const mcp2515_frame_t *frame);

#endif /* MCP2515_H */
