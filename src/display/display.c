#include "display/display.h"

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/gpio.h"

#include "FreeRTOS.h"
#include "task.h"

#include "board_config.h"
#include "util/dbg.h"

#define DISPLAY_W         128
#define DISPLAY_H         64
#define DISPLAY_PAGES     (DISPLAY_H / 8)        // 8 pages of 128 bytes
#define FRAMEBUFFER_SIZE  (DISPLAY_W * DISPLAY_PAGES)
#define I2C_CLOCK_HZ      400000
#define SPLASH_MS         2000
#define REFRESH_MS        250    // ~4 Hz — also the overcurrent flash period

// SSD1306 command bytes (control byte 0x00 precedes commands, 0x40 precedes data)
#define SSD1306_CMD       0x00
#define SSD1306_DATA      0x40

// --- Module state ---------------------------------------------------------

static i2c_inst_t   *s_i2c;
static uint8_t       s_i2c_addr;
static motor_t      *s_motor_main;
static motor_t      *s_motor_prog;
static dcc_engine_t *s_dcc;
static const char   *s_version;
static uint64_t      s_node_id;
static bool          s_ready;
static uint8_t       s_framebuffer[FRAMEBUFFER_SIZE];
static bool          s_inverted;  // last invert cmd sent, to avoid needless writes

