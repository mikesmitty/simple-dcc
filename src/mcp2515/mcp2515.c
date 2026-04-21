#include "mcp2515/mcp2515.h"

#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "pico/stdlib.h"

/* --------------------------------------------------------------------- */
/* SPI command set (Section 12 of the MCP2515 datasheet)                 */
/* --------------------------------------------------------------------- */

#define CMD_RESET        0xC0
#define CMD_READ         0x03
#define CMD_WRITE        0x02
#define CMD_BIT_MODIFY   0x05
#define CMD_READ_STATUS  0xA0
#define CMD_RX_STATUS    0xB0
#define CMD_RTS_TXB0     0x81
#define CMD_LOAD_TXB0    0x40   /* load-tx base, TXB0 starts at SIDH */
#define CMD_READ_RXB0    0x90   /* read-rx base, RXB0 starts at SIDH */
#define CMD_READ_RXB1    0x94

/* --------------------------------------------------------------------- */
/* Register addresses                                                    */
/* --------------------------------------------------------------------- */

#define REG_BFPCTRL      0x0C
#define REG_TXRTSCTRL    0x0D
#define REG_CANSTAT      0x0E
#define REG_CANCTRL      0x0F
#define REG_TEC          0x1C
#define REG_REC          0x1D
#define REG_CNF3         0x28
#define REG_CNF2         0x29
#define REG_CNF1         0x2A
#define REG_CANINTE      0x2B
#define REG_CANINTF      0x2C
#define REG_EFLG         0x2D
#define REG_TXB0CTRL     0x30
#define REG_RXB0CTRL     0x60
#define REG_RXB1CTRL     0x70
#define REG_RXM0SIDH     0x20
#define REG_RXM1SIDH     0x24
#define REG_RXF0SIDH     0x00
#define REG_RXF1SIDH     0x04
#define REG_RXF2SIDH     0x08
#define REG_RXF3SIDH     0x10
#define REG_RXF4SIDH     0x14
#define REG_RXF5SIDH     0x18

/* CANCTRL request-mode bits (top 3 bits of CANCTRL) */
#define MODE_NORMAL      0x00
#define MODE_CONFIG      0x80
#define MODE_LOOPBACK    0x40
#define MODE_LISTEN      0x60
#define MODE_MASK        0xE0

/* CANINTF flags */
#define INTF_RX0IF       0x01
#define INTF_RX1IF       0x02
#define INTF_TX0IF       0x04
#define INTF_ERRIF       0x20
#define INTF_MERRF       0x80

/* RXBnCTRL: accept any (RXM bits = 11 = off) — useful for initial
 * bring-up; filters stay all-zero but are disabled. */
#define RXBNCTRL_RXM_ANY 0x60

/* TXBnSIDL bit to mark frame as extended. */
#define TXBNSIDL_EXIDE   0x08

/* --------------------------------------------------------------------- */
/* Driver state                                                          */
/* --------------------------------------------------------------------- */

static const mcp2515_config_t *g_cfg;
static TaskHandle_t            g_rx_task;

/* --------------------------------------------------------------------- */
/* SPI primitives                                                        */
/* --------------------------------------------------------------------- */

static inline void cs_low(void)  { gpio_put(g_cfg->pin_cs, 0); }
static inline void cs_high(void) { gpio_put(g_cfg->pin_cs, 1); }

static void spi_reset(void)
{
    uint8_t cmd = CMD_RESET;
    cs_low();
    spi_write_blocking(g_cfg->spi, &cmd, 1);
    cs_high();
}

static uint8_t reg_read(uint8_t addr)
{
    uint8_t tx[2] = { CMD_READ, addr };
    uint8_t rx = 0;
    cs_low();
    spi_write_blocking(g_cfg->spi, tx, 2);
    spi_read_blocking(g_cfg->spi, 0, &rx, 1);
    cs_high();
    return rx;
}

static void reg_write(uint8_t addr, uint8_t val)
{
    uint8_t tx[3] = { CMD_WRITE, addr, val };
    cs_low();
    spi_write_blocking(g_cfg->spi, tx, 3);
    cs_high();
}

static void reg_write_many(uint8_t addr, const uint8_t *data, size_t n)
{
    uint8_t hdr[2] = { CMD_WRITE, addr };
    cs_low();
    spi_write_blocking(g_cfg->spi, hdr, 2);
    spi_write_blocking(g_cfg->spi, data, n);
    cs_high();
}

static void bit_modify(uint8_t addr, uint8_t mask, uint8_t data)
{
    uint8_t tx[4] = { CMD_BIT_MODIFY, addr, mask, data };
    cs_low();
    spi_write_blocking(g_cfg->spi, tx, 4);
    cs_high();
}

