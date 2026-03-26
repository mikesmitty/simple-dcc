#ifndef MOTOR_PROFILE_H
#define MOTOR_PROFILE_H

#include "board_config.h"

// EX-MotorShield8874 profiles
// These are already defined in board_config.h as PIN_* constants.
// This header provides the sense factor and current limit for ADC calculations.

// ADC conversion: the RP2350 ADC is 12-bit (0-4095).
// Current (mA) = ADC_reading * (3300.0 / 4096.0) / SENSE_FACTOR_8874
// SENSE_FACTOR_8874 = 1.27 mV/mA
// So current limit in ADC units = max_mA * 1.27 * 4096.0 / 3300.0

#define ADC_CURRENT_LIMIT_MAIN  ((uint16_t)(MAX_CURRENT_MAIN_MA * SENSE_FACTOR_8874 * 4096.0f / 3300.0f))
#define ADC_CURRENT_LIMIT_PROG  ((uint16_t)(MAX_CURRENT_PROG_MA * SENSE_FACTOR_8874 * 4096.0f / 3300.0f))

#endif // MOTOR_PROFILE_H
