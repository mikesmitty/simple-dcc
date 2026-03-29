#include "motor/motor.h"
#include "motor/profile.h"
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

static uint16_t ma_to_adc_units(uint16_t ma) {
    // Current (mA) = ADC_reading * (3300.0 / 4096.0) / SENSE_FACTOR_8874
    // ADC_reading = Current (mA) * SENSE_FACTOR_8874 * 4096.0 / 3300.0
    return (uint16_t)((float)ma * SENSE_FACTOR_8874 * 4096.0f / 3300.0f);
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
    m->current_limit = ma_to_adc_units(current_limit_ma);
    m->last_reading = 0;
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

    // Init ADC
    adc_init();
    adc_gpio_init(26 + adc_channel);
}

void motor_set_current_limit_ma(motor_t *m, uint16_t ma) {
    m->current_limit = ma_to_adc_units(ma);
}

static uint16_t motor_read_adc(motor_t *m) {
    adc_select_input(m->adc_channel);
    return adc_read();
}

static bool motor_check_fault(motor_t *m) {
    return !gpio_get(m->fault_pin); // active low
}

static bool motor_check_overcurrent(motor_t *m) {
    m->last_reading = motor_read_adc(m);
    return m->last_reading > m->current_limit;
}

static void motor_set_mode(motor_t *m, power_mode_t mode) {
    if (m->state == mode) return;

    if (mode == POWER_MODE_ON || mode == POWER_MODE_ALERT) {
        gpio_put(m->power_pin, 1);
    } else {
        gpio_put(m->power_pin, 0);
    }

    m->state_entered_tick = ticks_now();
    m->state = mode;
}

void motor_set_power(motor_t *m, bool on) {
    motor_set_mode(m, on ? POWER_MODE_ON : POWER_MODE_OFF);
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

    printf("TRACK %c ALERT%s%s\n", m->track_id,
           fault ? " FAULT" : "", overcurrent ? " OVERCURRENT" : "");
    motor_set_mode(m, POWER_MODE_ALERT);
}

static void handle_alert_state(motor_t *m) {
    uint32_t elapsed = ms_since(m->state_entered_tick);
    bool fault = motor_check_fault(m);
    bool overcurrent = motor_check_overcurrent(m);

    if (fault || overcurrent) {
        m->last_bad_tick = ticks_now();
        if (elapsed < OVERCURRENT_TIMEOUT_MS) return;

        printf("TRACK %c OVERLOAD after %lums\n", m->track_id, (unsigned long)elapsed);
        motor_set_mode(m, POWER_MODE_OVERLOAD);
        return;
    }

    // No faults — check if alert window has passed
    if (elapsed > ALERT_WINDOW_MS) {
        printf("TRACK %c NORMAL\n", m->track_id);
        motor_set_mode(m, POWER_MODE_ON);
    }
}

static void handle_overload_state(motor_t *m) {
    if (ms_since(m->state_entered_tick) < m->overcurrent_backoff_ms) {
        return;
    }

    // Exponential backoff
    m->overcurrent_backoff_ms *= 2;
    if (m->overcurrent_backoff_ms > MAX_RETRY_WINDOW_MS) {
        m->overcurrent_backoff_ms = MAX_RETRY_WINDOW_MS;
    }

    printf("TRACK %c POWER RESTORE\n", m->track_id);
    motor_set_mode(m, POWER_MODE_ALERT);
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

// Track monitor task data: array of motor pointers
typedef struct {
    motor_t **motors;
    uint8_t   count;
} track_monitor_params_t;

void task_track_monitor(void *params) {
    track_monitor_params_t *p = (track_monitor_params_t *)params;

    for (;;) {
        for (uint8_t i = 0; i < p->count; i++) {
            motor_update(p->motors[i]);
        }
        vTaskDelay(1); // 1ms monitoring period
    }
}
