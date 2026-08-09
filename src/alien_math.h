#ifndef AB3D2_ALIEN_MATH_H
#define AB3D2_ALIEN_MATH_H

#include <stddef.h>
#include <stdint.h>

/* modules/ai.s:ai_CalcSqrt's three word-precision refinement steps. */
int alien_math_calc_sqrt(int32_t source_value, int16_t *out_root,
                         char *error, size_t error_size);

#endif
