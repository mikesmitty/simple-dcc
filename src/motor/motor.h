#ifndef MOTOR_H
#define MOTOR_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    POWER_MODE_OFF = 0,
    POWER_MODE_ON,
    POWER_MODE_ALERT,
    POWER_MODE_OVERLOAD,
} power_mode_t;

typedef struct {
    // Pin config
    uint32_t power_pin;
    uint32_t signal_pin;
    uint32_t brake_pin;
    uint32_t fault_pin;
    uint32_t adc_channel;

    // Current sensing
    uint16_t current_limit;     // ADC units
    uint16_t last_reading;      // Last ADC reading

    // Power state FSM
    power_mode_t state;
    power_mode_t prev_state;

    // Timing (in FreeRTOS ticks)
    uint32_t state_entered_tick;
    uint32_t last_bad_tick;
    uint32_t overcurrent_backoff_ms;

    // Track ID for logging
    char track_id;
} motor_t;

void motor_init(motor_t *m, char track_id,
                uint32_t power_pin, uint32_t signal_pin, uint32_t brake_pin,
                uint32_t fault_pin, uint32_t adc_channel,
                uint16_t current_limit);
void motor_set_power(motor_t *m, bool on);
void motor_update(motor_t *m);
bool motor_is_on(motor_t *m);

// FreeRTOS task: 1ms overcurrent monitoring
void task_track_monitor(void *params);

#endif // MOTOR_H
