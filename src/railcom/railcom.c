#include "railcom/railcom.h"

#include "board_config.h"
#include "railcom/pom_read.h"
#include "railcom/railcom_decode.h"

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "hardware/irq.h"
#include "hardware/pio.h"
#include "hardware/uart.h"
#include "FreeRTOS.h"
#include "task.h"

#define RAILCOM_UART  uart0
#define RAILCOM_BAUD  250000

/* The cutout interrupt runs above configMAX_SYSCALL_INTERRUPT_PRIORITY
 * (0x50), so FreeRTOS critical sections never hold it off: the channel 1
 * drain has to land between ~177 us (channel 1 over) and ~231 us (first
 * channel 2 byte complete) after the packet end bit, and the PIO raises it
 * at ~205 us. It makes no FreeRTOS calls; a spare IRQ at the default
 * priority wakes the task instead. */
#define CUTOUT_IRQ_PRIORITY  0x40

/* Interrupt flags raised by cutout.pio. */
#define FLAG_START  0
#define FLAG_CH1    1
#define FLAG_END    2

/* While a read is under way the task wakes at least this often, to time
 * out attempts. */
#define POLL_MS  10

/* A channel 1 address counts once this many cutouts in a row agree. */
#define ADDRESS_REPEATS  3

#define RING_SIZE  16  /* power of 2 */

static wavegen_t    *g_wg;
static dcc_engine_t *g_dcc;
static uint          g_wake_irq;
static TaskHandle_t  g_task;

/* Cutout interrupt -> task. Single producer, single consumer. */
static railcom_cutout_t  g_cur;
static railcom_cutout_t  g_ring[RING_SIZE];
static volatile uint32_t g_head;  /* written by the interrupt */
static volatile uint32_t g_tail;  /* written by the task */

/* For the log. */
static volatile uint32_t g_cutouts;
static volatile uint32_t g_dropped;
static volatile uint32_t g_uart_errors;
static volatile uint32_t g_ch1_bytes;
static volatile uint32_t g_ch2_bytes;

/* The POM read handed over by railcom_pom_read(). g_busy is set until its
 * `done` has run. */
static struct {
    uint16_t            address;
    bool                is_long;
    uint16_t            cv;
    railcom_pom_done_fn done;
    void               *ctx;
} g_req;
static volatile bool g_req_pending;
static volatile bool g_busy;

/* ---- cutout interrupt ------------------------------------------------- */

/* Empty the UART's RX FIFO into `buf`, keeping at most `max` bytes. A byte
 * with a framing error or a break becomes 0x00, which is not a 4-of-8
 * code, so the bytes after it keep their places. */
static uint8_t __not_in_flash_func(drain)(uint8_t *buf, uint8_t max)
{
    uart_hw_t *hw = uart_get_hw(RAILCOM_UART);
    uint8_t n = 0;
    while (!(hw->fr & UART_UARTFR_RXFE_BITS)) {
        uint32_t dr = hw->dr;
        uint8_t b = (uint8_t)dr;
        if (dr & (UART_UARTDR_FE_BITS | UART_UARTDR_BE_BITS |
                  UART_UARTDR_OE_BITS)) {
            g_uart_errors++;
            b = 0x00;
        }
        if (n < max)
            buf[n++] = b;
    }
    return n;
}

static void __not_in_flash_func(push_cutout)(void)
{
    uint32_t head = g_head;
    if (head - g_tail >= RING_SIZE) {
        g_dropped++;
        return;
    }
    g_ring[head % RING_SIZE] = g_cur;
    __atomic_store_n(&g_head, head + 1, __ATOMIC_RELEASE);
    irq_set_pending(g_wake_irq);
}

