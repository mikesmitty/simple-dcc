#include "lcc/lcc_float16.h"

#include <string.h>

bool lcc_float16_is_nan(lcc_float16_t v)
{
    return ((v >> 10) & 0x1F) == 0x1F && (v & 0x3FF) != 0;
}

bool lcc_float16_is_zero(lcc_float16_t v)
{
    return (v & 0x7FFF) == 0;
}

bool lcc_float16_is_negative(lcc_float16_t v)
{
    return (v & 0x8000) != 0;
}

float lcc_float16_to_float(lcc_float16_t v)
{
    uint32_t sign = (uint32_t)(v >> 15) & 0x1u;
    uint32_t exp  = (uint32_t)(v >> 10) & 0x1Fu;
    uint32_t frac = (uint32_t)v & 0x3FFu;
    uint32_t bits;

    if (exp == 0) {
        if (frac == 0) {
            bits = sign << 31;
        } else {
            /* Subnormal — shift the fraction left until the implicit bit
             * (0x400) is set, decrementing the unbiased fp32 exponent in
             * lockstep. fp16 subnormals start with effective exp = -14. */
            int32_t e = -14;
            while ((frac & 0x400u) == 0) {
                frac <<= 1;
                e--;
            }
            frac &= 0x3FFu;
            bits = (sign << 31) | ((uint32_t)(e + 127) << 23) | (frac << 13);
        }
    } else if (exp == 0x1F) {
        bits = (sign << 31) | (0xFFu << 23) | (frac << 13);
    } else {
        bits = (sign << 31) | ((exp + (127u - 15u)) << 23) | (frac << 13);
    }

    float result;
    memcpy(&result, &bits, sizeof(result));
    return result;
}

lcc_float16_t lcc_float16_from_float(float f)
{
    uint32_t bits;
    memcpy(&bits, &f, sizeof(bits));

    uint32_t sign = (bits >> 31) & 0x1u;
    int32_t  exp  = (int32_t)((bits >> 23) & 0xFFu) - 127;
    uint32_t frac = bits & 0x7FFFFFu;

    if (exp == 128) {
        /* Inf or NaN — preserve a non-zero fraction so NaN stays NaN. */
        return (lcc_float16_t)((sign << 15) | (0x1Fu << 10) | (frac ? 0x200u : 0u));
    }
    if (exp > 15) {
        return (lcc_float16_t)((sign << 15) | (0x1Fu << 10));
    }
    if (exp < -24) {
        return (lcc_float16_t)(sign << 15);
    }
    if (exp < -14) {
        /* Subnormal in fp16. Re-add the implicit leading 1 then shift. */
        uint32_t shift = (uint32_t)(-exp - 14);
        uint32_t f16_frac = (frac | 0x800000u) >> (shift + 13);
        return (lcc_float16_t)((sign << 15) | f16_frac);
    }
    return (lcc_float16_t)((sign << 15)
                         | ((uint32_t)(exp + 15) << 10)
                         | (frac >> 13));
}
