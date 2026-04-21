/* Host test for src/mcp2515/mcp2515.c — verifies the 29-bit extended
 * ID pack/unpack around the MCP2515 TX/RX buffer layout and that the
 * SPI command framing (CMD_WRITE, TXB0 base address, RTS command)
 * matches the datasheet.
 *
 * We don't exercise mcp2515_init() here — that path depends on real
 * hardware responding in MODE_CONFIG, and the framing logic we care
 * about lives in mcp2515_send() and mcp2515_try_receive().
 */

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* --- FreeRTOS host mock ------------------------------------------------- */

#include "FreeRTOS.h"

/* task.h is an empty shim; declare what mcp2515.c uses. */
typedef int BaseType_t;
static inline void vTaskNotifyGiveFromISR(TaskHandle_t t, BaseType_t *hpw) {
    (void)t; (void)hpw;
}
#define portYIELD_FROM_ISR(x) ((void)0)

/* --- pico/hardware mocks ------------------------------------------------ */
/* mcp2515.c uses spi_init, spi_set_format, spi_write_blocking,
 * spi_read_blocking, gpio_*, sleep_ms. We replace those with recording
 * mocks so we can inspect SPI traffic and feed canned read responses. */

#define SPI_CPOL_0   0
#define SPI_CPHA_0   0
#define SPI_MSB_FIRST 0

#define GPIO_FUNC_SPI     1
#define GPIO_OUT          1
#define GPIO_IN           0
#define GPIO_IRQ_EDGE_FALL 0x4

typedef struct spi_inst spi_inst_t;
typedef unsigned int uint;

/* Recording log — every byte written via spi_write_blocking is appended
 * here so tests can assert on the exact wire sequence. */
#define SPI_LOG_SIZE 256
static uint8_t g_spi_write_log[SPI_LOG_SIZE];
static size_t  g_spi_write_log_len;

/* Canned-read queue — spi_read_blocking dequeues from here. */
static uint8_t g_spi_read_queue[SPI_LOG_SIZE];
static size_t  g_spi_read_queue_head;
static size_t  g_spi_read_queue_len;

/* GPIO state. */
static int g_gpio_cs_state;   /* tracks pin_cs last value */
static int g_cs_transitions;  /* number of low→high (end-of-xfer) events */

static inline void spi_init(spi_inst_t *s, uint hz) { (void)s; (void)hz; }
static inline void spi_set_format(spi_inst_t *s, uint bits, uint cpol,
                                  uint cpha, uint order) {
    (void)s; (void)bits; (void)cpol; (void)cpha; (void)order;
}
static inline int spi_write_blocking(spi_inst_t *s, const uint8_t *buf, size_t n) {
    (void)s;
    assert(g_spi_write_log_len + n <= SPI_LOG_SIZE);
    memcpy(&g_spi_write_log[g_spi_write_log_len], buf, n);
    g_spi_write_log_len += n;
    return (int)n;
}
static inline int spi_read_blocking(spi_inst_t *s, uint8_t tx, uint8_t *buf, size_t n) {
    (void)s; (void)tx;
    for (size_t i = 0; i < n; i++) {
        if (g_spi_read_queue_head < g_spi_read_queue_len) {
            buf[i] = g_spi_read_queue[g_spi_read_queue_head++];
        } else {
            buf[i] = 0;
        }
    }
    return (int)n;
}
static inline void gpio_init(uint pin)           { (void)pin; }
static inline void gpio_set_dir(uint pin, int d) { (void)pin; (void)d; }
static inline void gpio_set_function(uint pin, int f) { (void)pin; (void)f; }
static inline void gpio_pull_up(uint pin)        { (void)pin; }
static inline void gpio_put(uint pin, int v) {
    (void)pin;
    if (g_gpio_cs_state == 0 && v == 1) g_cs_transitions++;
    g_gpio_cs_state = v;
}
static inline void gpio_set_irq_enabled_with_callback(uint pin, uint32_t e, bool on, void *cb) {
    (void)pin; (void)e; (void)on; (void)cb;
}
static inline void sleep_ms(uint32_t ms) { (void)ms; }

/* The config stores spi_inst_t*; we need a non-NULL placeholder. */
static spi_inst_t *const MOCK_SPI = (spi_inst_t *)0x1;

