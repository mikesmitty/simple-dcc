#ifndef SVC_ACK_H
#define SVC_ACK_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Service Mode ACK detector (S-9.2.3), fed raw current-sense samples. No
 * hardware access, so it runs in the host tests.
 *
 * An ACK is >= 60 mA above idle for 6 ms +/- 1 ms. It is a rise above the
 * threshold that ends again, lasting SVC_ACK_MIN_US..SVC_ACK_MAX_US (DCC-EX's
 * defaults): neither a spike nor a load that switches on and stays on is
 * an ACK. The threshold is the baseline plus the larger of a floor and
 * 1.5x the baseline's own peak swing, so a decoder whose idle current is
 * noisy fails to read instead of reading wrong. A short moving average
 * keeps one stray sample from splitting a pulse or setting the swing.
 */

#define SVC_ACK_SAMPLE_US    50      /* caller's sample period */
#define SVC_ACK_AVG_SAMPLES  4       /* 200 us window */
#define SVC_ACK_WINDOW_US    (SVC_ACK_SAMPLE_US * SVC_ACK_AVG_SAMPLES)
#define SVC_ACK_MIN_US       2000
#define SVC_ACK_MAX_US       20000

typedef struct {
    uint16_t ring[SVC_ACK_AVG_SAMPLES];
    uint32_t ring_sum;
    uint16_t ring_pos;
    uint16_t ring_fill;

    uint32_t base_sum;
    uint32_t base_n;
    uint32_t base_peak_sum;  /* highest smoothed sum seen in the baseline */

    uint32_t threshold_sum;  /* scaled by SVC_ACK_AVG_SAMPLES, like ring_sum */
    bool     above;
    uint32_t above_since;    /* when the smoothed current crossed, in us */
    bool     acked;
} svc_ack_t;

/* Start over: call before the baseline samples for each instruction. */
void svc_ack_reset(svc_ack_t *d);

/* Feed one idle-current sample taken before the instruction is sent. */
void svc_ack_baseline(svc_ack_t *d, uint16_t raw);

/* Set the threshold from the baseline. `floor_raw` is the smallest rise
 * that counts, in raw units. Returns the baseline mean. */
uint16_t svc_ack_arm(svc_ack_t *d, uint16_t floor_raw);

/* Feed one sample taken at `now_us` while listening. Returns true once an
 * ACK has been seen, and keeps returning true. */
bool svc_ack_sample(svc_ack_t *d, uint16_t raw, uint32_t now_us);

/* True while a rise is in progress that could still end as an ACK, so the
 * caller keeps listening past its window. */
bool svc_ack_pending(const svc_ack_t *d, uint32_t now_us);

#endif /* SVC_ACK_H */
