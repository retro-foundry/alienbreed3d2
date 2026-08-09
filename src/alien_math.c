#include "alien_math.h"

#include <limits.h>
#include <stdio.h>

static void alien_math_set_error(char *error, size_t error_size, const char *message)
{
    if (error && error_size > 0u) {
        (void)snprintf(error, error_size, "%s", message);
    }
}

static int32_t alien_math_asr32_1(int32_t value)
{
    if (value >= 0) {
        return value / 2;
    }
    return (int32_t)(((uint32_t)value >> 1u) | UINT32_C(0x80000000));
}

static int alien_math_divs16(int32_t dividend, int16_t divisor,
                              int16_t *out_quotient,
                              char *error, size_t error_size)
{
    int32_t quotient;

    if (!out_quotient || divisor == 0 ||
        (dividend == INT32_MIN && divisor == -1)) {
        alien_math_set_error(error, error_size,
                             "ai_CalcSqrt DIVS received invalid source operands");
        return 0;
    }
    quotient = dividend / divisor;
    if (quotient < INT16_MIN || quotient > INT16_MAX) {
        alien_math_set_error(error, error_size,
                             "ai_CalcSqrt DIVS quotient exceeds a source word");
        return 0;
    }
    *out_quotient = (int16_t)quotient;
    return 1;
}

int alien_math_calc_sqrt(int32_t source_value, int16_t *out_root,
                         char *error, size_t error_size)
{
    uint32_t source_bits;
    uint32_t estimate;
    int highest_bit;

    if (!out_root) {
        alien_math_set_error(error, error_size, "ai_CalcSqrt result pointer is null");
        return 0;
    }
    if (source_value == 0) {
        *out_root = 0;
        return 1;
    }

    source_bits = (uint32_t)source_value;
    for (highest_bit = 31; highest_bit >= 0; --highest_bit) {
        if ((source_bits & (UINT32_C(1) << (uint32_t)highest_bit)) != 0u) {
            break;
        }
    }
    /* source_value is nonzero, so the source DBRA loop always finds a bit. */
    estimate = UINT32_C(1) << (uint32_t)(highest_bit / 2);
    for (uint8_t iteration = 0u; iteration < 3u; ++iteration) {
        int16_t estimate_word = (int16_t)(uint16_t)estimate;
        int32_t correction = (int32_t)((uint32_t)((int32_t)estimate_word * estimate_word) -
                                       (uint32_t)source_value);
        int16_t quotient;

        correction = alien_math_asr32_1(correction);
        if (!alien_math_divs16(correction, estimate_word, &quotient, error, error_size)) {
            return 0;
        }
        /* SUB.W leaves d0's high word zero from the initial BSET result. */
        estimate = (uint16_t)((uint16_t)estimate_word - (uint16_t)quotient);
        if ((int32_t)estimate <= 0) {
            estimate = 1u;
        }
    }
    /* ai_CalcSqrt's final MOVE.W/EXT.L returns the signed low source word. */
    *out_root = (int16_t)(uint16_t)estimate;
    return 1;
}
