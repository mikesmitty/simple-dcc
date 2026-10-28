#include "motor/motor.h"
#include "board_config.h"

#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "hardware/gpio.h"
#include "FreeRTOS.h"
#include "task.h"

// Timing constants (in ms)
#define MIN_OVERCURRENT_BACKOFF_MS    40
#define OVERCURRENT_FAULT_WINDOW_MS   5000
#define ALERT_WINDOW_MS               20
#define OVERCURRENT_TIMEOUT_MS        100
#define MAX_RETRY_WINDOW_MS           10000

static uint32_t ticks_now(void) {
    return xTaskGetTickCount();
}

static uint32_t ms_since(uint32_t tick) {
    return (ticks_now() - tick) * portTICK_PERIOD_MS;
}

// SENSE_FACTOR_8874 is mA per raw unit of the 12-bit ADC, so it already
// includes the ADC's 3.3 V / 4096 step.
uint16_t motor_ma_to_raw(uint16_t ma) {
    return (uint16_t)((float)ma / SENSE_FACTOR_8874 + 0.5f);
}

uint16_t motor_raw_to_ma(uint16_t raw) {
    return (uint16_t)((float)raw * SENSE_FACTOR_8874 + 0.5f);
}

void motor_init(motor_t *m, char track_id,
                uint32_t power_pin, uint32_t signal_pin, uint32_t brake_pin,
                uint32_t fault_pin, uint32_t adc_channel,
                uint16_t current_limit_ma) {
    m->track_id = track_id;
    m->power_pin = power_pin;
    m->signal_pin = signal_pin;
    m->brake_pin = brake_pin;
    m->fault_pin = fault_pin;
    m->adc_channel = adc_channel;
    m->current_limit = motor_ma_to_raw(current_limit_ma);
    m->limit_override = 0;
    m->last_reading = 0;
    m->alert_peak = 0;
    m->state = POWER_MODE_OFF;
    m->prev_state = POWER_MODE_OFF;
    m->state_entered_tick = 0;
    m->last_bad_tick = 0;
    m->overcurrent_backoff_ms = MIN_OVERCURRENT_BACKOFF_MS;

    // Init power pin (output, default off)
    gpio_init(power_pin);
    gpio_set_dir(power_pin, GPIO_OUT);
    gpio_put(power_pin, 0);

    // Init brake pin (output, default off — PIO controls it during operation)
    gpio_init(brake_pin);
    gpio_set_dir(brake_pin, GPIO_OUT);
    gpio_put(brake_pin, 0);

    // Init fault pin (input, active low)
    gpio_init(fault_pin);
    gpio_set_dir(fault_pin, GPIO_IN);
    gpio_pull_up(fault_pin);

    // Init ADC. Channel n is on GPIO ADC_BASE_PIN + n (40 + n on the
    // RP2350B); the last channel is the temperature sensor, with no pin.
    adc_init();
    if (adc_channel < NUM_ADC_CHANNELS - 1)
        adc_gpio_init(ADC_BASE_PIN + adc_channel);
}

void motor_set_current_limit_ma(motor_t *m, uint16_t ma) {
    m->current_limit = motor_ma_to_raw(ma);
}

void motor_override_limit(motor_t *m, uint16_t raw) {
    m->limit_override = raw;
}

static uint16_t trip_limit(const motor_t *m) {
    return m->limit_override ? m->limit_override : m->current_limit;
}

uint16_t motor_read_current_raw(motor_t *m) {
    // The ADC mux is shared by the track monitor and the Service Mode ACK
    // detector, which can run on different cores. Select + convert must be
    // one step or a reader gets the other track's channel. A conversion is
    // ~2 us, so the critical section is short.
    taskENTER_CRITICAL();
    adc_select_input(m->adc_channel);
    uint16_t v = adc_read();
    taskEXIT_CRITICAL();
    return v;
}

static bool motor_check_fault(motor_t *m) {
    return !gpio_get(m->fault_pin); // active low
}

static bool motor_check_overcurrent(motor_t *m) {
    m->last_reading = motor_read_current_raw(m);
    return m->last_reading > trip_limit(m);
}

// Call with the critical section held.
static void apply_mode(motor_t *m, power_mode_t mode) {
    if (m->state == mode) return;

    if (mode == POWER_MODE_ON || mode == POWER_MODE_ALERT) {
        gpio_put(m->power_pin, 1);
    } else {
        gpio_put(m->power_pin, 0);
    }

    m->state_entered_tick = ticks_now();
    m->state = mode;
}