/* --- mcp2515.c under test ---------------------------------------------- */

/* Shim the hardware headers out — we defined the needed APIs above. */
#define HARDWARE_GPIO_H
#define HARDWARE_SPI_H
#define _HARDWARE_GPIO_H
#define _HARDWARE_SPI_H
#define _PICO_STDLIB_H

#include "../src/mcp2515/mcp2515.c"

/* --- helpers ------------------------------------------------------------ */

static void reset_logs(void) {
    memset(g_spi_write_log,  0, sizeof(g_spi_write_log));
    memset(g_spi_read_queue, 0, sizeof(g_spi_read_queue));
    g_spi_write_log_len   = 0;
    g_spi_read_queue_head = 0;
    g_spi_read_queue_len  = 0;
    g_gpio_cs_state       = 1;
    g_cs_transitions      = 0;
}

static void queue_read(uint8_t b) {
    assert(g_spi_read_queue_len < SPI_LOG_SIZE);
    g_spi_read_queue[g_spi_read_queue_len++] = b;
}

static void setup_driver_cfg(void) {
    static mcp2515_config_t cfg;
    cfg.spi      = MOCK_SPI;
    cfg.pin_miso = 12;
    cfg.pin_mosi = 15;
    cfg.pin_sck  = 14;
    cfg.pin_cs   = 13;
    cfg.pin_int  = 24;
    cfg.crystal  = MCP2515_CRYSTAL_16MHZ;
    cfg.bitrate  = MCP2515_BITRATE_125K;
    g_cfg = &cfg;   /* bypass mcp2515_init's hardware probe */
}

/* Find `needle` (length `nlen`) in the spi write log. Returns the starting
 * offset or -1 if not present. */
static int find_in_log(const uint8_t *needle, size_t nlen) {
    if (nlen > g_spi_write_log_len) return -1;
    for (size_t i = 0; i + nlen <= g_spi_write_log_len; i++) {
        if (memcmp(&g_spi_write_log[i], needle, nlen) == 0)
            return (int)i;
    }
    return -1;
}

/* --- tests -------------------------------------------------------------- */

static void test_send_packs_extended_id(void) {
    reset_logs();
    setup_driver_cfg();

    /* mcp2515_send first polls REG_TXB0CTRL to check TXREQ; we need
     * that read to return 0 (buffer free). reg_read sends {CMD_READ,
     * addr} then reads 1 byte. */
    queue_read(0x00);   /* TXB0CTRL: TXREQ clear */

    mcp2515_frame_t f = {
        .id  = 0x12345678,
        .dlc = 3,
        .data = { 0xAA, 0xBB, 0xCC },
    };
    bool ok = mcp2515_send(&f);
    assert(ok == true);

    /* Expected write sequence after the TXB0CTRL read:
     *   CMD_READ=0x03, REG_TXB0CTRL=0x30      — the pre-check read
     *   CMD_WRITE=0x02, TXB0SIDH=0x31         — header
     *   SIDH=0x91, SIDL=0xA8, EID8=0x56,
     *     EID0=0x78, DLC=0x03, 0xAA, 0xBB, 0xCC
     *   CMD_RTS_TXB0=0x81                     — request-to-send
     */
    const uint8_t expect_read_hdr[]  = { 0x03, 0x30 };
    const uint8_t expect_write_hdr[] = { 0x02, 0x31 };
    const uint8_t expect_frame[]     = { 0x91, 0xA8, 0x56, 0x78,
                                         0x03, 0xAA, 0xBB, 0xCC };
    const uint8_t expect_rts[]       = { 0x81 };

    assert(find_in_log(expect_read_hdr,  sizeof(expect_read_hdr))  >= 0);
    int hdr_off = find_in_log(expect_write_hdr, sizeof(expect_write_hdr));
    assert(hdr_off >= 0);
    /* Payload must immediately follow the write header. */
    assert(memcmp(&g_spi_write_log[hdr_off + 2], expect_frame,
                  sizeof(expect_frame)) == 0);
    assert(find_in_log(expect_rts, sizeof(expect_rts)) >= 0);

    printf("  ok: send packs 29-bit ID → SIDH=0x91 SIDL=0xA8 EID8=0x56 EID0=0x78\n");
}

