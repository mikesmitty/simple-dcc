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

// ---- DCC engine limits ----
#define MAX_LOCOS           50
#define PACKET_POOL_SIZE    64

// ---- LED (onboard, active high on this board) ----
#define PIN_LED             23

#endif // BOARD_CONFIG_H
