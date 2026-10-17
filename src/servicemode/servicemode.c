#include "servicemode/servicemode.h"

#include "dcc/packet.h"

#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "wavegen_servicemode.pio.h"

/* ---- Timing (NMRA S-9.2.3 Direct mode) --------------------------------
 * "Reset packet" is whatever the PIO sends when its FIFO is empty. With
 * the 22-bit preamble at a 500 kHz SM clock one lasts ~8 ms, so these
 * waits double as reset-packet counts. */
#define POWER_ON_SETTLE_MS  200   /* power-on cycle: >= 20 packets first      */
#define BASELINE_MS         20    /* idle current, sampled before each burst  */
#define CMD_REPEATS         15    /* burst length (OpenMRN); S-9.2.3 min is 5 */
#define ACK_TAIL_MS         20    /* keep listening after the last packet     */
#define RECOVERY_MS         50    /* >= 6 resets after each instruction; they
                                   * also serve as the next one's >= 3 leading
                                   * resets */
#define WRITE_HOLD_MS       100   /* power kept on after a write so the
                                   * decoder can finish storing it (DCC-EX) */
#define IDLE_RECHECK_MS     1000

/* ---- ACK detection ----------------------------------------------------
 * An S-9.2.3 ACK is >= 60 mA above idle for 6 ms +/- 1 ms. The threshold
 * sits a little under 60 mA so a just-compliant decoder still clears it
 * over noise (DCC-EX's default is the same), and the minimum width rejects
 * the short spikes a motor or sound amp throws. Both want checking on
 * real hardware. */
#define ACK_THRESHOLD_MA    50
#define ACK_MIN_US          2500
#define SAMPLE_US           50

#define PIO_TX_FIFO_WORDS   8     /* TX FIFO is joined (8 deep) */

typedef struct {
    svc_op_t            op;
    uint16_t            cv;
    uint8_t             value;
    servicemode_done_fn done;
    void               *ctx;
} svc_request_t;

static motor_t      *g_motor;
static const motor_t *g_main;
static PIO           g_pio;
static uint          g_sm;
static QueueHandle_t g_queue;

bool servicemode_init(motor_t *prog_motor, const motor_t *main_motor)
{
    if (g_queue)                   return true;
    if (!prog_motor || !main_motor) return false;

    if (!pio_can_add_program(pio2, &wavegen_servicemode_program))
        return false;

    g_queue = xQueueCreate(1, sizeof(svc_request_t));
    if (!g_queue)
        return false;

    g_motor = prog_motor;
    g_main  = main_motor;
    g_pio   = pio2;
    uint offset = pio_add_program(pio2, &wavegen_servicemode_program);
    g_sm    = pio_claim_unused_sm(pio2, true);

    wavegen_servicemode_program_init(g_pio, g_sm, offset,
                                     (uint)prog_motor->signal_pin);
    pio_sm_set_enabled(g_pio, g_sm, true);
    return true;
}

svc_result_t servicemode_submit(svc_op_t op, uint16_t cv, uint8_t value,
                                servicemode_done_fn done, void *ctx)
{
    if (!g_queue || !done)
        return SVC_UNAVAILABLE;
    svc_request_t req = {
        .op = op, .cv = cv, .value = value, .done = done, .ctx = ctx,
    };
    return xQueueSend(g_queue, &req, 0) == pdTRUE ? SVC_OK : SVC_BUSY;
}

/* ---- ACK detector ----------------------------------------------------- */

typedef struct {
    uint16_t threshold;
    int32_t  filt;          /* low-passed reading, scaled x16 */
    bool     above;
    uint32_t above_since;   /* time_us_32() when filt crossed threshold */
} ack_detector_t;

static uint16_t sample_baseline(void)
{
    uint32_t sum = 0;
    uint32_t n   = 0;
    uint32_t start = time_us_32();
    while (time_us_32() - start < BASELINE_MS * 1000u) {
        sum += motor_read_current_raw(g_motor);
        n++;
        busy_wait_us_32(SAMPLE_US);
    }
    return n ? (uint16_t)(sum / n) : 0;
}

static void ack_init(ack_detector_t *d, uint16_t baseline)
{
    d->threshold = (uint16_t)(baseline + motor_ma_to_raw(ACK_THRESHOLD_MA));
    d->filt      = (int32_t)baseline << 4;
    d->above     = false;
}

/* Take one sample. True once the filtered current has stayed above the
 * threshold for ACK_MIN_US. The IIR (alpha 1/4, ~200 us at 50 us per
 * sample) smooths the dips at each DCC polarity change. */