static void __not_in_flash_func(cutout_isr)(void)
{
    PIO pio = WAVEGEN_CUTOUT_PIO;
    uint32_t flags = pio->irq;

    if (flags & (1u << FLAG_START)) {
        pio_interrupt_clear(pio, FLAG_START);
        g_cutouts++;
        /* The wavegen pushed this packet's first word at its start bit;
         * the next packet's comes only after the cutout. */
        uint32_t word = 0;
        if (g_wg->initialized && g_wg->mode == WAVEGEN_NORMAL) {
            while (!pio_sm_is_rx_fifo_empty(g_wg->wave_pio, g_wg->wave_sm))
                word = pio_sm_get(g_wg->wave_pio, g_wg->wave_sm);
        }
        g_cur.packet  = word;
        g_cur.ch1_len = 0;
        g_cur.ch2_len = 0;
        drain(NULL, 0);  /* nothing before the cutout is RailCom */
    }
    if (flags & (1u << FLAG_CH1)) {
        pio_interrupt_clear(pio, FLAG_CH1);
        g_cur.ch1_len = drain(g_cur.ch1, RAILCOM_CH1_MAX);
        g_ch1_bytes += g_cur.ch1_len;
    }
    if (flags & (1u << FLAG_END)) {
        pio_interrupt_clear(pio, FLAG_END);
        g_cur.ch2_len = drain(g_cur.ch2, RAILCOM_CH2_MAX);
        g_ch2_bytes += g_cur.ch2_len;
        if (g_cur.ch1_len || g_cur.ch2_len)
            push_cutout();
    }
}

static void wake_isr(void)
{
    BaseType_t woken = pdFALSE;
    if (g_task)
        vTaskNotifyGiveFromISR(g_task, &woken);
    portYIELD_FROM_ISR(woken);
}

bool railcom_init(wavegen_t *wg, dcc_engine_t *dcc)
{
    int wake = user_irq_claim_unused(false);
    if (wake < 0)
        return false;

    g_wg  = wg;
    g_dcc = dcc;

    uart_init(RAILCOM_UART, RAILCOM_BAUD);
    gpio_set_function(PIN_RAILCOM_RX, GPIO_FUNC_UART);
    gpio_pull_up(PIN_RAILCOM_RX);  /* idle high with no shield fitted */

    g_wake_irq = (uint)wake;
    irq_set_exclusive_handler(g_wake_irq, wake_isr);
    irq_set_enabled(g_wake_irq, true);

    /* The flags are set whether or not anyone listens, so this keeps
     * working across wavegen_reinit(). */
    PIO pio = WAVEGEN_CUTOUT_PIO;
    uint irq = pio_get_irq_num(pio, 0);
    pio_interrupt_clear(pio, FLAG_START);
    pio_interrupt_clear(pio, FLAG_CH1);
    pio_interrupt_clear(pio, FLAG_END);
    pio_set_irq0_source_mask_enabled(pio,
        (1u << pis_interrupt0) | (1u << pis_interrupt1) |
        (1u << pis_interrupt2), true);
    irq_set_exclusive_handler(irq, cutout_isr);
    irq_set_priority(irq, CUTOUT_IRQ_PRIORITY);
    irq_set_enabled(irq, true);
    return true;
}

/* ---- task --------------------------------------------------------------- */

static bool ring_pop(railcom_cutout_t *out)
{
    uint32_t tail = g_tail;
    if (tail == __atomic_load_n(&g_head, __ATOMIC_ACQUIRE))
        return false;
    *out = g_ring[tail % RING_SIZE];
    __atomic_store_n(&g_tail, tail + 1, __ATOMIC_RELEASE);
    return true;
}

railcom_result_t railcom_pom_read(uint16_t address, bool is_long, uint16_t cv,
                                  railcom_pom_done_fn done, void *ctx)
{
    if (!g_task || !done || !g_wg->initialized ||
        g_wg->mode != WAVEGEN_NORMAL)
        return RAILCOM_READ_OFF;
    if (__atomic_exchange_n(&g_busy, true, __ATOMIC_ACQ_REL))
        return RAILCOM_READ_BUSY;

    g_req.address = address;
    g_req.is_long = is_long;
    g_req.cv      = cv;
    g_req.done    = done;
    g_req.ctx     = ctx;
    __atomic_store_n(&g_req_pending, true, __ATOMIC_RELEASE);
    xTaskNotifyGive(g_task);
    return RAILCOM_READ_STARTED;
}