static uint8_t read_status(void)
{
    uint8_t cmd = CMD_READ_STATUS;
    uint8_t rx  = 0;
    cs_low();
    spi_write_blocking(g_cfg->spi, &cmd, 1);
    spi_read_blocking(g_cfg->spi, 0, &rx, 1);
    cs_high();
    return rx;
}

/* --------------------------------------------------------------------- */
/* Bit-timing tables (Tq = 1 / (2 * BRP / Fosc))                         */
/*                                                                       */
/* Values from the Microchip MCP2515 Application Notes AN754 / AN228.    */
/* Each row = { CNF1, CNF2, CNF3 } for the 4 supported bitrates.         */
/* --------------------------------------------------------------------- */

static const uint8_t cnf_table_8mhz[4][3] = {
    /* 125k */ { 0x01, 0xB1, 0x05 },
    /* 250k */ { 0x00, 0xB1, 0x05 },
    /* 500k */ { 0x00, 0x90, 0x02 },
    /* 1M   */ { 0x00, 0x80, 0x00 },
};

static const uint8_t cnf_table_16mhz[4][3] = {
    /* 125k */ { 0x03, 0xF0, 0x86 },
    /* 250k */ { 0x41, 0xF1, 0x85 },
    /* 500k */ { 0x00, 0xF0, 0x86 },
    /* 1M   */ { 0x00, 0xD0, 0x82 },
};

/* --------------------------------------------------------------------- */
/* Mode helpers                                                          */
/* --------------------------------------------------------------------- */

static bool enter_mode(uint8_t mode)
{
    bit_modify(REG_CANCTRL, MODE_MASK, mode);
    /* Give the chip a few polls to acknowledge the mode change via
     * CANSTAT. 10 ms is generous — datasheet says << 1 ms. */
    for (int i = 0; i < 10; i++) {
        if ((reg_read(REG_CANSTAT) & MODE_MASK) == mode)
            return true;
        sleep_ms(1);
    }
    return false;
}

/* --------------------------------------------------------------------- */
/* INT-pin ISR                                                           */
/* --------------------------------------------------------------------- */

static void gpio_irq_handler(uint gpio, uint32_t events)
{
    (void)events;
    if (g_cfg && gpio == g_cfg->pin_int && g_rx_task) {
        BaseType_t hpw = pdFALSE;
        vTaskNotifyGiveFromISR(g_rx_task, &hpw);
        portYIELD_FROM_ISR(hpw);
    }
}

/* --------------------------------------------------------------------- */
/* Public API                                                            */
/* --------------------------------------------------------------------- */

