#include "servicemode/svc_ack.h"

#include <string.h>

/* Push a sample through the moving average; returns the new window sum. */
static uint32_t smooth(svc_ack_t *d, uint16_t raw)
{
    d->ring_sum -= d->ring[d->ring_pos];
    d->ring[d->ring_pos] = raw;
    d->ring_sum += raw;
    d->ring_pos = (uint16_t)((d->ring_pos + 1) % SVC_ACK_AVG_SAMPLES);
    if (d->ring_fill < SVC_ACK_AVG_SAMPLES)
        d->ring_fill++;
    return d->ring_sum;
}

void svc_ack_reset(svc_ack_t *d)
{
    memset(d, 0, sizeof(*d));
}

void svc_ack_baseline(svc_ack_t *d, uint16_t raw)
{
    uint32_t sum = smooth(d, raw);
    d->base_sum += raw;
    d->base_n++;
    if (d->ring_fill == SVC_ACK_AVG_SAMPLES && sum > d->base_peak_sum)
        d->base_peak_sum = sum;
}

uint16_t svc_ack_arm(svc_ack_t *d, uint16_t floor_raw)
{
    uint32_t mean = d->base_n ? d->base_sum / d->base_n : 0;

    /* With too short a baseline to fill the window, fall back to its
     * mean: the threshold is then baseline + floor. */
    if (d->ring_fill < SVC_ACK_AVG_SAMPLES) {
        for (uint16_t i = d->ring_fill; i < SVC_ACK_AVG_SAMPLES; i++)
            smooth(d, (uint16_t)mean);
        d->base_peak_sum = d->ring_sum;
    }

    uint32_t mean_sum = mean * SVC_ACK_AVG_SAMPLES;
    uint32_t swing = d->base_peak_sum > mean_sum ? d->base_peak_sum - mean_sum : 0;
    uint32_t margin = swing + swing / 2;
    uint32_t floor_sum = (uint32_t)floor_raw * SVC_ACK_AVG_SAMPLES;
    d->threshold_sum = mean_sum + (margin > floor_sum ? margin : floor_sum);
    d->above = false;
    d->acked = false;
    return (uint16_t)mean;
}

bool svc_ack_sample(svc_ack_t *d, uint16_t raw, uint32_t now_us)
{
    uint32_t sum = smooth(d, raw);
    if (d->acked)
        return true;

    if (sum > d->threshold_sum) {
        if (!d->above) {
            d->above = true;
            d->above_since = now_us;
        }
        return false;
    }
    if (d->above) {
        uint32_t width = now_us - d->above_since;
        d->above = false;
        if (width >= SVC_ACK_MIN_US && width <= SVC_ACK_MAX_US)
            d->acked = true;
    }
    return d->acked;
}

bool svc_ack_pending(const svc_ack_t *d, uint32_t now_us)
{
    return !d->acked && d->above &&
           now_us - d->above_since <= SVC_ACK_MAX_US + SVC_ACK_WINDOW_US;
}