/* Channel 1 carries the address broadcast, alternating ID 1 (high byte)
 * and ID 2 (low byte). Log it when it settles on a new value: for now this
 * is how to see the detector working. */
static void watch_address(const railcom_cutout_t *c)
{
    static int16_t  high = -1, low = -1;
    static uint8_t  seen;
    static uint16_t logged = 0xFFFF;

    uint8_t id, payload;
    if (c->ch1_len != 2 || !railcom_decode12(c->ch1, &id, &payload))
        return;
    int16_t *part = id == RAILCOM_ID_ADR_HIGH ? &high
                  : id == RAILCOM_ID_ADR_LOW  ? &low : NULL;
    if (!part)
        return;
    if (*part != payload) {
        *part = payload;
        seen = 0;
        return;
    }
    if (seen < ADDRESS_REPEATS)
        seen++;
    if (high < 0 || low < 0 || seen < ADDRESS_REPEATS)
        return;

    uint16_t raw = (uint16_t)((high << 8) | low);
    if (raw == logged)
        return;
    logged = raw;
    /* High byte 0: short address. Its top bits set: long address in the
     * low 14 bits. */
    printf("RAILCOM ch1 address %u%s (raw %02X %02X)\n",
           (unsigned)(raw & 0x3FFF), (high & 0x80) ? " long" : "",
           (unsigned)high, (unsigned)low);
}

static uint32_t now_ms(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

void task_railcom(void *params)
{
    (void)params;
    g_task = xTaskGetCurrentTaskHandle();

    pom_read_t read;
    bool reading = false;

    for (;;) {
        ulTaskNotifyTake(pdTRUE, reading ? pdMS_TO_TICKS(POLL_MS)
                                         : portMAX_DELAY);

        if (__atomic_load_n(&g_req_pending, __ATOMIC_ACQUIRE)) {
            __atomic_store_n(&g_req_pending, false, __ATOMIC_RELEASE);
            dcc_packet_t pkt;
            packet_build_pom_read_byte(&pkt, g_req.address, g_req.is_long,
                                       g_req.cv, POM_READ_COPIES - 1);
            pom_read_start(&read, &pkt);
            reading = true;
        }

        railcom_cutout_t c;
        while (ring_pop(&c)) {
            watch_address(&c);
            if (reading)
                pom_read_cutout(&read, &c);
        }
        if (!reading)
            continue;

        switch (pom_read_poll(&read, now_ms())) {
        case POM_READ_SEND:
            dcc_pom_read_byte(g_dcc, g_req.address, g_req.is_long, g_req.cv,
                              POM_READ_COPIES - 1);
            break;
        case POM_READ_DONE:
            printf("RAILCOM POM read %u CV %u = %u (attempt %u)\n",
                   g_req.address, g_req.cv, read.value, read.attempts);
            reading = false;
            g_req.done(true, read.value, g_req.ctx);
            __atomic_store_n(&g_busy, false, __ATOMIC_RELEASE);
            break;
        case POM_READ_FAILED:
            printf("RAILCOM POM read %u CV %u: no answer (cutouts %lu, "
                   "dropped %lu, UART errors %lu, ch1 %lu, ch2 %lu)\n",
                   g_req.address, g_req.cv, (unsigned long)g_cutouts,
                   (unsigned long)g_dropped, (unsigned long)g_uart_errors,
                   (unsigned long)g_ch1_bytes, (unsigned long)g_ch2_bytes);
            reading = false;
            g_req.done(false, 0, g_req.ctx);
            __atomic_store_n(&g_busy, false, __ATOMIC_RELEASE);
            break;
        case POM_READ_WAIT:
            break;
        }
    }
}
