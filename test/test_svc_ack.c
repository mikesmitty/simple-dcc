#include <assert.h>
#include <stdio.h>
#include <stdbool.h>

#include "servicemode/svc_ack.h"

/* Synthetic current-sense traces in raw ADC units (1.27 mA each), one
 * sample every 52 us like the firmware's loop. Shapes come from the bench:
 * a quiet decoder idles at ~22 mA and ACKs ~600 mA for ~6 ms; a misbehaving
 * one pulls a 17/780 mA square wave every ~1 ms. */

#define PERIOD_US   52
#define FLOOR_RAW   39      /* 50 mA */
#define MA(x)       ((int)((x) / 1.27 + 0.5))

typedef struct {
    int  base;              /* idle level */
    int  noise_hi;          /* square-wave high level; 0 = quiet */
    int  noise_period;      /* samples per noise cycle */
    int  pulse;             /* rise added during the pulse */
    int  pulse_start;       /* sample index, counted from the burst start */
    int  pulse_len;         /* samples; -1 = never ends */
} trace_t;

static int level(const trace_t *t, int i)
{
    int v = t->base;
    if (t->noise_hi) {
        int phase = ((i % t->noise_period) + t->noise_period) % t->noise_period;
        v = phase < t->noise_period / 2 ? t->noise_hi : t->base;
    } else {
        v += (i * 7) % 5 - 2;   /* +/- 2 raw of jitter */
    }
    if (t->pulse && i >= t->pulse_start &&
        (t->pulse_len < 0 || i < t->pulse_start + t->pulse_len))
        v += t->pulse;
    return v < 0 ? 0 : v;
}

/* Baseline of 20 ms, then `listen` samples; returns whether it ACKed and,
 * through *at_us, when (time from the burst start). */
static bool run(const trace_t *t, int listen, uint32_t *at_us)
{
    svc_ack_t d;
    svc_ack_reset(&d);
    int bl = 20000 / PERIOD_US;
    for (int i = -bl; i < 0; i++)
        svc_ack_baseline(&d, (uint16_t)level(t, i));
    svc_ack_arm(&d, FLOOR_RAW);
    for (int i = 0; i < listen; i++) {
        if (svc_ack_sample(&d, (uint16_t)level(t, i), (uint32_t)i * PERIOD_US)) {
            if (at_us) *at_us = (uint32_t)i * PERIOD_US;
            return true;
        }
    }
    return false;
}

#define SAMPLES(us) ((us) / PERIOD_US)

static void test_quiet_decoder_ack(void)
{
    trace_t t = { .base = MA(22), .pulse = MA(600),
                  .pulse_start = SAMPLES(20000), .pulse_len = SAMPLES(6000) };
    uint32_t at = 0;
    assert(run(&t, SAMPLES(200000), &at));
    /* Counted once it has ended, not on the rise. */
    assert(at > 20000 + 6000 && at < 20000 + 6000 + 3000);
    printf("  PASS: 600 mA, 6 ms ACK on a quiet decoder\n");
}

static void test_weakest_compliant_ack(void)
{
    trace_t t = { .base = MA(22), .pulse = MA(65),
                  .pulse_start = SAMPLES(20000), .pulse_len = SAMPLES(5000) };
    assert(run(&t, SAMPLES(200000), NULL));
    printf("  PASS: 65 mA, 5 ms ACK (just over S-9.2.3's minimum)\n");
}

static void test_rise_below_floor(void)
{
    trace_t t = { .base = MA(22), .pulse = MA(35),
                  .pulse_start = SAMPLES(20000), .pulse_len = SAMPLES(6000) };
    assert(!run(&t, SAMPLES(200000), NULL));
    printf("  PASS: 35 mA rise is not an ACK\n");
}

static void test_spike_too_short(void)
{
    trace_t t = { .base = MA(22), .pulse = MA(2000),
                  .pulse_start = SAMPLES(20000), .pulse_len = SAMPLES(1000) };
    assert(!run(&t, SAMPLES(200000), NULL));
    printf("  PASS: 1 ms spike is not an ACK\n");
}

static void test_pulse_too_long(void)
{
    trace_t t = { .base = MA(22), .pulse = MA(3000),
                  .pulse_start = SAMPLES(20000), .pulse_len = SAMPLES(40000) };
    assert(!run(&t, SAMPLES(200000), NULL));
    printf("  PASS: 40 ms, 3 A pulse (a motor, not an ACK) is rejected\n");
}

static void test_load_that_stays_on(void)
{
    trace_t t = { .base = MA(22), .pulse = MA(600),
                  .pulse_start = SAMPLES(20000), .pulse_len = -1 };
    assert(!run(&t, SAMPLES(200000), NULL));

    /* And the caller stops waiting for it to end after SVC_ACK_MAX_US. */
    svc_ack_t d;
    svc_ack_reset(&d);
    for (int i = 0; i < 400; i++)
        svc_ack_baseline(&d, MA(22));
    svc_ack_arm(&d, FLOOR_RAW);
    uint32_t now = 0;
    for (; now < 2000; now += PERIOD_US)
        svc_ack_sample(&d, MA(22), now);
    uint32_t rise = now;
    for (; now < rise + 10000; now += PERIOD_US)
        svc_ack_sample(&d, MA(622), now);
    assert(svc_ack_pending(&d, now));
    for (; now < rise + SVC_ACK_MAX_US + SVC_ACK_WINDOW_US + 1000;
         now += PERIOD_US)
        svc_ack_sample(&d, MA(622), now);
    assert(!svc_ack_pending(&d, now));
    printf("  PASS: a load that switches on and stays on is not an ACK\n");
}

static void test_noisy_decoder_no_false_ack(void)
{
    for (int period = 15; period <= 70; period += 5) {
        trace_t t = { .base = MA(17), .noise_hi = MA(780),
                      .noise_period = period };
        assert(!run(&t, SAMPLES(200000), NULL));
    }
    printf("  PASS: 17/780 mA square-wave idle current gives no ACK\n");
}

static void test_no_baseline(void)
{
    /* Arming with no baseline at all still needs a full-size ACK. */
    svc_ack_t d;
    svc_ack_reset(&d);
    assert(svc_ack_arm(&d, FLOOR_RAW) == 0);
    bool ack = false;
    for (uint32_t now = 0; now < 20000 && !ack; now += PERIOD_US) {
        uint16_t v = (now >= 5000 && now < 11000) ? MA(600) : 0;
        ack = svc_ack_sample(&d, v, now);
    }
    assert(ack);
    printf("  PASS: arming without a baseline\n");
}

int main(void)
{
    printf("test_svc_ack:\n");
    test_quiet_decoder_ack();
    test_weakest_compliant_ack();
    test_rise_below_floor();
    test_spike_too_short();
    test_pulse_too_long();
    test_load_that_stays_on();
    test_noisy_decoder_no_false_ack();
    test_no_baseline();
    printf("All svc_ack tests passed.\n");
    return 0;
}
