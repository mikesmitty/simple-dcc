#include "servicemode/servicemode.h"

#include "dcc/packet.h"
#include "servicemode/svc_ack.h"

#include <stdio.h>

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
#define IDLE_SETTLE_MAX_MS  1000  /* longest wait for idle current to fall to
                                   * the programming limit                    */
#define IDLE_RECHECK_STEP_MS 50
#define BASELINE_MS         20    /* idle current, sampled before each burst  */
#define CMD_REPEATS         15    /* burst length (OpenMRN); S-9.2.3 min is 5 */
#define ACK_TAIL_MS         20    /* keep listening after the last packet     */
#define RECOVERY_MS         50    /* >= 6 resets after each instruction; they
                                   * also serve as the next one's >= 3 leading
                                   * resets */
#define WRITE_HOLD_MS       100   /* power kept on after a write so the
                                   * decoder can finish storing it (DCC-EX) */

/* ---- ACK detection (svc_ack.h has the rules) ---------------------------
 * The smallest rise that counts sits a little under S-9.2.3's 60 mA so a
 * just-compliant decoder still clears it (DCC-EX's default is the same).
 * On the bench, N-scale ESU decoders ACK ~550-720 mA for ~6 ms. */
#define ACK_THRESHOLD_MA    50
#define SAMPLE_US           SVC_ACK_SAMPLE_US

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

/* Idle current while reset packets go out, before each instruction.
 * Returns its mean, in raw units. */
static uint16_t sample_baseline(svc_ack_t *d)
{
    svc_ack_reset(d);
    uint32_t start = time_us_32();
    while (time_us_32() - start < BASELINE_MS * 1000u) {
        svc_ack_baseline(d, motor_read_current_raw(g_motor));
        busy_wait_us_32(SAMPLE_US);
    }
    return svc_ack_arm(d, motor_ma_to_raw(ACK_THRESHOLD_MA));
}

/* ---- idle current guard ------------------------------------------------
 * During an operation the track trips only at the main limit (see
 * track_power), which lets a decoder with a wiring fault draw that for the
 * whole read. A decoder idles at tens of mA, so one still over the
 * programming track's own limit with no ACK under way has a fault (on the
 * bench: a motor running on its own at ~570 mA). Treat it as a short. */
static bool idle_over_limit(uint16_t idle)
{
    if (idle <= g_motor->current_limit)
        return false;
    printf("TRACK %c IDLE %umA over the %umA programming limit\n",
           g_motor->track_id, motor_raw_to_ma(idle),
           motor_raw_to_ma(g_motor->current_limit));
    return true;
}

/* After power-on, give the idle current up to IDLE_SETTLE_MAX_MS to fall
 * to the programming limit, for a decoder still charging its capacitors. */
static bool idle_settles(void)
{
    TickType_t start = xTaskGetTickCount();
    for (;;) {
        svc_ack_t d;
        uint16_t idle = sample_baseline(&d);
        if (idle <= g_motor->current_limit)
            return true;
        if (!motor_is_on(g_motor) ||
            xTaskGetTickCount() - start >= pdMS_TO_TICKS(IDLE_SETTLE_MAX_MS))
            return !idle_over_limit(idle);
        vTaskDelay(pdMS_TO_TICKS(IDLE_RECHECK_STEP_MS));
    }
}

/* ---- one instruction -------------------------------------------------- */

/* The track monitor's FSM cuts power on overload; anything but ON/ALERT
 * while we hold the track powered means a short. */
static bool track_tripped(void)
{
    return !motor_is_on(g_motor);
}

/* Listen (and watch for a short) for one sample period. */
static svc_result_t listen(svc_ack_t *d, bool *ack)
{
    if (!*ack && svc_ack_sample(d, motor_read_current_raw(g_motor),
                                time_us_32()))
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

    svc_ack_t det;
    if (idle_over_limit(sample_baseline(&det)))
        return SVC_SHORT;
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
    /* An ACK only counts once it has ended, so one still under way at
     * the end of the tail gets to finish. */
    uint32_t tail_start = time_us_32();
    while (!*ack && (time_us_32() - tail_start < ACK_TAIL_MS * 1000u ||
                     svc_ack_pending(&det, time_us_32()))) {
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
        if (xQueueReceive(g_queue, &req, portMAX_DELAY) != pdTRUE)
            continue;

        /* The PIO has been sending reset packets all along, so the decoder
         * sees a clean power-on cycle. */
        track_power(true);
        vTaskDelay(pdMS_TO_TICKS(POWER_ON_SETTLE_MS));

        uint8_t value = req.value;
        svc_result_t res;
        if (!idle_settles())
            res = SVC_SHORT;
        else if (req.op == SVC_OP_READ)
            res = svc_cv_read(&io, req.cv, &value);
        else
            res = svc_cv_write(&io, req.cv, req.value);
        if (req.op == SVC_OP_WRITE && res != SVC_SHORT)
            vTaskDelay(pdMS_TO_TICKS(WRITE_HOLD_MS));
        track_power(false);
        req.done(res, value, req.ctx);
    }
}
