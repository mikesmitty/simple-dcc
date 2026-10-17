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
    uint16_t limit_override;    // ADC units; trips here instead when nonzero
    uint16_t last_reading;      // Last ADC reading
    uint16_t alert_peak;        // Highest reading since entering ALERT

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
                uint16_t current_limit_ma);
void motor_set_current_limit_ma(motor_t *m, uint16_t ma);
// Trip at `raw` ADC units instead of the configured limit until cleared with
// 0. Service Mode lends the programming track the main track's limit.
void motor_override_limit(motor_t *m, uint16_t raw);
// Off always wins. On starts a track that is off; one that's overloaded
// comes back on the FSM's backoff. Safe from any task.
void motor_set_power(motor_t *m, bool on);
void motor_update(motor_t *m);
bool motor_is_on(motor_t *m);

// Raw current-sense ADC reading for this channel. Safe from any task.
uint16_t motor_read_current_raw(motor_t *m);

// Current-sense calibration (SENSE_FACTOR_8874) in one place: mA <-> raw
// ADC units.
uint16_t motor_ma_to_raw(uint16_t ma);
uint16_t motor_raw_to_ma(uint16_t raw);

// Track monitor task data: array of motor pointers
typedef struct {
    motor_t **motors;
    uint8_t   count;
} track_monitor_params_t;

// FreeRTOS task: 1ms overcurrent monitoring
void task_track_monitor(void *params);

#endif // MOTOR_H
