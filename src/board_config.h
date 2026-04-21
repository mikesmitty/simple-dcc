#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

#include "pico/stdlib.h"

// ---- EX-MotorShield8874 pin assignments ----

// Channel A (Main track)
#define PIN_POWER_A         3
#define PIN_SIGNAL_A        22
#define PIN_BRAKE_A         9
#define PIN_FAULT_A         4
#define ADC_CHANNEL_A       1

// Channel B (Prog track)
#define PIN_POWER_B         11
#define PIN_SIGNAL_B        23
#define PIN_BRAKE_B         8
#define PIN_FAULT_B         5
#define ADC_CHANNEL_B       2

// ---- Motor shield current sensing ----
#define SENSE_FACTOR_8874   1.27f   // mA per ADC unit
#define MAX_CURRENT_MAIN_MA 2000
#define MAX_CURRENT_PROG_MA 250

// ---- OLED display (128x64 I2C SSD1306) defaults ----
#define DISPLAY_DEFAULT_SDA       20
#define DISPLAY_DEFAULT_SCL       21
#define DISPLAY_DEFAULT_I2C_ADDR  0x3C

// ---- MCP2515 CAN controller defaults (spi1 pin cluster, clear of the
//      EX-MotorShield8874 and OLED pins; change via CDI to match wiring) ----
#define MCP2515_DEFAULT_ENABLED   0   // opt-in: don't touch SPI if the chip isn't present
#define MCP2515_DEFAULT_SPI_INST  1   // 0 = spi0, 1 = spi1
#define MCP2515_DEFAULT_MISO      12
#define MCP2515_DEFAULT_MOSI      15
#define MCP2515_DEFAULT_SCK       14
#define MCP2515_DEFAULT_CS        13
#define MCP2515_DEFAULT_INT       24
#define MCP2515_DEFAULT_CRYSTAL   1   // 0 = 8 MHz, 1 = 16 MHz
#define MCP2515_DEFAULT_BITRATE   0   // 0 = 125k, 1 = 250k, 2 = 500k, 3 = 1M

// ---- DCC engine limits ----
#define MAX_LOCOS           50
#define PACKET_POOL_SIZE    80

#endif // BOARD_CONFIG_H