// --- 6x8 ASCII font, covers 0x20..0x7E -----------------------------------
// Each glyph is 6 columns x 8 rows; each column is one byte (LSB = top).
// The 6th column is always blank to give ~1 pixel character spacing.
// Derived from the classic "font6x8" bitmap used in many SSD1306 drivers.
static const uint8_t FONT_6X8[95][6] = {
    {0x00,0x00,0x00,0x00,0x00,0x00}, // ' '
    {0x00,0x00,0x5F,0x00,0x00,0x00}, // !
    {0x00,0x07,0x00,0x07,0x00,0x00}, // "
    {0x14,0x7F,0x14,0x7F,0x14,0x00}, // #
    {0x24,0x2A,0x7F,0x2A,0x12,0x00}, // $
    {0x23,0x13,0x08,0x64,0x62,0x00}, // %
    {0x36,0x49,0x55,0x22,0x50,0x00}, // &
    {0x00,0x05,0x03,0x00,0x00,0x00}, // '
    {0x00,0x1C,0x22,0x41,0x00,0x00}, // (
    {0x00,0x41,0x22,0x1C,0x00,0x00}, // )
    {0x14,0x08,0x3E,0x08,0x14,0x00}, // *
    {0x08,0x08,0x3E,0x08,0x08,0x00}, // +
    {0x00,0x50,0x30,0x00,0x00,0x00}, // ,
    {0x08,0x08,0x08,0x08,0x08,0x00}, // -
    {0x00,0x60,0x60,0x00,0x00,0x00}, // .
    {0x20,0x10,0x08,0x04,0x02,0x00}, // /
    {0x3E,0x51,0x49,0x45,0x3E,0x00}, // 0
    {0x00,0x42,0x7F,0x40,0x00,0x00}, // 1
    {0x42,0x61,0x51,0x49,0x46,0x00}, // 2
    {0x21,0x41,0x45,0x4B,0x31,0x00}, // 3
    {0x18,0x14,0x12,0x7F,0x10,0x00}, // 4
    {0x27,0x45,0x45,0x45,0x39,0x00}, // 5
    {0x3C,0x4A,0x49,0x49,0x30,0x00}, // 6
    {0x01,0x71,0x09,0x05,0x03,0x00}, // 7
    {0x36,0x49,0x49,0x49,0x36,0x00}, // 8
    {0x06,0x49,0x49,0x29,0x1E,0x00}, // 9
    {0x00,0x36,0x36,0x00,0x00,0x00}, // :
    {0x00,0x56,0x36,0x00,0x00,0x00}, // ;
    {0x08,0x14,0x22,0x41,0x00,0x00}, // <
    {0x14,0x14,0x14,0x14,0x14,0x00}, // =
    {0x00,0x41,0x22,0x14,0x08,0x00}, // >
    {0x02,0x01,0x51,0x09,0x06,0x00}, // ?
    {0x32,0x49,0x79,0x41,0x3E,0x00}, // @
    {0x7E,0x11,0x11,0x11,0x7E,0x00}, // A
    {0x7F,0x49,0x49,0x49,0x36,0x00}, // B
    {0x3E,0x41,0x41,0x41,0x22,0x00}, // C
    {0x7F,0x41,0x41,0x22,0x1C,0x00}, // D
    {0x7F,0x49,0x49,0x49,0x41,0x00}, // E
    {0x7F,0x09,0x09,0x09,0x01,0x00}, // F
    {0x3E,0x41,0x49,0x49,0x7A,0x00}, // G
    {0x7F,0x08,0x08,0x08,0x7F,0x00}, // H
    {0x00,0x41,0x7F,0x41,0x00,0x00}, // I
    {0x20,0x40,0x41,0x3F,0x01,0x00}, // J
    {0x7F,0x08,0x14,0x22,0x41,0x00}, // K
    {0x7F,0x40,0x40,0x40,0x40,0x00}, // L
    {0x7F,0x02,0x0C,0x02,0x7F,0x00}, // M
    {0x7F,0x04,0x08,0x10,0x7F,0x00}, // N
    {0x3E,0x41,0x41,0x41,0x3E,0x00}, // O
    {0x7F,0x09,0x09,0x09,0x06,0x00}, // P
    {0x3E,0x41,0x51,0x21,0x5E,0x00}, // Q
    {0x7F,0x09,0x19,0x29,0x46,0x00}, // R
    {0x46,0x49,0x49,0x49,0x31,0x00}, // S
    {0x01,0x01,0x7F,0x01,0x01,0x00}, // T
    {0x3F,0x40,0x40,0x40,0x3F,0x00}, // U
    {0x1F,0x20,0x40,0x20,0x1F,0x00}, // V
    {0x3F,0x40,0x38,0x40,0x3F,0x00}, // W
    {0x63,0x14,0x08,0x14,0x63,0x00}, // X
    {0x07,0x08,0x70,0x08,0x07,0x00}, // Y
    {0x61,0x51,0x49,0x45,0x43,0x00}, // Z
    {0x00,0x7F,0x41,0x41,0x00,0x00}, // [
    {0x02,0x04,0x08,0x10,0x20,0x00}, // backslash
    {0x00,0x41,0x41,0x7F,0x00,0x00}, // ]
    {0x04,0x02,0x01,0x02,0x04,0x00}, // ^
    {0x40,0x40,0x40,0x40,0x40,0x00}, // _
    {0x00,0x01,0x02,0x04,0x00,0x00}, // `
    {0x20,0x54,0x54,0x54,0x78,0x00}, // a
    {0x7F,0x48,0x44,0x44,0x38,0x00}, // b
    {0x38,0x44,0x44,0x44,0x20,0x00}, // c
    {0x38,0x44,0x44,0x48,0x7F,0x00}, // d
    {0x38,0x54,0x54,0x54,0x18,0x00}, // e
    {0x08,0x7E,0x09,0x01,0x02,0x00}, // f
    {0x0C,0x52,0x52,0x52,0x3E,0x00}, // g
    {0x7F,0x08,0x04,0x04,0x78,0x00}, // h
    {0x00,0x44,0x7D,0x40,0x00,0x00}, // i
    {0x20,0x40,0x44,0x3D,0x00,0x00}, // j
    {0x7F,0x10,0x28,0x44,0x00,0x00}, // k
    {0x00,0x41,0x7F,0x40,0x00,0x00}, // l
    {0x7C,0x04,0x18,0x04,0x78,0x00}, // m
    {0x7C,0x08,0x04,0x04,0x78,0x00}, // n
    {0x38,0x44,0x44,0x44,0x38,0x00}, // o
    {0x7C,0x14,0x14,0x14,0x08,0x00}, // p
    {0x08,0x14,0x14,0x18,0x7C,0x00}, // q
    {0x7C,0x08,0x04,0x04,0x08,0x00}, // r
    {0x48,0x54,0x54,0x54,0x20,0x00}, // s
    {0x04,0x3F,0x44,0x40,0x20,0x00}, // t
    {0x3C,0x40,0x40,0x20,0x7C,0x00}, // u
    {0x1C,0x20,0x40,0x20,0x1C,0x00}, // v
    {0x3C,0x40,0x30,0x40,0x3C,0x00}, // w
    {0x44,0x28,0x10,0x28,0x44,0x00}, // x
    {0x0C,0x50,0x50,0x50,0x3C,0x00}, // y
    {0x44,0x64,0x54,0x4C,0x44,0x00}, // z
    {0x00,0x08,0x36,0x41,0x00,0x00}, // {
    {0x00,0x00,0x7F,0x00,0x00,0x00}, // |
    {0x00,0x41,0x36,0x08,0x00,0x00}, // }
    {0x02,0x01,0x02,0x04,0x02,0x00}, // ~
};

