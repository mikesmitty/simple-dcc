#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

#include "pico/stdlib.h"

// ---- EX-MotorShield8874 pin assignments ----
// GPIO numbers for the shield's default jumpers on an Adafruit Metro RP2350.
// The Metro's D12/D13 header positions are GPIO 22/23, and A0-A5 are
// GPIO 41-46 (ADC channels 1-6).

// Channel A (Main track)
#define PIN_POWER_A         3   // D3
#define PIN_SIGNAL_A        22  // D12
#define PIN_BRAKE_A         9   // D9
#define PIN_FAULT_A         45  // A4 (nFAULT)
#define ADC_CHANNEL_A       1   // A0

// Channel B (Prog track)
#define PIN_POWER_B         11  // D11
#define PIN_SIGNAL_B        23  // D13 (also the Metro's red LED)
#define PIN_BRAKE_B         8   // D8
#define PIN_FAULT_B         46  // A5 (nFAULT)
#define ADC_CHANNEL_B       2   // A1

// ---- RailCom detector shield (hardware/railcom-detector) ----
// Its data comes in on D0, UART0 RX. The cutout gate is the main channel's
// brake pin (PIN_BRAKE_A, D9).
#define PIN_RAILCOM_RX      1   // D0

// ---- Motor shield current sensing ----
// mA per raw unit of the 12-bit ADC: mA = raw * SENSE_FACTOR_8874. From the
// EX-MotorShield8874 README (1.27 at 5 V IOREF, 1.24 at 3.3 V); DCC-EX uses
// 1.27 for 3.3 V boards. Convert with motor_ma_to_raw / motor_raw_to_ma.
#define SENSE_FACTOR_8874   1.27f
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
