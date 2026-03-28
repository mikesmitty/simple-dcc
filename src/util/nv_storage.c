#include "util/nv_storage.h"
#include "hardware/flash.h"
#include "pico/flash.h"
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "util/dbg.h"

// Use the last sector of flash for configuration.
// PICO_FLASH_SIZE_BYTES is defined by the SDK based on the board.
#ifndef PICO_FLASH_SIZE_BYTES
#define PICO_FLASH_SIZE_BYTES (4 * 1024 * 1024)
#endif

#define FLASH_CONFIG_OFFSET (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)
#define FLASH_TARGET_CONTENTS ((const uint8_t *)(XIP_BASE + FLASH_CONFIG_OFFSET))

bool nv_storage_init(void *buffer, size_t size) {
    if (size > FLASH_SECTOR_SIZE)
        return false;

    // Check if flash is erased (all 0xFF)
    for (size_t i = 0; i < size; i++) {
        if (FLASH_TARGET_CONTENTS[i] != 0xFF) {
            memcpy(buffer, FLASH_TARGET_CONTENTS, size);
            DBG("[NV] loaded %u bytes from flash (first bytes: %02X %02X %02X %02X)\n",
                (unsigned)size, ((uint8_t*)buffer)[0], ((uint8_t*)buffer)[1],
                ((uint8_t*)buffer)[2], ((uint8_t*)buffer)[3]);
            return true;
        }
    }

    return false;
}

// Context passed into flash_safe_execute callback
typedef struct {
    const uint8_t *data;
    uint32_t prog_size;
} flash_write_ctx_t;

static void flash_write_callback(void *param) {
    flash_write_ctx_t *ctx = (flash_write_ctx_t *)param;
    flash_range_erase(FLASH_CONFIG_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(FLASH_CONFIG_OFFSET, ctx->data, ctx->prog_size);
}

bool nv_storage_write(const void *buffer, size_t size) {
    if (size > FLASH_SECTOR_SIZE)
        return false;

    // Pad to page-aligned buffer for programming.
    // Use heap allocation to avoid stack overflow (timer task stack is small).
    uint32_t prog_size = (size + FLASH_PAGE_SIZE - 1) & ~(FLASH_PAGE_SIZE - 1);
    uint8_t *page_buffer = pvPortMalloc(FLASH_SECTOR_SIZE);
    if (!page_buffer)
        return false;

    memset(page_buffer, 0xFF, FLASH_SECTOR_SIZE);
    memcpy(page_buffer, buffer, size);

    flash_write_ctx_t ctx = {
        .data = page_buffer,
        .prog_size = prog_size,
    };

    // flash_safe_execute coordinates both cores under FreeRTOS SMP:
    // disables interrupts, pauses the other core, then calls our callback.
    int rc = flash_safe_execute(flash_write_callback, &ctx, 500);
    
    vPortFree(page_buffer);
    return rc == PICO_OK;
}