// --- I2C peripheral selection --------------------------------------------
// RP2350 pin mux: a GPIO pin with function-3 maps to I2C as follows:
//   SDA on pins where (pin % 4) == 0  -> I2C0
//   SDA on pins where (pin % 4) == 2  -> I2C1
// and SCL is always SDA + 1 on the same peripheral. Any other pairing is
// not a valid hardware mapping.
static i2c_inst_t *i2c_for_pins(uint8_t sda, uint8_t scl) {
    if (scl != sda + 1) return NULL;
    if ((sda % 4) == 0 && (scl % 4) == 1) return i2c0;
    if ((sda % 4) == 2 && (scl % 4) == 3) return i2c1;
    return NULL;
}

// --- Low-level I2C helpers -----------------------------------------------

static bool ssd1306_cmd(uint8_t cmd) {
    uint8_t buf[2] = { SSD1306_CMD, cmd };
    int n = i2c_write_timeout_us(s_i2c, s_i2c_addr, buf, 2, false, 10000);
    return n == 2;
}

static bool ssd1306_cmd_list(const uint8_t *cmds, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (!ssd1306_cmd(cmds[i])) return false;
    }
    return true;
}

static bool ssd1306_write_framebuffer(void) {
    // Set addressing to cover the full 128x64 GDDRAM.
    static const uint8_t setup[] = {
        0x21, 0, DISPLAY_W - 1,   // column address start/end
        0x22, 0, DISPLAY_PAGES - 1,  // page address start/end
    };
    if (!ssd1306_cmd_list(setup, sizeof(setup))) return false;

    // Data transfer: prefix with 0x40 control byte. The SDK doesn't have a
    // scatter-gather write, so build a small stack prefix and send the
    // framebuffer in page-sized chunks to keep stack use bounded.
    for (uint8_t page = 0; page < DISPLAY_PAGES; page++) {
        uint8_t chunk[1 + DISPLAY_W];
        chunk[0] = SSD1306_DATA;
        memcpy(&chunk[1], &s_framebuffer[page * DISPLAY_W], DISPLAY_W);
        int n = i2c_write_timeout_us(s_i2c, s_i2c_addr, chunk, sizeof(chunk),
                                     false, 50000);
        if (n != (int)sizeof(chunk)) return false;
    }
    return true;
}

static bool ssd1306_set_invert(bool invert) {
    return ssd1306_cmd(invert ? 0xA7 : 0xA6);
}

// Adafruit/classic 128x64 init sequence.
static bool ssd1306_init_controller(void) {
    static const uint8_t init_seq[] = {
        0xAE,             // display off
        0xD5, 0x80,       // clock div / osc freq
        0xA8, 0x3F,       // multiplex 1/64
        0xD3, 0x00,       // display offset 0
        0x40,             // start line 0
        0x8D, 0x14,       // charge pump on
        0x20, 0x00,       // horizontal addressing mode
        0xA1,             // segment remap (column 127 -> SEG0)
        0xC8,             // COM output scan dir: remapped
        0xDA, 0x12,       // COM pins hw config
        0x81, 0xCF,       // contrast
        0xD9, 0xF1,       // pre-charge period
        0xDB, 0x40,       // VCOM detect
        0xA4,             // resume to RAM content
        0xA6,             // normal (non-inverted)
        0xAF,             // display on
    };
    return ssd1306_cmd_list(init_seq, sizeof(init_seq));
}

// --- Framebuffer drawing --------------------------------------------------

static void fb_clear(void) {
    memset(s_framebuffer, 0, sizeof(s_framebuffer));
}

static void fb_draw_char(uint8_t x, uint8_t row, char c) {
    if (x + 6 > DISPLAY_W || row >= DISPLAY_PAGES) return;
    if (c < 0x20 || c > 0x7E) c = '?';
    const uint8_t *glyph = FONT_6X8[(uint8_t)c - 0x20];
    uint8_t *page = &s_framebuffer[row * DISPLAY_W + x];
    for (int i = 0; i < 6; i++) page[i] = glyph[i];
}

static void fb_draw_string(uint8_t x, uint8_t row, const char *s) {
    while (*s && x + 6 <= DISPLAY_W) {
        fb_draw_char(x, row, *s++);
        x += 6;
    }
}

// --- Status line rendering ------------------------------------------------

static void fmt_track_line(char *out, size_t n, const char *label,
                           motor_t *m) {
    if (motor_is_on(m)) {
        uint16_t ma = (uint16_t)((float)m->last_reading * SENSE_FACTOR_8874);
        snprintf(out, n, "%s: ON %4umA", label, ma);
    } else {
        snprintf(out, n, "%s: OFF", label);
    }
}

