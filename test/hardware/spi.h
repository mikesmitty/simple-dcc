#ifndef HARDWARE_SPI_H
#define HARDWARE_SPI_H
/* Host test stub — real Pico SDK declares spi_inst_t as opaque and
 * exports spi_init/read/write/etc. Tests that exercise mcp2515.c
 * provide their own mock implementations. */
typedef struct spi_inst spi_inst_t;
/* spi0 / spi1 are Pico SDK macros pointing at the two on-chip SPI
 * peripherals. Host tests never dereference these — code paths that
 * touch the real hardware are gated by a config `enabled` flag. */
#define spi0 ((spi_inst_t *)0x01)
#define spi1 ((spi_inst_t *)0x02)
#endif