static void test_send_honors_exide_bit(void) {
    reset_logs();
    setup_driver_cfg();
    queue_read(0x00);

    /* Any ID — the EXIDE bit (0x08) must always be set in SIDL so the
     * MCP2515 marks it as an extended frame. Without EXIDE the chip
     * would transmit an 11-bit standard frame, which LCC never uses. */
    mcp2515_frame_t f = { .id = 0x00000001, .dlc = 0 };
    assert(mcp2515_send(&f));

    int hdr = find_in_log((const uint8_t[]){ 0x02, 0x31 }, 2);
    assert(hdr >= 0);
    uint8_t sidl = g_spi_write_log[hdr + 2 + 1];  /* SIDL is 2nd byte */
    assert((sidl & 0x08) != 0);  /* EXIDE bit set */
    printf("  ok: send always sets EXIDE bit (SIDL=0x%02X)\n", sidl);
}

static void test_send_returns_false_if_txreq_busy(void) {
    reset_logs();
    setup_driver_cfg();
    queue_read(0x08);   /* TXB0CTRL: TXREQ set — buffer still busy */

    mcp2515_frame_t f = { .id = 0x100, .dlc = 0 };
    bool ok = mcp2515_send(&f);
    assert(ok == false);

    /* The driver should NOT have written a frame header. */
    const uint8_t no_write[] = { 0x02, 0x31 };
    assert(find_in_log(no_write, sizeof(no_write)) == -1);
    printf("  ok: send returns false when TXB0 still busy\n");
}

static void test_receive_unpacks_extended_id(void) {
    reset_logs();
    setup_driver_cfg();

    /* try_receive reads CANINTF, then SIDH/SIDL/EID8/EID0/DLC/data,
     * then bit-modifies CANINTF to ack. Queue the canned byte stream
     * in read order. */
    queue_read(0x01);  /* CANINTF: RX0IF */
    queue_read(0x91);  /* SIDH */
    queue_read(0xA8);  /* SIDL (EXIDE set) */
    queue_read(0x56);  /* EID8 */
    queue_read(0x78);  /* EID0 */
    queue_read(0x03);  /* DLC */
    queue_read(0xAA);  /* data[0] */
    queue_read(0xBB);  /* data[1] */
    queue_read(0xCC);  /* data[2] */

    mcp2515_frame_t out;
    memset(&out, 0, sizeof(out));
    bool got = mcp2515_try_receive(&out);
    assert(got == true);
    assert(out.id  == 0x12345678);
    assert(out.dlc == 3);
    assert(out.data[0] == 0xAA);
    assert(out.data[1] == 0xBB);
    assert(out.data[2] == 0xCC);
    printf("  ok: try_receive unpacks SIDH/SIDL/EID8/EID0 → 0x%08X\n", out.id);
}

static void test_receive_drops_standard_frames(void) {
    reset_logs();
    setup_driver_cfg();

    /* Same as above but SIDL without EXIDE (standard 11-bit frame).
     * Driver must return false (dropped). */
    queue_read(0x01);  /* CANINTF: RX0IF */
    queue_read(0x91);  /* SIDH */
    queue_read(0xA0);  /* SIDL — EXIDE clear */
    queue_read(0x00);
    queue_read(0x00);
    queue_read(0x00);

    mcp2515_frame_t out;
    bool got = mcp2515_try_receive(&out);
    assert(got == false);
    printf("  ok: try_receive drops standard (non-extended) frames\n");
}

static void test_receive_nothing_pending(void) {
    reset_logs();
    setup_driver_cfg();

    queue_read(0x00);  /* CANINTF: neither RX flag set */

    mcp2515_frame_t out;
    bool got = mcp2515_try_receive(&out);
    assert(got == false);
    printf("  ok: try_receive returns false when INTF has no RX flags\n");
}

int main(void) {
    printf("test_mcp2515_framing\n");
    test_send_packs_extended_id();
    test_send_honors_exide_bit();
    test_send_returns_false_if_txreq_busy();
    test_receive_unpacks_extended_id();
    test_receive_drops_standard_frames();
    test_receive_nothing_pending();
    printf("all ok\n");
    return 0;
}