static void fmt_loco_line(char *out, size_t n) {
    uint16_t addr; bool is_long; uint8_t step;
    if (!s_dcc || !dcc_get_last_throttle(s_dcc, &addr, &is_long, &step)) {
        snprintf(out, n, "Loco: --");
        return;
    }
    char dir = (step & 0x80) ? 'F' : 'R';
    uint8_t speed = step & 0x7F;
    snprintf(out, n, "Loco %u%c %c %u",
             (unsigned)addr, is_long ? 'L' : ' ', dir, speed);
}

static void fmt_node_line(char *out, size_t n) {
    uint32_t low = (uint32_t)(s_node_id & 0xFFFFFFULL);
    snprintf(out, n, "CS %02X.%02X.%02X",
             (unsigned)((low >> 16) & 0xFF),
             (unsigned)((low >> 8)  & 0xFF),
             (unsigned)(low         & 0xFF));
}

static bool any_overcurrent(void) {
    return (s_motor_main && s_motor_main->state == POWER_MODE_OVERLOAD)
        || (s_motor_prog && s_motor_prog->state == POWER_MODE_OVERLOAD);
}

// --- Public API -----------------------------------------------------------

bool display_init(uint8_t sda_pin, uint8_t scl_pin, uint8_t i2c_addr,
                  bool enabled,
                  motor_t *motor_main, motor_t *motor_prog,
                  dcc_engine_t *dcc,
                  const char *version,
                  uint64_t node_id) {
    s_motor_main = motor_main;
    s_motor_prog = motor_prog;
    s_dcc        = dcc;
    s_version    = version ? version : "";
    s_node_id    = node_id;

    if (!enabled) {
        printf("[DISP] disabled by CDI\n");
        return false;
    }

    s_i2c = i2c_for_pins(sda_pin, scl_pin);
    if (!s_i2c) {
        printf("[DISP] invalid I2C pin combo (sda=%u scl=%u), display off\n",
               sda_pin, scl_pin);
        return false;
    }
    s_i2c_addr = i2c_addr;

    i2c_init(s_i2c, I2C_CLOCK_HZ);
    gpio_set_function(sda_pin, GPIO_FUNC_I2C);
    gpio_set_function(scl_pin, GPIO_FUNC_I2C);
    gpio_pull_up(sda_pin);
    gpio_pull_up(scl_pin);

    if (!ssd1306_init_controller()) {
        printf("[DISP] SSD1306 init failed (no ack at 0x%02X)\n", i2c_addr);
        return false;
    }

    fb_clear();
    ssd1306_write_framebuffer();
    s_ready = true;
    printf("[DISP] SSD1306 ready on %s (sda=%u scl=%u addr=0x%02X)\n",
           s_i2c == i2c0 ? "i2c0" : "i2c1", sda_pin, scl_pin, i2c_addr);
    return true;
}

void task_display(void *params) {
    (void)params;

    if (!s_ready) {
        // display_init reported the reason; just idle so the task object
        // stays valid if FreeRTOS ever inspects it.
        for (;;) vTaskDelay(portMAX_DELAY);
    }

    // Boot splash.
    fb_clear();
    fb_draw_string(0, 2, "simple-dcc");
    fb_draw_string(0, 4, s_version);
    ssd1306_write_framebuffer();
    ssd1306_set_invert(false);
    s_inverted = false;
    vTaskDelay(pdMS_TO_TICKS(SPLASH_MS));

    bool flash_phase = false;
    for (;;) {
        char line[24];

        fb_clear();
        fmt_track_line(line, sizeof(line), "MAIN", s_motor_main);
        fb_draw_string(0, 0, line);
        fmt_track_line(line, sizeof(line), "PROG", s_motor_prog);
        fb_draw_string(0, 2, line);
        fmt_loco_line(line, sizeof(line));
        fb_draw_string(0, 4, line);
        fmt_node_line(line, sizeof(line));
        fb_draw_string(0, 6, line);
        ssd1306_write_framebuffer();

        // Overcurrent: alternate inverted/normal on each refresh cycle.
        // When not overcurrent, pin inversion to false.
        bool want_invert;
        if (any_overcurrent()) {
            flash_phase = !flash_phase;
            want_invert = flash_phase;
        } else {
            flash_phase = false;
            want_invert = false;
        }
        if (want_invert != s_inverted) {
            ssd1306_set_invert(want_invert);
            s_inverted = want_invert;
        }

        vTaskDelay(pdMS_TO_TICKS(REFRESH_MS));
    }
}