bool mcp2515_init(const mcp2515_config_t *cfg)
{
    if (!cfg || !cfg->spi) return false;
    g_cfg = cfg;

    /* SPI bus bring-up: 10 MHz, mode 0. Caller picks the SPI instance so
     * a separate device on the same bus is the caller's problem. */
    spi_init(cfg->spi, 10 * 1000 * 1000);
    spi_set_format(cfg->spi, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    gpio_set_function(cfg->pin_miso, GPIO_FUNC_SPI);
    gpio_set_function(cfg->pin_mosi, GPIO_FUNC_SPI);
    gpio_set_function(cfg->pin_sck,  GPIO_FUNC_SPI);

    gpio_init(cfg->pin_cs);
    gpio_set_dir(cfg->pin_cs, GPIO_OUT);
    gpio_put(cfg->pin_cs, 1);

    gpio_init(cfg->pin_int);
    gpio_set_dir(cfg->pin_int, GPIO_IN);
    gpio_pull_up(cfg->pin_int);

    /* Reset → CONFIG mode is automatic per datasheet (CANCTRL =
     * 0x87 on reset), but poll to be sure. */
    spi_reset();
    sleep_ms(5);
    if ((reg_read(REG_CANSTAT) & MODE_MASK) != MODE_CONFIG)
        return false;

    /* Probe — write/read a register to confirm SPI is alive. CNF1 is a
     * convenient scratch target before we program the real timing. */
    reg_write(REG_CNF1, 0xA5);
    if (reg_read(REG_CNF1) != 0xA5) return false;

    /* Program bit timing. */
    const uint8_t *cnf = (cfg->crystal == MCP2515_CRYSTAL_16MHZ)
                       ? cnf_table_16mhz[cfg->bitrate]
                       : cnf_table_8mhz[cfg->bitrate];
    reg_write(REG_CNF1, cnf[0]);
    reg_write(REG_CNF2, cnf[1]);
    reg_write(REG_CNF3, cnf[2]);

    /* Open acceptance: RXM = 11b on both buffers disables filtering.
     * With filtering disabled the mask/filter registers are ignored. */
    reg_write(REG_RXB0CTRL, RXBNCTRL_RXM_ANY);
    reg_write(REG_RXB1CTRL, RXBNCTRL_RXM_ANY);

    /* Disable BnF pin functions (we don't use them). */
    reg_write(REG_BFPCTRL,   0x00);
    reg_write(REG_TXRTSCTRL, 0x00);

    /* Enable RX0, RX1 and TX0 interrupts only — error handling is
     * polled in userspace via read_status(). */
    reg_write(REG_CANINTE, INTF_RX0IF | INTF_RX1IF | INTF_TX0IF);
    reg_write(REG_CANINTF, 0);

    /* Switch to NORMAL mode for live operation. */
    if (!enter_mode(MODE_NORMAL)) return false;

    /* Arm the GPIO interrupt on INT falling edge. pico_sdk's shared IRQ
     * table tolerates a single callback for the whole bank. */
    gpio_set_irq_enabled_with_callback(cfg->pin_int,
                                       GPIO_IRQ_EDGE_FALL,
                                       true,
                                       gpio_irq_handler);
    return true;
}

void mcp2515_set_rx_task(TaskHandle_t task)
{
    g_rx_task = task;
}

/* Pull the next pending RX buffer, if any. */
bool mcp2515_try_receive(mcp2515_frame_t *out)
{
    if (!g_cfg || !out) return false;

    uint8_t intf = reg_read(REG_CANINTF);
    uint8_t which;
    uint8_t base;
    if (intf & INTF_RX0IF)       { which = INTF_RX0IF; base = 0x61; } /* RXB0SIDH */
    else if (intf & INTF_RX1IF)  { which = INTF_RX1IF; base = 0x71; } /* RXB1SIDH */
    else                         { return false; }

    /* Streamed read starting at SIDH: SIDH, SIDL, EID8, EID0, DLC,
     * then data (up to 8). Using plain register reads one-by-one
     * keeps this portable; the batched READ_RX command also works
     * but requires more bookkeeping. */
    uint8_t sidh = reg_read(base + 0);
    uint8_t sidl = reg_read(base + 1);
    uint8_t eid8 = reg_read(base + 2);
    uint8_t eid0 = reg_read(base + 3);
    uint8_t dlc  = reg_read(base + 4) & 0x0F;

    bool extended = (sidl & 0x08) != 0;
    if (!extended) {
        /* Drop standard frames — LCC only uses extended. Ack the flag
         * so the buffer frees up. */
        bit_modify(REG_CANINTF, which, 0);
        return false;
    }

    /* 29-bit ID: SID[10:3] = SIDH, SID[2:0] = SIDL[7:5], EID[17:16]
     * = SIDL[1:0], EID[15:8] = EID8, EID[7:0] = EID0. */
    uint32_t id = ((uint32_t)sidh << 21)
                | (((uint32_t)sidl & 0xE0) << 13)
                | (((uint32_t)sidl & 0x03) << 16)
                | ((uint32_t)eid8  << 8)
                |  (uint32_t)eid0;

    out->id  = id;
    out->dlc = (dlc > 8) ? 8 : dlc;
    for (uint8_t i = 0; i < out->dlc; i++) {
        out->data[i] = reg_read(base + 5 + i);
    }

    /* Clear the INTF flag for this buffer so the chip can reuse it. */
    bit_modify(REG_CANINTF, which, 0);
    return true;
}

bool mcp2515_send(const mcp2515_frame_t *frame)
{
    if (!g_cfg || !frame) return false;

    /* Poll TXB0CTRL.TXREQ (bit 3): if set, the buffer is still busy. */
    if (reg_read(REG_TXB0CTRL) & 0x08)
        return false;

    /* Pack the 29-bit extended ID into SIDH/SIDL/EID8/EID0 per the
     * TX buffer layout (datasheet §3.1, figure 3-1). */
    uint32_t id   = frame->id & 0x1FFFFFFFu;
    uint8_t  sidh = (uint8_t)(id >> 21);
    uint8_t  sidl = (uint8_t)(((id >> 13) & 0xE0) | TXBNSIDL_EXIDE
                              | ((id >> 16) & 0x03));
    uint8_t  eid8 = (uint8_t)(id >> 8);
    uint8_t  eid0 = (uint8_t)id;
    uint8_t  dlc  = (frame->dlc > 8) ? 8 : frame->dlc;

    uint8_t buf[5 + 8];
    buf[0] = sidh;
    buf[1] = sidl;
    buf[2] = eid8;
    buf[3] = eid0;
    buf[4] = dlc;
    for (uint8_t i = 0; i < dlc; i++) buf[5 + i] = frame->data[i];

    /* Write SIDH..DLC then the data bytes via a single burst (starts at
     * TXB0SIDH = 0x31). */
    reg_write_many(0x31, buf, 5 + dlc);

    /* Request to send TXB0. */
    uint8_t cmd = CMD_RTS_TXB0;
    cs_low();
    spi_write_blocking(g_cfg->spi, &cmd, 1);
    cs_high();
    return true;
}