// The track monitor steps the FSM while the LCC and Service Mode tasks
// switch power, on either core. Each FSM step moves from the state it read
// and only if that is still the state, checked and changed under one
// critical section, so a power-off landing mid-step can't be undone by it.
static bool motor_transition(motor_t *m, power_mode_t from, power_mode_t to) {
    taskENTER_CRITICAL();
    bool ok = m->state == from;
    if (ok)
        apply_mode(m, to);
    taskEXIT_CRITICAL();
    return ok;
}

void motor_set_power(motor_t *m, bool on) {
    taskENTER_CRITICAL();
    if (!on)
        apply_mode(m, POWER_MODE_OFF);
    else if (m->state == POWER_MODE_OFF)
        apply_mode(m, POWER_MODE_ON);
    // Already on, or overloaded: the FSM's backoff decides when power
    // comes back.
    taskEXIT_CRITICAL();
}

bool motor_is_on(motor_t *m) {
    return m->state == POWER_MODE_ON || m->state == POWER_MODE_ALERT;
}

static void handle_on_state(motor_t *m) {
    bool fault = motor_check_fault(m);
    bool overcurrent = motor_check_overcurrent(m);

    if (!overcurrent && !fault) {
        if (ms_since(m->state_entered_tick) > OVERCURRENT_FAULT_WINDOW_MS) {
            m->overcurrent_backoff_ms = MIN_OVERCURRENT_BACKOFF_MS;
        }
        return;
    }

    if (!motor_transition(m, POWER_MODE_ON, POWER_MODE_ALERT))
        return;
    m->alert_peak = m->last_reading;
    printf("TRACK %c ALERT%s%s %umA\n", m->track_id,
           fault ? " FAULT" : "", overcurrent ? " OVERCURRENT" : "",
           motor_raw_to_ma(m->last_reading));
}

static void handle_alert_state(motor_t *m) {
    uint32_t elapsed = ms_since(m->state_entered_tick);
    bool fault = motor_check_fault(m);
    bool overcurrent = motor_check_overcurrent(m);
    if (m->last_reading > m->alert_peak)
        m->alert_peak = m->last_reading;

    if (fault || overcurrent) {
        m->last_bad_tick = ticks_now();
        if (elapsed < OVERCURRENT_TIMEOUT_MS) return;

        if (motor_transition(m, POWER_MODE_ALERT, POWER_MODE_OVERLOAD))
            printf("TRACK %c OVERLOAD after %lums (%umA, peak %umA, limit %umA)\n",
                   m->track_id, (unsigned long)elapsed,
                   motor_raw_to_ma(m->last_reading),
                   motor_raw_to_ma(m->alert_peak),
                   motor_raw_to_ma(trip_limit(m)));
        return;
    }

    // No faults — check if alert window has passed
    if (elapsed > ALERT_WINDOW_MS &&
        motor_transition(m, POWER_MODE_ALERT, POWER_MODE_ON)) {
        printf("TRACK %c NORMAL (peak %umA)\n", m->track_id,
               motor_raw_to_ma(m->alert_peak));
    }
}

static void handle_overload_state(motor_t *m) {
    if (ms_since(m->state_entered_tick) < m->overcurrent_backoff_ms) {
        return;
    }

    // Switched off meanwhile: stays off.
    if (!motor_transition(m, POWER_MODE_OVERLOAD, POWER_MODE_ALERT))
        return;

    // Exponential backoff
    m->overcurrent_backoff_ms *= 2;
    if (m->overcurrent_backoff_ms > MAX_RETRY_WINDOW_MS) {
        m->overcurrent_backoff_ms = MAX_RETRY_WINDOW_MS;
    }

    printf("TRACK %c POWER RESTORE\n", m->track_id);
}

void motor_update(motor_t *m) {
    switch (m->state) {
    case POWER_MODE_OFF:
        m->overcurrent_backoff_ms = MIN_OVERCURRENT_BACKOFF_MS;
        break;
    case POWER_MODE_ON:
        handle_on_state(m);
        break;
    case POWER_MODE_ALERT:
        handle_alert_state(m);
        break;
    case POWER_MODE_OVERLOAD:
        handle_overload_state(m);
        break;
    }
    m->prev_state = m->state;
}

void task_track_monitor(void *params) {
    track_monitor_params_t *p = (track_monitor_params_t *)params;

    for (;;) {
        for (uint8_t i = 0; i < p->count; i++) {
            motor_update(p->motors[i]);
        }
        vTaskDelay(1); // 1ms monitoring period
    }
}
