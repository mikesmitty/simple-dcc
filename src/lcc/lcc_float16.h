#ifndef LCC_FLOAT16_H
#define LCC_FLOAT16_H

#include <stdint.h>
#include <stdbool.h>

/*
 * OpenLCB binary16 (IEEE-754 half-precision) codec used by the Traction
 * protocol for velocity. Layout: 1-bit sign | 5-bit exp (bias 15) | 10-bit
 * fraction. Sign bit on velocity indicates direction (negative = reverse).
 * Reference: OpenLCB S-9.7.4.1.
 */

typedef uint16_t lcc_float16_t;

bool lcc_float16_is_nan(lcc_float16_t v);
bool lcc_float16_is_zero(lcc_float16_t v);
bool lcc_float16_is_negative(lcc_float16_t v);

/* Convert to native float. NaN → NaN, ±Inf → ±Inf, subnormals normalized. */
float lcc_float16_to_float(lcc_float16_t v);

/* Convert from native float. Overflow → ±Inf, underflow → ±0, NaN → NaN.
 * Uses round-toward-zero (truncation); good enough for throttle speeds. */
lcc_float16_t lcc_float16_from_float(float f);

#endif /* LCC_FLOAT16_H */
