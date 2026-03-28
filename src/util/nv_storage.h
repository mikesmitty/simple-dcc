#ifndef NV_STORAGE_H
#define NV_STORAGE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/**
 * @brief Initialize non-volatile storage.
 * Loads data from flash into the provided RAM buffer.
 * If flash is empty (all 0xFF), the buffer is left unchanged.
 * 
 * @param buffer Pointer to the RAM buffer to load data into.
 * @param size Size of the buffer in bytes.
 * @return true if data was successfully loaded from flash.
 */
bool nv_storage_init(void *buffer, size_t size);

/**
 * @brief Write data to non-volatile storage.
 * Erases the flash sector and programs the data from the buffer.
 * 
 * @param buffer Pointer to the RAM buffer containing data to save.
 * @param size Size of the buffer in bytes.
 * @return true if data was successfully written to flash.
 */
bool nv_storage_write(const void *buffer, size_t size);

#endif // NV_STORAGE_H