static bool ack_sample(ack_detector_t *d)
{
    int32_t raw = (int32_t)motor_read_current_raw(g_motor) << 4;
    d->filt += (raw - d->filt) / 4;

    uint32_t now = time_us_32();
    if ((d->filt >> 4) <= d->threshold) {
        d->above = false;
        return false;
    }
    if (!d->above) {
        d->above       = true;
        d->above_since = now;
        return false;
    }
    return (now - d->above_since) >= ACK_MIN_US;
}

/* ---- one instruction -------------------------------------------------- */

/* The track monitor's FSM cuts power on overload; anything but ON/ALERT
 * while we hold the track powered means a short. */
static bool track_tripped(void)
{
    return !motor_is_on(g_motor);
}

/* Listen (and watch for a short) for one sample period. */
static svc_result_t listen(ack_detector_t *d, bool *ack)
{
    if (!*ack && ack_sample(d))
        *ack = true;
    if (track_tripped())
        return SVC_SHORT;
    busy_wait_us_32(SAMPLE_US);
    return SVC_OK;
}

static svc_result_t hw_send(void *ctx, dcc_packet_t *pkt, bool *ack)
{
    (void)ctx;
    if (track_tripped())
        return SVC_SHORT;

    ack_detector_t det;
    ack_init(&det, sample_baseline());
    encoded_packet_t enc = packet_encode(pkt);

    /* Burst: keep the FIFO topped up so the copies go out back to back —
     * an empty FIFO makes the PIO slip in a reset packet, and decoders only
     * act on identical consecutive packets. Stop feeding on ACK. */
    int sent = 0;
    while (sent < CMD_REPEATS && !*ack) {
        if (pio_sm_get_tx_fifo_level(g_pio, g_sm) + enc.count
                <= PIO_TX_FIFO_WORDS) {
            for (uint8_t i = 0; i < enc.count; i++)
                pio_sm_put(g_pio, g_sm, enc.words[i]);
            sent++;
        }
        svc_result_t r = listen(&det, ack);
        if (r != SVC_OK)
            return r;
    }

    /* Copies already queued still go out; the ACK can land on any of
     * them or just after the last one. */
    while (!pio_sm_is_tx_fifo_empty(g_pio, g_sm)) {
        svc_result_t r = listen(&det, ack);
        if (r != SVC_OK)
            return r;
    }
    uint32_t tail_start = time_us_32();
    while (time_us_32() - tail_start < ACK_TAIL_MS * 1000u) {
        svc_result_t r = listen(&det, ack);
        if (r != SVC_OK)
            return r;
    }

    /* Decoder recovery: reset packets only. */
    vTaskDelay(pdMS_TO_TICKS(RECOVERY_MS));
    return track_tripped() ? SVC_SHORT : SVC_OK;
}

/* ---- task ------------------------------------------------------------- */

/* The programming track's own limit (250 mA) is for the track switched on
 * outside a CV operation. During one it trips at the main track's limit
 * instead, as DCC-EX does during ACK operations. A short still trips, at
 * the main limit, but a decoder with a wiring fault can draw up to it for
 * the length of the operation. */
static void track_power(bool on)
{
    if (on) {
        motor_override_limit(g_motor, g_main->current_limit);
        motor_set_power(g_motor, true);
    } else {
        motor_set_power(g_motor, false);
        motor_override_limit(g_motor, 0);
    }
}

void task_servicemode(void *params)
{
    (void)params;
    static const svc_io_t io = { .send = hw_send, .ctx = NULL };
    svc_request_t req;

    for (;;) {
        if (xQueueReceive(g_queue, &req, pdMS_TO_TICKS(IDLE_RECHECK_MS))
                != pdTRUE) {
            /* Idle: keep the track off. Re-asserted every idle period, not
             * just once — the track monitor's overload retry on the other
             * core can race a power-off and switch the track back on. */
            track_power(false);
            continue;
        }

        /* The PIO has been sending reset packets all along, so the decoder
         * sees a clean power-on cycle. */
        track_power(true);
        vTaskDelay(pdMS_TO_TICKS(POWER_ON_SETTLE_MS));

        uint8_t value = req.value;
        svc_result_t res = (req.op == SVC_OP_READ)
                         ? svc_cv_read(&io, req.cv, &value)
                         : svc_cv_write(&io, req.cv, req.value);
        if (req.op == SVC_OP_WRITE && res != SVC_SHORT)
            vTaskDelay(pdMS_TO_TICKS(WRITE_HOLD_MS));
        track_power(false);
        req.done(res, value, req.ctx);
    }
}
